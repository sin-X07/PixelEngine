# 规则教练决策数据与决策类型总清单

> 基准：Sun 电脑 live 工作树，2026-11-18 当晚 sftp 拉取核实。
> 文件：`CS2DGameCoach.cpp`（1567 行）、`CS2DGameCoachAI.cpp`（1278 行）、
> `CS2DGameSquad.cpp`（746 行）、`Coach/CoachTypes.hpp`、`Coach/TacticsTable.hpp`、
> `TacticalIntel.*`、`CS2DGame.hpp`、`Tuning.hpp`、`assets/config/coach/*.txt`、
> `assets/config/tactics/*.ini`。本地副本：`~/workspace/your_files/rules-coach/code/`。
>
> **原则（Sun 2026-11-18）**：不删除任何一个决策或数据。当前没有被实际调用的，
> 想办法接线让它能够被调用，而不是因为没被用过就当作死代码删除。
> 本清单里所有未接线项都保留，并给出接线点；没有任何一项建议删除。
>
> 状态标记：✅ 规则教练已用 ｜ 🔶 只给 LLM，规则教练还没用 ｜
> ⛔ 已存在但未接线（任何路径都没用）｜ 📦 游戏层已有，教练层可接 ｜
> ⚠️ 数据有质量问题（见 §6）

---

## §1 决策类型总表

### 1.1 战术意图 PlanIntent（13 个）

`Coach/CoachTypes.hpp:16-27`。`TacticsIntentKey` / `IntentLabel` / `ParseIntent`
三处映射都支持全部 13 个；但 schema（LLM 可见）只有 11 个，`TacticsTable::kIntents`
只有 11 个。

| 意图 | 含义 | 规则能发 | LLM 能发 | 备注 |
|---|---|---|---|---|
| Default | 默认分兵 | ✅ | ✅ | 回合开始默认 / 事件默认 / 消毒兜底 |
| RushA / RushB | 强打 A / B | ✅ | ✅ | 规则：仅 0 胜连败或 TimeLow；卡片：满起 + 敌分布条件 |
| SplitA / SplitB | 分路夹击 A / B | ⛔ | ✅ | 规则 `RuleCoachCore` 全文无赋值；只有 LLM 能发 |
| StackA / StackB | 重兵堆点 A / B | ✅ | ✅ | 规则：仅下包后 T 守包；卡片：CT 回合开始也可发 |
| RetakeA / RetakeB | 回防 A / B | ✅ | ✅ | 规则：仅下包后 CT；卡片同 |
| Save | 保枪 | ✅ | ✅ | 规则：经济崩；卡片：eco_save |
| RotateA / RotateB | 转点到 A / B | ✅（仅转点机制） | ⛔ | `CoachTryRotate` 发；schema 里没有，LLM 开不出 |
| FakeThenOther | 假打后转点 | ⛔ | ✅ | 规则全文无赋值；只有 LLM 能发；`branchTarget` = 真打侧 |

### 1.2 目标 target（4 个值）

`"A"` / `"B"` / `"MID"` / `""`（空）。schema 允许全部 4 个（`CoachAI.cpp:25`）。
规则实际只产生 `"A"` / `"B"` / `""`——**MID 没有任何规则分支会设**（⛔）。
C4 掉落时 CT 的 target 是区域名（如 `B_Apts`，不是 `"B"`，见 §6）。

### 1.3 节奏 Tempo（3 档）

`Slow` / `Normal` / `Fast`。激进系数 `TempoAggression`（`CoachTypes.hpp:91-97`）：
Fast 1.25、Normal 1.00、Slow 0.80。规则用法：EnemyDown → Fast、AllyDown → Slow、
Save → Slow、下包后 T 残局 → Slow、mustRush → Fast、TimeLow → Fast。

### 1.4 购买计划 BuyPlan（5 档）

`Eco` / `Half` / `Force` / `Full` / `AntiEco`（`CoachTypes.hpp:32`）。
`DecideBuyPlan`（`Coach.cpp:1409-1430`）：手枪局（回合 ≤1）→ Force；
`ShouldSaveNow` → Eco；均值 ≥3500 → Full（敌均 <2200 时叠加 AntiEco）；
≥2600 → Half；≥1800 → Force；否则 Eco。非爆破模式直接 Full。
`BotBuyLoadout` 接 AntiEco：SMG + 甲 + 满道具、不买 AWP/步枪（据 11-10 快照审计，
`CS2DGameRound.cpp:239/345`）。

### 1.5 角色：CoachRole（5）vs BotRole（3）对照

| CoachRole（教练词汇） | BotRole（执行层） | 下发映射（`CoachApplyPlan`，`Coach.cpp:1142-1146`） | 状态 |
|---|---|---|---|
| Entry 突破手 | Entry | Entry → Entry | ✅ |
| Support 支援 | Support | Support → Support | ✅ |
| Anchor 断后/守点 | Anchor | Anchor → Anchor | ✅ |
| Lurk 绕后/断后 | —（无此值） | Lurk → **Entry** | ⛔ 坍缩 |
| Awper 架狙 | —（无此值） | Awper → **Support**（走 default 分支） | ⛔ 坍缩 |

`CoachRole` 注释称"影响移速倍率、补枪资格、远追许可"；执行层 `BotRole`
只有 3 个值（`CS2DGame.hpp:54-58`），`SquadOrder` 也只有这 3 个角色。
槽位模板里 Lurk / Awper 出现频繁（42 张卡里大量使用），但下发时全部坍缩。

### 1.6 站姿 HoldStyle（4 个）

`Default`（沿用现有行为）/ `Anchor`（架点，不轻易前压、不参与搜点）/
`OffAngle`（侧身位，不站正面）/ `Passive`（被动，只打贴脸、不主动拉枪）
（`CoachTypes.hpp:38-44`）。数据来源：战术卡 `hold=` 与 ini `hold=`。
执行点：`CoachApplyPlan` 里 `hold!= Default` 才用战术表锚点——规则模板生成的槽位
`hold` 恒为 Default，所以**规则计划永远不用锚点**（⛔，仅 LLM 模板路径用）。

### 1.7 槽位 SlotOrder（9 个字段）

`CoachTypes.hpp:46-60`。每个槽位：

| 字段 | 含义 | 来源 |
|---|---|---|
| role | CoachRole（5 个） | 规则代码 / 战术表 / LLM |
| area | 目标区域名（空 = 不指定） | 同上 |
| canTrade | 允许补枪（默认 true） | 表 `noTrade` 可关；规则 Save 位设 false |
| canRotate | 允许自行换点（默认 false） | 表 `rotate` 可开；规则 Lurk 位（mid）设 true |
| lookArea | 到位后看向哪个区域 | 表 `look=` / 卡片 `look=` |
| lookAngle | 直接给角度（弧度，-99 = 未指定） | 表 `lookDeg=` |
| hold | HoldStyle（默认 Default） | 表 `hold=` / 卡片 `hold=` |
| util | 0 无 / 1 烟 / 2 闪 / 3 火 / 4 雷 | 表 `util=kind:Area` / 卡片 `util=` |
| utilArea | 投掷目标区域 | 同上 |

