# BOT 决策全图（决策树 / 教练 / LLM）

> 用途：把当前 AI 的**全部决策规则**摊开成一张表，改行为之前先在这里定位。
> 代码位置以**函数名**为准（行号会随重构漂移）。
> 数据基线见文末第 8 节；修改流程见 `tools/dev_replay/WORKFLOW.md`。

---

## 0. 一页总览

```
L3  LLM（异步、可选）
     只输出：intent / target / tempo / ttl_s / slots / reason
     绝不输出：坐标、瞄准、是否开火
        │  闭集校验 + 语义纠偏（CS2DGameCoachAI.cpp: ParseCoachPlan）
        ▼
L2  教练（规则层，零延迟；LLM 回来后择优）
     事件队列(0.4s 合并) → CoachThink(KEEP 规则) → RuleCoach/LLM
     → SanitizeCoachPlan(整形) → 买枪档/保枪/转点/记忆
        │  TeamPlan{intent,target,tempo,slots[5]} + squad 角色/驻守点
        ▼
L1.5 小队（0.5s 刷一次）
     角色 Entry/Support/Anchor、集火目标、补枪许可、游走许可
        │  SquadOrder{role,focus,site,hold,canTrade,canRotate}
        ▼
L1  BOT 决策树（每帧）
     拆包 > 受击转向 > 扫描 > 补枪 > 撤退 > 目标 > 弹药/手雷
     > 目标引导(A*) > 姿态 > 机动 > 射击纪律
```

**唯一目标真源**：`CS2DGame::SetBotGoal()`（`GoalSource`: `Coach(2) > Squad(1)`），
同时镜像 `squad_[i].hold*`，因此不会再出现"两个驻守点"冲突。

### 修改索引（想改什么 → 去哪）

| 想改的东西 | 文件 / 函数 |
| --- | --- |
| 每帧行为优先级、开火条件、姿态 | `CS2DGameBots.cpp` → `UpdateBOTs()` |
| 单体移动/寻路/卡墙脱困 | `CS2DGameBots.cpp` → `FollowPathTo()`、`NudgeOutOfStuck()` |
| 掩体选择 | `CS2DGameSquad.cpp` → `FindCoverPoint()` |
| 角色/集火/补枪许可 | `CS2DGameSquad.cpp` → `AssignSquadRoles()`、`UpdateSquadFocus()` |
| 某张图的区域与默认分兵 | `assets/config/tactics/<map>.ini` |
| 规则教练的意图选择 | `CS2DGameCoach.cpp` → `RuleCoachCore()` |
| 计划整形（防单点/防重复） | `CS2DGameCoach.cpp` → `SanitizeCoachPlan()` |
| 买枪档、保枪、转点 | `CS2DGameCoach.cpp` → `DecideBuyPlan()`、`ShouldSaveNow()`、`CoachTryRotate()` |
| LLM 提示词 / 输出契约 | `CS2DGameCoachAI.cpp` → `BuildSystemPrompt()`、`BuildStateDigest()`、`kCoachSchema` |
| LLM 回包校验 | `CS2DGameCoachAI.cpp` → `ParseCoachPlan()` |
| 数值（速度/听觉/补枪距离…） | `CS2D/Tuning.hpp` |
| 战术手册（自然语言，喂给 LLM） | `assets/config/coach/<map>.txt`、`_common.txt` |

---

## 1. L1 BOT 决策树（每帧，**严格按此顺序**）

`CS2DGameBots.cpp :: UpdateBOTs()`，每个 BOT 一个大循环，命中即 `continue`（后面的不再执行）。

