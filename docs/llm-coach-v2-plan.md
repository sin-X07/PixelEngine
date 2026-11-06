# LLM 教练 V2 计划：可执行战术库 + 计划落地 + 评测

> 状态：**待审核，未开工**。
> 前置：`docs/llm-coach-plan.md`（V1 五步，已完成）；本文档是它的续篇，重点解决
> "战术库大而不可执行"与"LLM 意图落不到 BOT 动作上"两个根本问题。

## 0. 目标与非目标

**目标**

1. 战术库保持"整本文档"注入（不做检索），用联网资料把每张图的战术补到"够用且可执行"。
2. 文档里**每一条战术都能一一映射到 BOT 的具体动作**，并且所需信息都能从局势里读出来。
3. 修掉决策日志暴露的问题（target 与 slots 脱节、守包/回防槽位散乱）。
4. 用现有工具做配对评测，给出"是否值得默认开启实时 LLM"的结论。

**非目标**

- 不做 RAG/向量检索（本阶段仍整本注入）。
- 不训练/微调模型。
- 不改 BOT 的基础战斗手感（瞄准、弹道、经济）——只改"教练→小队→BOT 目标/姿态"这条链。

## 1. 现状事实（已核实）

| 项 | 现状 |
| --- | --- |
| 战术库文件 | `assets/config/coach/_common.txt`（4,819 B）+ `dust_2/mirage/ancient_ruins.txt`（6.8–7.5 KB），合计 **11.7–12.3 KB/图** |
| 注入方式 | `LoadCoachPlaybook()` 拼接 common+map，按 `playbookId` 缓存，作为 system 前缀（字节稳定，命中 KV 缓存） |
| 截断 | `PE_COACH_PLAYBOOK` 默认 **4000 B** → `_common` 自身 4,819 B，**地图文件 0% 进入**；旧配置 8000 B 只进 42–46% |
| 计划结构 | `PlanIntent`（default/rush/split/stack/retake/save/rotate/fake）、`Tempo`、5×`SlotOrder{role,area,canTrade,canRotate}`、`ttl` |
| 槽位可执行字段 | 只有 `area`（去哪个区域）+ `role`（移速/补枪/远追）+ 两个布尔 |
| digest 已有 | 回合/比分/时间、炸弹状态、双方经济档与差值、我方区域分布+道具数、敌分布（heat）、敌人个别位置（按年龄）、上一条指令、笔记、事件、允许集 |
| digest 缺 | **敌方武器构成**、我方/敌方道具明细、控制区、队友存活位置（精确）、对方习惯（上回合主攻侧/首触）虽在 memory 但未进 digest |
| 视角朝向 | 战术表支持 `[look.<area>]`，`SetBotGoal` 会取第一个方向；守点扫描用它 |
| 实测问题 | 决策日志：180 条回合开始采纳里 **114 条只改 target、槽位不变**（=没落地）；下包后 LLM 把 5 人散到外围（规则是贴包交叉） |

## 2. 核心设计：把"散文"变成"可执行战术卡"

保留整本文档，但**重新组织成卡片**。每条卡 = 条件 + 载荷 + 回退，且**载荷字段必须能被引擎消费**。

### 2.1 卡片格式（写入同一批 .txt，人可读、机可解析）

```
[card] id=dust2_T_execute_a_long
when: 回合开始 & 我方满起 & (Long 已控 | 敌A≤1) & 剩余>45s
intent: rush_a            # 必须是 PlanIntent 枚举
target: A
tempo: fast
ttl: 14
slots:
  0 entry  Long       look=A_Car     util=flash:A_Site
  1 entry  Long_Door  look=Pit        util=smoke:CT_Cross
  2 support A_Site    look=CT_Spawn   hold=anchor
  3 support Pit       look=Long_Door  hold=offangle
  4 lurk   Cat        look=Short      hold=passive
fallback: default
```

### 2.2 字段 → 引擎 → BOT 动作 映射表（"1:1 可执行"的验收标准）