`ParseSlot` 语法（`TacticsTable.hpp`）：`Role|AreaAlias[|noTrade][|rotate][|look=<Area>|lookDeg=<度>|hold=anchor|offangle|passive|util=smoke|flash|molotov|he:Area]`。
规则代码手写的槽位只填 role/area/canTrade/canRotate 四项，look/hold/util 恒为默认——
**规则计划的槽位永远没有 look/hold/util**（⛔，只有表/LLM 路径有）。

### 1.8 事件 CoachEventType（12 种）

`CoachTypes.hpp:116-130`。优先级（`CoachOnEvent`，`Coach.cpp:135-164`，
据 11-10 快照审计）：RoundStart 100、BombPlanted 95、DefuseStarted 90、HumanDied 85、
EnemyDown 80、AllyDown 78、EnemySpotted 70、MatchPoint 60、TimeLow 55、EconomyLow 40、
Heartbeat 最低。合并窗口 0.4s（`Coach.cpp:159`）。

`CoachThink` 分类（`Coach.cpp:322-337`）：
- **criticalEv**（7 种，绕过 MinHold/冷却/sticky/T 锁）：RoundStart、BombPlanted、
DefuseStarted、TimeLow、EconomyLow、MatchPoint、HumanDied。
- **softEv**（5 种，计划仍有效时 KEEP、不重算）：Heartbeat、EnemyDown、AllyDown、
EnemySpotted、DefuseAborted。

注意：`RuleCoachCore` 有 EnemyDown / AllyDown 分支，但 soft-KEEP 会先拦截——
计划未过期且未下包时，这两个分支**跑不到**（规则层"减员变速"只在计划过期后才生效）。
MatchPoint / HumanDied 是 criticalEv，但 `RuleCoachCore` 没有它们的分支，
会落到回合开始逻辑（⛔：事件有，决策无）。

### 1.9 计划 TeamPlan（17 个字段）

`CoachTypes.hpp:63-88`：intent、target、tempo、slots[5]、reason、source（"rule"/"llm"）、
reshaped（被 Sanitize 整形过）、latencyWall（LLM 墙钟延迟，规则计划为 0）、
templateDefault（由通用分兵模板生成，可被战术表覆盖）、gen（代际号，旧回包丢弃）、
createdAt、valid、expiresAt、hasBranch、branchAfterSec、branchIntent、branchTarget、
branchTempo。

---

## §2 数据源总表

### 2.1 战术表 `assets/config/tactics/*.ini`（每图一份）

`TacticsTable::Load`（`TacticsTable.hpp:60-140`）。三图统计（实测）：

| 段 | dust_2 | mirage | ancient_ruins | 状态 |
|---|---|---|---|---|
| `[areas]` 关键词映射 | 6 键 | 6 键 | 6 键 | ✅ |
| `[slots.*]` plain（无 @） | 4 段 | 4 段 | 4 段 | ✅ 已加载 |
| `[slots.*@Target]` | 14 段 | 14 段 | 14 段 | ⛔ **未加载**（Load 只读 plain） |
| `[look.*]`（区域→看向角度，可多条） | 70 段 | 64 段 | 58 段 | ✅ |
| `[anchor.*]`（区域→驻守锚点 x/y） | 70 段 | 64 段 | 58 段 | ✅（仅 hold≠Default 时用） |
| `[perception]` + `[perception.0-3]` | 5 段 | 5 段 | 5 段 | ✅ |
| 区域总数（look/anchor ÷ 2） | 35 区 | 32 区 | 29 区 | — |
| 槽位行含 `util=` | 14 条 | 14 条 | 14 条 | ✅（仅 rush/retake 卡） |
| 槽位行含 `hold=` | 50 条 | 49 条 | 50 条 | ✅ |
| 槽位行含 `look=` | 70 条 | 70 条 | 70 条 | ✅ |

plain 4 段：`[slots.T.Default]`、`[slots.CT.Default]`、`[slots.T.RushA]`、`[slots.T.RushB]`。
@ 14 段：`T.Default@-`、`T.Default@A`、`T.Default@B`、`T.RushA@A`、`T.RushB@B`、
`T.SplitA@A`、`T.SplitB@B`、`T.Fake@A`（dust_2/ancient）或 `T.Fake@B`（mirage）、
`T.Save@-`、`CT.Default@-`、`CT.StackA@A`、`CT.StackB@B`、`CT.RetakeA@A`、`CT.RetakeB@B`。
`kIntents` 11 个名（含 Split/Fake/Save/Stack/Retake），但 plain 段只有 4 个存在——
Split/Fake/Save/Stack/Retake 的表只存在于 @ 段里。

`Perception`（4 档难度 × 半径/角度/反应/ settle）：默认取 `Tuning::BotFovRadiusMul`、
`BotFovAngleMul`、`BotReactionSecs`、`AimSettleSecs`；ini 可覆盖；`botProfile` 0–7，
默认 4。

### 2.2 战术手册 `assets/config/coach/*.txt`（只喂 LLM，规则不用）

`_common.txt` 65 行 + 每图 ~233 行、14 张卡。加载顺序：`_common` + 各图文件，
拼成 LLM system prompt；**规则教练不读这些文件**（🔶）。

