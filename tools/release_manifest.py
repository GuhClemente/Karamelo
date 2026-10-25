#!/usr/bin/env python3
"""dist/version.json - o manifesto do auto-update - antes e depois de empacotar.

Um manifesto so serve as tres builds: "version" + exe_* (Windows),
macos_bin_* e linux_bin_*. Cada build e publicada de uma maquina diferente
(Windows, WSL, Mac), entao:

  fetch     baixa o version.json que esta no site para dist/version.json.
            O site e a verdade: o dist/ local de uma maquina nao tem o bloco
            que a outra publicou, e subir ele apagaria esse bloco.
            Sem rede, avisa e segue com o local.

  finalize  antes do upload:
            - bloco macOS/Linux sem "<prefixo>_version" (de antes da 0.9.78)
              e de um binario de versao desconhecida. Se "version" subiu, o
              app antigo naquela plataforma veria a versao nova, baixaria o
              mesmo binario velho (o hash confere) e pediria de novo a cada
              inicio. Os campos do binario saem; o pacote (tar_url) fica para
              o site. Com o campo de versao, o app 0.9.78+ decide sozinho.
            - "notes" vem da secao do docs/SITE_SYNC.md cujo titulo tem "Versao <version>"
              (ex.: "## 42. Lancamento da Versao 0.9.78 (25/10/2026)")
              (os titulos em negrito, sem acento: o OSD so tem ASCII). Sem a
              secao, as notas da versao anterior ficariam - isso e erro.

Uso:
  python tools/release_manifest.py fetch
  python tools/release_manifest.py finalize
  python tools/release_manifest.py catalog-check

catalog-check (deploy_ports_catalog.bat): o data/ports.json local precisa ter
catalog_version MAIOR que o publicado - o app ignora catalogo de versao igual
ou menor, entao publicar sem subir o numero nao chega a ninguem.
"""
import json
import os
import re
import sys
import unicodedata
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "dist", "version.json")
URL = "https://karamelo-emu.com/downloads/version.json"
PLATFORMS = [("macos_bin", "macOS"), ("linux_bin", "Linux")]


def load():
    with open(MANIFEST, encoding="utf-8-sig") as f:
        return json.load(f)


def save(data):
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
        f.write("\n")


def fetch():
    try:
        req = urllib.request.Request(URL, headers={"User-Agent": "karamelo-release"})
        with urllib.request.urlopen(req, timeout=30) as r:
            body = r.read().decode("utf-8-sig")
        data = json.loads(body)
        if "version" not in data:
            raise ValueError("sem campo version")
    except Exception as e:
        print("  [AVISO] nao consegui baixar %s (%s) - seguindo com o dist/version.json local." % (URL, e))
        return 0
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    save(data)
    print("  version.json do site: v%s" % data["version"])
    return 0


def ascii_fold(s):
    s = s.replace("—", "-").replace("–", "-")
    s = unicodedata.normalize("NFKD", s)
    return "".join(c for c in s if ord(c) < 128 and not unicodedata.combining(c))


def app_version():
    with open(os.path.join(ROOT, "include", "app_info.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+APP_VERSION\s+"([^"]+)"', f.read())
    return m.group(1) if m else ""


def notes_for(version):
    path = os.path.join(ROOT, "docs", "SITE_SYNC.md")
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    head = re.compile(r"^## .*Vers.o %s\b" % re.escape(version))
    for i, line in enumerate(lines):
        if head.match(line):
            titles = []
            for body in lines[i + 1:]:
                if body.startswith("---") or body.startswith("## "):
                    break
                m = re.match(r"^- \*\*(.+?)\*\*", body)
                if m:
                    titles.append(m.group(1).rstrip(":"))
            if titles:
                return ascii_fold("; ".join(titles) + ".")
    return ""


def catalog_check():
    with open(os.path.join(ROOT, "data", "ports.json"), encoding="utf-8") as f:
        local = json.load(f)["catalog_version"]
    try:
        req = urllib.request.Request(URL.replace("version.json", "ports.json"),
                                     headers={"User-Agent": "karamelo-release"})
        with urllib.request.urlopen(req, timeout=30) as r:
            remote = json.loads(r.read().decode("utf-8-sig"))["catalog_version"]
    except urllib.error.HTTPError as e:
        if e.code == 404:
            print("  site ainda sem ports.json - publicando a v%d." % local)
            return 0
        print("  [ERRO] nao consegui ler o ports.json do site: %s" % e)
        return 1
    except Exception as e:
        print("  [ERRO] nao consegui ler o ports.json do site: %s" % e)
        return 1
    if local <= remote:
        print("  [ERRO] catalog_version local (%d) nao e maior que o do site (%d): o app"
              " ignoraria o catalogo. Suba o numero em data/ports.json." % (local, remote))
        return 1
    print("  catalogo v%d -> v%d" % (remote, local))
    return 0


def finalize():
    data = load()
    version = data.get("version", "")
    ok = True

    for prefix, name in PLATFORMS:
        if prefix + "_url" not in data:
            continue
        own = data.get(prefix + "_version", "")
        if own:
            if own != version:
                print("  %s na v%s; \"version\" (Windows) na v%s." % (name, own, version))
            continue
        # Sem campo de versao, o binario e o que foi publicado junto do
        # "version" atual do site. So vira problema quando este deploy sobe o
        # "version" (checkout em APP_VERSION): ai ele fica para tras.
        if version != app_version():
            continue
        for k in (prefix + "_url", prefix + "_size", prefix + "_sha256"):
            data.pop(k, None)
        print("  [AVISO] binario %s de versao desconhecida removido do manifesto: quem usa %s"
              " nao recebe atualizacao ate a proxima build %s." % (name, name, name))

    # As notas sao de "version" (a release do Windows). So quem esta lancando
    # essa versao - o checkout em APP_VERSION - responde por elas: o Mac
    # publicando a 0.9.78 antes do Windows encontra "version" 0.9.77 no site e
    # deixa as notas dela como estao.
    notes = notes_for(version)
    if notes:
        data["notes"] = notes
        print("  notas da v%s: %s" % (version, notes))
    elif version != app_version():
        print("  \"version\" do site e a v%s (Windows); notas mantidas." % version)
    else:
        print("  [ERRO] docs/SITE_SYNC.md nao tem uma secao com \"Versao %s\" no titulo - o site e o app"
              " mostrariam as notas da versao anterior. Escreva a secao e rode de novo." % version)
        ok = False

    save(data)
    return 0 if ok else 1


if __name__ == "__main__":
    commands = {"fetch": fetch, "finalize": finalize, "catalog-check": catalog_check}
    if len(sys.argv) != 2 or sys.argv[1] not in commands:
        sys.exit(__doc__)
    sys.exit(commands[sys.argv[1]]())
