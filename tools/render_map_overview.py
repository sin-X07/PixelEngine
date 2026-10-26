"""生成地图俯视图（碰撞 + 出生点 + 包点 + 区域），用于目视核对缩放效果。

用法: python tools/render_map_overview.py <map.tmj> <out.png>
"""
import json
import sys
import struct
import zlib


def read_layers(path):
    d = json.load(open(path, encoding='utf-8'))
    W, H = d['width'], d['height']
    tw = d.get('tilewidth', 8)
    th = d.get('tileheight', 8)
    grid = [[0] * W for _ in range(H)]
    groups = {}
    top = {}

    def walk(layers, group=None):
        for L in layers:
            t = L.get('type')
            if t == 'group':
                walk(L.get('layers', []), L.get('name'))
                continue
            if t != 'objectgroup':
                continue
            bucket = groups.setdefault(group, {}) if group else top
            key = L.get('name')
            bucket.setdefault(key, []).extend(L.get('objects', []))

    walk(d['layers'])

    for o in top.get('Collisions', []):
        rx, ry = float(o.get('x', 0)), float(o.get('y', 0))
        rw, rh = float(o.get('width', 0)), float(o.get('height', 0))
        if rw <= 0 or rh <= 0:
            continue
        c0, c1 = int(rx // tw), int((rx + rw - 0.001) // tw)
        r0, r1 = int(ry // th), int((ry + rh - 0.001) // th)
        for r in range(max(0, r0), min(H - 1, r1) + 1):
            for c in range(max(0, c0), min(W - 1, c1) + 1):
                grid[r][c] = 1
    return d, W, H, tw, th, grid, top, groups


def png_write(path, w, h, rgb_rows):
    raw = b''.join(b'\x00' + bytes(row) for row in rgb_rows)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def main():
    src, out = sys.argv[1], sys.argv[2]
    d, W, H, tw, th, grid, top, groups = read_layers(src)

    scale = max(1, 900 // max(W, H))
    pad = 8
    iw, ih = W * scale, H * scale
    Wp, Hp = iw + pad * 2, ih + pad * 2
    rows = [[16] * (Wp * 3) for _ in range(Hp)]

    def put(x, y, col):
        if 0 <= x < iw and 0 <= y < ih:
            r = rows[y + pad]
            r[(x + pad) * 3:(x + pad) * 3 + 3] = col

    # 碰撞
    for gy in range(H):
        for gx in range(W):
            if grid[gy][gx]:
                for dy in range(scale):
                    for dx in range(scale):
                        put(gx * scale + dx, gy * scale + dy, [235, 235, 235])

    def rect(o, col, fill=False):
        x, y = float(o.get('x', 0)), float(o.get('y', 0))
        w, h = float(o.get('width', 0)), float(o.get('height', 0))
        if w <= 0 or h <= 0:
            return
        x0, y0 = int(x / tw * scale), int(y / th * scale)
        x1, y1 = int((x + w) / tw * scale), int((y + h) / th * scale)
        if fill:
            for yy in range(y0, max(y0 + 1, y1)):
                for xx in range(x0, max(x0 + 1, x1)):
                    put(xx, yy, col)
        else:
            for xx in range(x0, max(x0 + 1, x1)):
                put(xx, y0, col)
                put(xx, max(y0, y1 - 1), col)
            for yy in range(y0, max(y0 + 1, y1)):
                put(x0, yy, col)
                put(max(x0, x1 - 1), yy, col)

    # 区域（组内对象层）
    for gname, layers in groups.items():
        for lname, objs in layers.items():
            for o in objs:
                rect(o, [120, 160, 110])
    # 包点
    for o in top.get('bomb_site', []):
        rect(o, [240, 190, 60], fill=True)
    # 出生点
    for o in top.get('spawn_t', []):
        rect(o, [235, 120, 100], fill=True)
    for o in top.get('spawn_ct', []):
        rect(o, [110, 170, 235], fill=True)
    # 购买区
    for o in top.get('buy_zone', []):
        rect(o, [200, 140, 235])

    png_write(out, Wp, Hp, rows)
    print(f'{src}: {W}x{H} tiles -> {out} ({Wp}x{Hp}, scale={scale})')
    print(f'  对象: collisions={len(top.get("Collisions", []))}, '
          f'bomb={len(top.get("bomb_site", []))}, '
          f'spawn_t={len(top.get("spawn_t", []))}, '
          f'spawn_ct={len(top.get("spawn_ct", []))}, '
          f'areas={sum(len(v) for v in groups.values()) if groups else 0}')


if __name__ == '__main__':
    main()