**_common.txt 全部规则（按节列出，供检验）：**
- 7 条：优先级（炸弹 > 经济 > 减员/人数 > 接触信息 > 笔记）；已下包 CT 只用
retake、T 只用 stack（或 target=包点侧的 default），禁因笔记改打另一侧；穷局优先 save，
T 禁 rush/split/fake，CT 只 default/stack/save；连续两回合同 intent+同 target 失败 →
必须换点或改 fake/split/default，禁第三次同 intent+target；开局前 20 秒偏 default 换信息，
中段再 execute，时间不足才 rush；不要每回合重复同一侧速推；reason ≤20 汉字、只写当下事实；
选模板先按阵营/炸弹状态筛、再按经济选最接近的一条，模板 ttl 仅建议、输出取 3~30。
- ：穷 ≈ 人均 <$1800；中 ≈ 1800~3499；富 ≈ ≥3500；引擎连败胜利时清零
（非 CS2 胜后降一档）；"中"靠购买=半起|强起拆开。购买计划 5 档定义（eco/半起/强起/满起/
反eco 各一段）。决策序 4 条：能满起 → 满起（敌穷叠加反 eco）；不能但败后能满 → eco；
败后仍不能满、或赛点/半场末、或敌经济脆 → 强起；能花一点仍保证下回合满 → 半起。
save 仅"回合开始早段 + 经济崩 + 不占人数优势 + 未占包点 + 未下包"；已占包点/已下包/
人多打人少时不许 save；保枪是就地收缩不交火；输掉满起后默认 eco→满起；强起失败后
下一回合强制 eco、禁连强；炸弹已下则经济让位。
- 5 条：主攻侧 18 秒内不换侧；C4 携带者跟主攻走，进点后无敌人先下包再追；
C4 掉落 T 派最近的人拾取后再进攻，CT 已知位置则收缩到该区域卡点；已下包 target 必须等于
包点侧；下包后 T 不全员贴包（1 人看拆包角度、1 人控回防路线、1 人延迟 peek）；残局少打多
不硬拼，守包等 CT 上手拆包再抓。
- 4 条：只有另一侧明显没人时才转，未知 ≠ 空，没情报不转；剩余 30 秒以上，
不少打多时转；转点后 8 秒内不来回改主意；执行先中路收拢再推；fake 必须有佯攻侧接触/噪音，
看到回防再转，否则改打佯攻侧。
- 4 条：先集结再进，两人同角度互换，utility 先清点；拆包需要 5 秒（⚠️ 已过期，
实际有钳 4s/无钳 10s）；先清包点近处，看得见 T 或刚挨打先反击，不要贴包；最后 6 秒才强拆
（⚠️ 与代码 11s/5s 阈值不一致）；已下包用 retake_<包点侧>，target 同侧；2v4 可打人数差/武器；
回防路线优先模板给出的两路，不单人滴入。
- 8 条：跑动 130 单位内暴露；静步不出声、移速降到 25；静步时机（进点/转角前、
残局听信息、已占点后收缩、下包/拆包最后一段、避免长廊跑步）；跑动时机（开局抢点、时间不足、
已知敌方位置需补枪、被闪后撤离）；听到脚步的 BOT 只会自己去查、不共享给队友（advisory 性质）。
- 3 条：最多 5 个、slot 0~4 不重复、role 五种、area 必须用当前地图真实区域名；
人数少 1 时 tempo 偏 slow，default/stack 等枪；TimeLow 时 T 冲有接触侧、CT 收位守点；
有接触先报区域再决策；道具只用于挡视线/隔离回防/拖延进点，不写具体投掷线。

**每图 14 张卡（42 张，结构相同、区域不同）：**

| 卡 | intent | target | tempo | ttl | 备注 |
|---|---|---|---|---|---|
| T_default | default | 空 | normal | 18 | 控中换信息 |
| T_rush_a | rush_a | A | fast | 12 | 满起 +（已控 \| 敌A≤1 \| 敌B≥3） |
| T_rush_b | rush_b | B | fast | 12 | （已控 \| 敌B≤1 \| 敌A≥3） |
| T_split_a | split_a | A | fast | 15 | Mid 已控 +（敌A≤2 \| 敌B≥3） |
| T_split_b | split_b | B | fast | 15 | Mid 已控 +（敌B≤2 \| 敌A≥3） |
| T_fake | fake | 另一侧 | normal | 18 | 上回合主攻本侧 +（敌本侧≥2 \| 敌另侧≤1） |
| T_eco_save | save | 空 | slow | 20 | 购买=eco + 敌经济富 |
| T_postplant_a/b | default | A/B | slow | 20 | 已下包 +（敌≥1 \| 时间<40s） |
| CT_default | default | 空 | normal | 18 | 回合开始 & 未下包 |
| CT_stack_a/b | stack_a/b | A/B | normal | 16 | 上回合主攻该侧 \| 敌该侧≥2 \| 敌经济穷 |
| CT_retake_a/b | retake_a/b | A/B | fast | 12 | 已下包@该侧 |

卡片字段：id / when / intent / target / tempo / ttl / slots（5 条，格式
`序号 角色 区域 | look=区域 | hold=… | util=…`）/ fallback（恒为 default）。
每图还有：区域表（A 侧/B 侧/中路/出生区域名清单）、`# [advisory]` 行
（ancient 6、dust_2 8、mirage 7，共 21 条）、few-shot JSON 示例 2 条/图
（T default、CT retake_a，共 6 条；**没有 Split/Fake/Save 的示例**）。

`when` 条件统计（42 条）：用"上回合主攻"9 条、用"敌经济"6 条、用"已控"12 条、
用敌分布计数（敌A≤1/敌B≥3 等）约 20 条、用"购买=eco"1 条、用"时间<40s"2 条、
用"已下包"4 条。⚠️ 见 §6：其中几类条件规则层现在没有对应的信号。

### 2.3 情报 TacticalIntel（按包点聚合的敌方威胁度）

`TacticalIntel.hpp/.cpp`。`SiteHeat` 字段：area（包点区域名）、cx/cy（包点中心）、
rects（包点矩形，可多块）、heat（衰减后威胁度）、lastSeen（最后一次观察时间）、
lastCount（最近一次累计敌人数，粗估）。常量：衰减 tau 14s、上限 6、一次观察 +1。
`SiteIndexAt`：落在包点矩形内归该点，否则归 220px 内最近的包点。
观察来源：ReportContact（接火上报，同一敌人 >2s 后重复上报才再次计数）与击杀
（`CS2DGameCombat.cpp:1047`，据 11-10 快照审计；喂入代码不在本次拉取的教练文件内）。
查询：`Heat(i)` / `HeatOfArea(area)` / `HeatOfSide(world, "A"/"B")` /
`ObservationCount()`（总观察次数，判断"有没有任何情报"）。

**现在谁在用**：`CanRotateNow` / `CoachTryRotate`（转点证据门）、`SanitizeCoachPlan`
（CT stack 无情报检查）、`BuildStateDigest`（敌分布）。**`RuleCoachCore` 零次使用**（⛔）。

### 2.4 记忆 RoundMemo + 状态 CoachState

**RoundMemo**（`CoachTypes.hpp:95-113`，每队最近 10 回合，`CoachRecordRound`
`Coach.cpp:1287-1315` 写入）：roundNo、plan（意图）、target、won、scoreT、scoreCT、
aliveEnd、bombPlanted、plantSite、firstContactArea、deaths[]（每条：team、area、x、y、
atSec）。**注意：记忆只记己方计划与结果，不记敌方主攻侧**（敌方习惯无字段，见 §5-9）。

**CoachState**（`CS2DGame.hpp:1237-1290`，每队一份）全部字段：
plan（当前生效计划）、buyPlan（本回合购买档）、memory（最近 10 回合）、pending
（待处理事件）、mergeWindow（合并窗口）、callsThisRound（本回合调用次数，上限 8）、
gen（代际号）、reason（HUD 显示）、appliedIntent（上次真正下发的意图）、
changesThisRound（本回合意图变更次数，⛔ 全仓只有一处读、零处写）、sameSideStreak
（连续打同一侧的次数，⛔ 全仓除声明外零读写）、lastIntentChangeSec（上次意图变更时刻）、
deaths（本回合阵亡列表）、firstContactArea、timeLowFired、matchPointFired、
notes（最近 5 条复盘结论，滚动）、lastOrder（上一条指令摘要）、rotateFrom/rotateTo/
rotateAt（转点状态）、lastAttackSide（最近一次进攻目标侧，`CoachThink:456` 与
`CoachTryRotate:1554` 写入）、planExpiresAt、llmPending、llmAnswered、llmGen、llmAt、
llmWallAt、llmLatencyWall、llmEventType、llmRoundNo、llmAliveT/llmAliveCT（发请求时存活数）、
ruleSnapshot（发请求那一刻的规则计划）、llmSnapshot（LLM 解析后未整形原意）、
hasRuleSnapshot、llmBombPlanted、lastThinkAt、stickyLlm（LLM 计划置顶，规则不得覆盖）。

