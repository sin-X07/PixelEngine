# PIX-37 开工前必读（给 DeepSeek）

> 你是本次任务的实施者。任务：实现 Linear 上的 PIX-37《规则教练全量接线实施计划》
> （37 项任务 T01–T37）。验收者是 Muse，决策者是 Sun（孙铭轩）。
> **先读完这份必读，再读完整计划 `docs/PIX-37-rules-coach-wiring-plan.md`，再动手。**
> 这份必读是自包含的：读完它，你就具备实施 PIX-37 所需的全部项目背景；
> 清单与审计全文也在仓库 `docs/` 里（见 §9），细节随时查。

## 0. 一句话目标

规则教练（RuleCoach）现在只用了手头信息的一小部分：大量决策类型与数据已经存在，
但代码没接线。你的任务是**把清单里的每一条决策与数据都接到规则教练上，让它在
回合中真正用出来**。验收标准只有一个：**在游戏中真正检测到规则教练使用了相关指令，
才算完成**——代码写完、编译通过、单测通过都只是支撑证据。

## 1. 项目速览：PixelEngine 是什么

- PixelEngine：C++17 / SDL2 俯视角 2D 战术射击游戏，CS2D-like 单机 5v5，
  玩家（T 或 CT）对抗 LLM 驱动的 BOT。模式：爆破 / 歼灭 / 死斗。
  回合流程：冻结 2s → 买枪（6s 准备，15s 窗口）→ 战斗 ≤90s → 结算 4s，先到 8 分胜。
  仓库：https://github.com/sin-X07/PixelEngine
- 你工作的电脑：Sun 的 Windows 电脑，仓库在
  `D:\BJTU\Cpp\Outstanding Projects\PixelEngine`，源码在 `PixelEngine\` 子目录下。
  HEAD 是 `ce41a76`（2026-11-17）+ 大量未提交改动——**以你电脑上的 live 工作树为准，
  本必读里的行号是 2026-11-18 核实的 live 行号，实现前先复核一次**（行号可能漂移，
  见 §8）。
- 工具链：cmake 4.3.2（`D:\cmake\...`）、MinGW gcc 15.2；构建脚本 `D:\build.ps1`
  （主程序 `build-release\EngineApp.exe`）、`D:\build_tests.ps1`（测试）。
  测试：`gameplay_tests`（`tools/buy_scope_test.cpp`），含 `--experience` 模式；
  无显示环境的会话里 SDL 建窗测试会失败（环境原因，不是代码问题），
  真实对局测试用 schtasks 交互式任务跑。
- 分工：Sun 定方向；Muse 写计划文档、做美术与音效、独立验收；本地 Codex 平时接代码
  （额度 2026-11-20 10:01 恢复）；**今天由你代替 Codex 实施 PIX-37**。
  改动留在工作区、**不提交、不 push**，由 Sun 过目后再定。

## 2. 规则教练架构全景

### 2.1 决策链路（自上而下）

```
事件 CoachOnEvent（0.4s 合并窗口，优先级见下）
  → CoachUpdate（每帧；转点评估每 0.75s；计划到期续期；沉默 30s 重评估；心跳 8s）
  → CoachThink（KEEP 逻辑、LLM sticky、T 18s 执行锁、变更预算）
  → RuleCoach / RuleCoachCore（规则决策核心，见 §2.2）
  → 战术表槽位覆盖 ApplySlotTemplate
  → SanitizeCoachPlan（消毒：防重复压点、无情报不赌点；目前只走 LLM 路）
  → CoachApplyPlan（槽位 → SetBotGoal + 角色/朝向/站姿/道具）
  → CS2DGameSquad（AssignSquadRoles / RefreshRolesForTeam / UpdateSquadFocus 集火）
  → BOT 决策树（CS2DGameBots.cpp）
