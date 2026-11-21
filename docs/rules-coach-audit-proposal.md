# 规则教练完善审计与改进提案

> 审计日期：2026-11-18。审计基准：Sun 电脑 live 工作树（HEAD ce41a76 + 未提交改动），
> 教练层文件当晚 sftp 拉取核实：`CS2DGameCoach.cpp`（1567 行）、`CS2DGameCoachAI.cpp`（1278 行）、
> `CS2DGameSquad.cpp`（746 行）、`Coach/CoachTypes.hpp`（135 行）、`Coach/TacticsTable.hpp`（241 行）、
> `TacticalIntel.*`、`CS2DGame.hpp`、`Tuning.hpp`、`assets/config/coach/*.txt`、`assets/config/tactics/*.ini`、
> `docs/llm-coach-v2-plan.md`。
> 对照 11 月 10 日快照：`CS2DGameCoachAI.cpp` 零改动；`CS2DGameCoach.cpp` 仅 2 处数值改动
> （保枪抖动 ±18→±36、转点携带者接近半径 140→280）；`CS2DGameSquad.cpp` 多了身体可见性系统
> （PIX-31 视野工作）；`TacticsTable.hpp` 多了 `[perception]` 难度表；战术手册零改动。
> 结论：教练决策逻辑自 11 月 10 日以来没动过，下面所有行号都是 live 行号。

## 0. 结论先行

规则教练的**管线是好的，决策是瘸的**。事件队列、合并窗口、8 次调用预算、LLM sticky、
T 18 秒执行锁、TTL 续期、转点证据门、买枪延迟等机制都很完整；但真正做决策的
`RuleCoachCore` 只用了手头信息的一小部分，而且有几处**数据已存在、代码却没接线**，
以及几处**未接线**。最要紧的三条：

1. **战术表 14 个 `@Target` 模板未被加载**：`materialize_cards.py` 把 42 张战术卡落成了
   `[slots.*@Target]` 段，但 `TacticsTable::Load` 从不读 `@` 段。每张图实际生效的模板只有
   4 个（T.Default、CT.Default、T.RushA、T.RushB）；Split / Fake / Save / Stack / Retake
   在任何路径下都没有表可用。这是 V2 评测里 13% `invalid_consistency` + 10% `invalid`
   拒绝率的一个直接成因。
2. **教师天花板**：规则教练在回合开始时只会发 Default / RushA / RushB / Save，
   永远发不出 Split / Fake，也没有 MID 控中意图。拿它当老师蒸馏，LoRA 数据集里就不会有
   这些战术——学生学不会老师不会的东西。
3. **四处未接线**（按 Sun 的原则：不删，全部接线）：`changesThisRound` 从不自增（每回合 2 次意图变更预算未生效）、
   `hasBranch` 永不设 true（整套"时间差打法"分支机制未接线）、`sameSideStreak` 从不读写、
   `SanitizeCoachPlan`（防重复压点、无情报不赌点）只走 LLM 路，不用于规则计划。

其余还有：规则教练不读情报热度（`intel_`）、CT 完全不用记忆、经济没有跨回合状态、
赛点没有战术、回防全员 Entry 且不认拆包钳、槽位按 BOT 下标顺序分（不看武器/技术）、
Lurk/Awper 在执行层坍缩成 Entry/Support。详见 §2，改进见 §4，待 Sun 定的事见 §6。

## 1. 现状盘点：链路与各模块

链路：事件（`CoachOnEvent`，0.4s 合并窗口，优先级 RoundStart 100 > BombPlanted 95 >
DefuseStarted 90 > HumanDied 85 > EnemyDown 80 > AllyDown 78 > EnemySpotted 70 >
MatchPoint 60 > TimeLow 55 > EconomyLow 40）→ `CoachUpdate`（每帧；转点评估每 0.75s；
计划到期续期/沉默 30s 重评估；心跳 8s）→ `CoachThink`（KEEP 逻辑、LLM sticky、T 执行锁、
变更预算）→ `RuleCoach`（`RuleCoachCore` + 战术表槽位覆盖）→ `CoachApplyPlan`
（槽位 → `SetBotGoal` + 角色/朝向/站姿/道具）→ `CS2DGameSquad`（`AssignSquadRoles`、
`RefreshRolesForTeam`、`UpdateSquadFocus` 集火）→ BOT 决策树。