回合开始经济快照（`CS2DGame.hpp:1288-1290`）：`roundStartAvgMoneyT_` /
`roundStartAvgMoneyCT_` / `roundEconomySnapshotValid_`（采购前快照，避免买完枪误判穷局）。

### 2.5 摘要 BuildStateDigest（只喂 LLM 的 14 组字段）

`CS2DGameCoachAI.cpp:553-748`。每一组都是规则教练现在不用的数据（🔶）：

1. 优先级行："炸弹>经济>减员>笔记"。
2. 回合/比分/时间/存活：`R<回合> T/CT <比分> <分:秒> 我<存活> 敌<存活>`。
3. 炸弹状态：掉落@区域（CT 已知位置时标注）；已下@区域 + 倒计时 Ns + "拆包需5s"
（⚠️ 数字已过期）；未下（玩家携带）/ 未下。
4. 经济：己方 穷/中/富（<1800 / <3500 / ≥3500）+ 购买=档位 + 敌经济 穷/中/富（差 ±$）。
5. 我方站位分布：按区域计数（最多显示 4 个区）。
6. 道具明细：总数 + 烟/闪/火/雷分项（决定能不能"道具开点"）。
7. 敌枪构成：狙/步枪/冲锋/霰弹/手枪人数（按每人最优武器分类，槽位 0–1）。
8. 近局习惯：上一回合主攻侧 + 首触区域 + 胜/负（只取最近 1 条记忆）。
9. 敌分布：每包点 `区域名=heat`；无观察时标注"（暂无情报）"。
10. 敌联系人：每个接触点 区域 + 年龄（秒）。
11. 上条：lastOrder。
12. 参考笔记：最新 1 条（明确标注优先级低于炸弹/经济）。
13. 事件：类型 + 相关区域（12 种全列）。
14. 合法闭集 + 近 3 回合：允许的 intent 列表、target 列表、area_n 数量；
最近 3 回合结构化行（回合号 意图→目标 胜/负 首触@区 已下包）。

### 2.6 经济数据

输入：`GetRoundStartEconomy`（采购前快照均值，双方各一）、`RoundNumber`、
`GetRoundElapsed`。`ShouldSaveNow`（`Coach.cpp:1451-1474`）条件：未下包、回合 >1、
已进行 ≤45s、非人数优势、T 未占包点、均值 <1800（`LowEconomyAverage`）。
阈值：3500 / 2600 / 1800（买档）、2200（反 eco 敌方上限）。
📦 **连败奖金**：`GetLossStreak(Team)` 存在（`CS2DGame.hpp:1126`，`economy_.LossStreak`），
`_common.txt` 明确写"胜利时 LossStreak 清零"——但教练层（规则 + 摘要）**全都不读**（⛔）。

### 2.7 调参 Tuning（教练相关旋钮）

`Tuning.hpp:284-295`：RoleRefreshInterval 2.0s（角色重算间隔）、RoleSwitchCooldown 8.0s
（同一 BOT 两次换角色最短间隔）、RoleTransitionSec 3.0s（速度倍率过渡）、IntentMinHold
10.0s（意图最短保持）、IntentMaxChanges 2（每回合最多变更次数，⛔ 预算未生效）、
IntentChangeCooldown 6.0s（两次变更最短间隔）、IntentRenewSec 8.0s（到期续期时长）、
SilentSeconds 30.0s（无接触多久算"沉默期"）、SameSideStreakLimit 3（连续同一侧打几次
才强制换边，⛔ 旋钮已定义、没有任何代码读它）。
难度感知 4 档：`BotFovRadiusMul[4]` / `BotFovAngleMul[4]` / `BotReactionSecs[4]` /
`AimSettleSecs[4]`（ini `[perception.N]` 可覆盖）。

### 2.8 调度与守卫数据

- 事件合并窗口 0.4s；无事件时定期发 Heartbeat（`CoachUpdate:287-301`）。
- KEEP：soft 事件且计划有效未过期 → 保留计划并退还调用次数（`CoachThink:339` 起）。
- sticky：LLM 计划在有效期内置顶，规则不得覆盖（除非下包/关键事件）。
- T 执行锁：计划创建后 18s 内不换意图（MatchPoint 除外，`CoachThink`）。
- 变更预算：意图至少保持 IntentMinHold 10s、两次变更间隔 ≥6s、每回合 ≤2 次
（前两条生效，第三条 ⛔ 未生效）。
- 计划 TTL：创建时 +20s；到期续期 +8s（IntentRenewSec）；沉默 30s 重评估。
- 调用预算：每队每回合上限 8 次，KEEP 路径退还。
- LLM 超时：回合开始 7s、其他 4s；TTL 地板：回合开始 18s、下包/拆包 12s、其他 10s。
- 转点评估：每 0.75s 检查一次（`CoachUpdate:171`）。
- 评估记录：`TraceCoachDecision` / `TraceCoachAdvisory`（规则/LLM/执行三并排日志）、
`counters_.intentChanges`（意图真变时 +1）、`counters_.llmAdvisory`。

### 2.9 小队执行数据

`CS2DGameSquad.cpp`。`SquadOrder`（`CS2DGame.hpp:61-72`）：role（BotRole 3 值）、
focus（集火目标：-2 无 / -1 玩家 / ≥0 BOT 索引）、site（负责包点索引）、hasHold /
holdX / holdY（驻守点）、canTrade（补枪）、canRotate（自行换点/游走，lurk 用）。
`AssignSquadRoles`（:477）：按"离敌方质心远近"排角色（前 N 个 Entry、后 N 个 Anchor，
数量按队伍人数定）。`RefreshRolesForTeam`（:428）：保证至少 1 个 Entry + 1 个 Anchor，
缺了就重指（最近/最远者）。`UpdateSquadFocus`（:604）：集火目标选择。
PIX-31 身体可见性：`QueryBodyVisibility`（按难度感知锥判定谁看得见谁），供小队与教练用。

### 2.10 游戏层可接数据（📦 教练层还没用，但游戏里已有）

