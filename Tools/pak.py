"""Read-only reader for the 12.50 pak files (pak v10, path-hash index, encrypted index).

    py pak.py find <regex>            list matching virtual paths (with pak + size)
    py pak.py extract <regex> [out]   extract matching files (default out: Tools/extract)
    py pak.py stats                   entry counts per pak / compression method

Opens paks from Fortnite-12.50-Source read-only. Never writes to the game tree.
The file list is cached in Tools/cache/pakindex.pkl (delete to rebuild).
"""
import ctypes, glob, os, pickle, re, struct, sys, zlib
from Crypto.Cipher import AES

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
GAME = os.path.join(ROOT, "Fortnite-12.50-Source", "FortniteGame")
PAKS = os.path.join(GAME, "Content", "Paks")
KEY = bytes.fromhex(open(os.path.join(TOOLS, "aes_main_key.txt")).read().strip()[2:])
CACHE = os.path.join(TOOLS, "cache", "pakindex.pkl")
MAGIC = 0x5A6F12E1


def aes(data):
    return AES.new(KEY, AES.MODE_ECB).decrypt(data)


def align16(n):
    return (n + 15) & ~15


class R:
    def __init__(self, b, o=0):
        self.b, self.o = b, o

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]; self.o += 4; return v

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.o)[0]; self.o += 4; return v

    def u64(self):
        v = struct.unpack_from("<Q", self.b, self.o)[0]; self.o += 8; return v

    def raw(self, n):
        v = self.b[self.o:self.o + n]; self.o += n; return v

    def fstr(self):
        n = self.i32()
        if n == 0:
            return ""
        if n < 0:
            s = self.raw(-n * 2).decode("utf-16-le")
        else:
            s = self.raw(n).decode("utf-8", "replace")
        return s.rstrip("\0")


# ------------------------------------------------------------------ index
def decode_entry(b, o):
    """UE 4.26-style encoded FPakEntry. Returns (entry dict, new offset)."""
    r = R(b, o)
    v = r.u32()
    method = (v >> 23) & 0x3F
    enc = bool(v & (1 << 22))
    nblocks = (v >> 6) & 0xFFFF
    bsize = r.u32() if (v & 0x3F) == 0x3F else (v & 0x3F) << 11
    off = r.u32() if v & (1 << 31) else r.u64()
    usize = r.u32() if v & (1 << 30) else r.u64()
    size = (r.u32() if v & (1 << 29) else r.u64()) if method else usize
    hdr = 8 + 8 + 8 + 4 + 20 + (4 + 16 * nblocks if method else 0) + 1 + 4
    blocks = []
    if nblocks == 1 and not enc:
        blocks.append((hdr, hdr + size))
    elif nblocks:
        cur = hdr
        for _ in range(nblocks):
            bs = r.u32()
            blocks.append((cur, cur + bs))
            cur += align16(bs) if enc else bs
    return dict(off=off, size=size, usize=usize, method=method, enc=enc, bsize=bsize, blocks=blocks, hdr=hdr), r.o


def read_pak_index(path):
    sz = os.path.getsize(path)
    with open(path, "rb") as f:
        f.seek(sz - 512)
        t = f.read(512)
        i = t.rfind(struct.pack("<I", MAGIC))
        enc_index = t[i - 1]
        ver, ioff, isz = struct.unpack_from("<IQQ", t, i + 4)
        f.seek(ioff)
        idx = f.read(isz)
        if enc_index:
            idx = aes(idx)
        r = R(idx)
        mount = r.fstr()
        r.i32()  # num entries
        r.u64()  # path hash seed
        if r.u32():
            r.u64(); r.u64(); r.raw(20)
        assert r.u32(), "no full directory index"
        doff, dsz = r.u64(), r.u64()
        r.raw(20)
        esz = r.u32()
        encoded = r.raw(esz)
        f.seek(doff)
        d = f.read(dsz)
        if enc_index:
            d = aes(d)
    files = {}
    dr = R(d)
    for _ in range(dr.i32()):
        dname = dr.fstr()
        for _ in range(dr.i32()):
            fname = dr.fstr()
            eoff = dr.i32()
            if eoff < 0:
                continue  # non-encoded entry list; not used by this build (checked in stats)
            e, _ = decode_entry(encoded, eoff)
            vpath = (mount + dname + fname).replace("../../../", "")
            files[vpath] = e
    return ver, mount, files


