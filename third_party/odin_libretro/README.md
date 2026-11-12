# Odin Libretro Core Wrapper (`cores/hikaru.dll`)

Este diretório contém o wrapper Libretro nativo em C++20 para o emulador **Odin**, permitindo a emulação transparente da placa arcade **Sega Hikaru** dentro do **Karamelo Emulador**.

---

## 1. Atribuição e Créditos Upstream

* **Projeto Upstream**: [Odin](https://github.com/Tovarichtch/Odin)
* **Autores Originais**: Réda Chérif-Touil (*Tovarichtch*) e Stefano Teso (*Valkyrie*)
* **Licença**: **GNU General Public License v3.0 (GPL-3.0)**
* **Repositório**: `https://github.com/Tovarichtch/Odin`

---

## 2. Hardware e Jogos Suportados

### Sega Hikaru (Arcade, 2000–2001)
* **Especificações**:
  * 2x Hitachi SH-4 (dual-processor lockstep/parallel bus)
  * GPU proprietária 3D Sega Hikaru (*Antarctic / Eurasia / PMX1*)
  * Áudio: Yamaha AICA (ARM7 + AICA DSP, 44.1 kHz PCM estéreo)
  * I/O: Sega MIE / JVS Arcade
* **Jogos Emulados**:
  * *Star Wars: Racer Arcade*
  * *Planet Harriers*
  * *Cyber Troopers Virtual-On Force*
  * *Air Trix*
  * *NASCAR Arcade*
  * *Brave Firefighters*

### Gaelco 3D
* *Smashing Drive*
* *ATV Track*
* *Gaelco Football*

---

## 3. Arquitetura do Wrapper Libretro (`libretro.cpp`)

1. **Ciclo de Execução (`retro_run`)**:
   * O frontend chama `retro_run()` a cada quadro (60 fps).
   * O wrapper faz a leitura dos controles via `input_poll_cb` e traduz as entradas digitais e analógicas para a estrutura `odin::CabinetInputs` (JVS).
   * Executa um passo completo da máquina através de `board->run_frame()`.
   * Envia os blocos de áudio de 44.1 kHz 16-bit PCM estéreo para `audio_batch_cb`.

2. **Renderização por Hardware (`RETRO_HW_RENDER`)**:
   * O core solicita um contexto OpenGL Core 4.3 ao frontend.
   * O Karamelo fornece um Framebuffer Object (FBO) com buffers de profundidade (depth) e stencil através de `src/hw_render.cpp`.
   * O backend gráfico do Odin desenha as listas de exibição da GPU Hikaru diretamente no FBO do Libretro.
   * Ao final do quadro, o core notifica o frontend usando o identificador especial `RETRO_HW_FRAME`.

3. **Mapeamento de Controles**:
   * Suporte a botões digitais (Start, Moeda, A, B, X, Y, L, R).
   * Suporte total a eixos analógicos (`RETRO_DEVICE_ANALOG`): volante, acelerador, freio e manche de voo (*Planet Harriers*).

