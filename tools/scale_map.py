#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
按比例缩小 TMJ 地图（坐标 / 尺寸 / 背景图声明尺寸），用于「地图太大」的场合。

做法:
  - 地图 width/height（tile 数）÷ N
  - 所有对象层里每个对象的 x / y / width / height ÷ N
  - imagelayer 的 imagewidth / imageheight ÷ N
  - tilewidth / tileheight 保持不变（实体、贴图、碰撞体尺寸不变）

因为渲染时背景图会被拉伸到 imagelayer 声明的尺寸，所以**不需要**真的去缩放图片文件，
原图保持全分辨率，缩小后依然清晰。

注意: 墙体会等比变薄。如果某段墙缩放后不足 1 个 tile，栅格化时会被吸附到 8px 网格，
可能与背景有轻微错位；本工具会打印最薄墙体供你确认。

用法:
  python tools/scale_map.py assets/cs2d/maps/ancient_ruins.tmj          # 默认 ÷2（面积 ÷4）
  python tools/scale_map.py <map.tmj> --div 4                           # 面积 ÷16
  python tools/scale_map.py <map.tmj> --div 2 --dry-run                 # 只看报告
  python tools/scale_map.py <map.tmj> --div 2 --out <other.tmj>         # 另存
"""

import argparse
import json
import os
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map", help="TMJ 文件路径")
    ap.add_argument("--div", type=int, default=2,
                    help="每条轴的缩放除数（2=面积÷4，4=面积÷16），默认 2")
    ap.add_argument("--out", default=None, help="输出路径（默认原地覆盖）")
    ap.add_argument("--dry-run", action="store_true", help="只打印报告不写文件")
    args = ap.parse_args()

    if args.div < 1:
        print("--div 必须 >= 1")
        return 1
    if not os.path.exists(args.map):
        print(f"文件不存在: {args.map}")
        return 1

    with open(args.map, encoding="utf-8") as f:
        data = json.load(f)

    if data.get("type") != "map":
        print("不是 Tiled 地图 JSON")
        return 1

    n = float(args.div)
    old_w, old_h = data.get("width", 0), data.get("height", 0)
    new_w = max(1, int(round(old_w / n)))
    new_h = max(1, int(round(old_h / n)))

    print(f"地图 tile: {old_w}x{old_h} -> {new_w}x{new_h}")
    tw, th = data.get("tilewidth", 8), data.get("tileheight", 8)
    print(f"像素: {old_w*tw}x{old_h*th} -> {new_w*tw}x{new_h*th}")
    print(f"面积: {old_w*old_h} -> {new_w*new_h} tile "
          f"(x{(new_w*new_h)/max(1, old_w*old_h):.3f})")

    data["width"] = new_w
    data["height"] = new_h

    min_wall = None
    obj_count = 0

    def scale_objects(layer):
        """缩放一个对象层的所有对象，返回处理数量。"""
        nonlocal min_wall
        n_obj = 0
        for obj in layer.get("objects", []):
            obj["x"] = float(obj.get("x", 0.0)) / n
            obj["y"] = float(obj.get("y", 0.0)) / n
            if obj.get("width"):
                obj["width"] = float(obj["width"]) / n
            if obj.get("height"):
                obj["height"] = float(obj["height"]) / n

            w, h = float(obj.get("width", 0)), float(obj.get("height", 0))
            if w > 0 and h > 0:
                thin = min(w, h)
                if min_wall is None or thin < min_wall:
                    min_wall = thin
            n_obj += 1
        return n_obj

    def walk(layer_list):
        """递归处理图层树：组图层（group）里的子对象层同样要缩放，
        否则 site 组下的战术区域会留在原坐标。"""
        nonlocal obj_count
        for layer in layer_list:
            ltype = layer.get("type")

            if ltype == "imagelayer":
                iw = layer.get("imagewidth")
                ih = layer.get("imageheight")
                if iw and ih:
                    layer["imagewidth"] = int(round(iw / n))
                    layer["imageheight"] = int(round(ih / n))
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
                # 缩小后 tile 层会错位，直接提示（本引擎的地图不使用 tile 层）
                if layer.get("data"):
                    print(f"[警告] tile 层 '{layer.get('name')}' 含瓦片数据，本工具不会重采样；"
                          f"缩小后该层会错位，请改用 imagelayer 背景")

    walk(data.get("layers", []))

    if min_wall is not None:
        tiles = min_wall / max(1, tw)
        print(f"最薄墙体: {min_wall:.2f}px ≈ {tiles:.2f} tile"
              + ("  [注意: 不足 1 tile，栅格化会吸附]" if tiles < 1.0 else ""))

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
