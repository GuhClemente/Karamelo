#!/usr/bin/env python3
"""
Master Automated Media Recording & Validation Suite for Karamelo Emulador & Site
Generates unified, high-quality 720p 30fps MP4/WebM videos and WebP screenshots
for ALL Karamelo cores and systems.
"""

import os
import sys
import time
import subprocess
import shutil
from concurrent.futures import ThreadPoolExecutor, as_completed
from PIL import Image

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SITE_DIR = os.path.abspath("c:/github/Karamelo Site")
RECORDER = os.path.join(ROOT_DIR, "tools", "record_gameplay", "out", "record_gameplay.exe")
STAGING_DIR = os.path.join(ROOT_DIR, "record_staging")

SITE_VIDEOS_DIR = os.path.join(SITE_DIR, "public", "gallery", "videos")
SITE_SHOTS_DIR = os.path.join(SITE_DIR, "public", "gallery", "shots")
GALLERY_DATA_JS = os.path.join(SITE_DIR, "app", "gallery-data.js")

# Full catalog of planned recordings for all systems
# Format: (id, name, core_dll, core_display, category, game_name, rom_relpath, duration, macro, res, cpu, desc_pt, desc_en, captions)
RECORDINGS = [
    # 1. Super Nintendo (SNES) - 3 games
    (
        "snes_mmx",
        "Super Nintendo (SNES)",
        "cores/snes.dll",
        "bsnes",
        "16-Bit",
        "Mega Man X",
        "roms/SNES/Mega Man X (USA) (PT BR).smc",
        35,
        "none",
        "256x224 (60.00 Hz)",
        "Ricoh 5A22 @ 3.58 MHz + SPC700 Audio",
        "O clássico de ação da Capcom em português com trilha sonora sintetizada no DSP Sony SPC700 e gráficos 240p cravados a 60 FPS.",
        "Capcom's classic action platformer in Portuguese with Sony SPC700 DSP stereo audio and locked 240p 60 FPS visuals.",
        [
            "Tela de Abertura Capcom 1993",
            "Prólogo Narrativo em Português",
            "Highway Stage: X em Ação Pixel-Perfect",
            "Filtro CRT Trinitron Scanlines 15kHz"
        ]
    ),
    (
        "snes_dkc",
        "Super Nintendo (SNES)",
        "cores/snes.dll",
        "bsnes",
        "16-Bit",
        "Donkey Kong Country",
        "roms/SNES/Donkey Kong Country (USA) (PT BR).smc",
        35,
        "none",
        "256x224 (60.00 Hz)",
        "Ricoh 5A22 @ 3.58 MHz + SPC700 Audio",
        "Gráficos pré-renderizados em Silicon Graphics pela Rareware com a célebre trilha atmosférica de David Wise em 60 FPS.",
        "Rareware's ACM pre-rendered visuals with David Wise's iconic atmospheric soundtrack running at locked 60 FPS.",
        [
            "Abertura Rareware / Nintendo",
            "Cranky Kong no Gramofone",
            "Donkey Kong & Diddy em Jungle Hijinx",
            "Shader CRT Trinitron Retrô"
        ]
    ),
    (
        "snes_actraiser",
        "Super Nintendo (SNES)",
        "cores/snes.dll",
        "bsnes",
        "16-Bit",
        "ActRaiser",
        "roms/SNES/ActRaiser (USA) (PT BR).sfc",
        35,
        "none",
        "256x224 (60.00 Hz)",
        "Ricoh 5A22 @ 3.58 MHz + SPC700 Audio",
        "Obra-prima da Quintet e Enix com a histórica trilha sinfônica de Yuzo Koshiro no coprocessador de áudio Sony SPC700.",
        "Quintet and Enix masterpiece featuring Yuzo Koshiro's legendary orchestral score on the Sony SPC700 sound DSP.",
        [
            "Abertura Oficial Quintet / Enix",
            "O Palácio nos Céus e Estátua Divina",
            "Descida para Filmore em 240p 60 FPS",
            "Filtro CRT Trinitron 15kHz NTSC"
        ]
    ),

    # 2. Sega Mega Drive / Genesis - 3 games
    (
        "genesis_sor2",
        "Sega Mega Drive / Genesis",
        "cores/genesis.dll",
        "Genesis Plus GX",
        "16-Bit",
        "Streets of Rage 2",
        "roms/Genesis/Streets of Rage 2 (USA) (PT BR).md",
        35,
        "none",
        "320x224 (59.92 Hz)",
        "Motorola 68000 @ 7.67 MHz + Z80 + YM2612",
        "O ápice do beat 'em up nos 16 bits com a lendária trilha dance de Yuzo Koshiro e Motohiro Kawashima no Yamaha YM2612.",
        "The pinnacle of 16-bit beat 'em ups featuring Yuzo Koshiro and Motohiro Kawashima's iconic Yamaha YM2612 FM soundtrack.",
        [
            "Abertura Oficial Sega 1992",
            "A Cidade Dominada pelo Crime",
            "Yuzo Koshiro YM2612 FM Audio",
            "Filtro CRT Trinitron com Phosphor Glow"
        ]
    ),
    (
        "genesis_sonic",
        "Sega Mega Drive / Genesis",
        "cores/genesis.dll",
        "Genesis Plus GX",
        "16-Bit",
        "Sonic The Hedgehog",
        "roms/Genesis/Sonic The Hedgehog (USA, Europe) (PT BR).md",
        35,
        "none",
        "320x224 (59.92 Hz)",
        "Motorola 68000 @ 7.67 MHz + Z80 + YM2612",
        "Velocidade supersônica em Green Hill Zone com física de rolamento precisa e áudio FM cristalino.",
        "Supersonic speed in Green Hill Zone with pixel-accurate physics and crisp Yamaha YM2612 FM audio.",
        [
            "Logo Clássico SEGA e Sonic piscando",
            "Green Hill Zone - Abertura",
            "Loopings e Anéis em Alta Velocidade",
            "Shader CRT Trinitron Phosphor"
        ]
    ),
    (
        "genesis_goldenaxe",
        "Sega Mega Drive / Genesis",
        "cores/genesis.dll",
        "Genesis Plus GX",
        "16-Bit",
        "Golden Axe",
        "roms/Genesis/Golden Axe (World) (Rev A) (PT BR).md",
        35,
        "none",
        "320x224 (59.92 Hz)",
        "Motorola 68000 @ 7.67 MHz + Z80 + YM2612",
        "A épica fantasia medieval da Sega com Ax Battler, Tyris Flare e Gilius Thunderhead em 60 FPS cravados.",
        "Sega's iconic medieval fantasy brawler with Ax Battler, Tyris Flare and Gilius Thunderhead running at locked 60 FPS.",
        [
            "Abertura Sega 1989",
            "Mapa de Yuria e Prólogo",
            "Batalha Medieval em 320x224",
            "Shader CRT Trinitron Retrô"
        ]
    ),

    # 3. NES / Famicom - 3 games
    (
        "nes_contra",
        "NES / Famicom",
        "cores/nes.dll",
        "Mesen",
        "8-Bit",
        "Contra",
        "roms/NES/Contra (U).nes",
        35,
        "none",
        "256x240 (60.09 Hz)",
        "Ricoh 2A03 (6502) @ 1.79 MHz + APU",
        "O lendário run 'n gun da Konami com precisão de ciclo de clock, áudio estéreo APU e ação ininterrupta a 60 FPS.",
        "Konami's legendary run 'n gun shooter with cycle-accurate timings, stereo APU audio and blazing 60 FPS action.",
        [
            "Tela de Título Konami Contra",
            "Bill Rizer & Lance Bean na Selva",
            "Tiroteio Intenso em 256x240",
            "Shader CRT Trinitron 8-Bit NTSC"
        ]
    ),
    (
        "nes_ducktales",
        "NES / Famicom",
        "cores/nes.dll",
        "Mesen",
        "8-Bit",
        "DuckTales",
        "roms/NES/Duck Tales (BR-USA) (T1.0).nes",
        35,
        "none",
        "256x240 (60.09 Hz)",
        "Ricoh 2A03 (6502) @ 1.79 MHz + APU",
        "Scrooge McDuck em busca dos tesouros mundiais com a lendária trilha sonora da Lua composta por Yoshihiro Sakaguchi.",
        "Scrooge McDuck's quest with the legendary Moon Theme by Yoshihiro Sakaguchi and cycle-accurate 2A03 audio.",
        [
            "Capcom / Disney 8-Bit Title",
            "Seleção de Fases: Amazônia, Transilvânia, Lua",
            "Tio Patinhas em 256x240 Pixel Perfect",
            "Shader CRT Trinitron 8-Bit NTSC"
        ]
    ),
    (
        "nes_megaman2",
        "NES / Famicom",
        "cores/nes.dll",
        "Mesen",
        "8-Bit",
        "Mega Man 2",
        "roms/NES/Mega Man 2 (U).nes",
        35,
        "none",
        "256x240 (60.09 Hz)",
        "Ricoh 2A03 (6502) @ 1.79 MHz + APU",
        "A obra seminal da Capcom com introdução no topo do edifício e seleção dos 8 Robot Masters.",
        "Capcom's masterwork featuring the rooftop building intro and the iconic 8 Robot Masters stage selection.",
        [
            "Abertura no Topo do Prédio: Mega Man",
            "Seleção dos 8 Robot Masters",
            "Metal Man Stage em Pixel Perfect",
            "Filtro CRT Trinitron Phosphor"
        ]
    ),

    # 4. Sega Master System - 3 games
    (
        "sms_alexkidd",
        "Sega Master System",
        "cores/sms.dll",
        "Gearsystem",
        "8-Bit",
        "Alex Kidd in Miracle World",
        "roms/MasterSystem/Alex Kidd 3 2021 _Final_60hz_PT BR_512K_v1.1.sms",
        35,
        "none",
        "256x192 (59.92 Hz)",
        "Zilog Z80A @ 3.58 MHz + SN76489 PSG",
        "O mascote clássico da Sega em versão 60Hz aprimorada em português, scroll horizontal suave e áudio PSG cristalino.",
        "Sega's original mascot in enhanced 60Hz Portuguese translation with butter-smooth scrolling and crisp PSG audio.",
        [
            "Sega Master System Logo",
            "Alex Kidd in Miracle World Title",
            "Scroll Horizontal 60Hz Fluido",
            "Shader CRT Trinitron Retrô"
        ]
    ),
    (
        "sms_castle",
        "Sega Master System",
        "cores/sms.dll",
        "Gearsystem",
        "8-Bit",
        "Castle of Illusion",
        "roms/MasterSystem/Castle of Illusion Starring Mickey Mouse (USA).sms",
        35,
        "none",
        "256x192 (59.92 Hz)",
        "Zilog Z80A @ 3.58 MHz + SN76489 PSG",
        "Mickey Mouse em uma das mais impressionantes produções visuais de 8 bits da Sega com animações primorosas.",
        "Mickey Mouse in one of Sega's most visually stunning 8-bit masterworks with fluid sprite animations.",
        [
            "Abertura Disney / Sega 1990",
            "O Castelo da Bruxa Mizrabel",
            "Mickey Mouse em Floresta Encantada",
            "Shader CRT Trinitron Scanlines"
        ]
    ),
    (
        "sms_shinobi",
        "Sega Master System",
        "cores/sms.dll",
        "Gearsystem",
        "8-Bit",
        "The Cyber Shinobi",
        "roms/MasterSystem/Cyber Shinobi, The (Europe, Brazil) (En).sms",
        35,
        "none",
        "256x192 (59.92 Hz)",
        "Zilog Z80A @ 3.58 MHz + SN76489 PSG",
        "Aventura cyberpunk com Joe Musashi enfrentando a organização Zeed no século XXI com espada cibernética e ninjutsus.",
        "Cyberpunk ninja action with Joe Musashi fighting the Zeed syndicate with cybernetic katana and ninjutsu powers.",
        [
            "Abertura Cyber Shinobi Sega",
            "Joe Musashi e Trilha Sintetizada",
            "Combate Urbano com Katana",
            "Filtro CRT Trinitron Phosphor"
        ]
    ),

    # 5. Game Boy Advance (GBA) - 3 games
    (
        "gba_castlevania",
        "Game Boy Advance (GBA)",
        "cores/gba.dll",
        "mGBA",
        "Portáteis",
        "Castlevania: Aria of Sorrow",
        "roms/GBA/Castlevania - Aria of Sorrow (BR-USA) (DUB) (T2.4).gba",
        35,
        "none",
        "240x160 (59.73 Hz)",
        "ARM7TDMI @ 16.78 MHz + DirectSound",
        "Soma Cruz no Castelo de Drácula em português com o sistema Tactical Soul e gráficos magníficos em 240x160.",
        "Soma Cruz in Dracula's castle featuring the Tactical Soul system and gorgeous 240x160 portable graphics.",
        [
            "Konami Aria of Sorrow Title",
            "Soma Cruz e Mina Hakuba no Santuário",
            "Castelo de Drácula em 60 FPS",
            "Shader GBA LCD Grid com Color Correction"
        ]
    ),
    (
        "gba_zelda",
        "Game Boy Advance (GBA)",
        "cores/gba.dll",
        "mGBA",
        "Portáteis",
        "The Legend of Zelda: A Link to the Past",
        "roms/GBA/A Link to the Past & Four Swords (BR) (T3.1).gba",
        35,
        "none",
        "240x160 (59.73 Hz)",
        "ARM7TDMI @ 16.78 MHz + DirectSound",
        "Zelda A Link to the Past & Four Swords em português com áudio PCM estéreo DirectSound e fidelidade absoluta mGBA.",
        "Zelda A Link to the Past & Four Swords with DirectSound stereo PCM and mGBA cycle-accurate emulation.",
        [
            "Nintendo / Capcom Triforce Intro",
            "Seleção de Jogo: Escolha um Arquivo",
            "Trilha Sonora Orquestrada DirectSound",
            "Shader GBA LCD Grid com Correção de Cor"
        ]
    ),
    (
        "gba_alteredbeast",
        "Game Boy Advance (GBA)",
        "cores/gba.dll",
        "mGBA",
        "Portáteis",
        "Altered Beast: Guardian of the Realms",
        "roms/GBA/Altered Beast - Guardian of the Realms (BR) -.gba",
        35,
        "none",
        "240x160 (59.73 Hz)",
        "ARM7TDMI @ 16.78 MHz + DirectSound",
        "Sequência exclusiva da THQ e 3d6 Games para GBA com transformações míticas e trilha de ação portátil.",
        "GBA exclusive sequel by THQ and 3d6 Games with mythical beast transformations and portable action.",
        [
            "Abertura Sega / THQ",
            "Seleção de Guerreiro Espartano",
            "Transformações Bestiais em 240x160",
            "Shader LCD GBA Original"
        ]
    ),

    # 6. Game Boy & Color - 3 games
    (
        "gb_ninjagaiden",
        "Game Boy & Color",
        "cores/gb.dll",
        "Gambatte",
        "Portáteis",
        "Ninja Gaiden Shadow DX",
        "roms/GameBoy/Ninja Gaiden Shadow DX (BR-USA) (T1.0).gb",
        35,
        "none",
        "160x144 (59.73 Hz)",
        "Sharp LR35902 (Z80-hybrid) @ 4.19 / 8.38 MHz",
        "Cutscene cinemática sobre o skyline de Nova York em português com paleta GBC e emulação precisa Gambatte.",
        "Epic cinematic cutscene over New York's skyline with enhanced GBC color palette and Gambatte accuracy.",
        [
            "Tecmo Ninja Gaiden Shadow DX",
            "Cutscene Cinemática sobre Nova York",
            "Ryu Hayabusa em Ação a 60 FPS",
            "Shader Dot-Matrix LCD Original"
        ]
    ),
    (
        "gb_re",
        "Game Boy & Color",
        "cores/gb.dll",
        "Gambatte",
        "Portáteis",
        "Resident Evil GBC",
        "roms/GameBoy/Resident Evil (BR) (Proto).gbc",
        35,
        "none",
        "160x144 (59.73 Hz)",
        "Sharp LR35902 (Z80-hybrid) @ 4.19 / 8.38 MHz",
        "O lendário protótipo de Resident Evil para Game Boy Color totalmente em português com Chris Redfield e Jill Valentine.",
        "The legendary unreleased Resident Evil GBC prototype in Portuguese featuring Chris Redfield and Jill Valentine.",
        [
            "Tela de Título Resident Evil GBC",
            "Seleção de Personagem: Chris / Jill",
            "A Mansão Spencer em 160x144",
            "Shader Dot-Matrix LCD Color"
        ]
    ),
    (
        "gb_turok",
        "Game Boy & Color",
        "cores/gb.dll",
        "Gambatte",
        "Portáteis",
        "Turok: Battle of the Bionosaurs",
        "roms/GameBoy/Turok - Battle of the Bionosaurs (BR) (T1.0).gb",
        35,
        "none",
        "160x144 (59.73 Hz)",
        "Sharp LR35902 (Z80-hybrid) @ 4.19 / 8.38 MHz",
        "O guerreiro nativo-americano enfrentando dinossauros cibernéticos com arco, dinamite e gráficos monocromáticos nítidos.",
        "Turok's prehistoric battle against cybernetic dinosaurs with bow, arrows and sharp monochrome action.",
        [
            "Acclaim / Iguana Entertainment Intro",
            "Turok Battle of the Bionosaurs Title",
            "Ação Pré-histórica em 160x144",
            "Shader Game Boy DMG Pocket LCD"
        ]
    ),

    # 7. Nintendo 64 - 3 games
    (
        "n64_1080",
        "Nintendo 64",
        "cores/n64_gopher.dll",
        "Gopher64 / ParaLLEl N64",
        "3D & 64-Bit",
        "1080° Snowboarding",
        "roms/Nintendo64/1080 TenEighty Snowboarding (Europe) (En,Ja,Fr,De).z64",
        35,
        "none",
        "320x240 / 640x480 (60.00 Hz)",
        "NEC VR4300 64-bit @ 93.75 MHz + RCP (Reality Engine)",
        "Renderização precisa do Reality Coprocessor da Silicon Graphics com física de partículas e áudio RSP em 60 FPS.",
        "Silicon Graphics Reality Coprocessor rendering with realistic snow physics and RSP audio at 60 FPS.",
        [
            "Nintendo 64 3D Spinning Logo",
            "1080 TenEighty Snowboarding Title",
            "Física de Neve e RCP Reality Coprocessor",
            "Shader CRT Trinitron 480i / 240p VI Filter"
        ]
    ),
    (
        "n64_waverace",
        "Nintendo 64",
        "cores/n64_gopher.dll",
        "Gopher64 / ParaLLEl N64",
        "3D & 64-Bit",
        "Wave Race 64",
        "roms/Nintendo64/Wave Race 64 Kawasaki Jet Ski (USA) (Rev 1).z64",
        35,
        "none",
        "320x240 / 640x480 (60.00 Hz)",
        "NEC VR4300 64-bit @ 93.75 MHz + RCP (Reality Engine)",
        "Física revolucionária de ondas poligonais 3D com Kawasaki Jet Skis e simulação hidrodinâmica em tempo real.",
        "Revolutionary 3D polygonal wave physics with licensed Kawasaki Jet Skis and real-time hydrodynamic buoyancy.",
        [
            "Kawasaki Heavy Industries & Nintendo Title",
            "Seleção de Jet Ski e Piloto",
            "Ondas 3D Poligonais em Dolphin Park",
            "Shader CRT Trinitron N64 Filter"
        ]
    ),
    (
        "n64_castlevania",
        "Nintendo 64",
        "cores/n64_gopher.dll",
        "Gopher64 / ParaLLEl N64",
        "3D & 64-Bit",
        "Castlevania 64",
        "roms/Nintendo64/Castlevania (USA) (Rev 2).z64",
        35,
        "none",
        "320x240 / 640x480 (60.00 Hz)",
        "NEC VR4300 64-bit @ 93.75 MHz + RCP (Reality Engine)",
        "A transição de Castlevania para o 3D gótico com Reinhardt Schneider e Carrie Fernandez e iluminação volumétrica.",
        "Castlevania's transition into full gothic 3D with Reinhardt Schneider, Carrie Fernandez and dynamic lighting.",
        [
            "Konami KCEK 3D Intro",
            "Abertura Gótica com Violino e Lua Cheia",
            "Os Portões da Mansão da Transilvânia",
            "Shader CRT Trinitron 15kHz"
        ]
    ),

    # 8. Arcade Classics - 3 games
    (
        "arcade_captcomm",
        "Arcade Classics (MAME)",
        "cores/arcade_fbneo.dll",
        "MAME / FinalBurn Neo",
        "Arcade",
        "Captain Commando",
        "roms/Arcade/captcomm.zip",
        35,
        "none",
        "384x224 (59.64 Hz)",
        "Motorola 68000 @ 10 MHz + Z80 + YM2151",
        "O lendário beat 'em up CPS-1 da Capcom ambientado em Metro City 2026 com o Capitão, Ginzu, Mack e Baby Head.",
        "Capcom's classic CPS-1 arcade beat 'em up set in Metro City 2026 with Captain, Ginzu, Mack and Baby Head.",
        [
            "Capcom CPS-1 Arcade Logo",
            "Captain Commando Title Screen",
            "Metro City 2026: Ação em 384x224",
            "Shader CRT Trinitron Arcade Cabinet"
        ]
    ),
    (
        "arcade_knights",
        "Arcade Classics (MAME)",
        "cores/arcade_fbneo.dll",
        "MAME / FinalBurn Neo",
        "Arcade",
        "Knights of the Round",
        "roms/Arcade/knights.zip",
        35,
        "none",
        "384x224 (59.64 Hz)",
        "Motorola 68000 @ 10 MHz + Z80 + YM2151",
        "A lenda arturiana nos arcades da Capcom com Arthur, Lancelot e Perceval, evolução de armaduras e áudio YM2151.",
        "Arthurian legend in Capcom arcades with Arthur, Lancelot and Perceval, RPG armor leveling and YM2151 FM audio.",
        [
            "Capcom Knights of the Round Title",
            "Prólogo do Santo Graal e Rei Arthur",
            "Combate de Espadas em 384x224",
            "Filtro CRT Arcade Cabinet 15kHz"
        ]
    ),
    (
        "arcade_dino",
        "Arcade Classics (MAME)",
        "cores/arcade_fbneo.dll",
        "MAME / FinalBurn Neo",
        "Arcade",
        "Cadillacs and Dinosaurs",
        "roms/Arcade/dino.zip",
        35,
        "none",
        "384x224 (59.64 Hz)",
        "Motorola 68000 @ 10 MHz + Z80 + QSound",
        "O fenômeno dos fliperamas dos anos 90 com Jack, Hannah, Mustapha e Mess, áudio estéreo QSound e ação frenética.",
        "The 90s arcade arcade phenomenon with Jack, Hannah, Mustapha and Mess, QSound 3D audio and dinosaur action.",
        [
            "Abertura Capcom / Mark Schultz",
            "Cadillacs and Dinosaurs Title",
            "Cidade dos Dinossauros em 60 FPS",
            "Shader CRT Trinitron Arcade"
        ]
    ),

    # 9. Atari Jaguar - 3 games
    (
        "jaguar_avp",
        "Atari Jaguar (64-Bit)",
        "cores/jaguar.dll",
        "Virtual Jaguar",
        "3D & 64-Bit",
        "Alien vs Predator",
        "roms/Jaguar/Alien vs Predator (1994).jag",
        35,
        "none",
        "320x240 (60.00 Hz)",
        "Tom & Jerry 64-bit @ 26.6 MHz + Motorola 68000",
        "O aclamado FPS atmosférico da Rebellion para o console 64-bit da Atari com texturas mapeadas e 3 campanhas distintas.",
        "Rebellion's acclaimed atmospheric FPS for Atari's 64-bit console with texture mapping and three campaigns.",
        [
            "Atari Jaguar 64-Bit Boot Screen",
            "Alien vs Predator Title Screen",
            "Base Espacial Militar em 3D",
            "Shader CRT Trinitron 15kHz"
        ]
    ),
    (
        "jaguar_karts",
        "Atari Jaguar (64-Bit)",
        "cores/jaguar.dll",
        "Virtual Jaguar",
        "3D & 64-Bit",
        "Atari Karts",
        "roms/Jaguar/Atari Karts (1995).jag",
        35,
        "none",
        "320x240 (60.00 Hz)",
        "Tom & Jerry 64-bit @ 26.6 MHz + Motorola 68000",
        "Corrida arcade com o mascote Bentley Bear em pistas coloridas e efeitos de escalonamento acelerados pelo chip Tom.",
        "Arcade kart racing starring Bentley Bear with colorful tracks and sprite scaling accelerated by the Tom chip.",
        [
            "Atari Jaguar Logo",
            "Atari Karts Title Screen",
            "Pistas Coloridas em Alta Velocidade",
            "Shader CRT Trinitron Scanlines"
        ]
    ),
    (
        "jaguar_brutal",
        "Atari Jaguar (64-Bit)",
        "cores/jaguar.dll",
        "Virtual Jaguar",
        "3D & 64-Bit",
        "Brutal Sports Football",
        "roms/Jaguar/Brutal Sports Football (1994) (Telegames).jag",
        35,
        "none",
        "320x240 (60.00 Hz)",
        "Tom & Jerry 64-bit @ 26.6 MHz + Motorola 68000",
        "Futebol violento intergaláctico com vikings, lagartos e robôs em campo aberto a 60 FPS.",
        "Violent futuristic football with vikings, lizards and robots battling on open fields at locked 60 FPS.",
        [
            "Telegames / Millennium Intro",
            "Brutal Sports Football Title",
            "Combate e Esporte em 320x240",
            "Shader CRT Trinitron Retrô"
        ]
    ),

    # 10. Sega 32X - 3 games
    (
        "32x_afterburner",
        "Sega 32X",
        "cores/32x.dll",
        "PicoDrive",
        "32-Bit",
        "After Burner Complete",
        "roms/32X/After Burner Complete (Europe).32x",
        35,
        "none",
        "320x224 (59.92 Hz)",
        "Dual Hitachi SH-2 32-bit @ 23 MHz + VDP",
        "Port definitivo de arcade rodando com os dois processadores Hitachi SH-2 da Sega e áudio PCM estéreo.",
        "Definitive arcade port powered by dual Hitachi SH-2 32-bit processors and CD-quality stereo PCM audio.",
        [
            "Sega 32X Arcade Hardware Intro",
            "After Burner Complete Title Screen",
            "F-14 Tomcat em Voo Supersônico",
            "Shader CRT Trinitron Phosphor"
        ]
    ),
    (
        "32x_blackthorne",
        "Sega 32X",
        "cores/32x.dll",
        "PicoDrive",
        "32-Bit",
        "Blackthorne",
        "roms/32X/Blackthorne (USA).32x",
        35,
        "none",
        "320x224 (59.92 Hz)",
        "Dual Hitachi SH-2 32-bit @ 23 MHz + VDP",
        "A obra-prima cinematográfica da Blizzard Entertainment em versão 32-bit aprimorada com novas fases de neve.",
        "Blizzard Entertainment's cinematic platformer in enhanced 32-bit edition featuring exclusive snow levels.",
        [
            "Interplay / Blizzard Entertainment Intro",
            "Kyle Vlaros e a Espingarda em Tuul",
            "Plataforma Cinemática em 320x224",
            "Filtro CRT Trinitron 15kHz"
        ]
    ),
    (
        "32x_spiderman",
        "Sega 32X",
        "cores/32x.dll",
        "PicoDrive",
        "32-Bit",
        "The Amazing Spider-Man: Web of Fire",
        "roms/32X/Amazing Spider-Man, The - Web of Fire (USA).32x",
        35,
        "none",
        "320x224 (59.92 Hz)",
        "Dual Hitachi SH-2 32-bit @ 23 MHz + VDP",
        "Raridade exclusiva do 32X desenvolvida pela BlueSky Software com o Homem-Aranha enfrentando a H.Y.D.R.A.",
        "Ultra-rare 32X exclusive by BlueSky Software with Spider-Man battling the H.Y.D.R.A. syndicate.",
        [
            "Sega / Marvel Comics Intro",
            "Web of Fire Title Screen",
            "Homem-Aranha Escalando Prédios em NY",
            "Shader CRT Trinitron Retrô"
        ]
    ),

    # 11. Panasonic 3DO - 3 games
    (
        "3do_roadrash",
        "Panasonic 3DO Interactive",
        "cores/3do.dll",
        "Opera",
        "32-Bit",
        "Road Rash",
        "roms/3DO/Road Rash (USA).chd",
        35,
        "none",
        "320x240 / 640x480 (59.94 Hz)",
        "ARM640 32-bit @ 12.5 MHz + Cel Engine DSP",
        "O aclamado reboot em CD-ROM da Electronic Arts com vídeos FMV, trilha sonora grunge licenciada e corridas em 3D.",
        "Electronic Arts' acclaimed CD-ROM reboot with full-motion video, licensed grunge soundtrack and 3D racing.",
        [
            "The 3DO Company & EA Opening",
            "Road Rash Title Screen",
            "Corrida Noturna na Costa da Califórnia",
            "Shader CRT Trinitron 32-Bit NTSC"
        ]
    ),
    (
        "3do_ssf2t",
        "Panasonic 3DO Interactive",
        "cores/3do.dll",
        "Opera",
        "32-Bit",
        "Super Street Fighter II Turbo",
        "roms/3DO/Super Street Fighter II Turbo (USA) (RE2).chd",
        35,
        "none",
        "320x240 / 640x480 (59.94 Hz)",
        "ARM640 32-bit @ 12.5 MHz + Cel Engine DSP",
        "A lendária versão de 3DO com trilha orquestrada em CD-DA cristalino e a estreia de Akuma nos consoles de mesa.",
        "Legendary 3DO port featuring uncompressed CD-DA arranged music and the console debut of Akuma.",
        [
            "Capcom / 3DO Company Intro",
            "Super Street Fighter II Turbo Title",
            "Ryu vs Ken em Ação a 60 FPS",
            "Shader CRT Trinitron Phosphor"
        ]
    ),
    (
        "3do_gex",
        "Panasonic 3DO Interactive",
        "cores/3do.dll",
        "Opera",
        "32-Bit",
        "Gex",
        "roms/3DO/Gex (USA Europe).chd",
        35,
        "none",
        "320x240 / 640x480 (59.94 Hz)",
        "ARM640 32-bit @ 12.5 MHz + Cel Engine DSP",
        "A estreia da osga sarcástica da Crystal Dynamics com voz digitalizada de Dana Gould e mundos de televisão.",
        "Crystal Dynamics' wise-cracking gecko debut with digitized speech by Dana Gould and television worlds.",
        [
            "Crystal Dynamics 3DO Intro",
            "Gex Title Screen",
            "Cemitério dos Filmes de Terror",
            "Filtro CRT Trinitron Scanlines"
        ]
    ),

    # 12. Fujitsu FM Towns - 3 games
    (
        "fmtowns_truxton",
        "Fujitsu FM Towns / Marty",
        "cores/fmtowns.dll",
        "Geartowns",
        "Computadores",
        "Tatsujin Ou (Truxton II)",
        "roms/FMTowns/Tatsujin Ou (aka Truxton II) (1993)(Ving)(Jp-En).zip",
        35,
        "none",
        "320x240 (55.40 Hz)",
        "Intel 386DX @ 16 MHz + Ricoh RF5C68 PCM",
        "Port pixel-perfect do clássico da Toaplan com áudio CD-DA original e 256 cores simultâneas do hardware japonês.",
        "Pixel-perfect Toaplan arcade port featuring original CD-DA audio and 256 simultaneous hardware colors.",
        [
            "Ving / Toaplan Arcade Intro",
            "Tatsujin Ou Title Screen",
            "Ação Shoot 'em Up Espacial",
            "Shader CRT Trinitron Arcade"
        ]
    ),
    (
        "fmtowns_splatterhouse",
        "Fujitsu FM Towns / Marty",
        "cores/fmtowns.dll",
        "Geartowns",
        "Computadores",
        "Splatterhouse",
        "roms/FMTowns/Splatterhouse (1992)(Ving)(Jp-En).zip",
        35,
        "none",
        "320x240 (55.40 Hz)",
        "Intel 386DX @ 16 MHz + Ricoh RF5C68 PCM",
        "A versão definitiva do clássico de terror da Namco com fidelidade visual idêntica à placa arcade e sangue sem censura.",
        "The definitive home port of Namco's horror classic with uncut gore and arcade-accurate graphics.",
        [
            "Namco / Ving Title Screen",
            "A Máscara do Terror e Mansão West",
            "Rick com Cutelo nos Corredores",
            "Shader CRT Trinitron Retrô"
        ]
    ),
    (
        "fmtowns_afterburner2",
        "Fujitsu FM Towns / Marty",
        "cores/fmtowns.dll",
        "Geartowns",
        "Computadores",
        "After Burner II",
        "roms/FMTowns/Afterburner 2 (1989)(Sega)(Jp-En).zip",
        35,
        "none",
        "320x240 (55.40 Hz)",
        "Intel 386DX @ 16 MHz + Ricoh RF5C68 PCM",
        "Conversão arcade primorosa da Sega para computadores FM Towns com rolagem de cenário suave e trilha sonora FM.",
        "Outstanding Sega arcade conversion for FM Towns computers with smooth scaling and rich FM synthesizer music.",
        [
            "Sega / CSG Opening",
            "After Burner II Title",
            "F-14 Tomcat Voando sobre o Deserto",
            "Filtro CRT Trinitron Phosphor"
        ]
    ),

    # 13. Atari 5200 - 3 games
    (
        "atari5200_berzerk",
        "Atari 5200 SuperSystem",
        "cores/atari5200.dll",
        "Atari800",
        "Consoles",
        "Berzerk",
        "roms/Atari5200/BerZerk (USA).zip",
        35,
        "none",
        "320x240 (59.92 Hz)",
        "MOS 6502C @ 1.79 MHz + ANTIC + POKEY",
        "O clássico labirinto de robôs de Stern com fala sintetizada pioneira no chip de áudio quad-core Atari POKEY.",
        "Stern's classic robot maze featuring pioneer synthesized speech powered by the quad-core Atari POKEY sound chip.",
        [
            "Atari 5200 Rainbow Boot",
            "Berzerk Title Screen",
            "Labirinto Elétrico com Robôs",
            "Shader CRT Trinitron 15kHz"
        ]
    ),
    (
        "atari5200_moonpatrol",
        "Atari 5200 SuperSystem",
        "cores/atari5200.dll",
        "Atari800",
        "Consoles",
        "Moon Patrol",
        "roms/Atari5200/Moon Patrol (USA).zip",
        35,
        "none",
        "320x240 (59.92 Hz)",
        "MOS 6502C @ 1.79 MHz + ANTIC + POKEY",
        "O jipe lunar da Irem enfrentando crateras, minas e discos voadores com parallax scrolling de três planos.",
        "Irem's lunar buggy leaping over craters and shooting UFOs with early 3-plane parallax scrolling.",
        [
            "Atari 5200 Moon Patrol Title",
            "Jipe Lunar na Superfície da Lua",
            "Crateras e Discos Voadores",
            "Shader CRT Trinitron Retrô"
        ]
    ),
    (
        "atari5200_starwars",
        "Atari 5200 SuperSystem",
        "cores/atari5200.dll",
        "Atari800",
        "Consoles",
        "Star Wars: The Arcade Game",
        "roms/Atari5200/Star Wars - The Arcade Game (USA).zip",
        35,
        "none",
        "320x240 (59.92 Hz)",
        "MOS 6502C @ 1.79 MHz + ANTIC + POKEY",
        "A histórica batalha da Estrela da Morte adaptada com gráficos vetoriais em primeira pessoa no hardware do 5200.",
        "The historic Death Star trench battle adapted with first-person vector graphics on Atari 5200 hardware.",
        [
            "Lucasfilm / Atari Title Screen",
            "Ataque aos TIE Fighters no Espaço",
            "A Trincheira da Estrela da Morte",
            "Filtro CRT Trinitron Scanlines"
        ]
    ),

    # 14. Atari 7800 - 1 game
    (
        "atari7800_ace",
        "Atari 7800 ProSystem",
        "cores/atari7800.dll",
        "ProSystem",
        "Consoles",
        "Ace of Aces",
        "roms/Atari7800/Ace of Aces.zip",
        35,
        "none",
        "320x240 (59.92 Hz)",
        "Atari SALLY (6502) @ 1.79 MHz + MARIA GPU",
        "Simulador de combate aéreo na Segunda Guerra Mundial utilizando a GPU MARIA capaz de desenhar quase 100 sprites por linha.",
        "WWII flight combat simulator utilizing the MARIA GPU capable of rendering nearly 100 sprites per scanline.",
        [
            "Atari 7800 ProSystem Title",
            "Cockpit do Caça Mosquito da RAF",
            "Combate Aéreo sobre a Europa",
            "Shader CRT Trinitron 15kHz"
        ]
    ),

    # 15. Atari Lynx - 3 games
    (
        "lynx_batman",
        "Atari Lynx",
        "cores/lynx.dll",
        "Handy",
        "Portáteis",
        "Batman Returns",
        "roms/Lynx/Batman Returns (USA, Europe).zip",
        35,
        "none",
        "160x102 (75.00 Hz)",
        "MOS 65SC02 @ 4 MHz + 'Suzy' 16-bit Math GPU",
        "O Cavaleiro das Trevas em Gotham City com escalonamento de sprites em tempo real acelerado pelo coprocessador Suzy.",
        "The Dark Knight in Gotham City featuring real-time hardware sprite scaling powered by the Suzy math coprocessor.",
        [
            "Atari Lynx Boot Screen",
            "Batman Returns Title Screen",
            "Batman nas Ruas de Gotham City",
            "Shader LCD Colorido Original Lynx"
        ]
    ),
    (
        "lynx_doubledragon",
        "Atari Lynx",
        "cores/lynx.dll",
        "Handy",
        "Portáteis",
        "Double Dragon",
        "roms/Lynx/Double Dragon (USA, Europe).zip",
        35,
        "none",
        "160x102 (75.00 Hz)",
        "MOS 65SC02 @ 4 MHz + 'Suzy' 16-bit Math GPU",
        "Billy e Jimmy Lee no port portátil mais dinâmico do clássico com zoom contínuo de sprites e áudio estéreo.",
        "Billy and Jimmy Lee in the most dynamic portable port featuring smooth sprite scaling and stereo audio.",
        [
            "Telegames Double Dragon Title",
            "Billy Lee nas Ruas da Cidade",
            "Combate Marcial em 160x102",
            "Shader Lynx LCD Grid"
        ]
    ),
    (
        "lynx_ninjagaiden",
        "Atari Lynx",
        "cores/lynx.dll",
        "Handy",
        "Portáteis",
        "Ninja Gaiden",
        "roms/Lynx/Ninja Gaiden (USA, Europe).zip",
        35,
        "none",
        "160x102 (75.00 Hz)",
        "MOS 65SC02 @ 4 MHz + 'Suzy' 16-bit Math GPU",
        "Adaptação fiel do arcade com Ryu Hayabusa executando saltos em postes, voadoras e cortes de espada.",
        "Faithful arcade conversion with Ryu Hayabusa performing wall jumps, dive kicks and katana slashes.",
        [
            "Tecmo / Atari Lynx Intro",
            "Ninja Gaiden Title Screen",
            "Combate Ninja em Alta Taxa de Quadros",
            "Shader LCD Portátil Retrô"
        ]
    ),

    # 16. Commodore 64 - 3 games
    (
        "c64_contra",
        "Commodore 64 (C64)",
        "cores/c64.dll",
        "VICE",
        "Computadores",
        "Contra (Gryzor)",
        "roms/C64/CONTRA_15273_01.zip",
        35,
        "none",
        "320x200 (50.12 Hz)",
        "MOS 6510 @ 0.985 MHz + SID 6581 Sound Chip",
        "A célebre conversão europeia de Contra com a inconfundível sonoridade analógica do sintetizador SID 6581.",
        "The iconic European conversion of Contra featuring the distinctive analog warmth of the MOS SID 6581 synthesizer.",
        [
            "Ocean Software / Konami Intro",
            "Gryzor Contra Title Screen",
            "Ação Run 'n Gun no Computador C64",
            "Shader CRT Commodore 1084S Monitor"
        ]
    ),
    (
        "c64_ghostbusters",
        "Commodore 64 (C64)",
        "cores/c64.dll",
        "VICE",
        "Computadores",
        "Ghostbusters",
        "roms/C64/GHOSTBU1_03133_03.zip",
        35,
        "none",
        "320x200 (50.12 Hz)",
        "MOS 6510 @ 0.985 MHz + SID 6581 Sound Chip",
        "O clássico imortal de David Crane com fala sintetizada 'Ghostbusters!' e a famosa melodia tocada no chip SID.",
        "David Crane's immortal classic with digitized speech and the iconic theme music played on the SID synthesizer.",
        [
            "Activision Ghostbusters Title",
            "Ecto-1 Navegando pelo Mapa de NY",
            "Captura de Fantasmas com Feixes de Prótons",
            "Shader CRT Trinitron Scanlines"
        ]
    ),
    (
        "c64_gng",
        "Commodore 64 (C64)",
        "cores/c64.dll",
        "VICE",
        "Computadores",
        "Ghosts 'n Goblins",
        "roms/C64/GHOSTNG1_03136_05.zip",
        35,
        "none",
        "320x200 (50.12 Hz)",
        "MOS 6510 @ 0.985 MHz + SID 6581 Sound Chip",
        "Arthur no cemitério assombrado com a extraordinária trilha sonora rearranjada por Chris Hülsbeck no chip SID.",
        "Arthur in the haunted graveyard featuring Chris Hülsbeck's legendary SID arranged soundtrack.",
        [
            "Elite / Capcom Ghosts 'n Goblins",
            "Sir Arthur no Cemitério Assombrado",
            "Lanças contra Zumbis em 320x200",
            "Filtro CRT Monitor Commodore"
        ]
    ),

    # 17. MSX / MSX2 - 3 games
    (
        "msx_aleste",
        "MSX / MSX2 / MSX2+",
        "cores/msx.dll",
        "fMSX",
        "Computadores",
        "Aleste",
        "roms/MSX/Aleste - Compile (1988) [English] [Translated] [3427].zip",
        35,
        "none",
        "256x212 (59.92 Hz)",
        "Zilog Z80A @ 3.58 MHz + Yamaha V9938 + PSG",
        "O shoot 'em up definitivo da Compile para o padrão MSX2 com armas intercambiáveis e ação frenética.",
        "Compile's definitive vertical shmup for the MSX2 standard with modular power-ups and intense action.",
        [
            "Compile Aleste Title Screen",
            "Prólogo Narrativo em Inglês",
            "Nave Aleste em Combate Orbital",
            "Shader CRT Trinitron 15kHz"
        ]
    ),
    (
        "msx_gradius2",
        "MSX / MSX2 / MSX2+",
        "cores/msx.dll",
        "fMSX",
        "Computadores",
        "Gradius 2",
        "roms/MSX/Gradius 2 (1987) (Konami).7z",
        35,
        "none",
        "256x212 (59.92 Hz)",
        "Zilog Z80A @ 3.58 MHz + Konami SCC Audio Chip",
        "Clássico absoluto da Konami equipado com o chip de áudio SCC de 5 canais de ondas customizadas exclusivas do MSX.",
        "Konami masterwork powered by the custom 5-channel SCC wavetable sound chip exclusive to MSX cartridges.",
        [
            "Konami MSX SCC Audio Intro",
            "Gradius 2 Title Screen",
            "Vic Viper no Espaço Profundo",
            "Shader CRT Trinitron NTSC"
        ]
    ),
    (
        "msx_metalgear2",
        "MSX / MSX2 / MSX2+",
        "cores/msx.dll",
        "fMSX",
        "Computadores",
        "Metal Gear 2: Solid Snake",
        "roms/MSX/Metal Gear 2 - Solid Snake (1990) (Konami).7z",
        35,
        "none",
        "256x212 (59.92 Hz)",
        "Zilog Z80A @ 3.58 MHz + Konami SCC Audio Chip",
        "A obra-prima de espionagem tática de Hideo Kojima com radar, rastejamento, rádio Codec e trilha SCC estéreo.",
        "Hideo Kojima's tactical espionage masterpiece featuring stealth radar, crawling, Codec and stereo SCC audio.",
        [
            "Konami / Hideo Kojima Title",
            "Infiltração de Solid Snake em Zanzibar Land",
            "Comunicações no Transceptor de Rádio",
            "Shader CRT Trinitron Scanlines"
        ]
    ),

    # 18. Neo Geo Pocket / Color (NGP) - 3 games
    (
        "ngp_metalslug",
        "SNK Neo Geo Pocket / Color",
        "cores/ngp.dll",
        "Beetle NeoPop",
        "Portáteis",
        "Metal Slug: 1st Mission",
        "roms/NGP/Metal Slug - 1st Mission (World) (En,Ja).zip",
        35,
        "none",
        "160x152 (59.92 Hz)",
        "Toshiba TLCS-900H 16-bit @ 6.14 MHz + Z80",
        "A clássica franquia da SNK adaptada brilhantemente ao portátil de 16 bits com missões não-lineares e veículos Slug.",
        "SNK's iconic run 'n gun adapted to the 16-bit handheld with branching missions and signature Metal Slug vehicles.",
        [
            "SNK Neo Geo Pocket Color Intro",
            "Metal Slug 1st Mission Title",
            "Marco Rossi em Ação Portátil",
            "Shader LCD Colorido Portátil"
        ]
    ),
    (
        "ngp_kof",
        "SNK Neo Geo Pocket / Color",
        "cores/ngp.dll",
        "Beetle NeoPop",
        "Portáteis",
        "The King of Fighters R-2",
        "roms/NGP/King of Fighters R-2 - Pocket Fighting Series (World) (En,Ja).zip",
        35,
        "none",
        "160x152 (59.92 Hz)",
        "Toshiba TLCS-900H 16-bit @ 6.14 MHz + Z80",
        "O aclamado jogo de luta portátil baseado em KOF '98 com modo Making de customização e animações expressivas.",
        "Acclaimed portable fighting game based on KOF '98 with expressive animations and deep character customization.",
        [
            "The King of Fighters R-2 Title",
            "Seleção de Trio: Kyo, Iori, Terry",
            "Combate Técnico em 160x152",
            "Shader Dot-Matrix LCD NGP"
        ]
    ),
    (
        "ngp_fatalfury",
        "SNK Neo Geo Pocket / Color",
        "cores/ngp.dll",
        "Beetle NeoPop",
        "Portáteis",
        "Fatal Fury: First Contact",
        "roms/NGP/Fatal Fury F-Contact - Pocket Fighting Series (World) (En,Ja).zip",
        35,
        "none",
        "160x152 (59.92 Hz)",
        "Toshiba TLCS-900H 16-bit @ 6.14 MHz + Z80",
        "Terry Bogard e os lutadores de South Town em combates ágeis adaptados da consagrada série Real Bout.",
        "Terry Bogard and South Town brawlers in fast-paced fighting action adapted from the Real Bout series.",
        [
            "SNK Fatal Fury First Contact Title",
            "Seleção de Lutadores de South Town",
            "Terry Bogard executando Power Geyser",
            "Shader LCD Retrô Pocket"
        ]
    ),

    # 19. Bandai WonderSwan / Color - 3 games
    (
        "wswan_makaimura",
        "Bandai WonderSwan / Color",
        "cores/wswan.dll",
        "Beetle WonderSwan",
        "Portáteis",
        "Makaimura for WonderSwan",
        "roms/WonderSwan/Makaimura for WonderSwan (Japan).zip",
        35,
        "none",
        "224x144 (75.47 Hz)",
        "NEC V30MZ 16-bit @ 3.07 MHz",
        "Capítulo exclusivo de Ghosts 'n Goblins desenvolvido sob medida para o hardware de Gunpei Yokoi com 75Hz de fluidez.",
        "Exclusive Ghosts 'n Goblins chapter tailored specifically for Gunpei Yokoi's handheld running at ultra-smooth 75Hz.",
        [
            "Bandai / Capcom WonderSwan Intro",
            "Makaimura for WonderSwan Title",
            "Sir Arthur no Reino dos Demônios",
            "Shader LCD WonderSwan Color"
        ]
    ),
    (
        "wswan_rockman",
        "Bandai WonderSwan / Color",
        "cores/wswan.dll",
        "Beetle WonderSwan",
        "Portáteis",
        "Rockman & Forte: Mirai kara no Chousensha",
        "roms/WonderSwan/Rockman and Forte - Challenger from the Future (Japan) (T-En by NikcDC v1.2.1) (n).zip",
        35,
        "none",
        "224x144 (75.47 Hz)",
        "NEC V30MZ 16-bit @ 3.07 MHz",
        "Aventura exclusiva de Mega Man e Bass em inglês enfrentando a ameaça dimensional do enigmático Dimesen.",
        "Exclusive Mega Man & Bass adventure translated to English featuring the mysterious dimensional foe Dimesen.",
        [
            "Capcom / Bandai Intro",
            "Rockman & Forte Title Screen",
            "Mega Buster em Ação a 75 Hz",
            "Shader LCD Portátil WonderSwan"
        ]
    ),
    (
        "wswan_finalfantasy",
        "Bandai WonderSwan / Color",
        "cores/wswan.dll",
        "Beetle WonderSwan",
        "Portáteis",
        "Final Fantasy I",
        "roms/WonderSwan/Final Fantasy (Japan) (T-En by Kalas v0.91) (i).zip",
        35,
        "none",
        "224x144 (75.47 Hz)",
        "NEC V30MZ 16-bit @ 3.07 MHz",
        "Remake visual de 16 bits de Final Fantasy I criado pela Squaresoft com novos sprites e arranjos musicais.",
        "16-bit visual remake of the original Final Fantasy by Squaresoft with enhanced sprites and arranged audio.",
        [
            "Squaresoft Final Fantasy Title",
            "Os Quatro Guerreiros da Luz",
            "Mapa do Mundo em 224x144",
            "Shader WonderSwan Color LCD"
        ]
    ),

    # 20. NEC PC-FX - 3 games
    (
        "pcfx_zeroigar",
        "NEC PC-FX",
        "cores/pcfx.dll",
        "Beetle PC-FX",
        "Consoles",
        "Choujin Heiki Zeroigar",
        "roms/PCFX/Choujin Heiki Zeroigar (English Translated, Rev 1.0.1).chd",
        35,
        "none",
        "320x240 / 640x480 (59.94 Hz)",
        "NEC V810 32-bit RISC @ 21.47 MHz + 6 Coprocessadores",
        "O principal shoot 'em up da plataforma com cenas de anime em tela cheia e áudio CD-DA traduzido em inglês.",
        "PC-FX flagship vertical shmup featuring full-motion anime cutscenes, uncompressed CD-DA audio and English text.",
        [
            "NEC Home Electronics PC-FX Boot",
            "Zeroigar Title Screen",
            "Mechas Espaciais e Batalhas em 60 FPS",
            "Shader CRT Trinitron 15kHz"
        ]
    ),
    (
        "pcfx_battleheat",
        "NEC PC-FX",
        "cores/pcfx.dll",
        "Beetle PC-FX",
        "Consoles",
        "Battle Heat",
        "roms/PCFX/Battle Heat (Japan).zip",
        35,
        "none",
        "320x240 (59.94 Hz)",
        "NEC V810 32-bit RISC @ 21.47 MHz + 6 Coprocessadores",
        "O inovador jogo de luta em animação interativa com descompressão JPEG em hardware a 30 FPS.",
        "Pioneering interactive anime fighter powered by hardware motion-JPEG decompression running in full screen.",
        [
            "Hudson Soft PC-FX Intro",
            "Battle Heat Title Screen",
            "Duelo de Anime em Tempo Real",
            "Filtro CRT Trinitron Retrô"
        ]
    ),
    (
        "pcfx_vajra",
        "NEC PC-FX",
        "cores/pcfx.dll",
        "Beetle PC-FX",
        "Consoles",
        "Kishin Douji Zenki FX: Vajra Fight",
        "roms/PCFX/Kishin Douji Zenki FX - Vajra Fight (Japan).zip",
        35,
        "none",
        "320x240 (59.94 Hz)",
        "NEC V810 32-bit RISC @ 21.47 MHz + 6 Coprocessadores",
        "Um dos mais valorizados beat 'em ups em 2D dos anos 90 com sprites gigantescos e múltiplos planos de rolagem.",
        "One of the rarest 2D beat 'em ups of the 90s featuring massive sprites, rich colors and multiple parallax planes.",
        [
            "Hudson Soft Zenki Vajra Fight",
            "Abertura em Anime Tradicional",
            "Combate Místico em 320x240",
            "Shader CRT Trinitron Phosphor"
        ]
    ),

    # 21. Commodore Amiga - 3 games
    (
        "amiga_turrican",
        "Commodore Amiga (500 / 1200)",
        "cores/amiga.dll",
        "PUAE",
        "Computadores",
        "Turrican",
        "roms/Amiga/Turrican (Germany).zip",
        35,
        "none",
        "320x256 (50.00 Hz PAL)",
        "Motorola 68000 @ 7.09 MHz + Paula 4ch Sound",
        "O lendário run 'n gun de Manfred Trenz e Chris Hülsbeck com áudio estéreo Paula de 4 canais e vastos labirintos.",
        "Manfred Trenz and Chris Hülsbeck's iconic run 'n gun with 4-channel DMA stereo Paula audio and sprawling worlds.",
        [
            "Rainbow Arts / Factor 5 Intro",
            "Turrican Title Screen",
            "Mundo Alienígena em 320x256 PAL",
            "Shader CRT Commodore 1084S"
        ]
    ),
    (
        "amiga_turrican2",
        "Commodore Amiga (500 / 1200)",
        "cores/amiga.dll",
        "PUAE",
        "Computadores",
        "Turrican II: The Final Fight",
        "roms/Amiga/Turrican II - The Final Fight (Germany).zip",
        35,
        "none",
        "320x256 (50.00 Hz PAL)",
        "Motorola 68000 @ 7.09 MHz + Paula 4ch Sound",
        "Considerado um dos maiores jogos de computador de todos os tempos com trilha sonora orquestrada de Chris Hülsbeck.",
        "Widely hailed as one of the greatest computer games in history with Chris Hülsbeck's orchestral chiptune opus.",
        [
            "Rainbow Arts Turrican II Intro",
            "Bren McGuire e a Armadura Turrican",
            "Ação em 50Hz Suave sem Quedas",
            "Shader CRT Trinitron PAL"
        ]
    ),
    (
        "amiga_guardian",
        "Commodore Amiga (500 / 1200)",
        "cores/amiga.dll",
        "PUAE",
        "Computadores",
        "Guardian CD32",
        "roms/Amiga/Guardian (Europe).chd",
        35,
        "none",
        "320x256 (50.00 Hz PAL)",
        "Motorola 68EC020 @ 14 MHz + AGA Chipset + Akiko",
        "Combate aéreo 3D em mundos poligonais aproveitando o poder do chipset AGA e chip Akiko do Amiga CD32.",
        "Full 3D aerial combat in textured worlds harnessing the Amiga CD32 AGA chipset and Akiko chunky-to-planar chip.",
        [
            "Amiga CD32 Boot Screen",
            "Guardian Title Screen",
            "Combate Poligonal 3D Fluido",
            "Shader CRT Retrô PAL"
        ]
    ),

    # 22. ColecoVision - 3 games
    (
        "coleco_burgertime",
        "ColecoVision",
        "cores/coleco.dll",
        "Coleco",
        "Consoles",
        "BurgerTime",
        "roms/ColecoVision/Burgertime (1984)(Coleco).zip",
        35,
        "none",
        "256x192 (60.00 Hz)",
        "Zilog Z80A @ 3.58 MHz + Texas TMS9928A",
        "Chef Peter Pepper montando hambúrgueres gigantes enquanto foge de salsichas e ovos em 60 FPS cravados.",
        "Chef Peter Pepper assembling towering burgers while dodging hot dogs and eggs at locked 60 FPS.",
        [
            "ColecoVision Coleco Logo",
            "BurgerTime Title Screen",
            "Chef Peter Pepper e Hambúrgueres",
            "Shader CRT Trinitron 15kHz"
        ]
    ),
    (
        "coleco_pacman",
        "ColecoVision",
        "cores/coleco.dll",
        "Coleco",
        "Consoles",
        "Pac-Man",
        "roms/ColecoVision/Pac-Man (1983) (Atarisoft) (Prototype).col",
        35,
        "none",
        "256x192 (60.00 Hz)",
        "Zilog Z80A @ 3.58 MHz + Texas TMS9928A",
        "A consagrada versão da Atarisoft para o ColecoVision com cores vibrantes do chip de vídeo TMS9928A.",
        "Atarisoft's celebrated ColecoVision port featuring vibrant Texas Instruments TMS9928A video colors.",
        [
            "Atarisoft Pac-Man Title",
            "Labirinto Clássico com Fantasmas",
            "Pílulas de Força em 256x192",
            "Shader CRT Trinitron Scanlines"
        ]
    ),
    (
        "coleco_pitfall2",
        "ColecoVision",
        "cores/coleco.dll",
        "Coleco",
        "Consoles",
        "Pitfall II: Lost Caverns",
        "roms/ColecoVision/Pitfall II - Lost Caverns (1983-84) (Activision).col",
        35,
        "none",
        "256x192 (60.00 Hz)",
        "Zilog Z80A @ 3.58 MHz + Texas TMS9928A",
        "Pitfall Harry nas cavernas subterrâneas com balões, morcegos e trilha musical contínua no chip SN76489.",
        "Pitfall Harry in subterranean caverns featuring bats, balloons and continuous musical score on the SN76489.",
        [
            "Activision ColecoVision Intro",
            "Pitfall II Lost Caverns Title",
            "Harry nas Cavernas Subterrâneas",
            "Shader CRT Trinitron Retrô"
        ]
    ),

    # 23. Sega Saturn - 3 games
    (
        "saturn_panzer",
        "Sega Saturn",
        "cores/saturn.dll",
        "Beetle Saturn",
        "Consoles",
        "Panzer Dragoon II Zwei",
        "roms/Saturn/Panzer Dragoon II Zwei (USA) (Track 1).bin",
        35,
        "none",
        "352x240 / 704x480 (59.94 Hz)",
        "Dual Hitachi SH-2 @ 28.6 MHz + VDP1/VDP2",
        "A consagrada fantasia pós-apocalíptica da Sega com dragões voadores, cenários VDP2 infinitos e trilha orquestrada.",
        "Sega's celebrated fantasy rail-shooter featuring flying dragons, infinite VDP2 backgrounds and arranged audio.",
        [
            "Sega Saturn 3D Boot Logo",
            "Team Andromeda Panzer Dragoon Title",
            "Voo do Dragão no VDP2 em 60 FPS",
            "Shader CRT Trinitron 15kHz"
        ]
    ),
    (
        "saturn_cyberia",
        "Sega Saturn",
        "cores/saturn.dll",
        "Beetle Saturn",
        "Consoles",
        "Cyberia",
        "roms/Saturn/Cyberia (USA) (EnFrDe).chd",
        35,
        "none",
        "352x240 / 704x480 (59.94 Hz)",
        "Dual Hitachi SH-2 @ 28.6 MHz + VDP1/VDP2",
        "Aventura de ficção científica cyberpunk com animações pré-renderizadas de alta taxa de dados e combates em 3D.",
        "Cyberpunk sci-fi thriller featuring cinematic pre-rendered video backdrops and 3D flight sequences.",
        [
            "Interplay / Xatrix Entertainment Intro",
            "Cyberia Title Screen",
            "Voo Sub-orbital e Combate Futurista",
            "Shader CRT Trinitron Scanlines"
        ]
    ),
    (
        "saturn_flashback",
        "Sega Saturn",
        "cores/saturn.dll",
        "Beetle Saturn",
        "Consoles",
        "Flashback",
        "roms/Saturn/Flashback RE.7z",
        35,
        "none",
        "352x240 / 704x480 (59.94 Hz)",
        "Dual Hitachi SH-2 @ 28.6 MHz + VDP1/VDP2",
        "A saga cinematográfica de Conrad B. Hart em Titan com cutscenes em FMV em tela cheia e áudio remasterizado em CD.",
        "Conrad B. Hart's cinematic sci-fi odyssey on Titan with full-screen FMV cutscenes and remastered CD audio.",
        [
            "Delphine Software Flashback Title",
            "Conrad B. Hart em Titan",
            "Plataforma Cinemática e Rotoscopia",
            "Shader CRT Trinitron Retrô"
        ]
    ),

    # 24. Sega Dreamcast - 3 games
    (
        "dreamcast_daytona",
        "Sega Dreamcast",
        "cores/dreamcast.dll",
        "Flycast",
        "3D & 64-Bit",
        "Daytona USA",
        "roms/Dreamcast/Daytona USA (USA).chd",
        35,
        "none",
        "640x480 (60.00 Hz)",
        "Hitachi SH-4 @ 200 MHz + PowerVR2 CLX2 3D",
        "A lenda das pistas da Sega em 480p nativo cravado a 60 FPS com a célebre voz de Takenobu Mitsuyoshi.",
        "Sega's iconic stock car racer running in crisp 480p at locked 60 FPS with Takenobu Mitsuyoshi's vocals.",
        [
            "Sega Dreamcast Swirl Boot Screen",
            "Daytona USA Title Screen",
            "Three Seven Speedway em 480p 60 FPS",
            "Shader CRT Trinitron VGA Scanlines"
        ]
    ),
    (
        "dreamcast_underdefeat",
        "Sega Dreamcast",
        "cores/dreamcast.dll",
        "Flycast",
        "3D & 64-Bit",
        "Under Defeat",
        "roms/Dreamcast/Under Defeat (Japan).chd",
        35,
        "none",
        "640x480 (60.00 Hz)",
        "Hitachi SH-4 @ 200 MHz + PowerVR2 CLX2 3D",
        "O primoroso shoot 'em up de helicópteros da G.rev com efeitos volumétricos de fumaça, reflexos e explosões dinâmicas.",
        "G.rev's breathtaking helicopter shooter featuring realistic volumetric smoke, water reflections and dynamic explosions.",
        [
            "G.rev Sega Dreamcast Intro",
            "Under Defeat Title Screen",
            "Combate de Helicóptero em 60 FPS",
            "Shader CRT Trinitron 480p"
        ]
    ),
    (
        "dreamcast_bassfishing",
        "Sega Dreamcast",
        "cores/dreamcast.dll",
        "Flycast",
        "3D & 64-Bit",
        "Sega Bass Fishing",
        "roms/Dreamcast/Sega Bass Fishing.cdi",
        35,
        "none",
        "640x480 (60.00 Hz)",
        "Hitachi SH-4 @ 200 MHz + PowerVR2 CLX2 3D",
        "O clássico esportivo arcade da Sega com física de peixes subaquática e áudio de alta energia a 60 FPS.",
        "Sega's energetic arcade fishing simulator with underwater camera physics and rock soundtrack at 60 FPS.",
        [
            "Sega Bass Fishing Arcade Title",
            "Lago Poligonal e Seleção de Isca",
            "Fisgada Subaquática em 480p",
            "Shader CRT VGA Monitor"
        ]
    ),

    # 25. SNK Neo Geo - 3 games
    (
        "neogeo_metalslug2",
        "SNK Neo Geo (MVS / AES / CD)",
        "cores/neogeo.dll",
        "Geolith / NeoCD",
        "Arcade",
        "Metal Slug 2",
        "roms/NeoGeo/Metal Slug 2 (World).chd",
        35,
        "none",
        "304x224 (59.18 Hz)",
        "Motorola 68000 @ 12 MHz + Z80 + YM2610",
        "A estreia de Eri Kasamoto e Fio Germi contra o General Morden com tanques Slug e áudio FM no sintetizador Yamaha YM2610.",
        "Eri Kasamoto and Fio Germi's debut fighting General Morden with tanks and Yamaha YM2610 FM audio.",
        [
            "SNK Neo Geo Giga Power Intro",
            "Metal Slug 2 Mission Title",
            "Ação Militar no Deserto em 304x224",
            "Shader CRT Trinitron Arcade"
        ]
    ),
    (
        "neogeo_metalslug3",
        "SNK Neo Geo (MVS / AES / CD)",
        "cores/neogeo.dll",
        "Geolith / NeoCD",
        "Arcade",
        "Metal Slug 3",
        "roms/NeoGeo/Metal Slug 3 (NGH-2560).neo",
        35,
        "none",
        "304x224 (59.18 Hz)",
        "Motorola 68000 @ 12 MHz + Z80 + YM2610",
        "Aclamado como o ápice da arte em pixel art do Neo Geo com rotas alternativas submarinas e zumbis alienígenas.",
        "Widely considered the pinnacle of 2D pixel art with branching underwater paths and alien zombie invasions.",
        [
            "SNK Neo Geo 100 Mega Shock",
            "Metal Slug 3 Title Screen",
            "Praia dos Crustáceos Gigantes",
            "Filtro CRT Trinitron Arcade Cabinet"
        ]
    ),
    (
        "neogeo_magicianlord",
        "SNK Neo Geo (MVS / AES / CD)",
        "cores/neogeo.dll",
        "Geolith / NeoCD",
        "Arcade",
        "Magician Lord",
        "roms/NeoGeo/Magician Lord (World).chd",
        35,
        "none",
        "304x224 (59.18 Hz)",
        "Motorola 68000 @ 12 MHz + Z80 + YM2610",
        "O clássico de lançamento do Neo Geo com o mago Elta enfrentando os asseclas de Gal Agiese em Cadacissa.",
        "Neo Geo launch showpiece starring the wizard Elta battling Gal Agiese across the realm of Cadacissa.",
        [
            "Alpha Denshi / SNK Magician Lord",
            "Prólogo Épico com Trilha FM YM2610",
            "Transformações Místicas em 304x224",
            "Shader CRT Trinitron Retrô"
        ]
    ),

    # 26. MS-DOS PC - 3 games
    (
        "dos_aitd",
        "MS-DOS PC (IBM PC / x86)",
        "cores/dosbox_pure.dll",
        "DOSBox Pure",
        "Computadores",
        "Alone in the Dark",
        "roms/DOS/Alone in the Dark.zip",
        35,
        "none",
        "320x200 (70.08 Hz)",
        "Intel 486 / Pentium + Sound Blaster 16",
        "O pioneiro survival horror 3D da Infogrames em Derceto Mansion com Edward Carnby e Emily Hartwood.",
        "Infogrames' genre-defining 3D survival horror in Derceto Mansion with Edward Carnby and Emily Hartwood.",
        [
            "Infogrames PC Boot Screen",
            "Alone in the Dark Title Screen",
            "O sótão de Derceto Mansion em 3D",
            "Shader CRT Monitor VGA IBM PC"
        ]
    ),
    (
        "dos_anotherworld",
        "MS-DOS PC (IBM PC / x86)",
        "cores/dosbox_pure.dll",
        "DOSBox Pure",
        "Computadores",
        "Another World (Out of This World)",
        "roms/DOS/Another World.zip",
        35,
        "none",
        "320x200 (70.08 Hz)",
        "Intel 486 / Pentium + Sound Blaster 16",
        "A revolução dos polígonos cinemáticos de Éric Chahi narrando o teletransporte de Lester Knight Chaykin a outro planeta.",
        "Éric Chahi's cinematic vector masterpiece following Lester Knight Chaykin's lightning teleportation.",
        [
            "Delphine Software PC Intro",
            "O Laboratório de Física e Tempestade",
            "Chegada ao Planeta Alienígena",
            "Shader CRT VGA 70Hz Scanlines"
        ]
    ),
    (
        "dos_battlechess",
        "MS-DOS PC (IBM PC / x86)",
        "cores/dosbox_pure.dll",
        "DOSBox Pure",
        "Computadores",
        "Battle Chess",
        "roms/DOS/Battle Chess (Enhanced).zip",
        35,
        "none",
        "320x200 (70.08 Hz)",
        "Intel 486 / Pentium + Sound Blaster 16",
        "O clássico de xadrez da Interplay onde as peças ganham vida e duelam com humor e animações detalhadas.",
        "Interplay's animated chess classic where pieces battle with witty humor and fluid combat animations.",
        [
            "Interplay Productions PC Title",
            "Tabuleiro de Xadrez Medieval",
            "Duelo de Peças em 320x200",
            "Shader CRT Monitor VGA Retrô"
        ]
    ),

    # 27. NEC PC-98 - 3 games
    (
        "pc98_rusty",
        "NEC PC-98",
        "cores/pc98.dll",
        "NP2kai",
        "Computadores",
        "Rusty",
        "roms/PC98/Rusty PC-98 - JeremyTV.rar",
        35,
        "none",
        "640x400 (56.40 Hz)",
        "NEC V30 / i486 @ 33 MHz + YM2608 OPNA FM",
        "A aclamada caçadora de vampiros Rusty em alta resolução 640x400 com a inconfundível trilha sonora do sintetizador Yamaha OPNA.",
        "Acclaimed gothic action platformer starring Rusty in 640x400 with rich Yamaha OPNA FM chiptune music.",
        [
            "C-Lab / Naoko Soft PC-98 Intro",
            "Rusty Title Screen em 640x400",
            "Ação Gótica com Chicote e Magia",
            "Shader CRT Monitor PC-98 Japonês"
        ]
    ),
    (
        "pc98_aitd",
        "NEC PC-98",
        "cores/pc98.dll",
        "NP2kai",
        "Computadores",
        "Alone in the Dark PC-98",
        "roms/PC98/aitd.rar",
        35,
        "none",
        "640x400 (56.40 Hz)",
        "NEC V30 / i486 @ 33 MHz + YM2608 OPNA FM",
        "Conversão oficial japonesa para computadores NEC PC-98 com resolução aprimorada e arranjos OPNA FM.",
        "Official Japanese release for NEC PC-98 computers with crisp high-resolution rendering and OPNA sound.",
        [
            "NEC PC-98 Infogrames Title",
            "Derceto Mansion em 640x400",
            "Investigação Sobrenatural em 3D",
            "Shader CRT Monitor PC-98 Scanlines"
        ]
    ),
    (
        "pc98_popmail",
        "NEC PC-98",
        "cores/pc98.dll",
        "NP2kai",
        "Computadores",
        "Popful Mail",
        "roms/PC98/popmail.rar",
        35,
        "none",
        "640x400 (56.40 Hz)",
        "NEC V30 / i486 @ 33 MHz + YM2608 OPNA FM",
        "O clássico RPG de ação da Nihon Falcom estrelado pela caçadora de recompensas Mail com retratos desenhados à mão.",
        "Nihon Falcom's classic action-RPG starring bounty hunter Mail with hand-drawn portraits and fast combat.",
        [
            "Nihon Falcom PC-98 Opening",
            "Popful Mail Title Screen",
            "Aventuras em Yekutol em 640x400",
            "Shader CRT Trinitron Retrô"
        ]
    ),

    # 28. Atari 2600 - 3 games
    (
        "atari2600_choplifter",
        "Atari 2600 (VCS)",
        "cores/atari2600.dll",
        "Stella",
        "8-Bit",
        "Choplifter",
        "roms/Atari2600/Choplifter.zip",
        35,
        "none",
        "160x192 (59.92 Hz)",
        "MOS 6507 @ 1.19 MHz + TIA Video/Audio",
        "Resgate de reféns com helicóptero enfrentando tanques e jatos com física de inércia precisa no emulador Stella.",
        "Hostage helicopter rescue facing tanks and jet fighters with precise inertia physics on the Stella core.",
        [
            "Broderbund / Atari Choplifter Title",
            "Decolagem do Helicóptero da Base",
            "Resgate de Reféns no Deserto",
            "Shader CRT Trinitron 15kHz NTSC"
        ]
    ),
    (
        "atari2600_doubledragon",
        "Atari 2600 (VCS)",
        "cores/atari2600.dll",
        "Stella",
        "8-Bit",
        "Double Dragon",
        "roms/Atari2600/Double Dragon.zip",
        35,
        "none",
        "160x192 (59.92 Hz)",
        "MOS 6507 @ 1.19 MHz + TIA Video/Audio",
        "Billy Lee combatendo a gangue Black Warriors no surpreendente porte de 128 bytes de RAM da Activision.",
        "Billy Lee battling the Black Warriors gang in Activision's technical showcase on 128 bytes of RAM.",
        [
            "Activision Double Dragon Title",
            "Billy Lee nas Ruas do Bairro",
            "Socos e Voadoras em 160x192",
            "Shader CRT Trinitron Phosphor"
        ]
    ),
    (
        "atari2600_kungfumaster",
        "Atari 2600 (VCS)",
        "cores/atari2600.dll",
        "Stella",
        "8-Bit",
        "Kung-Fu Master",
        "roms/Atari2600/Kung-Fu Master.zip",
        35,
        "none",
        "160x192 (59.92 Hz)",
        "MOS 6507 @ 1.19 MHz + TIA Video/Audio",
        "Thomas subindo os 5 andares do Templo do Diabo para resgatar Sylvia com socos, rasteiras e chutes voadores.",
        "Thomas ascending the Devil's Temple to rescue Sylvia with rapid punches, low sweeps and flying kicks.",
        [
            "Irem / Activision Title Screen",
            "Entrada do Templo do Diabo",
            "Combate Marcial em 160x192",
            "Filtro CRT Trinitron Retrô"
        ]
    ),

    # 29. PC Engine / TurboGrafx-16 - 2 games
    (
        "pce_gradius2",
        "PC Engine / TurboGrafx-16",
        "cores/pce.dll",
        "Beetle PCE",
        "16-Bit",
        "Gradius II: Gofer no Yabou",
        "roms/TurboGrafx16/Gradius II - Gofer no Yabou (Japan).chd",
        35,
        "none",
        "256x224 (59.82 Hz)",
        "Hudson HuC6280 @ 7.16 MHz + HuC6260 VDC",
        "Port lendário da Konami para o Super CD-ROM² com trilha sonora de orquestra em CD-DA e chefes gigantescos em 60 FPS.",
        "Legendary Konami Super CD-ROM² port with uncompressed CD-DA orchestral music and massive bosses at locked 60 FPS.",
        [
            "Konami Super CD-ROM² Intro",
            "Gradius II Title Screen",
            "Vic Viper contra os Sóis Flamejantes",
            "Shader CRT Trinitron Scanlines 15kHz"
        ]
    ),
    (
        "pce_popfulmail",
        "PC Engine / TurboGrafx-16",
        "cores/pce.dll",
        "Beetle PCE",
        "16-Bit",
        "Popful Mail",
        "roms/TurboGrafx16/Magical Fantasy Adventure - Popful Mail (English Dub)",
        35,
        "none",
        "256x224 (59.82 Hz)",
        "Hudson HuC6280 @ 7.16 MHz + HuC6260 VDC",
        "Aclamado RPG da Falcom para o Super CD-ROM² com dublagem completa em inglês e animações vibrantes.",
        "Acclaimed Falcom action-RPG with full English voice acting and colorful CD-ROM animations.",
        [
            "Nihon Falcom / NEC InterChannel Intro",
            "Popful Mail Title Screen",
            "Ação em Plataforma e Diálogos",
            "Shader CRT Trinitron Phosphor"
        ]
    ),

    # 30. Sega Mega CD - 1 game
    (
        "megacd_finalfight",
        "Sega Mega CD",
        "cores/genesis.dll",
        "Genesis Plus GX",
        "16-Bit",
        "Final Fight CD",
        "roms/MegaCD/Final Fight CD (USA).chd",
        35,
        "none",
        "320x224 (59.92 Hz)",
        "Motorola 68000 @ 12.5 MHz + Z80 + Ricoh RF5C164",
        "A versão definitiva dos anos 90 com Guy, Cody e Haggar, todas as fases do fliperama e trilha sonora em CD-DA.",
        "The definitive 90s console port featuring Guy, Cody and Haggar, all arcade stages and CD-DA soundtrack.",
        [
            "Sega CD BIOS & Capcom Intro",
            "Final Fight CD Title Screen",
            "Metro City Slums: Haggar e Cody",
            "Shader CRT Trinitron 15kHz"
        ]
    ),

    # 31. Nintendo DS - 1 game
    (
        "nds_chronotrigger",
        "Nintendo DS",
        "cores/nds.dll",
        "melonDS",
        "Portáteis",
        "Chrono Trigger DS",
        "roms/NDS/Chrono Trigger DS (BR).nds",
        35,
        "none",
        "256x384 (Dual Screen 60.00 Hz)",
        "ARM946E-S @ 67 MHz + ARM7TDMI @ 33 MHz",
        "Layout de tela dupla perfeitamente alinhado com a lendária trilha sonora de Yasunori Mitsuda em português.",
        "Seamless dual-screen layout with Yasunori Mitsuda's legendary soundtrack and touch emulation.",
        [
            "Tela Dupla NDS: Pintura de Akira Toriyama",
            "Pêndulo do Tempo Animado",
            "Abertura com Marle: 'Toque para Iniciar'",
            "Shader Dual-Screen Touchscreen LCD"
        ]
    ),

    # 32. Sony PlayStation 1 - 1 game
    (
        "psx_gta2",
        "Sony PlayStation (PS1)",
        "cores/psx.dll",
        "Beetle PSX",
        "32-Bit",
        "Grand Theft Auto 2",
        "roms/PlayStation/Grand Theft Auto 2 (USA).chd",
        35,
        "none",
        "320x240 / 640x480 (59.94 Hz)",
        "MIPS R3000A 32-bit @ 33.86 MHz + GTE + SPU 24ch",
        "O clássico GTA 2 em CD rodando com leitor CHD nativo, áudio 24 canais SPU e suporte RetroAchievements.",
        "Classic GTA 2 running via native libchdr CHD reader, 24-channel SPU audio and RetroAchievements support.",
        [
            "Sony Computer Entertainment PS1 BIOS",
            "Rockstar Games / DMA Design Intro",
            "GTA 2 Loading Screen com Trilha Original",
            "Shader CRT Trinitron 24-bit SPU CD-DA"
        ]
    ),

    # 33. Sony PlayStation 2 - 1 game
    (
        "ps2_phantasystar",
        "Sony PlayStation 2 (PS2)",
        "cores/ps2_play.dll",
        "LRPS2 (PCSX2) / Play!",
        "128-Bit",
        "Phantasy Star Generation: 1",
        "roms/PlayStation2/Phantasy Star Generation 1.iso",
        35,
        "none",
        "640x448 / 640x480 (59.94 Hz)",
        "Emotion Engine 128-bit @ 294.912 MHz + VU0/VU1 + GS",
        "Remake em alta fidelidade da saga de Alis Landale no sistema solar de Algol com gráficos poligonais e áudio estéreo.",
        "High-fidelity remake of Alis Landale's quest in the Algol solar system with polygonal rendering and stereo sound.",
        [
            "Sega Ages 2500 PS2 Intro",
            "Phantasy Star Generation: 1 Title",
            "Alis Landale na Cidade de Camineet",
            "Shader CRT Trinitron Componente 480i"
        ]
    ),

    # 34. Sony PSP - 1 game
    (
        "psp_rondo",
        "Sony PlayStation Portable (PSP)",
        "cores/psp.dll",
        "PPSSPP",
        "Portáteis",
        "Castlevania: The Dracula X Chronicles",
        "roms/PSP/Castlevania Rondo of Blood (PSP) (BR) (T1.0).iso",
        35,
        "none",
        "480x272 (60.00 Hz)",
        "MIPS R4000 32-bit @ 333 MHz + Media Engine",
        "O remake 2.5D de Rondo of Blood em português com Richter Belmont enfrentando o Conde Drácula em 16:9 widescreen.",
        "2.5D remake of Rondo of Blood with Richter Belmont facing Count Dracula in native 16:9 widescreen.",
        [
            "Konami PSP Widescreen Intro",
            "The Dracula X Chronicles Title",
            "Richter Belmont no Vilarejo em Chamas",
            "Shader LCD PSP Widescreen Grid"
        ]
    ),

    # 35. Nintendo GameCube - 1 game
    (
        "gamecube_mariosunshine",
        "Nintendo GameCube",
        "cores/gamecube.dll",
        "Dolphin",
        "Consoles",
        "Super Mario Sunshine",
        "roms/GameCube/Mario Sunshine (BR).iso",
        35,
        "none",
        "640x480 (60.00 Hz)",
        "IBM PowerPC 'Gekko' @ 485 MHz + ATI 'Flipper' GPU",
        "Mario e o dispositivo FLUDD na paradisíaca Ilha Delfino com física de água dinâmica e iluminação ambiente em português.",
        "Mario and the FLUDD water cannon on tropical Isle Delfino with dynamic fluid physics and ambient lighting.",
        [
            "Nintendo GameCube 3D Cube Intro",
            "Super Mario Sunshine Title Screen",
            "Delfino Airstrip: Jato d'Água do FLUDD",
            "Shader CRT Trinitron Componente 480p"
        ]
    ),

    # 36. Sinclair ZX Spectrum - 1 game
    (
        "spectrum_rtype",
        "Sinclair ZX Spectrum",
        "cores/spectrum.dll",
        "Fuse",
        "Computadores",
        "R-Type",
        "roms/ZXSpectrum/R-Type (1988)(Electric Dreams Software).tzx",
        35,
        "none",
        "256x192 (50.00 Hz PAL)",
        "Zilog Z80A @ 3.5 MHz + ULA Video",
        "A impressionante conversão de R-Type para fita cassete com rolagem suave e a icônica paleta monocromática com atributo de cor.",
        "Impressive tape cassette port of R-Type with smooth scrolling and the iconic attribute-clashing ZX Spectrum palette.",
        [
            "Electric Dreams R-Type Title Screen",
            "Nave R-9 no Labirinto Bydo",
            "Combate em 256x192 50Hz PAL",
            "Shader CRT Monitor Retrô PAL"
        ]
    ),

    # 37. Nintendo 3DS - 1 game
    (
        "3ds_shinobi3",
        "Nintendo 3DS",
        "cores/3ds.dll",
        "Citra",
        "Portáteis",
        "3D Shinobi III: Return of the Ninja Master",
        "roms/3DS/3D Shinobi III Return of the Ninja Master (Europe) (eShop).3ds",
        35,
        "none",
        "400x240 / 800x240 (60.00 Hz)",
        "Dual-core ARM11 MPCore @ 268 MHz + PICA200 GPU",
        "Edição 3D estereoscópica remasterizada pela M2 com parallax multicamadas aprofundado e áudio FM cristalino.",
        "M2's remastered stereoscopic 3D edition with deep multi-layer parallax scrolling and crisp FM sound.",
        [
            "Nintendo 3DS eShop Sega 3D Classics",
            "3D Shinobi III Title Screen",
            "Joe Musashi em Ação Ninja Estereoscópica",
            "Shader LCD 3DS Dual Grid"
        ]
    )
]

