# De onde vem a nossa interface: o que é porte do Main_MiSTer e o que é nosso

Este documento existe porque a pergunta "nossa interface é baseada em quais
partes do MiSTer original?" não tinha uma resposta séria até agora — só
comentários soltos no código dizendo "portado do Main_MiSTer" sem detalhar
quanto, exatamente, foi portado. Aqui está a resposta de verdade, feita
clonando o repositório original (https://github.com/MiSTer-devel/Main_MiSTer,
snapshot de 2026-09-07) e comparando arquivo por arquivo com o nosso `src/`.

Contexto rápido pra quem não conhece o Main_MiSTer: é o software que roda no
Linux ARM da placa MiSTer (De10-Nano), escrito em C, e cuida de tudo que a
FPGA não faz sozinha — desenhar o menu OSD sobre o vídeo, ler joystick/teclado
via SPI, carregar núcleos (cores) na FPGA, gerenciar configuração via INI. O
nosso projeto não tem FPGA nem Linux ARM — é um app Windows em C++ que roda
núcleos libretro via SDL3. Ou seja: a base de hardware é completamente
diferente, então "baseado em" aqui quase sempre quer dizer "a mesma ideia,
reescrita pra outra arquitetura". Duas exceções (`osd.cpp`/`osd.h` e
`charrom.cpp`/`charrom.h`) eram porte literal até este documento ser
atualizado; hoje são reescritas clean-room - ver a seção correspondente
abaixo para o porquê e o que exatamente mudou.

## Reescritos para remover porte literal (eram porte, não são mais)

`osd.cpp`/`osd.h` e `charrom.cpp`/`charrom.h` eram, até este documento ser
atualizado, porte direto e quase byte-a-byte do Main_MiSTer original (a
versão anterior desta mesma seção descrevia exatamente isso, com evidência
de diff). Como o restante deste projeto não tinha o código-fonte aberto
à época, manter código GPL-derivado incorporado no binário criava uma
obrigação de disponibilizar o código correspondente que não estava sendo
cumprida. A solução adotada foi uma reescrita clean-room dos dois arquivos,
numa branch separada, preservando o comportamento e o formato de dados que o
resto do app depende — mas com implementação própria, não copiada.

### `osd.cpp` / `osd.h` — o buffer do OSD

O que foi **preservado deliberadamente** (não é "expressão criativa", é o
contrato funcional com o resto do app):
- O formato de bytes do raster que `OsdGetBuffer()`/`OsdGetInvertMap()`
  expõem, byte a byte idêntico ao comportamento anterior — verificado por
  fuzzing diferencial (2000 cenários aleatórios, incluindo strings longas
  forçando múltiplas quebras de linha e índices de linha no limite/além do
  limite de 32 slots) comparando a implementação antiga com a nova, sem
  nenhuma divergência.
- As constantes (`OSD_ARROW_LEFT=1`, `OSD_ARROW_RIGHT=2`, `REPEATDELAY=500`,
  etc.) e a assinatura pública de cada função em `osd.h`.
- O tamanho fixo do cartão (15 linhas) e a remoção das funções que só faziam
  sentido com FPGA de verdade — decisões já tomadas antes desta reescrita e
  mantidas.

O que **mudou de fato** é a implementação: em vez de variáveis globais soltas
e incremento manual de ponteiro (`osdbufpos++`), o novo código usa uma classe
`RowEncoder` pequena e funções nomeadas por "tipo de célula" (`EmitTitleCell`,
`EmitArrowCell`, `EmitTextGlyphCell`), com nomes de variável e comentários
próprios explicando o "porquê" de cada bloco de código a partir do zero.

### `charrom.cpp` / `charrom.h` — a fonte 8x8 do menu

