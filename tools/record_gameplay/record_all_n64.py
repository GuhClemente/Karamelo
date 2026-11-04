#!/usr/bin/env python3
"""
Suíte de Gravação Automatizada de Nintendo 64 para o Karamelo Emulador
Gravações padronizadas de 90s, determinísticas (Attract Mode / sem inputs)
Multiplataforma: Windows, Linux e macOS.
"""

import os, sys, time, subprocess, platform

IS_WIN = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"

EXT_CORE = ".dll" if IS_WIN else (".dylib" if IS_MAC else ".so")
RECORDER_BIN = "record_gameplay.exe" if IS_WIN else "record_gameplay"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
RECORDER = os.path.join(SCRIPT_DIR, "out", RECORDER_BIN)

GAMES = [
    {
        "id": "waverace",
        "name": "Wave Race 64",
        "rom": os.path.join(ROOT_DIR, "roms/Nintendo64/Wave Race 64 Kawasaki Jet Ski (USA) (Rev 1).z64")
    },
    {
        "id": "starfox",
        "name": "Star Fox 64",
        "rom": os.path.join(ROOT_DIR, "roms/Nintendo64/Star Fox 64 (USA) (Rev 1).z64")
    },
    {
        "id": "castlevania",
        "name": "Castlevania",
        "rom": os.path.join(ROOT_DIR, "roms/Nintendo64/Castlevania (USA) (Rev 2).z64")
    },
    {
        "id": "perfectdark",
        "name": "James Bond (Perfect Dark)",
        "rom": os.path.join(ROOT_DIR, "roms/Nintendo64/Perfect Dark (U) (V1.1) [!].z64")
    },
    {
        "id": "topgear2",
        "name": "Top Gear Rally 2",
        "rom": os.path.join(ROOT_DIR, "roms/Nintendo64/Top Gear Rally 2 (USA).z64")
    }
]

VARIANTS = [
    {
        "suffix": "01_Parallel",
        "core": os.path.join(ROOT_DIR, f"cores/n64_parallel{EXT_CORE}"),
        "upscale": 0,
        "desc": "ParaLLEl N64 (Nativo 320x240)"
    },
    {
        "suffix": "02_Mupen",
        "core": os.path.join(ROOT_DIR, f"cores/n64_mupen{EXT_CORE}"),
        "upscale": 0,
        "desc": "Mupen64Plus-Next (Nativo 320x240)"
    },
    {
        "suffix": "03_Gopher_1x_Desligado",
        "core": os.path.join(ROOT_DIR, f"cores/n64_gopher{EXT_CORE}"),
        "upscale": 0,
        "desc": "Gopher64 (Desligado 320x240)"
    },
    {
        "suffix": "04_Gopher_2x_640",
        "core": os.path.join(ROOT_DIR, f"cores/n64_gopher{EXT_CORE}"),
        "upscale": 1,
        "desc": "Gopher64 (2x Horiz 640x240)"
    },
    {
        "suffix": "05_Gopher_4x_1280",
        "core": os.path.join(ROOT_DIR, f"cores/n64_gopher{EXT_CORE}"),
        "upscale": 2,
        "desc": "Gopher64 (4x Ultra 1280x240)"
    }
]

OUT_DIR = os.path.join(ROOT_DIR, "videos_comparativo_n64")
DURATION_SEC = 90

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    progress_file = os.path.join(OUT_DIR, "PROGRESS.txt")

    total_tasks = len(GAMES) * len(VARIANTS)
    current = 0

    print(f"=== INICIANDO GRAVAÇÃO PADRONIZADA DE N64 ({total_tasks} VÍDEOS DE {DURATION_SEC}s) ===")
    print(f"Plataforma: {platform.system()} ({platform.machine()})")
    print(f"Gravador: {RECORDER}")
    print(f"Saída: {OUT_DIR}\n")

    with open(progress_file, "a", encoding="utf-8") as pf:
        pf.write(f"\n--- Sessão: {time.strftime('%Y-%m-%d %H:%M:%S')} ({platform.system()}, {total_tasks} vídeos) ---\n")

    for g_idx, game in enumerate(GAMES, start=1):
        for v_idx, variant in enumerate(VARIANTS, start=1):
            current += 1
            base_name = f"{g_idx:02d}_{game['id']}_{variant['suffix']}"
            target_mp4 = os.path.join(OUT_DIR, f"{base_name}.mp4")

            # Skip if already exists and size > 1MB
            if os.path.exists(target_mp4) and os.path.getsize(target_mp4) > 1024 * 1024:
                print(f"[{current:02d}/{total_tasks:02d}] PULANDO (já gravado): {base_name}.mp4 ({os.path.getsize(target_mp4)/1024/1024:.2f} MB)")
                continue

            print(f"\n[{current:02d}/{total_tasks:02d}] Gravando {game['name']} - {variant['desc']}...")
            print(f"         Base: {base_name}")
            t0 = time.time()

            cmd = [
                RECORDER,
                variant["core"],
                game["rom"],
                str(DURATION_SEC),
                OUT_DIR,
                base_name,
                str(variant["upscale"]),
                "0",  # filter_mode: Raw
                "1",  # no_input: 1 (zero buttons)
                "1"   # skip_extras: 1 (only MP4)
            ]

            try:
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
                elapsed = time.time() - t0
                if os.path.exists(target_mp4):
                    fsize_mb = os.path.getsize(target_mp4) / 1024 / 1024
                    msg = f"[{current:02d}/{total_tasks:02d}] OK: {base_name}.mp4 ({fsize_mb:.2f} MB em {elapsed:.1f}s)"
                    print(msg)
                    with open(progress_file, "a", encoding="utf-8") as pf:
                        pf.write(f"{time.strftime('%H:%M:%S')} - {msg}\n")
                else:
                    msg = f"[{current:02d}/{total_tasks:02d}] ERRO: Arquivo {base_name}.mp4 não foi gerado!"
                    print(msg)
                    print("Tail do log:")
                    print("\n".join(res.stdout.splitlines()[-10:]))
                    with open(progress_file, "a", encoding="utf-8") as pf:
                        pf.write(f"{time.strftime('%H:%M:%S')} - {msg}\n")
            except Exception as e:
                print(f"[{current:02d}/{total_tasks:02d}] EXCEÇÃO: {e}")

    print("\n=== TODAS AS GRAVAÇÕES FORAM CONCLUÍDAS COM SUCESSO! ===")

if __name__ == "__main__":
    main()

