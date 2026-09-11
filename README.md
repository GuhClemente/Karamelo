# Karamelo Emulador — Native Multiplatform Retrogaming Suite (Windows, Linux, macOS)

> **Software Livre e Independente — GNU GPL-3.0**  
> Canal do YouTube: **[@GuhClemente](https://youtube.com/@GuhClemente)**  
> Desenvolvido por: **Guh Clemente & Antigravity Pair Team**  
> Core Engine: **Libretro API Architecture**

---

## 🌟 Visão Geral

O **Karamelo Emulador** é um frontend nativo em C++20 (64-bit) de alto desempenho para **Windows x86_64**, **Linux x64** e **macOS ARM64 (Apple Silicon)**. A interface e o OSD são inspirados no visual do MiSTer, mas são **implementação própria, escrita do zero** — nada aqui é porte de código do Main_MiSTer; o que foi preservado é a aparência e o contrato de dados, detalhado arquivo a arquivo em [docs/FRONTEND.md](docs/FRONTEND.md). O frontend é integrado a uma engine modular de execução de cores Libretro com escalonador de 60 FPS com correção de aspecto, filtros CRT, Ring Buffer de áudio estéreo de baixa latência e suporte nativo a **35 sistemas** através de **40 motores de emulação distintos** (lista completa e verificada em [CREDITS.md](CREDITS.md)). Também inclui uma categoria de "Ports & Recomp" com jogos recompilados nativamente (Zelda 64: Recompiled, Jak & Daxter, Super Mario 64, Valkyrie Profile, Perfect Dark e dezenas de outros — total de 42 ports, sendo 19 com suporte nativo a macOS e 16 a Linux — ver [CREDITS.md](CREDITS.md)).

---

## 🎮 Sistemas Suportados

A tabela abaixo é a mesma lógica usada pelo próprio app para decidir qual core abre cada arquivo (`MenuResolveCoreForPath()` em `src/menu.cpp`) — não uma lista à parte que pode ficar desatualizada. Cada pasta em `roms/<Sistema>/` também tem seu próprio `LEIA-ME.txt` com esses mesmos detalhes.

| Sistema | Core DLL | Motor | Extensões aceitas |
| :--- | :--- | :--- | :--- |
| Nintendo 64 | `n64_parallel.dll` (padrão) / `n64_mupen.dll` / `n64_gopher.dll` | ParaLLEl N64 / Mupen64Plus-Next / Gopher64 | `.z64` `.n64` `.v64`, `.zip` `.7z` `.rar` |
| Super Nintendo | `snes.dll` | bsnes | `.sfc` `.smc`, `.zip` `.7z` `.rar` |
| NES / Famicom | `nes.dll` | Mesen | `.nes` `.fds`, `.zip` `.7z` `.rar` |
| Genesis / Mega Drive | `genesis.dll` | Genesis Plus GX | `.md` `.gen` `.bin`, `.zip` `.7z` `.rar` |
| Mega CD / Sega CD | `genesis.dll` | Genesis Plus GX | `.chd` `.cue` `.iso` `.m3u` `.toc`, `.zip` `.7z` `.rar` |
| Master System / GG | `sms.dll` | Gearsystem | `.sms` `.gg` `.sg` `.bin`, `.zip` `.7z` `.rar` |
| 32X | `32x.dll` | PicoDrive | `.32x`, `.zip` `.7z` `.rar` |
| Game Boy / Color | `gb.dll` | Gambatte | `.gb` `.gbc`, `.zip` `.7z` `.rar` |
| Game Boy Advance | `gba.dll` | mGBA | `.gba`, `.zip` `.7z` `.rar` |
| Nintendo DS | `nds.dll` | melonDS | `.nds`, `.zip` `.7z` `.rar` |
| Nintendo 3DS | `3ds.dll` | Citra | `.3ds` `.cia`, `.zip` `.7z` `.rar` |
| GameCube | `gamecube.dll` | Dolphin | `.gcm` `.rvz` `.wbfs`, `.iso` `.cue` `.chd` `.m3u` `.toc`, `.zip` `.7z` `.rar` |
| PlayStation | `psx.dll` | Beetle PSX | `.chd` `.cue` `.iso` `.m3u` `.pbp` `.toc`, `.zip` `.7z` `.rar` |
| PlayStation 2 | `ps2.dll` | LRPS2 (PCSX2) / Play! (`ps2_play.dll`) | `.chd` `.cue` `.iso` `.m3u` `.toc` `.bin`, `.zip` `.7z` `.rar` |
| PSP | `psp.dll` | PPSSPP | `.cso`, `.chd` `.cue` `.iso` `.m3u` `.toc`, `.zip` `.7z` `.rar` |
| Sega Saturn | `saturn.dll` | Beetle Saturn | `.chd` `.cue` `.iso` `.m3u` `.toc` `.bin`, `.zip` `.7z` `.rar` |
| Sega Dreamcast | `dreamcast.dll` | Flycast | `.gdi` `.cdi`, `.chd` `.cue` `.iso` `.m3u` `.toc`, `.zip` `.7z` `.rar` |
| TurboGrafx-16 / PCE | `pce.dll` | Beetle PCE | `.pce` `.sgx`, `.chd` `.cue` `.iso` `.m3u` `.toc` (CD), `.zip` `.7z` `.rar` |
| PC-FX | `pcfx.dll` | Beetle PC-FX | `.pcfx`, `.zip` `.7z` `.rar` |
| NeoGeo (AES/MVS) | `neogeo.dll` | Geolith | `.neo` `.bin`, `.zip` |
| NeoGeo CD | `neocd_alt.dll` | NeoCD | `.chd` `.cue` `.iso` `.m3u` `.toc` |
| NeoGeo Pocket | `ngp.dll` | Beetle NeoPop | `.ngp` `.ngc`, `.zip` `.7z` `.rar` |
| WonderSwan | `wswan.dll` | Beetle WonderSwan | `.ws` `.wsc`, `.zip` `.7z` `.rar` |
| Atari 2600 | `atari2600.dll` | Stella | `.a26`, `.bin`, `.zip` `.7z` `.rar` |
| Atari 5200 | `atari5200.dll` | Atari800 | `.a52`, `.bin`, `.zip` `.7z` `.rar` |
| Atari 7800 | `atari7800.dll` | ProSystem | `.a78`, `.bin`, `.zip` `.7z` `.rar` |
| Atari Jaguar | `jaguar.dll` | Virtual Jaguar | `.j64` `.jag`, `.zip` `.7z` `.rar` |
| Atari Lynx | `lynx.dll` | Handy | `.lnx`, `.zip` `.7z` `.rar` |
| ColecoVision | `coleco.dll` | (motor não identificado, core válido) | `.col`, `.zip` `.7z` `.rar` |
| MSX / MSX2 / MSX2+ | `msx.dll` | fMSX | `.mx1` `.mx2` (forçam MSX1/MSX2), `.rom` `.dsk` `.cas`, `.zip` `.7z` `.rar` |
| Commodore Amiga | `amiga.dll` | PUAE | `.adf` `.hdf` `.lha`, `.iso` `.cue` `.chd` `.m3u` `.toc` (CD32), `.zip` `.7z` `.rar` |
| Commodore 64 | `c64.dll` | VICE | `.d64` `.t64` `.prg` `.crt`, `.zip` `.7z` `.rar` |
| ZX Spectrum | `spectrum.dll` | Fuse | `.tzx` `.tap` `.z80` `.sna`, `.zip` `.7z` `.rar` |
| Panasonic 3DO | `3do.dll` | Opera | `.iso` `.cue` `.chd` `.m3u` `.toc`, `.zip` `.7z` `.rar` |
| MS-DOS | `dosbox_pure.dll` | DOSBox Pure | `.zip` `.7z` `.rar` (pasta do jogo) |
| Arcade | `arcade_fbneo.dll` / `mame2003.dll` / `mame2010.dll` / `dreamcast.dll` (Naomi) | MAME 0.289 (apesar do nome do arquivo) / MAME 2003 / MAME 2010 / Flycast | `.zip` `.7z` `.rar` (romset completo) |

Jogos de arcade passam por uma troca automática e silenciosa de core: o app tenta cada candidato em sequência (e, desde a versão atual, também detecta um core que "carrega com sucesso" mas trava logo em seguida, descartando e tentando o próximo sozinho). Romsets Naomi/Atomiswave conhecidos (~210 títulos catalogados em `app/gamedb/naomi_atomiswave.json`, extraído do código-fonte real do MAME) vão direto para o Flycast, sem passar pela tentativa nos cores MAME primeiro.

---

## 🚀 Recursos Principais

* **Identificação e Extração de Arquivos Compactados:** Carrega diretamente jogos compactados em `.ZIP`, `.7Z` e `.RAR` de forma transparente com cache dinâmico em `cache/`.
* **Sistema Completo de Save State & Load State:**
  * 10 slots independentes por jogo (`0` a `9`).
  * Gravação persistente no disco em `saves/<Sistema>/<NomeDoJogo>.state<Slot>`.
  * Notificações HUD em tempo real na tela do jogo (*Toast Notifications*).
* **Atalhos Rápidos de Teclado:**
  * `F12`: Abrir o Menu OSD durante o jogo (`Esc` tambem fecha, mas nao abre - durante a partida `Esc` e `Tab` sao teclas do jogo).
  * `F5` ou `F2`: **Quick Save State** no slot ativo.
  * `F8` ou `F4`: **Quick Load State** do slot ativo.
  * `F6` / `F7`: Alternar Slot Anterior / Próximo (0 a 9).
  * `F9`: **Captura de Tela** em alta resolução salva na pasta `screenshots/`.
  * `Alt + Enter`: Alternar Modo Janela / Tela Cheia (*Fullscreen*).
* **Atalhos Rápidos no Controle (XInput / Xbox):**
  * `Select + R1 (RB)`: Salvar Estado Rápido.
  * `Select + L1 (LB)`: Carregar Estado Rápido.
  * `Start + Select` ou `Guide`: Alternar o Menu OSD do Karamelo.
* **Mouse como caneta/stylus:** em sistemas com tela de toque (DS, 3DS), o botão esquerdo do mouse funciona como caneta sobre a imagem do jogo. O cursor do Windows some sozinho enquanto o app está em foco.
* **Pipeline de Áudio em Ring Buffer:** buffers circulares com reciclagem `WHDR_DONE`, detecção automática da taxa nativa da placa de som (WASAPI), recalibração a cada jogo carregado e fila de latência que cresce sozinha se detectar underruns recorrentes numa máquina mais lenta.
* **Ports & Recomp:** categoria própria no menu principal com jogos "recompilados" nativamente (tecnologia N64Recomp e afins) - baixa, extrai e abre a versão mais recente de cada projeto direto do GitHub, sem sair do app. Ver [CREDITS.md](CREDITS.md) para a lista completa e os repositórios de origem.
* **Shaders e Filtros CRT:**
  * Aspect Ratio: `Original`, `4:3`, `16:9`.
  * Shader/Scanlines (10 opções): `Off (Raw)`, `Scanlines Leve`, `CRT Suave (TV)`, `PVM Pro 600TVL`, `Sony Trinitron`, `Arcade Shadow`, `Scanlines 50%`, `NTSC Composite`, `LCD Matrix`, `Tubo Curvado 3D`.
  * Papéis de Parede: 4 padrões embutidos (`None`, `Static Noise`, `Parallax Stars`, `Cyber Grid`) + qualquer `.raw` colocado em `Wallpapers/` é reconhecido automaticamente, sem precisar recompilar.
  * 6 Temas de Cor OSD: `Red`, `Blue`, `Green`, `Amber`, `Gray`, `Dark`.
* **Rolagem de nome comprido:** nomes de jogo ou títulos de tela que não cabem no espaço da OSD rolam automaticamente depois de alguns segundos parado (ajustável em Video > Rolagem Nome, incluindo desativar).

---

## 🏆 RetroAchievements

Integração via [rcheevos](https://github.com/RetroAchievements/rcheevos) v12.4.0 (MIT),
em `third_party/rcheevos/`.

Para ativar, preencha `Config/retroachievements.cfg` (relativo à pasta do executável):

```
username=SeuUsuarioRA
password=suaSenha
```

No primeiro login bem-sucedido o token é gravado e **a senha é apagada do
arquivo** — ela não fica em disco depois disso.

* `hardcore=1` segue a regra do site e **bloqueia savestates**. Comece com `0`.
* Cores sem `retro_get_memory_data` não suportam conquistas. O `n64_gopher.dll`
  (Gopher64) tem suporte real a conquistas, mas não é mais o padrão do menu
  "N64 Core" (trocado para ParaLLEl N64 por estabilidade - Gopher64 tem um bug
  conhecido de corrupção de memória ao fechar a sessão, em correção). O
  `n64.dll` genérico (usado só como último recurso, se nenhum dos três outros
  cores existir em disco) não exporta memória e não rende conquistas.
* Jogos traduzidos ou com hack normalmente não são reconhecidos, porque a
  identificação é por hash do conteúdo original.

---

## ⚠️ Higiene do repositório

**Não versione BIOS nem ROMs.** Redistribuí-las é violação de direito autoral. O
`.gitignore` cobre `bios/`, `roms/`, `cores/`, `saves/` e `cache/` (em qualquer
nível do repositório) — confira com `git check-ignore -v <arquivo>` antes de
adicionar qualquer coisa nova.

**Não versione `Config/`.** O `retroachievements.cfg` guarda o token da conta
depois do primeiro login. O diretório está ignorado e nenhum `.cfg` foi
commitado neste repositório — conferido varrendo o histórico inteiro. Mantenha
assim.

**Não versione credenciais de deploy.** O endereço e o usuário do servidor ficam
em `deploy_env.bat`, que está no `.gitignore`; o que vai pro repositório é o
`deploy_env.example.bat`, com valores de exemplo.

> **Histórico anterior.** O repositório que antecedeu este (antes do commit
> `troca de repositorio.`) tinha um `Config/retroachievements.cfg` com o token
> do RetroAchievements preenchido. Esse commit **não existe aqui** e nenhum
> `.cfg` aparece no histórico deste repositório, mas o token continua
> recuperável em qualquer cópia do repositório antigo. Se ele ainda estiver
> acessível em algum lugar, troque a senha da conta no RetroAchievements: isso
> revoga o token antigo e é o único passo que resolve de verdade, porque
> histórico já clonado é imutável.

---

## 💾 BIOS

Cada core procura a BIOS por um nome exato e num lugar exato. A lista completa,
sistema por sistema — o que é obrigatório, o que é opcional, MD5 esperado e
qual sistema não roda **nenhum** jogo sem ela — está em:

**[`packaging/bios-guide/BIOS_NECESSARIOS.txt`](packaging/bios-guide/BIOS_NECESSARIOS.txt)**

Esse arquivo é a fonte única de verdade sobre BIOS neste projeto (é o mesmo
que vai para dentro do pacote de release em `bios/BIOS_NECESSARIOS.txt`) —
evite duplicar essa informação em outro lugar, porque foi exatamente uma cópia
desatualizada dela que motivou esta reescrita do README.

Resumo rápido: **3DO, Atari 5200, Atari Lynx e PC-FX não rodam nenhum jogo**
sem a BIOS correspondente. MSX, Dreamcast/Naomi, NeoGeo (AES/MVS/CD), Mega CD,
TurboGrafx-CD, Saturn, NDS, PSP, ColecoVision e **PlayStation 2 (PCSX2/LRPS2 com BIOS real SCPH-70012)** já têm a BIOS completa e
conferida por MD5 neste checkout. PlayStation tem NTSC-U e PAL, falta NTSC-J.
Amiga tem Kickstart de A500/CD32, falta o de A1200 (jogos AGA). O resto
(NES, SNES, N64, Game Boy, GBA, Genesis, WonderSwan, Neo Geo Pocket,
Jaguar, C64, ZX Spectrum padrão, 32X, Atari 2600, DOSBox) não precisa de
nada.

---

## 📁 Estrutura do Projeto

```
Karamelo/
├── src/                       Código-fonte (.cpp)
│   ├── main_win32.cpp         Janela, Direct3D 11, entrada e HUD (Windows)
│   ├── main_linux.cpp         Janela SDL3, renderizador e entrada (Linux e macOS)
│   ├── core_runner.cpp        Thread do core libretro, áudio e escalonamento
│   ├── menu.cpp                Navegação do OSD e seleção de core por sistema
│   ├── osd.cpp                 Buffer raster do OSD (reescrita clean-room)
│   ├── charrom.cpp             Fonte bitmap 8x8 do OSD (reescrita clean-room)
│   ├── archive_helper.cpp      Extração de ZIP/7Z/RAR e escolha do arquivo do jogo
│   ├── chd_reader.cpp          Leitura de imagens CHD (para RetroAchievements)
│   ├── hw_render.cpp           Contexto OpenGL para cores com renderização por hardware
│   ├── input_map.cpp           Mapeamento de teclado/controle
│   ├── netplay.cpp             Netplay via UDP
│   ├── netplay_protocol.cpp    Framing do protocolo de rede do netplay
│   ├── port_runner.cpp         Download e execução de PC Ports (.exe, Linux e Mach-O macOS)
│   ├── retroachievements.cpp   Integração com rcheevos
│   ├── updater.cpp             Auto-atualização do próprio app (multiplataforma)
│   └── karamelo_math.cpp       Funções matemáticas compartilhadas (viewport, clamp, etc.)
├── include/                    Cabeçalhos (.h), incluindo libretro.h e app_info.h
├── packaging/                  Templates versionados de distribuição
│   ├── bios-guide/              Guia completo de BIOS (fonte única de verdade)
│   └── rom-folder-guides/       Um LEIA-ME.txt por sistema de ROM
├── build/                       Objetos intermediários (.obj / .o) — gerado
├── docs/                        Documentação técnica e briefing do site (SITE_SYNC.md)
└── app/                         Pasta de execução
    ├── Karamelo.exe / Karamelo  Binário final
    ├── cores/                   Motores libretro (.dll / .so / .dylib)
    ├── bios/                    BIOS por sistema
    ├── roms/                    Jogos organizados por sistema
    ├── saves/                   Savestates e cartões de memória
    ├── cache/                   Arquivos temporários descompactados
    ├── ports/                   Jogos recompilados baixados sob demanda
    └── Wallpapers/               Papéis de parede do menu
```

O executável localiza `cores/`, `bios/`, `roms/` e `saves/` a partir da própria pasta do executável (ou bundle `.app` no macOS). Os scripts de empacotamento geram os pacotes prontos para distribuição em `dist/`.

---

## 🛠️ Como Compilar

### Windows (x64)
Requisitos: **MSVC BuildTools (C++20 x64)**

```cmd
compile_port.bat
```
* Gera o executável em `app\Karamelo_v<versão>.exe` e executa os testes unitários.
* Para gerar o pacote de distribuição (`dist/Karamelo_v<versão>_Win64.zip`): `package_release.bat`
* Para fazer deploy para o servidor: `deploy_all.bat` (ou `upload_to_server.bat`)

### macOS (Apple Silicon ARM64)
Requisitos: **Xcode Command Line Tools** (Clang C++20) e **SDL3** (`brew install sdl3`)

```bash
./compile_macos.sh
```
* Gera o executável em `app/Karamelo` e executa a suíte de testes.
* Para gerar o pacote de distribuição (`dist/Karamelo_v<versão>_macOS_arm64.tar.gz`):
```bash
./package_macos.sh
```
* Para enviar para o servidor de downloads:
```bash
./deploy_macos.sh
```

### Linux (x86_64)
Requisitos: **GCC ou Clang (C++20)** e **SDL3** (`libsdl3-dev`)

```bash
./compile_linux.sh
```
* Gera o executável em `app/Karamelo` e executa os testes.
* Para gerar o pacote de distribuição (`dist/Karamelo_v<versão>_Linux64.tar.gz`):
```bash
./package_linux.sh
```
* Para enviar para o servidor de downloads:
```bash
./deploy_linux.sh
```

---

## 📜 Licença, Isenção de Responsabilidade e Créditos

* **Canal do YouTube:** [@GuhClemente](https://youtube.com/@GuhClemente)
* **Desenvolvedor:** Guh Clemente
* **Engine de Emulação:** Libretro API Architecture
* **Licença:** [GNU GPL-3.0](LICENSE.md) — explicação em português em [docs/LICENCA.md](docs/LICENCA.md). Qualquer pessoa pode usar, estudar, modificar, forkar e redistribuir o código, inclusive comercialmente. **Em troca, quem distribuir uma versão modificada é obrigado a publicar o código-fonte dela sob a mesma licença** — não existe fork fechado. Os cores libretro em `cores/` são programas independentes carregados em tempo de execução e mantêm cada um a sua própria licença.
* **Bibliotecas de terceiros:** [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) lista cada uma que entra no binário, com a licença original. O único copyleft é o `n64_gopher.dll` (GPL-3.0); o fonte modificado dele está versionado em `third_party/gopher64/`, que é justamente como a GPL é cumprida.
* **Aviso Legal / Disclaimer:** O **Karamelo** é um projeto de software independente e **NÃO possui qualquer afiliação, vínculo ou endosso de Alexey Melnikov, do projeto oficial MiSTer FPGA ou de seus mantenedores**.
* **Cores e Emuladores:** Todos os motores de emulação utilizados são plugins externos independentes compatíveis com a especificação Libretro, desenvolvidos por suas respectivas comunidades e regidos por suas licenças originais.

Lista completa e verificada de cada core/motor/port recompilado, com a
metodologia usada para identificar cada um: [CREDITS.md](CREDITS.md).
