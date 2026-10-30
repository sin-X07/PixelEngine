# -*- coding: utf-8 -*-
"""从 FPS Icons (CC-BY 3.0, David Stenfors) 生成投掷物图标。

输入：prev_assets/grenade_icons/FPS Icons/FPS Icons/
输出：assets/cs2d/sprites/grenade_<type>.png      (64x64，世界里/手里)
      assets/cs2d/sprites/grenade_<type>_d.png   (128x128，掉落物/HUD)

原始图标是纯白剪影，这里补上：
  - 每种投掷物的主色
  - 左上高光 + 右下压暗的简单立体感
  - 深色描边
让它们在深色地图上也能看清轮廓，并和项目里其他 2D 素材风格接近。
没有对应造型的（燃烧瓶/燃烧弹/诱饵）用相近图标 + 主色区分。
"""
import os
from PIL import Image, ImageFilter

SRC = r"D:\BJTU\Cpp\Outstanding Projects\prev_assets\grenade_icons\FPS Icons\FPS Icons"
# type -> (icon, base color)
TYPES = {
    "he":         ("hand_grenade_icon_1.png", (110, 122, 62)),
    "flash":      ("flashbang_icon_2.png",    (186, 192, 205)),
    "smoke":      ("smoke_grenade_icon_1.png", (148, 156, 164)),
    "incendiary": ("hand_grenade_icon_4.png", (196, 84, 52)),
    "molotov":    ("smoke_grenade_icon_2.png", (208, 148, 62)),
    "decoy":      ("remote_icon_1.png",       (84, 178, 150)),
}

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "assets", "cs2d", "sprites")


def build(src_path, base, size):
    im = Image.open(src_path).convert("RGBA")
    bbox = im.getbbox()
    if bbox:
        im = im.crop(bbox)

    # 统一在 256 的画布上做立体感，再缩到目标尺寸
    work = 256
    inner = int(work * 0.82)
    im.thumbnail((inner, inner), Image.LANCZOS)
    canvas = Image.new("RGBA", (work, work), (0, 0, 0, 0))
    canvas.paste(im, ((work - im.width) // 2, (work - im.height) // 2), im)

    mask = canvas.getchannel("A").point(lambda a: 255 if a > 16 else 0)
    dilated = mask.filter(ImageFilter.MaxFilter(7))   # 描边宽度
    outline = Image.composite(
        Image.new("RGBA", (work, work), (22, 24, 28, 255)),
        Image.new("RGBA", (work, work), (0, 0, 0, 0)),
        dilated)

    body = Image.new("RGBA", (work, work), (0, 0, 0, 0))
    px = body.load()
    w, h = work, work
    cx, cy = w * 0.36, h * 0.30            # 高光中心
    maxd = (w * w + h * h) ** 0.5 * 0.55
    for y in range(h):
        for x in range(w):
            if mask.getpixel((x, y)) == 0:
                continue
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / maxd
            shade = max(0.35, 1.05 - 0.55 * d)          # 左上亮、右下暗
            shade *= 0.9 + 0.2 * (1.0 - y / h)
            px[x, y] = (min(255, int(base[0] * shade)),
                        min(255, int(base[1] * shade)),
                        min(255, int(base[2] * shade)), 255)

    out = Image.new("RGBA", (work, work), (0, 0, 0, 0))
    out.alpha_composite(outline)
    out.alpha_composite(body)
    return out.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, (src, base) in TYPES.items():
        src_path = os.path.join(SRC, src)
        if not os.path.exists(src_path):
            print("MISSING", src_path)
            continue
        build(src_path, base, 64).save(os.path.join(OUT, f"grenade_{name}.png"))
        build(src_path, base, 128).save(os.path.join(OUT, f"grenade_{name}_d.png"))
        print("wrote", name, "from", src)


if __name__ == "__main__":
    main()