炸弹：`IsBombDropped` / 掉落坐标 / `HasBombIntel`（CT 是否已知掉落位置）/
`IsBombPlanted` / `GetBombTimer` / `GetBombX/Y` / `PlayerHasC4` / `bombCarrier_`。
BOT：每人 weapons[2]（主/副武器 id）、`WeaponDB::Get` 的 cls（Sniper/Rifle/SMG/
Shotgun/其他）、grenades 列表 + grenadeCount、alive、team、x/y。
联系人：`GetContactsOfTeam(team)`（每个接触点 x/y/age）。
比分/回合/时间：`GetScoreT/CT`、`GetRoundNumber`、`GetRoundElapsed`、
`round_.PhaseRemaining`、存活数 `AliveCount`。经济：`GetLossStreak`（⛔ 未接）。
拆包钳：`hasDefuseKit` 机制已落地（PIX-33，4s/10s 拆包）——回防逻辑没读它（⛔）。

---

## §3 决策→数据关系矩阵

行 = 决策点，列 = 它实际读的数据。空白 = 该数据存在但这个决策点不读。

| 决策点 | 记忆（己方胜负） | 情报 heat | 敌枪构成 | 道具明细 | 经济快照 | 比分/赛点 | 连败 | 敌方习惯 | 区域/锚点 | 其他 |
|---|---|---|---|---|---|---|---|---|---|---|
| RoundStart T | ✅ aTries/aWins/bTries/bWins | — | — | — | ✅ ShouldSaveNow | — | — | — | ✅ AreaHint | — |
| RoundStart CT | —（固定 2-2-1） | — | — | — | ✅ ShouldSaveNow | — | — | — | ✅ AreaHint | — |
| 下包后（状态分支） | — | — | — | — | — | — | — | — | ✅ 包点区域 | ✅ 炸弹倒计时、双方存活数 |
| BombPlanted 事件 | — | — | — | — | — | — | — | — | ✅ 包点侧 | — |
| DefuseStarted 事件 | — | — | — | — | — | — | — | — | ✅ 包点侧 | — |
| EnemyDown / AllyDown | —（固定模板） | — | — | — | — | — | — | — | ✅ AreaHint | ⚠️ 常被 soft-KEEP 拦截 |
| EconomyLow 事件 | — | — | — | — | ✅ ShouldSaveNow | — | — | — | — | — |
| TimeLow | ✅ plan.target→lastAttackSide | — | — | — | — | — | — | — | ✅ AreaHint | — |
| C4 掉落（CoachThink） | — | — | — | — | — | — | — | — | ✅ 掉落区域 | ✅ bombIntel_（CT） |
| 转点 CanRotateNow | — | ✅ 两侧 heat 差 ≥1.2 | — | — | — | — | — | — | — | ✅ 观察数≥2、计划龄≥12s、剩余≥30s、冷却 8s、携带者 280 内 |
| 消毒 Sanitize（仅 LLM） | — | ✅ CT stack 差 <0.4 | — | — | — | — | — | ✅ lastAttackSide | ✅ 槽位区域→侧 | — |
| 买档 DecideBuyPlan | — | — | — | — | ✅ 双方均值+回合号 | — | — | — | — | ✅ ShouldSaveNow |
| 下发 CoachApplyPlan | — | — | — | — | — | — | — | — | ✅ 锚点（仅 hold≠Default） | ✅ C4 携带者强制跟主攻侧 |
| LLM 摘要（对照） | ✅ 近 3 回合+近局 | ✅ 全量 heat | ✅ 5 类计数 | ✅ 烟/闪/火/雷 | ✅ 双方+差值 | ✅ 比分 | — | ✅ 近局主攻/首触 | ✅ 区域计数 | ✅ 联系人年龄、lastOrder、笔记 |

读法：规则教练的列大面积空白——摘要里 14 组字段，规则只用了其中 2 组半
（记忆胜负、经济快照、包点区域）。这就是"把只读给 LLM 的交给规则教练"的清单（§4）。

---

## §4 只给 LLM 的数据 → 交给规则教练的清单

每一项：数据已存在（位置）→ 规则教练的接线点。全部保留、不删除。

| # | 数据（现在只给 LLM） | 位置 | 交给规则教练的接线点 |
|---|---|---|---|
| 1 | 我方站位分布（按区域计数） | 摘要 §2.5-5 | `RuleCoachCore` 回合开始：判断"中路是否已控""两侧是否有人"，替代固定分兵 |
| 2 | 道具明细（烟/闪/火/雷分项） | 摘要 §2.5-6 | Split/Rush 发出条件：烟 ≥1 才能道具开点；无道具 → Default |
| 3 | 敌枪构成（狙/步枪/冲锋/霰弹/手枪） | 摘要 §2.5-7 | 打分：敌狙多 → 避长廊/慢打；敌冲锋多 → 保持距离/架点 |
| 4 | 敌分布 heat（全量） | 摘要 §2.5-9、`intel_` | `RuleCoachCore` 回合开始读 `HeatOfSide`：热侧避开/冷侧主攻（现在只有转点读） |
| 5 | 敌联系人（区域 + 年龄） | 摘要 §2.5-10 | TimeLow / EnemySpotted：按最新接触区域定主攻侧（现在只读 plan.target） |
| 6 | 近局习惯（上一回合主攻/首触/胜负） | 摘要 §2.5-8、记忆 | CT 回合开始读敌方习惯（需 §5-9 的敌方记忆字段）；T 的 avoidA/avoidB 扩展为"意图+目标对" |
| 7 | 近 3 回合结构化行 | 摘要 §2.5-14 | 替代现在的"只看胜负计数"：连续同 intent+同 target 失败 → 换点（手册总规则第 4 条） |
| 8 | 上条指令 lastOrder | 摘要 §2.5-11、CoachState | 变更预算判断：新意图与上条是否同向，避免来回横跳 |
| 9 | 参考笔记（最新 1 条） | 摘要 §2.5-12、CoachState.notes | 复盘结论进规则：笔记是 `CoachDebrief` 写的结构化结论，规则可按关键词（如"连续输 A"）触发换侧 |
| 10 | 经济差值（双方均值差） | 摘要 §2.5-4 | `DecideBuyPlan`：我穷敌更穷 → 强起打经济（手册有、规则没有） |
| 11 | 合法闭集（allow.intents/targets） | 摘要 §2.5-14、`BuildAllowSet` | 规则也按同一份 allow 集发意图：已下包禁换侧、穷局禁 rush/split/fake——现在这些约束只卡 LLM，不卡规则 |
| 12 | 战术手册 _common 全部规则 | §2.2 | 逐条落到 `RuleCoachCore` / `DecideBuyPlan` / `Sanitize`（见 §5-11；手册里"赛点强起""强起失败禁连强""输满起后 eco→满起"三条规则层完全没有） |
| 13 | 42 张战术卡（when 条件 + 槽位） | §2.2 | 卡片 when 条件翻译成规则分支：rush/split/fake/stack 的触发条件现在只在卡片里，不在代码里 |
| 14 | advisory 21 条 | §2.2 | 分类：可执行的（如"回防两路同时进"）→ 写进回防模板；纯提示的（如"听到脚步只自己查"）→ 标注 advisory、不承诺 |
| 15 | few-shot 2 条/图 | §2.2 | 补 Split/Fake/Save 的示例（现在只有 T default、CT retake_a）；示例本身也是规则模板的对照 |
| 16 | 槽位 look/hold/util | §1.7、§2.1 | 规则手写槽位补 look/hold/util 字段（现在恒为默认）；或规则直接用表槽位（§5-1 修好后） |
| 17 | 锚点 anchor | §2.1 | `CoachApplyPlan` 放开"仅 hold≠Default 才用锚点"的限制，让规则模板也能用锚点 |

