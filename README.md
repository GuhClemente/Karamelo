# Karamelo Emulador — Native Windows x64 Frontend & Retrogaming Emulation Suite

> **Software Gratuito e Independente (Freeware)**  
> Canal do YouTube: **[@GuhClemente](https://youtube.com/@GuhClemente)**  
> Desenvolvido por: **Guh Clemente & Antigravity Pair Team**  
> Core Engine: **Libretro API Architecture**

---

## 🌟 Visão Geral

O **Karamelo Emulador** é um port nativo em C++20 (64-bit) de alto desempenho da interface de usuário e OSD do MiSTer para o ambiente Windows x86_64, integrado a uma engine modular de execução de cores Libretro com escalonador de 60 FPS com correção de aspecto, filtros CRT, Ring Buffer de áudio estéreo de baixa latência e suporte nativo a **35 sistemas** através de **40 motores de emulação distintos** (lista completa e verificada em [CREDITS.md](CREDITS.md)). Também inclui uma categoria de "Ports & Recomp" com jogos recompilados nativamente (Zelda 64: Recompiled, Jak & Daxter, Super Mario 64, e dezenas de outros — ver CREDITS.md).

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
| Arcade | `arcade_fbneo.dll` / `mame2003.dll` / `mame2010.dll` / `dreamcast.dll` (Naomi) | FinalBurn Neo / MAME 2003 / MAME 2010 / Flycast | `.zip` `.7z` `.rar` (romset completo) |

Jogos de arcade passam por uma troca automática e silenciosa de core: o app tenta cada candidato em sequência (e, desde a versão atual, também detecta um core que "carrega com sucesso" mas trava logo em seguida, descartando e tentando o próximo sozinho). Romsets Naomi/Atomiswave conhecidos (~210 títulos catalogados em `app/gamedb/naomi_atomiswave.json`, extraído do código-fonte real do MAME) vão direto para o Flycast, sem passar pela tentativa nos cores MAME primeiro.

---

## 🚀 Recursos Principais

* **Identificação e Extração de Arquivos Compactados:** Carrega diretamente jogos compactados em `.ZIP`, `.7Z` e `.RAR` de forma transparente com cache dinâmico em `cache/`.
* **Sistema Completo de Save State & Load State:**
  * 10 slots independentes por jogo (`0` a `9`).
  * Gravação persistente no disco em `saves/<Sistema>/<NomeDoJogo>.state<Slot>`.
  * Notificações HUD em tempo real na tela do jogo (*Toast Notifications*).
* **Atalhos Rápidos de Teclado:**
  * `F12` ou `Tab`: Abrir / Fechar o Menu OSD do Karamelo.
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

## ⚠️ Antes de publicar o repositório

**Não versione BIOS nem ROMs.** Redistribuí-las é violação de direito autoral. O
`.gitignore` cobre `bios/`, `roms/`, `cores/`, `saves/` e `cache/` (em qualquer
nível do repositório) — confira com `git check-ignore -v <arquivo>` antes de
adicionar qualquer coisa nova.

**Há um token de conta no histórico.** O commit `02c7fb4` inclui
`Config/retroachievements.cfg` com o token do RetroAchievements preenchido, e
esse commit já foi enviado para o remoto. O arquivo foi removido do rastreamento
e o diretório `Config/` está ignorado, mas **isso não apaga o passado**: o token
continua recuperável no histórico.

Antes de tornar o repositório público:

1. **Invalide o token** — troque a senha da conta no RetroAchievements. Isso
   revoga o token antigo e é o único passo que realmente resolve, porque o
   histórico é imutável.
2. **Reescreva o histórico** com `git filter-repo` (ou comece um repositório
   novo) para remover o arquivo dos commits antigos.

Fazer só o passo 2 sem o 1 não basta: quem já clonou continua com o token.

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
├── src/                       Código-fonte (.cpp), 15 arquivos
│   ├── main_win32.cpp         Janela, loop de apresentação, entrada e HUD
│   ├── core_runner.cpp        Thread do core libretro, áudio e escalonamento
│   ├── menu.cpp                Navegação do OSD e seleção de core por sistema
│   ├── osd.cpp                 Buffer do OSD portado do Main_MiSTer
│   ├── charrom.cpp             Fonte 8x8 do MiSTer
│   ├── archive_helper.cpp      Extração de ZIP/7Z/RAR e escolha do arquivo do jogo
│   ├── chd_reader.cpp          Leitura de imagens CHD (para RetroAchievements)
│   ├── hw_render.cpp           Contexto OpenGL para cores com renderização por hardware
│   ├── input_map.cpp           Mapeamento de teclado/controle
│   ├── netplay.cpp             Netplay via UDP
│   ├── netplay_protocol.cpp    Framing do protocolo de rede do netplay
│   ├── port_runner.cpp         Download/execução dos "Ports & Recomp"
│   ├── retroachievements.cpp   Integração com rcheevos
│   ├── updater.cpp             Auto-atualização do próprio app
│   └── karamelo_math.cpp       Funções matemáticas compartilhadas (viewport, clamp, etc.)
├── include/                    Cabeçalhos (.h), incluindo libretro.h
├── packaging/                  Templates versionados usados por package_release.bat
│   ├── bios-guide/              Guia completo de BIOS (fonte única de verdade)
│   └── rom-folder-guides/       Um LEIA-ME.txt por sistema de ROM
├── build/                       Objetos intermediários (.obj) — gerado
├── docs/                        Capturas e material de referência
└── app/                         Pasta de execução
    ├── Karamelo_v<versão>.exe      Binário final (gerado pelo compile_port.bat)
    ├── cores/                   DLLs libretro
    ├── bios/                    BIOS por sistema (NeoGeo CD em bios/neocd/, Dreamcast em bios/dc/)
    ├── roms/                    Jogos, organizados por sistema
    ├── saves/                   Savestates e memória de cartão
    ├── cache/                   Arquivos extraídos de pacotes compactados
    ├── ports/                   Jogos recompilados baixados sob demanda
    └── Wallpapers/               Papéis de parede do menu
```

O executável precisa ficar dentro de `app/`: ele localiza `cores/`, `bios/`,
`roms/` e `saves/` a partir da própria pasta. `package_release.bat` gera um
pacote de distribuição equivalente em `dist/Karamelo_v<versão>_Win64/`,
com o executável renomeado para `Karamelo.exe`.

---

## 🛠️ Como Compilar

Requisitos: **MSVC BuildTools (C++20 x64)**

```cmd
compile_port.bat
```

O script se orienta pela própria localização, então o projeto pode ser clonado em
qualquer diretório. Os `.obj` vão para `build/` e o executável final é gravado em
`app\Karamelo_v<versão>.exe` (a versão vem de `include/app_info.h`). O script
também compila e roda a suíte de testes unitários automaticamente ao final.

Para gerar um pacote de distribuição completo (com a estrutura de pastas,
guias de BIOS/ROM e cores prontos), use `package_release.bat` em vez de
`compile_port.bat` diretamente.

---

## 📜 Licença, Isenção de Responsabilidade e Créditos

* **Canal do YouTube:** [@GuhClemente](https://youtube.com/@GuhClemente)
* **Desenvolvedor:** Guh Clemente
* **Engine de Emulação:** Libretro API Architecture
* **Licença do Frontend:** Software Gratuito / Freeware (Uso pessoal, não comercial). Código e frontend independentes.
* **Aviso Legal / Disclaimer:** O **Karamelo** é um projeto de software independente desenvolvido para o ecossistema Windows e **NÃO possui qualquer afiliação, vínculo ou endosso de Alexey Melnikov, do projeto oficial MiSTer FPGA ou de seus mantenedores**.
* **Cores e Emuladores:** Todos os motores de emulação utilizados são plugins externos independentes compatíveis com a especificação Libretro, desenvolvidos por suas respectivas comunidades e regidos por suas licenças originais.

Lista completa e verificada de cada core/motor/port recompilado, com a
metodologia usada para identificar cada um: [CREDITS.md](CREDITS.md).
