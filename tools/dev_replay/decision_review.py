#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
decision_review.py —— 把「同一决策点：规则计划 vs LLM 计划 vs 实际执行」导出成可读的对照表

用法：
    # 先跑带决策日志的批次（--coach=llm 时自动写 <map>-s<seed>.decisions.csv）
    dev_replay_runner --coach=llm --ticks=150000 --maps=dust_2 --seeds=1-5 ...

    # 生成对照文档
    python tools/dev_replay/decision_review.py --dir=build-evidence/xxx --out=build-evidence/review.md

输出：
    * 每个决策点一行：时间/事件/规则计划/LLM 计划/延迟/状态/实际执行
    * 分堆统计：LLM 与规则不同 & 被采纳的回合，胜负如何
"""
import argparse
import csv
import glob
import os
import sys
from collections import Counter, defaultdict


def load_rounds(path):
    """round -> winner / plant / defuse（用于把决策和该回合结果对应起来）"""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for r in csv.DictReader(f):
            try:
                out[int(r["round"])] = r
            except (KeyError, ValueError):
                pass
    return out


def load_decisions(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return list(csv.DictReader(f))


def fmt_plan(intent, target, tempo, slots):
    t = f"{intent}"
    if target and target != "-":
        t += f"→{target}"
    if tempo:
        t += f"({tempo})"
    if slots:
        t += f" [{slots}]"
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True, help="包含 *.decisions.csv 的目录")
    ap.add_argument("--out", default="", help="输出 markdown 路径")
    ap.add_argument("--max-games", type=int, default=10)
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.dir, "*.decisions.csv")))
    if not files:
        print(f"[review] {args.dir} 下没有 decisions.csv", file=sys.stderr)
        return 1

    status_cnt = Counter()
    diff_same_round = defaultdict(lambda: Counter())   # status -> {llm_differs: 0/1}
    rounds_with_llm_diff = Counter()                   # (map,seed,round) -> 0/1
    round_result = {}                                  # (map,seed,round) -> round row
    lines = []
    total_rows = 0

    for f in files[: args.max_games]:
        base = os.path.basename(f)[: -len(".decisions.csv")]
        rounds_path = os.path.join(args.dir, base + ".rounds.csv")
        rounds = load_rounds(rounds_path) if os.path.exists(rounds_path) else {}
        rows = load_decisions(f)
        if not rows:
            continue

        lines.append(f"\n## {base}\n")
        lines.append("| # | 回合 | 时间 | 事件 | 队伍 | 规则计划 | LLM 计划 | 延迟 | 状态 | 实际执行 | 回合结果 |")
        lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")

        for i, r in enumerate(rows, 1):
            total_rows += 1
            status = r.get("status", "")
            status_cnt[status] += 1
            rule = fmt_plan(r.get("rule_intent", ""), r.get("rule_target", ""),
                            r.get("rule_tempo", ""), r.get("rule_slots", ""))
            llm = fmt_plan(r.get("llm_intent", ""), r.get("llm_target", ""),
                           r.get("llm_tempo", ""), r.get("llm_slots", ""))
            differs = (r.get("llm_intent", "") != "" and
                       (r.get("llm_intent") != r.get("rule_intent") or
                        r.get("llm_target") != r.get("rule_target") or
                        r.get("llm_slots") != r.get("rule_slots")))
            rnd = r.get("round", "?")
            rr = rounds.get(int(rnd)) if rnd.isdigit() else None
            result = ""
            if rr:
                result = f"{rr.get('winner','-')}胜"
                result += f" 下包{rr.get('plant','0')} 拆包{rr.get('defuse','0')}"
            key = (base, rnd)
            if differs and status in ("applied", "advisory"):
                rounds_with_llm_diff[key] = 1
            if rr:
                round_result[key] = rr
            lines.append(
                f"| {i} | {rnd} | {r.get('elapsed_s','')}s | {r.get('event','')} | "
                f"{r.get('team','')} | {rule} | {llm or '（无回包）'} | "
                f"{r.get('latency_s','')}s | {status} | {r.get('applied_intent','')}→"
                f"{r.get('applied_target','')} ({r.get('applied_source','')}) | {result} |")
            if r.get("rule_reason") or r.get("llm_reason"):
                lines.append(f"| ↳ | | | | 理由 | {r.get('rule_reason','')} | {r.get('llm_reason','')} | | | | |")

    # ---- 汇总 ----
    lines.append("\n## 状态汇总\n")
    lines.append("| 状态 | 次数 |")
    lines.append("| --- | --- |")
    for k, v in status_cnt.most_common():
        lines.append(f"| {k} | {v} |")

    diff_keys = set(rounds_with_llm_diff)
    if diff_keys:
        won = sum(1 for k in diff_keys
                  if round_result.get(k, {}).get("winner", "") not in ("", "-"))
        lines.append(f"\n**LLM 与规则给出不同计划的回合**：{len(diff_keys)} 个，"
                     f"其中分出胜负 {won} 个（需要结合上方逐条理由判断是否更好）。\n")

    text = "\n".join(lines)
    if args.out:
        os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"[review] {total_rows} 条决策 → {args.out}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
