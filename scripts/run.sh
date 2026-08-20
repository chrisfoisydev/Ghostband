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

# ctest usually lives beside cmake, but a PATH that has one need not have the other — on
# this project cmake was symlinked out of a venv and ctest was not, so `ctest` was simply
# missing while `cmake` worked fine.
find_ctest() {
    command -v ctest 2>/dev/null && return 0
    local cmake_bin
    cmake_bin="$(command -v cmake 2>/dev/null)" || return 1
    local resolved
    resolved="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$cmake_bin")"
    local candidate="$(dirname "$resolved")/ctest"
    [[ -x "$candidate" ]] && echo "$candidate"
}

if [[ "${1:-}" != "--no-build" ]]; then
    echo "==> building"
    cmake --build build -j

    CTEST="$(find_ctest || true)"
    if [[ -z "$CTEST" ]]; then
        # A missing test runner must not stop the launch — the point of this script is to
        # get the new binary in front of you, and being unable to *run* the tests is a
        # different thing from the tests failing.
        echo "==> core tests SKIPPED: ctest not found on PATH or beside cmake"
        echo "    to fix: sudo ln -s \"\$(dirname \"\$(python3 -c 'import os,shutil;print(os.path.realpath(shutil.which(\"cmake\")))')\")/ctest\" /usr/local/bin/ctest"
    else
        echo "==> core tests"
        # A test *failure* does stop here. That is the whole reason for running them.
        "$CTEST" --test-dir build --output-on-failure
    fi
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
