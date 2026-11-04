# Pipeline de Gravação de Mídia Autônomo e Multiplataforma (Karamelo)

Este documento descreve a arquitetura, os scripts e os procedimentos para geração automatizada e padronizada de vídeos em alta definição e capturas para o **YouTube**, **Redes Sociais** e para o site oficial **karamelo-emu.com**.

O pipeline foi projetado para operar de forma nativa e idêntica em **Windows (x64)**, **Linux (x86_64)** e **macOS (Apple Silicon ARM64)**, abrangendo todos os 42 motores de emulação e 37 sistemas do Karamelo.

---

## 1. Princípios Centrais do Pipeline

1. **Determinismo Absoluto (Attract Mode)**:
   - Ao executar a gravação no modo `--no-input`, o gravador não envia nenhum comando de botão para o core.
   - O jogo inicia e executa o ciclo natural de demonstração (*Attract Mode* / cinemáticas pré-programadas da ROM).
   - Isso garante que gravações feitas com diferentes cores, filtros ou opções de upscaling apresentem **a exata mesma cena segundo a segundo**, permitindo montagens perfeitas de *split-screen* lado a lado no Premiere, DaVinci Resolve ou Final Cut sem descompasso de quadros.

2. **Qualidade Máxima com Espaço Controlado**:
   - Vídeo em H.264 com **CRF 18** (nível visualmente *lossless*, ideal para reedição sem perda de geração).
   - Áudio em **AAC 320 kbps estéreo**, gravado diretamente do callback de áudio nativo do core (sem compressão prévia nem ruído de captura de tela).
   - Resolução em **1280×720** (aspect ratio original de 4:3 com pillarbox lateral limpo).
   - Consumo médio de disco: **~40 MB a 55 MB por vídeo de 90 segundos**.

3. **Multiplataforma Nativa**:
   - Windows: `tools/record_gameplay/build.bat` (MSVC C++20).
   - Linux: `tools/record_gameplay/build_linux.sh` (GCC / Clang).
   - macOS: `tools/record_gameplay/build_macos.sh` (Clang ARM64 Apple Silicon).

---

## 2. Como Compilar o Gravador (`record_gameplay`)

O gravador headless reaproveita o motor `core_runner.cpp` e o renderizador do Karamelo, dispensando a interface gráfica completa e os subsistemas de rede para rodar rápido e silencioso em segundo plano.

### Windows (MSVC)
```bat
tools\record_gameplay\build.bat
```
*Gera o executável em `tools\record_gameplay\out\record_gameplay.exe`.*

### Linux (Ubuntu / Debian / Fedora)
```bash
chmod +x tools/record_gameplay/build_linux.sh
./tools/record_gameplay/build_linux.sh
```
*Gera o executável em `tools/record_gameplay/out/record_gameplay`.*

### macOS (Apple Silicon M1/M2/M3/M4)
```bash
chmod +x tools/record_gameplay/build_macos.sh
./tools/record_gameplay/build_macos.sh
```
*Gera o executável em `tools/record_gameplay/out/record_gameplay`.*

---

## 3. Uso da CLI (`record_gameplay`)

A sintaxe de linha de comando é padronizada:

```bash
record_gameplay <core_dll> <rom_path> <duracao_segundos> <dir_saida> [nome_base] [modo_upscale] [modo_filtro] [sem_input] [pular_extras]
```

### Parâmetros:
1. `core_dll`: Caminho para o core (`.dll`, `.so` ou `.dylib`).
2. `rom_path`: Caminho para o arquivo da ROM.
3. `duracao_segundos`: Duração exata do clipe em segundos (ex.: `90`).
4. `dir_saida`: Pasta onde o vídeo final será salvo (ex.: `videos_comparativo_n64`).
5. `nome_base`: Prefixo do arquivo de saída (ex.: `01_waverace_gopher_2x`).
6. `modo_upscale`: Modo de upscaling horizontal do N64 (apenas Gopher64):
   - `0`: Desligado (320×240 nativo).
   - `1`: 2x Horizontal (640×240 nativo).
   - `2`: 4x Ultra Horizontal (1280×240 supersampled).
