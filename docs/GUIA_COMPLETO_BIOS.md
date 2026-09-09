# 🕹️ Guia Oficial de BIOS e Arquivos de Sistema — Karamelo

> **Conferindo o que voce baixou:** `tools/check_bios.ps1` compara cada
> arquivo com o MD5 esperado e reconhece ponteiro do Git LFS — o caso real em
> que dois downloads "bem-sucedidos" tinham 131 bytes de texto no lugar da
> ROM, e o sintoma foi um core que simplesmente nao abria nada.


Este guia detalha **todos os arquivos de BIOS, firmwares, chaves criptográficas e assets de sistema** necessários ou recomendados para os **35 sistemas emulados nativamente** no **Karamelo**.

---

## 📦 O Pacote Oficial de BIOS (`Karamelo_Pack_BIOS.zip`)

Para simplificar a configuração, criamos o pacote consolidado **`Karamelo_Pack_BIOS.zip`** com **toda a hierarquia de pastas correta** e os arquivos já conferidos (MD5/SHA-256).

### 🚀 Como Instalar o Pacote de BIOS:
1. Baixe o arquivo `Karamelo_Pack_BIOS.zip`.
2. Extraia o conteúdo diretamente na **pasta raiz da instalação do Karamelo** (onde fica o `Karamelo.exe`).
3. As subpastas `bios/`, `bios/dc/`, `bios/neocd/`, `bios/pcsx2/`, `bios/PPSSPP/`, `bios/dolphin-emu/` e os arquivos de MSX na raiz serão posicionados automaticamente no local correto.
4. Abra o emulador e jogue!

---

## 🚦 Visão Geral por Categoria de Exigência

| Status | Significado | Sistemas |
| :--- | :--- | :--- |
| 🔴 **Obrigatória (Bloqueante)** | O sistema **não inicia nenhum jogo** sem esses arquivos. | 3DO, Atari 5200, Atari Lynx, PC-FX, ColecoVision, PS1, PS2, Saturn, Sega CD, PC Engine CD, NeoGeo CD, MSX. |
| 🟡 **Recomendada / Parcial** | O emulador funciona via HLE, mas a BIOS real traz som autêntico, animação de boot e 100% de compatibilidade. | Nintendo DS, Dreamcast/Naomi, NeoGeo AES/MVS, PSP, GameCube, 3DS (chaves AES), Amiga. |
| 🟢 **Zero Configuração** | O core **não precisa de nenhuma BIOS externa** (arquivos embutidos ou cycle-accurate sem ROM). | SNES, Genesis (cartucho), NES, Game Boy, Game Boy Color, Game Boy Advance, N64, 32X, Atari 2600, Atari Jaguar, Neo Geo Pocket, WonderSwan, DOSBox Pure, C64, ZX Spectrum. |

---

## 📋 Tabela Mestra dos 35 Sistemas

### 1. Sistemas com BIOS Obrigatória (Bloqueantes)

