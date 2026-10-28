#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
占位素材生成器 — 保证「设计先于素材」。

原则:
  1. 幂等: 目标文件已存在则跳过。
  2. 绝不覆盖真实素材: 只有 tools/placeholders.json 里记录过的"占位"才可能被 --force 重建。
  3. 同名同路径: 用户真实素材到位后直接覆盖即可, 无需改代码。

用法:
  python tools/gen_placeholders.py            # 只补齐缺失的
  python tools/gen_placeholders.py --force    # 重建清单内的占位
"""

import argparse
import json
import math
import os
import struct
import wave

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "tools", "placeholders.json")

# ---------------------------------------------------------------------------
# 占位清单
# ---------------------------------------------------------------------------

# 武器/道具世界贴图: (相对路径, 标签, RGB)
WORLD_SPRITES = [
    ("assets/cs2d/sprites/glock.png",         "GLOCK",   (90,  90, 160)),
    ("assets/cs2d/sprites/galil.png",         "GALIL",   (120, 90,  60)),
    ("assets/cs2d/sprites/awp.png",           "AWP",     (60,  80, 110)),
    ("assets/cs2d/sprites/shotgun.png",       "SHOTGUN", (110, 70,  70)),
    ("assets/cs2d/sprites/grenade_he.png",    "HE",      (80, 120,  60)),
    ("assets/cs2d/sprites/grenade_flash.png", "FLASH",   (150,150,  60)),
    ("assets/cs2d/sprites/grenade_smoke.png", "SMOKE",   (100,100, 110)),
    ("assets/cs2d/sprites/c4.png",            "C4",      (140, 60,  60)),
    ("assets/cs2d/sprites/defuse_kit.png",    "KIT",     (60, 110, 120)),
]

# HUD 武器图标: (相对路径, 标签, RGB), 尺寸 75x64
HUD_ICONS = [
    ("assets/images/weapons/glock_d.png",         "GLOCK",   (90,  90, 160)),
    ("assets/images/weapons/galil_d.png",         "GALIL",   (120, 90,  60)),
    ("assets/images/weapons/awp_d.png",           "AWP",     (60,  80, 110)),
    ("assets/images/weapons/shotgun_d.png",       "SHOTGUN", (110, 70,  70)),
    ("assets/images/weapons/grenade_he_d.png",    "HE",      (80, 120,  60)),
    ("assets/images/weapons/grenade_flash_d.png", "FLASH",   (150,150,  60)),
    ("assets/images/weapons/grenade_smoke_d.png", "SMOKE",   (100,100, 110)),
]

# 音效: (相对路径, 基准频率 Hz, 时长秒)
SOUNDS = [
    ("assets/audio/glock.wav",          700,  0.10),
    ("assets/audio/shotgun.wav",        180,  0.22),
    ("assets/audio/grenade_explode.wav", 90,  0.60),
    ("assets/audio/flash.mp3",         1200,  0.35),
    ("assets/audio/smoke.mp3",          300,  0.50),
    ("assets/audio/bomb_plant.wav",     520,  0.20),
    ("assets/audio/bomb_defuse.wav",    440,  0.20),
    ("assets/audio/bomb_beep.wav",      900,  0.06),
    ("assets/audio/round_start.wav",    600,  0.25),
    ("assets/audio/round_win.wav",      780,  0.40),
    ("assets/audio/round_lose.wav",     260,  0.40),
    ("assets/audio/footstep.mp3",       200,  0.05),
    ("assets/audio/armor_hit.wav",      500,  0.08),
]

# Tileset: 3 列 x 22 行, 每块 32x32
TILESET_PATH = "assets/cs2d/tilesets/cs_office_tiles.png"
TILESET_COLS, TILESET_ROWS, TILE_SIZE = 3, 22, 32


# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

def abs_path(rel):
    return os.path.join(ROOT, rel)


def load_prev():
    if os.path.exists(MANIFEST):
        try:
            with open(MANIFEST, "r", encoding="utf-8") as f:
                return set(json.load(f).get("generated", []))
        except Exception:
            return set()
    return set()


def save_prev(paths):
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump({"generated": sorted(paths)}, f, ensure_ascii=False, indent=2)


def should_write(rel, force, prev):
    """只有缺失 / (force 且确属占位) 时才写。真实素材永不覆盖。"""
    p = abs_path(rel)
    if not os.path.exists(p):
        return True
    if force and rel in prev:
        return True
    return False


def ensure_dir(rel):
    d = os.path.dirname(abs_path(rel))
    if d:
        os.makedirs(d, exist_ok=True)


def get_font():
    try:
        return ImageFont.load_default()
    except Exception:
        return None


def draw_centered_text(draw, box, text, fill=(255, 255, 255)):
    """在 box=(x0,y0,x1,y1) 内居中绘制文字（用 anchor 原生居中，避免 bbox 偏移）。"""
    font = get_font()
    x0, y0, x1, y1 = box
    if font is None:
        return
    cx = (x0 + x1) * 0.5
    cy = (y0 + y1) * 0.5
    # 文字过宽时逐字截断，保证不溢出占位框
    max_w = (x1 - x0) - 4
    while len(text) > 1:
        try:
            bbox = draw.textbbox((0, 0), text, font=font)
            if bbox[2] - bbox[0] <= max_w:
                break
        except Exception:
            break
        text = text[:-1]
    try:
        draw.text((cx, cy), text, font=font, fill=fill, anchor="mm")
    except Exception:
        d2 = draw
        d2.text((x0 + 2, cy - 4), text, font=font, fill=fill)


# ---------------------------------------------------------------------------
# PNG 生成
# ---------------------------------------------------------------------------

def gen_weapon_sprite(rel, label, color, size=64):
    """生成朝上的武器占位: 枪身 + 枪管 + 标签。"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 枪身（位于下方，朝上伸出枪管）
    body_x0 = size * 0.34
    body_x1 = size * 0.66
    body_y0 = size * 0.38
    body_y1 = size * 0.86
    d.rounded_rectangle([body_x0, body_y0, body_x1, body_y1], radius=2,
                        fill=color + (255,), outline=(20, 20, 20, 255))

    # 枪管（指向正上方）
    barrel_x0 = size * 0.45
    barrel_x1 = size * 0.55
    barrel_y0 = size * 0.08
    barrel_y1 = size * 0.40
    d.rectangle([barrel_x0, barrel_y0, barrel_x1, barrel_y1],
                fill=color + (255,), outline=(20, 20, 20, 255))

    # 标签（枪身内）
    draw_centered_text(d, (0, size * 0.48, size, size * 0.80), label)

    ensure_dir(rel)
    img.save(abs_path(rel))


