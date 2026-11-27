"""对比修补前后的两张整图，确认只有蒙版附近的像素发生了变化。

用法：
  python tools/comfyui/check_stitch_diff.py before.png after.png [--out diff.png] [--threshold 0]

输出：变化像素数量、占比、变化区域外接框；--out 会保存一张差异图
（原图压暗，变化像素标红），方便确认没有别处被悄悄改动。
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("before", type=Path)
    ap.add_argument("after", type=Path)
    ap.add_argument("--out", type=Path, help="差异可视化 PNG")
    ap.add_argument("--threshold", type=int, default=0, help="单通道差值大于该值才算变化（默认 0 = 任何变化）")
    a = ap.parse_args()

    b = np.asarray(Image.open(a.before).convert("RGB"), dtype=np.int16)
    f = np.asarray(Image.open(a.after).convert("RGB"), dtype=np.int16)
    if b.shape != f.shape:
        raise SystemExit(f"尺寸不同：{b.shape[1]}x{b.shape[0]} vs {f.shape[1]}x{f.shape[0]}")

    changed = (np.abs(b - f).max(axis=2) > a.threshold)
    n = int(changed.sum())
    total = changed.size
    print(f"尺寸 {b.shape[1]}x{b.shape[0]} | 变化像素 {n} ({n / total:.3%})")
    if n:
        ys, xs = np.nonzero(changed)
        print(f"变化区域外接框 x={xs.min()}..{xs.max()}  y={ys.min()}..{ys.max()}  "
              f"({xs.max() - xs.min() + 1}x{ys.max() - ys.min() + 1})")
    else:
        print("两张图完全一致")

    if a.out:
        vis = (b * 0.4).astype(np.uint8)
        vis[changed] = (255, 40, 40)
        Image.fromarray(vis).save(a.out)
        print("差异图 ->", a.out)


if __name__ == "__main__":
    main()