| # | 条件 | 动作 | 备注 |
| --- | --- | --- | --- |
| 0 | 每帧开头 | 递减 `blindTimer / lastHitTimer / defuseCooldown / wanderTimer`，`scanPhase += dt` | 状态衰减 |
| 1 | `ai.defusing` 且 `BotCanDefuse()` 为假 | 放弃拆包，`defuseCooldown = 2s`，落到后续逻辑 | 见 §1.1 |
| 2 | `ai.defusing` 且可拆 | **原地不动**（速度清零）并 `continue` | 拆包中不做任何别的事 |
| 3 | `lastHitTimer > 0` 且不在拆包 | 把枪口转向威胁来源 | 防背后被打还在贴包 |
| 4 | 无目标且静止 | 原地扫描：有听觉警报 → 朝声源 ±16°；否则以扫描中心 ±60° 缓慢摆头 | 视野是 120° 扇形，不扫描会永远背对敌人 |
| 5 | 补枪：队友 2s 内阵亡 + 同队 + `!tradePushed` + 非 Anchor + `canTrade` + 距离 ∈ [40, 350] | 冲向队友阵亡点（`tradePushed=true` 每回合一次） | `Tuning::TradeMinDist/MaxDist` |
| 6 | `retreatTimer > 0` 且 HP ≤ 35 | 找掩体（`FindCoverPoint`），顺带补弹 | `RetreatHealth/RetreatKeepHealth/RetreatDuration` |
| 7 | 目标选择 | 见 §1.2 | 可见 > 听声下包/拆包 > 听声前压 |
| 8 | 听到手雷落地 且 距离 < 110 | 反向躲开 40px（寻路），`continue` | 活下来优先于守点 |
| 9 | 静步判定 | `stealth = !target && (save计划 \|\| canRotate \|\| 听到脚步)` | 影响脚步音量与移速 |
| 10 | 主武器弹匣+备弹全空 | 切副武器 → 切近战（无弹可打时不空放） | 换枪有音效 |
| 11 | 换弹 | 脱战（看不见人）就补满；交战中仅当弹匣 ≤ 25% | `Tuning::LowAmmoFraction` |
| 12 | 手雷 | 无目标 + 有情报点 + 有雷 + 冷却好 + 距离 ∈ [90, 230] | 冷却 7s |
| 13 | **目标引导** | 见 §1.3 | 决定 destX/destY，A* 接管移动 |
| 14 | 姿态推导 | 见 §1.4 | 只在"有目标"时执行 |
| 15 | 交火机动 | 见 §1.5 | 寻路接管时不生效 |
| 16 | 射击纪律 | 见 §1.6 | 决定开火/节奏/散布 |
| 17 | 首次接火上报 | 每队每回合一次，供教练判断主攻方向 | `CoachEventType::EnemySpotted` |

### 1.1 拆包判定 `BotCanDefuse()`

| 条件 | 结果 |
| --- | --- |
| 非爆破模式 / 未下包 / 非存活 CT / 距包 > 26px / 计时 ≤ 0 | 不能拆 |
| **炸弹剩余 ≤ 6s** | **强制可拆**（无视敌人） |
| `defuseCooldown > 0`（刚放弃过） | 不能拆 |
| `lastHitTimer > 0`（正在挨打） | 不能拆 |
| 看得见任何敌人 | 不能拆 |
| 其他 | 可拆（不再因为"情报里包点附近有 T"就放弃） |

指定拆包手 `PickDefuseBot()`：优先"没挨打、没冷却、没看见敌人"的**最近** CT；
最后 6 秒退化为最近的存活 CT。

### 1.2 目标选择顺序

| 优先级 | 来源 | 条件 | 说明 |
| --- | --- | --- | --- |
| 1 | **可见敌人** | `FindVisibleEnemy()`：半径 100、120° 扇形、烟雾遮挡、`HasLineOfSight`，致盲时不可见 | 最近可见者 |
| 2 | **听声扑人** | 对方正在下包/拆包，距离 ≤ 300 | 不分朝向；T 听下包声、CT 听拆包声；命中散布 ×0.72 |
| 3 | 听声前压 | 近处枪声 ≤ 230 / 换弹 ≤ 190，且无目标、非 Anchor、未下包 | 只顶掉"软目标"（去驻守点） |
| 4 | 团队情报 | `ContactsOf(team)` 最新情报点 | 视野外靠队友共享，不做全图透视 |

> 目标用于：开火、姿态、交火机动；**目标本身不等于目的地**（目的地在 §1.3）。

### 1.3 目标引导（目的地）优先级

按顺序取第一个成立的：

