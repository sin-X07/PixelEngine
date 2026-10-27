"""为 WeaponDef.hpp 中声明但缺失的武器贴图生成占位 PNG。

占位风格与项目现有素材一致：小尺寸、纯色剪影、透明背景。
生成后可直接用真实素材覆盖同名文件，无需改代码。

用法:
    python tools/gen_weapon_placeholders.py            # 生成缺失的
    python tools/gen_weapon_placeholders.py --all      # 全部重生成（会覆盖已有！）
    python tools/gen_weapon_placeholders.py --dry-run  # 只看缺哪些
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITE_DIR = os.path.join(ROOT, 'assets', 'cs2d', 'sprites')

# 按武器类别决定占位色与形状（世界贴图）
CLASS_STYLE = {
    'Pistol':     ((180, 180, 190), 'pistol'),
    'Rifle':      ((150, 200, 140), 'rifle'),
    'Sniper':     ((210, 160, 120), 'sniper'),
    'Shotgun':    ((210, 140, 140), 'shotgun'),
    'MachineGun': ((190, 150, 200), 'mg'),
    'Grenade':    ((160, 200, 160), 'nade'),
    'ZeusGun':    ((240, 220, 120), 'pistol'),
}

# 世界贴图尺寸（宽 x 高）。参考现有素材：手枪约 24x12，步枪约 32x10。
WORLD_SIZE = {
    'pistol':  (26, 14),
    'rifle':   (34, 11),
    'sniper':  (40, 11),
    'shotgun': (34, 11),
    'mg':      (38, 13),
    'nade':    (12, 12),
}

# HUD 图标尺寸（方形，便于缩放到 44px 面板）
ICON_SIZE = (64, 64)


def parse_weapons():
    """从 WeaponDef.hpp 解析出 (spritePath, iconPath, cls) 三元组。

    注意：必须**逐条**武器定义解析。之前用跨行正则 (re.S) 会越过条目边界，
    把下一条的 sprite 当成本条的 icon，进而误判「缺失」并覆盖已有素材。
    这里先按 '{' ... '}' 切出每条定义，再在单条内抓取。
    """
    src = open(os.path.join(ROOT, 'PixelEngine/CS2D/Weapons/WeaponDef.hpp'),
               encoding='utf-8').read()

    results = []
    # 每条武器定义形如：  { WeaponId::X, "名", WeaponClass::C, ... },
    for entry in re.finditer(r'\{[^{}]*\}', src):
        body = entry.group(0)
        if 'WeaponClass::' not in body:
            continue
        cls_m = re.search(r'WeaponClass::(\w+)', body)
        if not cls_m:
            continue
        cls = cls_m.group(1)

        # 单条内按出现顺序：第一个是 spritePath，第二个是 iconPath（或 nullptr）
        paths = re.findall(r'"(cs2d/sprites/[\w\-]+\.png)"', body)
        if not paths:
            continue
        sprite = paths[0]
        icon = paths[1] if len(paths) > 1 else None
        results.append((sprite, icon, cls))
    return results


def draw_world(cls, color):
    style = CLASS_STYLE.get(cls, ((170, 170, 170), 'rifle'))[1]
    w, h = WORLD_SIZE.get(style, (30, 12))
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    body = (color[0], color[1], color[2], 255)
    dark = (color[0] // 2, color[1] // 2, color[2] // 2, 255)

    if style == 'pistol':
        # 枪管 + 握把
        d.rectangle([2, h // 2 - 2, w - 3, h // 2 + 1], fill=body)
        d.rectangle([4, h // 2 + 1, 9, h - 2], fill=dark)
    elif style == 'nade':
        d.ellipse([1, 1, w - 2, h - 2], fill=body, outline=dark)
        d.rectangle([w // 2 - 2, 0, w // 2 + 2, 3], fill=dark)
    elif style == 'sniper':
        d.rectangle([1, h // 2 - 2, w - 2, h // 2 + 1], fill=body)
        # 瞄准镜
        d.rectangle([w // 2 - 6, 1, w // 2 + 6, 4], fill=dark)
        d.rectangle([w - 8, h // 2 + 1, w - 4, h - 1], fill=dark)
    elif style == 'shotgun':
        d.rectangle([1, h // 2 - 2, w - 2, h // 2 + 1], fill=body)
        d.rectangle([1, h // 2 + 1, w // 2, h // 2 + 3], fill=dark)
    elif style == 'mg':
        d.rectangle([1, h // 2 - 3, w - 2, h // 2 + 2], fill=body)
        d.rectangle([w // 3, h // 2 + 2, w // 3 + 8, h - 1], fill=dark)
    else:  # rifle
        d.rectangle([1, h // 2 - 2, w - 3, h // 2 + 1], fill=body)
        d.rectangle([w // 2, h // 2 + 1, w // 2 + 5, h - 1], fill=dark)

    return img


def draw_icon(cls, color):
    w, h = ICON_SIZE
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 底板
    d.rounded_rectangle([2, 2, w - 3, h - 3], radius=8,
                        fill=(24, 32, 42, 235), outline=(color[0], color[1], color[2], 200))

    # 放大版剪影
    body = (color[0], color[1], color[2], 255)
    dark = (color[0] // 2, color[1] // 2, color[2] // 2, 255)
    style = CLASS_STYLE.get(cls, ((170, 170, 170), 'rifle'))[1]

    if style == 'nade':
        d.ellipse([20, 16, 44, 40], fill=body, outline=dark)
        d.rectangle([30, 10, 34, 18], fill=dark)
    else:
        # 通用：一条粗横杆 + 弹匣
        d.rectangle([10, 28, 54, 36], fill=body)
        d.rectangle([22, 36, 32, 50], fill=dark)
        if style in ('sniper', 'mg'):
            d.rectangle([16, 20, 48, 26], fill=dark)

    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true', help='全部重生成（覆盖已有素材）')
    ap.add_argument('--dry-run', action='store_true', help='只报告不写文件')
    args = ap.parse_args()

    weapons = parse_weapons()
    made = 0
    skipped = 0

    for sprite, icon, cls in weapons:
        color = CLASS_STYLE.get(cls, ((170, 170, 170), 'rifle'))[0]

        for rel, is_icon in ((sprite, False), (icon, True)):
            if not rel:
                continue
            out = os.path.join(ROOT, 'assets', rel)
            if os.path.exists(out) and not args.all:
                skipped += 1
                continue
            if args.dry_run:
                print('[would create]', rel)
                continue

            os.makedirs(os.path.dirname(out), exist_ok=True)
            img = draw_icon(cls, color) if is_icon else draw_world(cls, color)
            img.save(out)
            made += 1
            print('[created]', rel, img.size)

    print(f'\n完成：新建 {made} 个，跳过已有 {skipped} 个')


if __name__ == '__main__':
    sys.exit(main())
