"""Find packages whose imports reference a class matching a regex (header-only, fast), and count how many
exports (placed objects) in each package are instances of it.

    py find_classes.py <path-prefix>[,<prefix2>...] <class-regex> [ext]
e.g. py find_classes.py FortniteGame/Content/Athena/Apollo/ "NavMesh" .umap
Matches native classes ("Class") and blueprint classes ("BlueprintGeneratedClass", name ending in _C).
"""
import re, sys
from collections import Counter
import pak, uasset

prefixes = tuple(sys.argv[1].split(","))
rx = re.compile(sys.argv[2], re.I)
ext = sys.argv[3] if len(sys.argv) > 3 else ".umap"
idx = pak.build_index()
hits, instances, scanned = [], Counter(), 0
for v in sorted(idx):
    if not (v.startswith(prefixes) and v.endswith(ext)):
        continue
    scanned += 1
    try:
        p = uasset.Pkg(pak.read_file(idx[v]), b"")
    except Exception:
        continue
    match_idx = {}
    for n, i in enumerate(p.imports):
        if i["class_name"] in ("Class", "BlueprintGeneratedClass") and rx.search(i["name"]):
            match_idx[-(n + 1)] = i["name"]
    if not match_idx:
        continue
    counts = Counter(match_idx[e["class_index"]] for e in p.exports if e["class_index"] in match_idx)
    hits.append((v, dict(counts) if counts else {k: 0 for k in match_idx.values()}))
    instances.update(counts)
print(f"scanned {scanned} packages; {len(hits)} reference /{rx.pattern}/")
print("placed instances by class:", dict(instances))
for v, c in hits[:60]:
    print(" ", v.replace("FortniteGame/Content/", "").replace("FortniteGame/Plugins/", "Plugins/"), c)
