#!/usr/bin/env python3
"""Fixa a versao e o SHA-256 de cada port em data/ports.json.

Para cada port com repositorio no GitHub:
  - descobre a release (a "latest"; se nao houver, a mais nova com arquivos);
  - grava release_tag e, em asset_sha256, o hash de cada arquivo que o app
    pode baixar (o "digest" que o GitHub publica; se faltar, baixa e calcula).

Uso:
  python tools/pin_ports.py               # fixa so os ports ainda sem versao
  python tools/pin_ports.py --update      # move todos para a release mais nova
  python tools/pin_ports.py --port Zelda64Recomp --update
  python tools/pin_ports.py --check       # so relata o que tem versao nova (exit 1 se houver)

Token: GITHUB_TOKEN no ambiente, ou o do "gh auth token".
Quando algo muda, catalog_version sobe 1 e include/ports_catalog_json.h e
regerado. Detalhes em docs/PORTS_CATALOG.md.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOG = os.path.join(ROOT, "data", "ports.json")

# Arquivos que o seletor do app pode escolher. Checksums, notas e pacotes de
# codigo-fonte nao entram, nem builds antigas que o autor deixou na release com
# "_OLD" no nome (Silent Hill).
ASSET_RE = re.compile(r"\.(zip|7z|rar|tar\.gz|tar\.xz|dmg|appimage|exe)$", re.I)
SKIP_RE = re.compile(r"(source|src)\b|_old\b", re.I)

# Ordem dos campos: a mesma de PortCatalogToJson (src/port_catalog.cpp), para
# o arquivo sair identico ao que o app grava com --dump-port-catalog.
FIELDS = [
    ("id", "str"), ("display_name", "str"), ("repo", "str"), ("exe_hint", "str"),
    ("needs_rom", "bool"), ("rom_keywords", "list"), ("has_linux_build", "bool"),
    ("has_macos_build", "bool"), ("exe_relpath", "str"), ("launch_args", "list"),
    ("console_log_relpath", "str"), ("x360_game_dir", "str"), ("iso_keywords", "list"),
    ("iso_dirs", "list"), ("iso_exts", "list"), ("iso_disc_id", "str"), ("iso_disc_rev", "int"),
    ("iso_disc_label", "str"), ("hidden", "bool"), ("downloads_json_url", "str"),
    ("rom_exclude_keywords", "list"), ("iso_keywords_whole_word", "bool"), ("psp_game_dir", "str"),
    ("release_tag", "str"), ("asset_sha256", "map"), ("notes", "str"),
]


def token():
    t = os.environ.get("GITHUB_TOKEN")
    if t:
        return t
    try:
        return subprocess.run(["gh", "auth", "token"], capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return ""


TOKEN = token()


def api(path):
    req = urllib.request.Request("https://api.github.com/" + path,
                                 headers={"Accept": "application/vnd.github+json", "User-Agent": "karamelo-pin-ports"})
    if TOKEN:
        req.add_header("Authorization", "Bearer " + TOKEN)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def sha256_of_url(url, tries=4):
    # Downloads grandes de objects.githubusercontent.com as vezes caem no meio
    # (connection reset); tenta de novo do zero.
    for attempt in range(1, tries + 1):
        h = hashlib.sha256()
        req = urllib.request.Request(url, headers={"User-Agent": "karamelo-pin-ports"})
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    h.update(chunk)
            return h.hexdigest()
        except (OSError, urllib.error.URLError) as e:
            if attempt == tries:
                raise
            print("    falhou (%s), tentando de novo (%d/%d)..." % (e, attempt + 1, tries), flush=True)
            time.sleep(5 * attempt)


def newest_release(repo):
    """Same choice as DownloadAndInstall: /latest, else the newest release with files."""
    rel = api("repos/%s/releases/latest" % repo)
    if rel and rel.get("assets"):
        return rel
    for r in api("repos/%s/releases?per_page=10" % repo) or []:
        if r.get("assets") and not r.get("draft"):
            return r
    return None


def release_by_tag(repo, tag):
    return api("repos/%s/releases/tags/%s" % (repo, tag))


def hashes_for(rel):
    out = {}
    for a in rel["assets"]:
        name = a["name"]
        if not ASSET_RE.search(name) or SKIP_RE.search(name):
            continue
        digest = a.get("digest") or ""
        if digest.startswith("sha256:"):
            out[name] = digest[7:].lower()
        else:
            print("    calculando %s (%d MB)..." % (name, a["size"] // (1 << 20)), flush=True)
            out[name] = sha256_of_url(a["browser_download_url"])
    return out


def q(s):
    return json.dumps(s, ensure_ascii=False)


def dump(doc):
    out = ['{', '  "catalog_version": %d,' % doc["catalog_version"], '  "ports": [']
    entries = []
    for p in doc["ports"]:
        lines = []
        for key, kind in FIELDS:
            if key not in p:
                continue
            v = p[key]
            if kind == "str" and v == "":
                continue
            if kind == "bool" and v is False:
                continue
            if kind == "int" and v == -1:
                continue
            if kind in ("list", "map") and not v:
                continue
            if kind == "str":
                lines.append('      %s: %s' % (q(key), q(v)))
            elif kind == "bool":
                lines.append('      %s: true' % q(key))
            elif kind == "int":
                lines.append('      %s: %d' % (q(key), v))
            elif kind == "list":
                lines.append('      %s: [%s]' % (q(key), ", ".join(q(x) for x in v)))
            else:
                inner = ",\n".join('        %s: %s' % (q(k), q(h)) for k, h in v.items())
                lines.append('      %s: {\n%s\n      }' % (q(key), inner))
        unknown = [k for k in p if k not in dict(FIELDS)]
        if unknown:
            sys.exit("campo desconhecido em %s: %s" % (p.get("id"), unknown))
        entries.append("    {\n" + ",\n".join(lines) + "\n    }")
    out.append(",\n".join(entries))
    out.append("  ]")
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true", help="move para a release mais nova")
    ap.add_argument("--check", action="store_true", help="so relata versoes novas")
    ap.add_argument("--port", help="so este id")
    args = ap.parse_args()

    with open(CATALOG, encoding="utf-8") as f:
        doc = json.load(f)

    changed, outdated = [], []
    for p in doc["ports"]:
        if args.port and p["id"] != args.port:
            continue
        repo = p.get("repo")
        if not repo or p.get("downloads_json_url"):
            continue
        pinned = p.get("release_tag", "")
        if pinned and not args.update and not args.check:
            continue
        try:
            rel = newest_release(repo)
        except Exception as e:
            print("  %-26s ERRO: %s" % (p["id"], e))
            continue
        if not rel:
            print("  %-26s sem release com arquivos - fica sem versao fixa" % p["id"])
            continue
        tag = rel["tag_name"]
        if args.check:
            if tag != pinned:
                outdated.append((p["id"], pinned or "(sem versao)", tag))
            continue
        if tag == pinned and p.get("asset_sha256"):
            continue
        try:
            hashes = hashes_for(rel)
        except Exception as e:
            print("  %-26s ERRO ao calcular hash: %s - fica como estava" % (p["id"], e))
            continue
        p["release_tag"] = tag
        p["asset_sha256"] = hashes
        changed.append((p["id"], pinned or "(sem versao)", tag))
        print("  %-26s %s -> %s (%d arquivos)" % (p["id"], pinned or "-", tag, len(p["asset_sha256"])), flush=True)

    if args.check:
        for pid, old, new in outdated:
            print("  %-26s %s -> %s" % (pid, old, new))
        print("%d port(s) com versao nova" % len(outdated))
        return 1 if outdated else 0

    if not changed:
        print("nada mudou")
        return 0

    doc["catalog_version"] += 1
    with open(CATALOG, "w", encoding="utf-8", newline="\n") as f:
        f.write(dump(doc))
    print("catalog_version -> %d, %d port(s) alterado(s)" % (doc["catalog_version"], len(changed)))
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "embed_ports.py")], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