def analyze_and_fix_screenshots(base_id, mp4_path, staging_dir):
    """
    Validates the 4 screenshots generated for base_id.
    If any image is black or has <= 2 unique colors (fade transition),
    it dynamically re-extracts an active frame from the MP4.
    """
    shots = ["_01", "_02", "_03", "_04_crt"]
    fallback_times = [6, 14, 24, 30]

    for idx, shot in enumerate(shots):
        webp_file = os.path.join(staging_dir, f"{base_id}{shot}.webp")
        needs_re_extract = False

        if not os.path.exists(webp_file) or os.path.getsize(webp_file) < 3000:
            needs_re_extract = True
        else:
            try:
                im = Image.open(webp_file)
                colors = im.getcolors(maxcolors=20)
                if colors and len(colors) <= 2:
                    needs_re_extract = True
            except Exception:
                needs_re_extract = True

        if needs_re_extract and os.path.exists(mp4_path):
            t = fallback_times[idx]
            # Try to extract a clean frame
            for offset in [0, 2, -2, 4, -3]:
                sample_t = max(2, t + offset)
                cmd = [
                    "ffmpeg", "-y", "-ss", str(sample_t),
                    "-i", mp4_path, "-vframes", "1",
                    "-c:v", "libwebp", "-quality", "92",
                    webp_file
                ]
                subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if os.path.exists(webp_file) and os.path.getsize(webp_file) > 3000:
                    try:
                        im = Image.open(webp_file)
                        colors = im.getcolors(maxcolors=20)
                        if not colors or len(colors) > 2:
                            break
                    except Exception:
                        pass

