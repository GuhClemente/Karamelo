# Guia de Controles e Mapeamentos do Karamelo

Este documento descreve a arquitetura de controles do Karamelo, as regras de mapeamento por sistema, o suporte a controles de 6 botões e o guia definitivo de uso com o controle **8BitDo M30** e o **Sega Saturn 3D Control Pad**.

---

## 1. Arquitetura de Controles do Karamelo

O Karamelo utiliza um pipeline moderno e de ultra-baixa latência baseado no **SDL3 Gamepad**, mantendo compatibilidade total com o modelo de estados **XInput** para garantir suporte a centenas de gamepads (Xbox, PlayStation DualSense/DualShock, Nintendo Switch Pro/Joy-Cons, 8BitDo, Retro-Bit, controles genéricos DInput e manches Arcade).

### Presets Globais de Botões (Menu > Controles > Preset Botões)
1. **MiSTer / Nintendo** (Padrão): Baseado no layout canônico do SNES / MiSTer FPGA. O botão inferior físico do Xbox (A) atua como B (Pulo/Cancelar) e o botão direito físico (B) atua como A (Confirmar).
2. **Xbox Nativo**: Mapeamento 1:1 com a serigrafia do controle de Xbox (A=A, B=B, X=X, Y=Y).
3. **PlayStation**: Adaptação para o padrão PlayStation (Cross/X embaixo, Circle na direita, Square na esquerda, Triangle no topo).
4. **8BitDo M30 (Cabo/XInput)**: A, B, X e Y nos mesmos lugares do MiSTer; ombro L = L, ombro R = R, Z = L2 e C = R2. Conferido em um M30 real (ver seção 5).
5. **Arcade 6-Botões**: Mapeamento clássico de fighting arcade (Fileira de cima: Soco Fraco, Médio, Forte; Fileira de baixo: Chute Fraco, Médio, Forte).

---

## 2. Controles de 6 Botões (`pad_six_mode`)

Em **Configurações > Controles > Controle 6 Botões**, para controles no padrão Sega (A, B, C embaixo; X, Y, Z em cima; L e R nos ombros) e arcade sticks:

* **Auto** *(padrão)*: liga o layout de 6 botões quando o controle do jogador 1:
  1. **foi calibrado** (ver abaixo), ou
  2. tem nome de controle de 6 botões no SDL3: `M30`, `Saturn`, `Mega Drive`, `Genesis`, `MD6`, `Fighting Commander`, `Fightstick`, `Arcade Stick`, `Neo Geo`… (só o nome conta, nunca o fabricante inteiro: um 8BitDo SN30 Pro ou Ultimate continua normal), ou
  3. o preset de botões escolhido é o **8BitDo M30**.

  Um controle comum (Xbox, PlayStation, Switch) não muda. A linha do menu mostra o resultado: **Auto/Calib** (calibrado), **Auto/Detect** (detectado pelo nome), **Auto/M30** (preset M30) ou **Auto/Inativo**.
* **Sega Padrão**: força a tabela Sega/DInput (C=RB, Z=LB, L=LT, R=RT).
* **8BitDo M30 XInput**: força a tabela do M30 no cabo (C=RT, Z=RB, L=LB, R=LT).
* **Desligado**: layout tradicional de 4 botões.

O layout acompanha o controle que está na mão do jogador 1: trocar de controle no meio do jogo troca o layout. Um remapeamento salvo para o sistema continua tendo prioridade.

### Calibrar 6 Botões
**Configurações > Controles > Calibrar 6 Botões**: aperte, quando pedido, **A, B, C, X, Y, Z, L, R** do próprio controle (START pula L ou R se o controle não tiver ombros; ESC no teclado cancela). O resultado fica em `Config/pads/<vendor>_<produto>_<nome>.cfg` e vale para aquele modelo de controle sempre que ele for conectado — é o mesmo princípio dos perfis de autoconfig do RetroArch. Calibrar é o jeito certo para qualquer firmware: cada fabricante manda C, Z e os ombros para um lugar diferente. **Apagar Calibração** remove o arquivo. Ao terminar a calibração, a opção "Controle 6 Botões" passa para **Auto**: só o Auto usa a calibração, os modos forçados (Sega Padrão, 8BitDo M30 XInput) usam a própria tabela.

