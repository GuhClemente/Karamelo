# Guia de Porte: Karamelo para Android (`.apk`)

Este documento estabelece o roteiro técnico, arquitetura e diretrizes para a criação do porte nativo do **Karamelo Emulador** para o ecossistema Android (smartphones, tablets, Android TV e consoles portáteis como Ayn Odin 2, Retroid Pocket e Logitech G Cloud).

---

## 1. Visão Geral da Arquitetura

O Karamelo é escrito em **C++20 limpo** e já utiliza **SDL3** nas versões de Linux e macOS. O SDL3 possui suporte oficial e de primeira classe ao Android através da camada JNI (`SDLActivity`), o que permite reaproveitar praticamente toda a base de código do frontend (`core_runner.cpp`, `osd.cpp`, `charrom.cpp`, `menu.cpp`).

```
[ Camada Java/Kotlin (Android) ]
    ├── SDLActivity (Ciclo de vida, Janela, Contexto SurfaceView)
    ├── Google Play Services (Login 1-toque do Karamelo Cloud)
    └── SAF Helper (Permissões de pasta de ROMs no MicroSD)
             │  (JNI - Java Native Interface)
             ▼
[ Camada Nativa C++20 (Karamelo Core) ]
    ├── main_android.cpp (Ponto de entrada SDL3)
    ├── core_runner.cpp (Thread Libretro, Ring Buffer de Áudio)
    ├── osd.cpp & menu.cpp (Interface gráfica MiSTer-like)
    ├── cloud_sync.cpp (Google Drive AppData Sync)
    └── voice_chat.cpp (Opus + RNNoise / Silero VAD)
             │
             ▼
[ Motores de Emulação (Libretro Cores) ]
    └── Cores dinâmicos arm64-v8a (.so) carregados via dlopen()
```

---

## 2. Requisitos e Ferramentas

* **IDE**: Android Studio (versão Hedgehog ou superior).
* **SDK Android**: Mínimo API 26 (Android 8.0 Oreo), Alvo API 34+ (Android 14+).
* **NDK (Native Development Kit)**: Versão 26 LTS ou superior (com suporte a C++20 no Clang).
* **Build System**: CMake 3.22+ integrado ao Gradle.
* **Arquitetura Primária**: `arm64-v8a` (64-bit ARM, padrão de todos os dispositivos modernos).

---

## 3. Motores de Emulação (Cores Prontos)

Não é necessário compilar os cores do zero. O projeto oficial do Libretro mantém os cores pré-compilados diariamente para a arquitetura `arm64-v8a`:

* **Repositório Oficial do Buildbot**:
  ```
  https://buildbot.libretro.com/nightly/android/latest/arm64-v8a/
  ```

Os cores são distribuídos como arquivos `.so` compactados em `.zip` (ex: `snes9x_libretro_android.so`, `genesis_plus_gx_libretro_android.so`, `mupen64plus_next_libretro_android.so`).

No Android, os cores podem ser baixados pelo próprio aplicativo na primeira inicialização ou incluídos na pasta de ativos/dados do app (`/data/data/com.karamelo.emu/cores/`).

---

## 4. Integração com Karamelo Cloud (Google Drive)

No Android, o login com a conta Google é ainda mais natural e seguro do que no desktop:

1. **Google Identity / Play Services**:
   - O aplicativo solicita permissão com 1 toque na tela usando a API oficial do Google Play Services (`GoogleSignInClient`).
   - O usuário escolhe a conta Google já logada no celular.
2. **Sincronização com `appDataFolder`**:
   - Utiliza a mesma pasta oculta do Google Drive que as versões de Windows, Mac e Linux utilizam.
   - Saves de bateria (`.srm`) e configurações (`karamelo.cfg`) são baixados ao iniciar e enviados ao fechar o jogo.
   - **Resultado**: O jogador joga no PC, salva, abre o celular na rua e o save continua exatamente do mesmo ponto.

---

## 5. Entrada e Controles (Input)

1. **Controles Físicos (Gamepads USB-C / Bluetooth)**:
   - Controles como GameSir G8, Razer Kishi, Xbox One/Series, DualSense e controles integrados (Retroid/Odin) são mapeados nativamente via SDL3 Gamepad API (`SDL_GamepadEvent`).
2. **Controles na Tela de Toque (Touch Overlay Virtual)**:
   - Criação de uma camada gráfica transparente sobre a tela renderizando botões virtuais (D-Pad, A, B, X, Y, L, R, Start, Select) para quem joga direto no smartphone sem controle externo.

---

## 6. Acesso a Arquivos (Scoped Storage & SAF)

A partir do Android 11, o Google exige o uso do **Storage Access Framework (SAF)** para ler cartões de memória microSD ou pastas do usuário:

* O usuário clica em `[ Selecionar Pasta de Jogos ]`.
* O sistema abre o seletor padrão do Android (`ACTION_OPEN_DOCUMENT_TREE`).
* O Karamelo salva a permissão persistente (`takePersistableUriPermission`) para ler a pasta `roms/` e `bios/` livremente.

---

## 7. Passos para Início do Desenvolvimento

1. [ ] Clonar o template oficial de SDL3 para Android (`third_party/SDL3/android-project`).
2. [ ] Configurar o `CMakeLists.txt` nativo linkando os fontes do Karamelo (`src/core_runner.cpp`, `src/menu.cpp`, `src/osd.cpp`, `src/charrom.cpp`).
3. [ ] Criar o script `download_cores_android.sh` para baixar a suíte de cores `arm64-v8a` do Buildbot.
4. [ ] Testar boot básico exibindo o menu OSD do Karamelo em um dispositivo real ou emulador Android Studio.
