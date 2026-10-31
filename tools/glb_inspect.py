"""Ad-hoc GLB inspector used while auditing assets/models/chickensoft/chicken.glb.

Reads the JSON chunk of a binary glTF and prints node hierarchy, animations,
primitive bounds and material info. Kept tiny on purpose (stdlib only).
"""
import json
import struct
import sys


def load_glb(path):
    data = open(path, "rb").read()
    magic, version, length = struct.unpack("<III", data[:12])
    off = 12
    chunks = []
    while off < length:
        clen, ctype = struct.unpack("<II", data[off:off + 8])
        off += 8
        chunks.append((ctype, data[off:off + clen]))
        off += clen
    return version, length, chunks, json.loads(chunks[0][1].decode("utf-8"))


def main(path):
    version, length, chunks, j = load_glb(path)
    print("file:", path)
    print("glb version:", version, "bytes:", length, "extra chunks:", len(chunks) - 1)
    print("asset:", j.get("asset"))
    print("animations:", [a.get("name") for a in j.get("animations", [])])
    print("nodes:", len(j.get("nodes", [])), "meshes:", len(j.get("meshes", [])),
          "skins:", len(j.get("skins", [])))
    print("materials:", [m.get("name") for m in j.get("materials", [])])
    print("images:", len(j.get("images", [])), "textures:", len(j.get("textures", [])))
    print("scene roots:", j["scenes"][j.get("scene", 0)]["nodes"])
    for i, n in enumerate(j.get("nodes", [])):
        keep = {k: n[k] for k in ("name", "translation", "rotation", "scale",
                                   "mesh", "skin", "children") if k in n}
        print("node", i, keep)
    acc = j["accessors"]
    for m in j.get("meshes", []):
        for pr in m["primitives"]:
            pos = acc[pr["attributes"]["POSITION"]]
            tri = acc[pr["indices"]]["count"] // 3 if "indices" in pr else 0
            print("prim", m.get("name"), "verts", pos["count"], "tris", tri,
                  "min", pos.get("min"), "max", pos.get("max"),
                  "material", pr.get("material"))
    for a in j.get("animations", []):
        ch = a.get("channels", [])
        paths = {}
        for c in ch:
            p = c["target"]["path"]
            paths[p] = paths.get(p, 0) + 1
        dur = 0.0
        for s in a.get("samplers", []):
            inp = acc[s["input"]]
            if inp.get("max"):
                dur = max(dur, inp["max"][0])
        print("anim", repr(a.get("name")), "channels", len(ch), paths,
              "duration", round(dur, 4), "s")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "assets/models/chickensoft/chicken.glb")