O mapeamento de cada sistema vem da própria tabela RetroPad do core (Genesis Plus GX, Beetle Saturn, Flycast, Opera); em jogos de luta de arcade, socos vão para a fileira de cima e chutes para a de baixo, pelos nomes que o core dá aos botões do jogo.

### Mapeamento por Console no Modo 6 Botões:

| Console / Sistema | Fileira Superior (X, Y, Z) | Fileira Inferior (A, B, C) | Ombros / Gatilhos (L, R) |
| :--- | :--- | :--- | :--- |
| **Sega Genesis / Mega Drive / CD / 32X** | **X**, **Y**, **Z** | **A**, **B**, **C** | RetroPad L2 / R2 |
| **Sega Saturn (Digital)** | **X**, **Y**, **Z** | **A**, **B**, **C** | **L** e **R** (Gatilhos de ombro) |
| **Sega Dreamcast** | **X**, **Y**, **Z** (RetroPad R) | **A**, **B**, **C** (RetroPad L) | **Gatilhos L e R** do Dreamcast |
| **3DO Interactive Multiplayer** | **X** (P / Stop), **Y**, **Z** (Select) | **A**, **B**, **C** | **L** e **R** |
| **Arcade / MAME / FBNeo** | Soco Fraco, Soco Médio, Soco Forte | Chute Fraco, Chute Médio, Chute Forte | Ações auxiliares / Coins |

---

## 3. Sega Dreamcast: gatilhos nos ombros, C e Z na frente

O controle do Dreamcast tem 4 botões frontais (A, B, X, Y) e 2 gatilhos analógicos (L e R). Com o layout de 6 botões:

1. **Ombros L e R** do controle = **gatilhos L e R** do Dreamcast (RetroPad L2/R2): mira e lanterna em *Resident Evil 3* e *The Ring*, acelerar e frear em *Crazy Taxi*.
2. **C e Z** = **RetroPad L e R** do Flycast, que são os botões C e Z do Arcade Stick / Naomi: em jogos de luta, ficam como golpes próprios.

Nenhum botão aciona outro junto. Até a 0.985 havia um espelhamento L↔L2 e R↔R2 que fazia C também apertar o gatilho (e o ombro também apertar C); em jogos de Naomi de 6 botões, cada golpe forte saía dobrado. Ele foi removido.

---

## 4. Sega Saturn: Suporte ao 3D Control Pad (Analógico)

O **Sega Saturn 3D Control Pad** (Modelo HSS-0137, lançado em 1996 junto a *NiGHTS into Dreams...*) introduziu a alavanca analógica circular e gatilhos analógicos de ombro ao Saturn, sendo o antecessor direto do controle do Dreamcast.

No menu **Configurações > Controles**, a opção **Controle Saturn** permite alternar:
* **Padrão (Digital)**: Emula o controle original digital de 6 botões do Saturn.
* **3D Pad (Analógico)**: declara na porta 1 o dispositivo que o próprio core lista para o 3D Control Pad (`SET_CONTROLLER_INFO`; no Beetle Saturn é `RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)`, não o `RETRO_DEVICE_ANALOG` puro). A troca vale a partir do próximo quadro, sem reiniciar o jogo, e é feita na thread do core, entre dois `retro_run()`.

### Principais Jogos do Saturn com Suporte Nativo ao 3D Pad:
* *NiGHTS into Dreams...* (Projetado especificamente para o controle)
* *Sega Rally Championship*
* *Panzer Dragoon II Zwei*
* *Sonic R*
* *Daytona USA: Championship Circuit Edition (CCE)*
* *Duke Nukem 3D* e *Quake*
* *Burning Rangers*
* *WipEout* e *WipEout 2097*
* *Powerslave (Exhumed)*
* *Manx TT SuperBike*

---

## 5. Guia Definitivo do 8BitDo M30 (Dicas e Truques de Hardware)

**Medido em um M30 real no cabo USB (modo XInput), em 20/11/2026**, pelo SDL3: nome `Xbox 360 Controller`, vendor/produto `045e:028e`, e os botões chegam como **A=A, B=B, C=RT, X=X, Y=Y, Z=RB, L=LB, R=LT**. A tabela "Sega Padrão" (C=RB, Z=LB) **não** serve para ele nesse modo. Os atalhos de modo e de firmware abaixo vêm do manual da 8BitDo e não foram conferidos aqui.

