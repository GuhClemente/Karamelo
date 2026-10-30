# Guia de Porte: Karamelo para PlayStation Vita Homebrew (`.vpk`)

Este documento estabelece o roteiro técnico, requisitos de compilação e arquitetura para a criação do porte nativo do **Karamelo Emulador** para o console portátil **PlayStation Vita (PS Vita / PS TV)** executando via HENkaku / VitaDeploy.

---

## 1. Por que o PlayStation Vita?

O PlayStation Vita (lançado em 2011) é um dos consoles portáteis mais queridos e ativos da cena de homebrew mundial:
* **Tela OLED Espetacular**: O modelo original (PCH-1000) possui tela OLED de 5 polegadas (960x544), ideal para a estética retrô, scanlines e shaders CRT do Karamelo.
* **Ergonomia e Controles de Fábrica**: D-Pad de altíssima precisão, dois direcionais analógicos, botões frontais, gatilhos L/R e tela touch.
* **Ecossistema Homebrew Amadurecido**: Suporte total a homebrew com HENkaku (firmware 3.60/3.65), gerenciador VitaDeploy e centenas de ports de C/C++ ativos.

---

## 2. PSP vs PS Vita: Por que o Vita é Compatível e o PSP não?

Uma dúvida comum é por que o Karamelo não suporta o PSP original (2004), mas suporta perfeitamente o PS Vita:

| Especificação | Sony PSP (2004) | Sony PS Vita (2011) | Impacto no Karamelo |
| :--- | :--- | :--- | :--- |
| **Memória RAM** | **32 MB** (ou 64 MB no PSP 2000+) | **512 MB de RAM + 128 MB VRAM** | O PSP estoura a memória antes de abrir o menu; o Vita tem 10x mais memória, comportando UI + Cores + ROMs. |
| **Processador (CPU)** | MIPS R4000 @ 333 MHz (Single-core) | **ARM Cortex-A9 Quad-Core @ 444 MHz** | O Vita possui poder de processamento multithread para rodar o frontend e cores de 16/32-bit a 60 FPS. |
| **Pipeline Gráfico** | Hardware gráfico legado proprietário (sem OpenGL) | **PowerVR SGX543MP4+ (VitaGL / OpenGL ES 2.0)** | Permite renderizar a interface e shaders do Karamelo via VitaGL com aceleração por hardware. |
| **Compilador / Toolchain** | MIPS GCC antigo (C++20 problemático) | **VitaSDK (GCC 10/12/13 com C++20 nativo)** | O código moderno em C++20 do Karamelo compila perfeitamente na toolchain do Vita. |
| **Carregamento Dinâmico** | PRX limitado | Módulos `.suprx` e carregamento de cores | Permite carregar motores Libretro como bibliotecas. |

---

## 3. Toolchain e Ambiente de Compilação

O desenvolvimento e empacotamento para o Vita utilizam a toolchain oficial da comunidade: **VitaSDK**.

### 3.1 Componentes Principais
* **VitaSDK (arm-vita-eabi-gcc / g++)**: Compilador cruzado com suporte a C++20 (`-std=c++20`).
* **VitaGL**: Reimplementação aberta de OpenGL ES 2.0 escrita por *Rinnegatamante*, mapeando chamadas gráficas diretamente para a GPU PowerVR do Vita com taxa de quadros máxima.
* **SDL2 / SDL3 para Vita**: Camada de entrada, janelas e eventos mapeada para os controles e tela do console.
* **vita-pack-vpk & vita-mksfoex**: Ferramentas para gerar o arquivo de metadados `param.sfo` e o arquivo instalável `.vpk`.

### 3.2 Instalação da Toolchain (Linux / WSL / macOS)
```bash
# Download e instalacao automatica do VitaSDK
git clone https://github.com/vitasdk/vdpm.git
cd vdpm
./bootstrap-vitasdk.sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH

# Instalacao das bibliotecas necessarias
./install-all.sh
```

Bibliotecas adicionais recomendadas no VitaSDK:
```bash
vdpm vitagl libpng libjpeg-turbo freetype zlib
```

---

## 4. Motores de Emulação (Cores do PS Vita)

O projeto Libretro já possui suporte oficial diário para o PS Vita em seu Buildbot:

* **Repositório do Buildbot Libretro para Vita**:
  ```
  https://buildbot.libretro.com/nightly/playstation/vita/
  ```

### 4.1 Cores Recomendados para o Karamelo no Vita:
* **Arcade / Neo Geo**: `fbneo_libretro.vpk / .a` (Metal Slug, The King of Fighters a 60 FPS).
* **Super Nintendo**: `snes9x2005_libretro` ou `snes9x2010_libretro` (Full speed cravado).
* **Mega Drive / Genesis / Master System**: `genesis_plus_gx_libretro` e `picodrive_libretro`.
* **Game Boy / GBC / GBA**: `gambatte_libretro` e `gpsp_libretro` (com dynarec ARM a 60 FPS).
* **PlayStation 1**: `pcsx_rearmed_libretro` (com renderizador acelerado por NEON).
* **PC Engine / TurboGrafx-16**: `mednafen_pce_fast_libretro`.
* **Atari / Commodore / MSX**: Cores clássicos rodam com consumo inferior a 15% de CPU.

---

## 5. Estrutura do Pacote Instalável (`Karamelo.vpk`)

O arquivo `.vpk` é um arquivo ZIP padronizado que o VitaShell instala na partição `ux0:app/KARAM0001/`:

```
Karamelo.vpk
├── eboot.bin                   # Binario executavel ELF compilado e assinado
├── param.sfo                   # Titulo, ID (KARAM0001) e permissoes
├── sce_sys/
│   ├── icon0.png               # Icone no menu LiveArea do Vita (128x128)
│   ├── bg.png                  # Papel de parede de fundo no LiveArea (848x560)
│   ├── startup.png             # Imagem de abertura
│   └── livearea/contents/
│       ├── template.xml        # Layout da pagina LiveArea
│       └── bg.png
├── data/
│   └── ports.json              # Catalogo de recompilacoes
└── cores/                      # Motores Libretro homologados
```

---

## 6. Otimizações Específicas para o Vita
1. **Controle de Clock da CPU**: Usar a API do sistema `scePowerSetArmClockFrequency(444)` para manter o processador em 444 MHz (clock máximo estável de fábrica sem drenar bateria).
2. **Resolução de Renderização**: A tela do PS Vita possui resolução nativa de **960x544** (proporção 16:9). Jogos em 4:3 (como SNES 256x224 ou PS1 320x240) são escalados perfeitamente com bordas verticais ou com a moldura retrô do Karamelo.
3. **Áudio de Baixa Latência**: Utilizar a API nativa `SceAudio` com buffer duplo de 512 amostras a 44.1 kHz / 48 kHz.