| 卡片字段 | 引擎落点 | BOT 实际动作 |
| --- | --- | --- |
| `intent` | `TeamPlan.intent` | 决定全队行为（rush/split/stack/retake/save 的推进与分配） |
| `target` | `TeamPlan.target` | C4 携带者跟随侧、转点判定 |
| `tempo` | `TeamPlan.tempo` | 移速/激进系数、推进节奏 |
| `slots[i].role` | `SlotOrder.role → SquadOrder.role` | 移速倍率（1.0/0.8/0.55）、补枪资格、远追许可 |
| `slots[i].area` | `SetBotGoal(area 中心)` | BOT 前往该区域并驻守 |
| `slots[i].look` | **新增** `SlotOrder.lookAngle → ai.lookAngle` | 到位后朝向该方向扫描（替代"面壁"） |
| `slots[i].hold` | **新增** `SlotOrder.holdStyle` | anchor=到位不轻易前压；offangle=侧身位；passive=不主动拉枪 |
| `slots[i].util` | **新增** `SlotOrder.utility` | 到位前/到位时投掷（需要落点校验，已有 `PlanGrenadeThrow`） |
| `canTrade/canRotate` | 已有 | 补枪/自行换点许可 |
| `fallback` | 解析失败时用规则模板 | 保证永远有可执行计划 |

**规则**：任何一行不能映射到上表的文字，必须：

1. 改写为可映射字段；或
2. 明确标注 `# [advisory]`（只作提示、不承诺执行），并在文档中列出清单；
3. 否则删除。

验收：`tools/coach/lint_playbook.py` 能解析全部卡片，输出"卡片数 / 不可执行行数 / 引用不存在区域数"，不可执行行必须为 0（advisory 除外，且单独计数）。

## 3. 阶段划分

### Phase A：计划模型扩展（引擎，0.5–1 天）

- `SlotOrder` 增加 `lookAngle`（float，弧度；-99=未指定）、`holdStyle`（Anchor/OffAngle/Passive）、`utility`（类型+目标区域）。
- 解析：战术表 `[slots.*]` 语法扩展（`Role|Area|look=Area|hold=offangle|util=flash:Area`）。
- 应用：`CoachApplyPlan` 把 look/hold/utility 写进 `BotRuntime`；`SetBotGoal` 用卡片的 look 覆盖默认敌方出生点朝向。
- LLM schema：`slots.items` 增加可选 `look` / `hold` / `util`（枚举 + 区域名校验），保持向后兼容。
- 兼容：缺省字段 = 现有行为。

**验收**：单测/`gameplay_tests` 通过；手写一张卡能观察到"朝向变化 + 投掷发生"。

### Phase B：digest 补齐（引擎，0.5 天）

目标：让卡片 `when:` 里出现的每个条件都能从 digest 读出来。

新增字段（短、稳定）：
- `敌枪: 步枪3/狙1/冲锋1`（由存活敌人武器槽统计，未知则标"未知"）
- `我道: 烟1闪2火0雷1`（我方存活者投掷物明细）
- `控制: A=2 B=1 M=2`（我方按区域人数，已有；改为三档摘要）
- `习惯: 上回合主攻A/首触Cat`（memory 摘要，已有数据未注入）
- `接触: Long(12s) Cat(30s)`（最近敌方接触点+年龄，已有 intel，压缩成一行）

**验收**：digest 单测/日志对照；LLM 回包 reason 能引用新字段（人工抽查 20 条）。

### Phase C：地图"育苗位/常规架点"数据（1–2 天，部分需你协助）

要兑现"知道哪里会来人、育苗位在哪"，需要每张图每个区域有：
`{区域, 观察方向, 常见来人入口, 推荐站位点(x,y), 类型: 架枪/侧身/补枪}`。

三种来源，建议组合：

| 来源 | 我能做 | 需要你 |
| --- | --- | --- |
| A. 几何自动推导：从区域中心向邻接区域射线，取可视入口方向 | ✅ 可做 | — |
| B. 战术表手填 `[look.<area>]` / `[angles.<area>]`（名称+坐标+角度） | ✅ 代码与格式我做 | ⚠️ 需要你确认坐标来源 |
| C. 游戏内"标记工具"：按 F 记录玩家当前位置/朝向为育苗位 | ✅ 可做（约 0.5 天） | 需要你进图站位、导出一份 csv |

**如果你希望"育苗位"是真人级别的（不是几何近似）**，需要你帮忙的只有一件事：
**进游戏在关键点位站一下、按标记键导出坐标**（我来做标记工具和导入）。否则我用 A 自动生成近似值。

