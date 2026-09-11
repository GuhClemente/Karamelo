# AGENTS.md — Karamelo Emulador Guidelines for AI Agents

Este documento é o guia de referência operacional e arquitetural para agentes de IA (incluindo Antigravity, Gemini e assistentes autônomos) trabalhando no repositório **Karamelo Emulador**.

---

## 1. Identidade e Princípios Centrais

* **Nome do Projeto**: Karamelo Emulador (nome curto: **Karamelo**).
* **Domínio Oficial**: `karamelo-emu.com`
* **Repositório**: `https://github.com/GuhClemente/Karamelo`
* **Autor**: Guh Clemente (YouTube: [@GuhClemente](https://youtube.com/@GuhClemente))
* **Licença**: **GNU General Public License v3.0 (GPL-3.0)**.
* **Nomes Antigos Proibidos**: O projeto chamava-se *MiSTer 4 ALL* até 08/09/2026. **Nunca use essa nomenclatura** nem variações antigas (*MiSTer Flavor*, *Sabor MiSTer*, *Sabor Mister Arcade Edition*).
* **Relação com MiSTer FPGA**: O Karamelo é inspirado visualmente na interface e OSD do MiSTer, mas é uma **implementação própria e clean-room escrita do zero em C++20**. **Não é porte de código do Main_MiSTer**. Veja detalhes em `docs/FRONTEND.md`.
* **Multiplataforma Nativa**:
  - **Windows**: x86_64 nativo (Win32 API, Direct3D 11, WASAPI).
  - **Linux**: x86_64 nativo (SDL3, OpenGL, ALSA/Pulse/PipeWire via SDL).
  - **macOS**: ARM64 nativo Apple Silicon M1/M2/M3/M4 (SDL3, Cocoa, Metal/OpenGL).

---

## 2. Regras de Ouro (Invioláveis)

1. **Fontes Únicas da Verdade (Não Invente Números)**:
   - Contagem de Sistemas e Motores: Leia SEMPRE de `include/app_info.h` (`APP_SYSTEM_COUNT = 35`, `APP_CORE_ENGINES = 40`). Nunca use `APP_CORE_FILES` (41) pois conta duplicata técnica de N64.
   - Contagem e Compatibilidade de Ports: Leia SEMPRE de `CREDITS.md` e verifique em `src/port_runner.cpp` (42 ports totais: 42 Windows 🪟, 19 macOS 🍎, 16 Linux 🐧).
   - Guia de BIOS: A fonte canônica é `packaging/bios-guide/BIOS_NECESSARIOS.txt`.
2. **Higiene de Repositório**:
   - **NUNCA versione ROMs, BIOS, saves, estados ou caches**. O `.gitignore` cobre `roms/`, `bios/`, `saves/`, `cores/`, `cache/`.
   - **NUNCA versione credenciais ou IPs de produção sensíveis**. Arquivos `deploy_env.sh` e `deploy_env.bat` permanecem no `.gitignore`.
3. **Sincronia Obrigatória com o Site**:
   - Qualquer alteração em contagens de sistemas, cores, suporte a SOs ou compatibilidade de ports **DEVE ser refletida imediatamente em `docs/SITE_SYNC.md`**, que é a especificação consumida pela IA do site `karamelo-emu.com`.

---

## 3. Arquitetura do Código-Fonte

```
src/
├── main_win32.cpp         # Loop de eventos Win32, D3D11 swapchain, renderizador e XInput (Windows)
├── main_linux.cpp         # Loop de eventos SDL3, renderizador e gamepads (Linux e macOS)
├── core_runner.cpp        # Thread do motor Libretro, Ring Buffer de áudio, shaders CRT e aspect ratio
├── menu.cpp               # Máquina de estados do menu OSD, navegador de arquivos e resolução de cores
├── osd.cpp                # Rasterizador do OSD MiSTer-like (clean-room bitmap buffer)
├── charrom.cpp            # Fonte bitmap 8x8 monocromática embutida
├── port_runner.cpp        # Subsistema de PC Ports & Recompilados (GitHub Releases, extração e execução)
├── archive_helper.cpp     # Descompactador transparente (ZIP, 7Z, RAR) e cache de ROMs
├── chd_reader.cpp         # Leitor e parser de imagens compactadas CHD (para RetroAchievements)
├── hw_render.cpp          # Contexto OpenGL para cores com renderização por hardware (Mupen64Plus, etc.)
├── input_map.cpp          # Mapeamento dinâmico de teclado e controles
├── netplay.cpp            # Netplay P2P via UDP
├── netplay_protocol.cpp   # Protocolo e framing de pacotes de sincronização de rede
├── retroachievements.cpp  # Integração nativa com rcheevos v12.4.0
├── updater.cpp            # Auto-atualizador multiplataforma consumindo version.json
└── karamelo_math.cpp      # Cálculos de aspect ratio, viewport integer-scaling e scanlines
```

---

## 4. Pipeline de Compilação, Empacotamento e Deploy

### macOS (Apple Silicon ARM64)
- **Compilar**: `./compile_macos.sh` (Gera `app/Karamelo` e executa a suíte de testes).
- **Testar Ports**: `./app/Karamelo --list-ports` (Lista todos os ports compatíveis com macOS).
- **Empacotar**: `./package_macos.sh` (Gera `dist/Karamelo_v0.9.4_macOS_arm64.tar.gz` e `dist/Karamelo_mac`).
- **Deploy**: `./deploy_macos.sh` (Gera artefatos, atualiza `dist/version.json` e envia via SCP para `187.127.59.127:/data/downloads/`).
- **Baixar Cores**: `./download_cores_macos.sh` (Baixa os 39 cores `.dylib` ARM64 para `app/cores/`).

### Linux (x86_64)
- **Compilar**: `./compile_linux.sh`
- **Empacotar**: `./package_linux.sh` (Gera `dist/Karamelo_v0.9.4_Linux64.tar.gz`).
- **Deploy**: `./deploy_linux.sh`
- **Baixar Cores**: `./download_cores_linux.sh`

### Windows (x86_64)
- **Compilar**: `compile_port.bat` (MSVC C++20 x64, gera `app\Karamelo_v<versão>.exe`).
- **Empacotar**: `package_release.bat` (Gera `dist/Karamelo_v<versão>_Win64.zip`).
- **Deploy**: `deploy_all.bat` ou `upload_to_server.bat`.

---

## 5. Subsistema de PC Ports & Recompilados (`port_runner.cpp`)

- **Contagem total**: 42 ports catalogados.
- **Suporte macOS**: 19 ports com executáveis nativos Mach-O (`ARM64` > `Universal` > `x86_64`).
- **Regras de Execução no macOS**:
  - `FindBestExecutable()` prioriza binários com permissão de execução (`chmod +x`), descartando utilitários como `crashpad_handler` ou `make`.
  - Extração de `.app` bundles: detecta executáveis dentro de `Contents/MacOS/<nome>`.
  - Extração de formatos: suporta `.zip`, `.tar.gz`, `.tar.xz`.
  - Flag de inspeção: `./app/Karamelo --list-ports` lista os 19 ports disponíveis no macOS.

---

## 6. Documentos de Referência Obrigatórios

- `docs/SITE_SYNC.md`: Briefing mestre para a IA mantenedora do site `karamelo-emu.com`.
- `CREDITS.md`: Catálogo completo de motores, cores e ports com seus repositórios e selos de SO.
- `README.md`: Documentação voltada aos usuários e desenvolvedores.
- `docs/FRONTEND.md`: Especificação da arquitetura limpa do frontend e compatibilidade de OSD.
- `CLAUDE.md`: Instruções específicas para Claude e agentes auxiliares.
