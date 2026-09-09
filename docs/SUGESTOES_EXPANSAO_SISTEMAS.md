# 🎮 Karamelo — Novos Sistemas e Sugestões de Expansão

> **Documento de Análise e Novas Plataformas (Apenas o que ainda NÃO está implementado)**  
> **Projeto:** Karamelo (Native Windows x64 Port)  
> **Base de Execução:** Libretro API Architecture (C++17 / OpenGL HW / Software Blit / Ring Buffer Audio)

---

## 📌 1. Sistemas Já Presentes no Projeto (19 Sistemas Ativos)

Para referência rápida, os seguintes sistemas **já estão implementados** no Karamelo e **não precisam ser adicionados**:

* **Consoles de Mesa:** NES, SNES, Nintendo 64, Genesis / Mega Drive, Mega CD, Sega Saturn, Dreamcast, PlayStation 1, PlayStation 2, GameCube, TurboGrafx-16 (PCE) e Atari 2600.
* **Portáteis:** Nintendo DS (NDS), Nintendo 3DS e Master System / Game Gear.
* **Arcade & Especializados:** NeoGeo AES/MVS, NeoGeo CD, Atari 7800 e Fliperamas Arcade (FBNeo, MAME 2003, MAME 2010).

---

## 🔍 2. Avaliação dos Sistemas Solicitados (PSP, PS3 e Xbox)

| Plataforma | Viável no Karamelo? | Motivo Técnico |
| :--- | :---: | :--- |
| **PSP** (PlayStation Portable) | ✅ **100% Viável (Sugerido)** | Usa o core oficial **PPSSPP** (`ppsspp.dll`). Totalmente compatível com o pipeline OpenGL FBO (`hw_render.cpp`) já existente no projeto. Suporta `.iso`, `.cso`, `.pbp` e `.chd` com saves e RetroAchievements. |
| **PS3** (PlayStation 3) | ❌ **Inviável como Core Interno** | O **RPCS3** é um emulador monolítico *standalone*. Não existe core Libretro funcional de PS3. Exige Vulkan, instalação de firmware Sony (`.PUP`) e compilação LLVM pesada de PPU/SPU. |
| **Xbox** (Original / Clássico) | ❌ **Inviável como Core Interno** | Os emuladores **xemu** e **Cxbx-Reloaded** não possuem cores Libretro funcionais. Rodam apenas como executáveis externos. |
| **Xbox 360** | ❌ **Inviável como Core Interno** | O **Xenia** foi projetado para DirectX 12 / Vulkan nativo e não possui porte para a arquitetura Libretro DLL. |

---

## 🚀 3. Novos Sistemas Sugeridos para Adicionar

Abaixo estão listados **apenas os sistemas que ainda NÃO estão no projeto**, divididos por categorias:

### 3.1 Portáteis Inéditos (Alta Prioridade)

1. **Game Boy Advance (GBA)** ⭐
   * **Core:** `mgba.dll`
   * **Extensões:** `.gba`, `.zip`, `.7z`
   * **Por que colocar:** Uma das maiores bibliotecas portáteis de todos os tempos. O core mGBA é super leve, com áudio perfeito e suporte total ao RetroAchievements.
2. **Game Boy & Game Boy Color (GB / GBC)** ⭐
   * **Core:** `gambatte.dll` ou `sameboy.dll`
   * **Extensões:** `.gb`, `.gbc`, `.zip`
   * **Por que colocar:** Suporte a paletas personalizadas, bordas de Super Game Boy e consumo nulo de CPU.
3. **PlayStation Portable (PSP)** ⭐
   * **Core:** `ppsspp.dll`
   * **Extensões:** `.iso`, `.cso`, `.pbp`, `.chd`
   * **Por que colocar:** Fecha a trinca dos portáteis Sony/Nintendo ao lado do NDS e 3DS já existentes.