### Phase D：战术库扩充（内容，1–2 天，联网取材）

结构（每张图，目标 18–24 KB，上限按最终体量定）：

1. `[areas]` 区域别名（已有）
2. `[look/angles]` 育苗位/架点表（Phase C 产出）
3. T 进攻：A/B 各 3–4 套 execute（长枪/短枪/道具开点）+ fake + 半起/强起/穷
4. T 下包后：每个包点 2 套守包（贴包交叉 / 退点等拆包）
5. CT 防守：2-1-2 / 3-1-1 / 双 AWP 等 3 套 setup + 各点 anchor/off-angle
6. CT 回防：每个包点 2 套（集结两路 / 分两翼）
7. 经济×意图矩阵、反 eco、anti-strat（对面 stack/快打/双狙的应对）
8. 残局（1v1/1v2/2v2）与时间阶段（>45s / 20–45s / <20s）
9. few-shot：每类 1–2 条（用真实区域名）

取材：CS2 主流战术站/职业比赛战术板（公开资料），**只取"能映射到本游戏动作"的部分**；
凡是依赖道具线、地图几何差异的内容，改写成本作可执行形式，不可执行就标 advisory 或删。

每个 `when:` 条件必须来自 digest 字段（Phase B 清单），不允许出现"如果对面很强"这种不可读条件。

**验收**：lint 通过（0 条不可执行）；每图卡片数 ≥ 25；抽 5 张实际跑对局验证动作发生。

### Phase E：引擎消费新字段 + 一致性校验（1–2 天）

1. `look`/`hold`/`util` 落到 BOT（Phase A 已建，这里补齐行为细节）。
2. **计划自洽校验**（修决策日志发现的问题）：
   - `target=A` 时，至少 N 个槽位在 A 侧；`target=MID` 时槽位含 Mid；否则拒绝或按 target 重建槽位。
   - 纯 target 变化（槽位不变）视为 no-op：不写日志"已采纳"，或自动补槽位。
3. **守包/回防模板**：`BombPlanted`/`DefuseStarted` 时槽位必须来自模板（贴包/两路），LLM 只允许改 tempo 与角色分配。
4. 校验失败时回退规则计划，并记 `invalid_consistency`。

**验收**：决策日志中"只改 target"比例 <5%；守包扩散到外围的案例为 0。

### Phase F：LLM 行为修复（沿用 V1 结论，0.5–1 天）

- 顾问模式扩大：守包/回防只记录，不执行（可配）。
- advisory 试点：无情报时的"控中"计划允许执行（开关 + A/B）。
- stale 闸门阈值按实测调（当前 2.5s，p90 3.5s）。
- 输出 token/手册上限重新设定（Phase D 完成后决定）。

### Phase G：评测（1–2 天，含跑局）

1. 规则 vs LLM 新配置：**30 局配对**（3 图 × seed 1–10，7B，`--jobs=1`）。
2. 工具全用：
   - `compare_benchmark.py`（回合级指标）
   - `eval_llm.py`（配对 + 决策级 + 延迟分布）
   - `decision_review.py`（逐条规则 vs LLM vs 执行）
   - `mcnemar.py`（配对显著性）
   - `gameplay_tests`（188）、同种子哈希（确定性）
3. 新增"人类相似度"代理指标：意图多样性（熵）、计划分歧率、对情报的反应（首触后换侧率）。
4. 结论门槛：若 T 胜率差 ≥ +5pp 且 p<0.1，才考虑默认开启实时 LLM；否则保持默认关闭。

### Phase H：风险与回滚

| 风险 | 缓解 |
| --- | --- |
| 战术库过大挤爆 prompt | 上限按最终体量定；超限按优先级裁剪并记录 |
| 新字段让 BOT 行为变差 | 全部新字段默认关闭/缺省回退；逐项开关 |
| 联网资料与本作地图不符 | 只写"区域可映射"的内容；lint 校验区域名 |
| 评测被小样本误导 | 配对 + 置信区间 + 多次 seed；结论门槛写死 |

## 4. 需要你决定/协助的事项

