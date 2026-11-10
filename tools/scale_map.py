#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
按比例缩放 TMJ 地图（坐标 / 尺寸 / 背景图声明尺寸），支持整数与浮点倍数。

两个方向：
  - 缩小（--div N）：用于「地图太大」，面积 ÷N²
  - 放大（--mul N）：用于「地图太小/太挤」，面积 ×N²

N 可以是浮点数，例如 --mul 1.5（面积 ×2.25）、--div 2.5（面积 ÷6.25）、--mul 0.75。

做法:
  - 地图 width/height（tile 数）按倍数换算，向上取整避免裁掉边缘对象
  - 所有对象层里每个对象的 x / y / width / height 同比例缩放
  - layer 的 offsetx / offsety 同比例缩放（Tiled 的图层偏移）
  - imagelayer 的 imagewidth / imageheight 同比例缩放（且不小于地图像素尺寸）
  - tilewidth / tileheight 保持不变（实体、贴图、碰撞体尺寸不变）
  - 缩放后若仍有对象越界（浮点取整导致），自动补 tile 直到覆盖

因为渲染时背景图会被拉伸到 imagelayer 声明的尺寸，所以**不需要**真的去缩放图片文件，
原图保持全分辨率：缩小后依然清晰，放大后也不会糊（只是被拉大）。

注意:
  - 缩小：墙体会等比变薄。若某段墙缩放后不足 1 个 tile，栅格化时会被吸附到 8px 网格，
    可能与背景有轻微错位；本工具会打印最薄墙体供你确认。
  - 放大：几何等比变大，但**实体尺寸/移速/视野/射程不变**，所以放大 1.5 倍等于
    "同样的角色在更大的地图上跑"，交火距离和转点时间都会变化，需要重新调参。
  - 浮点倍数会产生非整数坐标；碰撞栅格化按 8px 吸附，工具会提示有多少对象受影响。
  - 两种方向都建议先用 --dry-run 看报告，再决定是否原地覆盖。

用法:
  python tools/scale_map.py assets/cs2d/maps/ancient_ruins.tmj           # 默认 ÷2（面积 ÷4）
  python tools/scale_map.py <map.tmj> --div 2.5                          # 面积 ÷6.25
  python tools/scale_map.py <map.tmj> --mul 1.5                          # 面积 ×2.25（放大）
  python tools/scale_map.py <map.tmj> --mul 1.5 --out <big.tmj>          # 放大后另存
  python tools/scale_map.py <map.tmj> --mul 1.5 --dry-run                # 只看报告
