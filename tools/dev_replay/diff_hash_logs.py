#!/usr/bin/env python3
"""diff_hash_logs.py —— 对比两份 PXHASH1 逐 tick 哈希日志。

用法：
    python tools/dev_replay/diff_hash_logs.py baseline.txt current.txt [--context 5]

输出：
    * 头部参数差异（地图/种子/视口/脚本策略）
    * 第一个不一致的 tick 及双方哈希
    * 该 tick 前后若干行上下文，便于人工核对

退出码：0 = 完全一致；1 = 存在差异；2 = 文件格式问题。
"""

import argparse
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def read_log(path):
    header = {}
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                for token in line.lstrip("# ").split():
                    if "=" in token:
                        k, v = token.split("=", 1)
                        header[k] = v
                continue
            parts = line.split()
            if len(parts) != 2:
                raise ValueError(f"{path}: 无法解析行: {line!r}")
            rows.append((int(parts[0]), parts[1].lower()))
    return header, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("current")
    ap.add_argument("--context", type=int, default=3)
    args = ap.parse_args()

    try:
        hb, rb = read_log(args.baseline)
        hc, rc = read_log(args.current)
    except Exception as exc:  # noqa: BLE001
        print(f"[diff] 读取失败: {exc}", file=sys.stderr)
        return 2

    # 头部参数差异：不同地图/种子/策略时，哈希本来就该不同，先提示
    param_diff = [(k, hb.get(k), hc.get(k)) for k in sorted(set(hb) | set(hc))
                  if hb.get(k) != hc.get(k)]
    if param_diff:
        print("[diff] 头部参数不一致（这种比较通常没有意义）：")
        for k, a, b in param_diff:
            print(f"       {k}: {a} -> {b}")
        print()

    print(f"[diff] baseline: {len(rb)} tick, current: {len(rc)} tick")
    limit = min(len(rb), len(rc))
    first = None
    mismatch_count = 0
    for i in range(limit):
        if rb[i][1] != rc[i][1]:
            if first is None:
                first = i
            mismatch_count += 1

    if first is None and len(rb) == len(rc):
        print("[diff] 完全一致 OK")
        return 0

    if first is not None:
        tb, vb = rb[first]
        tc, vc = rc[first]
        print(f"[diff] 首个不一致 tick={tb}（current tick={tc}）")
        print(f"       baseline={vb}")
        print(f"       current ={vc}")
        for i in range(max(0, first - args.context), min(limit, first + args.context + 1)):
            mark = "  <<<" if i == first else ""
            print(f"       {rb[i][0]:>7} {rb[i][1]} | {rc[i][1]}{mark}")
        if mismatch_count:
            print(f"[diff] 不一致 tick 总数: {mismatch_count} / {limit}")
        if len(rb) != len(rc):
            print(f"[diff] 长度不同（一方提前结束）: {len(rb)} vs {len(rc)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
