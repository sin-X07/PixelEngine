#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
materialize_cards.py —— 把战术库里的 [card] 落成战术表模板（[slots.<Team>.<Intent>@<Target>]）

这样"文档里的战术"不仅对 LLM 是 few-shot，对规则教练也是可执行模板（LLM 关闭时也能用）。
同名键取第一个卡片（可通过在卡片上加 `prio=` 调整，默认按出现顺序）。
"""
import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
COACH = os.path.join(ROOT, "assets", "config", "coach")
TACTICS = os.path.join(ROOT, "assets", "config", "tactics")
MARK = "# --- materialized from coach cards (do not edit) ---"

INTENT_KEY = {
    "default": "Default", "rush_a": "RushA", "rush_b": "RushB",
    "split_a": "SplitA", "split_b": "SplitB", "stack_a": "StackA",
    "stack_b": "StackB", "retake_a": "RetakeA", "retake_b": "RetakeB",
    "save": "Save", "fake": "Fake",
}


def parse_cards(path):
    cards, cur = [], None
    for line in io.open(path, encoding="utf-8"):
        s = line.strip()
        m = re.match(r"^\[card\]\s+(.*)$", s)
        if m:
            if cur:
                cards.append(cur)
            cur = {"id": "", "intent": "", "target": "", "slots": []}
            for tok in m.group(1).split():
                if tok.startswith("id="):
                    cur["id"] = tok[3:]
            continue
        if cur is None:
            continue
        if s.startswith("intent:"):
            cur["intent"] = s.split(":", 1)[1].strip()
        elif s.startswith("target:"):
            cur["target"] = s.split(":", 1)[1].strip()
        elif re.match(r"^\d\s", s):
            cur["slots"].append(s)
    if cur:
        cards.append(cur)
    return cards


def team_of(card):
    cid = card["id"]
    if "_CT_" in cid:
        return "CT"
    if "_T_" in cid:
        return "T"
    return ""


def to_slot_line(line):
    """`0 Entry Long | look=A_Car | hold=anchor | util=flash:A_Site`
    → `Entry|Long|look=A_Car|hold=anchor|util=flash:A_Site`"""
    head, *rest = [p.strip() for p in line.split("|")]
    parts = head.split()
    if len(parts) < 3:
        return None
    role, area = parts[1], parts[2]
    return "|".join([role, area] + [r for r in rest if r])


def main():
    changed = 0
    for playbook in glob.glob(os.path.join(COACH, "*.txt")):
        map_id = os.path.basename(playbook)[:-4]
        if map_id.startswith("_"):
            continue
        ini = os.path.join(TACTICS, map_id + ".ini")
        if not os.path.exists(ini):
            continue
        cards = parse_cards(playbook)
        sections, seen = [], set()
        for c in cards:
            team = team_of(c)
            intent = INTENT_KEY.get(c["intent"])
            if not team or not intent or not c["slots"]:
                continue
            target = c["target"] or "-"
            key = f"{team}.{intent}@{target}"
            if key in seen:
                continue
            seen.add(key)
            lines = [f"[slots.{key}]"]
            for sl in c["slots"]:
                body = to_slot_line(sl)
                if not body:
                    continue
                idx = sl.split()[0]
                lines.append(f"{idx}={body}")
            sections.append("\n".join(lines))

        old = io.open(ini, encoding="utf-8").read() if os.path.exists(ini) else ""
        cut = old.find(MARK)
        if cut >= 0:
            old = old[:cut].rstrip() + "\n\n"
        block = "\n".join(sections)
        io.open(ini, "w", encoding="utf-8", newline="\n").write(old + MARK + "\n" + block + "\n")
        print(f"[ok] {map_id}: {len(sections)} templates → {ini}")
        changed += 1
    return 0 if changed else 1


if __name__ == "__main__":
    sys.exit(main())
