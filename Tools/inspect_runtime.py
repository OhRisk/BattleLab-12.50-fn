"""Inspect the locally captured mapped image at observed RVAs."""
from pathlib import Path
import re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

data = Path(sys.argv[1]).read_bytes()
md = Cs(CS_ARCH_X86, CS_MODE_64)
if sys.argv[2] == 'xrefs':
    target = int(sys.argv[3], 16)
    for m in re.finditer(rb'[\x48\x4c][\x8b\x89\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', data[:0x6600000]):
        p = m.start()
        if p + 7 + struct.unpack_from('<i', data, p+3)[0] == target:
            print(f'REF {p:x}')
            for i in md.disasm(data[p:p+20], p): print(f'  {i.address:x} {i.mnemonic} {i.op_str}')
else:
    start, size = int(sys.argv[2], 16), int(sys.argv[3], 16)
    for i in md.disasm(data[start:start+size], start): print(f'{i.address:x} {i.mnemonic} {i.op_str}')
