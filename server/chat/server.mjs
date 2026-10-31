// Karamelo Live - internet chat rooms.
//
// What it keeps: the last messages of each room and who is online, in memory
// only, gone on restart. What it writes to disk: the ban list, as keyed hashes of
// the Google account id. A login is a signed token held by the player, so a
// restart does not sign anyone out. Google's token is read for its anonymous id;
// the login never asks for a name or an e-mail, and the player's address is only
// handed over when someone accepts that player's invite.
//
// No dependencies: Node 20 or newer. TLS is done by the proxy in front of it.
//
//   CHAT_GOOGLE_CLIENT_IDS  comma-separated OAuth client ids accepted (required)
//   CHAT_HASH_SECRET        key for tags and the ban list (required, keep it stable)
//   CHAT_ADMIN_TOKEN        enables /v1/admin/*
//   CHAT_DATA_DIR           where bans.json lives (default ./data)
//   CHAT_TRUST_PROXY        "1" behind a proxy: read the client from X-Forwarded-For
//   CHAT_JWKS_URL, CHAT_ISSUER  only the tests change these
//   PORT                    default 8787

import http from "node:http";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const ROOM_COUNT = 4;
const MAX_ROOM_MESSAGES = 100;
const MAX_TEXT = 200;
const MAX_NICK = 24;
const MAX_GAME = 64;
const MAX_BODY = 8192;
const MAX_ONLINE_LISTED = 100;
const MAX_ONLINE = 20000;
const SESSION_MS = 30 * 24 * 3600 * 1000;
const ONLINE_MS = 30 * 1000;
const INVITE_MS = 10 * 60 * 1000;
const SEND_WINDOW_MS = 10 * 1000;
const SEND_MAX = 5;
const LOGIN_WINDOW_MS = 60 * 1000;
const LOGIN_MAX = 10;
const REQUEST_WINDOW_MS = 10 * 1000;
const REQUEST_MAX = 500; // per address, and a whole LAN house can share one
const MAX_REPORTS = 500;
const JWKS_CACHE_MS = 3600 * 1000;

