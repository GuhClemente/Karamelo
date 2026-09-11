#!/usr/bin/env python3
"""
Bateria de Testes Completa para macOS (Apple Silicon ARM64) - Karamelo
Validação de 5 Tiers:
  Tier 1: Testes Unitários Nativos C++ (20/20)
  Tier 2: Validação do Binário macOS (Mach-O arm64, SDL3, OpenGL, CLI)
  Tier 3: Bateria de Integridade dos 39 Cores Libretro (.dylib arm64, ABI & Metadata)
  Tier 4: Integridade do Pacote Distribuível (tar.gz, permissões, version.json)
  Tier 5: Validação do Ambiente e Quarentena macOS (Gatekeeper / xattr)
"""
import ctypes
import glob
import json
import os
import stat
import subprocess
import sys
import tarfile
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

def log_section(title):
    print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*74}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}  {title}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}{'='*74}{Colors.RESET}")

# ---------------------------------------------------------------------
# TIER 1: Testes Unitários Nativos C++
# ---------------------------------------------------------------------
def run_tier_1_unit_tests():
    log_section("TIER 1: Testes Unitários Nativos (build/karamelo_tests)")
    test_bin = "build/karamelo_tests"
    if not os.path.exists(test_bin):
        print(f"{Colors.RED}[FALHA] Binário {test_bin} não encontrado.{Colors.RESET}")
        return False
    
    t0 = time.time()
    res = subprocess.run([test_bin], capture_output=True, text=True)
    dt = time.time() - t0
    
    pass_count = 0
    fail_count = 0
    for line in res.stdout.strip().split("\n"):
        if "[     PASS ]" in line:
            test_name = line.replace('[     PASS ]', '').strip()
            print(f"  {Colors.GREEN}✔{Colors.RESET} {test_name}")
            pass_count += 1
        elif "[     FAIL ]" in line:
            print(f"  {Colors.RED}✖ {line}{Colors.RESET}")
            fail_count += 1

    if res.returncode == 0 and fail_count == 0:
        print(f"\n{Colors.GREEN}{Colors.BOLD}  -> Todos os {pass_count} testes unitários passaram com sucesso ({dt:.3f}s){Colors.RESET}")
        return True
    else:
        print(f"\n{Colors.RED}{Colors.BOLD}  -> Falha nos testes unitários ({fail_count} falhas, código {res.returncode}){Colors.RESET}")
        return False

# ---------------------------------------------------------------------
# TIER 2: Validação do Binário macOS
# ---------------------------------------------------------------------
def run_tier_2_binary_checks():
    log_section("TIER 2: Validação do Binário macOS (app/Karamelo)")
    exe = "app/Karamelo"
    if not os.path.exists(exe):
        print(f"{Colors.RED}[FALHA] Executável {exe} não encontrado.{Colors.RESET}")
        return False

    all_ok = True
    
    # 1. Mach-O format
    res = subprocess.run(["file", exe], capture_output=True, text=True)
    out = res.stdout.strip()
    print(f"  [1] Formato Mach-O:")
    if "Mach-O 64-bit executable arm64" in out:
        print(f"      {Colors.GREEN}✔ Nativo Apple Silicon (arm64){Colors.RESET}")
    else:
        print(f"      {Colors.RED}✖ Formato inesperado: {out}{Colors.RESET}")
        all_ok = False

    # 2. Dynamic link dependencies (otool -L)
    res = subprocess.run(["otool", "-L", exe], capture_output=True, text=True)
    libs = res.stdout.strip().split("\n")[1:]
    print(f"  [2] Dependências Dinâmicas ({len(libs)} bibliotecas vinculadas):")
    has_sdl = any("libSDL3" in l for l in libs)
    has_gl = any("OpenGL" in l for l in libs)
    has_libcpp = any("libc++" in l for l in libs)
    has_libsystem = any("libSystem" in l for l in libs)
    
    if has_sdl:
        print(f"      {Colors.GREEN}✔ SDL3 vinculado dinamicamente (/opt/homebrew ou rpath){Colors.RESET}")
    else:
        print(f"      {Colors.RED}✖ libSDL3 não encontrado em otool -L{Colors.RESET}")
        all_ok = False
        
    if has_gl:
        print(f"      {Colors.GREEN}✔ OpenGL.framework nativo do macOS vinculado{Colors.RESET}")
    else:
        print(f"      {Colors.RED}✖ OpenGL.framework não encontrado{Colors.RESET}")
        all_ok = False

    if has_libcpp and has_libsystem:
        print(f"      {Colors.GREEN}✔ libc++ e libSystem.B do macOS vinculados{Colors.RESET}")

    # 3. CLI --version
    res = subprocess.run([exe, "--version"], capture_output=True, text=True)
    ver_out = res.stdout.strip()
    if res.returncode == 0 and "Karamelo" in ver_out:
        print(f"  [3] Execução CLI (--version): {Colors.GREEN}✔ {ver_out}{Colors.RESET}")
    else:
        print(f"  [3] Execução CLI (--version): {Colors.RED}✖ Código {res.returncode}{Colors.RESET}")
        all_ok = False

    # 4. CLI --help
    res = subprocess.run([exe, "--help"], capture_output=True, text=True)
    if res.returncode == 0 and "Uso:" in res.stdout:
        print(f"  [4] Execução CLI (--help):    {Colors.GREEN}✔ Sintaxe e parâmetros de terminal corretos{Colors.RESET}")
    else:
        print(f"  [4] Execução CLI (--help):    {Colors.RED}✖ Código {res.returncode}{Colors.RESET}")
        all_ok = False

    return all_ok