4. **Neo Geo Pocket / Pocket Color (NGP / NGPC)**
   * **Core:** `mednafen_ngp.dll`
   * **Extensões:** `.ngp`, `.ngc`, `.zip`
   * **Por que colocar:** Jogos exclusivos da SNK (*SNK vs. Capcom Card Fighters*, *Metal Slug 1st/2nd Mission*).
5. **Bandai WonderSwan & WonderSwan Color (WS / WSC)**
   * **Core:** `mednafen_wswan.dll`
   * **Extensões:** `.ws`, `.wsc`, `.zip`
   * **Por que colocar:** Exclusivos japoneses de *Final Fantasy*, *Digimon*, *Klonoa* e animes.
6. **Atari Lynx**
   * **Core:** `handy.dll`
   * **Extensões:** `.lnx`, `.zip`
   * **Por que colocar:** Primeiro portátil colorido de 16-bit com escala de sprites por hardware.

---

### 3.2 Computadores Retrô e MS-DOS

1. **MS-DOS (PC Clássico / DOSBox Pure)** ⭐
   * **Core:** `dosbox_pure.dll`
   * **Extensões:** `.zip` (pacotes completos de jogos de DOS)
   * **Por que colocar:** ⭐ **Sensacional:** Permite rodar clássicos de PC (*Doom, Duke Nukem 3D, Prince of Persia, SimCity 2000, Wolfenstein 3D*) empacotados diretamente em arquivos `.zip`. O core faz mapeamento automático para o controle do Xbox/PlayStation sem precisar de teclado físico.
2. **MSX / MSX2 / MSX2+**
   * **Core:** `fmsx.dll` ou `bluemsx.dll`
   * **Extensões:** `.rom`, `.dsk`, `.zip`
   * **Por que colocar:** Berço de franquias históricas como *Metal Gear*, *Vampire Killer (Castlevania)*, *Snatcher* e *Parodius*.
3. **Commodore Amiga (500 / 1200 / CD32)**
   * **Core:** `puae.dll`
   * **Extensões:** `.adf`, `.lha`, `.chd`
   * **Por que colocar:** Grande sucesso europeu (*Turrican, Lemmings, Sensible Soccer, Speedball 2*).
4. **Commodore 64 (C64)**
   * **Core:** `vice_x64.dll`
   * **Extensões:** `.d64`, `.t64`, `.crt`, `.prg`, `.zip`
   * **Por que colocar:** O microcomputador mais vendido da história.
5. **ZX Spectrum**
   * **Core:** `fuse.dll`
   * **Extensões:** `.tzx`, `.tap`, `.z80`, `.zip`
   * **Por que colocar:** O clássico britânico de 8-bit com acervo imenso de jogos.

---

### 3.3 Consoles de Mesa Inéditos

