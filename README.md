# MiSTer Flavor — Native Windows x64 Port & Open Source Emulation Suite

> **Projeto Oficial de Código Aberto**  
> Canal do YouTube: **[@GuhClemente](https://youtube.com/@GuhClemente)**  
> Desenvolvido por: **Guh Clemente & Antigravity Pair Team**  
> Core Engine: **Libretro API Architecture**

---

## 🌟 Visão Geral

O **MiSTer Flavor** é um port nativo em C++17 (64-bit) de alto desempenho da interface de usuário e OSD do MiSTer para o ambiente Windows x86_64, integrado a uma engine modular de execução de cores Libretro com escalonador de 60 FPS com correção de aspecto, filtros CRT, Ring Buffer de áudio estéreo de baixa latência e suporte nativo a 11 consoles clássicos.

---

## 🎮 Sistemas Suportados

| Sistema | Core DLL | Extensões Suportadas | Pasta de ROMs |
| :--- | :--- | :--- | :--- |
| **Nintendo 64** | `n64.dll` | `.z64`, `.n64`, `.v64`, `.zip`, `.7z`, `.rar` | `roms/Nintendo64/` |
| **Super Nintendo (SNES)** | `snes.dll` | `.sfc`, `.smc`, `.bs`, `.zip`, `.7z`, `.rar` | `roms/SNES/` |
| **Genesis / Mega Drive** | `genesis.dll` | `.md`, `.gen`, `.bin`, `.smd`, `.zip`, `.7z`, `.rar` | `roms/Genesis/` |
| **Mega CD** | `genesis.dll` | `.chd`, `.cue`, `.iso`, `.m3u`, `.zip`, `.7z`, `.rar` | `roms/MegaCD/` |
| **NES / Famicom** | `nes.dll` | `.nes`, `.fds`, `.unf`, `.zip`, `.7z`, `.rar` | `roms/NES/` |
| **NeoGeo (AES/MVS)** | `neogeo.dll` | `.neo`, `.bin`, `.zip` | `roms/NeoGeo/` |
| **NeoGeo CD** | `neocd_alt.dll` | `.chd`, `.cue` | `roms/NeoGeo/` |
| **PlayStation (PS1)** | `psx.dll` | `.chd`, `.cue`, `.iso`, `.pbp`, `.m3u`, `.zip` | `roms/PlayStation/` |
| **Sega Saturn** | `saturn.dll` | `.chd`, `.cue`, `.iso`, `.toc`, `.m3u`, `.zip` | `roms/Saturn/` |
| **Master System & GG** | `sms.dll` | `.sms`, `.gg`, `.sg`, `.bin`, `.zip`, `.7z`, `.rar` | `roms/MasterSystem/` |
| **TurboGrafx-16 / PCE** | `pce.dll` | `.pce`, `.sgx`, `.chd`, `.cue`, `.iso`, `.zip` | `roms/TurboGrafx16/` |
| **Atari 2600** | `atari2600.dll` | `.a26`, `.bin`, `.zip`, `.7z`, `.rar` | `roms/Atari2600/` |

---

## 🚀 Recursos Principais

* **Identificação e Extração de Arquivos Compactados:** Carrega diretamente jogos compactados em `.ZIP`, `.7Z` e `.RAR` de forma transparente com cache dinâmico.
* **Sistema Completo de Save State & Load State:**
  * 10 slots independentes por jogo (`0` a `9`).
  * Gravação persistente no disco em `saves/<Sistema>/<NomeDoJogo>.state<Slot>`.
  * Notificações HUD em tempo real na tela do jogo (*Toast Notifications*).
* **Atalhos Rápidos de Teclado:**
  * `F12` ou `Tab`: Abrir / Fechar Menu OSD do MiSTer.
  * `F5` ou `F2`: **Quick Save State** no slot ativo.
  * `F8` ou `F4`: **Quick Load State** do slot ativo.
  * `F6` / `F7`: Alternar Slot Anterior / Próximo (0 a 9).
  * `F9`: **Captura de Tela** em alta resolução salva na pasta `screenshots/`.
  * `Alt + Enter`: Alternar Modo Janela / Tela Cheia (*Fullscreen*).
* **Atalhos Rápidos no Controle (XInput / Xbox):**
  * `Select + R1 (RB)`: Salvar Estado Rápido.
  * `Select + L1 (LB)`: Carregar Estado Rápido.
  * `Start + Select` ou `Guide`: Alternar Menu OSD do MiSTer.
* **Pipeline de Áudio em Ring Buffer:** buffers circulares com reciclagem `WHDR_DONE`, detecção automática da taxa nativa da placa de som (WASAPI), recalibração a cada jogo carregado e fila de latência que cresce sozinha se detectar underruns recorrentes numa máquina mais lenta.
* **Ports & Recomp:** categoria própria no menu principal com jogos "recompilados" nativamente (tecnologia N64Recomp e afins) - baixa, extrai e abre a versão mais recente de cada projeto direto do GitHub, sem sair do app. Ver [CREDITS.md](CREDITS.md) para a lista completa e os repositórios de origem.
* **Shaders e Filtros CRT:**
  * Aspect Ratio: `Original` (4:3) / `Widescreen` (16:9).
  * Scanlines: `Off`, `25%`, `50%`, `CRT Shadow Mask (Trinitron RGB Grille)`.
  * Papéis de Parede: `Estática de TV Analógica`, `Campo de Estrelas 3D`, `Cyber Grid`, `Deep Black`.
  * 6 Temas de Cor OSD: `Red / Burgundy`, `Classic Blue`, `Matrix Green`, `Amber CRT`, `Arcade Gray`, `Dark Obsidian`.

---

## 🏆 RetroAchievements

Integração via [rcheevos](https://github.com/RetroAchievements/rcheevos) v12.4.0 (MIT),
em `third_party/rcheevos/`.

Para ativar, preencha `MiSTer_Win32_Port/Config/retroachievements.cfg`:

```
username=SeuUsuarioRA
password=suaSenha
```

No primeiro login bem-sucedido o token é gravado e **a senha é apagada do
arquivo** — ela não fica em disco depois disso.

* `hardcore=1` segue a regra do site e **bloqueia savestates**. Comece com `0`.
* Cores sem `retro_get_memory_data` não suportam conquistas. O `n64.dll` atual
  é um build reduzido e não exporta essa função — para N64 com conquistas, use
  o `mupen64plus_next` do buildbot oficial do libretro.
* Jogos traduzidos ou com hack normalmente não são reconhecidos, porque a
  identificação é por hash do conteúdo original.

---

## ⚠️ Antes de publicar o repositório

**Não versione BIOS nem ROMs.** Redistribuí-las é violação de direito autoral. O
`.gitignore` cobre `bios/`, `roms/`, `cores/`, `saves/` e `cache/` — confira com
`git check-ignore -v <arquivo>` antes de adicionar qualquer coisa nova.

**Há um token de conta no histórico.** O commit `02c7fb4` inclui
`Config/retroachievements.cfg` com o token do RetroAchievements preenchido, e
esse commit já foi enviado para o remoto. O arquivo foi removido do rastreamento
e o diretório `Config/` está ignorado, mas **isso não apaga o passado**: o token
continua recuperável no histórico.

Antes de tornar o repositório público:

1. **Invalide o token** — troque a senha da conta no RetroAchievements. Isso
   revoga o token antigo e é o único passo que realmente resolve, porque o
   histórico é imutável.
2. **Reescreva o histórico** com `git filter-repo` (ou comece um repositório
   novo) para remover o arquivo dos commits antigos.

Fazer só o passo 2 sem o 1 não basta: quem já clonou continua com o token.

---

## 💾 BIOS

Cada core procura a BIOS por um nome exato e num lugar exato. Os arquivos abaixo
foram verificados por MD5 e confirmados bootando.

| Sistema | Onde | Arquivos | Estado |
| :--- | :--- | :--- | :--- |
| **Mega CD** | `bios/` | `bios_CD_U.bin`, `bios_CD_E.bin`, `bios_CD_J.bin` | ✅ |
| **TurboGrafx CD** | `bios/` | `syscard3.pce` | ✅ |
| **PlayStation** | `bios/` | `scph5502.bin` (PAL) | ⚠️ ver nota |
| **NeoGeo CD** | `bios/neocd/` | `neocd_f.rom`, `neocd_t.rom`, `neocd_z.rom`, `uni-bioscd.rom` | ✅ |
| **NeoGeo AES/MVS** | `bios/` | `neogeo.zip`, `aes.zip` | ✅ |
| **Saturn** | `bios/` | `sega_101.bin`, `mpr-17933.bin` | ✅ |
| **GameCube** | `bios/dolphin-emu/Sys/` | pacote de dados do Dolphin (5,6 MB) | ⚠️ core instável |
| **Master System / GG** | `bios/` | `bios_U.sms`, `bios_E.sms`, `bios_J.sms`, `bios.gg` | opcional |
| **MSX** | `bios/` | `MSX.ROM`, `MSX2.ROM`, `MSX2EXT.ROM`, `MSX2P.ROM`, `MSX2PEXT.ROM` | ✅ ver nota |

**Escolha pelo hash, não pelo nome.** Sets de BIOS trazem muitas variantes com
nomes parecidos — o set de Mega CD tinha treze arquivos, incluindo um marcado
`(Non-Working)` e três variantes de USA, e só três batiam com o que o Genesis
Plus GX valida. O que parecia "mais novo e melhor" pelo nome não era o correto.

**PlayStation é regional.** O core busca `scph5501.bin` (NTSC-U), `scph5500.bin`
(NTSC-J) ou `scph5502.bin` (PAL) conforme o jogo. Com apenas a PAL instalada, um
jogo americano boota mas o core avisa `Firmware is missing: scph5501.bin` — vale
completar o conjunto.

**NeoGeo CD tem subdiretório próprio.** Os `.rom` soltos em `bios/` fazem o core
falhar com *"No BIOS detected!"*; eles têm de estar em `bios/neocd/`.

**MSX (fMSX) precisa de 5 arquivos, um por modo/geração.** `MSX.ROM` (MSX1),
`MSX2.ROM` + `MSX2EXT.ROM` (MSX2) e `MSX2P.ROM` + `MSX2PEXT.ROM` (MSX2+) são
cada um "Required" para o respectivo modo — o core não tem fallback entre eles.
Verificados por MD5 contra [docs.libretro.com/library/fmsx](https://docs.libretro.com/library/fmsx/#bios):

| Arquivo | MD5 esperado |
| :--- | :--- |
| `MSX.ROM` | `364a1a579fe5cb8dba54519bcfcdac0d` |
| `MSX2.ROM` | `ec3a01c91f24fbddcbcab0ad301bc9ef` |
| `MSX2EXT.ROM` | `2183c2aff17cf4297bdb496de78c2e8a` |
| `MSX2P.ROM` | `847cc025ffae665487940ff2639540e5` |
| `MSX2PEXT.ROM` | `7c8243c71d8f143b2531f01afa6a05dc` |

Opcionais, não necessários pra rodar jogos comuns: `DISK.ROM` (disquete/BDOS),
`FMPAC.ROM` (cartucho de som FM), `MSXDOS2.ROM`, `PAINTER.ROM`, `KANJI.ROM`.

--- | :--- | :--- |
| **NeoGeo CD** (`neocd_alt.dll`) | `bios/neocd/` | `neocd_f.rom`, `neocd_t.rom`, `neocd_z.rom`, `uni-bioscd.rom`, `ng-lo.rom` |
| **NeoGeo AES/MVS** (`neogeo.dll`) | `bios/` | `neogeo.zip`, `aes.zip` |
| **Saturn** | `bios/` | `sega_101.bin`, `mpr-17933.bin`, `mpr-18811-mx.ic8` |

O core de NeoGeo CD falha com *"No BIOS detected!"* se os `.rom` estiverem soltos
em `bios/` em vez do subdiretório `neocd/`.

---

## 📁 Estrutura do Projeto

```
SaborMister/
├── src/                    Código-fonte (.cpp)
│   ├── main_win32.cpp      Janela, loop de apresentação, entrada e HUD
│   ├── core_runner.cpp     Thread do core libretro, áudio e escalonamento
│   ├── menu.cpp            Navegação do OSD e seleção de core por sistema
│   ├── osd.cpp             Buffer do OSD portado do Main_MiSTer
│   ├── charrom.cpp         Fonte 8x8 do MiSTer
│   ├── archive_helper.cpp  Extração de ZIP/7Z/RAR e escolha do arquivo do jogo
│   └── netplay.cpp         Netplay via UDP
├── include/                Cabeçalhos (.h), incluindo libretro.h
├── build/                  Objetos intermediários (.obj) — gerado
├── docs/                   Capturas e material de referência
└── MiSTer_Win32_Port/      Pasta de execução
    ├── MiSTer_Win32.exe    Binário final
    ├── cores/              DLLs libretro
    ├── bios/               BIOS por sistema (NeoGeo CD em bios/neocd/)
    ├── roms/               Jogos, organizados por sistema
    ├── saves/              Savestates e memória de cartão
    ├── cache/              Arquivos extraídos de pacotes compactados
    └── Wallpapers/         Papéis de parede do menu
```

O executável precisa ficar dentro de `MiSTer_Win32_Port/`: ele localiza `cores/`,
`bios/` e `saves/` a partir da própria pasta.

---

## 🛠️ Como Compilar

Requisitos: **MSVC BuildTools (C++17 x64)**

```cmd
compile_port.bat
```

O script se orienta pela própria localização, então o projeto pode ser clonado em
qualquer diretório. Os `.obj` vão para `build/` e o executável final é gravado em
`MiSTer_Win32_Port/MiSTer_Win32.exe`.

---

## 📜 Licença e Créditos

* **Canal do YouTube:** [@GuhClemente](https://youtube.com/@GuhClemente)
* **Desenvolvedor:** Guh Clemente
* **Engine de Emulação:** Libretro API Architecture
* **Licença:** Código aberto e público para todos os entusiastas e comunidade retrogamer!
