#!/usr/bin/env python3
"""Summarise the newest macOS .ips crash report for GhostBand.

Why this exists: an .ips file is two JSON documents concatenated, and `head` on one
produces unreadable soup. The three questions that decide what a crash *is* -- what
signal, how deep the stack, and which frames repeat -- are all answerable from it in
about twenty lines, and answering them wrongly costs a round trip each time.

A stack overflow from runaway recursion and a null dereference both surface as
EXC_BAD_ACCESS. The frame count is what separates them: a few dozen frames is a
pointer bug, a few thousand is recursion.

Usage:  python3 scripts/crashlog.py [path-to-.ips]
"""
import glob
import json
import os
import sys
from collections import Counter


def newest_report():
    pattern = os.path.expanduser("~/Library/Logs/DiagnosticReports/GhostBand*.ips")
    reports = sorted(glob.glob(pattern), key=os.path.getmtime, reverse=True)
    return reports[0] if reports else None


def load(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    # Header JSON on line 1, payload JSON after it.
    first_newline = text.index("\n")
    return json.loads(text[first_newline:])


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else newest_report()
    if not path:
        print("No GhostBand crash reports found.")
        return 1

    print(f"report   {os.path.basename(path)}")
    data = load(path)

    exc = data.get("exception", {})
    print(f"signal   {exc.get('signal', '?')}   type {exc.get('type', '?')}"
          f"   subtype {exc.get('subtype', '')}")
    if data.get("termination"):
        print(f"reason   {data['termination'].get('indicator', '')}")
    if data.get("asi"):
        # JUCE assertions and abort() messages land here.
        print(f"message  {data['asi']}")

    faulting = data.get("faultingThread", 0)
    threads = data.get("threads", [])
    if faulting >= len(threads):
        print("Could not locate the faulting thread.")
        return 1

    frames = threads[faulting].get("frames", [])
    images = data.get("usedImages", [])

    def name(frame):
        symbol = frame.get("symbol")
        if symbol:
            return symbol
        index = frame.get("imageIndex", -1)
        if 0 <= index < len(images):
            return (images[index].get("name") or "?") + f" + {frame.get('imageOffset', 0)}"
        return "?"

    print(f"\nfaulting thread {faulting}: {len(frames)} frames")
    if len(frames) > 400:
        print(">>> STACK OVERFLOW: this many frames means runaway recursion, "
              "not a pointer bug.")
        repeated = Counter(name(f) for f in frames).most_common(6)
        print("\nmost repeated frames (the recursion cycle):")
        for symbol, count in repeated:
            print(f"  {count:6d}x  {symbol}")

    print("\ntop frames:")
    for frame in frames[:24]:
        print(f"  {name(frame)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