| 顺序 | 条件 | 目的地 | 备注 |
| --- | --- | --- | --- |
| 1 | C4 掉落 且 本 BOT 是最近的 2 个 T 之一 | 掉落点 | `Tuning::BombRecoverCount` |
| 2 | 已下包 · T | 以包为心 85px 的环形站位（按 BOT 序号取角度） | 不挤在包上 |
| 3 | 已下包 · CT · 我是拆包手 | 包点 | — |
| 4 | 已下包 · CT · 非拆包手 | 包点附近情报点的 34px 环 / 140px 掩护环 | 先清点再架枪 |
| 5 | 未下包 · T · `ai.hasPlanHold` | **目标包点中心**（进攻期代码会把区域中心换成包点中心） | 见 Step 1 的修正 |
| 6 | 未下包 · CT · `ai.hasPlanHold` | 教练/小队给的驻守点 | 唯一真源 |
| 7 | 听觉前压（softHold 才能被顶掉） | 声源 | 硬目标（包点/拆包）不被顶 |
| 8 | 有团队情报 | 情报点 | — |
| 9 | 有可见敌人 | 敌人位置 | — |
| 10 | 巡逻兜底 | 有驻守点：岗位 ±90px；Entry/Support：敌方出生点质心 ±260px；否则：自身 ±300px | 目标缓存 `wanderTimer`，避免每帧抖动 |

### 1.4 姿态 `BotStance`（仅在"有目标"时推导）

| 顺序 | 条件 | 姿态 |
| --- | --- | --- |
| 1 | 致盲中 / HP < 30 / 无弹可打 | `Retreat` |
| 2 | 角色 = Anchor，或教练 tempo = Slow | `Defensive` |
| 3 | 角色 = Entry，或距离 < 180×0.6×tempo系数 | `Aggressive` |
| 4 | 有驻守点 且 距离 > 武器射程 | `MoveToCover` |
| 5 | 其他 | `Defensive` |

`tempo` 系数：`Fast 1.25 / Normal 1.00 / Slow 0.80`。

### 1.5 交火机动 `BotManeuver`

| 姿态 | 机动 | 位移规则 |
| --- | --- | --- |
| Retreat | `BackOff` | 远离目标（距离 < ideal×1.6 时后退） |
| Aggressive / Defensive | `Strafe` | 太远前进、太近后退、距离合适则侧向拉枪；被墙挡 → 换反侧 → 都不行原地 |
| MoveToCover | `None` | 交给寻路找掩体 |

`idealDist = 近战 12 / 枪械 射程×0.55`；速度 = `BotMoveSpeed × 武器倍率 × 角色倍率`，
角色倍率 `Entry 1.0 / Support 0.8 / Anchor 0.55`。

### 1.6 射击纪律

| 条件 | 射速倍率 | 散布倍率 |
| --- | --- | --- |
| 换弹中 / 无弹 | 不开火 | — |
| 距离 > 0.8×射程（点射） | ×2.5（更慢） | ×0.4（更准） |
| 距离 < 60 | ×0.8 | ×0.7 |
| 60 ≤ 距离 < 150 | ×1.0 | ×1.0 |
| 距离 ≥ 150 | ×1.5 | ×1.3 |
| Retreat / MoveToCover 且距离 > 50 | 不开火 | — |
| 近战 且距离 > 25 | 不开火 | — |

命中判定：每次开火 30% 走"精确"（抖动 0.08）、70% 走"打偏"（抖动 0.8）；
听声锁定时散布再 ×0.72。

---

## 2. L1.5 小队层（`CS2DGameSquad.cpp`，0.5s 节流）

### 2.1 角色分配 `AssignSquadRoles()`

1. 算敌方质心（存活敌人平均位置）。
2. 同队 BOT 按"离敌方质心距离"升序排序。
3. 前 `max(1,(n+2)/3)` 名 = `Entry`（5 人时 2 名）；
   后 `max(1,(n+3)/4)` 名 = `Anchor`（5 人时 2 名）；其余 = `Support`。
4. 爆炸图未下包时，同时按包点轮转分配 `site` 与驻守点（经 `SetBotGoal(Squad)`，不会覆盖教练计划）。

### 2.2 集火目标 `UpdateSquadFocus()`

只评估**团队情报**里的敌人（不做全图透视），评分 = 距离，再叠加修正，取最小者：

| 修正 | 值 |
| --- | --- |
| T 已下包 且敌人在包点 140px 内（可能在拆包） | −150 |
| 情报点周围每有一名队友（160px 内） | −45 / 人 |
| 情报点附近有队友在换弹或 HP<40（120px 内） | −60 |

### 2.3 许可

| 字段 | 来源 | 作用 |
| --- | --- | --- |
| `canTrade` | 教练 `SlotOrder.canTrade` | 是否允许补枪 |
| `canRotate` | 教练 `Lurk` 角色为 true | 允许游走换点（每 6s 在驻守点 30–80px 内换位） |
| `role` | 教练槽位 / 小队轮转 | 速度、补枪资格、追击距离 |

