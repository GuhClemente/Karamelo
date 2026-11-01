// test_chat_internet.mjs - the internet rooms for real: the actual chat server on
// this PC and two actual Karamelo processes talking through it. Login, message, who
// is online, invite, match, and a forged token refused.
//
// What it cannot cover is Google itself: the tokens are signed here with a test key
// the server is told to trust. server.test.mjs checks that verification on its own.
//
//   node tools/test_chat_internet.mjs
import { spawn } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createChatServer } from "../server/chat/server.mjs";

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const exe = process.env.KARAMELO_TEST_EXE || path.join(root, "app", process.platform === "win32" ? "Karamelo.exe" : "Karamelo");
if (!fs.existsSync(exe)) {
  console.error(`  no ${exe}`);
  process.exit(2);
}

const CLIENT_ID = "selftest.apps.example";
const ISSUER = "https://accounts.google.com";
const { publicKey, privateKey } = crypto.generateKeyPairSync("rsa", { modulusLength: 2048 });
const forger = crypto.generateKeyPairSync("rsa", { modulusLength: 2048 });
const jwk = { ...publicKey.export({ format: "jwk" }), kid: "k1", alg: "RS256", use: "sig" };

function idToken(sub, key = privateKey) {
  const b64 = (o) => Buffer.from(JSON.stringify(o)).toString("base64url");
  const now = Math.floor(Date.now() / 1000);
  const body = b64({ alg: "RS256", kid: "k1" }) + "." + b64({ iss: ISSUER, aud: CLIENT_ID, sub, iat: now, exp: now + 600 });
  return body + "." + crypto.sign("RSA-SHA256", Buffer.from(body), key).toString("base64url");
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), "karamelo-netchat-"));
const server = createChatServer({
  clientIds: [CLIENT_ID], clientSecret: "selftest-secret", hashSecret: "selftest", dataDir: path.join(work, "server"),
  getKey: async (kid) => (kid === "k1" ? jwk : undefined), log: () => {},
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
await new Promise((resolve) => server.relay.listen(0, "127.0.0.1", resolve));
const url = "http://127.0.0.1:" + server.address().port;

async function post(route, body, token) {
  const headers = { "Content-Type": "application/json" };
  if (token) headers.Authorization = "Bearer " + token;
  const r = await fetch(url + route, { method: "POST", headers, body: JSON.stringify(body) });
  return r.json();
}

// Two real games through the bridge: they must keep showing the same image. Needs a
// core and a game, so it is skipped where there are none, as in CI.
async function matchThroughBridge() {
  const app = path.join(root, "app");
  const ext = process.platform === "win32" ? "dll" : process.platform === "darwin" ? "dylib" : "so";
  const core = "cores/gb." + ext;
  const romDir = path.join(app, "roms", "GameBoy");
  const rom = fs.existsSync(romDir) ? fs.readdirSync(romDir).find((f) => /\.(gb|gbc)$/i.test(f)) : null;
  if (!rom || !fs.existsSync(path.join(app, core))) return null;
  const host = await post("/v1/login", { id_token: idToken("match-host"), nick: "H" });
  const guest = await post("/v1/login", { id_token: idToken("match-guest"), nick: "G" });
  const made = await post("/v1/match", {}, host.session);
  const joined = await post("/v1/match/join", { code: made.code }, guest.session);
  const play = (role, ticket) => new Promise((resolve) => {
    const out = path.join(work, "bridge_" + role + ".txt");
    const child = spawn(exe, ["--netplay-selftest", role, core, path.join(romDir, rom), "relay:" + ticket,
      String(made.relay_port), "300", out], {
      cwd: app, stdio: "ignore",
      env: { ...process.env, SDL_AUDIO_DRIVER: "dummy", SDL_VIDEO_DRIVER: process.env.SDL_VIDEO_DRIVER || "dummy" },
    });
    const killer = setTimeout(() => child.kill(), 80000);
    child.on("exit", (code) => {
      clearTimeout(killer);
      resolve({ code, out });
    });
  });
  const hostRun = play("host", made.ticket);
  await new Promise((r) => setTimeout(r, 2000));
  const [h, g] = await Promise.all([hostRun, play("guest", joined.ticket)]);
  const video = (f) => fs.existsSync(f + ".video")
    ? fs.readFileSync(f + ".video", "utf8").trim().split("\n").map((l) => l.trim().split(" ")) : [];
  const seen = new Map(video(h.out));
  let same = 0, diff = 0;
  for (const [frame, image] of video(g.out).slice(1)) {
    if (!seen.has(frame)) continue;
    if (seen.get(frame) === image) same++;
    else diff++;
  }
  return { h: h.code, g: g.code, same, diff };
}

function run(role, token, chatPort) {
  const dir = path.join(work, role);
  fs.mkdirSync(dir, { recursive: true });
  const result = path.join(dir, "result.txt");
  return new Promise((resolve) => {
    const child = spawn(exe, ["--internet-chat-selftest", role, url, token, String(chatPort), result], {
      cwd: dir, stdio: "ignore",
      env: { ...process.env, SDL_AUDIO_DRIVER: "dummy", SDL_VIDEO_DRIVER: process.env.SDL_VIDEO_DRIVER || "dummy" },
    });
    const killer = setTimeout(() => child.kill(), 70000);
    child.on("exit", (code) => {
      clearTimeout(killer);
      const text = fs.existsSync(result) ? fs.readFileSync(result, "utf8").trim() : "(no result)";
      resolve({ code, text });
    });
  });
}

let failed = 0;
function report(name, passed, details) {
  console.log(`  ${name}: ${passed ? "PASSED" : "FAILED"}`);
  if (details) console.log("      " + details);
  if (!passed) failed++;
}

try {
  const bad = await run("bad", idToken("forged", forger.privateKey), 55760);
  report("forged login refused", bad.code === 0, bad.code === 0 ? "" : `exit ${bad.code}`);

  const [a, b] = await Promise.all([run("a", idToken("account-a"), 55761), run("b", idToken("account-b"), 55762)]);
  report("internet chat, match code and bridge", a.code === 0 && b.code === 0,
    `exit ${a.code}: ${a.text}\n      exit ${b.code}: ${b.text}`);

  const game = await matchThroughBridge();
  if (game) {
    report("game through the bridge", (game.h === 0 || game.h === 5) && (game.g === 0 || game.g === 5) && game.same > 0 && game.diff === 0,
      `exit ${game.h}/${game.g}, image: ${game.same} equal, ${game.diff} different`);
  } else {
    console.log("  game through the bridge: skipped, no Game Boy core or game here");
  }
} finally {
  server.close();
  server.closeAllConnections();
  fs.rmSync(work, { recursive: true, force: true });
}
process.exit(failed === 0 ? 0 : 1);
