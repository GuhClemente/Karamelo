// What may reach a room. Two separate things:
//
// - The word list and links are swapped for something sweet, always. It runs here, so it
//   works with the moderation service off, down or unpaid.
// - Jev (TypeSafe AI) judges the message as a whole: a threat or a scam has no bad word to
//   swap, so it stops those. If it does not answer, the message goes on without it.
import fs from "node:fs";
import crypto from "node:crypto";

export const SWEET_WORDS = [
  "caramelinho", "docinho", "brigadeiro", "pudim", "jujuba", "pirulito", "bombom", "paçoca",
  "quindim", "cocada", "beijinho", "pé de moleque", "doce de leite", "algodão doce", "marshmallow",
  "balinha", "cupcake", "sorvetinho", "chocolatinho", "cajuzinho", "bolinho", "churros",
  "maria mole", "rapadura", "sagu", "goiabada", "suspiro", "panqueca", "karamelito", "karamelinho",
];

// In the list, but everyday words or who someone is: "comer", "mama", "lésbica". The admin
// panel can change this. Swapping them would make the chat absurd or offend whoever it names.
export const DEFAULT_ALLOWED = [
  "aborto", "amador", "aranha", "ariano", "bissexual", "burro", "camisinha", "cerveja", "cocaína",
  "comer", "consolo", "dum raio", "fecal", "frango assado", "heroína", "heterosexual", "homem gay",
  "homoerótico", "homosexual", "inferno", "lésbica", "mama", "passar um cheque", "pau", "pinto",
  "saco", "torneira",
];

const LEET = { "0": "o", "1": "i", "3": "e", "4": "a", "5": "s", "7": "t", "@": "a", "$": "s" };

export function normalizeWord(word) {
  return word.toLowerCase().normalize("NFD").replace(/\p{M}/gu, "")
    .replace(/[013457@$]/g, (c) => LEET[c]);
}

function collapse(word) {
  return word.replace(/(.)\1+/g, "$1");
}

export function loadBlocklist(file = new URL("./blocklist.txt", import.meta.url)) {
  try {
    return fs.readFileSync(file, "utf8").split(/\r?\n/).filter((l) => l && !l.startsWith("#"));
  } catch {
    return [];
  }
}

// Prepared once: the same list is checked for every message.
export function makeMatcher(words, allowed) {
  const allow = new Set(allowed.map((w) => normalizeWord(w).replace(/[^\p{L}\p{N}]+/gu, " ").trim()));
  const set = new Set();
  const collapsed = new Set();
  const literal = []; // what has no letters at all, like an emoji
  let longest = 1;
  for (const word of words) {
    const key = normalizeWord(word).replace(/[^\p{L}\p{N}]+/gu, " ").trim();
    if (!key) {
      if (word.trim()) literal.push(word.trim());
      continue;
    }
    if (allow.has(key)) continue;
    set.add(key);
    if (!key.includes(" ") && key.length > 2) collapsed.add(collapse(key));
    longest = Math.max(longest, key.split(" ").length);
  }
  return { set, collapsed, literal, longest };
}

const TOKEN = /[\p{L}\p{N}@$]+/gu;
const LINK = /(?:https?:\/\/|www\.)\S+|\b[\w-]+(?:\.[\w-]+)*\.(?:com|net|org|gg|io|br|xyz|ru|ly|me|tv|info|site|online|link|app|dev)\b(?:\/\S*)?/gi;