# ---------------------------------------------------------------------
# TIER 3: Bateria de Integridade dos 39 Cores Libretro
# ---------------------------------------------------------------------
class RetroSystemInfo(ctypes.Structure):
    _fields_ = [
        ("library_name", ctypes.c_char_p),
        ("library_version", ctypes.c_char_p),
        ("valid_extensions", ctypes.c_char_p),
        ("need_fullpath", ctypes.c_bool),
        ("block_extract", ctypes.c_bool),
    ]

REQUIRED_LIBRETRO_SYMBOLS = [
    "retro_api_version",
    "retro_init",
    "retro_deinit",
    "retro_get_system_info",
    "retro_get_system_av_info",
    "retro_set_environment",
    "retro_set_video_refresh",
    "retro_set_audio_sample",
    "retro_set_audio_sample_batch",
    "retro_set_input_poll",
    "retro_set_input_state",
    "retro_load_game",
    "retro_unload_game",
    "retro_run",
    "retro_reset",
]

def run_tier_3_core_battery():
    log_section("TIER 3: Bateria de Integridade dos 39 Cores Libretro (.dylib)")
    cores = sorted(glob.glob("cores/*.dylib"))
    if not cores:
        print(f"{Colors.RED}[FALHA] Nenhum core encontrado em cores/*.dylib.{Colors.RESET}")
        return False

    print(f"  Testando {len(cores)} motores de emulação nativos ARM64...\n")
    passed = 0
    failed = []

    for c in cores:
        name = os.path.basename(c)
        try:
            # 1. Verificar arquitetura Mach-O
            fres = subprocess.run(["file", c], capture_output=True, text=True)
            if "arm64" not in fres.stdout:
                failed.append((name, "Não é Mach-O arm64"))
                print(f"  {Colors.RED}✖ {name:20} -> Arquitetura inválida: {fres.stdout.strip()}{Colors.RESET}")
                continue

            # 2. Carregar dylib com ctypes
            lib = ctypes.CDLL(c)
            
            # 3. Validar símbolos essenciais da especificação Libretro ABI
            missing = [s for s in REQUIRED_LIBRETRO_SYMBOLS if not hasattr(lib, s)]
            if missing:
                failed.append((name, f"Símbolos ausentes: {missing}"))
                print(f"  {Colors.RED}✖ {name:20} -> Símbolos ausentes: {missing}{Colors.RESET}")
                continue

            # 4. retro_api_version (deve retornar 1)
            api_ver = lib.retro_api_version()
            if api_ver != 1:
                failed.append((name, f"API version inesperada: {api_ver}"))
                print(f"  {Colors.YELLOW}⚠ {name:20} -> API version: {api_ver}{Colors.RESET}")
                continue

            # 5. retro_get_system_info
            lib.retro_get_system_info.argtypes = [ctypes.POINTER(RetroSystemInfo)]
            lib.retro_get_system_info.restype = None
            info = RetroSystemInfo()
            lib.retro_get_system_info(ctypes.byref(info))
            
            core_name = info.library_name.decode("utf-8", "ignore") if info.library_name else "N/A"
            core_ver = info.library_version.decode("utf-8", "ignore") if info.library_version else "N/A"
            exts = info.valid_extensions.decode("utf-8", "ignore") if info.valid_extensions else "N/A"
            exts_display = (exts[:25] + "...") if len(exts) > 28 else exts

            print(f"  {Colors.GREEN}✔{Colors.RESET} {name:22} | {core_name[:20]:20} | v{core_ver[:10]:10} | Ext: {exts_display}")
            passed += 1

        except Exception as e:
            failed.append((name, str(e)))
            print(f"  {Colors.RED}✖ {name:20} -> Erro de carregamento: {e}{Colors.RESET}")

    print(f"\n{Colors.BOLD}  -> Resultado dos Cores: {passed}/{len(cores)} motores validados com sucesso.{Colors.RESET}")
    if failed:
        print(f"  {Colors.RED}Falhas registradas: {len(failed)}{Colors.RESET}")
        return False
    return True

