// node --test server/chat/server.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import crypto from "node:crypto";
import net from "node:net";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createChatServer, KeysUnavailable, sanitizeText } from "./server.mjs";
import { DEFAULT_ALLOWED, loadBlocklist, makeJev, makeMatcher, normalizeWord, sweeten, SWEET_WORDS } from "./moderation.mjs";

const CLIENT_ID = "test-client.apps.example";
const ISSUER = "https://accounts.google.com";
const { publicKey, privateKey } = crypto.generateKeyPairSync("rsa", { modulusLength: 2048 });
const stranger = crypto.generateKeyPairSync("rsa", { modulusLength: 2048 });
const jwk = { ...publicKey.export({ format: "jwk" }), kid: "k1", alg: "RS256", use: "sig" };

function sign(claims, { key = privateKey, alg = "RS256", kid = "k1" } = {}) {
  const b64 = (o) => Buffer.from(JSON.stringify(o)).toString("base64url");
  const body = b64({ alg, kid, typ: "JWT" }) + "." + b64(claims);
  return body + "." + crypto.sign("RSA-SHA256", Buffer.from(body), key).toString("base64url");
}

function idToken(sub, extra = {}) {
  const now = Math.floor(Date.now() / 1000);
  return sign({ iss: ISSUER, aud: CLIENT_ID, sub, iat: now, exp: now + 3600, ...extra });
}

let server, base, dataDir;

function startServer(loginMax = 100000) {
  const s = createChatServer({
    clientIds: [CLIENT_ID], clientSecret: "desktop-secret", hashSecret: "secret", adminToken: "admin-token", dataDir,
    getKey: async (kid) => (kid === "k1" ? jwk : undefined), log: () => {}, loginMax,
  });
  return new Promise((resolve) => s.listen(0, "127.0.0.1", () => resolve(s)));
}

async function call(route, body, token, ip) {
  const headers = { "Content-Type": "application/json" };
  if (token) headers.Authorization = "Bearer " + token;
  if (ip) headers["X-Forwarded-For"] = ip;
  const r = await fetch(base + route, { method: "POST", headers, body: JSON.stringify(body ?? {}) });
  return { status: r.status, body: await r.json() };
}

let counter = 0;
async function login(nick = "Ana") {
  const r = await call("/v1/login", { id_token: idToken("acct-" + ++counter), nick });
  assert.equal(r.status, 200);
  return r.body;
}

before(async () => {
  dataDir = fs.mkdtempSync(path.join(os.tmpdir(), "karamelo-chat-"));
  server = await startServer();
  base = "http://127.0.0.1:" + server.address().port;
});

after(() => {
  server.close();
  server.closeAllConnections();
  fs.rmSync(dataDir, { recursive: true, force: true });
});

test("refuses tokens that are not really from Google for this app", async () => {
  const now = Math.floor(Date.now() / 1000);
  const good = { iss: ISSUER, aud: CLIENT_ID, sub: "x", iat: now, exp: now + 3600 };
  const bad = {
    "other key": sign(good, { key: stranger.privateKey }),
    "unknown key": sign(good, { kid: "nope" }),
    "other app": sign({ ...good, aud: "someone-else" }),
    "other issuer": sign({ ...good, iss: "https://evil.example" }),
    "expired": sign({ ...good, exp: now - 3600 }),
    "no account": sign({ ...good, sub: "" }),
    "garbage": "a.b.c",
    "not a string": 42,
  };
  const b64 = (o) => Buffer.from(JSON.stringify(o)).toString("base64url");
  bad["alg none"] = b64({ alg: "none", kid: "k1" }) + "." + b64(good) + ".";
  for (const [name, id_token] of Object.entries(bad)) {
    const r = await call("/v1/login", { id_token, nick: "x" });
    assert.equal(r.status, 401, name);
  }
});