```

事件优先级（`CoachOnEvent`，`CS2DGameCoach.cpp:135-164`）：
RoundStart 100 > BombPlanted 95 > DefuseStarted 90 > HumanDied 85 > EnemyDown 80 >
AllyDown 78 > EnemySpotted 70 > MatchPoint 60 > TimeLow 55 > EconomyLow 40 >
Heartbeat 最低。

`CoachThink` 分类（`Coach.cpp:322-337`）：
- **criticalEv**（7 种，绕过 MinHold/冷却/sticky/T 锁）：RoundStart、BombPlanted、
  DefuseStarted、TimeLow、EconomyLow、MatchPoint、HumanDied。
- **softEv**（5 种，计划仍有效时 KEEP、不重算）：Heartbeat、EnemyDown、AllyDown、
  EnemySpotted、DefuseAborted。注意：soft-KEEP 会先拦截——计划未过期且未下包时，
  `RuleCoachCore` 的 EnemyDown/AllyDown 分支跑不到（T16 就是修这个的）。

### 2.2 `RuleCoachCore` 现在到底会什么（`CS2DGameCoach.cpp:512-756`）

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
  这就是你要改的核心。

### 2.3 LLM 链路（你不动它，但要理解它——规则教练是 LLM 的对照组）

- LLM 只做教练（本地 llama-server，Qwen2.5-7B，`assets/config/llm.ini`，
  http://127.0.0.1:9931，每队每回合上限 8 次调用）。`CoachTryLlm`
  （`CS2DGameCoachAI.cpp:913-992`）默认只在 RoundStart/BombPlanted 发请求；
  advisory 模式默认 ON（情报不足时只记录、不执行）；TTL 地板：回合开始 18s、
  下包/拆包 12s、其他 10s；超时 7s/4s。
- `BuildStateDigest`（`CoachAI.cpp:553-748`）给 LLM 的 14 组字段（见 §4），
  规则教练现在几乎都不用——PIX-37 就是把这些交给规则教练。
- `kCoachSchema` 的 intent 枚举只有 11 个（没有 rotate_a/rotate_b），LLM 永远开不出
  转点意图；转点只能由规则触发（`CoachTryRotate`）。
- **为什么做这件事**：规则教练是小模型教练（Unsloth Qwen3-0.6B LoRA）的老师，
  数据集从规则教练蒸馏。规则教练不会的东西，学生永远学不会。
  所以 P0 改动会改变教师分布——采数据要在 P0+P1 做完之后（T36）。

### 2.4 关键文件与行号（实现前复核）

| 文件 | 内容 | 关键行号 |
|---|---|---|
| `PixelEngine/CS2D/CS2DGameCoach.cpp`（1567 行） | 教练主体 | CoachOnEvent :135、CoachUpdate :165（分支触发 :205-244、转点评估 :171、Heartbeat :287-301）、CoachThink :319（criticalEv :322-329、softEv :332-337、soft-KEEP :339、T 18s 锁 :372、变更预算 :386-395、C4 掉落 :406-424、lastAttackSide :456）、RuleCoachCore :512-756、SanitizeCoachPlan :973-1096、CoachApplyPlan :1097-1286（角色映射 :1142-1146、C4 携带者 :1227-1232）、CoachRecordRound :1287-1315、CoachBeginRound :1337-1372、DecideBuyPlan :1409-1430、ShouldSaveNow :1451-1474、CanRotateNow :1475-1509、CoachTryRotate :1510-1566 |
| `PixelEngine/CS2D/CS2DGameCoachAI.cpp`（1278 行） | LLM 链路 | kCoachSchema :55-75（intent enum :24，无 rotate）、BuildAllowSet :155-190、BuildStateDigest :553-748、ParseCoachPlan :749-903、CoachTryLlm :913-992、CoachDrainLlm :1027-1278（Sanitize 调用 :1190、TTL 地板 :1194-1197、超时 :1270、advisory 门 :1211-1222） |
| `PixelEngine/CS2D/Coach/CoachTypes.hpp`（135 行） | 类型定义 | PlanIntent :16、Tempo :29、BuyPlan :32、CoachRole :35、HoldStyle :38、SlotOrder :46、TeamPlan :63（hasBranch :83）、TempoAggression :91、RoundMemo :95、CoachEventType :116 |
| `PixelEngine/CS2D/Coach/TacticsTable.hpp`（241 行） | 战术表加载 | Perception :34、Load :60-140、kIntents :96（11 个，无 Rotate）、ParseSlot、Slots :150 |
| `PixelEngine/CS2D/CS2DGameSquad.cpp`（746 行） | 小队执行 | RefreshRolesForTeam :428、AssignSquadRoles :477、UpdateSquadFocus :604、ReportContact :147-163、UpdateTeamContacts :185-230 |
| `PixelEngine/CS2D/CS2DGame.hpp` | 状态与 BOT | BotRole :54、SquadOrder :61、GetLossStreak :1126、CoachState :1237（changesThisRound :1247、sameSideStreak :1248）、经济快照 :1288-1290 |
| `PixelEngine/CS2D/Tuning.hpp` | 调参 | 角色 :284-287、意图预算 :289-295（含 SameSideStreakLimit :295）、移速倍率 :95-97 |
| `PixelEngine/CS2D/TacticalIntel.hpp/.cpp` | 情报 | SiteHeat 结构、tau 14s / 上限 6 / +1、SiteIndexAt 220px 阈值 |
| `PixelEngine/CS2D/CS2DGameBots.cpp` | BOT 决策树 | 按角色定移速三处：:908-910（planHold）、:1015-1017（寻路）、:1401（交火机动），据 2026-11-17 快照核实 |
| `PixelEngine/CS2D/CS2DGameCombat.cpp` | 战斗 | 击杀喂入情报 :1017（11-10 快照是 1047，已漂移） |
| `PixelEngine/CS2D/CS2DGameRound.cpp` | 回合 | TimeLow :32、MatchPoint :43、BeginNewRound :56、BotBuyLoadout :240（AntiEco :345） |
| `assets/config/tactics/*.ini`（每图一份） | 战术表 | 每图 18 个 `[slots.]` 段（4 plain + 14 @）、look/anchor 58–70 段、`[areas]` 6 键、`[perception]` 5 段 |
| `assets/config/coach/*.txt` | 战术手册 | `_common.txt` 65 行、每图 14 张卡 + advisory（6/8/7）+ few-shot 2 条 |
| `docs/llm-coach-v2-plan.md` | V2 评测基建 | 30 局配对基准（McNemar）、决策日志、plan trace、lint |

## 3. 关键数据结构（背下来再动手）

### 3.1 战术意图 PlanIntent（13 个）

`CoachTypes.hpp:16-27`。`TacticsIntentKey` / `IntentLabel` / `ParseIntent` 三处映射
都支持全部 13 个；但 schema（LLM 可见）只有 11 个，`TacticsTable::kIntents` 只有 11 个。

| 意图 | 规则能发 | LLM 能发 | 备注 |
|---|---|---|---|
| Default | ✅ | ✅ | 回合开始默认 / 事件默认 / 消毒兜底 |
| RushA / RushB | ✅ | ✅ | 规则：仅 0 胜连败或 TimeLow；卡片：满起 + 敌分布条件 |
| SplitA / SplitB | ⛔ | ✅ | 规则 `RuleCoachCore` 全文无赋值；只有 LLM 能发 |
| StackA / StackB | ✅ | ✅ | 规则：仅下包后 T 守包；卡片：CT 回合开始也可发 |
| RetakeA / RetakeB | ✅ | ✅ | 规则：仅下包后 CT；卡片同 |
| Save | ✅ | ✅ | 规则：经济崩；卡片：eco_save |
| RotateA / RotateB | ✅（仅转点机制） | ⛔ | `CoachTryRotate` 发；schema 里没有，LLM 开不出 |
| FakeThenOther | ⛔ | ✅ | 规则全文无赋值；只有 LLM 能发；`branchTarget` = 真打侧 |

### 3.2 其他决策类型

- **目标 target**（4 个值）: `"A"` / `"B"` / `"MID"` / `""`（空）。schema 允许全部 4 个，
  规则实际只产生 `"A"` / `"B"` / `""`——**MID 没有任何规则分支会设**。
  C4 掉落时 CT 的 target 是区域名（如 `B_Apts`，不是 `"B"`）。
- **节奏 Tempo**（3 档）: Slow / Normal / Fast，激进系数 Fast 1.25、Normal 1.00、
  Slow 0.80。规则用法：EnemyDown → Fast、AllyDown → Slow、Save → Slow、
  下包后 T 残局 → Slow、mustRush → Fast、TimeLow → Fast。
- **购买计划 BuyPlan**（5 档）: Eco / Half / Force / Full / AntiEco。
  `DecideBuyPlan`（`Coach.cpp:1409-1430`）：手枪局（回合 ≤1）→ Force；
  `ShouldSaveNow` → Eco；均值 ≥3500 → Full（敌均 <2200 时叠加 AntiEco）；
  ≥2600 → Half；≥1800 → Force；否则 Eco。非爆破模式直接 Full。
  `ShouldSaveNow` 条件：未下包、回合 >1、已进行 ≤45s、非人数优势、T 未占包点、
  均值 <1800。`BotBuyLoadout` 接 AntiEco：SMG + 甲 + 满道具、不买 AWP/步枪。
- **角色 CoachRole（5）vs BotRole（3）**: Entry / Support / Anchor 直通；
  **Lurk → Entry（坍缩）、Awper → Support（走 default 分支，坍缩）**。
  `CoachRole` 注释称"影响移速倍率、补枪资格、远追许可"——其中移速倍率属实
  （见下），补枪资格属实（`canTrade` 在 `Bots.cpp:586` trade push、`:791` assist 使用），
  **"远追许可"查无实据**：全仓只有 `CoachTypes.hpp:34` 注释本身提到，无对应机制代码。
  移速倍率（`Tuning.hpp:95-97`）：Entry 1.00 / Support 0.80 / Anchor 0.55，
  决策树三处按角色取值（`CS2DGameBots.cpp:908-910` planHold、`:1015-1017` 寻路、
  `:1401` 交火机动）。
- **站姿 HoldStyle**（4 个）: Default / Anchor（架点）/ OffAngle（侧身位）/
  Passive（被动）。执行点：`CoachApplyPlan` 里 `hold != Default` 才用战术表锚点——
  规则模板生成的槽位 `hold` 恒为 Default，所以**规则计划永远不用锚点**。
- **槽位 SlotOrder**（9 个字段）: role、area、canTrade（默认 true）、canRotate
  （默认 false）、lookArea、lookAngle（-99 = 未指定）、hold（默认 Default）、
  util（0 无 / 1 烟 / 2 闪 / 3 火 / 4 雷）、utilArea。规则代码手写的槽位只填
  role/area/canTrade/canRotate 四项，look/hold/util 恒为默认。
- **计划 TeamPlan**（17 个字段）: intent、target、tempo、slots[5]、reason、
  source（"rule"/"llm"）、reshaped、latencyWall、templateDefault、gen、createdAt、
  valid、expiresAt、hasBranch、branchAfterSec、branchIntent、branchTarget、branchTempo。
- **记忆 RoundMemo**（每队最近 10 回合）: roundNo、plan、target、won、scoreT、scoreCT、
  aliveEnd、bombPlanted、plantSite、firstContactArea、deaths[]（team/area/x/y/atSec）。
  **注意：记忆只记己方计划与结果，不记敌方主攻侧**（T13 就是补这个的）。
- **状态 CoachState**（每队一份，约 38 字段）: plan、buyPlan、memory、pending、
  mergeWindow、callsThisRound（上限 8）、gen、reason、appliedIntent、
  changesThisRound、sameSideStreak、lastIntentChangeSec、deaths、firstContactArea、
  timeLowFired、matchPointFired、notes（最近 5 条复盘结论）、lastOrder、
  rotateFrom/rotateTo/rotateAt、lastAttackSide、planExpiresAt、LLM in-flight 字段、
  ruleSnapshot/llmSnapshot、stickyLlm。
- **调参 Tuning**（`Tuning.hpp:284-295`）: RoleRefreshInterval 2.0s、RoleSwitchCooldown 8.0s、
  **RoleTransitionSec 3.0s（只有定义、零处引用——第 19 个未接线项，T27 实现它）**、
  IntentMinHold 10.0s、IntentMaxChanges 2（预算未生效，T05 接线）、
  IntentChangeCooldown 6.0s、IntentRenewSec 8.0s、SilentSeconds 30.0s、
  SameSideStreakLimit 3（旋钮已定义、没有任何代码读它，T07 接线）。

## 4. 数据源全貌：规则教练现在读什么、不读什么

| 数据源 | 规则教练现状 |
|---|---|
| 战术表 ini（每图 18 段：4 plain + 14 @） | 只读 4 个 plain 段；**14 个 @ 段从未被加载**（T04 修） |
| 战术手册（_common 65 行 + 42 张卡） | **只喂 LLM，规则不用**（T10/T12/T13/T18/T19/T21/T28/T34 分批落） |
| 情报 TacticalIntel（heat） | 只有转点与消毒读；**`RuleCoachCore` 零次使用**（T15 修） |
| 记忆 RoundMemo + CoachState | 规则只用其中一小部分（己方分侧胜负、经济快照、包点区域） |
| 摘要 BuildStateDigest（14 组字段） | **全部只给 LLM**（T10/T14/T15 分批交给规则） |
| 经济（GetLossStreak 连败奖金） | 函数已存在，教练层全不读（T18 修） |
| 拆包钳 hasDefuseKit（4s/10s） | 机制已落地（PIX-33），回防逻辑没读它（T20 修） |
| 小队执行（SquadOrder/AssignSquadRoles） | 两套角色系统：教练计划优先（已核实）；唯一窄窗口见 T25 |

**摘要 14 组字段**（`BuildStateDigest`，`CoachAI.cpp:553-748`——规则教练要接的
"现成数据"都在这里）：1. 优先级行；2. 回合/比分/时间/存活；3. 炸弹状态（含已过期
的"拆包需5s"）；4. 经济（己方穷/中/富 + 购买档位 + 敌经济 + 差值）；5. 我方站位分布
（按区域计数，最多 4 个区）；6. 道具明细（烟/闪/火/雷分项）；7. 敌枪构成
（狙/步枪/冲锋/霰弹/手枪人数）；8. 近局习惯（上一回合主攻侧 + 首触区域 + 胜/负，
只取最近 1 条）；9. 敌分布（每包点 heat）；10. 敌联系人（区域 + 年龄）；
11. 上条 lastOrder；12. 参考笔记（最新 1 条）；13. 事件（类型 + 相关区域）；
14. 合法闭集 + 近 3 回合结构化行。

**战术表数据量**（实测）：`[areas]` 6 键；`[slots.*]` plain 4 段、@ 14 段；
`[look.*]` 58–70 段；`[anchor.*]` 58–70 段；`[perception]` 5 段；
区域总数 29（ancient）/ 35（dust2）/ 32（mirage）；槽位行含 `util=` 14 条/图、
`hold=` 约 50 条/图、`look=` 70 条/图。

**战术卡**（42 张，14/图：T 9 + CT 5）：T_default、T_rush_a/b、T_split_a/b、T_fake、
T_eco_save、T_postplant_a/b；CT_default、CT_stack_a/b、CT_retake_a/b。
卡片字段：id / when / intent / target / tempo / ttl / slots（5 条）/ fallback（恒为 default）。
`when` 条件统计：用"上回合主攻"9 条、用"敌经济"6 条、用"已控"12 条、
用敌分布计数约 20 条、用"购买=eco"1 条、用"时间<40s"2 条、用"已下包"4 条。
advisory 21 条（ancient 6、dust_2 8、mirage 7）；few-shot 2 条/图（T default、CT retake_a，
**没有 Split/Fake/Save 的示例**）。

## 5. 审计发现的问题全貌（2026-11-18，对照 live 树核实）

结论先行：**规则教练的管线是好的，决策是瘸的。** 事件队列、合并窗口、8 次调用预算、
LLM sticky、T 18 秒执行锁、TTL 续期、转点证据门、买枪延迟等机制都很完整；
但真正做决策的 `RuleCoachCore` 只用了手头信息的一小部分，还有几处数据已存在、代码没接线。

1. **战术表 14 个 `@Target` 模板未被加载**：`materialize_cards.py` 把 42 张战术卡落成了
   `[slots.*@Target]` 段，但 `TacticsTable::Load` 从不读 `@` 段。每张图实际生效的模板只有
   4 个（T.Default、CT.Default、T.RushA、T.RushB）；Split / Fake / Save / Stack / Retake
   在任何路径下都没有表可用。V2 评测的 13% `invalid_consistency` + 10% `invalid` 拒绝率，
   至少一部分是这个机制造成的（LLM 按卡片出了 Split/Fake，引擎却找不到对应的槽位模板）。
2. **教师天花板**：规则教练在回合开始时只会发 Default / RushA / RushB / Save，
   永远发不出 Split / Fake，也没有 MID 控中意图。拿它当老师蒸馏，LoRA 数据集里就不会有
   这些战术——学生学不会老师不会的东西。
3. **四处未接线**（按 Sun 的原则：不删，全部接线）：`changesThisRound` 从不自增
   （每回合 2 次意图变更预算未生效）、`hasBranch` 永不设 true（整套"时间差打法"分支机制
   未接线）、`sameSideStreak` 从不读写、`SanitizeCoachPlan` 只走 LLM 路，不用于规则计划。
4. **规则教练不读情报、不读对手习惯**：`intel_` 在 `CS2DGameCoach.cpp` 里的全部使用
   只有消毒（LLM 路）、`CanRotateNow`、`CoachTryRotate`；`RuleCoachCore` **零次**。
   后果举例：对面连续三回合打 A，CT 还是固定 2-2-1。
5. **经济没有跨回合状态**：`DecideBuyPlan` 是纯快照函数。手册明确写的三条都没有实现：
   "强起失败后下一回合强制 eco，禁止连强"、"赛点/半场末 → 强起"、
   "输掉满起后默认 eco→满起"。连败奖金也没有进买档决策。
6. **回防模板：全员 Entry，不认拆包钳**：下包后 CT 5 个槽位全是 `Entry@包点`，
   没有 2 人掩护 1 人拆的分工，没有道具清点，没有钳子持有者优先。
   `mustRush` 阈值 11s 是按 10s 拆包算的；有钳子 4s 就能拆完，阈值应是 5s。
7. **槽位按 BOT 下标顺序分**：`CoachApplyPlan` 按 `bots_` 下标顺序把槽位 0–4 分给存活 BOT，
   不看谁拿 AWP（应分 Awper）、谁枪法好（应分 Entry）、谁钱少（应分 Save 位）。
8. **转点机制是规则层最完整的自适应**（别动它）：证据门（heat 差 ≥1.2、情报 ≥2 条、
   剩 30s+、不少 2 人、8s 冷却、仅 Default 阶段、计划已执行 12s+、无人进包点/
   携带者不在 280 内）+ 中止逻辑（已下包/剩 25s/少 2 人 → 回原侧）；
   执行 2 人中路重组 + 3 人转推。
9. **V2 评测基线**（`docs/llm-coach-v2-plan.md`，2026-11-06 完成）：30 局 T 胜率
   63.3%→46.7%（McNemar p=0.227，不显著）；12 局 58.3% vs 58.3%；
   拒绝率 invalid_consistency 13% + hard-rule invalid 10% + advisory 10%。

## 6. 清单全貌：19 个未接线项 + 17 项只给 LLM 的数据 + §6 质量问题

### 6.1 §5 未接线项总表（全部保留，逐条给接线点）

| # | 项 | 现状 | 接线点 | 对应任务 |
|---|---|---|---|---|
| 1 | `@Target` 模板 14 段/图 | Load 只读 plain 段 | `TacticsTable::Load` 里 `GetAll()` 再扫 `slots.` 前缀登记 | T04 |
| 2 | `changesThisRound` 预算 | 全仓一处读、零处写；`CoachBeginRound` 也不清零 | `CoachApplyPlan` 意图真变时 +1；`CoachBeginRound` 清零 | T05 |
| 3 | `hasBranch` 时间差打法 | 永不设 true；`CoachUpdate:205-244` 触发逻辑已就绪 | `RuleCoachCore` RoundStart 的 Default 计划设 `hasBranch` | T06 |
| 4 | `sameSideStreak` + `SameSideStreakLimit=3` | 字段 + 旋钮都已定义，零读写 | `CoachThink`/`CoachRecordRound` 更新；`Sanitize` 复用；`RuleCoachCore` 读它 | T07 |
| 5 | `SanitizeCoachPlan` 复用于规则 | 只在 `CoachDrainLlm:1190` 调用 | `CoachThink` 里 `RuleCoach` 返回后也调一次 | T08 |
| 6 | Rotate 进表 + 进 schema | `kIntents` 无 Rotate；schema 无 rotate_a/b | `kIntents` 加 RotateA/RotateB；`kCoachSchema` 加 rotate_a/rotate_b | T09 |
| 7 | Lurk / Awper 真角色 | 槽位模板大量使用，下发坍缩成 Entry/Support | `BotRole` 加 Lurk/Awper，配移速倍率与行为 | T26 |
| 8 | `LossStreak` 连败奖金 | `GetLossStreak` 已存在，教练层全不读 | `DecideBuyPlan`：连败 ≥2 且下回合奖金能满起 → Eco 存钱 | T18 |
| 9 | 敌方习惯记忆 | 记忆只记己方；CT stack 卡的"上回合主攻"无数据源 | `RoundMemo` 加敌方主攻侧/首触 | T13 |
| 10 | MID 控中意图 | target 允许 MID，无规则分支 | `RuleCoachCore` Default 变体：2 人控中 + 两侧各 1 人架点 + 1 人自由 | T11 |
| 11 | 赛点分支 | MatchPoint 是 criticalEv，但无决策分支 | `RuleCoachCore`：赛点 → Force 买档 + tempo Fast | T19 |
| 12 | 钳子感知回防 | mustRush 11s 按 10s 拆包算，不认 4s 钳子 | 有钳持有者阈值 11s→5s；钳子持有者优先进点拆 | T20 |
| 13 | 按能力分槽 | `CoachApplyPlan` 按 BOT 下标顺序分槽 | 分槽前排序：AWP 分 Awper、钱最少分 Save 位 | T22 |
| 14 | 跨回合经济状态机 | `DecideBuyPlan` 纯快照 | `CoachState` 加 `lastBuyPlan` + `lossStreak` | T18 |
| 15 | 回防分工模板 | 下包后 CT 全员 Entry@包点 | 钳子持有者进点拆 + 2 人掩护 + 2 人控回防路线；T 守包补分工 | T21 |
| 16 | 敌枪/道具/联系人进打分 | 摘要有，规则不用 | `RuleCoachCore` 回合开始改意图打分制 | T10、T14 |
| 17 | 消毒的 CT Stack 检查 | 只走 LLM 路 | 随 T08 复用 | T08 |
| 18 | `templateDefault` 覆盖链 | 规则模板允许表覆盖，但表只有 4 个 plain 段 | 随 T04：@ 段加载后覆盖自动生效 | T04、T24 |
| 19 | `RoleTransitionSec` 3.0s | 只有定义、零处引用（§6 新发现） | 按角色切换时刻插值倍率，3 秒过渡 | T27 |

### 6.2 §4 只给 LLM 的 17 项 → 交给规则教练

1. 我方站位分布 → T10（判断"中路是否已控""两侧是否有人"，替代固定分兵）
2. 道具明细 → T14（烟 ≥1 才能道具开点；无道具 → Default）
3. 敌枪构成 → T14（敌狙多 → 避长廊/慢打；敌冲锋多 → 保持距离/架点）
4. 敌分布 heat 全量 → T15（回合开始读 `HeatOfSide`：热侧避开/冷侧主攻）
5. 敌联系人 → T14（TimeLow / EnemySpotted：按最新接触区域定主攻侧）
6. 近局习惯 → T13（CT 读敌方习惯；T 的 avoidA/avoidB 扩展为"意图+目标对"）
7. 近 3 回合结构化行 → T10（连续同 intent+同 target 失败 → 换点）
8. 上条指令 lastOrder → T14（新意图与上条是否同向，避免来回横跳）
9. 参考笔记 → T14（复盘结论按关键词触发换侧）
10. 经济差值 → T18（我穷敌更穷 → 强起打经济）
11. 合法闭集 → T10（规则也按同一份 allow 集发意图：已下包禁换侧、穷局禁 rush/split/fake）
12. 战术手册 _common 全部规则 → T10/T18/T19/T21/T28（逐条落到 RuleCoachCore / DecideBuyPlan / Sanitize）
13. 42 张战术卡 → T12/T13（卡片 when 条件翻译成规则分支）
14. advisory 21 条 → T34（分类：可执行的进模板；纯提示的标注 advisory、不承诺）
15. few-shot 2 条/图 → T34（补 Split/Fake/Save 的示例）
16. 槽位 look/hold/util → T23（规则手写槽位补字段，或规则直接用表槽位）
17. 锚点 anchor → T24（放开"仅 hold≠Default 才用锚点"的限制）

### 6.3 §6 数据质量问题（T28–T34 逐项修，见完整计划）

- **过期数字**：拆包 5 秒（三处：摘要、手册回防节、retake 卡 advisory；实际有钳 4s / 无钳 10s）；
  "最后 6 秒才强拆"与代码 11s/5s 阈值不一致。
- **硬编码**：CT 的 C4 掉落 intent 硬编码 `StackA`（`CoachThink:418`），不管掉落在哪一侧；
  reason 长度两处标准不一致（schema 60 字符 vs 手册 20 汉字）；ttl 三处语义
  （卡片 12–20s、schema 3–30、TTL 地板 18/12/10）；经济"中"档 2600 边界只在代码里。
- **when 条件可判定性**："已控"（12/42 条）规则层没有区域控制度量；"敌A≤1 / 敌B≥3"
  （约 20 条）规则层只有 heat，没有分侧敌人数；"上回合主攻"（9 条）同一短语两种语义——
  T 的 fake 卡指己方（可判定），CT 的 stack 卡指敌方（记忆里没有，T13 补）；
  "敌经济穷/富"可判定但 `RuleCoachCore` 不读；"购买=eco"可判定但 `RuleCoachCore` 不读 buyPlan。
- **覆盖率**：util 只在 rush/retake 卡；few-shot 只有 T default、CT retake_a；
  advisory 21 条未分类；`[areas]` 只有 6 个关键词映射，`AreaHint("Long"/"Short"/"Tunnel")`
  在 mirage/ancient 上回退到包点。
- **决策多样性**：规则能发意图 6 类（缺 Split/Fake/MID）；CT 回合开始多样性为 1（固定 Default）；
  角色 5→3 坍缩；规则槽位 look/hold/util 三个维度是空的。
- **§6 已核实三项**（2026-11-19，依据是快照而非 live 树——见 §8）：
  CoachRole 移速倍率属实但经 BotRole 坍缩；两套角色系统教练计划优先
  （`AssignSquadRoles(dynamic=true)` 全仓无调用者；回合内只有 `RefreshRolesForTeam`
  每 2s 补断档；唯一窄冲突窗口：教练计划本身缺 Entry/Anchor 时补断档可能提 Support，
  T25 修）；情报喂入属实但行号漂移（ReportContact 过滤点在 ReportContact 不在 OnEnemySeen；
  击杀喂入 `Combat.cpp:1017`，只喂敌方阵亡位置，无 2s 过滤）。

## 7. PIX-37 任务总表（37 项，8 个阶段）

完整版在 `docs/PIX-37-rules-coach-wiring-plan.md`（每项含清单对照、接线位置、做法、
验收点；附录有清单逐条对照总表）。这里是总览，实施时以完整版为准。

| 阶段 | 任务 | 一句话 | 验收点（局内检测） |
|---|---|---|---|
| Phase 0 验收基建 | T01 | 结构化教练决策日志（JSONL） | 1 局 8 回合，每回合每队 ≥1 行，字段齐全 |
|  | T02 | 场景驱动器 + `tools/coach/verify_wiring.py` | 脚本跑通全部场景并输出 PASS/FAIL 表 |
|  | T03 | golden-hash 基线重跑 | 新基线两次运行哈希一致 |
| Phase 1 P0 | T04 | @ 模板加载 | 每图 18 段全加载；@ 段命中 ≥1 次 |
|  | T05 | changesThisRound 接线 | 同回合第 3 次改意图被预算拦截 |
|  | T06 | hasBranch 接线 | hasBranch=true 且 20s 后分支触发 ≥1 次 |
|  | T07 | sameSideStreak 接线 | 连续 3 回合后换侧 |
|  | T08 | Sanitize 复用于规则 | 无情报 Stack 被消毒改 Default |
|  | T09 | Rotate 进表 + 进 schema | 转点走表；schema 含 rotate |
| Phase 2 决策质量 | T10 | 意图打分制 | 30 局意图分布熵显著上升；穷局不发禁集意图 |
|  | T11 | MID 控中意图 | 场景 target=MID ≥1 次 |
|  | T12 | RoundStart 发 Split / Fake | 30 局中 Split 与 Fake 各 ≥1 次 |
|  | T13 | 敌方习惯记忆 | 敌连打 A 两回合后 CT 重防 A |
|  | T14 | 道具/敌枪/联系人/lastOrder/笔记 | 场景逐条：烟开点、敌狙慢打、TimeLow 按接触定侧 |
|  | T15 | 情报 heat 进 RoundStart | A 热 B 冷时主攻 B |
|  | T16 | 减员变速解拦截 | 减员后 tempo 变、intent 不变 |
|  | T17 | HumanDied 规则分支 | 玩家阵亡后规则计划更新 |
| Phase 3 经济与赛点 | T18 | 跨回合经济状态机 + LossStreak | Force 输→Eco；连败 ≥2→Eco；我穷敌更穷→Force |
|  | T19 | 赛点分支 | 赛点 Force + Fast |
|  | T20 | 钳子感知回防 | 有钳 5s / 无钳 11s |
|  | T21 | 回防分工模板 | CT 1 拆 + 2 掩护 + 2 控路；T 守包三分工 |
| Phase 4 槽位与执行 | T22 | 按能力分槽 | AWP 分 Awper；钱少分 Save |
|  | T23 | 规则槽位补 look/hold/util | 日志槽位含非默认字段 ≥1 次 |
|  | T24 | 锚点放开给规则模板 | 日志含 anchor 坐标；BOT 走到锚点附近 |
|  | T25 | RefreshRolesForTeam 护栏 | 有意全员 Anchor 时不补断档 |
| Phase 5 执行层角色 | T26 | Lurk/Awper 真角色 | 日志 role 出现；行为抽查 |
|  | T27 | RoleTransitionSec 实现 | 换角色后倍率 3s 插值过渡 |
| Phase 6 数据质量 | T28 | 拆包数字校准 | 三处一致且与代码阈值一致 |
|  | T29 | C4 掉落 StackA 硬编码 | 掉 B 侧发 StackB |
|  | T30 | reason/ttl/经济档对齐 | grep 复核一致 |
|  | T31 | "已控"信号 | 新增区域控制度量 |
|  | T32 | 分侧敌人数 | 新增分侧敌方计数 |
|  | T33 | [areas] 映射退化 | 三图返回真实区域 |
|  | T34 | few-shot 补齐 + advisory 分类 | 覆盖 5 类意图；21 条逐条分类 |
| Phase 7 收口 | T35 | 30 局配对基准 | 拒绝率 23%→10% 以内；熵上升；Split/Fake >0 |
|  | T36 | coach_rounds.jsonl + gen 字段 | 新版规则计划行；Split/Fake/MID 样本 >0 |
|  | T37 | 全量验收汇总 | 37 项逐项有结论；未通过项有下一批归属 |

## 8. 硬约束（违反任何一条，验收直接不通过）

1. **不删除任何一个决策或数据。** 当前没有被实际调用的，想办法接线让它能够被调用，
   而不是因为没被用过就当作死代码删除。这是 Sun 明确的、严重的原则。
   审计提案原先写"接线或删"的项（hasBranch、sameSideStreak、RoleTransitionSec、
   Lurk/Awper 坍缩），一律只接线、只实现。验收不通过的项不删、不降级，留在下一批。
2. **确定性红线**：不碰 `rng_` 主链——打分只用确定性输入；`Save` 的 ±36 抖动是既有行为，
   不动它；humanizer 噪音（反应延迟抖动、瞄准散布、偶尔失误）必须走独立的
   `rng_humanizer_`，绝不碰全局 `rng_`。改动后按 T03 重跑 golden-hash 基线。
   回放哈希只是开发阶段调试工具，绝不允许长进项目里（编译开关隔离、单独只读模块、
   一行 guarded 钩子、benchmark runner 独立于游戏二进制）。
3. **验收标准**：只有在游戏中真正检测到规则教练使用了相关指令，才算完成。
   代码写完、编译通过、单测通过都只是支撑证据。每项验收点见完整版，
   验收由 Muse 独立跑——你交代码时附上你自己跑过的验收结果（PASS/FAIL + 日志证据），
   不要只写"已完成"。
4. **不做项**：不改 LLM 链路本身（`CoachTryLlm`、`CoachDrainLlm` 的调用逻辑、超时、
   advisory 门均不动；只动 schema 的 intent 枚举 T09 与摘要数字校准 T28）；
   不改调度守卫数值（IntentMinHold 10s、IntentChangeCooldown 6s、IntentRenewSec 8s、
   SilentSeconds 30s、T 18s 执行锁、调用预算 8 次均不动；只接线未生效的预算 T05
   与未读的旋钮 T07、T27）；不改买档阈值（1800/2600/3500 不动）；
   不替 Sun 定 minimind 的三个待定（意图集 11 vs 13、台词字段、日志形状）与 LoRA 训练本身。
5. **行号可能漂移**：本必读与完整计划的行号是 2026-11-18 live 树核实的；
   §6 三项的核实依据还是快照（Bots.cpp 2026-11-17 01:53、Combat.cpp 2026-11-17 14:20），
   不是 live 树。实现前先在你电脑上复核：清单末尾有三条 Select-String 复核命令，
   在仓库根目录跑一次确认快照未漂移。发现行号或函数变了，以 live 代码为准，
   不要硬按行号改。
6. **工作区与提交**：改动留在工作区，**不提交、不 push**，由 Sun 过目后再定。
   不要动 `D:\PixelEngineBuild-Archive\` 下的备份；不要删任何文件（包括你认为的
   "死文件"——先问 Sun）。
7. **顺序**：T01–T03（验收基建）先做，再按阶段顺序做；不要跳过 P0 直接做 P1。
   T26（真角色，动 BOT 决策树）量最大，放在 Phase 5 是有意的——前面的任务先把
   规则教练的决策面铺开。
8. **遇到冲突先停**：计划与 live 代码冲突、或某项验收点跑不通时，停下来向 Sun
   （或通过 Sun 问 Muse）汇报，不要猜一个做法继续往下写。Sun 的沉默是"现在不想"，
   不是默许；他没说完、任务没定型时不要动手。
9. **诚实汇报**：跑过的测试如实写结果（通过多少、失败多少、失败原因）；
   没验证到的明说没验证到，不要冒充验过。数字以实际看到的为准，不要编造。

## 9. 参考文件索引（都在你电脑仓库里）

| 文件 | 内容 |
|---|---|
| `docs/PIX-37-rules-coach-wiring-plan.md` | **完整实施计划（主文档）**：37 项任务的清单对照、接线位置、做法、验收点；附录清单逐条对照总表 |
| `docs/PIX-37-deepseek-brief.md` | 本必读 |
| `docs/rules-coach-inventory.md` | 清单全文：决策类型总表、数据源总表、决策→数据矩阵、§4 的 17 项、§5 的 19 项、§6 质量清单、附录统计与文件索引 |
| `docs/rules-coach-audit-proposal.md` | 审计提案全文：现状盘点、硬问题、教师天花板分析、改进提案、与 minimind/LoRA 的衔接 |
| `docs/llm-coach-v2-plan.md` | V2 评测基建：30 局配对基准方法、拒绝率口径 |
| `docs/PIX-33-part2-audio-plan.md`、`docs/PIX-34-part3-ops-plan.md`、`docs/PIX-35-part4-hud-plan.md`、`docs/PIX-36-part5-experience-plan.md` | 之前各部分的完整计划（格式参考：Linear 上只放任务清单，完整版放仓库 `docs/`） |
| `assets/config/coach/*.txt`、`assets/config/tactics/*.ini` | 战术手册与战术表（你要改的配置数据） |
| `tools/coach/materialize_cards.py` | 42 张战术卡落成 `[slots.*@Target]` 段的生成脚本（理解 @ 段来源用） |

## 10. 阅读与开工顺序

1. 读完本必读（你正在读）。
2. 读完整计划 `docs/PIX-37-rules-coach-wiring-plan.md`（重点是每项的"接线位置"与"验收点"，以及附录的对照总表）。
3. 需要细节时查 `docs/rules-coach-inventory.md`（清单）与 `docs/rules-coach-audit-proposal.md`（审计）。
4. 在 live 树上复核关键行号（清单末尾三条 Select-String 命令），确认无漂移。
5. 从 T01 开始：先把验收基建（日志 + 场景驱动器）跑通，再按 Phase 1→7 做。
6. 每完成一个 Phase：`D:\build.ps1` 编译 + `D:\build_tests.ps1` 跑 `gameplay_tests`，
   把结果（通过/失败数、失败原因）记下来；验收点逐项自测，写进
   `docs/PIX-37-rules-coach-wiring-acceptance.md`（T37 的汇总表，逐项 PASS/FAIL）。
7. 全部做完（或遇到 blocker）后向 Sun 汇报：哪些项 PASS（附日志证据）、哪些 FAIL、
   哪些没做、为什么。Muse 会独立重跑验收。

## 11. 常见坑（前人踩过的，别再踩）

- **拆包不是 5 秒**：有钳 4s / 无钳 10s。手册与摘要里写 5 秒的地方都是过期数字（T28 修）。
- **C4 掉落的 CT 意图不要写死 StackA**：按实际掉落侧发 StackA/StackB（T29 修）。
- **`BotRole` 只有 3 个值**：Lurk/Awper 在执行层是坍缩的，模板里大量用不等于执行层有
  （T26 修）。不要在没扩展 `BotRole` 之前就假设 Lurk/Awper 能用。
- **soft-KEEP 会吃掉 EnemyDown/AllyDown 分支**：计划有效时这两个分支跑不到，
  这是设计不是 bug（T16 只调 tempo，不动 KEEP）。
- **不要动转点机制的证据门**：`CoachTryRotate` 是规则层最完整的自适应，门槛别放宽。
- **不要为了"修"而删**：看到零引用的字段/旋钮/函数，第一反应是接线，不是删除。
- **`AssignSquadRoles(dynamic=true)` 全仓无调用者**：不要"修"它——回合内只有
  `RefreshRolesForTeam` 补断档，教练计划优先（T25 只修窄窗口）。
- **不要把快照行号当 live 行号**：§6 三项的核实依据是快照，正式引用前先复核（§8 第 5 条）。
- **不要改 `rng_`**：任何新随机性走 `rng_humanizer_`；打分用确定性输入。
- **不要替 Sun 做决定**：minimind 三个待定、LoRA 训练、PIX-32/33 的统一验收，
  都不在你的任务里。