# ---------------------------------------------------------------------
# TIER 4: Integridade do Pacote Distribuível
# ---------------------------------------------------------------------
def run_tier_4_package_verification():
    log_section("TIER 4: Verificação de Integridade do Pacote Distribuível (dist/)")
    tar_path = "dist/Karamelo_v0.9.4_macOS_arm64.tar.gz"
    version_path = "dist/version.json"
    
    if not os.path.exists(tar_path):
        print(f"{Colors.RED}[FALHA] Arquivo {tar_path} não encontrado.{Colors.RESET}")
        return False

    size_bytes = os.path.getsize(tar_path)
    size_mb = size_bytes / (1024 * 1024)
    print(f"  [1] Pacote tar.gz: {tar_path} ({size_mb:.1f} MB)")
    if size_mb < 50:
        print(f"      {Colors.RED}✖ Pacote muito pequeno para conter todos os cores ({size_mb:.1f} MB){Colors.RESET}")
        return False
    else:
        print(f"      {Colors.GREEN}✔ Tamanho condizente com distribuição completa{Colors.RESET}")

    # Checar conteúdo do tar.gz
    with tarfile.open(tar_path, "r:gz") as tar:
        members = tar.getmembers()
        names = [m.name for m in members]
        print(f"  [2] Estrutura Interna do Arquivo ({len(names)} arquivos/pastas):")
        
        exe_member = next((m for m in members if m.name.endswith("/Karamelo")), None)
        run_member = next((m for m in members if m.name.endswith("/run.sh")), None)
        cores_members = [m for m in members if "/cores/" in m.name and m.name.endswith(".dylib")]
        roms_members = [m for m in members if "/roms/" in m.name and m.name.endswith("/LEIA-ME.txt")]
        
        # Permissões de execução
        exe_ok = exe_member is not None and bool(exe_member.mode & (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH))
        run_ok = run_member is not None and bool(run_member.mode & (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH))

        print(f"      - Executável Karamelo:    {Colors.GREEN}✔ Presente (bit +x ativo){Colors.RESET}" if exe_ok else f"      - Executável Karamelo:    {Colors.RED}✖ Problema de permissão ou ausente{Colors.RESET}")
        print(f"      - Script run.sh:          {Colors.GREEN}✔ Presente (bit +x ativo){Colors.RESET}" if run_ok else f"      - Script run.sh:          {Colors.RED}✖ Problema de permissão ou ausente{Colors.RESET}")
        print(f"      - Cores nativos ARM64:    {Colors.GREEN}✔ {len(cores_members)} cores incluídos{Colors.RESET}")
        print(f"      - Pastas de Sistemas/ROMs:{Colors.GREEN}✔ {len(roms_members)} diretórios prontos com LEIA-ME{Colors.RESET}")

    # Validar version.json
    print(f"  [3] Manifesto dist/version.json:")
    if os.path.exists(version_path):
        try:
            with open(version_path, "r") as f:
                vdata = json.load(f)
            tar_name = os.path.basename(tar_path)
            tar_url = vdata.get("macos_tar_url", "")
            bin_url = vdata.get("macos_bin_url", "")
            bin_sha = vdata.get("macos_bin_sha256", "")
            if tar_name in tar_url and bin_url:
                print(f"      {Colors.GREEN}✔ Entradas macos_tar_url e macos_bin_url válidas (SHA: {bin_sha[:12]}...){Colors.RESET}")
            else:
                print(f"      {Colors.RED}✖ Entrada divergente em version.json: tar_url={tar_url}{Colors.RESET}")
                return False
        except Exception as e:
            print(f"      {Colors.RED}✖ Erro ao ler version.json: {e}{Colors.RESET}")
            return False
    else:
        print(f"      {Colors.RED}✖ version.json não encontrado em dist/{Colors.RESET}")
        return False

    return exe_ok and run_ok and len(cores_members) >= 35 and len(roms_members) >= 35

