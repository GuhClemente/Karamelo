# libmspack (LZX decoder only)

Vendorizado de <https://github.com/kyz/libmspack>, commit
`55d501976171397ccd5d5a7a1ca7da065b1d9a06` (27/09/2026), pasta
`libmspack/mspack/`. Só os arquivos que o descompressor LZX precisa:
`lzxd.c`, `lzx.h`, `readbits.h`, `readhuff.h`, `mspack.h`, `system.h`,
`macros.h`. Nenhum foi modificado.

Uso: `src/x360_dump.cpp` descomprime o payload "normal" (LZX) de um
`default.xex` de Xbox 360 — o mesmo decodificador que o Xenia usa para isso.
A glue (um `mspack_system` sobre memória) fica em `x360_dump.cpp`; o
`system.c` da biblioteca não é compilado.

Licença: LGPL-2.1 (`COPYING.LIB`), compatível com a GPL-3.0 do Karamelo.
Aviso completo em `THIRD-PARTY-NOTICES.md`.
