"""Read-only: disassemble functions of the saved runtime image (Logs/Test004/runtime-image.bin, laid out by RVA).
Usage: py Tools/disasm_image.py <rva> [<rva> ...] [--full]
For each RVA prints the enclosing function (from .pdata), referenced UTF-16/ASCII strings, and the
instructions around the RVA (or the whole function with --full)."""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMG = open(r"E:\Fortnite Battle Lab revival\Logs\Test004\runtime-image.bin", "rb").read()
pe = struct.unpack_from("<I", IMG, 0x3C)[0]
opt = pe + 24
exc_rva, exc_size = struct.unpack_from("<II", IMG, opt + 112 + 3 * 8)
funcs = [struct.unpack_from("<III", IMG, exc_rva + i * 12) for i in range(exc_size // 12) if exc_rva + i * 12 + 12 <= len(IMG)]
funcs.sort()


def find_func(rva):
    lo, hi = 0, len(funcs) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        b, e, _ = funcs[mid]
        if rva < b:
            hi = mid - 1
        elif rva >= e:
            lo = mid + 1
        else:
            return b, e
    # .pdata unavailable in this (truncated) dump: use the int3 padding between functions.
    b = rva
    while b > 0 and not (IMG[b - 1] == 0xCC and IMG[b - 2] == 0xCC):
        b -= 1
    e = rva
    while e < len(IMG) - 2 and not (IMG[e] == 0xCC and IMG[e + 1] == 0xCC):
        e += 1
    return b, e


def string_at(rva):
    if rva <= 0 or rva + 4 >= len(IMG):
        return None
    raw = IMG[rva:rva + 400]
    if len(raw) > 3 and raw[1] == 0 and raw[3] == 0:
        s = raw.decode("utf-16le", "ignore").split("\x00")[0]
    else:
        s = raw.split(b"\x00")[0].decode("latin1")
    return s if len(s) >= 5 and all(ch.isprintable() for ch in s) else None


md = Cs(CS_ARCH_X86, CS_MODE_64)
full = "--full" in sys.argv
for arg in [a for a in sys.argv[1:] if not a.startswith("--")]:
    rva = int(arg, 16)
    fn = find_func(rva)
    if not fn:
        print(f"== {rva:x}: no .pdata entry")
        continue
    # Walk back over chained unwind entries to the primary function start.
    b, e = fn
    print(f"== {rva:x} in function {b:x}-{e:x} (+0x{rva - b:x})")
    insns = list(md.disasm(IMG[b:e], b))
    if not full:
        # Resynchronise: pick a start shortly before rva whose decoding lands exactly on rva.
        for back in range(0x80, 0x10, -1):
            cand = list(md.disasm(IMG[rva - back:rva + 0x60], rva - back))
            if any(i.address == rva for i in cand):
                insns = [i for i in insns if i.address < rva - back] + cand
                break
    for i in insns:
        tgt = None
        if "[rip " in i.op_str:
            d = int(i.op_str.split("[rip ")[1].split("]")[0].replace(" ", ""), 16)
            tgt = i.address + i.size + d
        s = string_at(tgt) if tgt else None
        near = abs(i.address - rva) <= 0x60
        if full or near or s:
            mark = ">>" if i.address <= rva < i.address + i.size or i.address == rva else "  "
            print(f"{mark}{i.address:x} {i.mnemonic} {i.op_str}" + (f"   ; {s!r}" if s else ""))

