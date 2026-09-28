#!/usr/bin/env python3
"""Gera include/karamelo_icon_png.h a partir de src/karamelo.ico.

O Windows embute o .ico no .exe pelo resource.rc. Linux e macOS nao tem
recurso de icone no binario, entao o main_linux.cpp aplica o mesmo desenho na
janela (SDL_SetWindowIcon: Dock no macOS, barra de tarefas no Linux) a partir
destes bytes. Assim os tres usam um arquivo so: mudou o src/karamelo.ico
(tools/make_icon.py), rode este script de novo.

Pega a maior imagem do .ico - o make_icon.py grava cada tamanho como PNG, e o
PNG vai para o header do jeito que esta, sem decodificar; quem decodifica e o
SDL_LoadPNG_IO do SDL3 embutido. Nao precisa de Pillow.

Uso:  python3 tools/embed_icon.py
"""

import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICO = os.path.join(ROOT, "src", "karamelo.ico")
OUT = os.path.join(ROOT, "include", "karamelo_icon_png.h")


def largest_png(data):
    count = struct.unpack("<HHH", data[:6])[2]
    best = None
    for i in range(count):
        w, h, _, _, _, _, size, off = struct.unpack("<BBBBHHII", data[6 + 16 * i:22 + 16 * i])
        w, h = w or 256, h or 256
        blob = data[off:off + size]
        if blob[:8] != b"\x89PNG\r\n\x1a\n":
            continue
        if best is None or w * h > best[0] * best[1]:
            best = (w, h, blob)
    return best


def main():
    with open(ICO, "rb") as f:
        best = largest_png(f.read())
    if best is None:
        sys.exit("nenhuma imagem PNG em " + ICO)
    w, h, png = best

    lines = []
    for i in range(0, len(png), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in png[i:i + 16]) + ",")

    with open(OUT, "w", newline="\n") as f:
        f.write("// Gerado por tools/embed_icon.py a partir de src/karamelo.ico - nao editar.\n")
        f.write("// %dx%d PNG, %d bytes. O mesmo icone que o resource.rc poe no .exe.\n" % (w, h, len(png)))
        f.write("#pragma once\n\n")
        f.write("static const unsigned char KARAMELO_ICON_PNG[] = {\n")
        f.write("\n".join(lines))
        f.write("\n};\n")
    print("%s: %dx%d, %d bytes" % (os.path.relpath(OUT, ROOT), w, h, len(png)))


if __name__ == "__main__":
    main()
