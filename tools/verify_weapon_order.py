"""校验 WeaponId 枚举顺序与 kWeapons 数组顺序严格一致。

Get() 是按索引查表的（idx = static_cast<int>(id)），一旦顺序错位，
所有武器都会返回错误的定义，而且不会报编译错误。
"""
import re
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HPP = os.path.join(ROOT, 'PixelEngine/CS2D/Weapons/WeaponDef.hpp')


def main():
    src = open(HPP, encoding='utf-8').read()

    # 1) 抓枚举：enum class WeaponId : int { ... };
    m = re.search(r'enum class WeaponId\s*:\s*int\s*\{(.*?)\};', src, re.S)
    if not m:
        print('!! 找不到 WeaponId 枚举')
        return 1
    enum_names = []
    for line in m.group(1).split(','):
        name = line.strip()
        name = re.sub(r'=.*', '', name).strip()      # 去掉显式赋值
        name = re.sub(r'//.*', '', name).strip()
        if not name:
            continue
        enum_names.append(name)

    # 2) 抓数组：kWeapons[] = { ... };  每条形如 { WeaponId::X, ... }
    arr = re.search(r'inline const WeaponDef kWeapons\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not arr:
        print('!! 找不到 kWeapons 数组')
        return 1
    arr_names = re.findall(r'\{\s*WeaponId::(\w+)\s*,', arr.group(1))

    print(f'枚举项数: {len(enum_names)}')
    print(f'数组项数: {len(arr_names)}')
    print(f'Count 位置检查: 枚举末项 = {enum_names[-1]}')

    # 枚举最后一项应是 Count（不参与数组）
    if enum_names[-1] == 'Count':
        enum_names = enum_names[:-1]

    ok = True
    if len(enum_names) != len(arr_names):
        print(f'!! 数量不一致：枚举 {len(enum_names)} vs 数组 {len(arr_names)}')
        ok = False

    for i, (e, a) in enumerate(zip(enum_names, arr_names)):
        if e != a:
            print(f'!! 第 {i} 项错位：枚举={e}  数组={a}')
            ok = False

    if ok:
        print(f'OK: {len(enum_names)} 项顺序完全一致，Get() 索引查表安全')
        return 0
    return 1


if __name__ == '__main__':
    sys.exit(main())
