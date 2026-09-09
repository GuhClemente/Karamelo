# Motores de emulação — lista para publicação

Número a publicar: **40 motores** em **35 sistemas**.

Conferido em 09/09/2026 por hash de cada `.dll` em `cores/`, não pelo nome do
arquivo. São 41 arquivos e 40 conteúdos distintos — `n64_parallel.dll` é cópia
byte a byte de `n64.dll`, então contam como um. Publicar 41 seria contar o
mesmo emulador duas vezes.

A fonte de verdade dos números continua sendo `include/app_info.h`
(`APP_CORE_ENGINES`). Esta lista é o detalhamento por trás daquele número.

---

## Nintendo

| motor | sistema |
|---|---|
| Mesen | NES / Famicom |
| bsnes | Super Nintendo |
| ParaLLEl N64 | Nintendo 64 |
| Mupen64Plus-Next | Nintendo 64 |
| Gopher64 | Nintendo 64 |
| Dolphin | GameCube |
| Gambatte | Game Boy / Game Boy Color |
| mGBA | Game Boy Advance |
| melonDS | Nintendo DS |
| Citra | Nintendo 3DS |

## Sega

| motor | sistema |
|---|---|
| Gearsystem | Master System / Game Gear |
| Genesis Plus GX | Mega Drive / Mega CD |
| PicoDrive | Sega 32X |
| Beetle Saturn | Sega Saturn |
| Flycast | Dreamcast, Naomi, Atomiswave |

## Sony

| motor | sistema |
|---|---|
| Beetle PSX | PlayStation |
| LRPS2 (PCSX2) | PlayStation 2 |
| Play! | PlayStation 2 |
| PPSSPP | PlayStation Portable |

## SNK

| motor | sistema |
|---|---|
| Geolith | Neo Geo AES / MVS |
| NeoCD | Neo Geo CD |
| Beetle NeoPop | Neo Geo Pocket / Color |

## NEC

| motor | sistema |
|---|---|
| Beetle PCE | PC Engine / TurboGrafx-16 |
| Beetle PC-FX | PC-FX |

## Atari

| motor | sistema |
|---|---|
| Stella | Atari 2600 |
| Atari800 | Atari 5200 |
| ProSystem | Atari 7800 |
| Handy | Atari Lynx |
| Virtual Jaguar | Atari Jaguar |

## Computadores

| motor | sistema |
|---|---|
| fMSX | MSX / MSX2 / MSX2+ |
| VICE | Commodore 64 |
| PUAE | Commodore Amiga |
| Fuse | ZX Spectrum |
| DOSBox Pure | MS-DOS |

## Outros

| motor | sistema |
|---|---|
| Opera | Panasonic 3DO |
| Bandai WonderSwan (Beetle) | WonderSwan / Color |
| *(não identificado)* | ColecoVision |

## Arcade

| motor | sistema |
|---|---|
| MAME 0.289 | Arcade |
| MAME 2003 | Arcade (romset 0.78) |
| MAME 2010 | Arcade (romset 0.139) |

---

## Duas ressalvas de honestidade

**O core de ColecoVision não se identifica.** O arquivo é `coleco.dll` e
funciona, mas não declara qual emulador é. Publique como "ColecoVision" sem
nome de motor, ou omita a linha — não invente um nome.

**O arquivo `arcade_fbneo.dll` não é FinalBurn Neo.** É uma build do MAME
0.289, confirmado por três evidências independentes: o core se identifica como
`MAME v0.289`, todas as opções que ele declara começam com `mame_`, e o arquivo
tem 372 MB (o FinalBurn Neo tem cerca de 60). **O projeto não distribui
FinalBurn Neo.** Se o site menciona FBNeo em algum lugar, está errado e precisa
sair.

---

## O que mudou em 09/09/2026

Quatro arquivos foram removidos do pacote — 25 MB que nenhum caminho do código
conseguia carregar: `pcsx2.dll` e `pcsx2_libretro.dll` (cópias de `ps2.dll`),
`play_libretro.dll` (cópia de `ps2_play.dll`) e `bluemsx.dll`, sobra de uma
tentativa abandonada de rotear MSX para blueMSX.

Por isso o número correto passou de 41 para **40**. O site publica 39, que é
uma contagem ainda mais antiga.
