# Guia de Controles e Mapeamentos do Karamelo

Este documento descreve a arquitetura de controles do Karamelo, as regras de mapeamento por sistema, o suporte a controles de 6 botões e o guia definitivo de uso com o controle **8BitDo M30** e o **Sega Saturn 3D Control Pad**.

---

## 1. Arquitetura de Controles do Karamelo

O Karamelo utiliza um pipeline moderno e de ultra-baixa latência baseado no **SDL3 Gamepad**, mantendo compatibilidade total com o modelo de estados **XInput** para garantir suporte a centenas de gamepads (Xbox, PlayStation DualSense/DualShock, Nintendo Switch Pro/Joy-Cons, 8BitDo, Retro-Bit, controles genéricos DInput e manches Arcade).

### Presets Globais de Botões (Menu > Controles > Preset Botões)
1. **MiSTer / Nintendo** (Padrão): Baseado no layout canônico do SNES / MiSTer FPGA. O botão inferior físico do Xbox (A) atua como B (Pulo/Cancelar) e o botão direito físico (B) atua como A (Confirmar).
2. **Xbox Nativo**: Mapeamento 1:1 com a serigrafia do controle de Xbox (A=A, B=B, X=X, Y=Y).
3. **PlayStation**: Adaptação para o padrão PlayStation (Cross/X embaixo, Circle na direita, Square na esquerda, Triangle no topo).
4. **Arcade 6-Botões**: Mapeamento clássico de fighting arcade (Fileira de cima: Soco Fraco, Médio, Forte; Fileira de baixo: Chute Fraco, Médio, Forte).

---

## 2. Subsistema Inteligente de 6 Botões (`pad_six_button`)

O Karamelo conta com suporte especializado para controles no padrão Sega de 6 botões (A, B, C embaixo; X, Y, Z em cima) e manches Arcade:

* **Auto (Detectar)** *(Padrão a partir da v0.985)*: O Karamelo inspeciona dinamicamente o nome do dispositivo via SDL3. Se o nome contiver `"M30"`, `"Saturn"`, `"Genesis"`, `"Fighting Commander"`, `"MD6"`, `"Fightstick"` ou `"Arcade Stick"`, ele ativa o perfil inteligente de 6 botões para sistemas compatíveis automaticamente.
* **Ligado**: Força o perfil de 6 botões em todos os sistemas compatíveis (ideal se o controle estiver conectado via XInput genérico e o Windows relatar apenas "Xbox 360 Controller").
* **Desligado**: Força o layout tradicional de 4 botões.

### Mapeamento por Console no Modo 6 Botões:

| Console / Sistema | Fileira Superior (X, Y, Z) | Fileira Inferior (A, B, C) | Ombros / Gatilhos (L, R) |
| :--- | :--- | :--- | :--- |
| **Sega Genesis / Mega Drive / CD / 32X** | **X**, **Y**, **Z** | **A**, **B**, **C** | Mode / Start secundário |
| **Sega Saturn (Digital)** | **X**, **Y**, **Z** | **A**, **B**, **C** | **L** e **R** (Gatilhos de ombro) |
| **Sega Dreamcast (Arcade / Luta)** | **X** (LP), **Y** (MP), **Z** (HP / Gatilho R) | **A** (LK), **B** (MK), **C** (HK / Gatilho L) | **Gatilhos L e R** (Espelhamento simultâneo nos ombros) |
| **3DO Interactive Multiplayer** | **X** (P / Stop), **Y**, **Z** (Select) | **A**, **B**, **C** | **L** e **R** |
| **Arcade / MAME / FBNeo** | Soco Fraco, Soco Médio, Soco Forte | Chute Fraco, Chute Médio, Chute Forte | Ações auxiliares / Coins |

---

## 3. Sega Dreamcast: Otimização para Luta e Ação

No Dreamcast, os controles originais possuem 4 botões frontais (A, B, X, Y) e 2 gatilhos analógicos (L e R). Em jogos de luta como *Street Fighter III: 3rd Strike*, *Marvel vs Capcom 2*, *Capcom vs SNK 2* e *Street Fighter Alpha 3*, a Sega e a Capcom mapeavam os golpes fortes nos gatilhos de ombro.

O Karamelo implementa uma **solução híbrida inteligente**:
1. **Fileira Frontal**: O botão **Z** aciona o Gatilho R (Soco Forte) e o botão **C** aciona o Gatilho L (Chute Forte).
2. **Espelhamento de Gatilhos Físicos**: Os gatilhos de ombro físicos superiores (LT e RT) permanecem espelhados em L e R.
   * Em jogos de luta: Você joga com os 6 botões frontais com a pegada de arcade clássico.
   * Em jogos de corrida / ação (*Crazy Taxi*, *Sonic Adventure*, *Shenmue*): Você pode acelerar e frear usando tanto os ombros físicos quanto os botões frontais.

---

## 4. Sega Saturn: Suporte ao 3D Control Pad (Analógico)

O **Sega Saturn 3D Control Pad** (Modelo HSS-0137, lançado em 1996 junto a *NiGHTS into Dreams...*) introduziu a alavanca analógica circular e gatilhos analógicos de ombro ao Saturn, sendo o antecessor direto do controle do Dreamcast.

No menu **Configurações > Controles**, a opção **Controle Saturn** permite alternar:
* **Padrão (Digital)**: Emula o controle original digital de 6 botões do Saturn.
* **3D Pad (Analógico)**: Inicializa a porta do Beetle Saturn com `RETRO_DEVICE_ANALOG`, ativando sensibilidade de 360 graus na alavanca analógica para os jogos compatíveis.

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

O **8BitDo M30** é amplamente considerado o melhor controle contemporâneo para fãs da Sega e de jogos de luta. Ele possui funcionalidades de firmware embutidas que muitos usuários desconhecem:

### Modos de Conexão ao Ligar o Controle
Ligue o controle segurando os botões indicados até os LEDs começarem a correr:

| Atalho de Ligar | Modo de Operação | LED Indicador | Recomendação no Karamelo |
| :--- | :--- | :--- | :--- |
| **START + X** | **XInput** (Xbox 360) | LEDs 1 e 2 acesos | **Padrão para Windows**. O Karamelo o detecta perfeitamente. Se o Windows relatar como genérico, ative "Controle 6 Botões: Ligado". |
| **START + Y** | **Switch Mode** | LED 1 aceso | **Excelente para Bluetooth**. O SDL3 reconhece o nome "8BitDo M30" nativamente e ativa o modo 6 botões no automático. |
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

* **Pergunta: No Dreamcast meus socos e chutes fortes pareciam invertidos.**
  * *Solução*: Certifique-se de estar na versão v0.985 ou posterior. O perfil de 6 botões do Dreamcast agora mapeia automaticamente o botão Z para o Soco Forte (Gatilho R) e C para o Chute Forte (Gatilho L).
* **Pergunta: Em *NiGHTS* no Saturn o personagem só anda em 8 direções duras.**
  * *Solução*: Abra o menu do Karamelo (**F12** ou **START + SELECT**), vá em **Configurações > Controles** e mude **Controle Saturn** para **3D Pad (Analogico)**. No 8BitDo M30, segure **SELECT + UP** por 3 segundos para que o direcional transmita eixos analógicos suaves de 360°.
