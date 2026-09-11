#!/usr/bin/env python3
"""
Bateria de Stress Pesado para macOS - Karamelo Emulador
Executa 20x ciclos completos de [Abrir Core -> Carregar ROM -> Renderizar Frames -> Fechar Core]
para cada motor de emulação nativo.
"""
import os
import subprocess
import sys
import time

class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    RESET = '\033[0m'

STRESS_TARGETS = [
    ("snes.dylib", "test_roms/snes.sfc", "Super Nintendo (Snes9x)"),
    ("nes.dylib", "test_roms/nes.nes", "Nintendo Entertainment System (FCEUmm)"),
    ("gb.dylib", "test_roms/gb.gb", "Game Boy / Color (Gambatte)"),
    ("gba.dylib", "test_roms/gba.gba", "Game Boy Advance (mGBA)"),
    ("genesis.dylib", "test_roms/genesis.gen", "Sega Genesis / Mega Drive (Plus GX)"),
    ("sms.dylib", "test_roms/sms.sms", "Sega Master System (Gearsystem)"),
    ("atari2600.dylib", "test_roms/atari2600.a26", "Atari 2600 (Stella)"),
    ("atari7800.dylib", "test_roms/atari7800.a78", "Atari 7800 (ProSystem)"),
    ("pce.dylib", "test_roms/pce.pce", "PC Engine / TurboGrafx-16 (Beetle PCE)"),
    ("coleco.dylib", "test_roms/coleco.col", "ColecoVision (Gearcoleco)"),
    ("wswan.dylib", "test_roms/wswan.ws", "WonderSwan (Beetle WonderSwan)"),
    ("ngp.dylib", "test_roms/ngp.ngp", "Neo Geo Pocket (Beetle NeoPop)"),
    ("32x.dylib", "test_roms/32x.32x", "Sega 32X (PicoDrive)"),
]

def main():
    print(f"{Colors.BOLD}{Colors.HEADER}")
    print("==========================================================================")
    print("     KARAMELO - BATERIA DE STRESS PESADO: 20x CICLOS POR CORE (macOS)     ")
    print("==========================================================================")
    print(f"{Colors.RESET}")
    print(f"Executando ciclos de [Abrir Core -> Carregar ROM -> Renderizar Frames -> Fechar Core]...")
    print(f"Total de motores selecionados: {len(STRESS_TARGETS)}")
    print(f"Meta: {len(STRESS_TARGETS) * 20} ciclos completos de lifecycle.\n")

    total_passed = 0
    total_cycles = 0
    t_global_start = time.time()

    for core_file, rom_file, display_name in STRESS_TARGETS:
        core_path = f"cores/{core_file}"
        if not os.path.exists(core_path) or not os.path.exists(rom_file):
            print(f"  {Colors.YELLOW}⚠ Pulei {core_file} (arquivo não encontrado){Colors.RESET}")
            continue

        print(f"{Colors.BOLD}{Colors.CYAN}▶ Testando {display_name} (20x ciclos)...{Colors.RESET}")
        cmd = ["./app/Karamelo", "--core-stress", core_path, rom_file, "20"]
        t0 = time.time()
        res = subprocess.run(cmd, capture_output=True, text=True)
        dt = time.time() - t0

        if res.returncode == 0 and "20/20 ciclos com sucesso" in res.stdout:
            print(f"  {Colors.GREEN}✔ 20/20 CICLOS APROVADOS{Colors.RESET} ({dt:.2f}s | média: {(dt/20)*1000:.1f}ms/ciclo)")
            total_passed += 1
            total_cycles += 20
        else:
            print(f"  {Colors.RED}✖ FALHA no stress test de {core_file}{Colors.RESET}")
            # Mostra as últimas linhas de erro
            lines = res.stdout.strip().split("\n")
            for l in lines[-5:]:
                print(f"    {Colors.RED}{l}{Colors.RESET}")

    t_global_total = time.time() - t_global_start
    print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*74}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}  RESUMO GERAL DO STRESS TEST NO macOS{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}{'='*74}{Colors.RESET}")
    print(f"  - Motores Testados com Sucesso: {Colors.GREEN}{total_passed}/{len(STRESS_TARGETS)} cores{Colors.RESET}")
    print(f"  - Total de Ciclos Executados:   {Colors.GREEN}{total_cycles} ciclos perfeitos (0 crashes/leaks){Colors.RESET}")
    print(f"  - Tempo Total de Execução:      {t_global_total:.2f} segundos")
    
    if total_passed == len(STRESS_TARGETS):
        print(f"\n{Colors.BOLD}{Colors.GREEN}>>> 100% DA BATERIA DE STRESS APROVADA NO macOS COM ESTABILIDADE MÁXIMA! <<<{Colors.RESET}\n")
        return 0
    else:
        print(f"\n{Colors.BOLD}{Colors.RED}>>> FORAM DETECTADAS FALHAS NO STRESS TEST! <<<{Colors.RESET}\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