1. **Sega 32X**
   * **Core:** `picodrive.dll`
   * **Extensões:** `.32x`, `.zip`
   * **Por que colocar:** Expansão de 32-bit do Mega Drive (*Knuckles' Chaotix, Virtua Racing Deluxe, Kolibri*).
2. **Panasonic 3DO**
   * **Core:** `opera.dll`
   * **Extensões:** `.iso`, `.chd`, `.cue`
   * **Por que colocar:** Pioneiro console de 32-bit em CD-ROM (*Road Rash, Need for Speed, Gex*).
3. **Atari Jaguar**
   * **Core:** `virtualjaguar.dll`
   * **Extensões:** `.j64`, `.jag`, `.zip`
   * **Por que colocar:** Console de 64-bit da Atari (*Alien vs Predator, Tempest 2000*).
4. **Atari 5200 & ColecoVision**
   * **Core:** `a5200.dll` e `gearcoleco.dll`
   * **Extensões:** `.a52`, `.col`, `.zip`
   * **Por que colocar:** Consoles clássicos do início dos anos 80 para completar a era pré-NES.
5. **NEC PC-FX**
   * **Core:** `mednafen_pcfx.dll`
   * **Extensões:** `.chd`, `.cue`
   * **Por que colocar:** Sucessor de 32-bit do PC Engine focado em FMV e jogos japoneses.

---

## 🛠️ 4. Tabela de Mapeamento dos Novos Sistemas

Para quando você decidir implementar os novos sistemas em `menu.cpp` e `core_runner.cpp`:

| Novo Sistema | Pasta de ROMs Sugerida | Core DLL | Extensões Principais | Renderizador |
| :--- | :--- | :--- | :--- | :--- |
| **Game Boy Advance** | `roms/GBA/` | `cores/gba.dll` (`mgba`) | `.gba`, `.zip`, `.7z` | Software 2D |
| **Game Boy / Color** | `roms/GameBoy/` | `cores/gb.dll` (`gambatte`) | `.gb`, `.gbc`, `.zip` | Software 2D |
| **PlayStation Portable** | `roms/PSP/` | `cores/psp.dll` (`ppsspp`) | `.iso`, `.cso`, `.pbp`, `.chd` | OpenGL 3D (HW) |
| **MS-DOS (PC Retrô)** | `roms/DOS/` | `cores/dosbox_pure.dll` | `.zip` | Software (Auto Gamepad) |
| **MSX / MSX2** | `roms/MSX/` | `cores/msx.dll` (`fmsx`) | `.rom`, `.dsk`, `.zip` | Software 2D |
| **Commodore Amiga** | `roms/Amiga/` | `cores/amiga.dll` (`puae`) | `.adf`, `.lha`, `.chd` | Software 2D |
| **Sega 32X** | `roms/32X/` | `cores/32x.dll` (`picodrive`) | `.32x`, `.zip` | Software 2D |
| **Panasonic 3DO** | `roms/3DO/` | `cores/3do.dll` (`opera`) | `.iso`, `.chd`, `.cue` | Software 2D/3D |
| **Neo Geo Pocket** | `roms/NGP/` | `cores/ngp.dll` (`mednafen_ngp`) | `.ngp`, `.ngc`, `.zip` | Software 2D |
| **WonderSwan** | `roms/WonderSwan/` | `cores/wswan.dll` (`mednafen_wswan`) | `.ws`, `.wsc`, `.zip` | Software 2D |
| **Commodore 64** | `roms/C64/` | `cores/c64.dll` (`vice_x64`) | `.d64`, `.t64`, `.zip` | Software 2D |
| **ZX Spectrum** | `roms/ZXSpectrum/` | `cores/spectrum.dll` (`fuse`) | `.tzx`, `.tap`, `.z80` | Software 2D |
| **Atari Jaguar** | `roms/Jaguar/` | `cores/jaguar.dll` (`virtualjaguar`) | `.j64`, `.jag`, `.zip` | Software 2D/3D |
| **Atari Lynx** | `roms/Lynx/` | `cores/lynx.dll` (`handy`) | `.lnx`, `.zip` | Software 2D |
| **Atari 5200** | `roms/Atari5200/` | `cores/atari5200.dll` (`a5200`) | `.a52`, `.zip` | Software 2D |
| **ColecoVision** | `roms/ColecoVision/` | `cores/coleco.dll` (`gearcoleco`) | `.col`, `.zip` | Software 2D |
| **PC-FX** | `roms/PCFX/` | `cores/pcfx.dll` (`mednafen_pcfx`) | `.chd`, `.cue` | Software 2D |

---

## 🎯 5. Top 4 Recomendações Imediatas

Se for expandir o projeto agora, as 4 adições com maior impacto e retorno imediato são:

1. **GBA (Game Boy Advance)** — Indispensável, leve, catálogo fantástico e suporte total a RetroAchievements.
2. **Game Boy / Game Boy Color** — Um clássico que roda com 0% de uso de CPU e suporte a paletas de cores.
3. **PSP (PlayStation Portable)** — Fecha o ecossistema de portáteis 3D aproveitando o suporte OpenGL existente.
4. **MS-DOS (DOSBox Pure)** — Permite jogar games clássicos de PC direto de arquivos `.zip` já configurados no controle.
