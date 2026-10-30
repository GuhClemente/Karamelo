# Guia de Porte: Karamelo para Portáteis Retrô Linux & PortMaster

Este documento estabelece a arquitetura técnica, modelo de empacotamento e diretrizes para disponibilizar o **Karamelo Emulador** para a vasta família de consoles portáteis baseados em Linux ARM (**Anbernic, Powkiddy, TrimUI, Miyoo e similares**) através da plataforma comunitária padrão **PortMaster**.

---

## 1. O Mercado de Portáteis Retrô Linux

Nos últimos anos, dezenas de milhões de portáteis dedicados a retrogaming foram vendidos mundialmente, operando sob distribuições Linux minimalistas especializadas:

* **Sistemas Operacionais Comuns**:
  * **ArkOS** (base Ubuntu/Debian para chips Rockchip).
  * **ROCKNIX / JELOS** (distro imutável para dezenas de SoCs).
  * **Knulli / Batocera Handheld** (baseada em Buildroot).
  * **Amberelec** (otimizada para dispositivos Anbernic).
  * **MinUI** (interface ultra-minimalista).

* **Dispositivos Populares Alvo**:
  * **Anbernic**: RG35XX Plus, RG35XX H, RG353V, RG353P, RG40XX, RG ARC, RG556.
  * **Powkiddy**: RGB30, RGB10 Max 3, X55, V10.
  * **TrimUI**: TrimUI Smart Pro (tela 16:9 de 720p).
  * **Miyoo**: Miyoo A30, Miyoo Mini Plus.

---

## 2. Por que o PortMaster é a Solução Ideal?

O **PortMaster** (`https://portmaster.games`) é o gerenciador de pacotes e instalador oficial da comunidade de portáteis Linux. 
* Ele permite que o usuário baixe e execute jogos e emuladores nativos direto pelo Wi-Fi do portátil com um único clique.
* O Karamelo em C++20 + SDL3 encaixa-se com perfeição nos padrões do PortMaster, pois ele é um binário autônomo e sem dependências pesadas de desktop.

---

## 3. Especificações Técnicas de Hardware

| Componente | Especificação Típica nos Portáteis | Impacto no Karamelo |
| :--- | :--- | :--- |
| **Processador (SoC)** | Rockchip RK3566, Allwinner H700, Rockchip RK3326 | CPUs ARM Cortex-A53 ou Cortex-A55 (ARM64 `aarch64` ou ARM32 `armhf`). |
| **Memória RAM** | **1 GB a 2 GB LPDDR4** | O Karamelo consome ~30 MB, sobrando mais de 90% da RAM para os cores e cache de ROMs. |
| **GPU & Gráficos** | Mali-G31 MP2 ou Mali-G52 (drivers Panfrost / Mesa) | Suporte completo a **OpenGL ES 2.0 / 3.0** e **Linux KMS/DRM / EGL**. |
| **Telas** | 640x480 (4:3) ou 1280x720 (16:9) IPS | Resoluções ideais para scanlines inteiras e aspect ratio perfeito de consoles antigos. |
| **Controles** | Gamepad embutido mapeado via Linux `evdev` (`/dev/input/event*`) | Reconhecimento nativo via SDL3 com latência zero. |

---

## 4. Estrutura do Pacote PortMaster (`karamelo.zip`)

No PortMaster, cada aplicação reside na pasta `/roms/ports/` (ou `tools/`):

```
/roms/ports/
├── Karamelo.sh                 # Script de inicializacao com runtime do PortMaster
└── karamelo/
    ├── karamelo.aarch64        # Binario compilado para ARM 64-bit
    ├── karamelo.armhf          # Binario compilado para ARM 32-bit (dispositivos legados)
    ├── karamelo.gptokeyb       # Mapeamento do gamepad fisico para atalhos
    ├── cover.png               # Imagem da capa (boxart para EmulationStation)
    ├── screenshot.png          # Print da interface
    ├── default.subfolder/      # Arquivos de configuracao iniciais
    ├── data/
    │   └── ports.json          # Catalogo de recompilacoes
    └── libs/                   # Bibliotecas compartilhadas (SDL3, libGL, etc.)
```

---

## 5. Script de Inicialização de Referência (`Karamelo.sh`)

O PortMaster fornece utilitários auxiliares (como `gptokeyb` para hotkeys e controle de brilho/volume) que são carregados no script:

```bash
#!/bin/bash
# =====================================================================
#   Karamelo Emulador - PortMaster Launcher
# =====================================================================
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR="/$directory/ports/karamelo"
cd $GAMEDIR

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Definir variaveis de ambiente de video e som
export DEVICE_ARCH="${DEVICE_ARCH:-aarch64}"
export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# Iniciar o daemon de controle fisico
$GPTOKEYB "karamelo.$DEVICE_ARCH" -c "./karamelo.gptokeyb" &

# Executar o Karamelo
./karamelo.$DEVICE_ARCH

# Finalizar daemons ao fechar
$ESUDO kill -9 $(pidof gptokeyb)
unset LD_LIBRARY_PATH
printf "\033c" > /dev/tty1
```

---

## 6. Vantagens Competitivas do Karamelo nos Portáteis

1. **Adeus ao EmulationStation Pesado**: Em aparelhos econômicos de 1 GB de RAM, o EmulationStation consome muita energia e esquenta o aparelho só na navegação. O Karamelo consome quase zero de bateria no menu.
2. **Tempo de Resposta Instantâneo**: A alternância entre sistemas e listas de jogos é instantânea devido ao motor em C++20.
3. **Web Studio no Bolso**: O usuário liga o Wi-Fi do portátil, abre o navegador do celular e transfere jogos sem precisar desligar o console nem tirar o cartão MicroSD.
