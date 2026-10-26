#!/bin/bash
# stress_cores_linux.sh - a mesma bateria do tools/stress_cores.ps1, no Linux.
#
# Carrega, desliga e carrega de novo cada core (.so) com uma ROM real, pelo
# autoteste headless do proprio app (--core-selftest), e classifica pelo codigo
# de saida: 0 = ok, 1 = o core recusou, outro = o processo morreu (o caso que
# nunca deveria acontecer), e passar do tempo = travou.
#
# Roda no Linux ou no WSL (WSLg da a janela e o GPU), a partir de app/:
#   ./tools/stress_cores_linux.sh              # todos, 1 ciclo
#   CYCLES=3 ./tools/stress_cores_linux.sh     # 3 ciclos
#   ./tools/stress_cores_linux.sh PSP          # so um sistema
#   INPUT_PROBE=0 ./tools/stress_cores_linux.sh   # sem apertar botoes
#
# Por padrao tambem aperta cada botao do controle e as teclas do layout com o
# jogo aberto (--input-probe) e reprova se o toque chegou ao core como outra
# coisa alem do jogador 1 - outra porta, clique de mouse, pistola. Saida 5.
#
# O app procura "cores/<nome>.dll" e resolve para <nome>.so no Linux - por isso o
# mapa abaixo usa os mesmos nomes do stress_cores.ps1.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/app"
EXE="$APP/Karamelo"
CYCLES="${CYCLES:-1}"
TIMEOUT="${TIMEOUT:-90}"
ONLY="${1:-}"
INPUT_PROBE="${INPUT_PROBE:-1}"
PROBE_ARG=""
[ "$INPUT_PROBE" = "1" ] && PROBE_ARG="--input-probe"

[ -x "$EXE" ] || { echo "app/Karamelo nao encontrado - rode ./compile_linux.sh antes."; exit 2; }

MAP=(
    "Arcade-FBNeo:arcade_fbneo:Arcade"   "Arcade-MAME03:mame2003:Arcade"   "Arcade-MAME10:mame2010:Arcade"
    "NES:nes:NES"                        "SNES:snes:SNES"                  "Genesis:genesis:Genesis"
    "MasterSystem:sms:MasterSystem"      "MegaCD:genesis:MegaCD"           "32X:32x:32X"
    "GameBoy:gb:GameBoy"                 "GBA:gba:GBA"                     "NDS:nds:NDS"
    "N64-Parallel:n64_parallel:Nintendo64" "N64-Mupen:n64_mupen:Nintendo64" "N64-Generic:n64:Nintendo64"
    "PlayStation:psx:PlayStation"        "Saturn:saturn:Saturn"            "Dreamcast:dreamcast:Dreamcast"
    "TurboGrafx16:pce:TurboGrafx16"      "NeoGeo:neogeo:NeoGeo"            "NeoGeo-CD:neocd_alt:NeoGeo"
    "MSX:msx:MSX"                        "Amiga:amiga:Amiga"               "C64:c64:C64"
    "ZXSpectrum:spectrum:ZXSpectrum"     "Atari2600:atari2600:Atari2600"   "Atari5200:atari5200:Atari5200"
    "Atari7800:atari7800:Atari7800"      "Lynx:lynx:Lynx"                  "Jaguar:jaguar:Jaguar"
    "ColecoVision:coleco:ColecoVision"   "WonderSwan:wswan:WonderSwan"     "NGP:ngp:NGP"
    "PCFX:pcfx:PCFX"                     "3DO:3do:3DO"                     "PlayStation2:ps2:PlayStation2"
    "PS2-Play:ps2_play:PlayStation2"     "PSP:psp:PSP"                     "GameCube:gamecube:GameCube"
    "3DS:3ds:3DS"                        "DOS:dosbox_pure:DOS"             "PC98:pc98:PC98"
    "FMTowns:fmtowns:FMTowns"
)
# ROMs sinteticas (tools/generate_test_roms.py) para quem nao tem ROM em roms/.
declare -A TESTROM=( [Atari5200]=atari5200.a52 [Lynx]=lynx.lnx [WonderSwan]=wswan.ws [NGP]=ngp.ngp [PCFX]=pcfx.cue [3DO]=3do.iso )