---

## 3. L2 教练层（`CS2DGameCoach.cpp`）

### 3.1 事件与合并

事件入队 `CoachOnEvent()`，**0.4s 合并窗口**内取优先级最高的一条：

| 事件 | 优先级 |
| --- | --- |
| RoundStart | 100 |
| BombPlanted | 95 |
| DefuseStarted | 90 |
| HumanDied | 85 |
| EnemyDown | 80 |
| AllyDown | 78 |
| EnemySpotted | 70 |
| MatchPoint | 60 |
| TimeLow | 55 |
| EconomyLow | 40 |
| DefuseAborted | 35 |
| 其他（Heartbeat 等） | 50 |

### 3.2 `CoachThink()` 的 KEEP 规则（决定"要不要重新规划"）

| 顺序 | 条件 | 行为 |
| --- | --- | --- |
| 1 | 已有计划未过期 + 未下包 + **软事件**（Heartbeat/EnemyDown/AllyDown/EnemySpotted/DefuseAborted） | 保留计划，只调 tempo（EnemyDown→Fast，AllyDown→Slow） |
| 2 | `stickyLlm` 且计划未过期、非关键事件 | 保留 LLM 计划 |
| 3 | T 未下包、已有 A/B 目标、计划创建 < 18s、非（RoundStart/EconomyLow/TimeLow/MatchPoint/BombPlanted） | **执行锁定**，不换边 |
| 4 | 其他 | 重新规划：`RuleCoach()`（+ 异步 LLM） |

### 3.3 意图表 `PlanIntent`

| 意图 | 语义 | 有效方 |
| --- | --- | --- |
| `Default` | 默认分兵（A/B 都有人） | T / CT |
| `RushA` / `RushB` | 全员快攻一点 | T |
| `SplitA` / `SplitB` | 分路夹击 | T |
| `StackA` / `StackB` | 重兵堆点（防守/守包） | CT |
| `RetakeA` / `RetakeB` | 回防（**仅已下包**） | CT |
| `RotateA` / `RotateB` | 有证据的转点 | T |
| `FakeThenOther` | 假打再转 | T |
| `Save` | 保枪（仅经济崩且未下包） | T / CT |

### 3.4 规则教练事件 → 意图（`RuleCoachCore()`）

| 触发 | 意图 | 备注 |
| --- | --- | --- |
| 已下包 · T · 人数不劣 | StackA/StackB（按包点侧） | 交叉火力守包；人数劣势时 Default+Slow（残局守包） |
| 已下包 · CT | RetakeA/RetakeB | 剩余 ≤11s 时 tempo=Fast（强拆） |
| `BombPlanted` 事件 | 同上 | — |
| `DefuseStarted` | T: Stack 回防 / CT: Retake 掩护拆弹 | — |
| `EnemyDown` | Default（tempo Fast） | 对面减员，前压 |
| `AllyDown` | Default（tempo Slow） | 收缩 |
| `EconomyLow` 且 `ShouldSaveNow` | Save | — |
| `TimeLow` | T: Rush（沿用当前主攻侧）；CT: Default 收位 | — |
| 回合开始 · 经济崩 | Save | — |
| 回合开始 · T · A 连败 | RushB | 记忆驱动换边 |
| 回合开始 · T · B 连败 | RushA | — |
| 回合开始 · T · 其他 | Default | 分兵找机会 |
| 回合开始 · CT | Default | A/B 各两人 + 中路 |

### 3.5 计划整形 `SanitizeCoachPlan()`（防"每回合全堆一点"）

| 条件 | 处理 |
| --- | --- |
| T `Default` 且 5 个槽位全在同一侧 | 强制改回 A/B 分兵 |
| T 进攻意图 且 与上回合主攻侧相同 | 强制改回 A/B 分兵 |
| CT `Default` 且全在同一侧 | 强制改回 A/B/中路 |
| CT `StackA/B` 但情报不足 2 条 或 威胁差 < 0.8 | 改回默认分兵（不赌点） |

### 3.6 转点 `CoachTryRotate()` / `CanRotateNow()`

判定顺序：**时间 → 人数 → 冷却 → 证据**（证据 = 两个包点威胁度差，未知不算空）。
中止条件（`CoachUpdate` 每帧检查）：已下包 / 非交战期 / 剩余 < 25s / 人数落后 ≥2 → 回到原目标。

### 3.7 买枪与保枪

`DecideBuyPlan()`：

