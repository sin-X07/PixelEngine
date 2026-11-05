#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
eval_llm.py —— LLM 教练 vs 规则教练 的配对评估

用法：
    # 1) 规则组
    dev_replay_runner --coach=rule --ticks=40000 --maps=dust_2,mirage --seeds=1-4 --out=out/rule
    # 2) LLM 组（同一批 map/seed/ticks）
    dev_replay_runner --coach=llm  --ticks=40000 --maps=dust_2,mirage --seeds=1-4 --out=out/llm

    python tools/dev_replay/eval_llm.py --rule=out/rule --llm=out/llm

输出：
    * 配对指标（同 map+seed 的差值）
    * LLM 计划统计（意图分布 / 延迟 / 整形率）
    * LLM 相对规则的"计划分歧率"（同回合同队，首个计划是否不同）
"""
import argparse
import csv
import glob
import json
import os
import statistics
import sys


def load_summary(path):
    rows = []
    for f in glob.glob(os.path.join(path, "summary.csv")):
        with open(f, encoding="utf-8", errors="replace") as fh:
            lines = [l for l in fh if not l.lstrip().startswith("#")]
        for r in csv.DictReader(lines):
            rows.append(r)
    return rows


def load_plans(path):
    plans = []
    for f in glob.glob(os.path.join(path, "*.plans.csv")):
        base = os.path.basename(f)[: -len(".plans.csv")]
        # 文件名形如 mirage-s3
        if "-s" in base:
            map_name, seed = base.rsplit("-s", 1)
        else:
            map_name, seed = base, "0"
        with open(f, encoding="utf-8", errors="replace") as fh:
            for r in csv.DictReader(fh):
                r["_map"] = map_name
                r["_seed"] = seed
                plans.append(r)
    return plans


def parse_int(v, default=0):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return default


def parse_float(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def paired(rule_rows, llm_rows, key=("map", "seed")):
    a = {tuple(r[k] for k in key): r for r in rule_rows}
    b = {tuple(r[k] for k in key): r for r in llm_rows}
    pairs = [(a[k], b[k]) for k in sorted(set(a) & set(b))]
    return pairs


def wilson(wins, n, z=1.96):
    if n == 0:
        return (0.0, 0.0)
    p = wins / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    s = z * ((p * (1 - p) / n + z * z / (4 * n * n)) ** 0.5)
    return ((c - s) / d, (c + s) / d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rule", required=True, help="规则组输出目录")
    ap.add_argument("--llm", required=True, help="LLM 组输出目录")
    ap.add_argument("--json", default="", help="可选：把结果写成 JSON")
    args = ap.parse_args()

    rule_rows = load_summary(args.rule)
    llm_rows = load_summary(args.llm)
    if not rule_rows or not llm_rows:
        print("[eval] 缺少 summary.csv（检查 --out 目录）", file=sys.stderr)
        return 1

    pairs = paired(rule_rows, llm_rows)
    if not pairs:
        print("[eval] 两组没有共同的 (map, seed)，无法配对", file=sys.stderr)
        return 1

    print(f"[eval] 配对局数 = {len(pairs)}")
    print()

    def delta(field, pct=False):
        diffs = []
        for r, l in pairs:
            a, b = parse_float(r.get(field)), parse_float(l.get(field))
            diffs.append(b - a)
        mean = statistics.mean(diffs) if diffs else 0.0
        if pct:
            mean *= 1.0
        sd = statistics.pstdev(diffs) if len(diffs) > 1 else 0.0
        return mean, sd

    # ---- 配对结果 ----
    rule_t = sum(1 for r, _ in pairs if r.get("winner") == "T")
    llm_t = sum(1 for _, l in pairs if l.get("winner") == "T")
    lo_r, hi_r = wilson(rule_t, len(pairs))
    lo_l, hi_l = wilson(llm_t, len(pairs))

    print("== 结果（配对） ==")
    print(f"{'指标':<18}{'规则':>10}{'LLM':>10}{'Δ':>10}")
    print(f"{'T 胜率':<18}{rule_t/len(pairs):>9.1%}{llm_t/len(pairs):>10.1%}"
          f"{(llm_t-rule_t)/len(pairs):>+10.1%}   (95%CI 规则 {lo_r:.0%}~{hi_r:.0%} / LLM {lo_l:.0%}~{hi_l:.0%})")

    numeric = [
        ("rounds", "回合数", False),
        ("avg_round_ticks", "回合时长", False),
        ("plants", "下包/局", False),
        ("defuses", "拆包/局", False),
        ("dmg_per_round", "伤害/回合", False),
        ("fb_win_pct", "首杀转化%", False),
        ("still_pct", "原地不动%", False),
        ("idle_pct", "发呆%", False),
        ("path_eff", "路径效率", False),
        ("ai_conflicts", "AI冲突", False),
        ("dir_reversals", "方向反转", False),
    ]
    for field, label, _ in numeric:
        mean, sd = delta(field)
        ra = statistics.mean(parse_float(r.get(field)) for r, _ in pairs)
        la = statistics.mean(parse_float(l.get(field)) for _, l in pairs)
        print(f"{label:<18}{ra:>10.2f}{la:>10.2f}{mean:>+10.2f}")

    # ---- 计划统计 ----
    plans = load_plans(args.llm)
    rule_plans = load_plans(args.rule)
    # 去重：同一回合同一队重复下发的"同一计划"只算一次
    # （trace 里 gen 变化会导致重复行；按 round/team/intent/target/source 归一）
    def dedupe(plist):
        seen = set()
        out = []
        for p in plist:
            k = (p["_map"], p["_seed"], p["round"], p["team"],
                 p["intent"], p["target"], p["source"])
            if k in seen:
                continue
            seen.add(k)
            out.append(p)
        return out

    plans = dedupe(plans)
    rule_plans = dedupe(rule_plans)

    if plans:
        print()
        print("== LLM 计划（LLM 组，已按回合同队去重） ==")
        by_src = {}
        by_intent = {}
        lat = []
        reshaped = 0
        for p in plans:
            by_src[p["source"]] = by_src.get(p["source"], 0) + 1
            if p["source"] == "llm":
                by_intent[p["intent"]] = by_intent.get(p["intent"], 0) + 1
                lat.append(parse_float(p.get("latency_s")))
                reshaped += parse_int(p.get("reshaped"))
        total_llm = sum(by_intent.values())
        print(f"计划条数: 规则 {by_src.get('rule',0)} / LLM {total_llm}")
        if total_llm:
            print("LLM 意图分布: " + ", ".join(
                f"{k}={v}({v/total_llm:.0%})" for k, v in
                sorted(by_intent.items(), key=lambda kv: -kv[1])))
            print(f"整形改写率: {reshaped/total_llm:.0%}")
            if lat:
                lat_sorted = sorted(lat)
                p50 = lat_sorted[len(lat_sorted) // 2]
                p90 = lat_sorted[min(len(lat_sorted) - 1, int(len(lat_sorted) * 0.9))]
                print(f"LLM 延迟: 均值 {statistics.mean(lat):.2f}s  p50 {p50:.2f}s  "
                      f"p90 {p90:.2f}s  max {max(lat):.2f}s")

    # ---- 计划分歧率（同回合同队，第一次计划是否不同） ----
    def first_plans(plist):
        out = {}
        for p in plist:
            key = (p["_map"], p["_seed"], p["round"], p["team"])
            if key not in out:
                out[key] = (p["intent"], p["target"])
        return out

    fr, fl = first_plans(rule_plans), first_plans(plans)
    common = set(fr) & set(fl)
    if common:
        diff = sum(1 for k in common if fr[k] != fl[k])
        print()
        print(f"== 计划分歧 ==")
        print(f"可比回合×队伍: {len(common)}  规则与 LLM 首个计划不同: "
              f"{diff} ({diff/len(common):.0%})")

    if args.json:
        payload = {
            "pairs": len(pairs),
            "t_win_rule": rule_t / len(pairs),
            "t_win_llm": llm_t / len(pairs),
            "metrics": {f: delta(f)[0] for f, _, _ in numeric},
        }
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, ensure_ascii=False, indent=2)
        print(f"\n[eval] JSON → {args.json}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
