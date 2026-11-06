#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
lint_playbook.py —— 战术库"可执行性"校验

规则：
  * 每个 [card] 必须能被引擎字段完整表达（intent/target/tempo/ttl/slots）
  * slots 的 area / look / util 目标区域必须在 [areas] 里
  * role / hold / util 类型必须在枚举内
  * 不能执行的经验性句子必须写成 `# [advisory] ...`

用法：
  python tools/coach/lint_playbook.py assets/config/coach/dust_2.txt
  python tools/coach/lint_playbook.py --all
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
COACH = os.path.join(ROOT, "assets", "config", "coach")

INTENTS = {"default", "rush_a", "rush_b", "split_a", "split_b", "stack_a",
           "stack_b", "retake_a", "retake_b", "save", "fake", "rotate_a", "rotate_b"}
ROLES = {"Entry", "Support", "Lurk", "Anchor", "Awper"}
HOLDS = {"anchor", "offangle", "passive"}
UTILS = {"smoke", "flash", "molotov", "he"}
TARGETS = {"", "A", "B", "MID"}


def parse_areas(lines):
    areas = set()
    in_areas = False
    for line in lines:
        s = line.strip()
        if s.startswith("[areas]"):
            in_areas = True
            continue
        if in_areas:
            if s.startswith("[") or s.startswith("【"):
                break
            if s:
                areas.update(s.split())
    return areas


def parse_cards(lines):
    cards, cur = [], None
    for lineno, line in enumerate(lines, 1):
        s = line.rstrip("\n")
        m = re.match(r"^\[card\]\s+(.*)$", s.strip())
        if m:
            if cur:
                cards.append(cur)
            cur = {"id": "", "line": lineno, "when": "", "intent": "", "target": "",
                   "tempo": "", "ttl": "", "slots": [], "fallback": ""}
            for tok in m.group(1).split():
                if tok.startswith("id="):
                    cur["id"] = tok[3:]
            continue
        if cur is None:
            continue
        st = s.strip()
        if st.startswith("when:"):
            cur["when"] = st[5:].strip()
        elif st.startswith("intent:"):
            cur["intent"] = st[7:].strip()
        elif st.startswith("target:"):
            cur["target"] = st[7:].strip()
        elif st.startswith("tempo:"):
            cur["tempo"] = st[6:].strip()
        elif st.startswith("ttl:"):
            cur["ttl"] = st[4:].strip()
        elif st.startswith("fallback:"):
            cur["fallback"] = st[9:].strip()
        elif re.match(r"^\d\s", st):
            cur["slots"].append(st)
        elif st == "" or st.startswith("#"):
            if cur["slots"] and st == "":
                pass
    if cur:
        cards.append(cur)
    return cards


def lint_file(path):
    lines = io.open(path, encoding="utf-8").read().splitlines()
    areas = parse_areas(lines)
    cards = parse_cards(lines)
    errs = []
    advisory = sum(1 for l in lines if l.strip().startswith("# [advisory]"))

    for c in cards:
        cid = c["id"] or f"line{c['line']}"
        if not c["id"]:
            errs.append(f"{cid}: 缺少 id")
        if c["intent"] not in INTENTS:
            errs.append(f"{cid}: intent '{c['intent']}' 不在枚举内")
        if c["target"] not in TARGETS:
            errs.append(f"{cid}: target '{c['target']}' 非法")
        if not c["when"]:
            errs.append(f"{cid}: 缺少 when")
        if len(c["slots"]) == 0:
            errs.append(f"{cid}: 没有 slots")
        seen = set()
        for s in c["slots"]:
            parts = [p.strip() for p in s.split("|")]
            head = parts[0].split()
            if len(head) < 3:
                errs.append(f"{cid}: slot 头部应为 '序号 角色 区域' → {s}")
                continue
            idx, role, area = head[0], head[1], head[2]
            if not idx.isdigit() or not (0 <= int(idx) <= 4):
                errs.append(f"{cid}: slot 序号非法 {idx}")
            if int(idx) in seen:
                errs.append(f"{cid}: slot 序号重复 {idx}")
            seen.add(int(idx))
            if role not in ROLES:
                errs.append(f"{cid}: role '{role}' 非法")
            if area not in areas:
                errs.append(f"{cid}: area '{area}' 不在 [areas]")
            for kv in parts[1:]:
                if "=" not in kv:
                    continue
                k, v = kv.split("=", 1)
                k, v = k.strip(), v.strip()
                if k == "look" and v not in areas:
                    errs.append(f"{cid}: look 区域 '{v}' 不存在")
                elif k == "hold" and v not in HOLDS:
                    errs.append(f"{cid}: hold '{v}' 非法")
                elif k == "util":
                    kind = v.split(":", 1)[0]
                    if kind not in UTILS:
                        errs.append(f"{cid}: util 类型 '{kind}' 非法")
                    if ":" in v and v.split(":", 1)[1] not in areas:
                        errs.append(f"{cid}: util 目标 '{v.split(':',1)[1]}' 不存在")
    size = os.path.getsize(path)
    return {
        "file": os.path.basename(path), "bytes": size, "cards": len(cards),
        "advisory": advisory, "errors": errs,
        "est_tokens": int(size / 3 * 1.1),  # 中文约 3B/字 ≈ 1 token
    }


def main():
    if "--all" in sys.argv:
        files = []
        for f in ("_common.txt", "dust_2.txt", "mirage.txt", "ancient_ruins.txt"):
            p = os.path.join(COACH, f)
            if os.path.exists(p):
                files.append(p)
    else:
        files = [a for a in sys.argv[1:] if not a.startswith("--")]
    bad = 0
    total_cards = 0
    for p in files:
        r = lint_file(p)
        total_cards += r["cards"]
        status = "OK" if not r["errors"] else f"{len(r['errors'])} ERR"
        print(f"{r['file']:26s} {r['bytes']:6d} B  cards={r['cards']:2d}  "
              f"advisory={r['advisory']:2d}  ~{r['est_tokens']:4d} tok  {status}")
        for e in r["errors"][:12]:
            print("    -", e)
        bad += len(r["errors"])
    print(f"\n合计卡片 {total_cards}，错误 {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
