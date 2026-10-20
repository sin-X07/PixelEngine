#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
占位地图生成器 — 生成符合 tools/ASSET_SPEC.md 对象层契约的 TMJ 地图。

产出:
  assets/cs2d/maps/de_5v5_placeholder.tmj   爆破模式 5v5
  assets/cs2d/maps/cs_hostage_placeholder.tmj 人质模式

幂等: 目标已存在则跳过 (--force 强制重写)。
"""

import argparse
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TILE = 8                 # 地图像素/tile
GROUND_GID = 1
WALL_GID = 2
TILESET_IMAGE = "../tilesets/cs_office_tiles.png"


# ---------------------------------------------------------------------------
# 通用构造
# ---------------------------------------------------------------------------

def tile_layer(name, grid):
    """由二维 grid 生成 tilelayer。"""
    h = len(grid)
    w = len(grid[0]) if h else 0
    data = [v for row in grid for v in row]
    return {
        "name": name,
        "type": "tilelayer",
        "visible": True,
        "opacity": 1,
        "x": 0, "y": 0,
        "width": w,
        "height": h,
        "data": data,
    }


def obj(otype, name, x, y, w=0, h=0, props=None):
    o = {
        "id": 0,
        "name": name,
        "type": otype,
        "x": x,
        "y": y,
        "width": w,
        "height": h,
        "rotation": 0,
        "visible": True,
        "opacity": 1,
    }
    if w == 0 and h == 0:
        o["point"] = True
    if props:
        o["properties"] = [
            {"name": k, "type": "string", "value": str(v)} for k, v in props.items()
        ]
    return o


def object_layer(name, objects):
    for i, o in enumerate(objects, start=1):
        o["id"] = i
    return {
        "name": name,
        "type": "objectgroup",
        "visible": True,
        "opacity": 1,
        "x": 0, "y": 0,
        "draworder": "topdown",
        "objects": objects,
    }


def merge_wall_rects(wall_grid, width, height):
    """贪心合并相邻墙体为尽量少的矩形（边界墙可并为 4 块）。"""
    remaining = {(c, r) for r in range(height) for c in range(width)
                 if wall_grid[r][c] == WALL_GID}
    rects = []
    for r in range(height):
        c = 0
        while c < width:
            if (c, r) not in remaining:
                c += 1
                continue
            # 横向延伸
            c2 = c
            while (c2 + 1, r) in remaining:
                c2 += 1
            # 纵向延伸（要求整行等宽仍全为墙）
            r2 = r
            while r2 + 1 < height and all((cc, r2 + 1) in remaining
                                          for cc in range(c, c2 + 1)):
                r2 += 1
            for rr in range(r, r2 + 1):
                for cc in range(c, c2 + 1):
                    remaining.discard((cc, rr))
            rects.append((c, r, c2 - c + 1, r2 - r + 1))
            c = c2 + 1
    return rects


def build_map(width, height, walls, entities, map_name):
    """walls: set((col,row)); entities: list[obj]  -> 完整 TMJ dict"""
    ground = [[GROUND_GID] * width for _ in range(height)]
    wall_grid = [[0] * width for _ in range(height)]
    for (c, r) in walls:
        if 0 <= c < width and 0 <= r < height:
            wall_grid[r][c] = WALL_GID

    # 碰撞矩形：贪心合并，避免每行一个对象导致文件膨胀
    collisions = [
        obj("Wall", "", c * TILE, r * TILE, w * TILE, h * TILE)
        for (c, r, w, h) in merge_wall_rects(wall_grid, width, height)
    ]

    return {
        "type": "map",
        "version": "1.10",
        "tiledversion": "1.12.2",
        "orientation": "orthogonal",
        "renderorder": "left-up",
        "infinite": False,
        "width": width,
        "height": height,
        "tilewidth": TILE,
        "tileheight": TILE,
        "nextlayerid": 8,
        "nextobjectid": 999,
        "tilesets": [{
            "firstgid": 1,
            "name": "cs_office_original",
            "image": TILESET_IMAGE,
            "imagewidth": 96,
            "imageheight": 704,
            "tilewidth": 32,
            "tileheight": 32,
            "tilecount": 66,
            "columns": 3,
            "margin": 0,
            "spacing": 0,
        }],
        "layers": [
            tile_layer("ground", ground),
            tile_layer("walls", wall_grid),
            object_layer("Collisions", collisions),
            object_layer("Entities", entities),
        ],
        "properties": [{"name": "map_name", "type": "string", "value": map_name}],
    }


def border_walls(width, height, thickness=1):
    s = set()
    for c in range(width):
        for t in range(thickness):
            s.add((c, t))
            s.add((c, height - 1 - t))
    for r in range(height):
        for t in range(thickness):
            s.add((t, r))
            s.add((width - 1 - t, r))
    return s


def rect_walls(c0, r0, c1, r1):
    """实心矩形墙块（含边界）。"""
    return {(c, r) for c in range(c0, c1 + 1) for r in range(r0, r1 + 1)}


def spawns(prefix, points):
    return [obj(prefix, "", x, y) for (x, y) in points]


# ---------------------------------------------------------------------------
# 爆破模式 5v5
# ---------------------------------------------------------------------------

def make_defusal_map():
    W = H = 100  # 100*8 = 800x800 世界像素
    walls = border_walls(W, H)

    # 掩体/隔断：营造 A/B 两路与中路
    walls |= rect_walls(46, 20, 53, 24)      # 上半中央块
    walls |= rect_walls(46, 74, 53, 78)      # 下半中央块
    walls |= rect_walls(24, 44, 28, 55)      # 左中隔断
    walls |= rect_walls(70, 44, 74, 55)      # 右中隔断
    walls |= rect_walls(36, 34, 42, 37)      # A 点外围
    walls |= rect_walls(58, 62, 64, 65)      # B 点外围

    entities = []
    # T 出生点（左下）
    entities += spawns("spawn_t", [
        (72, 700), (96, 716), (120, 700), (72, 736), (96, 744),
    ])
    # CT 出生点（右上）
    entities += spawns("spawn_ct", [
        (728, 72), (704, 88), (752, 96), (728, 112), (704, 120),
    ])
    # 包点 A（左上）与 B（右下）
    entities.append(obj("bomb_site", "A", 160, 96, 128, 128))
    entities.append(obj("bomb_site", "B", 512, 576, 128, 128))
    # 购买区
    entities.append(obj("buy_zone", "T", 40, 664, 152, 112, {"team": "t"}))
    entities.append(obj("buy_zone", "CT", 664, 40, 152, 96, {"team": "ct"}))

    return build_map(W, H, walls, entities, "de_5v5_placeholder")


# ---------------------------------------------------------------------------
# 人质模式
# ---------------------------------------------------------------------------

def make_hostage_map():
    W = H = 100
    walls = border_walls(W, H)

    walls |= rect_walls(30, 30, 34, 40)
    walls |= rect_walls(64, 30, 68, 40)
    walls |= rect_walls(30, 60, 34, 70)
    walls |= rect_walls(64, 60, 68, 70)
    walls |= rect_walls(44, 44, 56, 47)      # 中央掩体

    entities = []
    entities += spawns("spawn_ct", [
        (72, 700), (96, 716), (120, 700), (72, 736), (96, 744),
    ])
    entities += spawns("spawn_t", [
        (728, 72), (704, 88), (752, 96), (728, 112), (704, 120),
    ])
    # 4 名人质
    for i, (x, y) in enumerate([(240, 240), (400, 240), (240, 400), (400, 400)]):
        entities.append(obj("hostage", f"h{i}", x, y))
    entities.append(obj("buy_zone", "CT", 40, 664, 152, 112, {"team": "ct"}))
    entities.append(obj("buy_zone", "T", 664, 40, 152, 96, {"team": "t"}))

    return build_map(W, H, walls, entities, "cs_hostage_placeholder")


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

MAPS = [
    ("assets/cs2d/maps/de_5v5_placeholder.tmj", make_defusal_map),
    ("assets/cs2d/maps/cs_hostage_placeholder.tmj", make_hostage_map),
]


def main():
    ap = argparse.ArgumentParser(description="生成占位 TMJ 地图")
    ap.add_argument("--force", action="store_true", help="强制重写")
    args = ap.parse_args()

    for rel, factory in MAPS:
        path = os.path.join(ROOT, rel)
        if os.path.exists(path) and not args.force:
            print(f"  [skip] {rel} (已存在)")
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            # 紧凑 JSON：图块层数据体量大，缩进会让文件膨胀数倍
            json.dump(factory(), f, ensure_ascii=False, separators=(",", ":"))
        size_kb = os.path.getsize(path) / 1024.0
        print(f"  [+] {rel}  ({size_kb:.1f} KB)")


if __name__ == "__main__":
    main()