def record_single_game(entry):
    base_id, sys_name, core_dll, core_display, category, game_name, rom_rel, duration, macro, res, cpu, desc_pt, desc_en, captions = entry
    rom_full = os.path.join(ROOT_DIR, rom_rel)
    core_full = os.path.join(ROOT_DIR, core_dll)

    if not os.path.exists(rom_full):
        return (base_id, False, f"ROM not found: {rom_rel}")
    if not os.path.exists(core_full):
        return (base_id, False, f"Core not found: {core_dll}")

    target_mp4 = os.path.join(STAGING_DIR, f"{base_id}.mp4")
    target_webm = os.path.join(STAGING_DIR, f"{base_id}.webm")

    # If already recorded in staging with valid size and webm exists, validate screenshots and skip
    if os.path.exists(target_mp4) and os.path.getsize(target_mp4) > 200000 and os.path.exists(target_webm) and os.path.getsize(target_webm) > 100000:
        analyze_and_fix_screenshots(base_id, target_mp4, STAGING_DIR)
        print(f"[CACHE] {base_id} ({game_name}) já gravado e validado em staging.")
        return (base_id, True, "Cache")

    print(f"\n[INICIANDO] {base_id} -> {game_name} ({sys_name}) [{duration}s]...")

    cmd = [
        RECORDER,
        core_full,
        rom_full,
        str(duration),
        STAGING_DIR,
        base_id,
        "0",  # upscale
        "0",  # filter mode
        "1",  # no-input
        "0",  # skip-extras: 0 = generates shots and webm!
        macro if macro else "none"
    ]

    t0 = time.time()
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
        elapsed = time.time() - t0
    except subprocess.TimeoutExpired:
        return (base_id, False, "Timeout após 120s")
    except Exception as e:
        return (base_id, False, f"Exceção ao gravar: {e}")

    if not os.path.exists(target_mp4) or os.path.getsize(target_mp4) < 100000:
        return (base_id, False, "MP4 não gerado ou tamanho menor que 100KB")

    # Analyze and fix screenshots if any fell on black fade transitions
    analyze_and_fix_screenshots(base_id, target_mp4, STAGING_DIR)

    # Validate output
    mp4_sz = os.path.getsize(target_mp4) / 1024 / 1024
    webm_sz = os.path.getsize(target_webm) / 1024 / 1024 if os.path.exists(target_webm) else 0

    print(f"[OK] {base_id} ({game_name}) gravado em {elapsed:.1f}s | MP4: {mp4_sz:.2f}MB, WebM: {webm_sz:.2f}MB")
    return (base_id, True, "Sucesso")

