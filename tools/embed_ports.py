#!/usr/bin/env python3
"""Gera include/ports_catalog_json.h a partir de data/ports.json.

O catalogo de Ports & Recomp viaja dentro do executavel porque o auto-update
troca so o binario: um ports.json solto ao lado dele nunca chegaria a quem
atualizou pelo app. Rode depois de editar data/ports.json:

    python tools/embed_ports.py

Os testes de unidade (tests/test_port_catalog.cpp) reprovam o build se o
cabecalho gerado estiver diferente de data/ports.json, entao esquecer de rodar
isto nao passa despercebido.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "data", "ports.json")
DST = os.path.join(ROOT, "include", "ports_catalog_json.h")


def main():
    with open(SRC, "rb") as f:
        raw = f.read()

    # Recusa um JSON quebrado antes de embuti-lo: o app cairia para um
    # catalogo vazio no lugar de mostrar os ports.
    doc = json.loads(raw.decode("utf-8"))
    ids = [p["id"] for p in doc["ports"]]
    if len(ids) != len(set(ids)):
        sys.exit("ids duplicados em data/ports.json")

    visible = [p for p in doc["ports"] if not p.get("hidden")]
    lines = [
        "// Gerado por tools/embed_ports.py a partir de data/ports.json - nao editar.",
        "// catalog_version %d, %d ports (%d visiveis: %d com build Linux, %d com build macOS)."
        % (doc["catalog_version"], len(ids), len(visible),
           sum(1 for p in visible if p.get("has_linux_build")),
           sum(1 for p in visible if p.get("has_macos_build"))),
        "#pragma once",
        "",
        "static const unsigned char PORTS_CATALOG_JSON[] = {",
    ]
    for i in range(0, len(raw), 16):
        chunk = raw[i:i + 16]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("")

    with open(DST, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines))
    print("%s: %d bytes, %d ports" % (os.path.relpath(DST, ROOT), len(raw), len(ids)))


if __name__ == "__main__":
    main()
