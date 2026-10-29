# Guia de Porte: Karamelo para Nintendo Switch Homebrew (`.nro`)

Este documento estabelece o roteiro técnico, requisitos de compilação e arquitetura para a criação do porte nativo do **Karamelo Emulador** para o console **Nintendo Switch** (executando via Atmosphere / Homebrew Menu).

---

## 1. Por que o Nintendo Switch?

O Nintendo Switch é a plataforma de console dedicada mais adequada para o Karamelo:
* **Mesma Arquitetura (ARM64)**: O Switch utiliza o processador NVIDIA Tegra X1 (ARM Cortex-A57 de 64-bit). O Karamelo já é compilado e otimizado nativamente para ARM64 no macOS.
* **Potência de Sobra**: 4 GB de RAM unificada e GPU Maxwell, permitindo rodar com folga todos os sistemas clássicos até PlayStation 1, Nintendo 64, Dreamcast, Sega Saturn e PSP.
* **Modo Híbrido**: Resolução 720p na tela portátil (inclusive na tela OLED do modelo Switch OLED) e 1080p na TV via Dock.

---

## 2. Toolchain e Ambiente de Compilação

A compilação para o Switch é realizada através da toolchain padrão da cena homebrew: **devkitPro**.

### 2.1 Componentes Necessários
* **devkitA64**: Compilador GCC para arquitetura ARM64 (AArch64) com suporte completo a **C++20**.
* **libnx**: Biblioteca base de interface com o sistema operacional do Switch (Horizon OS / Atmosphere).
* **switch-tools**: Utilitários de empacotamento (`elf2nro`, `nacptool`).
* **switch-sdl3 / switch-sdl2**: Camada de janela, renderização OpenGL/Deeko3D e áudio.

### 2.2 Instalação da Toolchain
* No Windows via **devkitPro Installer** (MSYS2) ou no Linux/macOS via gerenciador de pacotes `dkp-pacman`:
  ```bash
  dkp-pacman -S switch-dev switch-sdl2 switch-mesa switch-glad switch-libpng switch-zlib switch-bzip2
  ```

---

## 3. Motores de Emulação (Cores do Switch)

O projeto Libretro compila oficialmente todos os cores diários para o Nintendo Switch através do seu Buildbot:

* **Repositório Oficial do Buildbot**:
  ```
  https://buildbot.libretro.com/nightly/nintendo/switch/libnx/latest/
  ```

### 3.1 Cores Testados e Homologados para Switch:
* **Super Nintendo**: `snes9x_libretro.nro` (60 FPS com filtros de scanlines).
* **Mega Drive / Master / 32X**: `genesis_plus_gx_libretro.nro` e `picodrive_libretro.nro`.
* **PlayStation 1**: `pcsx_rearmed_libretro.nro` e `beetle_psx_libretro.nro` (com upscale 2x/3x).
* **Nintendo 64**: `mupen64plus_next_libretro.nro` (60 FPS com alta fidelidade gráfica).
* **Dreamcast**: `flycast_libretro.nro` (60 FPS estável).
* **Sega Saturn**: `yabasanshiro_libretro.nro` e `beetle_saturn_libretro.nro`.
* **PSP**: `ppsspp_libretro.nro` (renderização na resolução nativa do Switch).
* **Arcade / Neo Geo**: `fbneo_libretro.nro` e `mame2003_plus_libretro.nro`.
* **Game Boy / GBC / GBA**: `gambatte_libretro.nro` e `mgba_libretro.nro`.

Os cores residem na pasta `cores/` do cartão SD e são carregados dinamicamente pelo frontend.

---

## 4. Suporte a PC Ports & Recompilados no Switch

O Nintendo Switch possui uma das comunidades mais ativas do mundo em portes de recompilações de código aberto. Vários ports presentes em `data/ports.json` já possuem versões executáveis nativas em formato `.nro`:

| Jogo / Port | Status no Switch | Binário / Projeto |
|---|---|---|
| **Ship of Harkinian** (Zelda: Ocarina of Time) | 🟢 Nativo 60 FPS | `soh.nro` oficial para Switch |
| **2 Ship 2 Harkinian** (Zelda: Majora's Mask) | 🟢 Nativo 60 FPS | `2soh.nro` oficial para Switch |
| **Super Mario 64** (sm64ex) | 🟢 Nativo Widescreen | `sm64ex.nro` |
| **Zelda: A Link to the Past** (zelda3 recomp) | 🟢 Nativo Widescreen | `zelda3.nro` |
| **Sonic 1, 2 e CD** (Decompilations) | 🟢 Nativo 60 FPS | `Sonic1.nro`, `Sonic2.nro`, `SonicCD.nro` |
| **Sonic Mania** | 🟢 Nativo 60 FPS | Decompilação nativa Switch |
| **DevilutionX** (Diablo 1) | 🟢 Nativo com controles | `devilutionx.nro` |
| **OpenLara** (Tomb Raider) | 🟢 Nativo 60 FPS | `OpenLara.nro` |
| **Fallout 1 Community Edition** | 🟢 Suporte a toque | `fallout1-ce.nro` |
| **Grand Theft Auto III / Vice City** (re3/reVC) | 🟢 Nativo 60 FPS | `re3.nro`, `reVC.nro` |

O catálogo `data/ports.json` pode ser expandido no futuro com a tag `"switch": { "download_url": "..." }` para baixar automaticamente a versão `.nro` correspondente.

---

## 5. Estrutura de Arquivos no Cartão MicroSD

Ao instalar o Karamelo no Switch, a pasta no cartão SD seguirá a estrutura padrão do ecossistema:

```
sdmc:/
└── switch/
    └── Karamelo/
        ├── Karamelo.nro         # Binário principal do frontend
        ├── Karamelo.nacp        # Metadados (Nome, Autor, Versão)
        ├── icon.jpg             # Ícone 256x256 no Homebrew Menu
        ├── karamelo.cfg         # Configurações do usuário
        ├── cores/               # Cores Libretro (.nro) baixados do Buildbot
        ├── roms/                # Pastas dos sistemas (SNES, MegaDrive, PS1...)
        ├── bios/                # Arquivos de BIOS obrigatórios
        └── saves/               # Saves de bateria (.srm) e estados
```

---

## 6. Particularidades de Implementação no C++

1. **Detecção de Dock (Portátil vs TV)**:
   - A biblioteca `libnx` fornece a função `appletGetOperationMode()`.
   - Quando o Switch é colocado no Dock (`AppletOperationMode_Docked`), o Karamelo ajusta a swapchain do SDL para **1920x1080**.
   - Quando retirado do Dock (`AppletOperationMode_Handheld`), ajusta para **1280x720** para máxima economia de bateria.
2. **Mapeamento de Joy-Cons**:
   - Mapear nativamente os botões `+`, `-`, `Home` e `Capture` (com proteção para o atalho de saída do emulador não fechar o app por engano).
3. **Suspensão e Retomada (Sleep Mode)**:
   - Implementar o tratamento de eventos `appletGetMessage()` para pausar a thread do emulador (`core_runner.cpp`) quando o usuário pressionar o botão Power do console.

---

## 7. Roteiro de Ativação

1. [ ] Configurar o ambiente devkitPro no computador de desenvolvimento.
2. [ ] Criar `Makefile.switch` integrando o `nacptool` e `elf2nro` para gerar o `Karamelo.nro`.
3. [ ] Criar o script `download_cores_switch.sh` para baixar a suíte de cores `.nro` do Buildbot.
4. [ ] Testar no emulador **Ryujinx / Yuzu** ou em um console físico desbloqueado.
