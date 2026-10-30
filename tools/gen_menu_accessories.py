# -*- coding: utf-8 -*-
"""生成主菜单自定义配件（低模纯色 glTF）。

产物：assets/models/custom/{cap,glasses,backpack,chicken_hat}.gltf
每个文件只含一个节点 + 一个 mesh，材质用 baseColorFactor（与现有
Menu3DModel 的 flat-color 管线一致），buffer 内嵌为 base64 data URI。
"""
import base64
import json
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "models", "custom")


class Builder:
    def __init__(self):
        self.positions = []
        self.normals = []
        self.indices = []
        self.groups = {}  # material -> (index_start, index_count)

    def _add_face(self, p0, p1, p2, p3, n):
        base = len(self.positions)
        for p in (p0, p1, p2, p3):
            self.positions.append(p)
            self.normals.append(n)
        self.indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def box(self, cx, cy, cz, sx, sy, sz, mat):
        hx, hy, hz = sx / 2.0, sy / 2.0, sz / 2.0
        x0, x1 = cx - hx, cx + hx
        y0, y1 = cy - hy, cy + hy
        z0, z1 = cz - hz, cz + hz
        start = len(self.indices)
        # +Z
        self._add_face((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (0, 0, 1))
        # -Z
        self._add_face((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (0, 0, -1))
        # +X
        self._add_face((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (1, 0, 0))
        # -X
        self._add_face((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (-1, 0, 0))
        # +Y
        self._add_face((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), (0, 1, 0))
        # -Y
        self._add_face((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), (0, -1, 0))
        self._group(mat, start, len(self.indices) - start)

    def cone(self, cx, cy, cz, radius, height, segments, mat):
        start = len(self.indices)
        apex = (cx, cy + height, cz)
        ring = []
        for i in range(segments):
            a = 2.0 * math.pi * i / segments
            ring.append((cx + math.cos(a) * radius, cy, cz + math.sin(a) * radius))
        # side faces
        for i in range(segments):
            p0 = ring[i]
            p1 = ring[(i + 1) % segments]
            base = len(self.positions)
            # normal roughly outward
            nx, nz = p0[0] - cx, p0[2] - cz
            ln = math.hypot(nx, nz) or 1.0
            n = (nx / ln, 0.35, nz / ln)
            for p in (p0, p1, apex):
                self.positions.append(p)
                self.normals.append(n)
            self.indices += [base, base + 1, base + 2]
        # base cap
        base = len(self.positions)
        for p in ring:
            self.positions.append(p)
            self.normals.append((0, -1, 0))
        for i in range(1, segments - 1):
            self.indices += [base, base + i, base + i + 1]
        self._group(mat, start, len(self.indices) - start)

    def _group(self, mat, start, count):
        if not mat:
            return
        if mat in self.groups:
            s, c = self.groups[mat]
            # 合并：若连续则扩展，否则记录第二段（简化：只支持连续）
            if s + c == start:
                self.groups[mat] = (s, c + count)
                return
            raise RuntimeError("material group not contiguous: " + mat)
        self.groups[mat] = (start, count)


def write_gltf(path, builder, materials, node_name):
    # 二进制 buffer：positions, normals, indices（uint32）
    blob = bytearray()
    pos_off = 0
    pos_len = len(builder.positions) * 12
    for p in builder.positions:
        blob += struct.pack("<3f", *p)
    norm_off = len(blob)
    norm_len = len(builder.normals) * 12
    for n in builder.normals:
        blob += struct.pack("<3f", *n)
    idx_off = len(blob)
    for i in builder.indices:
        blob += struct.pack("<I", i)
    idx_len = len(blob) - idx_off

    def acc_min_max():
        xs = [p[0] for p in builder.positions]
        ys = [p[1] for p in builder.positions]
        zs = [p[2] for p in builder.positions]
        return [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]

    pmin, pmax = acc_min_max()
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": len(builder.positions),
         "type": "VEC3", "min": pmin, "max": pmax},
        {"bufferView": 1, "componentType": 5126, "count": len(builder.normals),
         "type": "VEC3"},
        {"bufferView": 2, "componentType": 5125, "count": len(builder.indices),
         "type": "SCALAR"},
    ]
    buffer_views = [
        {"buffer": 0, "byteOffset": pos_off, "byteLength": pos_len},
        {"buffer": 0, "byteOffset": norm_off, "byteLength": norm_len},
        {"buffer": 0, "byteOffset": idx_off, "byteLength": idx_len},
    ]
    primitives = []
    mat_index = {m["name"]: i for i, m in enumerate(materials)}
    for name, (start, count) in builder.groups.items():
        primitives.append({
            "attributes": {"POSITION": 0, "NORMAL": 1},
            "indices": 2,
            "material": mat_index[name],
            "mode": 4,
        })
        # 每个材质一段 index：用 accessor 的 byteOffset 切分
        accessors.append({
            "bufferView": 2,
            "byteOffset": start * 4,
            "componentType": 5125,
            "count": count,
            "type": "SCALAR",
        })
        primitives[-1]["indices"] = len(accessors) - 1

    doc = {
        "asset": {"version": "2.0", "generator": "PixelEngine menu accessory generator"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": node_name, "mesh": 0}],
        "meshes": [{"name": node_name, "primitives": primitives}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{
            "byteLength": len(blob),
            "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode("ascii"),
        }],
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
    print("wrote", os.path.basename(path), "verts=", len(builder.positions),
          "tris=", len(builder.indices) // 3)


def mat(name, rgb):
    return {"name": name,
            "pbrMetallicRoughness": {"baseColorFactor": [rgb[0], rgb[1], rgb[2], 1.0],
                                     "metallicFactor": 0.0, "roughnessFactor": 0.8}}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # 鸭舌帽：帽体 + 帽檐 + 顶扣（挂点：Head）
    b = Builder()
    b.box(0.0, 0.10, -0.01, 0.30, 0.14, 0.30, "Cap")
    b.box(0.0, 0.035, 0.20, 0.30, 0.028, 0.18, "CapDark")
    b.box(0.0, 0.175, -0.01, 0.045, 0.02, 0.045, "CapGold")
    write_gltf(os.path.join(OUT_DIR, "cap.gltf"), b,
               [mat("Cap", (0.10, 0.16, 0.28)),
                mat("CapDark", (0.07, 0.11, 0.20)),
                mat("CapGold", (0.72, 0.58, 0.24))], "Cap")

    # 墨镜：镜片 + 鼻梁 + 镜腿（挂点：Head，正面 +Z）
    b = Builder()
    b.box(-0.075, 0.05, 0.145, 0.10, 0.052, 0.02, "Frame")
    b.box(0.075, 0.05, 0.145, 0.10, 0.052, 0.02, "Frame")
    b.box(0.0, 0.055, 0.145, 0.05, 0.014, 0.016, "Frame")
    b.box(-0.135, 0.05, 0.075, 0.016, 0.014, 0.15, "Frame")
    b.box(0.135, 0.05, 0.075, 0.016, 0.014, 0.15, "Frame")
    write_gltf(os.path.join(OUT_DIR, "glasses.gltf"), b,
               [mat("Frame", (0.05, 0.05, 0.06))], "Glasses")

    # 背包：主体 + 顶盖 + 侧袋（挂点：Neck，背面 -Z）
    b = Builder()
    b.box(0.0, -0.18, -0.16, 0.30, 0.34, 0.16, "Pack")
    b.box(0.0, 0.0, -0.16, 0.30, 0.08, 0.18, "PackDark")
    b.box(-0.17, -0.18, -0.16, 0.06, 0.22, 0.12, "PackDark")
    b.box(0.17, -0.18, -0.16, 0.06, 0.22, 0.12, "PackDark")
    write_gltf(os.path.join(OUT_DIR, "backpack.gltf"), b,
               [mat("Pack", (0.16, 0.22, 0.15)),
                mat("PackDark", (0.10, 0.14, 0.10))], "Backpack")

    # 小鸡帽子：帽檐 + 小圆锥（挂点：DEF-head）
    b = Builder()
    b.box(0.0, 0.01, 0.02, 0.16, 0.015, 0.16, "ChickHat")
    b.cone(0.0, 0.015, 0.02, 0.075, 0.11, 8, "ChickHat")
    write_gltf(os.path.join(OUT_DIR, "chicken_hat.gltf"), b,
               [mat("ChickHat", (0.66, 0.14, 0.12))], "ChickenHat")


if __name__ == "__main__":
    main()
