"""Discover every cosmetic item definition in the 12.50 paks and write LocalServer/data/cosmetics.json.

Classification is by the asset's actual class (read from the package header), not by filename.
Output: {"items": [{"templateId": "AthenaCharacter:cid_001_...", "path": "...", "class": "..."}], "counts": {...}}
"""
import json, os, sys
from collections import Counter
import pak, uasset

# item definition class -> MCP template type
CLASS_TO_TYPE = {
    "AthenaCharacterItemDefinition": "AthenaCharacter",
    "AthenaBackpackItemDefinition": "AthenaBackpack",
    "AthenaPetCarrierItemDefinition": "AthenaBackpack",
    "AthenaPetItemDefinition": "AthenaBackpack",
    "AthenaPickaxeItemDefinition": "AthenaPickaxe",
    "AthenaGliderItemDefinition": "AthenaGlider",
    "AthenaSkyDiveContrailItemDefinition": "AthenaSkyDiveContrail",
    "AthenaDanceItemDefinition": "AthenaDance",
    "AthenaSprayItemDefinition": "AthenaDance",
    "AthenaEmojiItemDefinition": "AthenaDance",
    "AthenaToyItemDefinition": "AthenaDance",
    "AthenaItemWrapDefinition": "AthenaItemWrap",
    "AthenaMusicPackItemDefinition": "AthenaMusicPack",
    "AthenaLoadingScreenItemDefinition": "AthenaLoadingScreen",
}

ROOTS = ("FortniteGame/Content/Athena/Items/Cosmetics/",)


def main():
    idx = pak.build_index()
    out, counts, unknown = [], Counter(), Counter()
    cands = [v[:-7] for v in idx if v.endswith(".uasset") and v.startswith(ROOTS)]
    print(len(cands), "candidate packages")
    for n, base in enumerate(sorted(cands)):
        name = base.rsplit("/", 1)[1]
        try:
            p = uasset.Pkg(pak.read_file(idx[base + ".uasset"]), b"")
        except Exception as e:
            unknown["<unreadable>"] += 1
            continue
        ex = next((e for e in p.exports if e["name"] == name), None)
        if not ex:
            continue
        cls = p.export_class(ex).rsplit("/", 1)[-1]
        t = CLASS_TO_TYPE.get(cls)
        if not t:
            unknown[cls] += 1
            continue
        out.append({"templateId": f"{t}:{name.lower()}", "path": "/" + base.replace("FortniteGame/Content/", "Game/")})
        counts[t] += 1
        if n % 2000 == 0:
            print(" ...", n)
    dst = os.path.join(pak.ROOT, "LocalServer", "data", "cosmetics.json")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    json.dump({"build": "12.50-CL-13137020", "counts": counts, "items": out}, open(dst, "w"), indent=1)
    print("counts:", dict(counts))
    print("non-cosmetic classes skipped:", dict(unknown.most_common(15)))
    print("wrote", dst, len(out), "items")


if __name__ == "__main__":
    main()
