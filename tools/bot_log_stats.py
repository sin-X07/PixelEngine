"""从对局日志里提取 BOT 行为指标，用于重构前后对比。

用法: python tools/bot_log_stats.py <logfile> [<logfile2> ...]
"""
import re
import sys
from collections import Counter


def analyze(path):
    txt = open(path, encoding='utf-8', errors='replace').read()
    lines = txt.splitlines()

    rounds = len(re.findall(r'第 \d+ 回合开始', txt))
    kills = len(re.findall(r'BOT 阵亡|玩家阵亡', txt))
    plants = len(re.findall(r'C4 已安装', txt))
    defuses = len(re.findall(r'C4 已拆除', txt))
    detonations = len(re.findall(r'C4 爆炸', txt))
    drops = len(re.findall(r'\[Drop\]', txt))
    heard = len(re.findall(r'听到(下包|拆包)声', txt))
    arrived = len(re.findall(r'到达计划点', txt))
    stuck = len(re.findall(r'卡住|瞬移', txt))

    hold_ok = sum(int(x) for x in re.findall(r'holdOk=(\d+)', txt))
    hold_fail = sum(int(x) for x in re.findall(r'holdFail=(\d+)', txt))

    # 阵亡阵营分布
    teams = Counter(re.findall(r'BOT 阵亡 \((\w+)\)', txt))

    return {
        'file': path,
        'rounds': rounds, 'kills': kills,
        'plants': plants, 'defuses': defuses, 'detonations': detonations,
        'drops': drops, 'heard': heard, 'arrived': arrived,
        'stuck': stuck, 'holdOk': hold_ok, 'holdFail': hold_fail,
        'T_deaths': teams.get('T', 0), 'CT_deaths': teams.get('CT', 0),
    }


def main():
    files = sys.argv[1:]
    if not files:
        print('用法: python tools/bot_log_stats.py <logfile> [...]')
        return 1
    rows = [analyze(f) for f in files]
    keys = ['rounds', 'kills', 'plants', 'defuses', 'detonations', 'drops',
            'heard', 'arrived', 'stuck', 'holdOk', 'holdFail',
            'T_deaths', 'CT_deaths']
    w = max(len(k) for k in keys) + 2
    print('metric'.ljust(w) + ''.join(r['file'].split('/')[-1][:22].rjust(24)
                                      for r in rows))
    for k in keys:
        print(k.ljust(w) + ''.join(str(r[k]).rjust(24) for r in rows))

    for r in rows:
        if r['rounds']:
            print(f"\n{r['file']}: 每回合阵亡 {r['kills']/r['rounds']:.2f}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
