"""列出 WeaponDef.hpp 中声明但磁盘上不存在的精灵贴图。"""
import re
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    src = open(os.path.join(ROOT, 'PixelEngine/CS2D/Weapons/WeaponDef.hpp'),
               encoding='utf-8').read()
    paths = sorted(set(re.findall(r'"(cs2d/sprites/[^"]+)"', src)))
    missing = []
    for p in paths:
        full = os.path.join(ROOT, 'assets', p)
        if not os.path.exists(full):
            missing.append(p)
    print('声明的贴图总数:', len(paths))
    print('缺失数量:', len(missing))
    for m in missing:
        print('  ', m)
    return missing


if __name__ == '__main__':
    main()
