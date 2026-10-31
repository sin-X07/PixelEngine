"""为 BOT 角色精灵生成「贴合轮廓」的描边素材。

输入：assets/cs2d/sprites/{ct_player,t_player,t_player_alt}.png（2 列 × 3 行、单帧 32×32）
输出：同目录 *_outline.png —— 纯白描边、其余透明；运行时用 SDL_SetTextureColorMod
      按「友军=蓝 / 敌军=红 / 自己=白」着色，所以一份素材可以同时服务两种颜色。

做法：对 alpha 做膨胀（默认 2px，含对角），减去原始 alpha 得到外扩的一圈，
      再向外扩散 1px 做柔边，避免硬锯齿；描边不覆盖角色本体像素。

用法：python tools/gen_outline_sprites.py [--width 2]
"""
import argparse
import os
import sys

from PIL import Image, ImageFilter


def dilate(mask, radius):
    """方形结构元素的膨胀：radius=1 → 3x3，radius=2 → 5x5。"""
    out = mask
    for _ in range(radius):
        out = out.filter(ImageFilter.MaxFilter(3))
    return out


def make_outline(src_path, dst_path, width):
    img = Image.open(src_path).convert("RGBA")
    alpha = img.getchannel("A")
    # 阈值化，避免半透明像素把描边染脏
    binary = alpha.point(lambda v: 255 if v > 24 else 0)
    grown = dilate(binary, width)
    ring = Image.new("L", img.size, 0)
    ring_px = ring.load()
    g_px = grown.load()
    b_px = binary.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            if g_px[x, y] and not b_px[x, y]:
                ring_px[x, y] = 255
    # 不做额外柔边/外扩：需求是"只留一个像素点"，任何平滑都会把线加粗
    out = Image.new("RGBA", img.size, (255, 255, 255, 0))
    out_px = out.load()
    r_px = ring.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            if r_px[x, y]:
                out_px[x, y] = (255, 255, 255, 255)
    out.save(dst_path)
    # 统计一下覆盖情况，便于确认不是空图
    opaque = sum(1 for p in out.getdata() if p[3] > 0)
    print(f"  {os.path.basename(dst_path)}: {opaque} outline px")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=1, help="描边宽度（像素，默认 1）")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    sprite_dir = os.path.join(here, "..", "assets", "cs2d", "sprites")
    names = ["ct_player", "t_player", "t_player_alt"]
    print(f"生成描边素材：width={args.width}px → {os.path.normpath(sprite_dir)}")
    for name in names:
        src = os.path.join(sprite_dir, name + ".png")
        if not os.path.exists(src):
            print(f"  跳过（缺文件）：{src}", file=sys.stderr)
            continue
        make_outline(src, os.path.join(sprite_dir, name + "_outline.png"), args.width)


if __name__ == "__main__":
    main()