export function sweeten(text, matcher, { links = true, pick = randomSweet } = {}) {
  let changed = false;
  let out = text;
  if (links) {
    out = out.replace(LINK, () => {
      changed = true;
      return pick();
    });
  }
  for (const item of matcher.literal) {
    if (out.includes(item)) {
      out = out.split(item).join(pick());
      changed = true;
    }
  }
  const tokens = [...out.matchAll(TOKEN)].map((m) => ({ start: m.index, end: m.index + m[0].length, key: normalizeWord(m[0]) }));
  const spans = [];
  for (let i = 0; i < tokens.length;) {
    let hit = 0;
    // The longest phrase first: "puta que pariu" is one swap, not two.
    for (let n = Math.min(matcher.longest, tokens.length - i); n >= 1 && !hit; n--) {
      if (matcher.set.has(tokens.slice(i, i + n).map((t) => t.key).join(" "))) hit = n;
    }
    if (!hit && matcher.collapsed.has(collapse(tokens[i].key))) hit = 1;
    // Letters spread out: "p u t a".
    if (!hit && tokens[i].key.length === 1) {
      let n = 1;
      while (i + n < tokens.length && tokens[i + n].key.length === 1) n++;
      if (n >= 2 && matcher.set.has(tokens.slice(i, i + n).map((t) => t.key).join(""))) hit = n;
    }
    if (hit) {
      spans.push([tokens[i].start, tokens[i + hit - 1].end]);
      i += hit;
    } else {
      i++;
    }
  }
  for (const [start, end] of spans.reverse()) {
    out = out.slice(0, start) + pick() + out.slice(end);
    changed = true;
  }
  return { text: out, changed };
}

function randomSweet() {
  return SWEET_WORDS[crypto.randomInt(SWEET_WORDS.length)];
}

const JEV_URL = "https://api.typesafe.ai/v1/systemone";
const JEV_QUESTION = "A mensagem tem discurso de odio, ameaca, assedio grave, conteudo sexual, golpe ou pedido de dados pessoais (senha, endereco, telefone, IP)? Conversa normal, giria e provocacao leve de jogo nao contam.";
const FAILS_BEFORE_PAUSE = 5;

// The word "unavailable" covers everything that is not a verdict. Never an exception:
// a moderation service having a bad day must not stop people from talking.
export function makeJev({ apiKey, timeoutMs = 1500, call, now = Date.now }) {
  const stats = { calls: 0, ok: 0, failed: 0, blocked: 0, skipped: 0, last_error: "", last_ok_at: 0, last_ms: 0, paused_until: 0 };
  let consecutive = 0;
  const realCall = async (text) => {
    const r = await fetch(JEV_URL, {
      method: "POST",
      headers: { Authorization: `Bearer ${apiKey}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        state: { mensagem_de_chat: text, contexto: "Sala de chat publica de um emulador de jogos" },
        questions: { breaks_rules: { type: "noul", instructions: JEV_QUESTION } },
        model: "jev-latest",
      }),
      signal: AbortSignal.timeout(timeoutMs),
    });
    if (!r.ok) return { status: r.status };
    const data = await r.json();
    return { status: 200, risk: data?.answers?.breaks_rules?.noul };
  };
  const fetcher = call || realCall;
  return {
    stats,
    configured: Boolean(apiKey || call),
    // The risk from 0 to 1, or null when there is no answer.
    async risk(text) {
      if (!apiKey && !call) return null;
      if (now() < stats.paused_until) {
        stats.skipped++;
        return null;
      }
      stats.calls++;
      const started = now();
      let result;
      try {
        result = await fetcher(text);
      } catch (e) {
        result = { error: e?.name === "TimeoutError" ? "timeout" : "no_connection" };
      }
      stats.last_ms = now() - started;
      if (result.status === 200 && typeof result.risk === "number") {
        stats.ok++;
        stats.last_ok_at = now();
        stats.last_error = "";
        consecutive = 0;
        return result.risk;
      }
      stats.failed++;
      consecutive++;
      stats.last_error = result.error || (result.status ? `HTTP ${result.status}` : "bad_answer");
      // A key refused or no credit does not fix itself in a second: stop asking for a while.
      const account = [401, 402, 403, 429].includes(result.status);
      if (account) stats.paused_until = now() + 10 * 60 * 1000;
      else if (consecutive >= FAILS_BEFORE_PAUSE) stats.paused_until = now() + 60 * 1000;
      return null;
    },
  };
}