cd "$APP" || exit 2
ok=0; fail=0; crash=0; leak=0; untested=0; skipped=()
failed=(); crashed=(); leaked=(); not_tested=()
for entry in "${MAP[@]}"; do
    IFS=: read -r sys core dir <<< "$entry"
    [ -n "$ONLY" ] && [ "$sys" != "$ONLY" ] && continue
    if [ ! -f "cores/$core.so" ]; then skipped+=("$sys (sem cores/$core.so)"); continue; fi
    rom=$(find "roms/$dir" -maxdepth 1 -type f ! -iname '*.txt' ! -iname '*.nfo' ! -iname '*.jpg' ! -iname '*.png' \
          ! -iname '*.sav' ! -iname '*.srm' ! -iname '*.state' ! -iname '*.bak' ! -iname '*.log' ! -iname '*.ini' \
          ! -iname '*.cfg' ! -iname '*.xml' ! -iname '*.dat' -name '*.*' 2>/dev/null | LC_ALL=C sort | head -1)
    if [ -z "$rom" ] && [ -n "${TESTROM[$sys]:-}" ] && [ -f "$ROOT/test_roms/${TESTROM[$sys]}" ]; then
        rom="../test_roms/${TESTROM[$sys]}"
    fi
    if [ -z "$rom" ]; then skipped+=("$sys (sem ROM em roms/$dir)"); continue; fi

    printf '  %-14s[' "$sys"
    worst=PASS; start=$SECONDS
    for ((i = 1; i <= CYCLES; i++)); do
        timeout -k 10 "$TIMEOUT" "$EXE" --core-selftest "cores/$core.dll" "$rom" "cores/$core.dll" "$rom" $PROBE_ARG >/dev/null 2>&1
        rc=$?
        case $rc in
            0) printf '.' ;;
            1) printf 'F'; [ "$worst" = PASS ] && worst=FAIL ;;
            5) printf 'I'; { [ "$worst" = PASS ] || [ "$worst" = FAIL ]; } && worst=INPUT ;;
            6) printf '?'; [ "$worst" = PASS ] && worst=UNTESTED ;;
            124) printf 'T'; worst=TIMEOUT ;;
            *) printf 'X'; worst="CRASH(rc=$rc)" ;;
        esac
    done
    printf ']  %s  (%ds)\n' "$worst" $((SECONDS - start))
    case $worst in
        PASS) ok=$((ok + 1)) ;;
        FAIL) fail=$((fail + 1)); failed+=("$sys $rom") ;;
        INPUT) leak=$((leak + 1)); leaked+=("$sys $rom") ;;
        UNTESTED) untested=$((untested + 1)); not_tested+=("$sys $rom") ;;
        *) crash=$((crash + 1)); crashed+=("$sys $worst $rom") ;;
    esac
done

echo "  . = ciclo ok    F = core recusou    I = toque chegou como outra coisa    ? = toques nao testados    X = processo morreu    T = travou"
echo "  $ok sistemas ok, $fail com recusa, $crash derrubaram ou travaram o processo, $leak com toque vazando, $untested sem os toques testados"
for u in "${not_tested[@]}"; do echo "    TOQUES NAO TESTADOS: $u (o core nao leu a entrada)"; done
for l in "${leaked[@]}"; do echo "    TOQUE VAZOU: $l (detalhe: 'INPUT-PROBE vazou' em app/karamelo.log)"; done
for c in "${crashed[@]}"; do echo "    MORREU/TRAVOU: $c"; done
for f in "${failed[@]}"; do echo "    RECUSOU: $f"; done
if [ ${#skipped[@]} -gt 0 ]; then
    echo "  NAO testados (${#skipped[@]}):"
    for s in "${skipped[@]}"; do echo "    $s"; done
fi
[ $crash -eq 0 ] && [ $fail -eq 0 ] && [ $leak -eq 0 ]
