#!/usr/bin/env python3
"""compare_benchmark.py —— 对比两份 PXBENCH1 基准汇总，输出"变好还是退化"的初步判断。

用法：
    python tools/dev_replay/compare_benchmark.py baseline/summary.csv current/summary.csv

输出：胜场、平均回合数、平均回合时长、下包/拆包、玩家 K/D 的差异，
      并按阈值给出 WARN（需要人看）：胜场差 ≥3 局、回合时长变化 ≥20%。

退出码：0 = 无告警；1 = 有告警（供脚本判断，不阻塞）。
"""

import argparse
import csv
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def read_summary(path):
    meta, rows = {}, []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                for tok in line.lstrip("# ").split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        meta[k] = v
                continue
            if not line.strip():
                continue
            if not rows and line.lower().startswith("map,"):
                header = line.strip().split(",")
                continue
            values = line.strip().split(",")
            rows.append(dict(zip(header, values)))
    return meta, rows


def stats(rows):
    n = len(rows)
    if n == 0:
        return None
    t = sum(1 for r in rows if r["winner"] == "T")
    ct = sum(1 for r in rows if r["winner"] == "CT")
    rounds = sum(int(r["rounds"]) for r in rows) / n
    avg_round = sum(float(r["avg_round_ticks"]) for r in rows) / n
    plants = sum(int(r["plants"]) for r in rows) / n
    defuses = sum(int(r["defuses"]) for r in rows) / n
    pk = sum(int(r["player_kills"]) for r in rows)
    pd = sum(int(r["player_deaths"]) for r in rows)

    def avg(key, default=0.0):
        if key not in rows[0]:
            return default
        return sum(float(r[key]) for r in rows) / n

    return dict(n=n, t=t, ct=ct, rounds=rounds, avg_round=avg_round,
                plants=plants, defuses=defuses, pk=pk, pd=pd,
                still=avg("still_pct"), idle=avg("idle_pct"),
                path_eff=avg("path_eff"), damage=avg("dmg_per_round"),
                fb_win=avg("fb_win_pct"), plant_near=avg("plant_near"),
                defuse_near=avg("defuse_near"), conflicts=avg("ai_conflicts"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("current")
    args = ap.parse_args()

    mb, rb = read_summary(args.baseline)
    mc, rc = read_summary(args.current)
    sb, sc = stats(rb), stats(rc)
    if not sb or not sc:
        print("[compare] 汇总为空，无法比较", file=sys.stderr)
        return 2

    print(f"[compare] baseline tag={mb.get('tag', '?')}  games={sb['n']}")
    print(f"[compare] current  tag={mc.get('tag', '?')}  games={sc['n']}")

    # 参数不一致时提醒（不同脚本策略/视口的比较通常无意义）
    keys = ["ticks", "viewport", "aimTurn", "aimErr", "reaction"]
    diffs = [(k, mb.get(k), mc.get(k)) for k in keys if mb.get(k) != mc.get(k)]
    if diffs:
        print("[compare] ⚠ 参数不同（比较仅供参考）：")
        for k, a, b in diffs:
            print(f"          {k}: {a} -> {b}")

    warns = []

    def show(name, a, b, fmt="{:.2f}"):
        delta = b - a
        print(f"[compare] {name:<16} {fmt.format(a):>10} -> {fmt.format(b):>10}   "
              f"({delta:+.2f})")
        return delta

    print()
    # 用胜率比较：两次运行的样本数可能不同，直接比胜场会误报
    rt_b, rt_c = sb["t"] / sb["n"], sc["t"] / sc["n"]
    print(f"[compare] T 胜率 {rt_b:.1%} -> {rt_c:.1%} ({(rt_c - rt_b) * 100:+.1f}pp)"
          f"   [T {sb['t']}/{sb['n']} -> {sc['t']}/{sc['n']}]")
    if sb["n"] != sc["n"]:
        print(f"[compare] 注意：样本数不同（{sb['n']} vs {sc['n']}），已改用胜率比较")
    if abs(rt_c - rt_b) >= 0.15:
        warns.append(f"T 胜率变化 {(rt_c - rt_b) * 100:+.1f}pp（阈值 15pp）")

    d_rounds = show("平均回合数", sb["rounds"], sc["rounds"], "{:.1f}")
    d_avg = show("平均回合时长(tick)", sb["avg_round"], sc["avg_round"], "{:.0f}")
    show("下包/局", sb["plants"], sc["plants"], "{:.2f}")
    show("拆包/局", sb["defuses"], sc["defuses"], "{:.2f}")
    kd_b = sb["pk"] / max(1, sb["pd"])
    kd_c = sc["pk"] / max(1, sc["pd"])
    show("玩家 K/D", kd_b, kd_c, "{:.2f}")

    if sb["avg_round"] > 0 and abs(d_avg) / sb["avg_round"] >= 0.20:
        warns.append(f"平均回合时长变化 {d_avg / sb['avg_round'] * 100:+.0f}%（阈值 20%）")
    if abs(d_rounds) >= 2.0:
        warns.append(f"平均回合数变化 {d_rounds:+.1f}")

    # ---- 决策质量（BOT 行为直接相关的指标）----
    def quality(key, label, fmt, unit=""):
        if key not in sb or key not in sc:
            return None
        a, b = float(sb[key]), float(sc[key])
        d = b - a
        print(f"[compare] {label:<16} {fmt.format(a):>10}{unit} -> {fmt.format(b):>10}{unit}   "
              f"({d:+.2f})")
        return d

    if all(k in sb for k in ("still", "idle", "path_eff", "damage", "fb_win",
                             "plant_near", "defuse_near")):
        print()
        print("[compare] ---- 决策质量 ----")
        d_still = quality("still", "原地不动占比", "{:.1f}", "%")
        d_idle = quality("idle", "发呆占比", "{:.1f}", "%")
        d_eff = quality("path_eff", "路径效率", "{:.2f}")
        d_dmg = quality("damage", "每回合伤害", "{:.0f}")
        d_fb = quality("fb_win", "首杀转化率", "{:.1f}", "%")
        quality("plant_near", "下包点人数", "{:.2f}")
        quality("defuse_near", "拆包点人数", "{:.2f}")
        d_conf = quality("conflicts", "AI 冲突/局", "{:.2f}")

        # 质量阈值：这些是"行为退化"的早期信号
        if d_still is not None and d_still >= 10.0:
            warns.append(f"原地不动占比上升 {d_still:+.1f}pp（阈值 10pp）")
        if d_idle is not None and d_idle >= 10.0:
            warns.append(f"发呆占比上升 {d_idle:+.1f}pp（阈值 10pp）")
        if d_eff is not None and d_eff <= -0.10:
            warns.append(f"路径效率下降 {d_eff:+.2f}（阈值 0.10，越低越绕路）")
        if d_dmg is not None and sb["damage"] > 0 and d_dmg / max(1.0, sb["damage"]) <= -0.15:
            warns.append(f"每回合伤害下降 {d_dmg / sb['damage'] * 100:+.0f}%（阈值 15%）")
        if d_fb is not None and d_fb <= -10.0:
            warns.append(f"首杀转化率下降 {d_fb:+.1f}pp（阈值 10pp）")
        if d_conf is not None and d_conf > 0:
            warns.append(f"AI 冲突增加 {d_conf:+.2f}/局")

    print()
    if warns:
        print("[compare] WARN：以下变化需要人工复核")
        for w in warns:
            print("          - " + w)
        return 1
    print("[compare] OK：未触发阈值（不代表绝对强度，只说明与基线接近）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
