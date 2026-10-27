#!/usr/bin/env python3
"""校验 assets/config/coach 下各图战术库的区域名是否与 tmj 的 site 层一致。

用法:
    python tools/check_coach_playbooks.py
    python tools/check_coach_playbooks.py --assets assets

检查项:
  - 每张有 tmj 的地图是否有 <mapId>.txt；
  - 文件里的 [areas] 清单与 tmj site 组子图层是否一一对应（大小写精确）；
  - few-shot / 模板里 "area":"X" / area="X" 的引用是否在区域表内。
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Optional

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass


def tmj_areas(tmj_path: Path) -> set:
    data = json.loads(tmj_path.read_text(encoding="utf-8"))
    areas = set()

    def walk(layers, group: Optional[str] = None) -> None:
        for layer in layers:
            if layer.get("type") == "group":
                walk(layer.get("layers", []), layer.get("name"))
            elif group is not None:
                areas.add(layer.get("name", ""))

    walk(data.get("layers", []))
    return {a for a in areas if a}


def parse_manifest(text: str) -> list[str]:
    """读取 [areas] 段；空行和 # 注释跳过，遇到下一个 [section] 或 【标题】结束。"""
    lines = text.splitlines()
    out: list[str] = []
    in_areas = False
    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("["):
            in_areas = line in ("[areas]", "[area]")
            continue
        if not in_areas:
            continue
        if line.startswith("#") or line.startswith("【"):
            break
        line = line.split("#", 1)[0].strip()
        if not line or line.startswith("【"):
            break
        for token in line.replace(",", " ").replace(";", " ").split():
            if token:
                out.append(token)
    return out


def json_area_refs(text: str) -> set[str]:
    refs: set[str] = set()
    pos = 0
    while True:
        pos = text.find('"area"', pos)
        if pos < 0:
            break
        p = pos + len('"area"')
        while p < len(text) and text[p].isspace():
            p += 1
        if p >= len(text) or text[p] != ":":
            pos = p
            continue
        p += 1
        while p < len(text) and text[p].isspace():
            p += 1
        if p >= len(text) or text[p] not in "\"'":
            pos = p
            continue
        quote = text[p]
        p += 1
        end = text.find(quote, p)
        if end < 0:
            break
        refs.add(text[p:end])
        pos = end + 1
    return refs


def snake_tokens(text: str) -> set[str]:
    """正文里的 snake_case token（跳过 # 注释行与 intent/字段名）。"""
    skip = {
        "ttl_s", "retake_", "retake_a", "retake_b",
        "rush_", "rush_a", "rush_b", "split_", "split_a", "split_b",
        "stack_", "stack_a", "stack_b", "rotate_", "rotate_a", "rotate_b",
        "force_buy", "half_buy", "full_buy", "eco_buy",
    }
    out = set()
    for line in text.splitlines():
        if line.strip().startswith("#"):
            continue
        for token in re.findall(r"[A-Za-z0-9_]+", line):
            if "_" in token and token not in skip:
                out.add(token)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", default="assets", type=Path,
                        help="assets 目录（默认: assets）")
    args = parser.parse_args()

    maps_dir = args.assets / "cs2d" / "maps"
    coach_dir = args.assets / "config" / "coach"
    if not maps_dir.is_dir():
        print(f"[ERROR] 找不到地图目录: {maps_dir}", file=sys.stderr)
        return 2
    if not coach_dir.is_dir():
        print(f"[ERROR] 找不到战术库目录: {coach_dir}", file=sys.stderr)
        return 2

    maps = {p.stem: p for p in sorted(maps_dir.glob("*.tmj"))}
    if not maps:
        print(f"[ERROR] {maps_dir} 下没有 tmj", file=sys.stderr)
        return 2

    errors = 0
    for map_id, tmj in maps.items():
        areas = tmj_areas(tmj)
        if not areas:
            print(f"[SKIP] {map_id}: tmj 没有 site 组区域")
            continue

        playbook = coach_dir / f"{map_id}.txt"
        if not playbook.is_file():
            print(f"[WARN] {map_id}: 无专属战术库 {playbook}（运行时只用 _common.txt）")
            continue

        text = playbook.read_text(encoding="utf-8")
        declared = parse_manifest(text)
        if not declared:
            print(f"[ERROR] {map_id}: 缺少 [areas] 区域清单")
            errors += 1
            continue

        declared_set = set(declared)
        unknown = sorted(declared_set - areas)
        missing = sorted(areas - declared_set)
        duplicates = sorted({a for a in declared if declared.count(a) > 1})
        refs_unknown = sorted(json_area_refs(text) - areas)
        prose_unknown = sorted(snake_tokens(text) - areas)

        if unknown:
            errors += 1
            print(f"[ERROR] {map_id}: [areas] 含未知区域: {', '.join(unknown)}")
        if missing:
            errors += 1
            print(f"[ERROR] {map_id}: [areas] 漏掉区域: {', '.join(missing)}")
        if duplicates:
            errors += 1
            print(f"[ERROR] {map_id}: [areas] 有重复: {', '.join(duplicates)}")
        if refs_unknown:
            errors += 1
            print(f"[ERROR] {map_id}: 示例引用未知区域: {', '.join(refs_unknown)}")
        if prose_unknown:
            errors += 1
            print(f"[ERROR] {map_id}: 正文含未知区域: {', '.join(prose_unknown)}")

        if not (unknown or missing or duplicates or refs_unknown or prose_unknown):
            print(f"[OK] {map_id}: {len(declared)} 个区域，引用一致")

    if errors:
        print(f"\nFAIL: {errors} 个问题")
        return 1
    print("\nPASS: 全部地图战术库区域名一致")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
