#!/bin/bash
# test_netplay_unix.sh - two real Karamelo processes in a Netplay match, on Linux or
# macOS. Same test as tools/test_netplay.ps1: each side presses its own buttons and
# they compare the game state every second. See the comment there.
#
#   ./tools/test_netplay_unix.sh [frames]
#
# A system without its core or a game in app/roms is skipped, not failed.
FRAMES="${1:-300}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/app"
EXT="so"
[ "$(uname)" = "Darwin" ] && EXT="dylib"
export SDL_AUDIO_DRIVER=dummy
export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-dummy}"
cd "$APP" || exit 2

PORT=55700
FAILED=0
TESTED=0
OUT="$(mktemp -d)"
run_one() {
    local name="$1" core="cores/$2.$EXT" dir="$3"; shift 3
    [ -f "$core" ] || { echo "  $name: skipped, no core"; return; }
    local rom=""
    for pat in "$@"; do
        rom="$(find "roms/$dir" -maxdepth 1 -type f -name "$pat" 2>/dev/null | sort | head -1)"
        [ -n "$rom" ] && break
    done
    [ -n "$rom" ] || { echo "  $name: skipped, no game in roms/$dir"; return; }

    PORT=$((PORT + 1))
    TESTED=$((TESTED + 1))
    ./Karamelo --netplay-selftest host "$core" "$rom" 127.0.0.1 "$PORT" "$FRAMES" "$OUT/h$PORT" &
    local host_pid=$!
    sleep 2
    ./Karamelo --netplay-selftest guest "$core" "$rom" 127.0.0.1 "$PORT" "$FRAMES" "$OUT/g$PORT" &
    local guest_pid=$!
    wait "$host_pid"; local hc=$?
    wait "$guest_pid"; local gc=$?
    if { [ "$hc" = 0 ] || [ "$hc" = 5 ]; } && { [ "$gc" = 0 ] || [ "$gc" = 5 ]; }; then
        echo "  $name: PASSED ($hc/$gc)"
    else
        echo "  $name: FAILED ($hc/$gc)"
        FAILED=$((FAILED + 1))
    fi
    cat "$OUT/h$PORT" "$OUT/g$PORT" 2>/dev/null | sed 's/^/      /'
}

run_one GameBoy gb GameBoy '*.gb' '*.gbc'
run_one GBA gba GBA '*.gba'
run_one NES nes NES '*.nes'
run_one SNES snes SNES '*.sfc' '*.smc'
run_one Genesis genesis Genesis '*.md' '*.gen'
run_one MasterSystem sms MasterSystem '*.sms'

# Chat, invite and voice over real sockets. Needs no game, so it also runs in CI.
SOCIAL_A="$OUT/a"; SOCIAL_B="$OUT/b"
mkdir -p "$SOCIAL_A" "$SOCIAL_B"
EXE="$APP/Karamelo"
(cd "$SOCIAL_A" && "$EXE" --social-selftest a 55737 "$OUT/sa" >/dev/null 2>&1) &
A_PID=$!
(cd "$SOCIAL_B" && "$EXE" --social-selftest b 55737 "$OUT/sb" >/dev/null 2>&1) &
B_PID=$!
wait "$A_PID"; AC=$?
wait "$B_PID"; BC=$?
if [ "$AC" = 0 ] && [ "$BC" = 0 ]; then echo "  chat and invite: PASSED"; else echo "  chat and invite: FAILED ($AC/$BC)"; FAILED=$((FAILED + 1)); fi
cat "$OUT/sa" "$OUT/sb" 2>/dev/null | sed 's/^/      /'
(cd "$SOCIAL_A" && "$EXE" --voice-selftest 55738 2 "$OUT/v" >/dev/null 2>&1)
VC=$?
if [ "$VC" = 0 ]; then echo "  voice: PASSED"; else echo "  voice: FAILED ($VC)"; FAILED=$((FAILED + 1)); fi
cat "$OUT/v" 2>/dev/null | sed 's/^/      /'

# The internet rooms: the real chat server and two Karamelo processes through it.
if command -v node >/dev/null 2>&1; then
    node --test "$ROOT/server/chat/server.test.mjs" >"$OUT/server.log" 2>&1
    SC=$?
    if [ "$SC" = 0 ]; then echo "  chat server: PASSED"; else echo "  chat server: FAILED"; cat "$OUT/server.log"; FAILED=$((FAILED + 1)); fi
    node "$ROOT/tools/test_chat_internet.mjs"
    IC=$?
    [ "$IC" = 0 ] || FAILED=$((FAILED + 1))
else
    echo "  WARNING: no node here, the internet rooms were NOT tested"
fi

rm -rf "$OUT"
echo "  $TESTED games tested, $FAILED failed"
[ "$FAILED" = 0 ]
