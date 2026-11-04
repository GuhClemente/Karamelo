#!/usr/bin/env python3
"""
Suíte Universal de Gravação Automatizada Multiplataforma para o Karamelo Emulador
Permite gravar gameplays, attract modes e comparativos de qualquer core/sistema:
Windows, Linux e macOS.

Exemplos de uso:
  python record_suite.py --rom "roms/SNES/Super Mario World (USA).sfc" --core cores/snes.dll --duration 60
  python record_suite.py --system "Nintendo64" --duration 90 --no-input
  python record_suite.py --system "SNES" --duration 60 --no-input
"""

import os, sys, time, argparse, subprocess, platform

IS_WIN = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"

EXT_CORE = ".dll" if IS_WIN else (".dylib" if IS_MAC else ".so")
RECORDER_BIN = "record_gameplay.exe" if IS_WIN else "record_gameplay"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
RECORDER = os.path.join(SCRIPT_DIR, "out", RECORDER_BIN)

SYSTEM_CORE_MAP = {
    "nintendo64": "n64_gopher",
    "n64": "n64_gopher",
    "snes": "snes",
    "genesis": "genesis",
    "megadrive": "genesis",
    "nes": "nes",
    "gameboy": "gb",
    "gb": "gb",
    "gba": "gba",
    "playstation": "psx",
    "psx": "psx",
    "ps1": "psx",
    "saturn": "saturn",
    "dreamcast": "dreamcast",
    "arcade": "arcade_fbneo",
    "neogeo": "neogeo",
    "master": "sms",
    "mastersystem": "sms",
    "fmtowns": "fmtowns",
    "pc98": "pc98",
    "msx": "msx",
    "amiga": "amiga"
}

def parse_args():
    p = argparse.ArgumentParser(description="Karamelo Automated Media Recording Suite")
    p.add_argument("--rom", help="Caminho direto para a ROM a gravar")
    p.add_argument("--core", help="Caminho direto para a DLL/SO/dylib do core")
    p.add_argument("--system", help="Nome do sistema (ex: n64, snes, genesis, psx)")
    p.add_argument("--duration", type=int, default=60, help="Duração da gravação em segundos (padrão: 60)")
    p.add_argument("--out", default="videos_gravados", help="Diretório de saída dos vídeos")
    p.add_argument("--no-input", action="store_true", default=True, help="Gravar sem inputs de botão (Attract Mode)")
    p.add_argument("--with-input", action="store_true", help="Ativar scripts automáticos de botão se disponíveis")
    p.add_argument("--upscale", type=int, default=0, help="Modo de Upscale (0=Desligado, 1=2x, 2=4x - apenas N64 Gopher)")
    p.add_argument("--filter", type=int, default=0, help="Modo de filtro de vídeo (0=Raw, 1=Bilinear, 8=CRT Scanlines)")
    return p.parse_args()

def main():
    args = parse_args()
    if not os.path.exists(RECORDER):
        print(f"[ERRO] Gravador não encontrado em: {RECORDER}")
        print("Compile o gravador primeiro executando:")
        if IS_WIN:
            print("  tools\\record_gameplay\\build.bat")
        elif IS_MAC:
            print("  tools/record_gameplay/build_macos.sh")
        else:
            print("  tools/record_gameplay/build_linux.sh")
        sys.exit(1)

    out_dir = os.path.join(ROOT_DIR, args.out)
    os.makedirs(out_dir, exist_ok=True)
    no_input = 0 if args.with_input else 1

    # Modo 1: ROM e Core diretos
    if args.rom and args.core:
        rom_path = os.path.abspath(args.rom)
        core_path = os.path.abspath(args.core)
        base_name = os.path.splitext(os.path.basename(rom_path))[0].replace(" ", "_")
        run_record(core_path, rom_path, args.duration, out_dir, base_name, args.upscale, args.filter, no_input)
        return

    # Modo 2: Por Sistema
    if args.system:
        sys_key = args.system.lower()
        core_name = SYSTEM_CORE_MAP.get(sys_key, sys_key)
        core_file = os.path.join(ROOT_DIR, "cores", f"{core_name}{EXT_CORE}")
        
        # Procurar pasta de ROMs do sistema
        rom_dir = None
        for d in os.listdir(os.path.join(ROOT_DIR, "roms")):
            if d.lower() == sys_key or d.lower().replace(" ", "") == sys_key:
                rom_dir = os.path.join(ROOT_DIR, "roms", d)
                break
        
        if not rom_dir or not os.path.exists(rom_dir):
            print(f"[ERRO] Pasta de ROMs para o sistema '{args.system}' não encontrada em roms/")
            sys.exit(1)

        rom_files = [os.path.join(rom_dir, f) for f in os.listdir(rom_dir) if not f.lower().startswith("leia-me")]
        if not rom_files:
            print(f"[AVISO] Nenhuma ROM encontrada em {rom_dir}")
            sys.exit(0)

        print(f"=== Gravando {len(rom_files)} ROMs do sistema {args.system} com {core_name} ({args.duration}s cada) ===")
        for i, rom_path in enumerate(rom_files, start=1):
            base_name = f"{i:02d}_{os.path.splitext(os.path.basename(rom_path))[0].replace(' ', '_')}"
            run_record(core_file, rom_path, args.duration, out_dir, base_name, args.upscale, args.filter, no_input)
        return

    print("Use --help para ver as opções. Exemplo:")
    print("  python record_suite.py --system Nintendo64 --duration 90")

def run_record(core_path, rom_path, duration, out_dir, base_name, upscale, filter_mode, no_input):
    target_mp4 = os.path.join(out_dir, f"{base_name}.mp4")
    if os.path.exists(target_mp4) and os.path.getsize(target_mp4) > 1024 * 1024:
        print(f"[PULANDO] Já gravado: {target_mp4}")
        return

    print(f"\n[GRAVANDO] {base_name} ({duration}s)...")
    cmd = [
        RECORDER,
        core_path,
        rom_path,
        str(duration),
        out_dir,
        base_name,
        str(upscale),
        str(filter_mode),
        str(no_input),
        "1" # skip_extras: só MP4
    ]

    t0 = time.time()
    try:
        subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        elapsed = time.time() - t0
        if os.path.exists(target_mp4):
            mb = os.path.getsize(target_mp4) / 1024 / 1024
            print(f"[OK] {base_name}.mp4 gerado com sucesso ({mb:.2f} MB em {elapsed:.1f}s)")
        else:
            print(f"[FALHA] Não foi possível gerar {base_name}.mp4")
    except Exception as e:
        print(f"[EXCEÇÃO] {e}")

if __name__ == "__main__":
    main()