O **8BitDo M30** é amplamente considerado o melhor controle contemporâneo para fãs da Sega e de jogos de luta. Ele possui funcionalidades de firmware embutidas que muitos usuários desconhecem:

### Modos de Conexão ao Ligar o Controle
Ligue o controle segurando os botões indicados até os LEDs começarem a correr:

| Atalho de Ligar | Modo de Operação | LED Indicador | Recomendação no Karamelo |
| :--- | :--- | :--- | :--- |
| **START + X** | **XInput** (Xbox 360) | LEDs 1 e 2 acesos | Funciona, mas o M30 se apresenta como **"Xbox 360 Controller"** (045e:028e) até no próprio USB: o nome M30 não aparece em lugar nenhum. Use **Calibrar 6 Botões** uma vez, ou o preset **8BitDo M30**. |
| **START + Y** | **Switch Mode** | LED 1 aceso | Só sem fio. **No cabo USB não muda nada**: ligado com START+Y e plugado, o M30 testado continuou "Xbox 360 Controller" 045e:028e. Por Bluetooth, se aparecer com o próprio nome, o **Auto** o detecta sozinho (não conferido); calibrar continua recomendado, porque o lugar de C, Z e dos ombros depende do modo. |
| **START + B** | **DInput** (Android / PC) | LED 1 piscando | Modo DirectInput tradicional. |
| **START + A** | **macOS** | LEDs 1, 2 e 3 acesos | Modo compatível com Apple Silicon e Macs. |

---

### Atalhos Secretos de Firmware do 8BitDo M30 (Segure por 3 segundos):

1. **Alternar D-Pad para Alavanca Analógica Esquerda (`Left Stick`)**:
   * **Atalho**: Segure **`SELECT + UP`** (ou `MINUS + CIMA`) por **3 segundos**.
   * **Efeito**: O LED do controle piscará. Agora, o D-Pad físico do M30 envia comandos como o **Analógico Esquerdo**!
   * **Utilidade**: Essencial para navegar em menus analógicos ou jogar jogos de Saturn em modo 3D Pad e Dreamcast sem precisar de uma alavanca física separada.
2. **Retornar para D-Pad Digital Puro**:
   * **Atalho**: Segure **`SELECT + LEFT`** (ou `MINUS + ESQUERDA`) por **3 segundos**.
   * **Efeito**: O direcional volta a operar como D-Pad digital nativo clássico.
3. **Alternar D-Pad para Alavanca Analógica Direita (`Right Stick`)**:
   * **Atalho**: Segure **`SELECT + RIGHT`** por **3 segundos**.
4. **Inverter Botões de Face (Layout Nintendo vs Xbox)**:
   * **Atalho**: Segure **`SELECT + DOWN`** por **3 segundos**.
5. **Função Turbo**:
   * Pressione o botão desejado + **STAR** (botão de estrela) para ativar o disparo rápido contínuo.

---

## 6. Solução Rápida de Problemas (Troubleshooting)

* **Pergunta: Meu controle de 6 botões não ativa o layout (menu mostra "Auto/Inativo").**
  * *Solução*: o controle não se identifica pelo nome (o M30 no cabo diz ser um Xbox 360). Use **Calibrar 6 Botões** uma vez: daí em diante aparece "Auto/Calib". Observação: a calibração vale para o modelo que o controle diz ser, então um Xbox 360 de verdade ligado no mesmo PC também pegaria o layout de 6 botões nos sistemas Sega; nesse caso, use **Apagar Calibração** antes de jogar com o Xbox 360. No cabo USB o M30 não tem modo que mostre o próprio nome (conferido com START+Y).
* **Pergunta: No Dreamcast o golpe forte saía junto com o gatilho.**
  * *Solução*: corrigido: o espelhamento entre C/Z e os gatilhos foi removido. Ombros = gatilhos, C e Z = botões próprios.
* **Pergunta: Em *NiGHTS* no Saturn o personagem só anda em 8 direções duras.**
  * *Solução*: Abra o menu do Karamelo (**F12** ou **START + SELECT**), vá em **Configurações > Controles** e mude **Controle Saturn** para **3D Pad (Analogico)**. No 8BitDo M30, segure **SELECT + UP** por 3 segundos para que o direcional transmita eixos analógicos suaves de 360°.
