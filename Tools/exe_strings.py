"""Grep reflected names / literals (ASCII + UTF-16LE) out of the read-only client exe.

Usage: py exe_strings.py <regex> [<regex> ...]
Prints unique matching strings, sorted. Case-insensitive.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "Fortnite-12.50-Source", "FortniteGame", "Binaries", "Win64",
                   "FortniteClient-Win64-Shipping.exe")

data = open(EXE, "rb").read()
ascii_strs = re.findall(rb"[\x20-\x7e]{4,}", data)
wide_strs = [m.decode("utf-16-le") for m in re.findall(rb"(?:[\x20-\x7e]\x00){4,}", data)]
pool = {s.decode("ascii") for s in ascii_strs} | set(wide_strs)

for pat in sys.argv[1:]:
    rx = re.compile(pat, re.I)
    hits = sorted(s for s in pool if rx.search(s))
    print(f"=== /{pat}/  ({len(hits)} hits)")
    for h in hits[:400]:
        print("  " + h[:200])