| Sistema | Core / Engine | Nome do Arquivo | Hash MD5 Esperado | Onde Colocar | Observações / Função |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **PlayStation 1 (PS1)** | `psx.dll`<br>*(Beetle PSX)* | `scph5501.bin`<br>`scph5502.bin`<br>`scph5500.bin` | `490f666e1afb15b7362b406ed1cea246`<br>`32736f17079d0b2b70244077792b0794`<br>`9c0421858e217805f4abe18697afeab9` | `app/bios/` | `scph5501` (EUA), `scph5502` (EUR), `scph5500` (JAP). Essencial para boot de discos. |
| **PlayStation 2 (PS2)** | `ps2.dll`<br>*(PCSX2 / LRPS2)* | `SCPH-70012_BIOS_V12_USA_200.BIN`<br>`SCPH-70012_BIOS_V12_USA_200.mec`<br>`SCPH-70012_BIOS_V12_USA_200.nvm`<br>`GameIndex.yaml` | `d333558cc14561c1fdc334c75d5f37b7`<br>`3faf7c064a4984f53e2ef5e80ed543bc`<br>`116b0e0f3d0ecef3997cf53266fa27a1`<br>*(banco de dados)* | `app/bios/` ou `app/bios/pcsx2/bios/` | BIOS real de 4MB (Slim v12) + NVRAM persistente. O Karamelo sincroniza automaticamente para o PCSX2. |
| **Sega CD / Mega CD** | `genesis.dll`<br>*(Genesis Plus GX)* | `bios_CD_U.bin`<br>`bios_CD_E.bin`<br>`bios_CD_J.bin` | `2efd74e3232ff260e30763cb55abb822`<br>`e66fa1dc5820d254611f740e58fa8ad0`<br>`278a9397d192149e84e820ac621a8eff` | `app/bios/` | Obrigatório para boot de discos ISO/BIN/CHD em cada região (EUA/EUR/JAP). |
| **Sega Saturn** | `saturn.dll`<br>*(Beetle Saturn)* | `sega_101.bin`<br>`mpr-17933.bin`<br>`saturn_bios.bin` | `85ec9ca47d8f6807718151cbcca8b964`<br>`255113d5a6ac30128cb5004505f235db`<br>*(variante universal)* | `app/bios/` | Obrigatório para execução precisa dos processadores SH-2 dual. |
| **NeoGeo CD** | `neocd_alt.dll`<br>*(NeoCD)* | `neocd.bin`<br>`neocd_f.rom`<br>`neocd_z.rom`<br>`uni-bioscd.rom` | `df9de49040464eb066914563a6117b07`<br>`5e08cdb65b63b58be4523b1898b2c458`<br>`c1634b868cd7d6928e83344fc8654854`<br>*(UniBIOS CD)* | ⚠️ **`app/bios/neocd/`** | **ATENÇÃO:** Deve ficar dentro da subpasta `neocd/`. Se colocado na raiz de `bios/`, o core acusa *"No BIOS detected!"*. |
| **PC Engine CD / TurboGrafx-CD** | `pce.dll`<br>*(Beetle PCE)* | `syscard3.pce` | `ff1a613d5447d0a8011f09a80f945091` | `app/bios/` | Super CD-ROM2 System Card v3.0. Obrigatório para jogos em CD. |
| **MSX / MSX2 / MSX2+** | `msx.dll`<br>*(fMSX)* | `MSX.ROM`<br>`MSX2.ROM`<br>`MSX2EXT.ROM`<br>`MSX2P.ROM`<br>`MSX2PEXT.ROM`<br>`DISK.ROM`<br>`FMPAC.ROM`<br>`MSXDOS2.ROM` | `364a1a579fe5cb8dba54519bcfcdac0d`<br>`ec3a01c91f24fbddcbcab0ad301bc9ef`<br>`2183c2aff17cf4297bdb496de78c2e8a`<br>`847cc025ffae665487940ff2639540e5`<br>`7c8243c71d8f143b2531f01afa6a05dc`<br>*(BDOS / Disquete)*<br>`6f69cc8b5ed761b03afd78000dfb0e19`<br>`6418d091cd6907bbcf940324339e43bb` | **Raiz do emulador** (`app/`) e `app/bios/` | O core fMSX carrega a partir do diretório atual. O Karamelo distribui na raiz e em `bios/`. |
| **ColecoVision** | `coleco.dll` | `coleco.rom` | `2c66f5911e5b42b8ebe113403548eee7` | `app/bios/` | ROM de boot do BIOS de 8KB. |
| **3DO Interactive** | `3do.dll`<br>*(Opera)* | `panafz1.bin` *(ou `panafz10.bin`)* | `f47264dd47fe30f73ab3c010015c155b` | `app/bios/` | Qualquer BIOS de Panasonic FZ-1, FZ-10 ou Goldstar serve. |
| **Atari 5200** | `atari5200.dll`<br>*(Atari800)* | `5200.rom`<br>`ATARIXL.ROM`<br>`ATARIBAS.ROM`<br>`ATARIOSA.ROM`<br>`ATARIOSB.ROM` | `281f20ea4320404ec820fb7ec0693b38`<br>`06daac977823773a3eea3422fd26a703`<br>`0bac0c6a50104045d902df4503a4c30b`<br>`eb1f32f5d9f382db1bbfb8d7f9cb343a`<br>`a3e8d617c95d08031fe1b20d541434b2` | `app/bios/` | Todos os 5 chips de sistema Atari OS/Basic. |
| **Atari Lynx** | `lynx.dll`<br>*(Handy)* | `lynxboot.img` | `fcd403db69f54290b51035d82f835e7b` | `app/bios/` | Boot ROM de 512 bytes. |
| **PC-FX** | `pcfx.dll`<br>*(Beetle PC-FX)* | `pcfx.rom` | `08e36edbea28a017f79f8d4f7ff9b6d7` | `app/bios/` | BIOS oficial NEC PC-FX. |