def build_index():
    if os.path.exists(CACHE):
        return pickle.load(open(CACHE, "rb"))
    allf = {}
    for p in sorted(glob.glob(os.path.join(PAKS, "*.pak"))):
        _, _, files = read_pak_index(p)
        for vp, e in files.items():
            e["pak"] = os.path.basename(p)
            allf[vp] = e  # later paks override earlier ones (same as engine mount order for these names)
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    pickle.dump(allf, open(CACHE, "wb"))
    return allf


# ------------------------------------------------------------------ data
_oodle = None


def oodle():
    global _oodle
    if _oodle is None:
        dll = os.path.join(GAME, "Binaries", "Win64", "oo2core_5_win64.dll")
        _oodle = ctypes.WinDLL(dll)
        fn = _oodle.OodleLZ_Decompress
        fn.restype = ctypes.c_ssize_t
        fn.argtypes = [ctypes.c_char_p, ctypes.c_ssize_t, ctypes.c_char_p, ctypes.c_ssize_t] + [ctypes.c_int] * 3 + \
                      [ctypes.c_void_p] * 2 + [ctypes.c_void_p] * 3 + [ctypes.c_ssize_t, ctypes.c_int]
    return _oodle


def decompress(method, data, outsize):
    if method == 0:
        return data[:outsize]
    # Method names are blank in these footers; detect by stream header.
    if data[:1] in (b"\x78",):
        return zlib.decompress(data)[:outsize]
    out = ctypes.create_string_buffer(outsize)
    n = oodle().OodleLZ_Decompress(data, len(data), out, outsize, 1, 0, 0, None, None, None, None, None, 0, 3)
    if n != outsize:
        raise IOError(f"oodle decompress failed ({n} != {outsize})")
    return out.raw


def read_file(e):
    with open(os.path.join(PAKS, e["pak"]), "rb") as f:
        if not e["method"]:
            f.seek(e["off"] + e["hdr"])
            data = f.read(align16(e["size"]) if e["enc"] else e["size"])
            if e["enc"]:
                data = aes(data)
            return data[:e["usize"]]
        out = bytearray()
        remaining = e["usize"]
        for s, t in e["blocks"]:
            f.seek(e["off"] + s)
            n = t - s
            blk = f.read(align16(n) if e["enc"] else n)
            if e["enc"]:
                blk = aes(blk)
            want = min(e["bsize"], remaining)
            out += decompress(e["method"], blk[:n], want)
            remaining -= want
        return bytes(out)


# ------------------------------------------------------------------ CLI
def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "stats"
    idx = build_index()
    if cmd == "stats":
        from collections import Counter
        print(len(idx), "files")
        print("by method:", Counter(e["method"] for e in idx.values()))
        print("encrypted:", sum(e["enc"] for e in idx.values()))
    elif cmd == "find":
        rx = re.compile(sys.argv[2], re.I)
        for vp in sorted(idx):
            if rx.search(vp):
                e = idx[vp]
                print(f"{vp}  [{e['pak']}, {e['usize']} B]")
    elif cmd == "extract":
        rx = re.compile(sys.argv[2], re.I)
        out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(TOOLS, "extract")
        n = 0
        for vp in sorted(idx):
            if rx.search(vp):
                dst = os.path.join(out, vp)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "wb").write(read_file(idx[vp]))
                n += 1
        print("extracted", n, "files to", out)


if __name__ == "__main__":
    main()
