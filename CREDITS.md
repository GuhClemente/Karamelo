# Créditos dos cores

O Karamelo não emula nada por conta própria. Ele é um frontend: carrega
cores [libretro](https://www.libretro.com/) de terceiros, que são quem faz a
emulação. Este arquivo diz qual projeto está por trás de cada DLL.

## Como esta lista foi feita

Cada core foi verificado de duas formas, sem confiar no nome do arquivo:

1. **Tabela de exports do PE** — os 41 arquivos exportam os seis símbolos que a
   API libretro exige (`retro_api_version`, `retro_init`, `retro_run`,
   `retro_load_game`, `retro_get_system_info`, `retro_set_environment`).
2. **Extensões declaradas pelo próprio core**, que são a assinatura do sistema.

O frontend também registra no log, a cada jogo carregado, o que o core diz de
si mesmo:

```
[CORE] 'ParaLLEl N64' v1.0 beed083 ext=n64|v64|z64|bin|u1|ndd|zip
```

Versões não estão listadas aqui de propósito: número em documento envelhece, e
o do log é sempre o real.

## Os cores

| DLL | Motor | Sistema |
|---|---|---|
| `snes.dll` | bsnes | Super Nintendo |
| `nes.dll` | Mesen | NES / Famicom |
| `genesis.dll` | Genesis Plus GX | Mega Drive, Mega CD |
| `32x.dll` | PicoDrive | Sega 32X |
| `sms.dll` | Gearsystem | Master System, Game Gear |
| `pce.dll` | Beetle PCE | PC Engine / TurboGrafx |
| `pcfx.dll` | Beetle PC-FX | PC-FX |
| `psx.dll` | Beetle PSX | PlayStation |
| `ps2.dll` | LRPS2 (PCSX2) / Play! (`ps2_play.dll`) | PlayStation 2 |
| `psp.dll` | PPSSPP | PlayStation Portable |
| `saturn.dll` | Beetle Saturn | Sega Saturn |
| `dreamcast.dll` | Flycast | Dreamcast, Naomi, Atomiswave |
| `gamecube.dll` | Dolphin | GameCube |
| `n64.dll` | build generico, nao identifica o motor | Nintendo 64 |
| `n64_gopher.dll` | Gopher64 (Parallel-RDP) | Nintendo 64 |
| `n64_parallel.dll` | ParaLLEl N64 | Nintendo 64 |
| `n64_mupen.dll` | Mupen64Plus-Next | Nintendo 64 |
| `nds.dll` | melonDS | Nintendo DS |
| `3ds.dll` | Citra | Nintendo 3DS |
| `gb.dll` | Gambatte | Game Boy / Color |
| `gba.dll` | mGBA | Game Boy Advance |
| `neogeo.dll` | Geolith | Neo Geo AES / MVS |
| `neocd_alt.dll` | NeoCD | Neo Geo CD |
| `ngp.dll` | Beetle NeoPop | Neo Geo Pocket |
| `wswan.dll` | Beetle WonderSwan | WonderSwan |
| `lynx.dll` | Handy | Atari Lynx |
| `jaguar.dll` | Virtual Jaguar | Atari Jaguar |
| `atari2600.dll` | Stella | Atari 2600 |
| `atari5200.dll` | Atari800 | Atari 5200 |
| `atari7800.dll` | ProSystem | Atari 7800 |
| `coleco.dll` | (nao identificado) | ColecoVision |
| `msx.dll` | fMSX | MSX / MSX2 |
| `c64.dll` | VICE | Commodore 64 |
| `amiga.dll` | PUAE | Commodore Amiga |
| `spectrum.dll` | Fuse | ZX Spectrum |
| `3do.dll` | Opera | Panasonic 3DO |
| `dosbox_pure.dll` | DOSBox Pure | MS-DOS |
| `arcade_fbneo.dll` | **MAME 0.289** (o nome do arquivo mente) | Arcade |
| `mame2003.dll` | MAME 2003 | Arcade (romset 0.78) |
| `mame2010.dll` | MAME 2010 | Arcade (romset 0.139) |

**40 motores distintos em 41 arquivos.** A diferença é uma só: `n64_parallel.dll`
é cópia byte a byte de `n64.dll`, conferido por hash e não pelo nome. As duas
são usadas — o menu carrega `n64_parallel.dll` pelo nome, e `n64.dll` é o
último recurso genérico quando nenhum dos três cores de N64 existe em disco.

Até 09/09/2026 havia mais quatro arquivos aqui, somando **25 MB embarcados em
todo release sem que nada pudesse carregá-los**: `pcsx2.dll` e
`pcsx2_libretro.dll` (cópias de `ps2.dll`), `play_libretro.dll` (cópia de
`ps2_play.dll`) e `bluemsx.dll`, sobra de uma tentativa abandonada de rotear
MSX para blueMSX em vez de fMSX. Nenhum era referenciado por
`MenuResolveCoreForPath()` nem por qualquer outro caminho de carregamento.
Foram apagados.

Três observações:

- `n64.dll` se identifica apenas como `Nintendo 64 v1.0` e não diz qual motor é.
  Contém `parallel-rdp` internamente, o que sugere relação com o Gopher64 (que
  também usa Parallel-RDP) ou com o ParaLLEl, mas isso é inferência. É também
  o único dos quatro cores de N64 que não exporta memória, e por isso não
  rende conquistas no RetroAchievements. Na prática ele só é usado como último
  recurso, quando nenhum dos outros três existe em disco.
- O core padrão de N64 é o `n64_parallel.dll` (ParaLLEl N64), tanto na opção 0
  do menu "N64 Core" quanto no fallback de `GetN64CoreDll()`.
- `n64_gopher.dll` (Gopher64) é o terceiro selecionável e o único dos quatro
  com suporte real a RetroAchievements, integrado em `49cf96e`. Deixou de ser o
  padrão por estabilidade: tem um bug conhecido de corrupção de memória ao
  encerrar a sessão.
- `coleco.dll` é um core libretro válido e aceita `col|cv|bin|rom`, mas não
  declara um nome que permita identificar o motor com segurança.

## Ports & Recomp

Diferente dos cores acima, esses não são emulados: são jogos "recompilados"
estaticamente para rodar como executável nativo (tecnologia
[N64Recomp](https://github.com/N64Recomp/N64Recomp) e variações da mesma
técnica para outras plataformas), baixados sob demanda da API de Releases do
GitHub direto do projeto de cada um - nada disso vem empacotado dentro do
instalador do Karamelo. Cada linha da tabela foi baixada, extraída e aberta
para confirmar que o link é válido e que o executável certo é identificado (o app prefere o maior/mais raso `.exe` do
pacote, evitando instaladores, ferramentas de build e outros arquivos que às
vezes vêm junto).

A coluna **SO** diz em qual build do Karamelo (Windows, Linux ou macOS) aquele
jogo pode ser baixado: 🪟 Windows, 🐧 Linux, 🍎 macOS (Apple Silicon /
Universal). O total de cada plataforma está logo abaixo da tabela - aqui não,
de propósito: este parágrafo ficou dizendo "19 têm 🍎" por dez dias enquanto a
tabela mudava três vezes. A tabela é a fonte; o rodapé conta uma vez só.

| Jogo | SO | Repositório |
|---|---|---|
| Dr. Mario 64 | 🪟 | [theboy181/drmario64_recomp_plus](https://github.com/theboy181/drmario64_recomp_plus) |
| Zelda 64: Recompiled (OoT/MM) | 🪟🐧🍎 | [Zelda64Recomp/Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) |
| Goemon 64 | 🪟🐧🍎 | [klorfmorf/Goemon64Recomp](https://github.com/klorfmorf/Goemon64Recomp) |
| Dinosaur Planet | 🪟 | [DinosaurPlanetRecomp/dino-recomp](https://github.com/DinosaurPlanetRecomp/dino-recomp) |
| Harvest Moon 64 | 🪟🐧🍎 | [HarvestMoon64Recomp/HarvestMoon64Recomp](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp) |
| Snowboard Kids 2 | 🪟🍎 | [cdlewis/snowboardkids2-recomp](https://github.com/cdlewis/snowboardkids2-recomp) |
| Pokemon Stadium | 🪟 | [mstan/PokemonStadiumRecomp](https://github.com/mstan/PokemonStadiumRecomp) |
| Banjo 64 | 🪟🐧🍎 | [BanjoRecomp/BanjoRecomp](https://github.com/BanjoRecomp/BanjoRecomp) |
| Bomberman 64 | 🪟🐧🍎 | [RevoSucks/BM64Recomp](https://github.com/RevoSucks/BM64Recomp) |
| Chameleon Twist | 🪟 | [Rainchus/ChameleonTwist1-JP-Recomp](https://github.com/Rainchus/ChameleonTwist1-JP-Recomp) |
| Mega Man 64 | 🪟🐧🍎 | [MegaMan64Recomp/MegaMan64Recompiled](https://github.com/MegaMan64Recomp/MegaMan64Recompiled) |
| Quest 64 | 🪟 | [Rainchus/Quest64-Recomp](https://github.com/Rainchus/Quest64-Recomp) |
| Bomberman Hero | 🪟🐧🍎 | [RevoSucks/BMHeroRecomp](https://github.com/RevoSucks/BMHeroRecomp) |
| Zelda OoT (Ship of Harkinian) | 🪟🐧🍎 | [harbourmasters/shipwright](https://github.com/harbourmasters/shipwright) |
| Zelda MM (2 Ship 2 Harkinian) | 🪟🐧🍎 | [harbourmasters/2ship2harkinian](https://github.com/harbourmasters/2ship2harkinian) |
| Star Fox 64 (Starship) | 🪟🐧 | [harbourmasters/starship](https://github.com/harbourmasters/starship) |
| Star Fox (SNES, Enhanced) | 🪟🐧🍎 | [kandowontu/starfox-enhanced](https://github.com/kandowontu/starfox-enhanced) |
| Mario Kart 64 (SpaghettiKart) | 🪟🐧🍎 | [harbourmasters/spaghettikart](https://github.com/harbourmasters/spaghettikart) |
| Super Mario 64 (Ghostship) | 🪟🐧🍎 | [harbourmasters/ghostship](https://github.com/harbourmasters/ghostship) |
| Perfect Dark | 🪟🍎 | [perfect-dark-pc-port/perfect_dark](https://github.com/perfect-dark-pc-port/perfect_dark) |
| Super Mario 64 Coop Deluxe | 🪟🐧🍎 | [coop-deluxe/sm64coopdx](https://github.com/coop-deluxe/sm64coopdx) |
| Animal Crossing (GameCube) | 🪟 | [flyngmt/ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port) |
| Banjo-Kazooie: Nuts & Bolts | 🪟 | [masterspike52/reNut](https://github.com/masterspike52/reNut) |
| Dragon Ball Z Budokai | 🪟 | [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) |
| Infinite Mario 64 | 🪟🐧🍎 | [Brawmario/infinite-mario-64-ever](https://github.com/Brawmario/infinite-mario-64-ever) |
| Jak & Daxter (OpenGOAL) | 🪟 | [open-goal/jak-project](https://github.com/open-goal/jak-project) |
| LoD: Severed Chains | 🪟 | [Legend-of-Dragoon-Modding/Severed-Chains](https://github.com/Legend-of-Dragoon-Modding/Severed-Chains) |
| REDRIVER 2 | 🪟🐧 | [OpenDriver2/REDRIVER2](https://github.com/OpenDriver2/REDRIVER2) |
| Castlevania: Symphony of the Night | 🪟 | [GuhClemente/SymphonyRecomp](https://github.com/GuhClemente/SymphonyRecomp) |
| Sonic 1 Forever | 🪟 | [ElspethThePict/S1Forever](https://github.com/ElspethThePict/S1Forever) |
| Sonic 3 A.I.R. | 🪟🐧🍎 | [Eukaryot/sonic3air](https://github.com/Eukaryot/sonic3air) |
| Sonic Unleashed Recompiled | 🪟 | [hedge-dev/UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) |
| Space Station Silicon Valley | 🪟🍎 | [Cellenseres/SSSV_Recomp](https://github.com/Cellenseres/SSSV_Recomp) |
| Super Mario Bros. Remastered | 🪟🐧 | [JHDev2006/Super-Mario-Bros.-Remastered-Public](https://github.com/JHDev2006/Super-Mario-Bros.-Remastered-Public) |
| Super Mario World | 🪟🐧 | [mstan/SuperMarioWorldRecomp](https://github.com/mstan/SuperMarioWorldRecomp) |
| Super Metroid | 🪟 | [mstan/SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) |
| Viva Pinata: Trouble in Paradise | 🪟 | [SolarCookies/TiP-Recomp](https://github.com/SolarCookies/TiP-Recomp) |
| WipEout Phantom Edition | 🪟 | [wipeout-phantom-edition/wipeout-phantom-edition](https://github.com/wipeout-phantom-edition/wipeout-phantom-edition) |
| OutRun (CannonBall DX) | 🪟 | [Endprodukt/cannonball-dx](https://github.com/Endprodukt/cannonball-dx) |
| Valkyrie Profile | 🪟🐧🍎 | [Ed1z19/ValkyrieRecomp](https://github.com/Ed1z19/ValkyrieRecomp) |
| Wave Race 64 | 🪟🍎 | [elliotttate/wave-race-64-recomp](https://github.com/elliotttate/wave-race-64-recomp) |
| Pokemon Snap | 🪟🐧🍎 | [JackandBeans/Snap64Recomp](https://github.com/JackandBeans/Snap64Recomp) |
| Body Harvest | 🪟🐧 | [danielgomesvieira2000/body-harvest-recomp](https://github.com/danielgomesvieira2000/body-harvest-recomp) |
| Diddy Kong Racing | 🪟🐧 | [ThatGuyMcd/DKR-R](https://github.com/ThatGuyMcd/DKR-R) |
| Diablo (DevilutionX) | 🪟🐧🍎 | [diasurgical/devilutionX](https://github.com/diasurgical/devilutionX) |
| Star Wars: Dark Forces (TFE) | 🪟 | [TheForceEngine/TheForceEngine](https://github.com/TheForceEngine/TheForceEngine) |
| Fallout (Community Edition) | 🪟🐧🍎 | [alexbatalov/fallout1-ce](https://github.com/alexbatalov/fallout1-ce) |
| Pokemon Red and Blue (reblue) | 🪟🐧 | [zolaware/reblue](https://github.com/zolaware/reblue) |
| Spider-Man (OpenSpidey) | 🪟 | [GTTeancum/OpenSpideyPS1](https://github.com/GTTeancum/OpenSpideyPS1) |
| F-Zero X | 🪟🍎 | [craigshaw/FZeroRecomp](https://github.com/craigshaw/FZeroRecomp) |
| AeroGauge | 🪟🐧 | [alondero/aerogauge-recomp](https://github.com/alondero/aerogauge-recomp) |
| Crash Bandicoot | 🪟🐧 | [Matteo842/CrashBandicoot-Launcher](https://github.com/Matteo842/CrashBandicoot-Launcher) |
| Super Mario Strikers | 🪟🐧🍎 | [new-coke/strikers](https://github.com/new-coke/strikers) |
| Pikmin (Open Nectar) | 🪟🐧 | [SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port](https://github.com/SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port) |
| Soulcalibur II (Ring Out) | 🪟🐧 | [jackpoison-prog/RingOut](https://github.com/jackpoison-prog/RingOut) |
| Super Smash Bros. Melee (Melee Unlocked) | 🪟 | [Hero88go/melee-unlocked](https://github.com/Hero88go/melee-unlocked) |
| Monster Hunter Portable 3rd (Yakumo) | 🪟🐧🍎 | [TeamGDB/Yakumo](https://github.com/TeamGDB/Yakumo) |
| Star Fox Adventures (Foxhollow) | 🪟🐧🍎 | [JackPriceBurns/foxhollow](https://github.com/JackPriceBurns/foxhollow) |

**33 de 58 têm build Linux** e **27 de 58 têm build macOS** (Apple Silicon /
Universal), verificado em 21/09/2026 contra a release mais recente de cada
repositório (Melee Unlocked, adicionado em 27/09/2026, só publica Windows;
Yakumo, adicionado em 27/09/2026, e Foxhollow, adicionado em 29/09/2026,
publicam os três). Os outros publicam build utilizável apenas no Windows por este
app. Revisado em 27/09/2026: Super Metroid voltou a 🪟 apenas (a v0.3.9 só
publica Windows; até a v0.3.8 havia Linux e macOS), Pokemon Snap ganhou 🍎
(a v1.1.0 é a primeira com `macos-universal.zip`) e Sonic 3 A.I.R. ganhou 🍎
(o `.dmg` da release estável falhava na extração, corrigida nesta data). No macOS, o Karamelo suporta pacotes `.app`, arquivos `.zip`,
`.tar.xz`/`.tar.gz` e imagens de disco `.dmg` (montadas e extraídas
transparentemente via `hdiutil`) com binários Mach-O nativos e prioriza
compilações ARM64.
No Linux, o extrator abre `.zip`, `.tar.gz`, `.tar.xz` e pacotes `.AppImage`
diretamente, reconhecendo também binários avulsos sem nenhum empacotamento.

Um detalhe só da linha do Valkyrie Profile: diferente de todo o resto da
tabela, o zip baixado não traz o jogo pronto - ele traz um assistente que
precisa de Python 3 instalado e baixa uma toolchain de compilação na primeira
execução para gerar o binário a partir dos discos do jogador. E o motor por
trás (PSXRecomp) é licenciado PolyForm Noncommercial, não a mesma licença dos
demais projetos desta lista - isso não afeta a licença do Karamelo em si (o
port é um binário externo, baixado sob demanda, nunca embutido), mas vale
saber antes de indicar essa entrada para alguém.

E um detalhe da linha do Melee Unlocked: recompilação estática do Super Smash
Bros. Melee **NTSC 1.02** (GameCube) com Slippi online e FPS destravado,
licença GPL-2.0, ainda em beta. O jogador fornece o próprio `.iso` em
`roms/GameCube/` (nome contendo "melee"); o Karamelo lê o cabeçalho do disco
e só usa o que for `GALE01` revisão 2 — então ISOs 1.00, Training Mode ou
20XX na mesma pasta são ignorados, e se só houver esses o aviso diz que o
disco é o errado. O ISO é lido no lugar, não copiado. Jogo online exige conta
Slippi (logada uma vez pelo Slippi Launcher); offline não.

Um detalhe da linha do Foxhollow: é um port nativo do **Star Fox Adventures**
(GameCube) feito a partir da decompilação SFA-Decomp e do renderizador
Aurora, licença CC0-1.0. É a única linha da tabela que **não baixa do GitHub**:
o release do GitHub não traz binários, e os builds oficiais saem do servidor
do próprio projeto (`api.foxhollow.dev/releases`, o mesmo que o launcher deles
usa). O jogador põe o `.iso` ou `.rvz` do próprio disco em `roms/GameCube/`
(nome contendo "fox" e "adventures"); o Karamelo passa o caminho para o jogo,
sem copiar nada. Aceita USA 1.0/1.1, Europa 1.0/1.1 e Japão 1.0.

E um detalhe da linha do Yakumo: é o **Monster Hunter Portable 3rd HD Ver.**
(PSP) recompilado estaticamente para C++ (PSPRecomp, Vulkan + SDL3, licença
MIT), o primeiro recompilado de **PSP** da tabela. O jogo nunca saiu
oficialmente fora do Japão, então a versão oficial é **em japonês**; existe
tradução para inglês feita por fãs, e o próprio README do Yakumo cita o jogo
com esse patch. O Yakumo só aceita a imagem `NPJB-40001` — o ISO de PSP que
vem dentro da versão HD do PS3 — e confere o código do disco e o executável
criptografado: um patch que só troca textos passa; um ISO de UMD comum
(ULJM), comprimido (`.cso`) ou com o executável alterado é recusado. O Karamelo não mexe no ISO: na primeira abertura o
próprio Yakumo mostra uma tela de setup (funciona com controle) para escolher
a imagem, confere e prepara o jogo. O pacote tem ~220 MB e o projeto está em
*alpha* (v0.6.0-alpha.4 em 24/09/2026), com releases quase diárias.
Conferido em 27/09/2026: instalação pelo Karamelo no Windows (abre em Vulkan
na tela de setup), no Linux (pacote certo, binário sem biblioteca faltando)
e no macOS (o `.dmg` instala; abre nativo em ARM64, Vulkan via MoltenVK,
na tela de setup — testado num Mac M1).

E um detalhe da linha do Wave Race 64: o projeto se descreve como "em beta" e
o pacote baixado é bem maior que o normal desta tabela (~390 MB, contra
dezenas de MB da maioria) porque embute um pacote de texturas HD e trilha
sonora substituta junto com o jogo recompilado - baixa e roda normalmente,
só demora mais.

Assim como os cores, nenhum ROM/disco/ISO é baixado ou distribuído pelo app -
só o executável de cada projeto, que é open source e distribuído livremente
pelos próprios desenvolvedores. A imagem original do jogo continua sendo
responsabilidade de quem usa o app possuir legalmente; onde o próprio port
sabe pedir a ROM (a maioria), ele pede na primeira execução. O CannonBall DX é
diferente dos demais nesse ponto: ele não pede a ROM sozinho, então o app
procura um `outrun.zip` (o romset MAME do OutRun original) já presente em
`roms/Arcade`/`roms/MAME` e copia automaticamente para dentro da pasta do
port na primeira vez que ele é aberto.

O mecanismo de download (escolha do asset certo entre vários que uma release
pode oferecer, e a preferência por executável raso/não-nested ao invés do
maior arquivo) foi desenhado depois de estudar o código-fonte real de
[SirDiabo/GithubLauncher](https://github.com/SirDiabo/GithubLauncher) e seu
fork [dobsondev/N64RecompLauncher](https://github.com/dobsondev/N64RecompLauncher)
- nenhum código foi copiado, mas a lógica de detecção foi adaptada de lá
depois que a abordagem original deste projeto (pegar o maior `.exe`) errou o
executável em mais de um port real.

### Fora do menu: The Darkness (Xbox 360)

| Jogo | SO | Repositório |
|---|---|---|
| The Darkness (Xbox 360) | 🪟 | [portingpete/The-Darkness-Recomp](https://github.com/portingpete/The-Darkness-Recomp) |

**Não entra na tabela acima nem em nenhuma contagem.** A entrada existe no
código (`hidden = true` em `src/port_runner.cpp`) e funciona — dump preparado
e jogo aberto em 27/09/2026 com um disco real —, mas o runtime do projeto ainda
está instável demais para oferecer, então fica fora do menu e do site até
segunda ordem. Para testar: `Karamelo.exe --prepare-port TheDarknessRecomp`.

É o primeiro recompilado de **Xbox 360** (XenonRecomp, GPL-3.0) e o jogador precisa fornecer o dump do
próprio disco. O projeto original pede para extrair o ISO com o "Xbox 360
Image Browser" e rodar o XexTool do xorloser duas vezes; o Karamelo faz as
duas coisas sozinho — basta pôr o `.iso` em `roms/Xbox360/` — e só aceita o
resultado se o SHA-256 de `basefile.exe` e `_uncrypted.xex` bater com o que o
próprio `DarkRecomp.exe` instalado exige. O layout exato do `_uncrypted.xex`
foi portado do [XexTool-RE](https://github.com/RexxColder/XexTool-RE) (MIT,
© 2026 Logan Greer e colaboradores), reimplementação aberta do XexTool 6.3.
Como funciona: [docs/XBOX360_RECOMP.md](docs/XBOX360_RECOMP.md). O repositório só publica
*prereleases* (v0.1.2 em 27/09/2026) e o autor chama o runtime de "development
build, gameplay incomplete".

## Bibliotecas usadas diretamente

| Biblioteca | Para quê | Licença |
|---|---|---|
| [rcheevos](https://github.com/RetroAchievements/rcheevos) | RetroAchievements | MIT |
| [libchdr](https://github.com/rtissera/libchdr) | leitura de CHD | BSD-3-Clause |

Essas duas estão em `third_party/` com o fonte junto.

## ⚠ Licenças dos cores: pendência antes de distribuir

**Esta lista identifica os cores, mas não resolve a questão de licenciamento.**

As licenças diferem entre si. Vários cores são GPL, o que obriga quem
distribui o binário a disponibilizar o código-fonte correspondente. E alguns
têm, historicamente, cláusulas restringindo uso comercial — Genesis Plus GX,
FinalBurn Neo e as builds antigas de MAME entre eles.

Nenhuma dessas licenças foi conferida arquivo a arquivo. Enquanto essa
verificação não for feita no repositório de origem de cada core, o pacote que
inclui as DLLs carrega uma obrigação de origem desconhecida — e chutar aqui
seria pior do que registrar a pendência.

A saída que boa parte dos frontends adota é **não incluir as DLLs** no pacote,
deixando o download por conta do usuário. Isso elimina a obrigação de
redistribuição de uma vez, ao custo de um passo a mais na primeira execução.

Questão separada, e mais simples: os arquivos de **BIOS** já estão cobertos
pelo `.gitignore` e não entram em pacote nenhum.