---

### 2. Sistemas com BIOS Recomendada ou Assets de Sistema

| Sistema | Core / Engine | Arquivos Necessários | Onde Colocar | Observações / Função |
| :--- | :--- | :--- | :--- | :--- |
| **Sega Dreamcast & Arcade Naomi** | `dreamcast.dll`<br>*(Flycast)* | `dc_boot.bin` *(Dreamcast)*<br>`naomi.zip` *(Naomi Arcade)*<br>`awbios.zip` *(Atomiswave)* | `app/bios/dc/` *(ou `app/bios/`)* | Boot ROM original do Dreamcast (`e10c53c2f8b90bab96ead2d368858623`) e sets de BIOS das placas arcade. |
| **NeoGeo AES / MVS** | `neogeo.dll`<br>*(Geolith)* | `neogeo.zip` *(ou solto em `bios/`: `neo-epo.bin`, `sp-1v1_3db8c.bin`, `uni-bios_*.rom`)* | `app/bios/` | Suporta BIOS originais MVS/AES e UniBIOS (v3.3, v4.0). |
| **Nintendo DS (NDS)** | `nds.dll`<br>*(melonDS)* | `bios7.bin`<br>`bios9.bin`<br>`firmware.bin` | `app/bios/` | ARM7 (`df692a80...`), ARM9 (`a392174e...`) e firmware. Permite inicialização real com animação original do Nintendo DS. |
| **Sony PSP** | `psp.dll`<br>*(PPSSPP)* | Pasta **`PPSSPP/`** completa:<br>• Fontes (`flash0/font/*.pgf`, `Roboto`, `Inconsolata`)<br>• Sons de UI (`system/`)<br>• Shaders, `compat.ini`, `lang/` | ⚠️ **`app/bios/PPSSPP/`** | **CRÍTICO:** Sem essa pasta, a interface interna do PPSSPP fica sem fontes e o jogo congela ao encerrar. Não são ROMs proprietárias, são assets livres do PPSSPP. |
| **Nintendo GameCube** | `gamecube.dll`<br>*(Dolphin)* | Pasta **`dolphin-emu/Sys/`** completa:<br>• Fontes de sistema (`GC/font_*.bin`)<br>• Certificados SSL e DSP firmware | ⚠️ **`app/bios/dolphin-emu/Sys/`** | Assets oficiais de sistema do Dolphin. |
| **Nintendo 3DS** | `3ds.dll`<br>*(Citra)* | `aes_keys.txt` | ⚠️ **`app/saves/3DS/Citra/sysdata/`** | Chaves criptográficas AES para execução de jogos comerciais `.3ds` e `.cia` descriptografados em tempo real. |
| **Commodore Amiga** | `amiga.dll`<br>*(PUAE)* | `kick34005.A500` *(Kickstart 1.3)*<br>`kick40060.CD32` *(Kickstart CD32)*<br>`kick40060.CD32.ext` *(ROM estendida, **obrigatória** para CD32)*<br>`kick40068.A1200` *(Kickstart 3.1)* | `app/bios/` | O core possui fallback AROS embutido, mas Kickstarts originais garantem fidelidade sonora do chip Paula e sincronia AGA. **Jogos de CD32 (`.chd`/`.cue`) exigem o par `kick40060.CD32` + `kick40060.CD32.ext`** — só com o Kickstart, o core para na tela pedindo disquete. |
| **Sega Master System / Game Gear** | `sms.dll`<br>*(Gearsystem)* | `bios_U.sms`<br>`bios_E.sms`<br>`bios_J.sms`<br>`bios.gg` | `app/bios/` | Opcional. Permite visualização do logo da SEGA e tela de abertura original. |
| **Atari 7800** | `atari7800.dll`<br>*(ProSystem)* | `7800 BIOS (U).rom` | `app/bios/` | Opcional. MD5: `0763f1ffb006ddbe32e52d497ee848ae`. |
| **Commodore 64 (C64)** | `c64.dll`<br>*(VICE)* | *(Opcional)* `JiffyDOS_C64.bin`<br>`JiffyDOS_1541-II.bin` | `app/bios/vice/` | O core já traz todo o kernel básico embutido. JiffyDOS acelera o acesso ao disco virtual. |

