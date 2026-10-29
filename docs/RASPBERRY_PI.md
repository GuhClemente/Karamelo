# Guia de Porte: Karamelo para Raspberry Pi & SBCs ARM64

Este documento estabelece o roteiro técnico, arquitetura e diretrizes para compilação e execução nativa do **Karamelo Emulador** na família **Raspberry Pi (Pi 4, Pi 400, Pi 5, Compute Module 4, Pi Zero 2 W)** e computadores de placa única (SBCs) como **Orange Pi, Radxa Rock e TV Boxes**.

---

## 1. Por que o Karamelo é Perfeito para o Raspberry Pi?

As soluções tradicionais para Raspberry Pi (como RetroPie, Recalbox e Batocera) sofrem com os limites de hardware do Pi:
* Dependem de interfaces pesadas em Python ou EmulationStation com scripts intermediários.
* Consomem mais de 300 MB a 500 MB de RAM só para manter a interface aberta.
* Sofrem com engasgos na navegação de listas e tempo de carregamento perceptível.

O **Karamelo** resolve isso pela própria raiz da sua arquitetura:
1. **Binário Único em C++20 Puro**: Consumo de ~25 MB a 30 MB de RAM.
2. **Boot Instantâneo**: Inicializa direto no menu em menos de 1 segundo.
3. **KMS/DRM Direto via SDL3 (Sem X11/Wayland)**: Renderiza direto no framebuffer da GPU VideoCore com latência zero e sem overhead de servidor de janelas.
4. **OSD MiSTer FPGA Nativo**: Menus bitmap monocromáticos ultra-leves que não drenam a GPU.
5. **Karamelo Studio Integrado**: Gerenciamento completo de ROMs e BIOS via Wi-Fi pelo celular/PC sem precisar plugar teclado ou mouse no Raspberry Pi.

---

## 2. Dispositivos e Arquiteturas Suportadas

| Dispositivo | SoC / Processador | Arquitetura | GPU | Status / Desempenho |
| :--- | :--- | :--- | :--- | :--- |
| **Raspberry Pi 5** | Broadcom BCM2712 (4x Cortex-A76 @ 2.4GHz) | `aarch64` (ARM64) | VideoCore VII (Vulkan 1.3, GLES 3.1) | **Desempenho Total** (60 FPS cravado até Dreamcast/Saturn/PS2) |
| **Raspberry Pi 4 / 400** | Broadcom BCM2711 (4x Cortex-A72 @ 1.8GHz) | `aarch64` (ARM64) | VideoCore VI (GLES 3.1, Vulkan) | **Excelente** (Full speed em todos os 16/32-bit, N64 e PS1) |
| **Raspberry Pi Zero 2 W** | Broadcom BCM2710A1 (4x Cortex-A53 @ 1.0GHz) | `aarch64` (ARM64) | VideoCore IV | **Ótimo** para Arcade, SNES, Mega Drive, GBA e PS1 |
| **Orange Pi 5 / 5 Plus** | Rockchip RK3588 (4x A76 + 4x A55) | `aarch64` (ARM64) | Mali-G610 MP4 (Panfrost) | **Extremo** (4K 60FPS com shaders CRT pesados) |
| **TV Boxes (Amlogic)** | S905X3 / S905X4 (4x Cortex-A55) | `aarch64` (ARM64) | Mali-G31 MP2 | **Console Retrô Econômico** de baixíssimo custo |

---

## 3. Stack Gráfica e Modo Sem Janelas (KMS/DRM)

No Raspberry Pi OS Lite (sem desktop gráfico instalado), o SDL3 permite renderização de alto desempenho via **KMS/DRM (Kernel Mode Setting / Direct Rendering Manager)**:

```
[ Karamelo Frontend (C++20) ]
              │
              ▼
    [ SDL3 Video Subsystem ]
              │
   ┌──────────┴──────────┐
   ▼                     ▼
[ Linux KMS/DRM ]   [ EGL / GLES 3.1 ]
   │                     │
   ▼                     ▼
[ DRM Framebuffer ] [ Mesa V3D / VideoCore ]
              │
              ▼
       [ Saída HDMI TV ]
```