test("messages reach the other player, in order and only once", async () => {
  const a = await login("Ana");
  const b = await login("Bia");
  const start = (await call("/v1/poll", { room: 1, since: 0 }, b.session)).body.seq;
  assert.equal((await call("/v1/send", { room: 1, text: "oi" }, a.session)).status, 200);
  await call("/v1/send", { room: 1, text: "tudo bem?" }, a.session);
  const first = (await call("/v1/poll", { room: 1, since: start }, b.session)).body;
  assert.deepEqual(first.messages.map((m) => m.text), ["oi", "tudo bem?"]);
  assert.equal(first.messages[0].sender, a.tag);
  assert.equal((await call("/v1/poll", { room: 1, since: first.seq }, b.session)).body.messages.length, 0);
  const other = await call("/v1/poll", { room: 2, since: 0 }, b.session);
  assert.ok(!other.body.messages.some((m) => m.text === "oi"), "rooms are separate");
});

test("the app gets what it needs to start a login, and nothing else", async () => {
  const r = await call("/v1/login-config");
  assert.equal(r.status, 200);
  assert.deepEqual(r.body, { client_id: CLIENT_ID, client_secret: "desktop-secret" });
});

test("everything but login needs a session", async () => {
  for (const route of ["/v1/poll", "/v1/send", "/v1/match", "/v1/match/join", "/v1/report", "/v1/nick"]) {
    assert.equal((await call(route, { room: 0, text: "x" })).status, 401, route);
    assert.equal((await call(route, { room: 0, text: "x" }, "made-up")).status, 401, route);
  }
});

test("nobody can pose as someone else", async () => {
  const a = await login("Ana");
  const b = await login("Ana");
  assert.notEqual(a.tag, b.tag, "same nickname, different accounts");
  await call("/v1/send", { room: 3, text: "falso", sender: b.tag }, a.session);
  const got = (await call("/v1/poll", { room: 3, since: 0 }, a.session)).body.messages.at(-1);
  assert.equal(got.sender, a.tag);
});

