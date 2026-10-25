"""Read-only: resolve exact function bounds for RVAs using the .pdata table of a running game process.
Usage: py Tools/live_func.py <pid> <rva> [<rva> ...]"""
import ctypes as c, struct, sys
import ctypes.wintypes as w

pid = int(sys.argv[1])
k = c.WinDLL("kernel32", use_last_error=True)
psapi = c.WinDLL("psapi")
k.OpenProcess.restype = c.c_void_p
h = k.OpenProcess(0x410, False, pid)
if not h:
    raise OSError(c.get_last_error())
mods = (c.c_void_p * 1024)()
need = w.DWORD()
psapi.EnumProcessModulesEx(c.c_void_p(h), mods, c.sizeof(mods), c.byref(need), 3)
base = mods[0]


def read(addr, n):
    buf = c.create_string_buffer(n)
    got = c.c_size_t()
    k.ReadProcessMemory(c.c_void_p(h), c.c_void_p(addr), buf, n, c.byref(got))
    return buf.raw[: got.value]


hdr = read(base, 0x1000)
pe = struct.unpack_from("<I", hdr, 0x3C)[0]
exc_rva, exc_size = struct.unpack_from("<II", hdr, pe + 24 + 112 + 3 * 8)
table = read(base + exc_rva, exc_size)
funcs = [struct.unpack_from("<III", table, i) for i in range(0, len(table) - 11, 12)]
print(f"base {base:#x}, {len(funcs)} functions")
for arg in sys.argv[2:]:
    rva = int(arg, 16)
    hit = [f for f in funcs if f[0] <= rva < f[1]]
    for b, e, unwind in hit:
        # Chained unwind info (flag 4) points at the primary function entry.
        info = read(base + unwind, 64)
        flags = info[0] >> 3
        primary = b
        if flags & 4:
            count = info[2]
            off = 4 + ((count + 1) & ~1) * 2
            primary = struct.unpack_from("<I", info, off)[0]
        print(f"{rva:#x}: chunk {b:#x}-{e:#x}, primary function {primary:#x}")
    if not hit:
        print(f"{rva:#x}: no .pdata entry")