---

## §5 未接线项总表（全部保留，逐条给接线点）

| # | 项 | 现状 | 接线点 | 量级 |
|---|---|---|---|---|
| 1 | `@Target` 模板 14 段/图 | Load 只读 plain 段 | `TacticsTable::Load` 里 `GetAll()` 再扫 `slots.` 前缀登记 | ~20 行 |
| 2 | `changesThisRound` 预算 | 全仓一处读、零处写；`CoachBeginRound` 也不清零 | `CoachApplyPlan` 意图真变时 +1（已有 `counters_.intentChanges` 的判断点）；`CoachBeginRound` 清零 | ~5 行 |
| 3 | `hasBranch` 时间差打法 | 永不设 true；`CoachUpdate:205-244` 触发逻辑已就绪 | `RuleCoachCore` RoundStart 的 Default 计划设 `hasBranch`（如 20s 后按情报转 Rush 另一侧） | ~15 行 |
| 4 | `sameSideStreak` + `SameSideStreakLimit=3` | 字段 + 旋钮都已定义，零读写 | `CoachThink`/`CoachRecordRound` 按实际主攻侧更新（同侧 +1、换侧清零）；`Sanitize` 用它替代/补充 `lastAttackSide` 做连续同侧判断；`RuleCoachCore` 回合开始读它（≥2 强制换侧） | ~20 行 |
| 5 | `SanitizeCoachPlan` 复用于规则 | 只在 `CoachDrainLlm:1190` 调用 | `CoachThink` 里 `RuleCoach` 返回后也调一次（跳过已下包分支，函数本身已处理） | ~3 行 |
| 6 | Rotate 进表 + 进 schema | `kIntents` 无 Rotate；schema 无 rotate_a/b；`IntentLabel`/`ParseIntent` 已支持 | `kIntents` 加 RotateA/RotateB；`kCoachSchema` 加 rotate_a/rotate_b | ~4 行 |
| 7 | Lurk / Awper 真角色 | 槽位模板大量使用，下发坍缩成 Entry/Support | `BotRole` 加 Lurk/Awper，配移速倍率与行为（Lurk 绕后/断后、Awper 架狙）；`CoachApplyPlan` 映射直通 | 决策树改动，量大 |
| 8 | `LossStreak` 连败奖金 | `GetLossStreak` 已存在，教练层全不读 | `DecideBuyPlan`：连败 ≥2 且下回合奖金能满起 → Eco 存钱；摘要补上连败数字 | ~10 行 |
| 9 | 敌方习惯记忆 | 记忆只记己方；CT stack 卡的"上回合主攻"无数据源 | `RoundMemo` 加敌方主攻侧/首触（`CoachRecordRound` 从对手记忆或本回合接触推导）；或 digest 的"近局"改记敌方 | 字段 + 写入，~15 行 |
| 10 | MID 控中意图 | target 允许 MID，无规则分支 | `RuleCoachCore` Default 变体：2 人控中 + 两侧各 1 人架点 + 1 人自由（开局无情报时用） | ~20 行 |
| 11 | 赛点分支 | MatchPoint 是 criticalEv，但无决策分支 | `RuleCoachCore`：赛点 → Force 买档 + tempo Fast（手册决策序第 3 条） | ~10 行 |
| 12 | 钳子感知回防 | mustRush 11s 按 10s 拆包算，不认 4s 钳子；钳子持有者无优先槽位 | 回防：有钳持有者 `mustRush` 阈值 11s→5s；钳子持有者优先进点拆，其余掩护 | ~15 行 |
| 13 | 按能力分槽 | `CoachApplyPlan` 按 BOT 下标顺序分槽 | 分槽前排序：拿 AWP 的分 Awper、钱最少的分 Save 位、其余按枪法/移速分 Entry/Support | ~25 行 |
| 14 | 跨回合经济状态机 | `DecideBuyPlan` 纯快照 | `CoachState` 加 `lastBuyPlan` + `lossStreak`：上回合 Force 且输 → 强制 Eco；赛点 → Force；输满起 → eco→满起 | ~30 行 |
| 15 | 回防分工模板 | 下包后 CT 全员 Entry@包点 | 钳子持有者进点拆 + 2 人掩护 + 2 人控回防路线；T 守包补分工（看拆包角度/控回防路线/延迟 peek，手册已写） | 模板 + 代码，~30 行 |
| 16 | 敌枪/道具/联系人进打分 | 摘要有，规则不用 | `RuleCoachCore` 回合开始改意图打分制，信号全取自摘要已有字段（见审计提案 §4-7） | 打分函数，~60 行 |
| 17 | 消毒的 CT Stack 检查 | 只走 LLM 路 | 随 §5-5 复用；规则 CT 回合开始发 Stack 时同样过"无情报不赌点"检查 | 随 5 |
| 18 | `templateDefault` 覆盖链 | 规则模板 `templateDefault=true` 允许表覆盖，但表只有 4 个 plain 段 | 随 §5-1：@ 段加载后，Split/Fake/Save/Stack/Retake 的表覆盖自动生效 | 随 1 |

---

## §6 数据质量检查清单（供 Sun 逐项检验）

打勾 = 数据没问题；打叉 = 要改。每一项都注明位置，改不改由你定。

**过期数字**
- [ ] 拆包 5 秒：三处——摘要 `CoachAI.cpp`（"拆包需5s"）、`_common.txt` 回防节、
  retake 卡 advisory（"拆包 5s：两路同时进"）。实际有钳 4s / 无钳 10s。
- [ ] "最后 6 秒才强拆"（_common）与代码阈值不一致：规则 `mustRush` 是 11s（无钳）、
  有钳应为 5s。手册数字与代码数字要对齐。

**硬编码与不一致**
- [ ] CT 的 C4 掉落 intent 硬编码 `StackA`（`CoachThink:418`）：不管掉落在哪一侧都发
  StackA，target 却是实际区域名。应按实际侧发 StackA/StackB。
- [ ] reason 长度两处标准不一致：schema 限 60 字符（`CoachAI.cpp:28`），手册限 20 汉字
  （_common 总规则第 6 条）。
- [ ] ttl 三处语义：卡片 ttl 12–20s、schema 3–30、TTL 地板 18/12/10——是否一致？
- [ ] 经济档边界：摘要用 穷<1800/中<3500/富；`DecideBuyPlan` 用 1800/2600/3500——
  "中"档内部 Half/Force 的 2600 边界只在代码里，手册里没有。