A fonte agora vem do [Unscii](https://github.com/viznut/unscii), que o
próprio autor coloca em Domínio Público / CC0. Os glyphs 32-126 (ASCII
imprimível) vêm do `unscii-8.hex`; os únicos índices fora desse intervalo que
o app realmente lê (`0x10`/`0x11`/`0x14`/`0x15`, usados por `OsdWriteOffset`
para as setas de paginação, e `0x16`, usado por `menu.cpp` como marcador de
"abre submenu") são triângulos/chevrons geométricos simples, desenhados do
zero para este arquivo. Todo o resto do array (o conjunto de ícones
específicos do MiSTer - logo da Atari, símbolo de bluetooth, etc. - que este
app nunca indexa) ficou em branco em vez de inventar conteúdo sem uso.

Detalhe técnico que só apareceu depois de testar a troca de fonte na prática:
este codebase guarda cada glyph "por coluna" (`charfont[ch][col]`, com o bit
`row` marcando o pixel), não "por linha" como a maioria dos formatos de fonte
bitmap - confirmado comparando com `DrawCharTo()` e com o trecho de
`main_win32.cpp` que lê o raster do OSD. Os dados do Unscii são "por linha";
cada glyph foi transposto para a convenção deste app, não copiado
diretamente.

`charrom_load()`/`LoadFont()` (carregar uma fonte substituta de um arquivo em
disco) não foi portado: não há nenhuma chamada a essa função em lugar nenhum
deste `src/`.

## Inspirado no MiSTer, mas escrito do zero (não é porte de código)

### `menu.cpp` — a navegação e a lista de sistemas

O `menu.cpp` original do MiSTer tem **8473 linhas**; o nosso tem **3092**.
Não é porte — é outra arquitetura resolvendo um problema parecido. O
original cuida de coisas que não existem aqui: parsing de `.ini` por-core
com "DIP switches" configuráveis, menu de configuração de joystick bruto
(mapeamento de HID cru), boot automático de core, integração com o
`user_io.cpp` (comunicação SPI com a FPGA). Nosso menu.cpp lida com opções de
núcleos libretro (uma API padronizada, bem mais simples que ler um `.ini`
arbitrário por core) e não tem nenhuma camada de hardware para conversar.

O que **é** genuinamente herdado, mesmo sem ser porte de código:
- O conjunto de teclas de navegação é idêntico:
  `KEY_UP/DOWN/LEFT/RIGHT/HOME/END/PAGEUP/PAGEDOWN` — os mesmos nomes, o
  mesmo papel (`Home`/`End` pulam pro início/fim da lista, `PageUp`/`PageDown`
  pulam páginas inteiras) em ambos os projetos.
- A "aparência" do menu (o cartão de 15 linhas com fundo próprio, a barra de
  título, a seta indicando mais opções à esquerda/direita) é a mesma
  convenção visual do MiSTer real, porque é desenhada usando a API do
  `osd.cpp` portado (`OsdSetTitle`, `OsdWrite`, `OSD_ARROW_LEFT/RIGHT`) — o
  menu **parece** o MiSTer porque usa literalmente a mesma "tela" que o
  MiSTer usa, mesmo sendo outro código decidindo o que desenhar nela.

### `input_map.cpp` — o layout padrão do controle

Não tem nenhuma relação de código com `input.cpp`/`joymapping.cpp` do
original (que lidam com enumeração de dispositivos HID brutos via
`/dev/input/js*`, um problema que não existe no Windows com SDL3). Mas o
**layout padrão de botões** é intencionalmente o do MiSTer/RetroPad — o
comentário no próprio arquivo diz: *"Gamepad: Standard MiSTer / Nintendo
RetroPad mapping on Xbox layout"* — ou seja, a escolha de qual botão físico
faz o quê é uma decisão de design herdada, não o código que a implementa.

## Investigado e descartado (achei parecido, não é)

Esses arquivos do MiSTer original pareciam candidatos óbvios por causa do
nome ou do domínio, mas a comparação real não confirmou nenhuma relação:

- **`cfg.cpp`** (824 linhas) — parsing de `MiSTer.ini` via uma tabela
  tipada de variáveis (`ini_var_t`, com tipo/min/max por campo). Nossa
  persistência de configurações (em `menu.cpp`, `MenuSaveSettings`/
  `MenuLoadSettings`) é um parser de `chave=valor` por linha, com um prefixo
  `coreopt:` pras opções de core — desenho completamente diferente, sem
  tabela tipada nenhuma. Não é porte.
- **`gamecontroller_db.cpp`** (574 linhas) — faz o parsing manual do
  `gamecontrollerdb.txt` do SDL2 pra mapear botões físicos. Nosso
  `gamepad_sdl.cpp` não faz esse trabalho: usa a API nativa `SDL_Gamepad` do
  SDL3 diretamente, que já resolve o mapeamento por dentro. Os arquivos
  `gamecontrollerdb.txt` que aparecem em `app/ports/*/` pertencem aos jogos
  "Ports & Recomp" baixados de terceiros (cada um tem o seu próprio, vindo do
  respectivo projeto), não ao nosso código.
- **`karamelo_math.cpp`** (`SnapToStandardRate`, `ComputeViewport`) — parecia
  candidato pela função de "encaixar taxa de atualização na mais próxima
  padrão", que é um problema que o `video.cpp`/`scaler.cpp` do MiSTer
  também resolve. Não achei nenhuma linha de código correspondente nos dois
  arquivos originais — é implementação nossa, com uma nota de depuração
  própria deste projeto (*"a virtual display adapter... made DWM report
  59.000Hz"*) que não existe no MiSTer. Mesma ideia de domínio (taxas de
  atualização de vídeo são as mesmas fisicamente, em qualquer sistema),
  código independente.
- **`recent.cpp`** (lista de jogos recentes) e **`cheats.cpp`** (códigos
  tipo Game Genie) — os únicos usos das palavras "recent"/"cheat" no nosso
  `menu.cpp` são incidentais (uma mensagem de "versão mais recente" do
  updater, e "cheats" como nome de pasta excluída de uma varredura de ROM).
  Nenhuma das duas funcionalidades existe no nosso projeto.
- **Tudo que é hardware puro**: `fpga_io.cpp`, `fpga_manager.h`, `spi.cpp`,
  `smbus.cpp`, `shmem.cpp`, `hdmi_cec.cpp`, `ide.cpp`/`ide_cdrom.cpp`,
  `scheduler.cpp`, `user_io.cpp`, `hardware.cpp`, `brightness.cpp`,
  `battery.cpp`, `hdmi_cec.cpp` — não têm nenhum equivalente possível aqui,
  já que não existe FPGA, barramento SPI, EEPROM ou HDMI-CEC num app
  Windows. Não foram sequer candidatos.

## Tabela resumo

| Arquivo nosso | Origem | Relação real |
|---|---|---|
| `osd.cpp` / `osd.h` | `osd.cpp` / `osd.h` | Era porte direto; reescrito clean-room, mesmo formato de raster |
| `charrom.cpp` / `charrom.h` | `charrom.cpp` / `charrom.h` | Era porte direto; fonte trocada pela Unscii (CC0/domínio público) |
| `menu.cpp` | `menu.cpp` | Inspirado (mesma convenção de teclas e visual via OSD), código próprio |
| `input_map.cpp` | `input.cpp` / `joymapping.cpp` | Só o layout padrão de botões é herdado; código totalmente diferente |
| `karamelo_math.cpp` | `video.cpp` / `scaler.cpp` | Mesmo domínio (taxas de vídeo), código independente |
| `menu.cpp` (persistência) | `cfg.cpp` | Sem relação — desenhos de parsing diferentes |
| `gamepad_sdl.cpp` | `gamecontroller_db.cpp` | Sem relação — usamos a API nativa do SDL3 em vez disso |
| — | `fpga_io.cpp`, `spi.cpp`, `user_io.cpp`, etc. | Sem equivalente possível (não há FPGA) |

## Metodologia

Análise feita clonando `https://github.com/MiSTer-devel/Main_MiSTer`
(`git clone --depth 1`) para comparação lado a lado com `diff` real contra
cada arquivo candidato do nosso `src/`/`include/`, não por memória ou
suposição. Onde a semelhança de nome/domínio não se confirmou em código
(`cfg.cpp`, `gamecontroller_db.cpp`, `karamelo_math.cpp`, `joymapping.cpp`),
isso está registrado explicitamente acima como descartado, não omitido.