1. **育苗位数据来源**：接受"几何自动推导 + 少量手填"（快），还是要我做一个游戏内标记工具、你进图采点（准，需要你花 20–30 分钟）？
2. **计划模型扩展**：同意给 `SlotOrder` 加 `look/hold/util` 三个字段、并给 schema 加对应可选项吗？（这是"1:1 可执行"的前提）
3. **上限预算**：目标 18–24 KB/图（约 6–7k tokens），单槽 8192 仍够；是否允许把 `-c` 提到 12288 留余量？
4. **联网资料范围**：有没有指定来源（如 HLTV/某战术站），还是我自行选取公开资料并在 `SOURCES.md` 记录？
5. **是否允许为了采点新增一个 dev-only 工具**（`--angle-marker`，不影响发布版）？

## 5. 交付物清单

- `docs/llm-coach-v2-plan.md`（本文档）
- `assets/config/coach/*.txt`（扩充后的战术库，含卡片与育苗位表）
- `assets/config/tactics/<map>.ini`（look/angle 数据）
- `PixelEngine/CS2D/Coach/CoachTypes.hpp`、`TacticsTable.hpp`、`CS2DGameCoach*.cpp`（字段与校验）
- `tools/coach/lint_playbook.py`（可执行性校验）
- `tools/dev_replay/decision_review.py`（已有，扩展）
- 评测报告：更新 `tools/dev_replay/LLM_EVAL_RESULTS.md`

## 6. 时间估算（agent 工作量）

| 阶段 | 估时 |
| --- | --- |
| A 计划模型扩展 | 0.5–1 天 |
| B digest 补齐 | 0.5 天 |
| C 育苗位数据 | 1–2 天（含工具） |
| D 战术库扩充 | 1–2 天（含联网取材与 lint） |
| E 引擎消费 + 自洽校验 | 1–2 天 |
| F LLM 行为修复 | 0.5–1 天 |
| G 评测（含跑局） | 1–2 天 |
| **合计** | **约 6–9 个工作日**（跑局时间另计：30 局单串 ≈ 40 分钟） |


---

## 执行结果（2026-11-06，已完成）

| 阶段 | 状态 | 结果 |
| --- | --- | --- |
| A 计划模型扩展 | ✅ | `SlotOrder` 增加 `lookArea/lookAngle/hold/util/utilArea`，战术表语法扩展，BOT 消费朝向/站姿/投掷 |
| B digest 补齐 | ✅ | 敌枪构成、道具明细、近局习惯（主攻侧/首触/胜负） |
| C 育苗位数据 | ✅ | `gen_map_anchors.py` 从 TMJ 几何自动生成 29–35 区域 × 2 阵营的锚点与朝向（`assets/config/tactics/*.ini` 生成块） |
| D 战术库扩充 | ✅ | 42 张可执行卡片（每图 14 张），`lint_playbook.py` 0 错误；注入 11.3KB/图（默认上限提到 20000B，此前地图部分 0%） |
| E 引擎消费 + 自洽 | ✅ | target↔槽位校验、no-op 检测、下包模板、卡片 materialize 成 `[slots.*@Target]` |
| F LLM 行为修复 | ✅ | 顾问模式保留；`PE_COACH_ADVISORY_EXEC` 可放开；stale 闸保留 |
| G 评测 | ✅ | 30 局配对 + 12 局验证 + 决策日志 + lint/单测/哈希 |

### 关键数据

- 30 局：T 胜率 63.3% → 46.7%（McNemar p=0.227，**不显著**）；意图分布 default 41% / stack 29% / retake 22%
- 12 局（模板落地后）：58.3% vs 58.3%（打平）；伤害 +5.6，首杀转化 +5.1pp
- 计划拒绝：`invalid_consistency` 13%、硬规则 `invalid` 10%、advisory 10%

### 结论

战术库已成为"可执行文档"，LLM 的选择被显著改变；但**胜率仍未提升**。
后续重点：降低计划被拒率、验证 stack/retake 模板是否真的优于规则模板。

### 需要你确认的遗留项

1. 自动生成的锚点是几何近似，只给了 `hold` 显式声明的卡片使用；若你希望规则教练也用，需要人工采点校准。
2. 战术库还可以继续扩（当前 11.3KB，单槽 8192 下约剩一半预算），但本轮数据显示"扩内容"不是瓶颈，
   建议优先修"计划被拒/不落地"。
