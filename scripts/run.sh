#!/usr/bin/env bash
# Build GhostBand and launch the binary that was just built.
#
# WHY THIS EXISTS
# ---------------
# `moreThanOneInstanceAllowed()` returns false, so `open GhostBand.app` with an instance
# already running silently *focuses the old one* rather than launching the new build. It
# looks exactly like a build that did nothing, and it cost several rounds of testing
# against a stale binary before anyone noticed. See KNOWN_ISSUES.md §20.
#
# Usage:  ./scripts/run.sh [--no-build]

set -euo pipefail
cd "$(dirname "$0")/.."

APP="build/src/app/GhostBand_artefacts/RelWithDebInfo/GhostBand.app"
BIN="$APP/Contents/MacOS/GhostBand"

if [[ "${1:-}" != "--no-build" ]]; then
    echo "==> building"
    cmake --build build -j
    echo "==> core tests"
    ctest --test-dir build --output-on-failure
fi

if [[ ! -x "$BIN" ]]; then
    echo "No binary at $BIN" >&2
    echo "Configure first:  cmake -B build -DGHOSTBAND_BUILD_APP=ON" >&2
    exit 1
fi

# Quit first, always. This is the whole point of the script.
if pgrep -x GhostBand > /dev/null; then
    echo "==> quitting the running instance"
    pkill -x GhostBand || true
    # Give it a moment to release the audio device; a new instance that opens CoreAudio
    # while the old one still holds it comes up with no output and no obvious reason.
    for _ in $(seq 1 20); do
        pgrep -x GhostBand > /dev/null || break
        sleep 0.25
    done
fi

echo "==> launching $(date '+%H:%M:%S')  (built $(date -r "$BIN" '+%H:%M:%S'))"
open "$APP"

LOG_DIR="$HOME/Library/Application Support/GhostBand/logs"
echo
echo "Log:  tail -f \"\$(ls -t '$LOG_DIR'/ghostband-*.log | head -1)\""