"""

import argparse
import json
import math
import os
import sys


def main():
    ap = argparse.ArgumentParser(
        description="按比例缩放 TMJ 地图：--div 缩小 / --mul 放大（均支持浮点）")
    ap.add_argument("map", help="TMJ 文件路径")
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--div", type=float, default=None,
                       help="缩小：每条轴的除数（2=面积÷4，2.5=面积÷6.25）")
    group.add_argument("--mul", type=float, default=None,
                       help="放大：每条轴的倍数（1.5=面积×2.25，2=面积×4）")
    ap.add_argument("--out", default=None, help="输出路径（默认原地覆盖）")
    ap.add_argument("--dry-run", action="store_true", help="只打印报告不写文件")
    args = ap.parse_args()

    # 默认行为保持向后兼容：不带参数时等价于 --div 2
    if args.div is None and args.mul is None:
        args.div = 2.0
    if args.div is not None and args.div <= 0:
        print("--div 必须 > 0")
        return 1
    if args.mul is not None and args.mul <= 0:
        print("--mul 必须 > 0")
        return 1
    if not os.path.exists(args.map):
        print(f"文件不存在: {args.map}")
        return 1

    with open(args.map, encoding="utf-8") as f:
        data = json.load(f)

    if data.get("type") != "map":
        print("不是 Tiled 地图 JSON")
        return 1

    factor = float(args.mul) if args.mul is not None else 1.0 / float(args.div)
    enlarging = factor > 1.0 + 1e-9      # 注意：--mul 0.5 也是缩小
    shrinking = factor < 1.0 - 1e-9
    if args.mul is not None:
        direction = f"×{args.mul:g}"
    else:
        direction = f"÷{args.div:g}"

    def sc(v):
        """按 factor 缩放一个坐标/尺寸，保留 4 位小数避免浮点噪声。"""
        return round(float(v) * factor, 4)

    tw, th = data.get("tilewidth", 8), data.get("tileheight", 8)
    old_w, old_h = data.get("width", 0), data.get("height", 0)

    # 向上取整：宁可地图大 1 tile，也不要因为浮点取整把边缘对象裁掉
    new_w = max(1, int(math.ceil(old_w * factor - 1e-9)))
    new_h = max(1, int(math.ceil(old_h * factor - 1e-9)))

    print(f"缩放方向: {direction}（每条轴，factor={factor:.6g}）")
    print(f"地图 tile: {old_w}x{old_h} -> {new_w}x{new_h}")
    print(f"像素: {old_w*tw}x{old_h*th} -> {new_w*tw}x{new_h*th}")
    print(f"面积: {old_w*old_h} -> {new_w*new_h} tile "
          f"(x{(new_w*new_h)/max(1, old_w*old_h):.3f})")

    if enlarging:
        print("[注意] 放大只改几何：BOT 尺寸/移速/视野/射程/道具半径都不变，"
              "交火与转点节奏会变化，需要重新调参。")
    elif shrinking:
        print("[注意] 缩小会让墙体等比变薄；不足 1 tile 的墙在栅格化时会被吸附。")
    if abs(factor - round(factor)) > 1e-6:
        print(f"[注意] 浮点倍数会产生非整数坐标；碰撞按 8px 栅格吸附，"
              f"可能出现 ≤8px 的错位（下方会报告受影响对象数）。")

    data["width"] = new_w
    data["height"] = new_h

    min_wall = None
    max_wall = None
    obj_count = 0
    frac_count = 0          # 有非整数坐标/尺寸的对象数
    max_extent_x = 0.0
    max_extent_y = 0.0

    def scale_objects(layer):
        """缩放一个对象层的所有对象，返回处理数量。"""
        nonlocal min_wall, max_wall, frac_count, max_extent_x, max_extent_y
        n_obj = 0
        for obj in layer.get("objects", []):
            ox, oy = float(obj.get("x", 0.0)), float(obj.get("y", 0.0))
            ow, oh = float(obj.get("width", 0) or 0), float(obj.get("height", 0) or 0)
            obj["x"] = sc(ox)
            obj["y"] = sc(oy)
            if obj.get("width"):
                obj["width"] = sc(obj["width"])
            if obj.get("height"):
                obj["height"] = sc(obj["height"])

            vals = [obj["x"], obj["y"]]
            if obj.get("width"):
                vals.append(obj["width"])
            if obj.get("height"):
                vals.append(obj["height"])
            if any(abs(v - round(v)) > 0.01 for v in vals):
                frac_count += 1

            w, h = float(obj.get("width", 0)), float(obj.get("height", 0))
            max_extent_x = max(max_extent_x, float(obj["x"]) + w)
            max_extent_y = max(max_extent_y, float(obj["y"]) + h)
            if w > 0 and h > 0:
                thin = min(w, h)
                if min_wall is None or thin < min_wall:
                    min_wall = thin
                if max_wall is None or thin > max_wall:
                    max_wall = thin
            n_obj += 1
        return n_obj

    def walk(layer_list):
        """递归处理图层树：组图层（group）里的子对象层同样要缩放，
        否则 site 组下的战术区域会留在原坐标。"""
        nonlocal obj_count
        for layer in layer_list:
            ltype = layer.get("type")

            # Tiled 图层偏移（不是所有地图都有）
            for key in ("offsetx", "offsety"):
                if layer.get(key):
                    layer[key] = sc(layer[key])

            if ltype == "imagelayer":
                iw = layer.get("imagewidth")
                ih = layer.get("imageheight")
                if iw and ih:
                    # 背景会被拉伸到地图像素尺寸，因此不能小于地图
                    layer["imagewidth"] = max(1, int(round(float(iw) * factor)),
                                              new_w * tw)
                    layer["imageheight"] = max(1, int(round(float(ih) * factor)),
                                               new_h * th)
                    print(f"[imagelayer '{layer.get('name')}'] "
                          f"{iw}x{ih} -> {layer['imagewidth']}x{layer['imageheight']}")

            elif ltype == "objectgroup":
                n_obj = scale_objects(layer)
                obj_count += n_obj
                print(f"[objectgroup '{layer.get('name')}'] {n_obj} 个对象")

            elif ltype == "group":
                print(f"[group '{layer.get('name')}'] 递归处理子图层")
                walk(layer.get("layers", []))

            elif ltype == "tilelayer":
                # 缩放后 tile 层会错位，直接提示（本引擎的地图不使用 tile 层）
                if layer.get("data"):
                    print(f"[警告] tile 层 '{layer.get('name')}' 含瓦片数据，本工具不会重采样；"
                          f"缩放后该层会错位，请改用 imagelayer 背景")

    walk(data.get("layers", []))

    # 浮点取整可能让对象略微越界：补 tile 直到覆盖（只补不裁）
    grew = False
    if max_extent_x > new_w * tw + 1e-6:
        extra = int(math.ceil((max_extent_x - new_w * tw) / tw))
        new_w += extra
        data["width"] = new_w
        grew = True
    if max_extent_y > new_h * th + 1e-6:
        extra = int(math.ceil((max_extent_y - new_h * th) / th))
        new_h += extra
        data["height"] = new_h
        grew = True
    if grew:
        print(f"[修正] 对象越界，地图补到 {new_w}x{new_h} tile "
              f"({new_w*tw}x{new_h*th}px)")

    if min_wall is not None:
        tiles = min_wall / max(1, tw)
        print(f"最薄墙体: {min_wall:.2f}px ≈ {tiles:.2f} tile"
              + ("  [注意: 不足 1 tile，栅格化会吸附]" if tiles < 1.0 else ""))
        if max_wall is not None and max_wall != min_wall:
            print(f"最厚墙体: {max_wall:.2f}px ≈ {max_wall/max(1, tw):.2f} tile")
        if frac_count:
            print(f"[提示] {frac_count}/{obj_count} 个对象含非整数坐标/尺寸，"
                  f"碰撞栅格化会吸附到 8px 网格")

    if args.dry_run:
        print("(dry-run, 未写回)")
        return 0

    out = args.out or args.map
    with open(out, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f"已写回: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
