"""Read-only: locate engine 'check/ensure' sites by source file + line in the saved runtime image.
Usage: py Tools/find_check.py <file-substring> <line> [context_bytes]
Finds code that references the source-path string and loads the line number as an immediate,
then disassembles backwards from it (resynchronised) to show the guarded condition."""
import re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMG = open(r"E:\Fortnite Battle Lab revival\Logs\Test004\runtime-image.bin", "rb").read()
sub, line = sys.argv[1], int(sys.argv[2])
ctx = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x180
md = Cs(CS_ARCH_X86, CS_MODE_64)

# ASCII (ensure) and UTF-16 (check) path strings containing the substring.
targets = set()
for enc in ("ascii", "utf-16le"):
    for m in re.finditer(re.escape(sub.encode(enc)), IMG):
        s = m.start()
        step = 2 if enc == "utf-16le" else 1
        while s >= step and IMG[s - step] != 0:
            s -= step
        targets.add(s)
print("path strings:", [hex(t) for t in sorted(targets)])

imm = struct.pack("<I", line)
for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", IMG[:0x6000000]):
    at = m.start()
    if at + 7 + struct.unpack_from("<i", IMG, at + 3)[0] not in targets:
        continue
    window = IMG[at - 0x40:at + 0x40]
    if imm not in window:
        continue
    print(f"== path xref {at:x} (line {line} immediate nearby)")
    for back in range(ctx, ctx - 0x30, -1):
        ins = list(md.disasm(IMG[at - back:at + 0x30], at - back))
        if any(i.address == at for i in ins):
            for i in ins:
                extra = ""
                if "[rip " in i.op_str:
                    d = int(i.op_str.split("[rip ")[1].split("]")[0].replace(" ", ""), 16)
                    t = i.address + i.size + d
                    raw = IMG[t:t + 120]
                    txt = raw.decode("utf-16le", "ignore").split("\x00")[0] if len(raw) > 3 and raw[1] == 0 else raw.split(b"\x00")[0].decode("latin1")
                    if len(txt) > 3 and txt.isprintable():
                        extra = f"   ; {txt!r}"
                print(f"{'>>' if i.address == at else '  '}{i.address:x} {i.mnemonic} {i.op_str}{extra}")
            break