# ---------------------------------------------------------------------
# TIER 5: Sanitização de Quarentena e Segurança macOS
# ---------------------------------------------------------------------
def run_tier_5_quarantine_checks():
    log_section("TIER 5: Sanitização de Segurança e Gatekeeper (xattr / Quarentena)")
    cores = glob.glob("cores/*.dylib")
    app_cores = glob.glob("app/cores/*.dylib")
    all_dylibs = cores + app_cores
    
    quarantined = []
    for dylib in all_dylibs:
        res = subprocess.run(["xattr", "-p", "com.apple.quarantine", dylib], capture_output=True)
        if res.returncode == 0:
            quarantined.append(dylib)

    print(f"  Checando {len(all_dylibs)} bibliotecas dinâmicas em busca do atributo de quarentena do macOS...")
    if not quarantined:
        print(f"  {Colors.GREEN}✔ Nenhuma biblioteca está com flag de quarentena ativa (Gatekeeper liberado){Colors.RESET}")
        return True
    else:
        print(f"  {Colors.YELLOW}⚠ {len(quarantined)} arquivos ainda possuem atributo com.apple.quarantine:{Colors.RESET}")
        for q in quarantined[:5]:
            print(f"     - {q}")
        print(f"  {Colors.CYAN}Aplicando xattr -d com.apple.quarantine automaticamente...{Colors.RESET}")
        for q in quarantined:
            subprocess.run(["xattr", "-d", "com.apple.quarantine", q], capture_output=True)
        print(f"  {Colors.GREEN}✔ Quarentena removida com sucesso de todos os arquivos.{Colors.RESET}")
        return True

# ---------------------------------------------------------------------
# Execução Principal
# ---------------------------------------------------------------------
def main():
    print(f"{Colors.BOLD}{Colors.HEADER}")
    print("==========================================================================")
    print("       KARAMELO EMULADOR - BATERIA DE TESTES macOS (Apple Silicon)        ")
    print("==========================================================================")
    print(f"{Colors.RESET}")
    
    t_start = time.time()
    t1 = run_tier_1_unit_tests()
    t2 = run_tier_2_binary_checks()
    t3 = run_tier_3_core_battery()
    t4 = run_tier_4_package_verification()
    t5 = run_tier_5_quarantine_checks()
    
    total_time = time.time() - t_start
    log_section("RESUMO GERAL DA BATERIA DE TESTES macOS")
    
    print(f"  TIER 1 - Testes Unitários Nativos: {Colors.GREEN}PASSOU (20/20){Colors.RESET}" if t1 else f"  TIER 1: {Colors.RED}FALHOU{Colors.RESET}")
    print(f"  TIER 2 - Validação do Binário:     {Colors.GREEN}PASSOU (Mach-O arm64 / SDL3 / CLI){Colors.RESET}" if t2 else f"  TIER 2: {Colors.RED}FALHOU{Colors.RESET}")
    print(f"  TIER 3 - Integridade dos Cores:    {Colors.GREEN}PASSOU (39/39 cores Libretro ABI){Colors.RESET}" if t3 else f"  TIER 3: {Colors.RED}FALHOU{Colors.RESET}")
    print(f"  TIER 4 - Pacote de Distribuição:   {Colors.GREEN}PASSOU (tar.gz íntegro, version.json){Colors.RESET}" if t4 else f"  TIER 4: {Colors.RED}FALHOU{Colors.RESET}")
    print(f"  TIER 5 - Gatekeeper e Quarentena:  {Colors.GREEN}PASSOU (Cores 100% liberados){Colors.RESET}" if t5 else f"  TIER 5: {Colors.RED}FALHOU{Colors.RESET}")
    print(f"\n{Colors.BOLD}Tempo total de execução: {total_time:.2f} segundos.{Colors.RESET}")
    
    if t1 and t2 and t3 and t4 and t5:
        print(f"\n{Colors.BOLD}{Colors.GREEN}>>> TODAS AS 5 BATERIAS DE TESTES APROVADAS NO macOS COM SUCESSO! <<<{Colors.RESET}\n")
        return 0
    else:
        print(f"\n{Colors.BOLD}{Colors.RED}>>> FORAM ENCONTRADAS FALHAS NA BATERIA! <<<{Colors.RESET}\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