| 模块 | 现状 | 评价 |
|---|---|---|
| 事件与调度 | 12 种事件全接线；TimeLow（剩 30s）/ MatchPoint（赛点）/ EconomyLow（开局低经济）各只发一次；每队每回合上限 8 次调用，KEEP 不计次 | 好，不动 |
| `RuleCoachCore`（CS2DGameCoach.cpp:512-756） | 见 §1.1 | **核心短板** |
| 经济（:1374-1474） | 快照均值 + 1800/2600/3500 分档；`ShouldSaveNow` 四重守卫（未下包、45s 内、非人数优势、未占包点）；AntiEco（我富敌穷 → SMG+甲）已接线到 `BotBuyLoadout` | 阈值合理，但无跨回合状态 |
| 转点（:1475-1566） | 证据门（heat 差 ≥1.2、情报 ≥2 条、剩 30s+、不少 2 人、8s 冷却、仅 Default 阶段、计划已执行 12s+、无人进包点/携带者不在 280 内）+ 中止逻辑（已下包/剩 25s/少 2 人 → 回原侧）；执行 2 人中路重组 + 3 人转推 | 好，是规则层最完整的自适应 |
| 消毒（:973-1096） | T 防重复压点（同侧连续 → 默认分兵）、T 防单点、CT 无情报不赌点（heat 差 <0.4 → 默认分兵） | 好，但**只走 LLM 路** |
| 执行（:1097-1286） | C4 携带者强制跟主攻侧；T 攻点目标用包点中心；保枪就地 ±36 抖动；锚点只用于显式 hold 的卡片 | 好，见 §2 槽位问题 |
| 战术表（TacticsTable.hpp） | 每图 12 个意图段（名义）+ look/hold/util/anchor/perception 数据丰富（util 14 条/图、look 70 条/图、hold ~50 条/图、anchor 58–70 个/图、perception 4 档） | 数据丰富，**加载有 bug（§2.1）** |
| 战术手册（coach/*.txt） | `_common` 65 行 + 每图 14 张卡（42 张），lint 0 错误；11.3KB/图注入 | 内容好，**只喂 LLM，不喂规则** |
| 记忆（RoundMemo，每队 10 回合） | plan/target/won/比分/存活/下包/首触/死亡列表全有；`CoachRecordRound` 每回合写 | 数据全，**规则只用其中一小部分** |
| 评测基建 | V2 已有：30 局配对基准（McNemar）、决策日志（规则/LLM/执行三并排）、plan trace、lint | 好，改动后可直接重跑 |

### 1.1 `RuleCoachCore` 现在到底会什么（回合开始 T/CT）

- 下包后：T 人少 → Default/slow 守包等拆包再抓；T 人多 → Stack 交叉火力守包；
  CT → Retake，`bombLeft ≤ 11s` 才 mustRush（11 = 10s 拆包 + 1s 余量）。
- 事件优先：BombPlanted → DefuseStarted → EnemyDown（Default/fast 前压）→
  AllyDown（Default/slow 收缩）→ EconomyLow+ShouldSaveNow → Save → TimeLow
  （T 按当前主攻侧 Rush，CT 收位守点）。
- 回合开始 T：只看**己方**分侧胜负——某侧 ≥2 次且 0 胜 → Rush 另一侧；否则 Default 分兵。
  CT：固定 2-2-1（Anchor A、Support AShort、Entry Mid、Anchor B、Support BLong），
  **完全不用记忆**。
- 全程**不读 `intel_`**（情报热度）、不读敌枪构成、不读道具明细、不读比分/赛点、
  不读连败/上回合买档。`BuildStateDigest` 给 LLM 的信息，规则教练自己几乎都不用。

## 2. 已核实的硬问题（按影响排序）

### 2.1 P0 — 战术表 `@Target` 模板未被加载（加载 bug）

- `TacticsTable::Load`（live 241 行版）只循环 11 个 plain 意图名拼
  `"slots." + team + "." + intent` 读段；`GetAll()` 循环只处理 `look.`/`anchor.` 前缀。
  文件里没有任何读 `@` 段的代码；`Slots()` 是精确 key 查找。
- 每张图 ini 里实际有 18 个 `[slots.]` 段：4 个 plain（T.Default、CT.Default、T.RushA、T.RushB，
  mirage.ini:13/20/27/34）+ 14 个 `@` 段（`T.Default@-`、`T.RushA@A`、`T.SplitA@A`、
  `T.Fake@B`、`T.Save@-`、`CT.StackA@A`、`CT.RetakeA@A` 等，mirage.ini:362-440）。
  14 个 `@` 段是 `tools/coach/materialize_cards.py` 从 42 张战术卡生成的。
- 后果：`ApplySlotTemplate` 的 `@Target` 优先查找永远 miss，只能退回 plain；
  Split / Fake / Save / Stack / Retake **在任何路径下都没有表**——规则侧
  `ApplyTacticsSlots`（只查 plain）拿不到，LLM 侧 `ApplySlotTemplate` 也拿不到。
  V2 评测的 13% `invalid_consistency` + 10% `invalid` 拒绝率，至少一部分是这个机制
  造成的（LLM 按卡片出了 Split/Fake，引擎却找不到对应的槽位模板）。
- 修复量：`Load` 里加一遍 `GetAll()` 扫 `slots.` 前缀的段（或把 `@` 段也登记进 `slots` map），
  约 20 行。修完立刻让 14 张卡/图生效。

### 2.2 P0 — 教师天花板：规则教练发不出 Split / Fake / MID

- `RuleCoachCore` 全文没有任何 `SplitA/SplitB/FakeThenOther` 赋值；回合开始 T 的意图
  只有 Default / RushA / RushB / Save；事件分支只有 Default；TimeLow 只有 Rush。
  Rotate 只由 `CoachTryRotate` 发（不在 `RuleCoachCore` 内）。
- `target == "MID"` 没有任何规则分支会设；`PlanTargetConsistent` 虽然支持 'M'，
  但没有上游会产生它。
- 对 LoRA 的直接影响：`coach_rounds.jsonl` 的老师数据里，Split/Fake/MID 的样本数是 0。
  学生（Qwen3-0.6B LoRA）即使训出来，也只会 Default/Rush/Save 三板斧。
  要么规则教练学会发这些意图（§4 P1），要么这些意图的样本只能由 7B 老师提供
  （§6 待定）。

### 2.3 P0 — 四处未接线（不删，全部接线）

- `changesThisRound`（CS2DGame.hpp:1247）：**全仓只有一处读**（Coach.cpp:390
  `>= IntentMaxChanges`），**没有任何一处自增**。`counters_.intentChanges`
  （Coach.cpp:1105，`CoachApplyPlan` 里意图真变时 +1）是另一个计数器，没接进预算。
  结论：每回合 2 次意图变更预算**未生效，需接线**；实际生效的只有 `IntentMinHold` 10s
  和 `IntentChangeCooldown` 6s。
- `hasBranch` / `branchAfterSec` / `branchIntent`（CoachTypes.hpp:83-87）：全仓
  只有三处赋值，全是 `false`（Coach.cpp:216/230/1039）。`CoachUpdate` 里的分支触发逻辑
  （:205-244）永远等不到 `hasBranch == true` 的计划。整套"前 20 秒卡点、20 秒后提速转点"
  的时间差打法未接线。
- `sameSideStreak`（CS2DGame.hpp:1248）："连续打同一侧的次数"，**全仓除声明外零读写**。
  防重复实际靠 `lastAttackSide`（仅 LLM 消毒用）和 `avoidA/avoidB`（仅 T 回合开始用）。
  需接线（见 P0 提案 4）。
- `SanitizeCoachPlan` 只在 `CoachDrainLlm`（CoachAI.cpp:1190）调用，规则计划不经过它。
  规则计划没有"连续两回合压同一侧 → 改默认分兵"和"CT 无情报不赌点"这两道检查
  （`RuleCoachCore` 的 `avoidA/avoidB` 只覆盖"某侧 0 胜"这一种重复）。

### 2.4 P1 — 规则教练不读情报、不读对手习惯

- `intel_` 在 `CS2DGameCoach.cpp` 里的全部使用：消毒（:1083-1085，LLM 路）、
  `CanRotateNow`（:1484）、`CoachTryRotate`（:1519-1520）。`RuleCoachCore` **零次**。
- 后果举例：对面连续三回合打 A，CT 还是固定 2-2-1；对面 A 点 heat 很高，
  T 回合开始还是 Default 分兵（要等转点机制在回合中段、满足一堆门槛后才转）。
  `RoundMemo` 里有 `firstContactArea` 和每回合主攻侧，但规则只用 `target` 的胜负计数，
  不用首触区域、不读"对面主攻侧"（`memory` 是**己方**的，不是敌方的——敌方习惯
  根本没进记忆结构，digest 里"近局"写的是己方上一回合）。
- 注意：敌方习惯需要新增记忆字段（敌方每回合主攻侧/首触），不是现有字段的简单复用。

### 2.5 P1 — 经济没有跨回合状态

- `DecideBuyPlan`（:1409-1430）是纯快照函数：只看本回合均值、敌方均值、回合号。
  `_common.txt` 明确写的三条都没有实现：
  - "强起失败后下一回合强制 eco，禁止连强"——没有"上回合买档"状态；
  - "赛点/半场末 → 强起"——没有比分感知（`MatchPoint` 事件有，但 `RuleCoachCore`
    和 `DecideBuyPlan` 都没有分支）；
  - "输掉满起后默认 eco→满起"——没有输赢-买档状态机。
- 连败奖金（$1400→$3400）也没有进买档决策：`ShouldSaveNow` 只看均值 <1800，
  不看"再输一回合就 +$2900"这种跨回合算计。

### 2.6 P1 — 回防模板：全员 Entry，不认拆包钳

- 下包后 CT：5 个槽位全是 `Entry@包点`（:640-648），没有 2 人掩护 1 人拆的分工，
  没有道具清点，没有钳子持有者优先。
- `mustRush` 阈值 11s 是按 10s 拆包算的；有钳子 4s 就能拆完，阈值应是 5s。
  钳子持有者信息在教练层可读（`hasDefuseKit` 机制已在 PIX-33 落地），但回防逻辑没用。
- T 守包侧相对好（交叉火力 + 残局 slow），但也没有"1 人看拆包角度、1 人控回防路线、
  1 人延迟 peek"的分工——`_common.txt` 写了，代码没写。

### 2.7 P1 — 槽位按 BOT 下标顺序分，不看武器/技术/经济

- `CoachApplyPlan`（:1117-1190）按 `bots_` 下标顺序把槽位 0–4 分给存活 BOT，
  不看谁拿 AWP（应分 Awper）、谁枪法好（应分 Entry）、谁钱少（应分 Save 位）。
- 角色在执行层坍缩：`BotRole` 只有 Entry/Support/Anchor 三个值（CS2DGame.hpp:54-58），
  `CoachApplyPlan` 把 Lurk → Entry、Awper → Support（:1145-1152）。
  教练词汇 5 个角色，执行只有 3 个——`Awper`（架狙）和 `Lurk`（断后/绕后）是教练的 fiction。
  另外 `AssignSquadRoles` 还会按"离敌方质心远近"重排角色（:约 560-600），
  可能覆盖教练的槽位角色（有 `RoleSwitchCooldown` 8s 缓冲，但语义上两套角色系统在打架）。

### 2.8 P1 — 消毒/意图集/角色表的三处不一致

- `TacticsTable::kIntents` 只有 11 个，没有 RotateA/RotateB——转点意图拿不到表
  （`CoachTryRotate` 是手写槽位，不走表）。
- `kCoachSchema` 的 intent enum 只有 11 个，没有 `rotate_a/rotate_b`——LLM 永远开不出
  转点意图；转点只能由规则触发（与 minimind 步骤 1–2 稿的待定 1 是同一件事）。
- `TacticsIntentKey` 把 `FakeThenOther` 映射成 "Fake"，但 `kIntents` 里有 "Fake"，
  `IntentLabel`/`ParseIntent` 里也有 "fake"——这一处是对的；真正缺的是 plain
  `[slots.T.Fake]` 段（只有 `@` 段，且是死的，见 §2.1）。

### 2.9 P2 — 手册与规则实现脱节（抽查）

`_common.txt` 写了、规则没做的条目（不完全统计）：

- "连续两回合同 intent + 同 target 失败 → 必须换点"——规则只查己方分侧 0 胜，
  不查 intent+target 对。
- "开局约前 20 秒偏 default 换信息"——规则有 T 18s 执行锁（精神相近），但 CT 没有等价物，
  且锁的是"不换侧"不是"换信息"。
- "fake 必须有佯攻侧接触/噪音；看到回防再转"——规则发不出 Fake，无从谈起。
- "拆包需要 5 秒"——手册数字已过期（实际有钳 4s / 无钳 10s），"最后 6 秒才强拆"
  与代码 11s 阈值也不一致。手册需要一次数字校准。
- "听到脚步的 BOT 只会自己去查，不共享给队友"——这是行为描述，规则层没有对应机制可调，
  属于 advisory，应标注而非当作规则写。

## 3. 教师天花板：为什么这直接决定 LoRA 质量

`coach_rounds.jsonl` 的老师 = 规则教练（+ 7B，如果开着 LLM）。规则教练的意图分布
决定数据集的意图分布。现在的规则教练在回合开始只能产出：

| 意图 | 规则教练能发 | 来源 |
|---|---|---|
| Default | ✅ | 回合开始默认 / 事件默认 |
| RushA / RushB | ✅（仅 0 胜连败或 TimeLow） | `RuleCoachCore` |
| Save | ✅（经济崩） | `RuleCoachCore` |
| StackA / StackB（T 守包） | ✅（仅下包后） | `RuleCoachCore` |
| RetakeA / RetakeB | ✅（仅下包后 CT） | `RuleCoachCore` |
| RotateA / RotateB | ✅（仅 `CoachTryRotate`，回合中段） | 转点机制 |
| SplitA / SplitB | ❌ | 无任何分支 |
| FakeThenOther | ❌ | 无任何分支 |
| MID 目标 | ❌ | 无任何分支 |

也就是说，不做 §4 的 P1，LoRA 数据集里 Split/Fake/MID 的样本数就是 0，
学生只会 Default/Rush/Save/Stack/Retake。7B 老师可以补这部分样本（它能发 Split/Fake），
但 7B 的样本要先批量跑出来，而且 7B 自己的计划有 ~33% 被拒
（V2 数据：`invalid_consistency` 13% + `invalid` 10% + `advisory` 10%），
被拒的计划不会进 `coach_rounds.jsonl`（`CoachRecordRound` 只记最终生效计划）。
所以**规则教练的覆盖面就是数据集的覆盖面下限**。

## 4. 改进提案（按优先级）

### P0 — 修 bug / 死代码（小改动，大收益；建议第一批）

1. **`TacticsTable::Load` 补读 `@` 段**：`GetAll()` 里再扫 `slots.` 前缀的段，把
   `[slots.<Team>.<Intent>@<Target>]` 也登记进 `slots` map（key 保留 `@Target` 后缀）。
   验收：`lint` 或单测断言每图 18 个段全加载；`ApplySlotTemplate` 的 `@` 查找命中。
   预期效果：Split/Fake/Save/Stack/Retake 的表立刻生效；LLM 计划拒绝率下降。
2. **`changesThisRound` 接线**：`CoachApplyPlan` 里意图真变时（已有 `counters_.intentChanges`
   的判断点，:1105）同步 `++st.changesThisRound`；`CoachResetRound` 里清零。
   验收：连续 3 次非关键事件改意图，第 3 次被预算拦截。
3. **`hasBranch` 接线**：在 `RuleCoachCore` 的回合开始分支里，对 Default 计划设 `hasBranch`
   （如 20s 后按情报转 Rush 另一侧），让 `CoachUpdate` 的分支触发逻辑活起来。
   "时间差打法"是教练最像真人的部分，字段和触发逻辑都保留，不删除。
4. **`sameSideStreak` 接线**：接到消毒逻辑里——`CoachThink`/`CoachRecordRound` 里每回合
   按实际主攻侧更新（同侧 +1、换侧清零），`SanitizeCoachPlan` 复用时用它替代/补充
   `lastAttackSide` 做"连续同侧"判断；`RuleCoachCore` 回合开始也可以读它
   （连续同侧 ≥2 → 强制换侧）。
5. **`SanitizeCoachPlan` 复用于规则计划**：`CoachThink` 里 `RuleCoach` 返回后也调一次
   （跳过已下包分支，消毒函数本身已处理）。注意：规则计划的 `avoidA/avoidB` 与消毒的
   "同侧连续 → 默认分兵" 是两套逻辑，复用后要测一次，避免双重整形把计划改僵。
6. **`kIntents` 补 RotateA/RotateB + `kCoachSchema` 补 `rotate_a/rotate_b`**：
   前者让转点意图能走表；后者让 LLM 也能开转点（与 minimind 步骤 1–2 稿的待定 1 联动，
   建议训练版 schema 用 13 个意图）。

### P1 — 决策质量（规则教练的核心升级；教师天花板在这里）

7. **意图打分制**：`RuleCoachCore` 回合开始分支改成"对每个候选意图打分，取最高"，
   信号全部取自 `BuildStateDigest` 已有字段：情报热度（`HeatOfSide` 差值）、敌枪构成
   （狙多 → 避开长廊 / 慢打）、道具明细（烟 ≥1 才能道具开点）、对手习惯（新增敌方
   记忆字段：敌方每回合主攻侧/首触）、比分与赛点、经济差（我穷敌更穷 → 强起打经济，
   系统提示里有、规则里没有）。打分权重放 `tactics.ini` 或 `Tuning`，不要硬编码在 C++ 里。
8. **RoundStart 发 Split / Fake 的条件**：
   - Split：有情报（heat 差小、两路都有人）+ 经济 Full/Half + 时间充裕 → 分路夹击；
   - Fake：佯攻侧有接触/噪音 + 主攻侧 heat 低 → 佯攻后转点（`FakeThenOther` 已有
     `branchTarget` 语义可用，见提案 3）。
   - 两个意图的槽位模板走 §2.1 修好后的 `@` 表（`T.SplitA@A`、`T.Fake@B` 已存在）。
9. **CT 记忆驱动防守**：CT 回合开始读敌方记忆——对面连续两回合主攻 A（或 A 侧 heat 高）
   → StackA / 3-1-1 重防 A；对面无固定侧 → 2-2-1。把 `avoidA/avoidB` 的逻辑镜像到 CT。
10. **MID 控中意图**：`target == "MID"` 的 Default 变体：2 人控中 + 两侧各 1 人架点，
    1 人自由。用在"无情报 + 开局"的回合（替代纯 Default 分兵）。
11. **跨回合经济状态机**：`CoachState` 加 `lastBuyPlan` + `lossStreak`（`CoachRecordRound`
    已有比分/胜负，顺手记）：
    - 上回合 Force 且输 → 这回合强制 Eco（禁连强）；
    - 赛点（任一队到 `ScoreToWin - 1`）→ Force（除非已下包/残局）；
    - 连败 ≥2 且下回合奖金能满起 → Eco 存钱；
    - 阈值本身不动（1800/2600/3500 已合理）。
12. **回防模板重做（CT 下包后）**：钳子持有者优先进点拆（kit-aware：有钳 `mustRush` 阈值
    11s → 5s）；其余 4 人分两路集结 + 2 人掩护拆包者；道具清点（有烟先封回防路）。
    T 守包侧补分工：1 人看拆包角度、1 人控回防路线、1 人延迟 peek（`_common.txt` 已写）。
13. **槽位按 BOT 能力分**：`CoachApplyPlan` 分槽位前先排个序——拿 AWP 的分 Awper、
    钱最少的分 Save 位、其余按枪法/移速分 Entry/Support。`BotRuntime.difficulty`
    已有（`ResetBotAi` 保留它），直接用。

### P2 — 执行层与评估

14. **Lurk / Awper 扩展成真角色**：扩展 `BotRole` 加 Lurk/Awper，配不同的移速倍率与行为
    （Lurk：绕后/断后，不参与正面；Awper：架狙位，移速慢、开镜优先）。槽位模板里大量用了
    Lurk/Awper，教练词汇保留；要动 BOT 决策树，工作量大，行为参数见 §6。
15. **锚点给规则模板用**：`CoachApplyPlan` 里 `hold != Default` 才用锚点的限制放开
    到规则模板（V2 遗留项 1：几何近似锚点只给了显式 hold 的卡片用；规则模板用之前
    需要人工采点校准，或先只在 mirage 上试点）。
16. **评测**：用 V2 的工具链重跑 30 局配对基准（规则改动前 vs 改动后；再加规则 vs LLM），
    指标：胜率（McNemar）、意图分布熵、Split/Fake 出现率、计划拒绝率、首杀转化。
    验收门槛：拒绝率（`invalid_consistency` + `invalid`）从 23% 降到 10% 以内；
    意图分布熵显著上升（不再是 Default 一家独大）。
17. **手册数字校准**：`_common.txt` 的"拆包需要 5 秒"→ 改成"有钳 4s / 无钳 10s"，
    "最后 6 秒才强拆" 与代码 11s/5s 阈值对齐；标出哪些是 advisory（不承诺执行）。

## 5. 与 minimind / LoRA 的衔接

- 顺序建议：**P0 先修 → 跑 30 局基准（拿到改动后的教师分布）→ 再做 P1 → 再采数据训 LoRA**。
  P0 修完（尤其 §2.1）会直接改变 `coach_rounds.jsonl` 的意图分布，采数据要在这之后，
  否则数据集里还是旧分布。
- 教师覆盖：P1 做完后，规则教练能产出的意图见 §3 的表全亮（Split/Fake/MID 补上），
  7B 只负责补"质量"（更好的 reason、更细的 tempo），不负责补"覆盖"。
- `coach_rounds.jsonl`（步骤 1–2 稿）不受本次改动影响：写盘点 `CoachRecordRound` 不动；
  但 `plan.source` 要能区分"规则新版"和"规则旧版"（建议 `gen` 或版本号字段，
  避免新旧教师数据混在一起）。
- 确定性：本次改动不碰 `rng_` 主链（打分用确定性输入；`Save` 的 ±36 抖动已在主链，
  但那是既有行为，不动它）。`rng_humanizer_` 不变。


## 附录：文件索引（live 行号）

- `PixelEngine/CS2D/CS2DGameCoach.cpp`：`CoachOnEvent` :135、`CoachUpdate` :165、
  `CoachThink` :319、`RuleCoach` :505、`RuleCoachCore` :512、`SanitizeCoachPlan` :973、
  `CoachApplyPlan` :1097、`CoachRecordRound` :1287、`CoachBeginRound` :1337、
  `DecideBuyPlan` :1409、`ShouldSaveNow` :1451、`CanRotateNow` :1475、`CoachTryRotate` :1510、
  `ApplySlotTemplate` :806、`PlanTargetConsistent` :792。
- `PixelEngine/CS2D/CS2DGameCoachAI.cpp`：`kCoachSchema` :55-75、`BuildAllowSet` :155-190、
  `BuildStateDigest` :553-748、`ParseCoachPlan` :749-903、`CoachTryLlm` :913、
  `CoachDrainLlm` :1027（`SanitizeCoachPlan` 调用 :1190）。
- `PixelEngine/CS2D/Coach/CoachTypes.hpp`：`PlanIntent` :16、`BuyPlan` :32、
  `CoachRole` :35、`SlotOrder` :46、`TeamPlan` :63（`hasBranch` :83）、`RoundMemo` :95。
- `PixelEngine/CS2D/Coach/TacticsTable.hpp`：`Perception` :34、`Load` :~60-140、
  `kIntents` :96（11 个，无 Rotate）、`Slots` :~150。
- `PixelEngine/CS2D/CS2DGameSquad.cpp`：`AssignSquadRoles` :~360、`RefreshRolesForTeam` :~420、
  `UpdateSquadFocus` :~500。
- `PixelEngine/CS2D/CS2DGame.hpp`：`BotRole` :54（仅 3 值）、`CoachState` :1237
  （`sameSideStreak` :1248、`changesThisRound` :1247）、`counters_.intentChanges` :610。
- `PixelEngine/CS2D/Tuning.hpp`：`IntentMinHold` 10s :290、`IntentMaxChanges` 2 :291、
  `IntentChangeCooldown` 6s :292、`IntentRenewSec` 8s :293、`SilentSeconds` 30s :294、
  `RoleSwitchCooldown` 8s :286。
- `PixelEngine/CS2D/CS2DGameRound.cpp`：TimeLow（剩 30s）:32、MatchPoint :43、
  EconomyLow :72、`BeginNewRound` :56、`BotBuyLoadout` :240（AntiEco :345）。
- `assets/config/tactics/*.ini`：每图 4 个 plain 段 + 14 个 `@` 段 + `[perception]`；
  util 14 条/图、look 70 条/图、hold ~50 条/图、anchor 58–70 个/图。
- `assets/config/coach/*.txt`：`_common.txt` 65 行 + 每图 14 张卡（42 张），lint 0 错误。
- `docs/llm-coach-v2-plan.md`：V2 已完成（2026-11-06）；30 局 T 胜率 63.3%→46.7%
  （p=0.227 不显著）；12 局 58.3% vs 58.3%；拒绝率 13%+10%+10%。
