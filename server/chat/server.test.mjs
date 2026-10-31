// node --test server/chat/server.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createChatServer, KeysUnavailable, sanitizeText } from "./server.mjs";

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
  for (const route of ["/v1/poll", "/v1/send", "/v1/invite", "/v1/invite/accept", "/v1/report", "/v1/nick"]) {
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

test("the account id never leaves the server", async () => {
  const sub = "very-secret-account-id";
  const r = await call("/v1/login", { id_token: idToken(sub), nick: "Eva" });
  await call("/v1/send", { room: 0, text: "oi" }, r.body.session);
  const poll = await call("/v1/poll", { room: 0, since: 0 }, r.body.session);
  const text = JSON.stringify([r.body, poll.body]);
  assert.ok(!text.includes(sub));
  assert.ok(!poll.body.messages.some((m) => "account" in m));
});

test("an invite hides the address until someone accepts it", async () => {
  const host = await login("Host");
  const guest = await login("Guest");
  const made = await call("/v1/invite", { room: 2, game: "Sonic", port: 55435 }, host.session);
  assert.equal(made.status, 200);
  const poll = (await call("/v1/poll", { room: 2, since: 0 }, guest.session)).body;
  const inv = poll.messages.find((m) => m.invite === made.body.invite);
  assert.equal(inv.game, "Sonic");
  assert.ok(!JSON.stringify(poll).includes(made.body.public_ip), "address not in the room");
  const accepted = await call("/v1/invite/accept", { invite: inv.invite }, guest.session);
  assert.equal(accepted.status, 200);
  assert.equal(accepted.body.ip, made.body.public_ip);
  assert.equal(accepted.body.port, 55435);
  assert.equal((await call("/v1/invite/accept", { invite: inv.invite }, host.session)).status, 400, "own invite");
  assert.equal((await call("/v1/invite/accept", { invite: "nope" }, guest.session)).status, 404);
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
