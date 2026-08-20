#!/usr/bin/env bash
# Answer one question: is the GhostBand on screen the code in this checkout?
#
# WHY THIS EXISTS
# ---------------
# "It looks exactly the same" has two completely different causes — the change did not
# land, or the change did not run — and they need opposite responses. Guessing between
# them has already cost several rounds. `moreThanOneInstanceAllowed()` is false, so `open`
# on a running instance focuses the old one silently (KNOWN_ISSUES.md §20), and a failed
# build leaves the previous binary in place and executable.
#
# This checks the binary itself rather than the build log, so it is true regardless of how
# the app was launched.

set -uo pipefail
cd "$(dirname "$0")/.."

BIN="build/src/app/GhostBand_artefacts/RelWithDebInfo/GhostBand.app/Contents/MacOS/GhostBand"

echo "checkout"
echo "  HEAD            $(git log --oneline -1)"
echo "  local changes   $(git status --porcelain | wc -l | tr -d ' ') file(s)"
echo "  upstream        $(git rev-list --count HEAD..@{u} 2>/dev/null || echo '?') commit(s) not pulled"

echo
echo "binary"
if [[ ! -x "$BIN" ]]; then
    echo "  MISSING         no binary at $BIN"
    echo "  fix             cmake -B build -DGHOSTBAND_BUILD_APP=ON && cmake --build build -j"
    exit 1
fi
echo "  built           $(date -r "$BIN" '+%Y-%m-%d %H:%M:%S')"
echo "  newest source   $(find src -name '*.cpp' -o -name '*.h' | xargs stat -f '%m %N' \
                            | sort -rn | head -1 \
                            | awk '{printf "%s  ", strftime("%Y-%m-%d %H:%M:%S", $1); print $2}')"

# A string that only exists in the current UI. Grep the binary rather than trusting
# timestamps: a relink can touch the file without the change being in it.
marker_count=$(strings "$BIN" 2>/dev/null | grep -c 'YOUR RIG')
if [[ "$marker_count" -gt 0 ]]; then
    echo "  contains        'YOUR RIG'  -> this binary has the current setup screen"
else
    echo "  contains        NOTHING     -> this binary predates the current setup screen"
    echo "  fix             cmake --build build -j   (and read the output for errors)"
fi

echo
echo "process"
if pgrep -x GhostBand > /dev/null; then
    pid=$(pgrep -x GhostBand | head -1)
    echo "  running         pid $pid"
    echo "  from            $(ps -o comm= -p "$pid")"
    echo "  started         $(ps -o lstart= -p "$pid")"
    echo
    echo "  If 'started' predates 'built', you are looking at the old build."
    echo "  fix             ./scripts/run.sh --no-build"
else
    echo "  running         no"
fi
