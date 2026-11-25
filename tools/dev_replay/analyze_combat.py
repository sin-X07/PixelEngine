#!/usr/bin/env python3
"""Analyze a dev_replay combat log (P2-5).

Usage: python tools/dev_replay/analyze_combat.py <combat.csv> [<combat2.csv> ...]

Answers, per team and overall:
  * reaction-time distribution (ticks + seconds @60Hz) for first shots at a
    newly-seen target (shot rows with reaction_ticks >= 0)
  * shots, attributed hits, hit rate (heuristic: same-tick attribution;
    unattributed hit_taken rows are excluded from the rate, see the caveat)
  * reloads, empty-magazine episodes, empty->reloaded follow-through rate
"""
import csv
import sys
from collections import Counter, defaultdict


def percentile(vals, q):
    if not vals:
        return None
    vals = sorted(vals)
    if len(vals) == 1:
        return float(vals[0])
    pos = q * (len(vals) - 1)
    lo = int(pos)
    hi = min(lo + 1, len(vals) - 1)
    frac = pos - lo
    return vals[lo] * (1.0 - frac) + vals[hi] * frac


def fmt_pct(v):
    return "n/a" if v is None else "%.1f" % v


def main():
    paths = sys.argv[1:]
    if not paths:
        print("usage: analyze_combat.py <combat.csv> [<combat2.csv> ...]")
        return 2
    rows = []
    for path in paths:
        with open(path, newline="") as f:
            rows.extend(list(csv.DictReader(f)))
    if not rows:
        print("no rows in", ", ".join(paths))
        return 0

    print("files: %s  rows: %d" % (", ".join(paths), len(rows)))
    print("rounds seen: %d (ticks %s..%s)" % (
        len({r["round"] for r in rows}), rows[0]["tick"], rows[-1]["tick"]))

    by_team = defaultdict(list)
    shots = Counter()
    hit_dealt = Counter()
    hit_taken = Counter()
    hit_taken_unattr = Counter()
    reloads = Counter()
    empties = Counter()
    empty_reloaded = Counter()
    per_actor = defaultdict(Counter)

    for r in rows:
        team = r["team"]
        ev = r["event"]
        actor = r["actor"]
        per_actor[actor][ev] += 1
        if ev == "shot":
            shots[team] += 1
            rt = int(r["reaction_ticks"])
            if rt >= 0:
                by_team[team].append(rt)
        elif ev == "hit_dealt":
            hit_dealt[team] += 1
        elif ev == "hit_taken":
            hit_taken[team] += 1
            if r["detail"] == "unattributed":
                hit_taken_unattr[team] += 1
        elif ev == "reload_start":
            reloads[team] += 1
        elif ev == "empty":
            empties[team] += 1
        elif ev == "empty_reloaded":
            empty_reloaded[team] += 1

    print()
    print("== reaction time (first shot at a newly-seen target; ticks, 60 ticks = 1s) ==")
    for team in ("T", "CT"):
        vals = by_team.get(team, [])
        if not vals:
            print("  %s: no measured reactions" % team)
            continue
        print("  %s: n=%d median=%s p25=%s p75=%s p90=%s max=%d  (median %.2fs)" % (
            team, len(vals),
            fmt_pct(percentile(vals, 0.50)), fmt_pct(percentile(vals, 0.25)),
            fmt_pct(percentile(vals, 0.75)), fmt_pct(percentile(vals, 0.90)),
            max(vals), (percentile(vals, 0.50) or 0) / 60.0))
    all_vals = by_team.get("T", []) + by_team.get("CT", [])
    if all_vals:
        print("  ALL: n=%d median=%s p25=%s p75=%s p90=%s max=%d" % (
            len(all_vals), fmt_pct(percentile(all_vals, 0.50)),
            fmt_pct(percentile(all_vals, 0.25)), fmt_pct(percentile(all_vals, 0.75)),
            fmt_pct(percentile(all_vals, 0.90)), max(all_vals)))

    print()
    print("== shots / hits (hit rate uses attributed hit_dealt only) ==")
    print("  caveat: attribution is a same-tick heuristic (shooter fired this tick,")
    print("  LOS to victim, <800px). Unattributed hit_taken rows are NOT in the rate.")
    for team in ("T", "CT"):
        rate = (100.0 * hit_dealt[team] / shots[team]) if shots[team] else 0.0
        print("  %s: shots=%d hit_dealt=%d hit_rate=%.1f%% hit_taken=%d (unattributed %d)" % (
            team, shots[team], hit_dealt[team], rate,
            hit_taken[team], hit_taken_unattr[team]))

    print()
    print("== reloads / empty magazines ==")
    for team in ("T", "CT"):
        ft = (100.0 * empty_reloaded[team] / empties[team]) if empties[team] else 0.0
        print("  %s: reload_starts=%d empty_episodes=%d empty_reloaded=%d follow-through=%.1f%%" % (
            team, reloads[team], empties[team], empty_reloaded[team], ft))
    print("  empty->reloaded means: the actor hit 0 ammo (reserve>0) and later")
    print("  refilled via a reload. (Death-respawn refills do not count.)")

    print()
    print("== per actor (player = scripted player) ==")
    for actor in ["player"] + ["bot%d" % i for i in range(9)]:
        c = per_actor.get(actor)
        if not c:
            continue
        print("  %-7s shots=%-4d hit_dealt=%-3d hit_taken=%-3d reloads=%-3d empty=%-2d empty_reloaded=%d" % (
            actor, c.get("shot", 0), c.get("hit_dealt", 0), c.get("hit_taken", 0),
            c.get("reload_start", 0), c.get("empty", 0), c.get("empty_reloaded", 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