| 条件 | 档位 |
| --- | --- |
| 非爆破模式 | Full |
| 第 1 回合 | Force（手枪局也买甲/雷） |
| `ShouldSaveNow` | Eco |
| 平均 ≥ 3500 且 对手 < 2200 | AntiEco（SMG+甲） |
| 平均 ≥ 3500 | Full |
| 平均 ≥ 2600 | Half |
| 平均 ≥ 1800 | Force |
| 其他 | Eco |

`ShouldSaveNow()` 全部满足才保枪：未下包 + 回合内 ≤45s + 不是人数优势 +
T 未站上包点 + 经济低于阈值；同时 `RuleCoach` 里 `Save` 意图只在"经济崩且未下包"成立。

### 3.8 记忆

`RoundMemo`（最近 10 回合）：计划意图、目标侧、胜负、存活、是否下包、首触区域、
阵亡位置。用途：A/B 连败换边、LLM 的"近局"摘要、赛后复盘一句话。

---

## 4. L3 LLM（`CS2DGameCoachAI.cpp` + `LLM/LLMBotController.cpp`）

### 4.1 什么时候请求

只在 `RoundStart / BombPlanted / DefuseStarted / TimeLow / MatchPoint` 发起；
**Heartbeat 不请求**（省 token），同队同时只允许 1 个在途请求。
超时/失败 → 继续用规则计划（规则层永远先落地，零延迟）。

### 4.2 System prompt 结构（`BuildSystemPrompt`，按地图缓存）

1. 角色："你是 CS2D 战队主教练。只做战术决策，不做瞄准与走位。"
2. 输出契约：**只输出一个 JSON 对象**，给出示例。
3. 字段说明：`intent`（只能从 user 行白名单选）、`target`、`tempo`、
   `ttl_s`（3–30）、`slots`（≤5，slot 0–4，area 必须是可选区域）、`reason`（≤20 字）。
4. 当前地图 id + **可选区域清单**（来自 TMJ 的 areas）。
5. 战术手册（`assets/config/coach/<map>.txt` + `_common.txt`，按 10KB 截断）。

### 4.3 User 消息（`BuildStateDigest`，每回合变）

```
优先级:炸弹>经济>减员>笔记
R12 T 6:5 1:23 我4 敌3
炸弹:已下@A_Site 倒计时32s 拆包需5s 经济中 购买=Full
敌分布:A_Site=0.8 B_Site=0.2 (暂无情报时标注)
敌: A_Site x1(3s) Mid x1(6s)
上条:<上一次指令摘要>
参考笔记(勿覆盖炸弹/经济): <最新一条复盘>
事件: 敌方减员@Mid
允许:intent=default,rush_a,rush_b,... target=A,B,- area_n=35
近局: R11 default->A 负 首触@A_Long; R10 rush_b->B 胜 已下包
```

**合法闭集**：`intent` / `target` / `area` 均由 `BuildAllowSet()` 按当前局势动态收窄，
并同时用于 `json_schema` 的 enum，模型越界→游戏侧整份拒绝。

### 4.4 游戏侧二次校验（`ParseCoachPlan`）

| 规则 | 处理 |
| --- | --- |
| JSON 解析失败 / 缺 intent | 整份拒绝，用规则计划 |
| intent 不在白名单 | 整份拒绝 |
| target 不在白名单 | 整份拒绝 |
| 非下包却说 retake | 纠正为 `Default` |
| `Save` 但经济不穷 / 已下包 | 纠正为 `Default`（下包时改为 RetakeA） |
| CT 说 rush/split | 纠正为对应 `Stack` |
| T 说 retake/stack | 纠正为 `Default` |
| 已下包但意图/目标不在包点一侧 | **整份拒绝** |
| `Rotate*` 但无证据 / 目标侧不符 | 整份拒绝 |
| 穷局主动进攻 | 整份拒绝（只允许 Save/Default/Stack） |
| slot 的 area 不在区域表 | 跳过该 slot |
| area 大小写不匹配 | 统一成引擎里的精确名 |

### 4.5 回退与粘性

- 规则计划**先落地**（零延迟），LLM 回包后若通过校验则替换（`stickyLlm = true`）。
- 粘性期间非关键事件不覆盖；计划到期 / 关键事件（下包、时间不足、赛点…）才重规划。
- 连续失败不会阻塞：`llmPending` 超时清空，规则层继续跑。

---

