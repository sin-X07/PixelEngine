#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
map_collision_debug.py —— 把 TMJ 的碰撞层按引擎规则栅格化，并叠在背景图上，
用来定位「空气墙」（画面上是空地、实际走不过去）。

引擎的栅格化规则（PixelEngine/CS2D/CS2DWorld.cpp）：
    colMin = floor(x / tileW)
    colMax = floor((x + w - 0.001) / tileH)
即：矩形只要「碰到」某个 tile 一点，整个 tile 都判为 Solid。
所以：
  * 非整数坐标 / 非 8px 对齐的矩形会被放大到整格（最多各方向 +7.99px）
  * 细条（< 1 tile）的墙会被放大成一整格
  * 旧图碰撞数据套在新背景上时，会出现"地板上还有碰撞"

用法：
  python tools/map_collision_debug.py assets/cs2d/maps/dust_2.tmj --out build-evidence/collision.png
  python tools/map_collision_debug.py <map.tmj> --out x.png --scale 0.5 --no-image
  python tools/map_collision_debug.py <map.tmj> --snap-out <map_snapped.tmj> --snap-mode grow
"""
import argparse
import json
import math
import os
import sys
from collections import deque

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_layer(layers, name):
    for l in layers:
        if l.get("type") == "group":
            r = find_layer(l.get("layers", []), name)
            if r is not None:
                return r
        elif l.get("name") == name:
            return l
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--out", default="", help="预览 PNG 输出路径")
    ap.add_argument("--scale", type=float, default=0.5, help="预览缩放（默认 0.5）")
    ap.add_argument("--no-image", action="store_true", help="不叠背景图，只要网格")
    ap.add_argument("--margin", type=float, default=None,
                    help="center 模式的外扩边距(px)，默认读地图属性或 2")
    ap.add_argument("--set-margin", type=float, default=None,
                    help="把 collisionMargin 写进地图属性（配合 --write）")
    ap.add_argument("--mode", choices=["auto", "cover", "center"], default="auto",
                    help="栅格化模式：auto=读地图属性，cover/center=强制指定")
    ap.add_argument("--set-mode", choices=["cover", "center"], default=None,
                    help="把 collisionMode 写进地图属性（配合 --write）")
    ap.add_argument("--write", default="", help="把修改后的地图写到这个路径（常与 --set-mode 搭配）")
    ap.add_argument("--regen-out", default="",
                    help="按背景图重新生成 Collisions 层，写到这个新 TMJ（不改原文件）")
    ap.add_argument("--regen-threshold", type=float, default=0.5,
                    help="单个 tile 被判为墙所需的暗色像素占比（默认 0.5）")
    ap.add_argument("--snap-out", default="", help="输出对齐到网格后的 TMJ")
    ap.add_argument("--snap-mode", choices=["round", "grow", "shrink"], default="round",
                    help="对齐方式：round=就近 / grow=只扩不缩 / shrink=只缩不扩")
    args = ap.parse_args()

    with open(args.map, encoding="utf-8") as f:
        data = json.load(f)
    TW, TH = data["tilewidth"], data["tileheight"]
    W, H = data["width"], data["height"]
    pxw, pxh = W * TW, H * TH

    col = find_layer(data["layers"], "Collisions")
    if col is None:
        print("找不到 Collisions 层")
        return 1
    objs = col.get("objects", [])

    # 地图属性 collisionMode
    map_mode = "cover"
    props = data.get("properties")
    if isinstance(props, list):
        for pr in props:
            if pr.get("name") == "collisionMode" and pr.get("value"):
                map_mode = str(pr["value"])
    map_margin = 2.0
    if isinstance(props, list):
        for pr in props:
            if pr.get("name") == "collisionMargin" and pr.get("value") is not None:
                try:
                    map_margin = float(pr["value"])
                except (TypeError, ValueError):
                    map_margin = 2.0
    margin = args.margin if args.margin is not None else map_margin
    mode = args.mode if args.mode != "auto" else map_mode
    if mode == "center":
        print(f"碰撞边距: {margin:g}px")
    if mode not in ("cover", "center"):
        mode = "cover"
    print(f"碰撞模式: {mode}" + ("（地图属性）" if args.mode == "auto" else "（命令行强制）"))

    def rasterize(rects, tw, th, ww, hh, mm, mg=0.0):
        g = bytearray(ww * hh)
        for o in rects:
            x, y = float(o.get("x", 0)), float(o.get("y", 0))
            w, h = float(o.get("width", 0) or 0), float(o.get("height", 0) or 0)
            if w <= 0 or h <= 0:
                continue
            if mm == "center":
                x0, y0, x1, y1 = x - mg, y - mg, x + w + mg, y + h + mg
                if x1 - x0 < tw:
                    mid = (x0 + x1) * 0.5
                    x0, x1 = mid - tw * 0.5, mid + tw * 0.5
                if y1 - y0 < th:
                    mid = (y0 + y1) * 0.5
                    y0, y1 = mid - th * 0.5, mid + th * 0.5
            else:
                x0, y0, x1, y1 = x, y, x + w, y + h

            c0 = max(0, int(math.floor(x0 / tw)))
            c1 = min(ww - 1, int(math.floor((x1 - 0.001) / tw)))
            r0 = max(0, int(math.floor(y0 / th)))
            r1 = min(hh - 1, int(math.floor((y1 - 0.001) / th)))
            for r in range(r0, r1 + 1):
                if mm == "center":
                    cy = (r + 0.5) * th
                    if not (y0 < cy < y1):
                        continue
                base = r * ww
                for c in range(c0, c1 + 1):
                    if mm == "center":
                        cx = (c + 0.5) * tw
                        if not (x0 < cx < x1):
                            continue
                    g[base + c] = 1
        return g

    grid = rasterize(objs, TW, TH, W, H, mode, margin if mode == "center" else 0.0)

    frac = 0
    sliver = 0
    outside = 0
    for o in objs:
        x, y = float(o.get("x", 0)), float(o.get("y", 0))
        w, h = float(o.get("width", 0) or 0), float(o.get("height", 0) or 0)
        if w <= 0 or h <= 0:
            continue
        if any(abs(v - round(v)) > 0.01 for v in (x, y, w, h)):
            frac += 1
        if w < TW or h < TH:
            sliver += 1
        if x + w > pxw + 0.01 or y + h > pxh + 0.01:
            outside += 1

    solid = sum(grid)
    # 孤立的 solid 单元（8 邻域没有 solid，且不贴地图边缘）：最像"空气墙"
    isolated = []
    for r in range(1, H - 1):
        for c in range(1, W - 1):
            if not grid[r * W + c]:
                continue
            n = 0
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    if dr == 0 and dc == 0:
                        continue
                    n += grid[(r + dr) * W + (c + dc)]
            if n == 0:
                isolated.append((c, r))

    print(f"地图: {W}x{H} tile ({pxw}x{pxh}px), tile={TW}x{TH}")
    print(f"碰撞矩形: {len(objs)}  非整数坐标: {frac}  细条(<1 tile): {sliver}  越界: {outside}")
    print(f"Solid 单元: {solid}/{W*H} ({solid/(W*H):.1%})")
    print(f"孤立 solid 单元（疑似空气墙）: {len(isolated)}")
    for c, r in isolated[:10]:
        print(f"    tile({c},{r}) ≈ ({c*TW},{r*TH})px")
    if isolated:
        # 聚类，方便定位
        seen = set(isolated)
        clusters = []
        for p in isolated:
            if p in seen:
                continue
            q = deque([p]); seen.discard(p); comp = []
            while q:
                x, y = q.popleft(); comp.append((x, y))
                for nx in range(x - 2, x + 3):
                    for ny in range(y - 2, y + 3):
                        if (nx, ny) in seen:
                            seen.discard((nx, ny)); q.append((nx, ny))
            clusters.append(comp)
        print(f"疑似空气墙聚成 {len(clusters)} 处：")
        for comp in sorted(clusters, key=len, reverse=True)[:8]:
            xs = [p[0] for p in comp]; ys = [p[1] for p in comp]
            print(f"    {len(comp):3d} 格  tile x[{min(xs)}..{max(xs)}] y[{min(ys)}..{max(ys)}]")

    if args.out:
        scale = max(0.1, args.scale)
        out_w, out_h = max(1, int(pxw * scale)), max(1, int(pxh * scale))
        if args.no_image:
            img = Image.new("RGB", (out_w, out_h), (24, 28, 34))
        else:
            imgpath = None
            for l in data["layers"]:
                if l.get("type") == "imagelayer" and l.get("image"):
                    imgpath = os.path.normpath(os.path.join(os.path.dirname(args.map), l["image"]))
                    break
            if imgpath and os.path.exists(imgpath):
                img = Image.open(imgpath).convert("RGB").resize((out_w, out_h), Image.LANCZOS)
            else:
                print(f"[警告] 背景图不存在: {imgpath}")
                img = Image.new("RGB", (out_w, out_h), (24, 28, 34))

        overlay = Image.new("RGBA", (out_w, out_h), (0, 0, 0, 0))
        d = ImageDraw.Draw(overlay)
        cw = out_w / W
        ch = out_h / H
        for r in range(H):
            for c in range(W):
                if grid[r * W + c]:
                    d.rectangle([c * cw, r * ch, (c + 1) * cw, (r + 1) * ch],
                                fill=(255, 0, 0, 90))
        # 碰撞矩形轮廓
        for o in objs:
            x, y = o.get("x", 0) * scale, o.get("y", 0) * scale
            w, h = o.get("width", 0) * scale, o.get("height", 0) * scale
            d.rectangle([x, y, x + w, y + h], outline=(255, 220, 0, 255), width=1)
        # 孤立单元用洋红标记
        for c, r in isolated:
            d.rectangle([c * cw, r * ch, (c + 1) * cw, (r + 1) * ch],
                        fill=(255, 0, 255, 220))
        img = Image.alpha_composite(img.convert("RGBA"), overlay).convert("RGB")
        img.save(args.out)
        print(f"预览已写出: {args.out}")

    if args.regen_out:
        try:
            import numpy as np
        except ImportError:
            print("重新生成碰撞需要 numpy（用项目自带 Python 运行）")
            return 1
        # 背景图路径
        imgpath = None
        for l in data["layers"]:
            if l.get("type") == "imagelayer" and l.get("image"):
                imgpath = os.path.normpath(os.path.join(os.path.dirname(args.map), l["image"]))
                break
        if not imgpath or not os.path.exists(imgpath):
            print(f"找不到背景图: {imgpath}")
            return 1

        im = Image.open(imgpath).convert("RGB").resize((pxw, pxh), Image.LANCZOS)
        a = np.asarray(im).astype(np.float32)
        R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
        V = a.mean(axis=2)
        # 墙候选：暗、偏红棕、不是绿色植被
        wall = (V < 145) & (R >= G * 0.95) & ~((G > R * 1.05) & (G > B * 1.1))
        frac = wall.reshape(H, TH, W, TW).mean(axis=(1, 3))
        solid = frac > args.regen_threshold

        # 先对墙做 2 次膨胀，闭合 AI 图里的细小缺口，避免外侧 flood fill 漏进可玩区
        dil = solid.copy()
        for _ in range(2):
            d2 = dil.copy()
            d2[1:, :] |= dil[:-1, :]
            d2[:-1, :] |= dil[1:, :]
            d2[:, 1:] |= dil[:, :-1]
            d2[:, :-1] |= dil[:, 1:]
            dil = d2

        # 地图外侧（从边界连通出去的非墙区域）也算实心，避免走出地图
        outside = np.zeros((H, W), dtype=bool)
        dq = deque()
        for c in range(W):
            for r in (0, H - 1):
                if not dil[r, c] and not outside[r, c]:
                    outside[r, c] = True; dq.append((r, c))
        for r in range(H):
            for c in (0, W - 1):
                if not dil[r, c] and not outside[r, c]:
                    outside[r, c] = True; dq.append((r, c))
        while dq:
            r, c = dq.popleft()
            for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nr, nc = r + dr, c + dc
                if 0 <= nr < H and 0 <= nc < W and not dil[nr, nc] and not outside[nr, nc]:
                    outside[nr, nc] = True; dq.append((nr, nc))
        # 保留原始墙（不保留膨胀），外侧整体填实
        solid = solid | outside

        # 合并成矩形：逐行 run，再纵向合并相同 run
        rects = []
        active = {}   # (c0,c1) -> [r0, r1]
        for r in range(H):
            row = solid[r]
            runs = []
            c = 0
            while c < W:
                if not row[c]:
                    c += 1; continue
                c0 = c
                while c < W and row[c]:
                    c += 1
                runs.append((c0, c - 1))
            new_active = {}
            for run in runs:
                if run in active:
                    new_active[run] = [active.pop(run)[0], r]
                else:
                    new_active[run] = [r, r]
            for run, (r0, r1) in active.items():
                rects.append((run[0], r0, run[1] - run[0] + 1, r1 - r0 + 1))
            active = new_active
        for run, (r0, r1) in active.items():
            rects.append((run[0], r0, run[1] - run[0] + 1, r1 - r0 + 1))

        objs_new = []
        for i, (c0, r0, w, h) in enumerate(rects):
            objs_new.append({
                "id": i + 1,
                "name": "",
                "type": "",
                "x": c0 * TW,
                "y": r0 * TH,
                "width": w * TW,
                "height": h * TH,
                "visible": True,
                "rotation": 0,
                "opacity": 1,
            })
        col["objects"] = objs_new
        col["objectalignment"] = col.get("objectalignment", "topleft")
        with open(args.regen_out, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"已按背景图重生成碰撞: {len(objs_new)} 个矩形 → {args.regen_out}")
        print(f"  tile 判墙: {solid.mean():.1%}（旧碰撞 {sum(grid)/len(grid):.1%}）")
        if args.out:
            # 同时输出一张仅看新碰撞的预览
            sc2 = max(0.1, args.scale)
            ow, oh = max(1, int(pxw * sc2)), max(1, int(pxh * sc2))
            base = im.resize((ow, oh), Image.LANCZOS).convert("RGBA")
            ov = Image.new("RGBA", (ow, oh), (0, 0, 0, 0))
            dd2 = ImageDraw.Draw(ov)
            cw2, ch2 = ow / W, oh / H
            for r in range(H):
                for c in range(W):
                    if solid[r, c]:
                        dd2.rectangle([c * cw2, r * ch2, (c + 1) * cw2, (r + 1) * ch2],
                                      fill=(0, 255, 0, 80))
            Image.alpha_composite(base, ov).convert("RGB").save(args.out)
            print(f"新碰撞预览: {args.out}")

    if args.set_margin is not None and args.write:
        plist = data.get("properties")
        if not isinstance(plist, list):
            plist = []
        found = False
        for pr in plist:
            if pr.get("name") == "collisionMargin":
                pr["value"] = args.set_margin
                found = True
        if not found:
            plist.append({"name": "collisionMargin", "type": "float", "value": args.set_margin})
        data["properties"] = plist
        with open(args.write, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"已写入 collisionMargin={args.set_margin:g}: {args.write}")
        return 0

    if args.set_mode and args.write:
        plist = data.get("properties")
        if not isinstance(plist, list):
            plist = []
        found = False
        for pr in plist:
            if pr.get("name") == "collisionMode":
                pr["value"] = args.set_mode
                found = True
        if not found:
            plist.append({"name": "collisionMode", "type": "string", "value": args.set_mode})
        data["properties"] = plist
        with open(args.write, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"已写入 collisionMode={args.set_mode}: {args.write}")
    elif args.write:
        with open(args.write, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"已写出地图: {args.write}")

    if args.snap_out:
        def snap(v, mode, up):
            q = v / TW
            if mode == "round":
                return round(q) * TW
            if mode == "grow":
                return (math.ceil(q) if up else math.floor(q)) * TW
            return (math.floor(q) if up else math.ceil(q)) * TW

        for o in objs:
            x, y = float(o.get("x", 0)), float(o.get("y", 0))
            w, h = float(o.get("width", 0) or 0), float(o.get("height", 0) or 0)
            nx = snap(x, args.snap_mode, False)
            ny = snap(y, args.snap_mode, False)
            nr = snap(x + w, args.snap_mode, True)
            nb = snap(y + h, args.snap_mode, True)
            o["x"], o["y"] = nx, ny
            o["width"], o["height"] = max(TW, nr - nx), max(TH, nb - ny)
        with open(args.snap_out, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"已写出对齐后的 TMJ: {args.snap_out}（mode={args.snap_mode}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