def main():
    os.makedirs(STAGING_DIR, exist_ok=True)
    os.makedirs(SITE_VIDEOS_DIR, exist_ok=True)
    os.makedirs(SITE_SHOTS_DIR, exist_ok=True)

    print("=" * 75)
    print(f"KARAMELO MEDIA PIPELINE: Gravando {len(RECORDINGS)} jogos de 37 sistemas...")
    print("=" * 75)

    success_count = 0
    failed_count = 0
    results = {}

    # Sequential or 2-thread execution for rock-solid stability and NVENC encoding
    with ThreadPoolExecutor(max_workers=2) as pool:
        future_map = {pool.submit(record_single_game, r): r for r in RECORDINGS}
        for fut in as_completed(future_map):
            r = future_map[fut]
            base_id = r[0]
            try:
                bid, ok, msg = fut.result()
                results[bid] = (ok, msg)
                if ok:
                    success_count += 1
                else:
                    failed_count += 1
                    print(f"[FALHA] {bid}: {msg}")
            except Exception as e:
                failed_count += 1
                results[base_id] = (False, str(e))
                print(f"[ERRO] {base_id}: {e}")

    print("\n" + "=" * 75)
    print(f"GRAVAÇÕES CONCLUÍDAS: {success_count} SUCESSOS, {failed_count} FALHAS.")
    print("=" * 75)

    # Copy files to site gallery
    print("\n[DEPLOY LOCAL] Copiando arquivos validados para c:\\github\\Karamelo Site\\public\\gallery...")
    copied_videos = 0
    copied_shots = 0

    for r in RECORDINGS:
        bid = r[0]
        ok, _ = results.get(bid, (False, ""))
        if not ok:
            continue

        mp4_src = os.path.join(STAGING_DIR, f"{bid}.mp4")
        webm_src = os.path.join(STAGING_DIR, f"{bid}.webm")

        if os.path.exists(mp4_src):
            shutil.copy2(mp4_src, os.path.join(SITE_VIDEOS_DIR, f"{bid}.mp4"))
            copied_videos += 1
        if os.path.exists(webm_src):
            shutil.copy2(webm_src, os.path.join(SITE_VIDEOS_DIR, f"{bid}.webm"))
            copied_videos += 1

        for shot in ["_01", "_02", "_03", "_04_crt"]:
            w_src = os.path.join(STAGING_DIR, f"{bid}{shot}.webp")
            if os.path.exists(w_src):
                shutil.copy2(w_src, os.path.join(SITE_SHOTS_DIR, f"{bid}{shot}.webp"))
                copied_shots += 1

    print(f"[DEPLOY LOCAL] Copiados {copied_videos} vídeos e {copied_shots} capturas WebP com sucesso.")

    # Update gallery-data.js
    update_gallery_data()