test("a name cannot imitate another player's tag", async () => {
  const a = await login("Bia#E13D51");
  assert.match(a.tag, /^BiaE13D51#[0-9A-F]{6}$/);
  const renamed = await call("/v1/nick", { nick: "Ana#FFFFFF" }, a.session);
  assert.match(renamed.body.tag, /^AnaFFFFFF#[0-9A-F]{6}$/);
  assert.equal((await call("/v1/nick", { nick: "###" }, a.session)).status, 400);
});

test("the account id never leaves the server", async () => {
  const sub = "very-secret-account-id";
  const r = await call("/v1/login", { id_token: idToken(sub), nick: "Eva" });
  await call("/v1/send", { room: 0, text: "oi" }, r.body.session);
  const poll = await call("/v1/poll", { room: 0, since: 0 }, r.body.session);
  const text = JSON.stringify([r.body, poll.body]);
  assert.ok(!text.includes(sub));
  assert.ok(!poll.body.messages.some((m) => "account" in m));
});

function relayConnect(port) {
  return new Promise((resolve, reject) => {
    const sock = net.connect(port, "127.0.0.1", () => resolve(sock));
    sock.on("error", reject);
  });
}

function firstData(sock) {
  return new Promise((resolve) => sock.once("data", (d) => resolve(d.toString())));
}

function closed(sock) {
  return new Promise((resolve) => {
    if (sock.destroyed) resolve();
    else sock.once("close", resolve);
  });
}

test("no response about a match says where anyone is", async () => {
  const host = await login("Host");
  const guest = await login("Guest");
  const made = await call("/v1/match", {}, host.session, "203.0.113.7");
  assert.equal(made.status, 200);
  assert.match(made.body.code, /^KRI-[2-9A-HJ-NP-Z]{5}-[2-9A-HJ-NP-Z]{5}$/);
  const joined = await call("/v1/match/join", { code: made.body.code }, guest.session, "203.0.113.8");
  assert.equal(joined.status, 200);
  const poll = await call("/v1/poll", { room: 0, since: 0 }, guest.session);
  const text = JSON.stringify([made.body, joined.body, poll.body]);
  assert.ok(!/\b\d{1,3}(\.\d{1,3}){3}\b/.test(text), "no address in any answer");
  assert.ok(!/"(ip|public_ip|host_ip|address)"/.test(text));
  assert.equal((await call("/v1/invite", { room: 0, game: "x" }, host.session)).status, 404, "room invites are gone");
});

test("a match code works once, and not for its own host", async () => {
  const host = await login("Host");
  const made = await call("/v1/match", {}, host.session);
  assert.equal((await call("/v1/match/join", { code: made.body.code }, host.session)).status, 400);
  const guest = await login("Guest");
  assert.equal((await call("/v1/match/join", { code: "KRI-22222-22222" }, guest.session)).status, 404);
  assert.equal((await call("/v1/match/join", { code: made.body.code.toLowerCase() }, guest.session)).status, 200);
  const late = await login("Late");
  assert.equal((await call("/v1/match/join", { code: made.body.code }, late.session)).status, 404);
});

test("the bridge joins exactly the two players of a match", async () => {
  const relay = createChatServer({
    clientIds: [CLIENT_ID], hashSecret: "secret", dataDir,
    getKey: async (kid) => (kid === "k1" ? jwk : undefined), log: () => {}, loginMax: 100000,
  });
  await new Promise((r) => relay.listen(0, "127.0.0.1", r));
  await new Promise((r) => relay.relay.listen(0, "127.0.0.1", r));
  const oldBase = base;
  base = "http://127.0.0.1:" + relay.address().port;
  try {
    const host = await login("Host");
    const guest = await login("Guest");
    const made = (await call("/v1/match", {}, host.session)).body;
    const joined = (await call("/v1/match/join", { code: made.code }, guest.session)).body;
    assert.equal(made.relay_port, relay.relay.address().port);

    // A made-up ticket gets nothing.
    const stranger = await relayConnect(made.relay_port);
    stranger.write("KRMB1 " + "0".repeat(48) + "\n");
    await closed(stranger);

    const a = await relayConnect(made.relay_port);
    const aReady = firstData(a);
    a.write("KRMB1 " + made.ticket + "\n");
    const b = await relayConnect(made.relay_port);
    const bReady = firstData(b);
    b.write("KRMB1 " + joined.ticket + "\n");
    assert.equal(await aReady, "!");
    assert.equal(await bReady, "!");

    const toB = firstData(b);
    a.write("from host");
    assert.equal(await toB, "from host");
    const toA = firstData(a);
    b.write("from guest");
    assert.equal(await toA, "from guest");

    // A ticket opens one connection: copying it does not get a third party in.
    const again = await relayConnect(made.relay_port);
    again.write("KRMB1 " + made.ticket + "\n");
    await closed(again);

    // One leaving ends it for the other.
    const bGone = closed(b);
    a.destroy();
    await bGone;
  } finally {
    base = oldBase;
    relay.close();
    relay.closeAllConnections();
  }
});

test("a flood is cut and text is cleaned", async () => {
  const a = await login("Flood");
  const results = [];
  for (let i = 0; i < 8; i++) results.push((await call("/v1/send", { room: 0, text: "m" + i }, a.session)).status);
  assert.deepEqual(results, [200, 200, 200, 200, 200, 429, 429, 429]);
  assert.equal(sanitizeText("a‮b\nc\u0000d  e", 200), "a b c d e");
  assert.equal(sanitizeText("x".repeat(500), 200).length, 200);
  assert.equal(sanitizeText(7, 200), "");
});

test("login attempts from one address are limited", async () => {
  const strict = await startServer(3);
  const oldBase = base;
  base = "http://127.0.0.1:" + strict.address().port;
  try {
    const results = [];
    for (let i = 0; i < 5; i++) results.push((await call("/v1/login", { id_token: "a.b.c" })).status);
    assert.deepEqual(results, [401, 401, 401, 429, 429]);
  } finally {
    base = oldBase;
    strict.close();
    strict.closeAllConnections();
  }
});

test("a session cannot be altered, and leaving ends it", async () => {
  const a = await login("Ana");
  const b = await login("Bia");
  const [payload, signature] = a.session.split(".");
  const data = JSON.parse(Buffer.from(payload, "base64url").toString());
  const forged = Buffer.from(JSON.stringify({ ...data, n: "Bia" })).toString("base64url") + "." + signature;
  assert.equal((await call("/v1/poll", { room: 0 }, forged)).status, 401, "changed name");
  const longer = Buffer.from(JSON.stringify({ ...data, e: Date.now() * 2 })).toString("base64url") + "." + signature;
  assert.equal((await call("/v1/poll", { room: 0 }, longer)).status, 401, "longer life");

  const renamed = await call("/v1/nick", { nick: "Ana2" }, a.session);
  assert.ok(renamed.body.tag.startsWith("Ana2#"));
  assert.equal(renamed.body.tag.split("#")[1], a.tag.split("#")[1], "same account, same number");
  assert.equal((await call("/v1/poll", { room: 0 }, renamed.body.session)).body.tag, renamed.body.tag);

  assert.equal((await call("/v1/logout", {}, b.session)).status, 200);
  assert.equal((await call("/v1/poll", { room: 0 }, b.session)).status, 401);
});

test("not reaching Google is a try-later, not a refused login", async () => {
  const cut = createChatServer({
    clientIds: [CLIENT_ID], hashSecret: "secret", dataDir,
    getKey: async () => { throw new KeysUnavailable(); }, log: () => {},
  });
  await new Promise((resolve) => cut.listen(0, "127.0.0.1", resolve));
  const oldBase = base;
  base = "http://127.0.0.1:" + cut.address().port;
  try {
    assert.equal((await call("/v1/login", { id_token: idToken("anyone") })).status, 503);
  } finally {
    base = oldBase;
    cut.close();
    cut.closeAllConnections();
  }
});

const realMatcher = makeMatcher(loadBlocklist(), DEFAULT_ALLOWED);
const swap = (text, opts) => sweeten(text, realMatcher, { pick: () => "DOCE", ...opts }).text;

test("list words are swapped for something sweet, however they are written", () => {
  assert.ok(loadBlocklist().length > 400, "both languages loaded");
  assert.equal(swap("que merda de jogo"), "que DOCE de jogo");
  assert.equal(swap("MERDA"), "DOCE");
  assert.equal(swap("que m3rd4 mano"), "que DOCE mano");
  assert.equal(swap("p u t a"), "DOCE");
  assert.equal(swap("merdaaaa"), "DOCE");
  assert.equal(swap("what the fuck"), "what the DOCE");
  assert.equal(swap("vai tomar no cu"), "DOCE", "a phrase is one swap");
  assert.equal(swap("puta que pariu"), "DOCE");
  // Random, and from the list.
  const picked = new Set();
  for (let i = 0; i < 200; i++) picked.add(sweeten("merda", realMatcher).text);
  assert.ok(picked.size > 5);
  for (const word of picked) assert.ok(SWEET_WORDS.includes(word));
});

test("normal words stay as they are", () => {
  for (const text of ["bora comer uma pizza", "esse jogo é um inferno de difícil", "cerveja gelada",
    "a aranha do castlevania", "nem a pau", "class assassin bass", "computador", "ação é ótima"]) {
    assert.equal(swap(text), text);
  }
  assert.equal(normalizeWord("AÇÃO"), "acao");
});

test("links are swapped too, unless turned off", () => {
  assert.equal(swap("olha https://site-ruim.com/x aqui"), "olha DOCE aqui");
  assert.equal(swap("entra em discord.gg/abc agora"), "entra em DOCE agora");
  assert.equal(swap("www.golpe.net"), "DOCE");
  assert.equal(swap("olha https://a.com", { links: false }), "olha https://a.com");
  assert.equal(swap("versao 1.2.3 ok"), "versao 1.2.3 ok");
});

async function withServer(options, run) {
  const s2 = createChatServer({
    clientIds: [CLIENT_ID], hashSecret: "secret", adminToken: "admin-token",
    dataDir: fs.mkdtempSync(path.join(os.tmpdir(), "karamelo-mod-")),
    getKey: async (kid) => (kid === "k1" ? jwk : undefined), log: () => {}, loginMax: 100000, ...options,
  });
  await new Promise((r) => s2.listen(0, "127.0.0.1", r));
  const oldBase = base;
  base = "http://127.0.0.1:" + s2.address().port;
  try {
    await run();
  } finally {
    base = oldBase;
    s2.close();
    s2.closeAllConnections();
  }
}

const lastInRoom = async (room, session) => (await call("/v1/poll", { room, since: 0 }, session)).body.messages.at(-1);

test("Jev stops the grave ones; list words are swapped even when it approves", async () => {
  await withServer({ jevCall: async (text) => ({ status: 200, risk: text.includes("matar") ? 0.97 : 0.02 }) }, async () => {
    const a = await login("Ana");
    const blocked = await call("/v1/send", { room: 0, text: "vou te matar" }, a.session);
    assert.equal(blocked.status, 422);
    const bad = await call("/v1/send", { room: 0, text: "que merda" }, a.session);
    assert.equal(bad.status, 200);
    assert.ok(!bad.body.text.includes("merda"), "the sender learns what went out");
    const seen = await lastInRoom(0, a.session);
    assert.equal(seen.text, bad.body.text);
    assert.equal((await call("/v1/send", { room: 0, text: "bom jogo" }, a.session)).body.text, undefined);
  });
});

test("with Jev down, out of credit or unconfigured, the chat goes on with the list", async () => {
  const outcomes = [
    async () => { throw new Error("network"); },
    async () => ({ status: 500 }),
    async () => ({ status: 402 }),
    async () => ({ status: 200 }), // an answer without a verdict
    undefined,
  ];
  for (const jevCall of outcomes) {
    await withServer({ jevCall }, async () => {
      const a = await login("Ana");
      const r = await call("/v1/send", { room: 0, text: "que merda" }, a.session);
      assert.equal(r.status, 200);
      assert.ok(!r.body.text.includes("merda"));
    });
  }
});

test("a dead service stops being asked, so the chat does not wait for it", async () => {
  let t = 1000;
  let calls = 0;
  const unpaid = makeJev({ call: async () => { calls++; return { status: 402 }; }, now: () => t });
  assert.equal(await unpaid.risk("x"), null);
  assert.equal(await unpaid.risk("x"), null);
  assert.equal(calls, 1, "a refused account pauses at once");
  assert.equal(unpaid.stats.last_error, "HTTP 402");
  t += 11 * 60 * 1000;
  await unpaid.risk("x");
  assert.equal(calls, 2, "and it tries again later");

  calls = 0;
  const down = makeJev({ call: async () => { calls++; throw new Error("x"); }, now: () => t });
  for (let i = 0; i < 8; i++) await down.risk("x");
  assert.equal(calls, 5, "five failures pause it");
});

test("the panel controls it all, and only with the token", async () => {
  await withServer({ jevCall: async () => ({ status: 200, risk: 0.5 }) }, async () => {
    const a = await login("Ana");
    assert.equal((await fetch(base + "/v1/admin/status")).status, 404);
    const status = async () => (await fetch(base + "/v1/admin/status", { headers: { Authorization: "Bearer admin-token" } })).json();
    const before = await status();
    assert.equal(before.jev.configured, true);
    assert.ok(!JSON.stringify(before).includes("admin-token"));
    assert.ok(!/\b\d{1,3}(\.\d{1,3}){3}\b/.test(JSON.stringify(before)), "no addresses");

    const set = (body) => call("/v1/admin/settings", body, "admin-token");
    assert.equal((await call("/v1/admin/settings", { chat_enabled: false }, a.session)).status, 404);

    await set({ extra_words: ["karamelada"] });
    assert.ok(!(await call("/v1/send", { room: 1, text: "karamelada" }, a.session)).body.text.includes("karamelada"));
    await set({ allowed_words: ["merda"] });
    assert.equal((await call("/v1/send", { room: 1, text: "merda" }, a.session)).body.text, undefined);
    await set({ allowed_words: [], words_enabled: false });
    assert.equal((await call("/v1/send", { room: 1, text: "merda" }, a.session)).body.text, undefined);
    await set({ words_enabled: true, jev_block_at: 0.4 });
    assert.equal((await call("/v1/send", { room: 1, text: "algo" }, a.session)).status, 422);
    await set({ jev_enabled: false });
    assert.equal((await call("/v1/send", { room: 1, text: "algo" }, a.session)).status, 200);
    await set({ chat_enabled: false, matches_enabled: false });
    assert.equal((await call("/v1/send", { room: 1, text: "algo" }, a.session)).status, 503);
    assert.equal((await call("/v1/match", {}, a.session)).status, 503);
    await set({ chat_enabled: true });

    // A ban can be undone from the panel.
    await call("/v1/send", { room: 2, text: "oi" }, a.session);
    await call("/v1/admin/ban", { tag: a.tag }, "admin-token");
    const banned = (await status()).bans.find((b) => b.tag === a.tag);
    assert.ok(banned);
    assert.equal((await call("/v1/poll", { room: 0 }, a.session)).status, 401);
    assert.equal((await call("/v1/admin/unban", { id: banned.id }, "admin-token")).status, 200);
    assert.equal((await call("/v1/poll", { room: 0 }, a.session)).status, 200);
  });
});

test("too much data in one request is refused", async () => {
  const a = await login("Big");
  let refused = false;
  try {
    const r = await call("/v1/send", { room: 0, text: "x".repeat(20000) }, a.session);
    refused = r.status !== 200;
  } catch {
    refused = true; // connection cut mid-upload
  }
  assert.ok(refused);
});

test("report and ban: the account is out, even after a restart", async () => {
  const bad = await login("Troll");
  const judge = await login("Juiz");
  const sub = "banned-account";
  const r = await call("/v1/login", { id_token: idToken(sub), nick: "Troll2" });
  await call("/v1/send", { room: 1, text: "ofensa" }, r.body.session);
  assert.equal((await call("/v1/report", { room: 1, tag: r.body.tag }, judge.session)).status, 200);
  assert.equal((await call("/v1/report", { room: 1, tag: "Ninguem#000000" }, judge.session)).status, 404);

  assert.equal((await fetch(base + "/v1/admin/reports")).status, 404, "admin hidden without token");
  const adminGet = await fetch(base + "/v1/admin/reports", { headers: { Authorization: "Bearer admin-token" } });
  const reports = (await adminGet.json()).reports;
  const rep = reports.find((x) => x.tag === r.body.tag);
  assert.equal(rep.evidence[0].text, "ofensa");
  assert.ok(!("account" in rep));

  assert.equal((await call("/v1/admin/ban", { tag: r.body.tag }, bad.session)).status, 404, "players cannot ban");
  assert.equal((await call("/v1/admin/ban", { tag: r.body.tag }, "admin-token")).status, 200);
  assert.equal((await call("/v1/poll", { room: 1, since: 0 }, r.body.session)).status, 401, "session dropped");
  assert.equal((await call("/v1/login", { id_token: idToken(sub), nick: "Volta" })).status, 403);
  const poll = await call("/v1/poll", { room: 1, since: 0 }, judge.session);
  assert.ok(!poll.body.messages.some((m) => m.text === "ofensa"), "messages removed");

  const bansFile = fs.readFileSync(path.join(dataDir, "bans.json"), "utf8");
  assert.ok(!bansFile.includes(sub), "the file has only a keyed hash");
  assert.ok(!bansFile.includes("ofensa"), "and no text");

  const reborn = await startServer();
  const oldBase = base;
  base = "http://127.0.0.1:" + reborn.address().port;
  try {
    assert.equal((await call("/v1/login", { id_token: idToken(sub), nick: "Volta" })).status, 403);
    const history = await call("/v1/poll", { room: 1, since: 0 }, judge.session);
    assert.equal(history.status, 200, "a restart signs nobody out");
    assert.equal(history.body.messages.length, 0, "messages do not survive a restart");
    assert.equal(history.body.tag, judge.tag);
  } finally {
    base = oldBase;
    reborn.close();
    reborn.closeAllConnections();
  }
});