**when 条件可判定性（卡片的条件，规则层有没有对应的信号）**
- [ ] "已控"（12/42 条 when 用）：规则层**没有区域控制度量**——"Ramp 已控""Mid 已控"
  无法判定。这是数据缺口：要么加信号（我方在该区人数/存活），要么改条件。
- [ ] "敌A≤1 / 敌B≥3"（约 20 条）：规则层只有 heat（衰减威胁度），没有**分侧敌人数**。
  heat ≠ 人数。转点用的也是 heat 差（≥1.2），不是人数。
- [ ] "上回合主攻X"（9 条）：**同一短语两种语义**。T 的 fake 卡指己方上回合主攻
  （记忆里有己方 target，可判定）；CT 的 stack 卡指**敌方**上回合主攻（记忆里没有，
  见 §5-9）。CT 的 stack_a/b 的 when 条件现在无法判定。
- [ ] "敌经济穷/富"（6 条）：摘要有敌经济标签，规则层 `DecideBuyPlan` 有敌均值——
  可判定，但 `RuleCoachCore` 不读。
- [ ] "购买=eco"（1 条）：`DecideBuyPlan` 的结果在 `CoachState.buyPlan` 里——可判定，
  但 `RuleCoachCore` 不读 buyPlan（只调 `ShouldSaveNow`）。

**覆盖率**
- [ ] util 覆盖：14 条/图，全在 rush/retake 卡；default/split 卡基本没有 util。
  Split 卡要不要道具开点？
- [ ] hold 覆盖：50/50/49 条；look= 70 条/图。检查：有没有卡片引用了
  look/anchor 覆盖之外的区域？（区域总数 29/35/32，look/anchor 全覆盖。）
- [ ] few-shot 覆盖：只有 T default、CT retake_a 两种；Split/Fake/Save/Stack
  没有示例。LLM 学格式时看不到这些意图的槽位写法。
- [ ] advisory 21 条：逐条分类——可执行的规则 vs 纯提示。纯提示的（如"听到脚步只自己查"）
  不应被当作规则执行；可执行的（如"回防两路同时进"）应进模板。
- [ ] `[areas]` 只有 6 个关键词映射：卡片里的区域名是真实 TMJ 名，直接可用；
  但 `AreaHint("Long"/"Short"/"Tunnel")` 在 mirage/ancient 上回退到包点——
  规则代码的"侧翼区域"在非 dust_2 图上是退化的（只有包点，没有长廊）。

**决策多样性（与 LoRA 数据集直接相关）**
- [ ] 规则能发的意图：Default/Rush/Save/Stack/Retake/Rotate（6 类）；发不出的：
  Split/Fake/MID（§1.1）。数据集里这三类样本数为 0，除非 7B 补。
- [ ] CT 回合开始：固定 2-2-1，零记忆、零情报。CT 的决策多样性为 1（只有 Default）。
- [ ] 角色多样性：教练 5 角色 → 执行 3 角色（§1.5）。
- [ ] 槽位多样性：规则手写槽位只有 role/area/canTrade/canRotate（§1.7），
  look/hold/util 三个维度是空的。

**待核实（2026-11-19 已核实，见下；核实依据：`CS2DGameBots.cpp` 2026-11-17 01:53 快照、
`CS2DGameCombat.cpp` 2026-11-17 14:20 快照、`CS2DGameSquad.cpp` / `CS2DGameCoach.cpp` /
`CoachTypes.hpp` / `Tuning.hpp` 2026-11-18 live 副本。11-17 14:20 至 11-18 晚间的工作
只有文档（minimind 准备、规则教练审计与本清单），Bots/Combat 大概率未动，
但正式引用前仍建议在 Sun 电脑上对 live 树跑一次 grep 复核——见末尾复核命令。）**
- [x] `CoachRole` 注释"影响移速倍率"——**属实，但经 `BotRole` 坍缩**。决策树
  `CS2DGameBots.cpp` 三处按角色定移速：:908-910（planHold 路径 `planMul`）、
  :1015-1017（寻路 `roleMul`）、:1401（交火机动 `cap`）；倍率 `Tuning.hpp:95-97`：
  Entry 1.00 / Support 0.80 / Anchor 0.55。映射在 `CoachApplyPlan` :1141-1146：
  Lurk→Entry（拿 1.00 全速）、Awper→Support（拿 0.80）。所以注释对 Entry/Support/Anchor
  直接成立，对 Lurk/Awper 是坍缩后间接成立——5 角色并无 5 档倍率。
  - 补枪资格属实：`canTrade` 在 `Bots.cpp:586`（trade push，Anchor 除外）、`:791`
    （assist）使用，`CoachApplyPlan` 下发 `so.canTrade`。
  - "远追许可"查无实据：全仓只有 `CoachTypes.hpp:34` 注释本身提到"远追"，
    无 chase/pursue/far 机制代码。`canRotate`（自行换点/游走）可能是它指的东西，
    但不能等同——注释措辞应改或补机制。
  - ⚠️ 新发现：`RoleTransitionSec` 3.0s（`Tuning.hpp:287`，"速度倍率过渡时间"）
    在本地全部文件里**只有定义、零处引用**——角色一换速度立即跳变，没有 3 秒过渡。
    这是第 19 个未接线项：要么实现过渡（按角色切换时刻插值倍率），要么删旋钮
    （按 Sun 的不删除原则：实现它）。
- [x] `AssignSquadRoles` 按"离敌方质心远近"排角色，是否会覆盖教练的槽位角色？
  **已核实：不会常规覆盖，教练计划优先。** `AssignSquadRoles(dynamic=true)` 全仓
  无调用者；非动态调用只在对局开始（`CS2DGame.cpp:185`）与每回合开始
  （`CS2DGameRound.cpp:64` `BeginNewRound`）。回合内动态角色只有
  `RefreshRolesForTeam`（`Bots.cpp:444-453`，每 2s 检查一次，但仅存活人数变化时才触发），
  且只补断档：某队缺 Entry 或缺 Anchor 时才把最靠前/最靠后的一个 Support 改过去，
  带 8s 冷却，其余角色不动。`CoachApplyPlan` 每次下发都直接写 `squad_[i].role`
  （:1141-1146），`SetBotGoal` 用 Coach `force=true`，注释 :1213 明确写
  "而不是被 AssignSquadRoles 的轮转岗位拉回另一侧"，并同步 `squad_[i].site`。
  优先级：**教练计划 > 补断档 > 回合开始轮转**。唯一窄冲突窗口：教练计划本身
  缺 Entry 或缺 Anchor 时（如全员 Anchor 的 Save/守包计划），`RefreshRolesForTeam`
  可能把一个 Support 提成 Entry/Anchor——与教练意图相悖的单点覆盖，P1 接线时留意。
