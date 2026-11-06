#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_postplant.py —— 单独评估"下包后"这条线：规则 vs LLM（模板化守包/回防）

用法：
  python tools/dev_replay/analyze_postplant.py --rule=DIR --llm=DIR

输出：
  * 每个下包回合：该队伍最终是否赢、当时 LLM 计划状态
  * 分组胜率：下包后 LLM 采纳的守包/回防计划 vs 规则计划
"""
import argparse
import csv
import glob
import os
from collections import Counter, defaultdict


def load_rounds(d):
    out = {}
    for f in glob.glob(os.path.join(d, "*.rounds.csv")):
        base = os.path.basename(f)[: -len(".rounds.csv")]
        with open(f, encoding="utf-8", errors="replace") as fh:
            for r in csv.DictReader(fh):
                out[(base, r["round"])] = r
    return out


def load_decisions(d):
    out = defaultdict(list)
    for f in glob.glob(os.path.join(d, "*.decisions.csv")):
        base = os.path.basename(f)[: -len(".decisions.csv")]
        with open(f, encoding="utf-8", errors="replace") as fh:
            for r in csv.DictReader(fh):
                out[(base, r["round"])].append(r)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rule", required=True)
    ap.add_argument("--llm", required=True)
    args = ap.parse_args()

    rule_rounds = load_rounds(args.rule)
    llm_rounds = load_rounds(args.llm)
    llm_dec = load_decisions(args.llm)

    # 1) 下包回合总体胜率（按包点方）
    stats = Counter()
    for key, rr in rule_rounds.items():
        if int(rr.get("plant", 0)) > 0:
            stats["rule_planted_rounds"] += 1
            stats["rule_planted_win"] += int(rr.get("winner") == "T")
    for key, rr in llm_rounds.items():
        if int(rr.get("plant", 0)) > 0:
            stats["llm_planted_rounds"] += 1
            stats["llm_planted_win"] += int(rr.get("winner") == "T")

    print("== 下包回合（T 视角）==")
    for tag in ("rule", "llm"):
        n = stats[f"{tag}_planted_rounds"]
        w = stats[f"{tag}_planted_win"]
        if n:
            print(f"  {tag:5s} n={n:3d} T胜={w:3d} ({w/n:.0%})")

    # 2) LLM 下包后决策状态
    st = Counter()
    intents = Counter()
    for key, rows in llm_dec.items():
        for r in rows:
            if r["event"] != "bomb_planted":
                continue
            st[r["status"]] += 1
            if r["status"] == "applied":
                intents[r["llm_intent"]] += 1
    print("\n== LLM 下包后决策状态 ==")
    print("  ", dict(st))
    print("   applied intents:", dict(intents))

    # 3) 下包后"计划不同且被采纳"的回合胜率
    diff = Counter()
    for key, rows in llm_dec.items():
        rr = llm_rounds.get(key)
        if not rr or int(rr.get("plant", 0)) == 0:
            continue
        first = next((r for r in rows if r["event"] == "bomb_planted"), None)
        if not first:
            continue
        team = first["team"]
        if first["status"] == "applied":
            differs = (first["llm_intent"] != first["rule_intent"] or
                       first["llm_target"] != first["rule_target"] or
                       first["llm_slots"] != first["rule_slots"])
            k = "applied_diff" if differs else "applied_same"
            diff[k + "_n"] += 1
            diff[k + "_win"] += int(rr.get("winner") == team)
    print("\n== 下包后首个 LLM 决策 vs 回合结果（按该队）==")
    for k in ("applied_diff", "applied_same"):
        n = diff[k + "_n"]
        if n:
            print(f"  {k:12s} n={n:3d} 该队胜={diff[k+'_win']:3d} "
                  f"({diff[k+'_win']/n:.0%})")


if __name__ == "__main__":
    main()