## 5. 战术表（Step 2）

`assets/config/tactics/<mapId>.ini`：

```ini
[areas]                ; 教练关键词 → 地图真实区域名
A_Site = A_Site
Long   = Long

[slots.T.Default]      ; 角色|区域别名[|noTrade][|rotate]
0 = Entry|Long
1 = Support|A_Site
2 = Lurk|Mid|rotate
3 = Anchor|Tunnel
4 = Support|B_Site
```

- 只覆盖**通用分兵模板**（`TeamPlan.templateDefault`），事件整形计划不受影响。
- 缺表/缺项 → 回退代码默认；加载时校验区域是否存在，不存在会告警。
- 加载后 `RuleCoach` 的 `ApplyTacticsSlots()` 按 `(队伍, 意图)` 套用。

---

## 6. 关键数值（`CS2D/Tuning.hpp`）

| 类别 | 参数 | 值 |
| --- | --- | --- |
| 视野 | Boss 视野半径 / 扇形角 / 软边 | 100 / 120° / 20 |
| 移动 | BOT 基础速度 / 角色倍率 | 60 / Entry 1.0、Support 0.8、Anchor 0.55 |
| 寻路 | 重算间隔 / 目标漂移阈值 / 路点判定 | 0.8s / 70px / 8px |
| 撤退 | 触发血量 / 维持血量 / 时长 | 30 / 35 / 2s |
| 补枪 | 最远 / 最近 | 350 / 40 |
| 换弹 | 低弹阈值 | 25% |
| 听觉 | 下包/拆包 / 脚步 / 枪声(手枪~狙击) | 300 / 130 / 520–? |
| 前压 | 枪声 / 换弹 | 230 / 190 |
| 手雷 | 投掷距离 / 冷却 / 危险半径 | 90–230 / 7s / 110 |
| 集火 | 刷新 / 队友半径 / 掩护半径 / 权重 | 0.5s / 160 / 120 / 45、60 |
| 守包 | T 环形 / CT 掩护环 | 85 / 90 |
| C4 回收 | 回收人数 | 2 |

---

## 7. 叠加出来的"实际行为"示例

**T 回合开局的完整链路**：

1. RoundStart 事件（优先级 100）→ `CoachThink` → 规则给 `Default`（Long 突破 / A 点支援 / Mid 游走 / B 洞断后 / B 点支援）。
2. 同步发 LLM（异步），回包若合规则替换计划并粘性 12s。
3. `CoachApplyPlan` → 5 个槽位映射到 5 个 BOT：`SetBotGoal(Coach)` 写驻守点 + 角色 + canTrade/canRotate。
4. 每帧 `UpdateBOTs`：无目标 → 走 A* 去目标点；途中听到脚步/枪声 → 静步或前压；
   看见敌人 → 姿态推导 → 交火机动 → 射击纪律。
5. 队友阵亡 → 附近非 Anchor 的队友补枪；血量 <30 → 找掩体；
6. 下包 → `BombPlanted` 事件 → 教练改 Stack（守包环）；CT 转 Retake。

---

## 8. 当前状态（可对照修改）

**基线（60 局 = 3 图 × seed 1–20，tag=137839a）**

| 指标 | 值 |
| --- | --- |
| T 胜率 | 40.0% |
| 平均回合数 / 时长 | 13.0 / 4123 tick |
| 下包 / 拆包（每局） | 10.82 / 8.23 |
| 原地不动 / 发呆 | 30.0% / 0.5% |
| 路径效率（overall / 短 / 中 / 长） | 0.47 / 0.11 / 0.62 / 0.83 |
| 每回合伤害 | 795 |
| 首杀转化率 | 64.9% |
| AI 冲突/局 | 0.05 |

**已知缺口 / 下一步**

1. `mirage` / `ancient_ruins` 的 `Long/Short/Tunnel` 仍是旧行为回退值（表里标了 `TODO(战术调参)`）。
2. 拆包点人数基线 1.31（有时只有 1 人回防）——可考虑教练在 Retake 时保证 ≥2 人到点。
3. CT `Stack` 的情报门槛（≥2 条 且 威胁差 ≥0.8）是经验值，值得用 `--trace` 标定。
4. LLM 目前只能选 intent/target/tempo + 槽位区域；若要用战术表驱动更多，需扩展 schema。
5. `--trace` 已可用（`kind ∈ target/stance/maneuver/goal/defusing`），可作为调参依据。
