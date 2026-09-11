# CLAUDE.md — Instructions for Claude / Claude Code

This file provides project guidelines, architecture details, and essential commands for Claude working on the **Karamelo Emulador** codebase.

---

## 1. Project Overview & Philosophy

* **Project**: **Karamelo Emulador** (short name: **Karamelo**).
* **Website**: `karamelo-emu.com`
* **Repository**: `https://github.com/GuhClemente/Karamelo`
* **Author**: Guh Clemente (YouTube: [@GuhClemente](https://youtube.com/@GuhClemente))
* **License**: **GNU General Public License v3.0 (GPL-3.0)**.
* **Former Names**: The project was named *MiSTer 4 ALL* until 2026-09-08. **Never use the former name or variations** (*MiSTer Flavor*, *Sabor MiSTer*, *Sabor Mister Arcade Edition*).
* **Relationship to MiSTer FPGA**: Visual inspiration only for OSD and layout. **All frontend and OSD code is an original, clean-room C++20 implementation written from scratch** (`docs/FRONTEND.md`). It is NOT a port of `Main_MiSTer`.
* **Multiplatform Support**:
  - **Windows x64**: Native Win32 API, Direct3D 11, WASAPI audio.
  - **Linux x64**: Native SDL3, OpenGL, threaded audio ring buffer.
  - **macOS ARM64**: Native Apple Silicon M1-M4 (SDL3, Cocoa, Metal/OpenGL).

---

## 2. Core Rules & Single Sources of Truth

1. **Never Invent Counts or Numbers**:
   - Systems & Engines: Read from `include/app_info.h` (`APP_SYSTEM_COUNT = 35`, `APP_CORE_ENGINES = 40`). Never report `APP_CORE_FILES` (41) as an engine count.
   - Ports & Recomp: Source of truth is `CREDITS.md` and `src/port_runner.cpp` (Total: 42 ports — 42 Windows 🪟, 19 macOS 🍎, 16 Linux 🐧).
   - BIOS Information: Source of truth is `packaging/bios-guide/BIOS_NECESSARIOS.txt`.
2. **Repository Hygiene**:
   - **Never commit BIOS files, ROMs, saves, or runtime caches**. The `.gitignore` enforces this (`bios/`, `roms/`, `saves/`, `cache/`, `cores/`).
   - **Never commit production secrets**. `deploy_env.sh` and `deploy_env.bat` are gitignored.
3. **Keep `docs/SITE_SYNC.md` in Sync**:
   - This document is the explicit briefing used by the website's AI. Any change to system counts, core engines, supported OSs, or ports must be updated in `docs/SITE_SYNC.md`.

---

## 3. Essential Commands

### macOS (Apple Silicon ARM64)
```bash
# Build the application and run unit tests
./compile_macos.sh

# Test port runner availability
./app/Karamelo --list-ports

# Package distribution archive (dist/Karamelo_v<version>_macOS_arm64.tar.gz)
./package_macos.sh

# Deploy to production download server
./deploy_macos.sh

# Download native ARM64 Libretro cores (.dylib)
./download_cores_macos.sh
```

### Linux (x86_64)
```bash
# Build
./compile_linux.sh

# Package distribution
./package_linux.sh

# Deploy
./deploy_linux.sh

# Download cores (.so)
./download_cores_linux.sh
```

### Windows (x86_64)
```cmd
:: Build with MSVC (C++20 x64)
compile_port.bat

:: Package distribution zip
package_release.bat

:: Deploy to server
deploy_all.bat
:: or
upload_to_server.bat
```

---

## 4. Key Subsystems & Architecture

* **`src/main_win32.cpp`**: Win32 window management, Direct3D 11 swapchain, raw input, and WASAPI audio ring buffer.
* **`src/main_linux.cpp`**: SDL3 window, event pump, gamepad handling, and renderer for Linux and macOS.
* **`src/core_runner.cpp`**: Libretro core hosting thread, audio resampler, aspect ratio calculations, scanlines and CRT shader pipeline. Loads `.dll` (Win), `.so` (Linux), `.dylib` (macOS).
* **`src/port_runner.cpp`**: PC Ports & Static Recomp runner. Fetches latest GitHub releases, matches platform assets, unpacks `.zip`/`.tar.gz`/`.tar.xz`, extracts `.app` bundles (`Contents/MacOS/`), resolves executable architectures (`ARM64` > `Universal` > `x86_64`), sets permissions (`chmod +x`), and launches the games.
* **`src/menu.cpp` & `src/osd.cpp`**: Clean-room raster OSD engine mimicking the MiSTer retro aesthetic without using GPL code from MiSTer.
* **`src/updater.cpp`**: Checks `https://karamelo-emu.com/downloads/version.json` for binary or full-package auto-updates across Windows, Linux, and macOS.
