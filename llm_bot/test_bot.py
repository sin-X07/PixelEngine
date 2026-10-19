import urllib.request
import json
import sys

SYSTEM_PROMPT = """你是一个FPS游戏中的突击兵BOT。你必须直接输出战术决策，不要分析，不要解释。
严格输出JSON格式，不要输出其他任何文字：
{"action":"行动","target":"目标","shoot":"射击模式","reason":"简短理由"}

=== 核心规则（必须遵守）===
- 血量<30% → 必须选"防守"或"撤退"，绝不进攻
- 血量30%-70% → 可"掩体移动"或"防守"
- 血量>70%且敌人数≤1 → 可"进攻"
- 敌人数≥2且无队友 → 必须"撤退"
- 使用狙击枪 → shoot必须是"点射"，action不能是"进攻"
- 使用手枪/近战 → 优先"掩体移动"找掩护，不正面交火
- 敌人正对且距离<10米 → 可"连射"
- 敌人背对/侧对 → 优先"点射"，不惊动敌人

可用行动: 进攻, 防守, 撤退, 掩体移动
可用移动方向: 前方, 左前方, 右前方, 后退
射击模式: 连射, 点射, 停止射击"""

test_states = [
    "血量67/100，护甲50。武器AK47(弹药23/30)。位置仓库区。前方15米有敌人正在开火。队友在左后方8米。",
    "血量23/100，护甲0。武器手枪(弹药8/12)。位置开阔地。左侧5米有掩体。两个敌人在右前方20米。队友已阵亡。",
    "血量100/100，护甲100。武器狙击枪(弹药5/5)。位置高地。前方30米有敌人背对我方。队友在右侧10米。",
]

URL = "http://127.0.0.1:9931/v1/chat/completions"

for i, state in enumerate(test_states):
    payload = json.dumps({
        "model": "gpt-3.5-turbo",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": state}
        ],
        "max_tokens": 80,
        "temperature": 0.7
    }).encode("utf-8")

    req = urllib.request.Request(URL, data=payload, headers={
        "Content-Type": "application/json"
    })

    try:
        resp = urllib.request.urlopen(req, timeout=30)
        result = json.loads(resp.read().decode("utf-8"))
        content = result["choices"][0]["message"]["content"]
        print(f"\n{'='*60}")
        print(f"场景 {i+1}: {state}")
        print(f"BOT决策: {content}")
    except Exception as e:
        print(f"\n场景 {i+1} 请求失败: {e}")
        print("请确认 llama-server 已在另一个窗口运行!")
        sys.exit(1)

print(f"\n{'='*60}")
print("测试完成!")