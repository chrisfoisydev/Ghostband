#!/usr/bin/env python3
"""Trim magenta-realtime's root CMakeLists.txt down to core + hello_mrt2.

Phase 0 of Follow needs exactly two things from upstream: the magentart::core
inference library, and the hello_mrt2 CLI used as a sanity check. Upstream's root
CMakeLists.txt additionally configures the AUv3 plugin, the standalone app, the jam
and collider demos, Max/PD/SuperCollider externals, and three npm/React UI builds.

Configuring those costs disk (a SuperCollider fetch plus a full TensorFlow clone) and
build time, for code we never link against.

This script comments out only those lines. It is reversible: a .bak file is written,
and `git checkout CMakeLists.txt` inside the upstream repo restores the original.

Usage:
    python3 trim-mrt2.py ~/magenta-realtime
    python3 trim-mrt2.py ~/magenta-realtime --restore
"""

import sys
import shutil
from pathlib import Path

# Everything Follow does NOT need. `core` and `examples/hello_mrt2` are absent by design.
DROP_SUBDIRS = [
    "examples/mrt2/auv3",
    "examples/mrt2/standalone",
    "examples/jam",
    "examples/collider",
    "examples/max",
    "examples/pd",
    "examples/sc",
]

# The React UI targets. `build_mrt2_ui` is declared ALL, so it runs on a plain
# `cmake --build` even when a single target was requested.
DROP_PREFIXES = [
    "add_custom_target(npm_install_root",
    "add_custom_target(build_mrt2_ui",
    "add_dependencies(build_mrt2_ui",
]

MARKER = "# [follow-trim]"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    repo = Path(sys.argv[1]).expanduser()
    cmakelists = repo / "CMakeLists.txt"
    backup = repo / "CMakeLists.txt.follow-bak"

    if not cmakelists.is_file():
        print(f"error: {cmakelists} not found — is that a magenta-realtime checkout?")
        return 1

    if "--restore" in sys.argv:
        if not backup.is_file():
            print(f"error: no backup at {backup}")
            return 1
        shutil.copy2(backup, cmakelists)
        print(f"restored {cmakelists} from backup")
        return 0

    text = cmakelists.read_text()
    if MARKER in text:
        print("already trimmed — nothing to do")
        return 0

    shutil.copy2(cmakelists, backup)

    out, dropped = [], []
    # A multi-line add_custom_target continues until its closing paren; track depth so
    # we comment the whole block, not just its first line.
    depth = 0

    for line in text.splitlines():
        stripped = line.strip()

        if depth > 0:
            out.append(f"{MARKER} {line}")
            depth += line.count("(") - line.count(")")
            continue

        if any(stripped.startswith(p) for p in DROP_PREFIXES):
            out.append(f"{MARKER} {line}")
            dropped.append(stripped.split("(")[0] + "(...)")
            depth = line.count("(") - line.count(")")
            continue

        if stripped.startswith("add_subdirectory(") and any(
            f"add_subdirectory({d})" == stripped for d in DROP_SUBDIRS
        ):
            out.append(f"{MARKER} {line}")
            dropped.append(stripped)
            continue

        out.append(line)

    cmakelists.write_text("\n".join(out) + "\n")

    print(f"backup written to {backup}\ncommented out {len(dropped)} entries:")
    for d in dropped:
        print(f"  - {d}")
    print("\nkept: core, examples/hello_mrt2")
    print("restore with: python3 trim-mrt2.py <repo> --restore")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
