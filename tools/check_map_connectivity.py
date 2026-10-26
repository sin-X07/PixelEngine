"""碰撞连通性检查：复刻 CS2DWorld 的栅格化规则，验证缩放前后
出生点 / 包点之间仍然连通，且走廊不会因吸附堵死。

用法: python tools/check_map_connectivity.py <map.tmj>
"""
import json
import sys
from collections import deque

TILE = 8          # tilewidth / tileheight
PLAYER = 7.0      # 玩家命中盒边长（CS2DGame.hpp: size = 7.0f）
AGENT_R = 4.0     # PathFinder 角色半径（Tuning.hpp）


def build_grid(path):
    d = json.load(open(path, encoding='utf-8'))
    W, H = d['width'], d['height']
    tw = d.get('tilewidth', TILE)
    th = d.get('tileheight', TILE)
    grid = [[0] * W for _ in range(H)]   # 0=空 1=实心

    def walk(layers):
        for L in layers:
            if L.get('type') == 'group':
                walk(L.get('layers', []))
                continue
            if L.get('name', '').lower() != 'collisions':
                continue
            for o in L.get('objects', []):
                rx, ry = float(o.get('x', 0)), float(o.get('y', 0))
                rw, rh = float(o.get('width', 0)), float(o.get('height', 0))
                if rw <= 0 or rh <= 0:
                    continue
                c0 = int(rx // tw)
                c1 = int((rx + rw - 0.001) // tw)
                r0 = int(ry // th)
                r1 = int((ry + rh - 0.001) // th)
                for r in range(max(0, r0), min(H - 1, r1) + 1):
                    for c in range(max(0, c0), min(W - 1, c1) + 1):
                        grid[r][c] = 1

    walk(d['layers'])

    def collect(layer_names):
        out = []

        def walk2(layers):
            for L in layers:
                if L.get('type') == 'group':
                    walk2(L.get('layers', []))
                    continue
                if L.get('name') in layer_names:
                    for o in L.get('objects', []):
                        out.append((float(o.get('x', 0)), float(o.get('y', 0)),
                                    float(o.get('width', 0)), float(o.get('height', 0))))
        walk2(d['layers'])
        return out

    spawns = collect({'spawn_t', 'spawn_ct'})
    bombs = collect({'bomb_site'})
    return d, W, H, tw, th, grid, spawns, bombs


def passable_grid(W, H, grid, radius):
    """按角色半径膨胀墙体，得到可通行格。"""
    # 以 8px 格为单位，radius 换算成格数（向上取整，保守）
    pad = max(0, int(radius // TILE))
    ok = [[0] * W for _ in range(H)]
    for r in range(H):
        for c in range(W):
            bad = False
            for dr in range(-pad, pad + 1):
                for dc in range(-pad, pad + 1):
                    rr, cc = r + dr, c + dc
                    if rr < 0 or rr >= H or cc < 0 or cc >= W or grid[rr][cc]:
                        bad = True
                        break
                if bad:
                    break
            ok[r][c] = 0 if bad else 1
    return ok


def center_tile(x, y, tw, th):
    return int(y // th), int(x // tw)


def bfs_reach(ok, W, H, start):
    if start is None:
        return set()
    seen = {start}
    q = deque([start])
    while q:
        r, c = q.popleft()
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            rr, cc = r + dr, c + dc
            if 0 <= rr < H and 0 <= cc < W and ok[rr][cc] and (rr, cc) not in seen:
                seen.add((rr, cc))
                q.append((rr, cc))
    return seen


def main():
    path = sys.argv[1]
    d, W, H, tw, th, grid, spawns, bombs = build_grid(path)
    ok = passable_grid(W, H, grid, AGENT_R)

    solid = sum(sum(row) for row in grid)
    passable = sum(sum(row) for row in ok)
    print(f'map={path}')
    print(f'  grid {W}x{H}, solid tiles={solid} ({solid/(W*H)*100:.1f}%), '
          f'passable={passable} ({passable/(W*H)*100:.1f}%)')

    # 连通分量
    comp = {}
    ncomp = 0
    sizes = []
    for r in range(H):
        for c in range(W):
            if ok[r][c] and (r, c) not in comp:
                ncomp += 1
                comps = bfs_reach(ok, W, H, (r, c))
                for t in comps:
                    comp[t] = ncomp
                sizes.append(len(comps))
    sizes.sort(reverse=True)
    print(f'  passable components={ncomp}, biggest={sizes[:3]}')

    # 出生点落点
    def probe(name, rects):
        pts = []
        for (x, y, w, h) in rects:
            if w == 0 and h == 0:      # 点对象
                rr, cc = center_tile(x, y, tw, th)
            else:
                rr, cc = center_tile(x + w / 2, y + h / 2, tw, th)
            inside = 0 <= rr < H and 0 <= cc < W
            pts.append((rr, cc, inside and bool(ok[rr][cc]),
                        comp.get((rr, cc))))
        return pts

    for label, rects in (('spawn', spawns), ('bombsite', bombs)):
        pts = probe(label, rects)
        bad = [p for p in pts if not p[2]]
        comps = sorted({p[3] for p in pts if p[3] is not None})
        print(f'  {label}: n={len(pts)}, on_passable={len(pts)-len(bad)}, '
              f'components={comps}')


if __name__ == '__main__':
    main()