- [x] 情报喂入：**属实，行号已漂移。** `ReportContact`（`CS2DGameSquad.cpp:147-163`，
  2026-11-18 live 副本）：同一敌人首次上报 → `OnEnemySeen`；重复上报仅当
  `c.age > 2.0f` 才再次喂入。">2s"过滤点在 `ReportContact`，不在
  `TacticalIntel::OnEnemySeen` 本身（后者每调一次 heat+1、lastCount+1、
  observations_+1，无去重）。击杀喂入属实：`CS2DGameCombat.cpp:1017`
  （2026-11-17 14:20 快照；11-10 快照的 1047 已漂移，part4 快照为 984），
  `if (victim.team != player_.team) intel_.OnEnemySeen(victim.x, victim.y, clockSec_)`，
  在击杀处理内，注释"敌方阵亡位置也是一种情报（会衰减）"。注意条件是
  "受害者不是玩家所在队"——只喂敌方（相对玩家）阵亡位置，玩家队友 BOT 阵亡不喂，
  且击杀喂入无 2s 过滤、无距离检查。补充：`UpdateTeamContacts`（:185-230）每帧让
  每个活着的 BOT 上报所有 `CanSeeActor` 的敌人，玩家视野也共享给队友；同一敌人在
  同一队的联系人表里按 id 去重（多 BOT 同时看到只喂一次）；联系人记忆
  `CONTACT_MEMORY` 6.0s（`CS2DGame.hpp:1151`）。

> Live 复核命令（在 Sun 电脑仓库根目录跑一次即可确认快照未漂移）：
> `Select-String -Pattern "SpeedMulEntry|RoleTransitionSec" PixelEngine\CS2D\CS2DGameBots.cpp,
> PixelEngine\CS2D\Tuning.hpp`；
> `Select-String -Pattern "AssignSquadRoles\(" PixelEngine\CS2D\*.cpp`；
> `Select-String -Pattern "OnEnemySeen" PixelEngine\CS2D\CS2DGameCombat.cpp,
> PixelEngine\CS2D\CS2DGameSquad.cpp`。

---

## 附录 A：数量统计总表

| 类别 | 数量 |
|---|---|
| 意图 PlanIntent | 13（schema 11、表 kIntents 11） |
| 目标 target | 4（A/B/MID/空） |
| 节奏 Tempo | 3（激进系数 1.25/1.00/0.80） |
| 买档 BuyPlan | 5 |
| 角色 CoachRole / BotRole | 5 / 3 |
| 站姿 HoldStyle | 4 |
| 槽位字段 SlotOrder | 9 |
| 事件 CoachEventType | 12（critical 7、soft 5） |
| 计划字段 TeamPlan | 17 |
| 战术表 ini（每图） | areas 6 键、slots 18 段（4 plain + 14 @）、look 58–70 段、anchor 58–70 段、perception 5 段 |
| 战术卡 | 42 张（14/图：T 9 + CT 5） |
| _common 规则 | 约 30 条（7 节） |
| advisory | 21 条（不含 _common 的 0 条） |
| few-shot | 6 条（2/图） |
| 摘要字段组 | 14 组 |
| 记忆 RoundMemo 字段 | 11 + 死亡子结构 5 |
| CoachState 字段 | 约 38 |
| 情报 SiteHeat 字段 | 6 + 常量 3 |
| 调参旋钮（教练） | 9 + 难度感知 4 档 × 4 项 |
| 未接线项 | 18（§5） |

## 附录 B：文件索引（live 行号）

- `PixelEngine/CS2D/CS2DGameCoach.cpp`：`CoachOnEvent` :135、`CoachUpdate` :165（分支触发 :205-244、
  转点评估 :171、Heartbeat :287-301）、`CoachThink` :319（criticalEv :322-329、softEv :332-337、
  soft-KEEP :339、T 18s 锁 :372、变更预算 :386-395、C4 掉落 :406-424、`lastAttackSide` 写入 :456）、
  `RuleCoachCore` :512-756、`SanitizeCoachPlan` :973-1096、`CoachApplyPlan` :1097-1286
  （角色映射 :1142-1146、C4 携带者 :1227-1232）、`CoachRecordRound` :1287-1315、
  `CoachBeginRound` :1337-1372、`GetRoundStartEconomy` :1374、`DecideBuyPlan` :1409-1430、
  `ShouldSaveNow` :1451-1474、`CanRotateNow` :1475-1509、`CoachTryRotate` :1510-1566。
- `PixelEngine/CS2D/CS2DGameCoachAI.cpp`：`kCoachSchema` :55-75（intent enum :24、无 rotate）、
  `BuildAllowSet` :155-190、`BuildStateDigest` :553-748、`ParseCoachPlan` :749-903、
  `CoachTryLlm` :913-992（`coachLlmRoundStartOnly_` :942）、`CoachDrainLlm` :1027-1278
  （`SanitizeCoachPlan` 调用 :1190、TTL 地板 :1194-1197、超时 :1270、advisory 门 :1211-1222）。
- `PixelEngine/CS2D/Coach/CoachTypes.hpp`：`PlanIntent` :16、`Tempo` :29、`BuyPlan` :32、
  `CoachRole` :35、`HoldStyle` :38、`SlotOrder` :46、`TeamPlan` :63（`hasBranch` :83）、
  `TempoAggression` :91、`RoundMemo` :95、`CoachEventType` :116。
- `PixelEngine/CS2D/Coach/TacticsTable.hpp`：`Perception` :34、`Load` :60-140、
  `kIntents` :96（11 个，无 Rotate）、`ParseSlot`（Role|Area 语法）、`Slots` :150。
- `PixelEngine/CS2D/CS2DGameSquad.cpp`：`RefreshRolesForTeam` :428、`AssignSquadRoles` :477、
  `UpdateSquadFocus` :604。
- `PixelEngine/CS2D/CS2DGame.hpp`：`BotRole` :54、`SquadOrder` :61、`GetLossStreak` :1126、
  `CoachState` :1237（`changesThisRound` :1247、`sameSideStreak` :1248、
  `lastAttackSide`、`notes`、`lastOrder`）、经济快照 :1288-1290。
- `PixelEngine/CS2D/Tuning.hpp`：角色 :284-287、意图预算 :289-295（含 `SameSideStreakLimit` :295）。
- `PixelEngine/CS2D/TacticalIntel.hpp/.cpp`：`SiteHeat` 结构、tau 14s / 上限 6 / +1、
  `SiteIndexAt` 220px 阈值。
- `assets/config/tactics/*.ini`：每图 18 个 `[slots.]` 段（4 plain + 14 @）、
  look/anchor 58–70 段、`[areas]` 6 键、`[perception]` 5 段。
- `assets/config/coach/*.txt`：`_common.txt` 65 行、每图 14 张卡 + advisory（6/8/7）+
  few-shot 2 条。
- `docs/llm-coach-v2-plan.md`：V2 已完成（2026-11-06）；30 局 T 胜率 63.3%→46.7%
  （p=0.227 不显著）；12 局 58.3% vs 58.3%；拒绝率 13%+10%+10%。
