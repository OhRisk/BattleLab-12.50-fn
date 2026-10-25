"""Read-only: find code references to a UTF-16 string in the saved runtime image and disassemble around them.
Usage: py Tools/find_xref.py "<string>" [before_bytes] [after_bytes]"""
import re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMG = open(r"E:\Fortnite Battle Lab revival\Logs\Test004\runtime-image.bin", "rb").read()
text = sys.argv[1]
before = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x100
after = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x20
needle = text.encode("utf-16le") + b"\x00\x00"
targets = [m.start() for m in re.finditer(re.escape(needle), IMG)]
print("string at", [hex(t) for t in targets])
md = Cs(CS_ARCH_X86, CS_MODE_64)
# lea r64, [rip+disp32]: (48|4C) 8D modrm(00 xxx 101)
for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", IMG[:0x6000000]):
    at = m.start()
    disp = struct.unpack_from("<i", IMG, at + 3)[0]
    if at + 7 + disp in targets:
        print(f"== xref at {at:x}")
        for back in range(before, before - 0x20, -1):
            ins = list(md.disasm(IMG[at - back:at + after], at - back))
            if any(i.address == at for i in ins):
                for i in ins:
                    extra = ""
                    if "[rip " in i.op_str:
                        d = int(i.op_str.split("[rip ")[1].split("]")[0].replace(" ", ""), 16)
                        extra = f"   ; -> {i.address + i.size + d:x}"
                    print(f"{'>>' if i.address == at else '  '}{i.address:x} {i.mnemonic} {i.op_str}{extra}")
                break