export function sanitizeText(raw, maxLen) {
  if (typeof raw !== "string") return "";
  // Control, bidi and zero-width characters: they fake other people's lines.
  const clean = raw
    .replace(/[\u0000-\u001f\u007f-\u009f​-‏‪-‮⁠-⁩﻿]/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  return Array.from(clean).slice(0, maxLen).join("");
}

function b64urlJson(part) {
  return JSON.parse(Buffer.from(part, "base64url").toString("utf8"));
}

// Checks signature, audience, issuer and dates. Returns the account id or throws.
export async function verifyGoogleIdToken(token, { clientIds, issuers, getKey, now = Date.now() }) {
  if (typeof token !== "string" || token.length > 4096) throw new Error("bad token");
  const parts = token.split(".");
  if (parts.length !== 3) throw new Error("bad token");
  const header = b64urlJson(parts[0]);
  const claims = b64urlJson(parts[1]);
  // Fixed to RS256: taking the algorithm from the token is the classic forgery.
  if (header.alg !== "RS256" || typeof header.kid !== "string") throw new Error("bad algorithm");
  const jwk = await getKey(header.kid);
  if (!jwk) throw new Error("unknown key");
  const key = crypto.createPublicKey({ key: jwk, format: "jwk" });
  const good = crypto.verify("RSA-SHA256", Buffer.from(parts[0] + "." + parts[1]), key,
    Buffer.from(parts[2], "base64url"));
  if (!good) throw new Error("bad signature");
  if (!clientIds.includes(claims.aud)) throw new Error("bad audience");
  if (!issuers.includes(claims.iss)) throw new Error("bad issuer");
  const seconds = now / 1000;
  if (typeof claims.exp !== "number" || claims.exp < seconds - 60) throw new Error("expired");
  if (typeof claims.iat === "number" && claims.iat > seconds + 300) throw new Error("from the future");
  if (typeof claims.sub !== "string" || !claims.sub) throw new Error("no account");
  return claims.sub;
}

export class KeysUnavailable extends Error {
  constructor() {
    super("google keys unavailable");
  }
}

function makeGoogleKeyFetcher(url) {
  let keys = new Map();
  let fetchedAt = 0;
  let lastTry = 0;
  return async (kid) => {
    const t = Date.now();
    const stale = t - fetchedAt > JWKS_CACHE_MS;
    // A token with an invented kid must not make us hit Google on every request.
    if ((stale || !keys.has(kid)) && t - lastTry > 30 * 1000) {
      lastTry = t;
      try {
        const resp = await fetch(url, { signal: AbortSignal.timeout(5000) });
        if (resp.ok) {
          const data = await resp.json();
          keys = new Map((data.keys || []).map((k) => [k.kid, k]));
          fetchedAt = t;
        }
      } catch (e) {
        console.log(`google keys: ${e.message}`);
      }
    }
    // Not knowing the keys says nothing about the token: the player did nothing wrong.
    if (keys.size === 0) throw new KeysUnavailable();
    return keys.get(kid);
  };
}

function overLimit(map, key, windowMs, max, now) {
  let w = map.get(key);
  if (!w || now - w.start >= windowMs) {
    w = { start: now, count: 0 };
    map.set(key, w);
  }
  w.count++;
  return w.count > max;
}

export function createChatServer(config) {
  const {
    clientIds,
    hashSecret,
    adminToken = "",
    dataDir = "data",
    trustProxy = false,
    jwksUrl = "https://www.googleapis.com/oauth2/v3/certs",
    issuers = ["https://accounts.google.com", "accounts.google.com"],
    getKey = makeGoogleKeyFetcher(jwksUrl),
    log = (line) => console.log(line),
    loginMax = LOGIN_MAX,
  } = config;
  if (!clientIds || clientIds.length === 0) throw new Error("CHAT_GOOGLE_CLIENT_IDS is required");
  if (!hashSecret) throw new Error("CHAT_HASH_SECRET is required");

  const hmac = (text) => crypto.createHmac("sha256", hashSecret).update(text).digest("hex");
  const rooms = Array.from({ length: ROOM_COUNT }, () => ({ seq: 0, messages: [] }));
  const sessions = new Map();  // account -> who is around now; rebuilt from tokens
  const revoked = new Map();   // signed-out token hash -> when it expires
  const invites = new Map();   // invite id -> { ip, port, tag, game, at, room }
  const reports = [];
  const sendLimits = new Map();
  const loginLimits = new Map();
  const requestLimits = new Map();
  const bansPath = path.join(dataDir, "bans.json");
  let bans = new Set();
  try {
    bans = new Set(JSON.parse(fs.readFileSync(bansPath, "utf8")));
  } catch { /* no bans yet */ }

  function saveBans() {
    fs.mkdirSync(dataDir, { recursive: true });
    const tmp = bansPath + ".tmp";
    fs.writeFileSync(tmp, JSON.stringify([...bans]));
    fs.renameSync(tmp, bansPath);
  }

  function makeSessionToken(account, nick, expires) {
    const payload = Buffer.from(JSON.stringify({ a: account, n: nick, e: expires })).toString("base64url");
    return payload + "." + hmac("token:" + payload);
  }

  function readSessionToken(token) {
    const dot = token.indexOf(".");
    if (dot <= 0 || !safeEqual(token.slice(dot + 1), hmac("token:" + token.slice(0, dot)))) return null;
    let data;
    try {
      data = JSON.parse(Buffer.from(token.slice(0, dot), "base64url").toString("utf8"));
    } catch {
      return null;
    }
    if (Date.now() > data.e || bans.has(data.a) || revoked.has(hmac("revoked:" + token))) return null;
    return data;
  }

  function clientIp(req) {
    if (trustProxy) {
      // The proxy appends what it saw: the last entry is the only one it vouches for.
      const parts = String(req.headers["x-forwarded-for"] || "").split(",");
      const last = parts[parts.length - 1].trim();
      if (last) return last.replace(/^::ffff:/, "");
    }
    return String(req.socket.remoteAddress || "").replace(/^::ffff:/, "");
  }

  function tagFor(account, nick) {
    return nick + "#" + hmac("tag:" + account).slice(0, 6).toUpperCase();
  }

  function publicMessage(m) {
    return { seq: m.seq, sender: m.sender, text: m.text, at: m.at, game: m.game, invite: m.invite };
  }

  function appendMessage(roomIdx, fields) {
    const room = rooms[roomIdx];
    const msg = { seq: ++room.seq, at: Math.floor(Date.now() / 1000), game: "", invite: "", ...fields };
    room.messages.push(msg);
    if (room.messages.length > MAX_ROOM_MESSAGES) room.messages.shift();
    return msg;
  }

  function sweep() {
    const t = Date.now();
    for (const [key, s] of sessions) if (t - s.seen > ONLINE_MS * 10) sessions.delete(key);
    for (const [key, expires] of revoked) if (t > expires) revoked.delete(key);
    for (const [id, inv] of invites) if (t - inv.at > INVITE_MS) invites.delete(id);
    for (const map of [sendLimits, loginLimits, requestLimits])
      for (const [key, w] of map) if (t - w.start > 120 * 1000) map.delete(key);
  }
  const sweeper = setInterval(sweep, 60 * 1000);
  sweeper.unref();

  function onlineList(now) {
    const out = [];
    for (const s of sessions.values()) {
      if (now - s.seen > ONLINE_MS) continue;
      if (out.some((p) => p.tag === s.tag)) continue;
      out.push({ tag: s.tag, game: s.game, status: s.status });
      if (out.length >= MAX_ONLINE_LISTED) break;
    }
    return out;
  }

  const routes = {
    async "POST /v1/login"(req, body, ip) {
      if (overLimit(loginLimits, ip, LOGIN_WINDOW_MS, loginMax, Date.now()))
        return [429, { error: "too_many" }];
      let sub;
      try {
        sub = await verifyGoogleIdToken(body.id_token, { clientIds, issuers, getKey });
      } catch (e) {
        log(`login refused: ${e.message}`); // the reason only, never the token
        if (e instanceof KeysUnavailable) return [503, { error: "try_later" }];
        return [401, { error: "bad_login" }];
      }
      const account = hmac("account:" + sub);
      if (bans.has(account)) return [403, { error: "banned" }];
      const nick = sanitizeText(body.nick, MAX_NICK) || "Jogador";
      return [200, {
        session: makeSessionToken(account, nick, Date.now() + SESSION_MS),
        tag: tagFor(account, nick),
        expires_in: Math.floor(SESSION_MS / 1000),
      }];
    },

    async "POST /v1/logout"(req, body, ip, session, bearer, expires) {
      revoked.set(hmac("revoked:" + bearer), expires);
      sessions.delete(session.account);
      return [200, { ok: true }];
    },

    // The name lives in the token, so a new name is a new token.
    async "POST /v1/nick"(req, body, ip, session, bearer, expires) {
      const nick = sanitizeText(body.nick, MAX_NICK);
      if (!nick) return [400, { error: "bad_nick" }];
      session.tag = tagFor(session.account, nick);
      return [200, { tag: session.tag, session: makeSessionToken(session.account, nick, expires) }];
    },

    // Called every couple of seconds: new messages and who is online.
    async "POST /v1/poll"(req, body, ip, session) {
      const roomIdx = body.room;
      if (!Number.isInteger(roomIdx) || roomIdx < 0 || roomIdx >= ROOM_COUNT)
        return [400, { error: "bad_room" }];
      session.game = sanitizeText(body.game, MAX_GAME);
      if (Number.isInteger(body.status) && body.status >= 1 && body.status <= 3) session.status = body.status;
      const room = rooms[roomIdx];
      const since = Number.isInteger(body.since) && body.since >= 0 ? body.since : 0;
      const messages = room.messages.filter((m) => m.seq > since).map(publicMessage);
      return [200, { room: roomIdx, seq: room.seq, messages, online: onlineList(Date.now()), tag: session.tag }];
    },

    async "POST /v1/send"(req, body, ip, session) {
      const roomIdx = body.room;
      if (!Number.isInteger(roomIdx) || roomIdx < 0 || roomIdx >= ROOM_COUNT)
        return [400, { error: "bad_room" }];
      if (overLimit(sendLimits, session.account, SEND_WINDOW_MS, SEND_MAX, Date.now()))
        return [429, { error: "too_many" }];
      const text = sanitizeText(body.text, MAX_TEXT);
      if (!text) return [400, { error: "empty" }];
      const msg = appendMessage(roomIdx, { sender: session.tag, account: session.account, text });
      return [200, { seq: msg.seq }];
    },

    // The address stays here until someone accepts: a room full of strangers must
    // not get the IP of everyone who invites.
    async "POST /v1/invite"(req, body, ip, session) {
      const roomIdx = body.room;
      if (!Number.isInteger(roomIdx) || roomIdx < 0 || roomIdx >= ROOM_COUNT)
        return [400, { error: "bad_room" }];
      if (overLimit(sendLimits, session.account, SEND_WINDOW_MS, SEND_MAX, Date.now()))
        return [429, { error: "too_many" }];
      const game = sanitizeText(body.game, MAX_GAME);
      if (!game) return [400, { error: "empty" }];
      const port = Number.isInteger(body.port) && body.port > 0 && body.port <= 65535 ? body.port : 55435;
      const id = crypto.randomBytes(12).toString("hex");
      invites.set(id, { ip, port, tag: session.tag, account: session.account, game, at: Date.now() });
      const msg = appendMessage(roomIdx, { sender: session.tag, account: session.account, text: game, game, invite: id });
      return [200, { seq: msg.seq, invite: id, public_ip: ip }];
    },

    async "POST /v1/invite/accept"(req, body, ip, session) {
      const inv = invites.get(body.invite);
      if (!inv) return [404, { error: "gone" }];
      if (inv.account === session.account) return [400, { error: "own_invite" }];
      return [200, { ip: inv.ip, port: inv.port, tag: inv.tag, game: inv.game }];
    },

    async "POST /v1/report"(req, body, ip, session) {
      const roomIdx = body.room;
      const tag = sanitizeText(body.tag, 40);
      if (!tag || !Number.isInteger(roomIdx) || roomIdx < 0 || roomIdx >= ROOM_COUNT)
        return [400, { error: "bad_report" }];
      if (overLimit(sendLimits, session.account, SEND_WINDOW_MS, SEND_MAX, Date.now()))
        return [429, { error: "too_many" }];
      // What the admin needs to judge it, since the messages leave memory soon.
      const evidence = rooms[roomIdx].messages.filter((m) => m.sender === tag).slice(-20)
        .map((m) => ({ text: m.text, at: m.at }));
      const target = rooms[roomIdx].messages.findLast((m) => m.sender === tag);
      if (!target) return [404, { error: "not_found" }];
      reports.push({ tag, account: target.account, by: session.tag, room: roomIdx, at: Math.floor(Date.now() / 1000), evidence });
      if (reports.length > MAX_REPORTS) reports.shift();
      log(`report: ${tag} by ${session.tag}`);
      return [200, { ok: true }];
    },
  };

  const adminRoutes = {
    async "GET /v1/admin/reports"() {
      return [200, { reports: reports.map(({ account, ...r }) => r) }];
    },
    // Bans the account behind a tag seen in the rooms or in a report.
    async "POST /v1/admin/ban"(req, body) {
      const tag = String(body.tag || "");
      let account = "";
      for (const s of sessions.values()) if (s.tag === tag) account = s.account;
      if (!account) {
        for (const r of rooms) {
          const m = r.messages.findLast((x) => x.sender === tag);
          if (m) account = m.account;
        }
      }
      if (!account) {
        const rep = reports.findLast((r) => r.tag === tag);
        if (rep) account = rep.account;
      }
      if (!account) return [404, { error: "not_found" }];
      bans.add(account);
      saveBans();
      sessions.delete(account);
      for (const r of rooms) r.messages = r.messages.filter((m) => m.account !== account);
      return [200, { ok: true }];
    },
  };

  function safeEqual(a, b) {
    const x = Buffer.from(hmac("cmp:" + a));
    const y = Buffer.from(hmac("cmp:" + b));
    return crypto.timingSafeEqual(x, y);
  }

  function readBody(req) {
    return new Promise((resolve, reject) => {
      let size = 0;
      const chunks = [];
      req.on("data", (c) => {
        size += c.length;
        if (size > MAX_BODY) {
          reject(new Error("too large"));
          req.destroy();
          return;
        }
        chunks.push(c);
      });
      req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
      req.on("error", reject);
    });
  }

  async function handle(req) {
    const url = new URL(req.url, "http://x");
    const route = `${req.method} ${url.pathname}`;
    if (route === "GET /healthz") return [200, { ok: true }];
    const ip = clientIp(req);
    if (overLimit(requestLimits, ip, REQUEST_WINDOW_MS, REQUEST_MAX, Date.now()))
      return [429, { error: "too_many" }];

    const auth = String(req.headers.authorization || "");
    const bearer = auth.startsWith("Bearer ") ? auth.slice(7) : "";
    let body = {};
    if (req.method === "POST") {
      try {
        const raw = await readBody(req);
        body = raw ? JSON.parse(raw) : {};
      } catch {
        return [400, { error: "bad_body" }];
      }
      if (typeof body !== "object" || body === null || Array.isArray(body)) return [400, { error: "bad_body" }];
    }

    const admin = adminRoutes[route];
    if (admin) {
      if (!adminToken || !bearer || !safeEqual(bearer, adminToken)) return [404, { error: "not_found" }];
      return admin(req, body);
    }
    const fn = routes[route];
    if (!fn) return [404, { error: "not_found" }];
    if (route === "POST /v1/login") return fn(req, body, ip);

    const token = bearer ? readSessionToken(bearer) : null;
    if (!token) return [401, { error: "no_session" }];
    let session = sessions.get(token.a);
    if (!session) {
      if (sessions.size >= MAX_ONLINE) sweep();
      if (sessions.size >= MAX_ONLINE) return [503, { error: "full" }];
      session = { account: token.a, tag: tagFor(token.a, token.n), game: "", status: 1, seen: 0 };
      sessions.set(token.a, session);
    }
    session.tag = tagFor(token.a, token.n);
    session.seen = Date.now();
    return fn(req, body, ip, session, bearer, token.e);
  }

  const server = http.createServer(async (req, res) => {
    let status = 500;
    let payload = { error: "server" };
    try {
      [status, payload] = await handle(req);
    } catch (e) {
      log(`error: ${e.message}`);
    }
    if (res.destroyed) return;
    const text = JSON.stringify(payload);
    res.writeHead(status, {
      "Content-Type": "application/json; charset=utf-8",
      "Content-Length": Buffer.byteLength(text),
      "Cache-Control": "no-store",
    });
    res.end(text);
  });
  server.requestTimeout = 15000;
  server.headersTimeout = 10000;
  return server;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const port = Number(process.env.PORT || 8787);
  const server = createChatServer({
    clientIds: String(process.env.CHAT_GOOGLE_CLIENT_IDS || "").split(",").map((s) => s.trim()).filter(Boolean),
    hashSecret: process.env.CHAT_HASH_SECRET || "",
    adminToken: process.env.CHAT_ADMIN_TOKEN || "",
    dataDir: process.env.CHAT_DATA_DIR || "data",
    trustProxy: process.env.CHAT_TRUST_PROXY === "1",
    jwksUrl: process.env.CHAT_JWKS_URL || undefined,
    issuers: process.env.CHAT_ISSUER ? [process.env.CHAT_ISSUER] : undefined,
  });
  server.listen(port, () => console.log(`Karamelo chat on port ${port}`));
  for (const sig of ["SIGTERM", "SIGINT"]) process.on(sig, () => server.close(() => process.exit(0)));
}
