"""Plot the actual C++ ballistics exports produced by gameplay_tests --export-ballistics."""
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read_groups(path):
    groups = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            groups[int(row["id"])].append(row)
    return groups


def main():
    directory = Path(__file__).resolve().parents[1] / "build-evidence/pix34-round3/ballistics"
    curves = read_groups(directory / "recoil-curves.csv")
    impacts = read_groups(directory / "impact-samples.csv")
    parameters = read_groups(directory / "converted-parameters.csv")
    assert len(curves) == len(impacts) == len(parameters) == 34
    colors = ("#3176ab", "#d08028", "#448c69", "#a75d83")
    for weapon_id, rows in curves.items():
        name = rows[0]["name"]
        figure, axes = plt.subplots(1, 2, figsize=(11, 4.7), constrained_layout=True)
        for mode in (0, 1):
            selected = [row for row in rows if int(row["scope"]) == mode]
            x = [float(row["kick_x"]) * 1080 for row in selected]
            y = [-float(row["kick_y"]) * 1080 for row in selected]
            axes[0].plot(x, y, "o-", markersize=2.5, linewidth=1, label="Normal" if mode == 0 else "Alternate")
            for index in [index for index in (0, 9, 19, 29, 39) if index < len(selected)]:
                axes[0].annotate(str(index + 1), (x[index], y[index]), xytext=(3, 4), textcoords="offset points", fontsize=7)
        axes[0].set(title=f"{len(selected)}-shot recoil with per-weapon recovery", xlabel="Horizontal offset / aim distance × 1080", ylabel="Upward offset / aim distance × 1080")
        axes[0].axvline(0, color="#b7b7b7", linewidth=0.7)
        axes[0].legend(fontsize=8)
        for mode in (0, 1):
            for speed in (0, 1):
                selected = [row for row in impacts[weapon_id] if int(row["scope"]) == mode and int(row["speed"]) == speed]
                label = ("Normal" if mode == 0 else "Alternate") + (" moving" if speed else " stationary")
                color = colors[mode * 2 + speed]
                axes[1].scatter([float(row["forward"]) for row in selected], [float(row["lateral"]) for row in selected], s=5, alpha=0.4, color=color, label=label, rasterized=True)
        axes[1].set(title="512 independent first-shot samples per state", xlabel="Forward distance (world units)", ylabel="Lateral displacement (world units)")
        axes[1].legend(fontsize=8, loc="upper left")
        for axis in axes:
            axis.grid(alpha=0.18)
            axis.spines[["top", "right"]].set_visible(False)
        figure.suptitle(f"{name} · CS2 snapshot 04d3e4a · PixelEngine 2D adaptation", fontsize=12)
        figure.savefig(directory / f"{weapon_id:02d}-ballistics.png", dpi=150)
        plt.close(figure)
    print(f"Exported {len(curves)} weapon figures to {directory}")


if __name__ == "__main__":
    main()
