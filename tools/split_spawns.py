#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 TMJ 地图里的「通用出生点」按空间位置拆分为 spawn_t / spawn_ct。

背景:
  CS2DWorld 只把 spawn_t / spawn_ct 识别为阵营专属出生点；
  通用 SpawnPoint 会退化为全图共用（双方可能从同一侧出生）。

用法:
  python tools/split_spawns.py assets/cs2d/maps/cs_office.tmj
  python tools/split_spawns.py <map.tmj> --axis x   # 强制按 x 轴切分
  python tools/split_spawns.py <map.tmj> --dry-run  # 只报告不写回

默认自动选择较长的轴作为切分轴。
"""

import argparse
import json
import os
import sys

GENERIC_TYPES = {"spawnpoint", "spawn", ""}


def find_entities(layers):
    for layer in layers:
        name = str(layer.get("name", "")).lower()
        if layer.get("type") == "objectgroup" and name in ("entities", "objects"):
            return layer
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map", help="TMJ 文件路径")
    ap.add_argument("--axis", choices=["x", "y"], default=None,
                    help="切分轴（默认取地图较长轴）")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.map):
        print(f"文件不存在: {args.map}")
        return 1

    with open(args.map, encoding="utf-8") as f:
        data = json.load(f)

    if data.get("type") != "map":
        print("不是 Tiled 地图 JSON")
        return 1

    layer = find_entities(data.get("layers", []))
    if layer is None:
        print("未找到 Entities/objects 对象层")
        return 1

    axis = args.axis
    if axis is None:
        axis = "x" if data.get("width", 0) >= data.get("height", 0) else "y"

    # 注意: 对象坐标是世界像素，地图尺寸是 tile 数，必须换算
    if axis == "x":
        extent_px = data.get("width", 0) * data.get("tilewidth", 8)
    else:
        extent_px = data.get("height", 0) * data.get("tileheight", 8)
    mid = extent_px * 0.5

    generic = []
    for obj in layer.get("objects", []):
        t = str(obj.get("type", "")).strip().lower()
        if t in GENERIC_TYPES:
            generic.append(obj)

    if not generic:
        print("没有需要拆分的通用出生点")
        return 0

    t_count = ct_count = 0
    for obj in generic:
        coord = float(obj.get(axis, 0.0))
        if coord < mid:
            obj["type"] = "spawn_t"
            t_count += 1
        else:
            obj["type"] = "spawn_ct"
            ct_count += 1

    print(f"切分轴: {axis} (中点 {mid})")
    print(f"spawn_t: {t_count}  spawn_ct: {ct_count}")

    if t_count == 0 or ct_count == 0:
        print("警告: 一侧没有出生点，请检查切分轴或手动调整")

    if args.dry_run:
        print("(dry-run, 未写回)")
        return 0

    with open(args.map, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f"已写回: {args.map}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