def update_gallery_data():
    print("\n[CATÁLOGO] Atualizando c:\\github\\Karamelo Site\\app\\gallery-data.js...")
    entries_js = []

    for r in RECORDINGS:
        bid, sys_name, core_dll, core_display, category, game_name, rom_rel, duration, macro, res, cpu, desc_pt, desc_en, captions = r

        shots_js = []
        shots_suffixes = ["_01", "_02", "_03", "_04_crt"]
        for idx, sfx in enumerate(shots_suffixes):
            cap = captions[idx] if idx < len(captions) else "Captura Pixel-Perfect 60 FPS"
            shots_js.append(f'      {{ src: "/gallery/shots/{bid}{sfx}.webp", caption: "{cap}" }}')

        shots_block = ",\n".join(shots_js)

        entry = f"""  {{
    id: "{bid}",
    name: "{sys_name}",
    core: "{core_display}",
    category: "{category}",
    game: "{game_name}",
    cover: "/gallery/shots/{bid}_02.webp",
    video: "/gallery/videos/{bid}.webm",
    videoMp4: "/gallery/videos/{bid}.mp4",
    shots: [
{shots_block}
    ],
    resolution: "{res}",
    cpu: "{cpu}",
    desc: {{
      pt: "{desc_pt}",
      en: "{desc_en}"
    }}
  }}"""
        entries_js.append(entry)

    full_js = "export const ALL_GALLERY_CORES = [\n" + ",\n\n".join(entries_js) + "\n];\n"

    with open(GALLERY_DATA_JS, "w", encoding="utf-8") as f:
        f.write(full_js)

    print(f"[CATÁLOGO] gallery-data.js atualizado com sucesso com {len(entries_js)} entradas de jogos!")

if __name__ == "__main__":
    main()