* **Vantagem**: Menor latência de vídeo possível no ecossistema Linux (igual ao MiSTer FPGA via HDMI).
* **Consumo de CPU**: Próximo de 0% de overhead para o sistema operacional.

---

## 4. Cores Libretro para Linux ARM64 (`aarch64`)

O ecossistema Libretro já compila e mantém nativamente todos os motores para `linux/aarch64` em formato de bibliotecas compartilhadas `.so`:

* **Repositório Oficial do Buildbot Libretro**:
  ```
  https://buildbot.libretro.com/nightly/linux/aarch64/
  ```

Os motores utilizam as mesmas convenções do Karamelo x86_64, com extensões `.so`:
- `snes9x_libretro.so`
- `genesis_plus_gx_libretro.so`
- `mednafen_psx_hw_libretro.so`
- `mupen64plus_next_libretro.so`
- `flycast_libretro.so`
- `fbneo_libretro.so`

---

## 5. Script de Compilação Cruzada / Nativa (`compile_rpi.sh`)

Para compilar diretamente no Raspberry Pi (com Raspberry Pi OS 64-bit):

```bash
#!/bin/bash
set -e

echo "=== Compilando Karamelo para Raspberry Pi (ARM64) ==="

# Instalar dependências essenciais
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build libasound2-dev \
    libpulse-dev libpipewire-0.3-dev libdrm-dev libgbm-dev libegl1-mesa-dev \
    libgles2-mesa-dev libevdev-dev libudev-dev

# Flags otimizadas para Cortex-A72 (Pi 4) ou Cortex-A76 (Pi 5)
ARCH_FLAGS="-march=armv8-a+crc -mtune=cortex-a72"
if grep -q "Raspberry Pi 5" /proc/cpuinfo 2>/dev/null; then
    ARCH_FLAGS="-march=armv8.2-a+crypto -mtune=cortex-a76"
fi

# Compilar binário Karamelo
g++ -std=c++20 -O3 $ARCH_FLAGS -flto \
    -D_GNU_SOURCE -DSDL_PLATFORM_LINUX \
    -I include \
    -I third_party/SDL3/include \
    -I third_party/rcheevos/include \
    -I third_party/libchdr/include \
    src/*.cpp \
    third_party/rcheevos/src/*.c \
    third_party/rcheevos/src/rapi/*.c \
    third_party/rcheevos/src/rcheevos/*.c \
    third_party/rcheevos/src/rhash/*.c \
    -lSDL3 -lEGL -lGLESv2 -ldrm -lgbm -lasound -lpthread -ldl \
    -o app/Karamelo_rpi_arm64

echo "=== Build concluído com sucesso: app/Karamelo_rpi_arm64 ==="
```

---

## 6. Integração com o Karamelo OS (Imagem Live SD Card)

Além da versão executável para quem já usa o Raspberry Pi OS, o roadmap contempla o **Karamelo OS para Raspberry Pi**:

1. **Imagem de Cartão MicroSD Flasheável** (`.img.xz`):
   - Gravável em segundos via *Raspberry Pi Imager* ou *BalenaEtcher*.
   - Sistema operacional base minimalista (Kernel Linux + Busybox + Drivers Mesa V3D + SDL3 + Karamelo).
   - Tamanho da imagem base: **menos de 150 MB** (comparado aos 4 GB do Batocera ou RetroPie).
2. **Primeiro Boot Automatizado**:
   - Expansão automática do sistema de arquivos para ocupar todo o cartão MicroSD.
   - Ponto de acesso Wi-Fi próprio (*Karamelo-Setup*) para configurar o Wi-Fi doméstico em 10 segundos caso não esteja no cabo de rede.
   - Inicialização do **Karamelo Studio** na porta `8080`, permitindo enviar os jogos do celular para o Raspberry Pi deitado no sofá.
3. **GPIO & Controles de Gabinete**:
   - Mapeamento nativo dos pinos GPIO do Raspberry Pi para permitir colocar o Pi dentro de bartops e máquinas arcade sem precisar de placa controladora USB intermediária.

