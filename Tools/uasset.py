"""Minimal cooked-package (.uasset/.uexp) reader for Fortnite 12.50 (UE4.25-era, tagged properties).

    py uasset.py <virtual path without extension | regex>   -> prints JSON (imports, exports, properties)
    py uasset.py --out <dir> <regex>                        -> writes one .json per matching package

Reads directly from the paks through pak.py (read-only). Unknown binary structs are kept as
{"_raw": hex, "_struct": name} so nothing is silently lost.
"""
import json, os, re, struct, sys
import pak

BINARY_STRUCTS = {
    "Vector": "<3f", "Vector2D": "<2f", "Vector4": "<4f", "Rotator": "<3f", "Quat": "<4f",
    "LinearColor": "<4f", "Color": "<4B", "IntPoint": "<2i", "IntVector": "<3i",
    "Box": "<6fB", "Box2D": "<4fB", "DateTime": "<q", "Timespan": "<q", "FrameNumber": "<i",
    "Guid": "<4I", "NavAgentSelector": "<I",
    "SimpleCurveKey": "<2f", "RichCurveKey": "<3B6f",
}


class Pkg:
    def __init__(self, uasset, uexp):
        self.a, self.e = uasset, uexp
        r = pak.R(uasset)
        assert r.u32() == 0x9E2A83C1, "bad tag"
        legacy = r.i32()
        if legacy != -4:
            r.i32()
        self.ue4ver = r.i32()
        r.i32()
        for _ in range(r.i32()):
            r.raw(20)
        self.header_size = r.i32()
        r.fstr()
        self.pkg_flags = r.u32()
        ncount, noff = r.i32(), r.i32()
        if not (self.pkg_flags & 0x80000000):  # PKG_FilterEditorOnly
            r.fstr()
        r.i32(); r.i32()  # gatherable text
        ecount, eoff = r.i32(), r.i32()
        icount, ioff = r.i32(), r.i32()

        r.o = noff
        self.names = []
        for _ in range(ncount):
            self.names.append(r.fstr())
            r.u32()  # two u16 hashes

        r.o = ioff
        self.imports = []
        for _ in range(icount):
            cpkg, cname = self.fname(r), self.fname(r)
            outer = r.i32()
            name = self.fname(r)
            self.imports.append(dict(class_package=cpkg, class_name=cname, outer=outer, name=name))

        r.o = eoff
        self.exports = []
        for _ in range(ecount):
            cls, sup, tmpl, outer = r.i32(), r.i32(), r.i32(), r.i32()
            name = self.fname(r)
            flags = r.u32()
            size, off = r.u64(), r.u64()
            r.raw(12)  # forced/notforclient/notforserver
            r.raw(16); r.u32()  # package guid, flags
            r.raw(8)  # notalwaysloaded, isasset
            r.raw(20)  # dependency indices
            self.exports.append(dict(class_index=cls, super_index=sup, outer=outer, name=name,
                                     flags=flags, size=size, offset=off))

    def fname(self, r):
        i, n = r.i32(), r.i32()
        s = self.names[i] if 0 <= i < len(self.names) else f"<bad name {i}>"
        return s if n == 0 else f"{s}_{n - 1}"

    def ref(self, idx):
        """FPackageIndex -> readable path."""
        if idx == 0:
            return None
        if idx < 0:
            imp = self.imports[-idx - 1]
            parts = [imp["name"]]
            o = imp["outer"]
            while o < 0:
                parts.append(self.imports[-o - 1]["name"]); o = self.imports[-o - 1]["outer"]
            return "/".join(reversed(parts)) if len(parts) > 1 else parts[0]
        return "export:" + self.exports[idx - 1]["name"]

    def export_class(self, ex):
        return self.ref(ex["class_index"]) or "Class"

    # ---------------------------------------------------------- properties
    def read_props(self, r, end):
        props = {}
        while r.o < end:
            name = self.fname(r)
            if name == "None":
                break
            ptype = self.fname(r)
            size = r.i32()
            aidx = r.i32()
            tag = {"type": ptype}
            if ptype == "StructProperty":
                tag["struct"] = self.fname(r); r.raw(16)
            elif ptype == "BoolProperty":
                tag["bool"] = r.u8()
            elif ptype in ("ByteProperty", "EnumProperty"):
                tag["enum"] = self.fname(r)
            elif ptype in ("ArrayProperty", "SetProperty"):
                tag["inner"] = self.fname(r)
            elif ptype == "MapProperty":
                tag["key"] = self.fname(r); tag["value"] = self.fname(r)
            if r.u8():
                r.raw(16)
            start = r.o
            try:
                val = self.read_value(r, tag, size)
            except Exception as ex:  # keep going: size tells us where the next tag is
                val = {"_error": str(ex), "_raw": self.e[start:start + min(size, 64)].hex()} if False else \
                      {"_error": str(ex), "_type": ptype}
            r.o = start + size
            key = name if aidx == 0 else f"{name}[{aidx}]"
            props[key] = val
        return props

    def read_value(self, r, tag, size, in_container=False):
        t = tag["type"]
        if t == "BoolProperty":
            return bool(r.u8()) if in_container else bool(tag["bool"])
        if t == "IntProperty":
            return r.i32()
        if t == "UInt32Property":
            return r.u32()
        if t == "Int64Property":
            return struct.unpack("<q", r.raw(8))[0]
        if t == "UInt64Property":
            return r.u64()
        if t == "Int16Property":
            return struct.unpack("<h", r.raw(2))[0]
        if t == "UInt16Property":
            return struct.unpack("<H", r.raw(2))[0]
        if t == "Int8Property":
            return struct.unpack("<b", r.raw(1))[0]
        if t == "FloatProperty":
            return round(struct.unpack("<f", r.raw(4))[0], 6)
        if t == "DoubleProperty":
            return struct.unpack("<d", r.raw(8))[0]
        if t == "NameProperty":
            return self.fname(r)
        if t == "StrProperty":
            return r.fstr()
        if t in ("ObjectProperty", "ClassProperty", "InterfaceProperty", "WeakObjectProperty", "LazyObjectProperty"):
            return self.ref(r.i32())
        if t in ("SoftObjectProperty", "SoftClassProperty"):
            p = self.fname(r); sub = r.fstr()
            return p + (":" + sub if sub else "")
        if t == "ByteProperty":
            if in_container or tag.get("enum") in (None, "None"):
                return r.u8() if size in (1, 0) or in_container else self.fname(r)
            return self.fname(r)
        if t == "EnumProperty":
            return self.fname(r)
        if t == "TextProperty":
            return self.read_text(r)
        if t == "StructProperty":
            return self.read_struct(r, tag.get("struct"), size)
        if t in ("ArrayProperty", "SetProperty"):
            if t == "SetProperty":
                r.i32()
            n = r.i32()
            inner = tag["inner"]
            if inner == "StructProperty":
                self.fname(r); self.fname(r); isz = r.i32(); r.i32()
                sname = self.fname(r); r.raw(16)
                if r.u8():
                    r.raw(16)
                each = isz // n if n else 0
                return [self.read_struct(r, sname, each) for _ in range(n)]
            return [self.read_value(r, {"type": inner}, 0, True) for _ in range(n)]
        if t == "MapProperty":
            r.i32()  # keys to remove
            n = r.i32()
            out = []
            for _ in range(n):
                k = self.read_value(r, {"type": tag["key"], "struct": None}, 0, True)
                v = self.read_value(r, {"type": tag["value"], "struct": None}, 0, True)
                out.append([k, v])
            return out
        return {"_unsupported": t, "_raw": r.raw(size).hex()[:256]}

    def read_struct(self, r, sname, size):
        if sname in BINARY_STRUCTS:
            fmt = BINARY_STRUCTS[sname]
            vals = struct.unpack(fmt, r.raw(struct.calcsize(fmt)))
            return [round(v, 5) if isinstance(v, float) else v for v in vals]
        if sname == "GameplayTagContainer":
            return [self.fname(r) for _ in range(r.i32())]
        if sname in ("SoftObjectPath", "SoftClassPath", "StringAssetReference"):
            p = self.fname(r); sub = r.fstr()
            return p + (":" + sub if sub else "")
        if sname in ("PerPlatformFloat",):
            r.u8(); return round(struct.unpack("<f", r.raw(4))[0], 6)
        if sname in ("PerPlatformInt",):
            r.u8(); return r.i32()
        if sname is None:  # struct inside map with unknown type: best effort tagged
            return self.read_props(r, len(self.e))
        # default: tagged struct
        start = r.o
        try:
            v = self.read_props(r, start + size if size else len(self.e))
            v = {"_struct": sname, **v}
            return v
        except Exception:
            r.o = start
            return {"_struct": sname, "_raw": r.raw(size).hex()[:256]}

    def read_text(self, r):
        r.u32()  # flags
        ht = struct.unpack("<b", r.raw(1))[0]
        if ht == -1:
            if r.i32():
                return r.fstr()
            return ""
        if ht == 0:  # base
            ns, key, src = r.fstr(), r.fstr(), r.fstr()
            return src
        if ht == 11:  # StringTableEntry
            table = self.fname(r); key = r.fstr()
            return f"<stringtable {table}:{key}>"
        return f"<text history {ht}>"

    # ---------------------------------------------------------- DataTable rows
    def read_datatable_rows(self, r):
        rows = {}
        for _ in range(r.i32()):
            rn = self.fname(r)
            rows[rn] = self.read_props(r, len(self.e))
        return rows

    # ---------------------------------------------------------- CurveTable rows
    def read_curvetable_rows(self, r):
        """UE4.24+: int32 NumRows, uint8 CurveTableMode (1=Simple, 2=Rich), then FName + tagged curve struct.
        Returns {row: [[time, value], ...]} (simple) or the key list (rich: [interp, tangentMode, weightMode, time, value, ...])."""
        n = r.i32()
        mode = r.u8()
        rows = {}
        for _ in range(n):
            name = self.fname(r)
            curve = self.read_props(r, len(self.e))
            keys = curve.get("Keys", [])
            if mode == 2:
                keys = [[k[3], k[4]] for k in keys]  # time, value
            rows[name] = keys
        return rows

    def dump(self):
        out = {"imports": [f"{i['class_name']} {self.ref(-(n + 1))}" for n, i in enumerate(self.imports)], "exports": []}
        for ex in self.exports:
            off = ex["offset"] - self.header_size
            r = pak.R(self.e, off)
            end = off + ex["size"]
            cls = self.export_class(ex)
            item = {"name": ex["name"], "class": cls}
            if ex["super_index"]:
                item["super"] = self.ref(ex["super_index"])
            try:
                if cls.endswith("Function") or cls in ("Class", "BlueprintGeneratedClass", "WidgetBlueprintGeneratedClass",
                                                       "AnimBlueprintGeneratedClass") or cls.endswith("Property"):
                    item["note"] = "class/function body not decoded"
                else:
                    item["properties"] = self.read_props(r, end)
                    if cls.endswith("DataTable") or cls.endswith("CompositeDataTable"):
                        r.i32()
                        item["rows"] = self.read_datatable_rows(r)
                    elif cls.endswith("CurveTable"):
                        r.i32()
                        item["rows"] = self.read_curvetable_rows(r)
            except Exception as ex2:
                item["_error"] = f"{type(ex2).__name__}: {ex2}"
            out["exports"].append(item)
        return out


def load(vpath, idx=None):
    idx = idx or pak.build_index()
    base = vpath[:-7] if vpath.endswith(".uasset") else vpath
    a = pak.read_file(idx[base + ".uasset"])
    e = pak.read_file(idx[base + ".uexp"]) if base + ".uexp" in idx else b""
    return Pkg(a, e)


def main():
    args = sys.argv[1:]
    out_dir = None
    if args and args[0] == "--out":
        out_dir = args[1]; args = args[2:]
    idx = pak.build_index()
    rx = re.compile(args[0], re.I)
    matches = [v[:-7] for v in sorted(idx) if v.endswith(".uasset") and rx.search(v)]
    for base in matches:
        try:
            d = load(base, idx).dump()
        except Exception as ex:
            d = {"_error": f"{type(ex).__name__}: {ex}"}
        d["path"] = base
        if out_dir:
            dst = os.path.join(out_dir, base.replace("FortniteGame/Content/", "") + ".json")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            json.dump(d, open(dst, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
        else:
            print(json.dumps(d, indent=1, ensure_ascii=False))
    if out_dir:
        print(f"wrote {len(matches)} packages to {out_dir}")


if __name__ == "__main__":
    main()