def gen_hud_icon(rel, label, color, w=75, h=64):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([1, 1, w - 2, h - 2], radius=4,
                        fill=color + (255,), outline=(20, 20, 20, 255))
    draw_centered_text(d, (0, 0, w, h), label)
    ensure_dir(rel)
    img.save(abs_path(rel))


def gen_tileset(rel):
    """3 列 x 22 行 x 32px 的网格占位图块集。"""
    w = TILESET_COLS * TILE_SIZE
    h = TILESET_ROWS * TILE_SIZE
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    total = TILESET_COLS * TILESET_ROWS
    for i in range(total):
        col = i % TILESET_COLS
        row = i // TILESET_COLS
        x0, y0 = col * TILE_SIZE, row * TILE_SIZE
        x1, y1 = x0 + TILE_SIZE, y0 + TILE_SIZE

        # 用序号生成稳定色相，避免整张图一片灰
        hue = (i * 37) % 360
        r, g, b = hsv_to_rgb(hue, 0.25, 0.55)
        d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=(r, g, b, 255),
                    outline=(40, 40, 40, 255))
        draw_centered_text(d, (x0, y0, x1, y1), str(i))

    ensure_dir(rel)
    img.save(abs_path(rel))


def hsv_to_rgb(h, s, v):
    h = h % 360
    c = v * s
    x = c * (1 - abs((h / 60.0) % 2 - 1))
    m = v - c
    if h < 60:      rp, gp, bp = c, x, 0
    elif h < 120:   rp, gp, bp = x, c, 0
    elif h < 180:   rp, gp, bp = 0, c, x
    elif h < 240:   rp, gp, bp = 0, x, c
    elif h < 300:   rp, gp, bp = x, 0, c
    else:           rp, gp, bp = c, 0, x
    return (int((rp + m) * 255), int((gp + m) * 255), int((bp + m) * 255))


# ---------------------------------------------------------------------------
# WAV 生成
# ---------------------------------------------------------------------------

def gen_wav(rel, freq_hz, duration_s, rate=22050, volume=0.35):
    """生成带淡入淡出的正弦提示音，避免爆音。"""
    n = max(1, int(rate * duration_s))
    frames = bytearray()
    for i in range(n):
        t = i / rate
        # 线性淡入淡出（各占 10%）
        env = 1.0
        fade = max(1, int(n * 0.10))
        if i < fade:
            env = i / fade
        elif i > n - fade:
            env = (n - i) / fade
        # 加一点二次谐波，听起来不那么刺耳
        sample = (math.sin(2 * math.pi * freq_hz * t) * 0.8
                  + math.sin(4 * math.pi * freq_hz * t) * 0.2)
        val = int(max(-1.0, min(1.0, sample * env * volume)) * 32767)
        frames += struct.pack("<h", val)

    ensure_dir(rel)
    with wave.open(abs_path(rel), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(bytes(frames))


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="生成占位素材")
    ap.add_argument("--force", action="store_true",
                    help="重建清单内已存在的占位（不覆盖真实素材）")
    args = ap.parse_args()

    prev = load_prev()
    generated = set(prev)
    created, skipped = [], []

    def do(rel, kind, fn):
        if should_write(rel, args.force, prev):
            fn()
            generated.add(rel)
            created.append((kind, rel))
        else:
            skipped.append((kind, rel))

    for rel, label, color in WORLD_SPRITES:
        do(rel, "sprite", lambda r=rel, l=label, c=color: gen_weapon_sprite(r, l, c))

    for rel, label, color in HUD_ICONS:
        do(rel, "icon", lambda r=rel, l=label, c=color: gen_hud_icon(r, l, c))

    do(TILESET_PATH, "tileset", lambda: gen_tileset(TILESET_PATH))

    for rel, freq, dur in SOUNDS:
        do(rel, "sound", lambda r=rel, f=freq, d=dur: gen_wav(r, f, d))

    save_prev(generated)

    print(f"占位素材: 新建 {len(created)} 项, 跳过 {len(skipped)} 项")
    for kind, rel in created:
        print(f"  [+] {kind:8} {rel}")
    if skipped:
        print("  (已存在，未改动)")
    print(f"清单: {os.path.relpath(MANIFEST, ROOT)}")


if __name__ == "__main__":
    main()