---

### 3. Sistemas 100% Plug & Play (NÃO precisam de BIOS)

Estes sistemas funcionam **imediatamente**, basta selecionar a ROM ou jogo:

1. **Super Nintendo (SNES)** — *bsnes v115 (emulação ciclo-exata, DSP/SuperFX embutidos)*
2. **Mega Drive / Genesis (Cartucho)** — *Genesis Plus GX*
3. **Sega 32X** — *PicoDrive (HLE de BIOS)*
4. **NES / Famicom** — *Mesen v0.9.9 (255 mappers com timings perfeitos)*
5. **Game Boy / Game Boy Color** — *SameBoy (Cycle-accurate com boot interno)*
6. **Game Boy Advance (GBA)** — *mGBA (BIOS open-source de alta compatibilidade embutida)*
7. **Nintendo 64 (N64)** — *Gopher64 / ParaLLEl N64 / Mupen64Plus-Next*
8. **Atari 2600** — *Stella*
9. **Atari Jaguar** — *Virtual Jaguar*
10. **Neo Geo Pocket / Pocket Color** — *Mednafen Neopop*
11. **WonderSwan / WonderSwan Color** — *Mednafen Cygne*
12. **PC-DOS** — *DOSBox Pure (Kernel DOS e BIOS de PC x86 integrados com montagem ZIP)*
13. **Sinclair ZX Spectrum** — *Fuse (ROMs 48K/128K embutidas)*
14. **Arcade (Capcom CPS1, CPS2, CPS3, etc.)** — *FinalBurn Neo (cada jogo traz suas ROMs no próprio .zip)*

---

## 📂 Mapa de Pastas da Instalação do Karamelo

```text
Karamelo/
├── Karamelo.exe          <-- Executável principal
├── MSX.ROM, MSX2.ROM...      <-- Arquivos de BIOS do MSX (também ficam na raiz)
├── cores/                    <-- DLLs dos 35 emuladores nativos
├── saves/
│   └── 3DS/Citra/sysdata/
│       └── aes_keys.txt      <-- Chaves AES do Nintendo 3DS
└── bios/                     <-- Pasta principal de BIOS
    ├── scph5501.bin, scph5502.bin, scph5500.bin  (PS1)
    ├── bios_CD_U.bin, bios_CD_E.bin, bios_CD_J.bin (Sega CD)
    ├── sega_101.bin, saturn_bios.bin             (Saturn)
    ├── syscard3.pce                              (PC Engine CD)
    ├── coleco.rom, 5200.rom, lynxboot.img        (Atari / Coleco)
    ├── panafz1.bin                               (3DO)
    ├── bios7.bin, bios9.bin, firmware.bin        (Nintendo DS)
    ├── dc/
    │   ├── dc_boot.bin                           (Dreamcast)
    │   ├── naomi.zip                             (Naomi Arcade)
    │   └── awbios.zip                            (Atomiswave)
    ├── neocd/
    │   ├── neocd.bin, neocd_f.rom, uni-bioscd.rom (NeoGeo CD - SUBPASTA OBRIGATÓRIA)
    ├── pcsx2/
    │   ├── bios/
    │   │   ├── SCPH-70012_BIOS_V12_USA_200.BIN   (PlayStation 2)
    │   │   ├── SCPH-70012_BIOS_V12_USA_200.mec
    │   │   └── SCPH-70012_BIOS_V12_USA_200.nvm
    │   └── resources/
    │       └── GameIndex.yaml                    (Base de dados do PCSX2)
    ├── PPSSPP/                                   (Assets e fontes do PSP)
    │   ├── flash0/font/
    │   ├── lang/
    │   └── compat.ini
    └── dolphin-emu/
        └── Sys/                                  (Assets e certificados do GameCube)
```

---

## 🔍 Como Conferir os Hashes MD5 no Windows (PowerShell)

Abra o PowerShell na pasta do Karamelo e execute:

```powershell
Get-FileHash "bios\scph5501.bin" -Algorithm MD5
Get-FileHash "bios\pcsx2\bios\SCPH-70012_BIOS_V12_USA_200.BIN" -Algorithm MD5
```
