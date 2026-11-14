# Auditoria e Integração de PC Ports & Recompilados — Pixel Cherry Ninja #391

> **Data**: 14 de Novembro de 2026  
> **Fonte**: Pixel Cherry Ninja #391 (*CPS3 SF3 Recomps, Castlevania 64 with Ray Tracing, MiSTer, Analogue Pocket News & More*)  
> **Status**: **6 novos títulos integrados** no catálogo oficial (`data/ports.json` v11)  
> **Novas Contagens**: **103 Ports Totais** (103 Windows 🪟, 65 Linux 🐧, 45 macOS 🍎)

---

## 🎯 1. Títulos Integrados Diretamente ao Karamelo

| Jogo | Plataformas | Repositório | Release Tag | Requisitos de ROM / Imagem |
| :--- | :--- | :--- | :--- | :--- |
| **Castlevania 64** | 🪟 Windows | [DankZoneStudios/CV64Recomp](https://github.com/DankZoneStudios/CV64Recomp) | `0.8.1` | ROM de N64 em `roms/N64/` |
| **Castlevania: Rondo of Blood** | 🪟 Windows, 🐧 Linux | [gminustwo/RondoRecomp](https://github.com/gminustwo/RondoRecomp) | `v1.2` | Imagem de disco PC Engine CD (`.chd`, `.iso`, `.cue`, `.zip`) em `roms/PCEngineCD/` |
| **Street Fighter III (CPS-3 Trilogy)** | 🪟 Windows, 🐧 Linux, 🍎 macOS | [tmcleroy/sf3comp](https://github.com/tmcleroy/sf3comp) | `v0.1.0-beta.3` | ROMset CPS-3 (`sfiii`, `sfiii2`, `sfiii3`) em `roms/Arcade/` ou `roms/CPS3/` |
| **Animal Crossing: City Folk** | 🪟 Windows, 🐧 Linux, 🍎 macOS | [rob646464/city-folk-recompiled](https://github.com/rob646464/city-folk-recompiled) | `v1.0.2` | Disco de Wii (`.iso`, `.rvz`, `.wbfs`) em `roms/Wii/` |
| **Road Rash Trilogy** | 🪟 Windows | [BTr-RJ/RoadRashTrilogy_Recomp](https://github.com/BTr-RJ/RoadRashTrilogy_Recomp) | `v0.60` | ROM do Mega Drive em `roms/MegaDrive/` |
| **Syndicate Plus** | 🪟 Windows, 🍎 macOS | [Buyukcaglar/synd](https://github.com/Buyukcaglar/synd) | `v1.0` | Arquivos originais do jogo DOS |

---

## 🔬 2. Detalhes Técnicos dos Novos Ports

### 1. Castlevania 64 (N64 Recomp + Ray Tracing)
- **Engine**: N64 Recomp SDK com renderizador Direct3D 12.
- **Novidades na v0.8.1**: Suporte opcional a Ray Tracing / Path Tracing em tempo real, 60 FPS cravados sem descompassar os ciclos de jogo, câmera livre com analógico direito moderno.
- **Instalação**: O Karamelo extrai o pacote Windows x64 e valida a presença da ROM original do jogador em `roms/N64`.

### 2. Castlevania: Rondo of Blood (PC Engine CD)
- **Engine**: Recompilação C nativa direta do código m68000/HuC6280 e bios do Super CD-ROM².
- **Destaques**: Suporte a imagens compactadas `.chd` diretamente na memória (sem descompactar), suporte a modo portátil (`user/` local), física modernizada opcional de chicote em 8 direções (*Super Castlevania IV*).
- **Compatibilidade**: Disponível para Windows (zip x64) e Linux (AppImage nativo).

### 3. Street Fighter III (Capcom CPS-3)
- **Engine**: Recompilação estática das três iterações de CPS-3 (*3rd Strike*, *2nd Impact*, *New Generation*).
- **Vantagens**: A placa CPS-3 emulada sofre com tempos de carregamento para memória Flash e exigência pesada de CPU para sincronia do SH-2. Na recompilação nativa, o boot é instantâneo (sub-100ms), 60 FPS perfeitos, widescreen opcional e netcode rollback P2P moderno.
- **Compatibilidade**: Totalmente multiplataforma nativo: Windows x64, Linux x86_64 e macOS ARM64.

### 4. Animal Crossing: City Folk (Wii)
- **Engine**: Recompilação estática C++ de Wii com backend Aurora (Direct3D 12, Vulkan, Metal).
- **Destaques**: Resoluções 4K nítidas, correções visuais nos reflexos d'água na praia e rios, suporte a gamepad moderno.

### 5. Road Rash Trilogy (Mega Drive)
- **Engine**: Tradução estática m68k para C nativo da clássica trilogia de corridas violentas da EA.
- **Destaques**: Widescreen sem distorção, alta taxa de quadros e suporte a jogatina em rede local/online.

### 6. Syndicate Plus (DOS x64 Native)
- **Engine**: Recompilação estática x86 de 16/32-bit para binário nativo de 64-bit moderno (Windows e macOS ARM64).
- **Destaques**: Zero necessidade de emuladores pesados (como DOSBox-Staging); roda direto na CPU host com consumo ínfimo de memória e áudio sintetizado original.

---

## ⏳ 3. Projetos para Futuras Ondas (Ainda sem release binária aberta)
- **Ridge Racer PSP**: Código em beta com instalador proprietário (`Setup.exe`), acompanhando para empacotamento portátil limpo.
- **Alien vs Predator HD**: Projeto atualmente restrito ao Patreon do autor, aguardando liberação pública do repositório.
- **God of War: Betrayal Remaster**: Projeto demonstrado em vídeo, acompanhando publicação do código-fonte.
