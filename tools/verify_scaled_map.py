"""缩放一致性校验：确认 mirage.tmj 的所有坐标都是原图除以 2 的结果。

用法: python tools/verify_scaled_map.py <original.tmj> <scaled.tmj> --div 2
"""
import json
import sys


def flatten(path):
    d = json.load(open(path, encoding='utf-8'))
    out = []

    def walk(layers):
        for L in layers:
            t = L.get('type')
            if t == 'group':
                walk(L.get('layers', []))
                continue
            if t == 'imagelayer':
                out.append(('imagelayer', L.get('name'),
                            None, L.get('imagewidth'), L.get('imageheight')))
                continue
            if t == 'objectgroup':
                for o in L.get('objects', []):
                    out.append((L.get('name'), o.get('id'),
                                (o.get('x', 0), o.get('y', 0),
                                 o.get('width', 0), o.get('height', 0)),
                                None, None))

    walk(d['layers'])
    return d, out


def main():
    a_path, b_path = sys.argv[1], sys.argv[2]
    div = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0

    da, la = flatten(a_path)
    db, lb = flatten(b_path)

    print(f'width : {da["width"]} -> {db["width"]}  (expect {da["width"]/div:.1f})')
    print(f'height: {da["height"]} -> {db["height"]}  (expect {da["height"]/div:.1f})')
    print(f'objects: {len(la)} -> {len(lb)}')

    if len(la) != len(lb):
        print('!! 对象数量不一致，无法逐项对比')
        return 1

    bad = []
    for i, (oa, ob) in enumerate(zip(la, lb)):
        if oa[0] != ob[0]:
            bad.append((i, 'layer name', oa[0], ob[0]))
            continue
        if oa[1] == 'imagelayer' or oa[2] is None:
            if oa[3] is not None:
                exp_w, exp_h = oa[3] / div, oa[4] / div
                if abs(ob[3] - exp_w) > 1.0 or abs(ob[4] - exp_h) > 1.0:
                    bad.append((i, 'image size', (oa[3], oa[4]), (ob[3], ob[4])))
            continue
        for k, name in enumerate(('x', 'y', 'w', 'h')):
            exp = float(oa[2][k]) / div
            got = float(ob[2][k])
            if abs(exp - got) > 0.051:
                bad.append((i, f'{oa[0]}#{oa[1]}.{name}', exp, got))

    if bad:
        print(f'!! {len(bad)} 处不一致:')
        for b in bad[:20]:
            print('   ', b)
        return 1

    print(f'OK: 全部 {len(la)} 个图层/对象坐标均为原图 /{div:.0f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
