# Créditos dos cores

O MiSTer 4 ALL não emula nada por conta própria. Ele é um frontend: carrega
cores [libretro](https://www.libretro.com/) de terceiros, que são quem faz a
emulação. Este arquivo diz qual projeto está por trás de cada DLL.

## Como esta lista foi feita

Cada core foi verificado de duas formas, sem confiar no nome do arquivo:

1. **Tabela de exports do PE** — os 40 arquivos exportam os seis símbolos que a
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
| `ps2.dll` | Play! | PlayStation 2 |
| `psp.dll` | PPSSPP | PlayStation Portable |
| `saturn.dll` | Beetle Saturn | Sega Saturn |
| `dreamcast.dll` | Flycast | Dreamcast, Naomi, Atomiswave |
| `gamecube.dll` | Dolphin | GameCube |
| `n64.dll` | build generico, nao identifica o motor | Nintendo 64 |
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
| `arcade_fbneo.dll` | FinalBurn Neo | Arcade |
| `mame2003.dll` | MAME 2003 | Arcade (romset 0.78) |
| `mame2010.dll` | MAME 2010 | Arcade (romset 0.139) |

**39 motores distintos em 40 arquivos.** `play_libretro.dll` é cópia byte a byte
de `ps2.dll` e nada no código a referencia — é peso morto e pode ser removida.

Duas observações:

- `n64.dll` se identifica apenas como `Nintendo 64 v1.0` e não diz qual motor é.
  Contém `parallel-rdp` internamente, o que sugere um build do ParaLLEl, mas
  isso é inferência. É também o único dos três que não exporta memória, e por
  isso não rende conquistas no RetroAchievements.
- `coleco.dll` é um core libretro válido e aceita `col|cv|bin|rom`, mas não
  declara um nome que eu conseguisse identificar com segurança.

## Ports & Recomp

Diferente dos cores acima, esses não são emulados: são jogos "recompilados"
estaticamente para rodar como executável nativo do Windows (tecnologia
[N64Recomp](https://github.com/N64Recomp/N64Recomp) e variações da mesma
técnica para outras plataformas), baixados sob demanda da API de Releases do
GitHub direto do projeto de cada um - nada disso vem empacotado dentro do
instalador do MiSTer 4 ALL. Cada linha foi baixada, extraída e aberta de
verdade nesta sessão para confirmar que o link ainda é válido e que o
executável certo é identificado (o app prefere o maior/mais raso `.exe` do
pacote, evitando instaladores, ferramentas de build e outros arquivos que às
vezes vêm junto).

| Jogo | Repositório |
|---|---|
| Dr. Mario 64 | [theboy181/drmario64_recomp_plus](https://github.com/theboy181/drmario64_recomp_plus) |
| Zelda 64: Recompiled (OoT/MM) | [Zelda64Recomp/Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) |
| Goemon 64 | [klorfmorf/Goemon64Recomp](https://github.com/klorfmorf/Goemon64Recomp) |
| Dinosaur Planet | [DinosaurPlanetRecomp/dino-recomp](https://github.com/DinosaurPlanetRecomp/dino-recomp) |
| Harvest Moon 64 | [HarvestMoon64Recomp/HarvestMoon64Recomp](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp) |
| Snowboard Kids 2 | [cdlewis/snowboardkids2-recomp](https://github.com/cdlewis/snowboardkids2-recomp) |
| Pokemon Stadium | [mstan/PokemonStadiumRecomp](https://github.com/mstan/PokemonStadiumRecomp) |
| Banjo 64 | [BanjoRecomp/BanjoRecomp](https://github.com/BanjoRecomp/BanjoRecomp) |
| Bomberman 64 | [RevoSucks/BM64Recomp](https://github.com/RevoSucks/BM64Recomp) |
| Chameleon Twist | [Rainchus/ChameleonTwist1-JP-Recomp](https://github.com/Rainchus/ChameleonTwist1-JP-Recomp) |
| Mega Man 64 | [MegaMan64Recomp/MegaMan64Recompiled](https://github.com/MegaMan64Recomp/MegaMan64Recompiled) |
| Quest 64 | [Rainchus/Quest64-Recomp](https://github.com/Rainchus/Quest64-Recomp) |
| Bomberman Hero | [RevoSucks/BMHeroRecomp](https://github.com/RevoSucks/BMHeroRecomp) |
| Zelda OoT (Ship of Harkinian) | [harbourmasters/shipwright](https://github.com/harbourmasters/shipwright) |
| Zelda MM (2 Ship 2 Harkinian) | [harbourmasters/2ship2harkinian](https://github.com/harbourmasters/2ship2harkinian) |
| Star Fox 64 (Starship) | [harbourmasters/starship](https://github.com/harbourmasters/starship) |
| Star Fox (SNES, Enhanced) | [kandowontu/starfox-enhanced](https://github.com/kandowontu/starfox-enhanced) |
| Mario Kart 64 (SpaghettiKart) | [harbourmasters/spaghettikart](https://github.com/harbourmasters/spaghettikart) |
| Super Mario 64 (Ghostship) | [harbourmasters/ghostship](https://github.com/harbourmasters/ghostship) |
| Perfect Dark | [perfect-dark-pc-port/perfect_dark](https://github.com/perfect-dark-pc-port/perfect_dark) |
| Super Mario 64 Coop Deluxe | [coop-deluxe/sm64coopdx](https://github.com/coop-deluxe/sm64coopdx) |
| Animal Crossing (GameCube) | [flyngmt/ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port) |
| Banjo-Kazooie: Nuts & Bolts | [masterspike52/reNut](https://github.com/masterspike52/reNut) |
| Dragon Ball Z Budokai | [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) |
| Infinite Mario 64 | [Brawmario/infinite-mario-64-ever](https://github.com/Brawmario/infinite-mario-64-ever) |
| Jak & Daxter (OpenGOAL) | [open-goal/jak-project](https://github.com/open-goal/jak-project) |
| LoD: Severed Chains | [Legend-of-Dragoon-Modding/Severed-Chains](https://github.com/Legend-of-Dragoon-Modding/Severed-Chains) |
| REDRIVER 2 | [OpenDriver2/REDRIVER2](https://github.com/OpenDriver2/REDRIVER2) |
| Castlevania: Symphony of the Night | [gfdac/SymphonyRecomp](https://github.com/gfdac/SymphonyRecomp) |
| Sonic 1 Forever | [ElspethThePict/S1Forever](https://github.com/ElspethThePict/S1Forever) |
| Sonic 3 A.I.R. | [Eukaryot/sonic3air](https://github.com/Eukaryot/sonic3air) |
| Sonic Unleashed Recompiled | [hedge-dev/UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) |
| Space Station Silicon Valley | [Cellenseres/SSSV_Recomp](https://github.com/Cellenseres/SSSV_Recomp) |
| Super Mario Bros. Remastered | [JHDev2006/Super-Mario-Bros.-Remastered-Public](https://github.com/JHDev2006/Super-Mario-Bros.-Remastered-Public) |
| Super Mario World | [mstan/SuperMarioWorldRecomp](https://github.com/mstan/SuperMarioWorldRecomp) |
| Super Metroid | [mstan/SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) |
| Viva Pinata: Trouble in Paradise | [SolarCookies/TiP-Recomp](https://github.com/SolarCookies/TiP-Recomp) |
| WipEout Phantom Edition | [wipeout-phantom-edition/wipeout-phantom-edition](https://github.com/wipeout-phantom-edition/wipeout-phantom-edition) |

Assim como os cores, nenhum ROM/disco/ISO é baixado ou distribuído pelo app -
só o executável de cada projeto, que é open source e distribuído livremente
pelos próprios desenvolvedores. A imagem original do jogo continua sendo
responsabilidade de quem usa o app possuir legalmente; onde o próprio port
sabe pedir a ROM (a maioria), ele pede na primeira execução.

O mecanismo de download (escolha do asset certo entre vários que uma release
pode oferecer, e a preferência por executável raso/não-nested ao invés do
maior arquivo) foi desenhado depois de estudar o código-fonte real de
[SirDiabo/GithubLauncher](https://github.com/SirDiabo/GithubLauncher) e seu
fork [dobsondev/N64RecompLauncher](https://github.com/dobsondev/N64RecompLauncher)
- nenhum código foi copiado, mas a lógica de detecção foi adaptada de lá
depois que a abordagem original deste projeto (pegar o maior `.exe`) errou o
executável em mais de um port real.

## Bibliotecas usadas diretamente

| Biblioteca | Para quê | Licença |
|---|---|---|
| [rcheevos](https://github.com/RetroAchievements/rcheevos) | RetroAchievements | MIT |
| [libchdr](https://github.com/rtissera/libchdr) | leitura de CHD | BSD-3-Clause |

Essas duas estão em `third_party/` com o fonte junto.

## ⚠ Licenças dos cores: pendência antes de distribuir

**Esta lista identifica os cores, mas não resolve a questão de licenciamento** —
e ela ficou maior, porque o projeto passou de 22 para 39 motores.

As licenças diferem entre si. Vários são GPL, o que exige disponibilizar o
código-fonte correspondente a quem recebe o binário. E alguns têm,
historicamente, cláusulas restringindo uso comercial — Genesis Plus GX,
FinalBurn Neo e as builds antigas de MAME entre eles.

Não confirmei licença nenhuma aqui, e não vou chutar: errar isso é pior do que
não escrever nada. **Antes de publicar o projeto com as DLLs incluídas**, cada
core precisa ser conferido no repositório de origem.

A saída mais simples, e a que boa parte dos frontends adota, é **não incluir as
DLLs** no pacote e deixar o usuário baixá-las por conta própria. Isso remove a
obrigação de redistribuição de uma vez.

Isso é separado, e menos grave, do que a questão dos arquivos de BIOS — que já
estão cobertos pelo `.gitignore` e não devem entrar em nenhum pacote.