7. `modo_filtro`: Shader de vídeo:
   - `0`: Raw (Pixels puros, sem shader — recomendado para comparar geometria).
   - `1`: Bilinear suave.
   - `8`: CRT Scanlines Leve.
8. `sem_input`: Modo determinístico:
   - `1`: **Ativado (Attract Mode puro, nenhum botão pressionado)**.
   - `0`: Injeta rotinas automáticas de botões para jogos específicos.
9. `pular_extras`:
   - `1`: **Gera diretamente o `.mp4` de alta qualidade**, pulando geração de capturas BMP intermediárias e transcodificação para WebM.
   - `0`: Gera screenshots periódicos e versão WebM VP9.

---

## 4. Scripts Automatizados de Gravação

### 4.1. Bateria Dedicada de Nintendo 64 (`record_all_n64.py`)
Grava uma matriz completa com 4 jogos e 5 variantes por jogo (total de 20 vídeos de 90 segundos):
- **Jogos**: *Wave Race 64*, *Star Fox 64*, *Castlevania* e *James Bond (Perfect Dark)*.
- **Variantes**:
  1. `ParaLLEl N64` (Nativo 320×240)
  2. `Mupen64Plus-Next` (Nativo 320×240)
  3. `Gopher64` (Desligado 320×240)
  4. `Gopher64` (Upscale 2x 640×240)
  5. `Gopher64` (Upscale 4x Ultra 1280×240)

**Como rodar**:
```bash
python tools/record_gameplay/record_all_n64.py
```
*O script é idempotente: se for interrompido, ao rodar novamente ele pula os vídeos que já foram gravados com sucesso e retoma de onde parou.*

---

### 4.2. Suíte Universal para Todos os Cores (`record_suite.py`)
Permite gravar vídeos de qualquer sistema suportado pelo Karamelo.

#### Gravar uma ROM específica com qualquer Core:
```bash
# Gravar 60 segundos de Super Mario World no SNES:
python tools/record_gameplay/record_suite.py \
  --rom "roms/SNES/Super Mario World (USA).sfc" \
  --core cores/snes.dll \
  --duration 60

# Gravar 90 segundos de Sonic the Hedgehog no Mega Drive:
python tools/record_gameplay/record_suite.py \
  --rom "roms/Genesis/Sonic The Hedgehog (USA, Europe).md" \
  --core cores/genesis.dll \
  --duration 90
```

#### Gravar todas as ROMs de uma pasta de Sistema:
```bash
# Grava 60 segundos de cada jogo presente na pasta roms/GBA/
python tools/record_gameplay/record_suite.py --system gba --duration 60

# Grava 90 segundos de cada jogo do Nintendo 64 com Upscale 2x:
python tools/record_gameplay/record_suite.py --system n64 --duration 90 --upscale 1
```

---

## 5. Fluxo de Trabalho Recomendado

### Para Vídeos do YouTube (Edição Split-Screen / Comparativos)
1. Execute a bateria com `no_input = 1`.
2. Importe os arquivos `.mp4` para o editor de vídeo (Premiere / DaVinci Resolve / Final Cut).
3. Crie uma linha do tempo em 1080p ou 4K a 60 FPS.
4. Alinhe os clipes no quadro 0 — a animação de introdução e o gameplay demo estarão perfeitamente sincronizados.
5. Aplique máscaras de corte vertical (Crop 50% / 50%) para exibir o antes e depois, com rótulos como `Nativo 320×240` vs `Upscale Horiz 2x (640×240)` vs `Modo 4x Ultra (1280×240)`.

### Para o Site (`karamelo-emu.com`)
1. Utilize clipes mais curtos (15 a 30 segundos) de jogabilidade marcante.
2. Transcodifique para WebM/MP4 otimizado para web (autoplay em loop sem áudio):
   ```bash
   ffmpeg -i video_input.mp4 -an -c:v libx264 -crf 23 -preset slow -pix_fmt yuv420p video_web.mp4
   ```
3. Utilize os frames de zoom da pasta `docs/images/` para os componentes de *Slider Interativo*.

