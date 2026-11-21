# PIX-37 规则教练全量接线实施计划

> 目标：清单里的每一条决策类型与数据源，都能被规则教练读到、并在回合中真正用出来。
> 对照基准：`~/workspace/your_files/rules-coach/规则教练决策数据与决策类型总清单.md`
> （2026-11-18 live 树核实，§6 三项待核实已于 2026-11-19 核实完毕）与
> `~/workspace/your_files/rules-coach/规则教练完善审计与改进提案.md`。
> 代码基准：Sun 电脑 live 工作树（HEAD ce41a76 + 未提交改动），行号以清单附录 B 为准；
> 实现前在 live 树上复核一次行号（清单末尾三条 Select-String 命令已给出复核方法）。
> 实施：本地 Codex（额度 2026-11-20 10:01 恢复）；验收：Muse；指派：Sun。
> 完整版在仓库 `docs/PIX-37-rules-coach-wiring-plan.md`；Linear 上只放任务清单。

## 0. 硬原则（Sun 2026-11-18，贯穿全部任务）

**不删除任何一个决策或数据。** 当前没有被实际调用的，想办法接线让它能够被调用，
而不是因为没被用过就当作死代码删除。这是严重不允许的。

- 本计划没有任何一项建议删除。凡审计提案原先写"接线或删"的项（hasBranch、
  sameSideStreak、RoleTransitionSec、Lurk/Awper 坍缩），一律只接线、只实现。
- 验收不通过的项不删、不降级，留在下一批继续做（见 T37）。
- 手册里标注 advisory（纯提示）的条目不硬写成规则，但也不删，见 T34 分类。
- 确定性红线：本次改动不碰 `rng_` 主链（打分只用确定性输入；`Save` 的 ±36 抖动是既有
  行为，不动它）；`rng_humanizer_` 不变。教练行为会变，golden hash 基线按 T03 重跑。

## 1. 验收总则：什么叫"完成"

**只有在游戏中真正检测到规则教练使用了相关指令，才算完成。** 代码写完、编译通过、
单测通过，都只是支撑证据，不是完成标准。每一项任务都有自己的验收点，格式统一为
"局内检测 + 运行方式"，全部走同一套验收基建（Phase 0）。

- 验收基建（T01–T03）先行：没有日志与场景驱动器，后面所有验收点都无法执行。
- 验收方式分三档：**局内日志**（结构化决策日志里出现该指令/信号）、**场景测试**
  （注入指定对局状态，断言教练的输出）、**30 局基准**（分布级指标，见 T35）。
- 每项验收点写明"至少出现几次"或"必须出现"，不写"应该""大概"。
- 验收由 Muse 独立跑：Codex 交代码，Muse 按验收点逐项跑场景/基准，逐项打勾；
  不通过的项写回 T37 的未通过清单，不返工到"看起来差不多"为止。

## 2. Phase 0 — 验收基建（先做，后面所有任务的验收都依赖它）

### T01 结构化教练决策日志

- 清单对照：§1.9（TeamPlan 17 字段）、§2.8（调度与守卫）、§2.4（CoachState）。
- 改动位置：`CS2DGameCoach.cpp` `CoachApplyPlan`（:1097-1286）出口处；
  新写盘点复用 `CoachRecordRound`（:1287-1315）的回合写盘路径。
- 做法：每次下发规则计划写一行 JSONL（`coach_wiring.jsonl`，与 `coach_rounds.jsonl`
  同目录）：round、team、source="rule"、intent、target、tempo、slots[5] 全 9 字段
  （role/area/canTrade/canRotate/lookArea/lookAngle/hold/util/utilArea）、
  templateKey（实际命中的表段，如 `T.SplitA@A` 或 plain 回退）、signals
  （heatA/heatB、enemyComp 五类计数、utility 烟/闪/火/雷、buyPlan、lossStreak、
  lastAttackSide、sameSideStreak、enemyLastAttackSide、enemyCountA/B、control 信号）、
  guards（KEEP/sticky/T 锁/预算拦截标记）、sanitized（是否被消毒改过及原因）、
  hasBranch/branchIntent/branchAfterSec。
- 验收点：跑 1 局完整比赛（8 回合），每回合每队至少 1 行；全部字段齐全，
  无缺失；templateKey 能区分 @ 段命中与 plain 回退。

### T02 场景驱动器 + 验收脚本

- 清单对照：全部任务的验收执行入口。
- 改动位置：新增 `tools/coach/verify_wiring.py`（新文件）+ headless 模式的场景注入
  入口（经济状态、比分、炸弹状态、钳子持有、情报 heat、敌方习惯、分侧敌人数、
  区域控制，均可注入；复用现有 `gameplay_tests` 的 headless 能力）。
- 做法：脚本按任务逐项跑场景（每个场景 = 注入状态 → 跑 N 回合 → 读 T01 日志断言），
  输出逐项 PASS/FAIL 表。场景清单与各任务的验收点一一对应。
- 验收点：脚本能跑通全部场景并输出完整表格；任一场景注入失败即报 FAIL，不静默跳过。

### T03 golden-hash 基线重跑

- 清单对照：确定性红线（见 §0）。
- 改动位置：复用回放哈希 benchmark runner（游戏二进制之外，dev-only）。
- 做法：P0+P1 改动前后各跑一次 replay-hash 基准，记录新基线。教练行为变化导致
  旧基线失效是预期的；重跑后新基线在连续两次运行中稳定，即证明改动是确定性的。
- 验收点：新基线两次运行哈希一致；改动清单里注明哪些行为变了、为什么变。

## 3. Phase 1 — P0：修 bug / 接线（小改动，大收益）

### T04 战术表 `@Target` 模板加载（§5-1）

- 清单对照：§2.1（14 段 @ 未加载）、§1.1（Split/Fake/Save/Stack/Retake 无表可用）、§5-18（templateDefault 覆盖链）。
- 改动位置：`Coach/TacticsTable.hpp` `Load`（:60-140）、`GetAll()`。
- 做法：`GetAll()` 再扫 `slots.` 前缀的段，把 `[slots.<Team>.<Intent>@<Target>]`
  也登记进 slots map（key 保留 `@Target` 后缀）。只加加载，不改查找语义。
- 验收点：启动日志/单测断言每图 18 段全加载；局内：Split/Fake 计划下发时日志
  templateKey 命中 @ 段（非 plain 回退）至少 1 次。

### T05 `changesThisRound` 预算接线（§5-2）

- 清单对照：§2.7（IntentMaxChanges=2 未生效）、§2.8（变更预算）。
- 改动位置：`CS2DGameCoach.cpp` `CoachApplyPlan`（:1105，`counters_.intentChanges`
  的判断点）、`CoachBeginRound`（:1337-1372，清零）。
- 做法：意图真变时同步 `++st.changesThisRound`；回合开始清零。不删预算，不改数值。
- 验收点：场景——同一回合内连续 3 次非关键事件改意图，第 3 次被预算拦截
  （日志含 budget-blocked 标记，意图未变）。

### T06 `hasBranch` 时间差打法接线（§5-3）

- 清单对照：§1.9（hasBranch/branch* 字段）、§2.8（分支触发逻辑已就绪）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）回合开始分支；
  触发逻辑 `CoachUpdate`（:205-244）不动。
- 做法：Default 计划设 `hasBranch=true`（如 20s 后按情报转 Rush 另一侧），
  branchIntent/branchAfterSec/branchTarget 一并填。只接线，不改触发逻辑。
- 验收点：局内日志出现 hasBranch=true 的 Default 计划，且 20s 后按情报触发分支
  （branchIntent=Rush 另一侧）至少 1 次。

### T07 `sameSideStreak` 接线（§5-4）

- 清单对照：§2.4（sameSideStreak 零读写）、§2.7（SameSideStreakLimit=3 无代码读）。
- 改动位置：`CS2DGameCoach.cpp` `CoachThink`（:319-491）/`CoachRecordRound`（:1287-1315）；
  `SanitizeCoachPlan`（:973-1096）。
- 做法：每回合按实际主攻侧更新（同侧 +1、换侧清零）；`Sanitize` 用它替代/补充
  `lastAttackSide` 做连续同侧判断；`RuleCoachCore` 回合开始读它（≥2 强制换侧）。
- 验收点：场景——连续同侧 3 回合后，下一回合规则改发另一侧或 Default
  （日志 sameSideStreak=3 且意图切换）；日志每回合含 streak 字段。

### T08 `SanitizeCoachPlan` 复用于规则计划（§5-5、§5-17）

- 清单对照：§1.1（消毒兜底）、§2.3（CT stack 无情报检查）、§3（Sanitize 行）。
- 改动位置：`CS2DGameCoach.cpp` `CoachThink`（:319-491），`RuleCoach` 返回后调一次。
- 做法：规则计划也过消毒（跳过已下包分支，函数本身已处理）；规则 CT 回合开始发
  Stack 时同样过"无情报不赌点"检查。注意规则计划的 avoidA/avoidB 与消毒的
  "同侧连续 → 默认分兵"是两套逻辑，复用后测一次，避免双重整形把计划改僵。
- 验收点：场景——CT 无情报时发 Stack，消毒后变 Default（日志含 sanitized 标记与原因）；
  场景——T 连续同侧压点被改默认分兵（同上）。

### T09 Rotate 进表 + 进 schema（§5-6）

- 清单对照：§1.1（RotateA/RotateB）、§2.1（kIntents 11 个无 Rotate）、§2.5（schema 无 rotate）。
- 改动位置：`Coach/TacticsTable.hpp` `kIntents`（:96）、`CS2DGameCoachAI.cpp`
  `kCoachSchema`（:24，intent enum）。
- 做法：`kIntents` 加 RotateA/RotateB；`kCoachSchema` 加 rotate_a/rotate_b。
  （与 minimind 步骤 1–2 稿的待定 1 联动：训练版 schema 用 13 个意图。）
- 验收点：转点意图走表（日志 templateKey 含 Rotate 表段）；schema 含 rotate_a/rotate_b
  （LLM 路验收以规则路为主，LLM 能开出转点为加分项）。

## 4. Phase 2 — 决策质量：意图与信号（教师天花板在这里）

### T10 意图打分制（§5-16、§4-1/4/7/11）

- 清单对照：§3（规则只用 2 组半数据）、§2.5（摘要 14 组）、§4-1（站位分布）、
  §4-4（heat）、§4-7（近 3 回合）、§4-11（合法闭集）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）回合开始分支。
- 做法：回合开始改为"对每个候选意图打分，取最高"。信号全部取自 `BuildStateDigest`
  已有字段：情报热度（`HeatOfSide` 差值）、近 3 回合结构化行（连续同 intent+同 target
  失败 → 换点，手册总规则第 4 条）、我方站位分布（中路是否已控、两侧是否有人，
  替代固定分兵）、合法闭集（规则也按同一份 allow 集发意图：已下包禁换侧、穷局禁
  rush/split/fake——现在这些约束只卡 LLM，不卡规则）。打分权重放 `tactics.ini`
  或 `Tuning`，不要硬编码在 C++ 里。
- 验收点：30 局基准中意图分布熵较基线显著上升（不再是 Default 一家独大）；
  日志每行含打分输入（heat 差、站位分布、近 3 回合行）；穷局场景下规则不发
  rush/split/fake（日志 intent 不在禁集内）。

### T11 MID 控中意图（§5-10、§1.2）

- 清单对照：§1.2（MID 无规则分支）、§3（RoundStart T 行）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）回合开始分支。
- 做法：Default 变体：2 人控中 + 两侧各 1 人架点 + 1 人自由。用在"无情报 + 开局"的回合
  （替代纯 Default 分兵）。
- 验收点：场景——开局无情报时，T 发 target=MID 的 Default 变体（日志 target=MID
  且槽位含 2 个 Mid 控中位）至少 1 次。

### T12 RoundStart 发 Split / Fake（§1.1、§4-13、§2.2）

- 清单对照：§1.1（Split/Fake 规则发不出）、§2.2（42 张卡的 when 条件）、§4-13（卡片条件翻译）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）回合开始分支；
  槽位模板走 T04 修好后的 @ 表（`T.SplitA@A`、`T.Fake@B` 已存在）。
- 做法：Split 条件——有情报（heat 差小、两路都有人）+ 经济 Full/Half + 时间充裕 →
  分路夹击；Fake 条件——佯攻侧有接触/噪音 + 主攻侧 heat 低 → 佯攻后转点
  （`FakeThenOther` 的 `branchTarget` 语义可用，见 T06）。
- 验收点：30 局基准中 Split 与 Fake 各至少出现 1 次（日志 intent=SplitA/B、
  FakeThenOther，templateKey 命中 @ 段）；场景——满足 Split 条件时发 Split，
  不满足时不发（各 1 次）。

### T13 敌方习惯记忆（§5-9、§4-6、§2.4）

- 清单对照：§2.4（记忆只记己方，敌方习惯无字段）、§6（"上回合主攻"两种语义，
  CT stack 卡无法判定）、§4-6（近局习惯）。
- 改动位置：`Coach/CoachTypes.hpp` `RoundMemo`（:95-113）加敌方主攻侧/首触字段；
  `CS2DGameCoach.cpp` `CoachRecordRound`（:1287-1315）写入（从对手记忆或本回合接触推导）。
- 做法：只加字段、不删现有字段。CT 回合开始读敌方习惯：对面连续两回合主攻 A
  （或 A 侧 heat 高）→ StackA / 3-1-1 重防 A；对面无固定侧 → 2-2-1。
  T 的 avoidA/avoidB 扩展为"意图+目标对"（连续同 intent+同 target 失败 → 换点）。
- 验收点：日志含 enemyLastAttackSide 字段；场景——敌方连续两回合主攻 A，
  第三回合 CT 发 StackA 或 3-1-1 重防 A；场景——T 连续同 intent+同 target 失败两回合，
  第三回合换意图或换侧。

### T14 道具 / 敌枪 / 联系人 / lastOrder / 笔记进规则（§4-2/3/5/8/9）

- 清单对照：§2.5-6（道具明细）、§2.5-7（敌枪构成）、§2.5-10（敌联系人）、
  §2.5-11（lastOrder）、§2.5-12（参考笔记）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）回合开始与事件分支。
- 做法：Split/Rush 发出条件——烟 ≥1 才能道具开点，无道具 → Default；打分——敌狙多 →
  避长廊/慢打，敌冲锋多 → 保持距离/架点；TimeLow/EnemySpotted——按最新接触区域定
  主攻侧（现在只读 plan.target）；变更预算判断——新意图与上条是否同向，避免来回横跳；
  复盘结论进规则——笔记按关键词（如"连续输 A"）触发换侧。
- 验收点：场景——烟 ≥1 时 Split/Rush 允许道具开点（日志 util=1 且信号 utility.smoke≥1）；
  场景——敌狙多时规则选慢打/避长廊（日志含 enemyComp.sniper 计数与 tempo=Slow）；
  场景——TimeLow 时按最新接触区域定主攻侧（日志 target=接触区所在侧）；
  场景——新意图与上条同向时不横跳（日志 lastOrder 与 intent 一致）。

### T15 情报 heat 进 RoundStart（§4-4、§2.3）

- 清单对照：§2.3（`RuleCoachCore` 零次使用 `intel_`）、§3（RoundStart 行）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）回合开始分支。
- 做法：回合开始读 `HeatOfSide`：热侧避开/冷侧主攻（现在只有转点读 heat）。
- 验收点：场景——A 侧 heat 高、B 侧冷，T 回合开始主攻 B（日志 heatA/heatB 与
  intent=Default/RushB 一致）。

### T16 减员变速解拦截（§1.8 soft-KEEP）

- 清单对照：§1.8（EnemyDown/AllyDown 分支常被 soft-KEEP 拦截，规则层"减员变速"
  只在计划过期后生效）。
- 改动位置：`CS2DGameCoach.cpp` `CoachThink`（:339 soft-KEEP 处）。
- 做法：soft 事件且计划有效时，意图不变、tempo 可调（EnemyDown → Fast 前压，
  AllyDown → Slow 收缩）。只调 tempo，不换意图，不动 KEEP 本身。
- 验收点：场景——计划有效期内减员，tempo 变化且 intent 不变（日志 intent 同前、
  tempo 已变）。

### T17 HumanDied 规则分支（§1.8）

- 清单对照：§1.8（HumanDied 是 criticalEv，但 `RuleCoachCore` 没有它的分支，
  会落到回合开始逻辑）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）。
- 做法：玩家阵亡 → 规则重算（T 转 Default/slow 收缩或按残局守包；CT 收位守点）。
  只加分支，不改事件优先级。
- 验收点：场景——玩家阵亡后下一条规则计划的 intent/tempo 与阵亡前不同
  （日志 event=HumanDied 且计划已更新）。

## 5. Phase 3 — 经济与赛点（跨回合状态）

### T18 跨回合经济状态机 + LossStreak（§5-8、§5-14、§4-10）

- 清单对照：§2.6（`GetLossStreak` 教练层全不读）、§2.4（CoachState 无 lastBuyPlan）、
  §4-10（经济差值）、§2.2（手册"强起失败禁连强""输满起后 eco→满起"）。
- 改动位置：`CS2DGame.hpp` `CoachState`（:1237-1290）加 `lastBuyPlan` + `lossStreak`；
  `CS2DGameCoach.cpp` `CoachRecordRound`（:1287-1315，顺手记比分/胜负）、
  `DecideBuyPlan`（:1409-1430）。
- 做法：上回合 Force 且输 → 这回合强制 Eco（禁连强）；连败 ≥2 且下回合奖金能满起 →
  Eco 存钱；输满起 → eco→满起；经济差值进买档（我穷敌更穷 → 强起打经济，手册有、
  规则没有）。阈值本身不动（1800/2600/3500 已合理）。
- 验收点：场景——上回合 Force 且输，这回合 buyPlan=Eco（日志 lastBuyPlan=Force +
  won=false → Eco）；场景——连败 ≥2 且奖金能满起 → Eco；场景——我穷敌更穷 →
  Force（日志含经济差值）。

### T19 赛点分支（§5-11、§4-12）

- 清单对照：§1.8（MatchPoint 是 criticalEv，无决策分支）、§2.2（手册决策序第 3 条
  "赛点/半场末 → 强起"）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）、`DecideBuyPlan`（:1409-1430）。
- 做法：赛点 → Force 买档 + tempo Fast（除非已下包/残局）。只加分支，不改 MatchPoint
  事件本身（`CS2DGameRound.cpp:43-50` 只发一次）。
- 验收点：场景——比分 7-7（赛点），规则发 Force 买档 + tempo Fast
  （日志 buyPlan=Force、tempo=Fast、event=MatchPoint）。

### T20 钳子感知回防（§5-12、§2.10）

- 清单对照：§2.10（hasDefuseKit 机制已落地，回防逻辑没读它）、§6（mustRush 11s
  按 10s 拆包算，不认 4s 钳子）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）下包后 CT 分支。
- 做法：有钳持有者 `mustRush` 阈值 11s → 5s；钳子持有者优先进点拆，其余掩护。
  无钳保持 11s。
- 验收点：场景——有钳持有者存活且炸弹已下，mustRush 阈值 5s（日志 mustRushAt=5s
  且 kit carrier 槽位 role=Entry 进点拆）；场景——无钳时 11s。

### T21 回防分工模板（§5-15、§4-14）

- 清单对照：§2.2（手册回防节"先清包点近处、两路同时进"）、§4-14（advisory
  可执行项进模板）、§3（下包后行：CT 全员 Entry@包点）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）下包后分支；
  槽位模板走 T04 修好后的 @ 表（`CT.RetakeA@A` 等）。
- 做法：CT——钳子持有者进点拆 + 2 人掩护 + 2 人控回防路线（不再是全员 Entry@包点）；
  T 守包补分工——1 人看拆包角度、1 人控回防路线、1 人延迟 peek（手册已写）。
  道具清点：有烟先封回防路。
- 验收点：局内下包后 CT 槽位日志出现分工（1 拆 + 2 掩护 + 2 控回防路线）；
  T 守包日志出现三分工；有烟时日志 util=1 且 utilArea=回防路。

## 6. Phase 4 — 槽位与执行（分槽、锚点、护栏）

### T22 按能力分槽（§5-13）

- 清单对照：§3（`CoachApplyPlan` 行：按 BOT 下标顺序分槽）、§2.9（SquadOrder）。
- 改动位置：`CS2DGameCoach.cpp` `CoachApplyPlan`（:1097-1286）分槽前。
- 做法：分槽前排序——拿 AWP 的分 Awper、钱最少的分 Save 位、其余按枪法/移速分
  Entry/Support。`BotRuntime.difficulty` 已有（`ResetBotAi` 保留它），直接用。
- 验收点：场景——队内有人拿 AWP，分槽日志把 Awper 位分给该 BOT；钱最少的分
  Save 位；不再是纯下标顺序（日志 slot→bot 映射与下标顺序不一致时有据可查）。

### T23 规则槽位补 look / hold / util（§4-16、§1.7）

- 清单对照：§1.7（规则手写槽位只填 4 项，look/hold/util 恒为默认）、§2.1（表里
  look 70 条/图、hold ~50 条/图、util 14 条/图）。
- 改动位置：`CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）手写槽位处；
  或规则直接用表槽位（T04 修好后）。
- 做法：规则手写槽位补 look/hold/util 字段；或规则计划优先取表槽位
  （表里有就用表里的 look/hold/util）。
- 验收点：局内规则计划日志的槽位含非默认 look/hold/util 至少 1 次
  （或全部走表槽位，日志 templateKey 非空）。

### T24 锚点放开给规则模板（§4-17、§5-18）

- 清单对照：§1.6（规则计划永远不用锚点）、§2.1（anchor 58–70 个/图）、
  §2.9（`CoachApplyPlan` 里 hold≠Default 才用锚点）。
- 改动位置：`CS2DGameCoach.cpp` `CoachApplyPlan`（:1097-1286）锚点使用处。
- 做法：放开"仅 hold≠Default 才用锚点"的限制，让规则模板也能用锚点。
  （V2 遗留：几何近似锚点只给了显式 hold 的卡片用；规则模板用之前需要人工采点校准，
  或先只在 mirage 上试点。）
- 验收点：规则模板计划日志含 anchor 坐标（holdX/holdY 非空），且 BOT 实际走到
  锚点附近（位置日志/回放抽查，1 次即可）。

### T25 RefreshRolesForTeam 护栏（§6 窄冲突窗口）

- 清单对照：§6 已核实（教练计划优先；唯一窄窗口：教练计划本身缺 Entry/Anchor 时，
  补断档可能把一个 Support 提上去，与教练意图相悖）。
- 改动位置：`CS2DGameSquad.cpp` `RefreshRolesForTeam`（:428）。
- 做法：补断档前先问教练计划——教练计划缺 Entry/Anchor 是有意的（如全员 Anchor
  的 Save/守包计划）时不提人；只在教练计划本身要求 Entry/Anchor 却缺人时补。
- 验收点：场景——教练发全员 Anchor 的 Save 计划，回合内无 Support 被提成
  Entry/Anchor（日志 role 无变化）；场景——教练计划要求 Entry 却缺人，补断档
  正常补（日志有补位记录）。

## 7. Phase 5 — 执行层角色（动 BOT 决策树，量大）

### T26 Lurk / Awper 扩展成真角色（§5-7、§1.5）

- 清单对照：§1.5（教练 5 角色 → 执行 3 角色，Lurk/Awper 坍缩）、§2.1（槽位模板
  大量使用 Lurk/Awper）、§2.7（BotRole 3 值）。
- 改动位置：`CS2DGame.hpp` `BotRole`（:54-58）加 Lurk/Awper；
  `CS2DGameCoach.cpp` `CoachApplyPlan`（:1142-1146）映射直通；
  BOT 决策树（`CS2DGameBots.cpp` 三处按角色定移速：:908-910、:1015-1017、:1401，
  据 2026-11-17 快照核实）配新倍率与行为。
- 做法：Lurk——绕后/断后，不参与正面；Awper——架狙位，移速慢、开镜优先。
  行为参数先按模板里的 look/hold 数据定，不够再补（审计提案 §6 待定 4 的建议）。
  移速倍率与行为要与 T27 的过渡机制一致。
- 验收点：局内日志 role=Lurk/Awper 出现；行为抽查——Lurk 绕后/断后不参与正面
  （位置日志），Awper 架狙位移速慢（移速遥测）；决策树三处按新角色取值
  （代码断言或日志）。

### T27 RoleTransitionSec 3.0s 实现（§5-19，§6 新发现）

- 清单对照：§6（`RoleTransitionSec` 3.0s 只有定义、零处引用，角色一换速度立即跳变）。
- 改动位置：`CS2DGame.hpp` `CoachState`/`BotRuntime` 记录角色切换时刻；
  `CS2DGameBots.cpp` 三处按角色定移速处（:908-910、:1015-1017、:1401）。
- 做法：按角色切换时刻插值倍率，3 秒内从旧倍率过渡到新倍率。**按 Sun 的不删除原则：
  实现它，而不是删旋钮。**
- 验收点：场景——BOT 换角色后 3 秒内移速倍率插值过渡（遥测记录倍率渐变，
  而非瞬跳）；过渡结束后倍率等于目标角色倍率。

## 8. Phase 6 — 数据质量校准（清单 §6 逐项）

### T28 拆包数字校准（§6 过期数字）

- 清单对照：§6（拆包 5 秒三处过期；"最后 6 秒才强拆"与代码阈值不一致）。
- 改动位置：`CS2DGameCoachAI.cpp` 摘要（"拆包需5s"）、`assets/config/coach/_common.txt`
  回防节、retake 卡 advisory；代码阈值 `mustRush` 11s/5s（T20）。
- 做法："拆包需要 5 秒"→ 改成"有钳 4s / 无钳 10s"；"最后 6 秒才强拆"与代码
  11s/5s 阈值对齐。只改数字与文字，不改机制。
- 验收点：三处文本一致且与代码阈值一致（grep 复核）；T20 的 5s/11s 与手册数字一致。

### T29 CT 的 C4 掉落 StackA 硬编码（§6 硬编码）

- 清单对照：§6（`CoachThink:418` 不管掉落在哪一侧都发 StackA，target 却是实际区域名）。
- 改动位置：`CS2DGameCoach.cpp` `CoachThink`（:406-424）C4 掉落分支。
- 做法：按实际侧发 StackA/StackB，target 保持实际区域名。
- 验收点：场景——C4 掉 B 侧，CT 发 StackB（日志 intent=StackB，target=掉落区域）；
  场景——掉 A 侧发 StackA。

### T30 reason / ttl / 经济档边界对齐（§6 硬编码与不一致）

- 清单对照：§6（reason 限 60 字符 vs 手册 20 汉字；ttl 三处语义；经济"中"档
  2600 边界只在代码里）。
- 改动位置：`CS2DGameCoachAI.cpp` `kCoachSchema`（:28，reason 限 60 字符）、
  `_common.txt` 总规则第 6 条；卡片 ttl / schema 3–30 / TTL 地板 18/12/10；
  `DecideBuyPlan`（:1409-1430）2600 边界。
- 做法：reason 取一个统一标准（建议 60 字符，schema 为准，手册改）；ttl 写明三者关系
  （卡片建议值、schema 上下限、TTL 地板是执行层下限，三者不冲突）；经济"中"档
  2600 边界写进手册。只对齐文字与文档，不改数值。
- 验收点：grep 复核三处一致；手册含 2600 边界说明。

### T31 "已控"信号（§6 when 条件可判定性）

- 清单对照：§6（12/42 条 when 用"已控"，规则层没有区域控制度量）、§4-1（站位分布）。
- 改动位置：新增区域控制度量（我方在该区人数/存活，取自 §2.5-5 站位分布）；
  `CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）rush/split 条件。
- 做法：定义"已控"= 我方在该区 ≥1 人存活且无敌方近期接触（或人数 ≥2，取一者，
  写进文档）；rush/split 的"已控"条件用它判定。不满足时不发对应意图。
- 验收点：场景——某区我方 2 人存活时判定"已控"（日志含 control 信号）；
  场景——该区无人时不发依赖"已控"的 Rush/Split。

### T32 分侧敌人数（§6 when 条件可判定性）

- 清单对照：§6（约 20 条 when 用"敌A≤1 / 敌B≥3"，规则层只有 heat，heat ≠ 人数）。
- 改动位置：新增分侧敌方计数（存活数 + 近期接触数，区别于 heat）；
  `CS2DGameCoach.cpp` `RuleCoachCore`（:512-756）rush/split/stack 条件；
  T01 日志 signals 加 enemyCountA/B。
- 做法：分侧敌人数 = 该侧存活敌方 BOT 数（游戏层已有 `AliveCount`，按区域/包点侧分）；
  heat 仍用于威胁度，两者并存、不互相替代。
- 验收点：场景——敌 A 侧 ≤1 时 rush 条件可判定（日志含 enemyCountA/B 且与意图一致）；
  场景——敌 B 侧 ≥3 时对应条件触发。

### T33 `[areas]` 映射退化（§6 覆盖率）

- 清单对照：§6（`[areas]` 只有 6 个关键词映射；`AreaHint("Long"/"Short"/"Tunnel")`
  在 mirage/ancient 上回退到包点，规则代码的"侧翼区域"在非 dust_2 图上是退化的）。
- 改动位置：`assets/config/tactics/*.ini` `[areas]`；`AreaHint` 调用处。
- 做法：给 mirage/ancient 补真实区域映射（长廊/短廊/通道的实际区域名），
  不再回退到包点；或文档注明退化并给出每图的等价区域。
- 验收点：三图 AreaHint 均返回真实区域而非包点回退（单测/日志复核）。

### T34 few-shot 补齐 + advisory 分类（§6 覆盖率）

- 清单对照：§6（few-shot 只有 T default、CT retake_a；Split/Fake/Save/Stack 无示例；
  advisory 21 条未分类）、§4-14/15。
- 改动位置：`assets/config/coach/*.txt` 每图 few-shot；清单文档 §6。
- 做法：补 Split/Fake/Save 的 few-shot 示例（格式与现有两条一致）；advisory 21 条
  逐条分类——可执行的（如"回防两路同时进"）→ 写进回防模板（T21）；纯提示的
  （如"听到脚步只自己查"）→ 标注 advisory、不承诺执行。**分类不删除任何一条。**
- 验收点：文档/配置层面——few-shot 覆盖 5 类意图（default/retake/split/fake/save）；
  advisory 21 条逐条有分类标注；可执行项已进模板（T21 验收复核）。

## 9. Phase 7 — 基准与数据采集（收口）

### T35 30 局配对基准重跑

- 清单对照：审计提案 §4-16（评测）、V2 工具链（`docs/llm-coach-v2-plan.md`）。
- 改动位置：复用 V2 评测基建（30 局配对基准 McNemar、决策日志、plan trace）。
- 做法：规则改动前 vs 改动后；再加规则 vs LLM。指标：胜率（McNemar）、意图分布熵、
  Split/Fake 出现率、计划拒绝率、首杀转化。
- 验收点：拒绝率（`invalid_consistency` + `invalid`）从 23% 降到 10% 以内；
  意图分布熵显著上升（不再是 Default 一家独大）；Split/Fake 出现率 >0；
  胜率变化如实记录（不要求提升，只要求记录 + McNemar）。

### T36 `coach_rounds.jsonl` 写盘 + gen 字段

- 清单对照：minimind 步骤 1–2 稿（日志在 `CoachRecordRound` 写盘，
  新文件 `coach_rounds.jsonl`，一行一队一回合，含 match_id、plan.source）；
  审计提案 §5（`plan.source` 要能区分"规则新版"和"规则旧版"）。
- 改动位置：`CS2DGameCoach.cpp` `CoachRecordRound`（:1287-1315）。
- 做法：写盘点不动；`plan.source` 加 gen/版本号字段，避免新旧教师数据混在一起。
  （minimind 的三个待定：意图集 11 vs 13、台词字段、日志形状，仍等 Sun 定；
  本任务只做写盘与版本区分，不替他定。）
- 验收点：跑 30 局后文件含新版规则计划行，source=rule 且 gen=新版；
  Split/Fake/MID 样本数 >0（与 T12/T11 联动）。

### T37 全量验收汇总

- 清单对照：本计划全部任务。
- 改动位置：无代码改动；验收记录写进仓库 `docs/PIX-37-rules-coach-wiring-acceptance.md`。
- 做法：Muse 按 T01–T36 逐项跑验收，输出 PASS/FAIL 表；不通过的项写进未通过清单，
  保留在下一批，不删、不降级、不写"基本完成"。
- 验收点：汇总表本身就是验收——37 项逐项有结论；未通过项有明确的下一批归属。

## 10. 不做项

- 不改 LLM 链路本身（`CoachTryLlm`、`CoachDrainLlm` 的调用逻辑、超时、advisory 门
  均不动）；只动 schema 的 intent 枚举（T09）与摘要数字校准（T28）。
- 不改事件优先级与调度守卫的数值（IntentMinHold 10s、IntentChangeCooldown 6s、
  IntentRenewSec 8s、SilentSeconds 30s、T 18s 执行锁、调用预算 8 次均不动）；
  只接线未生效的预算（T05）与未读的旋钮（T07、T27）。
- 不动 `rng_` 主链与 `rng_humanizer_`；不改回放哈希工具本身，只重跑基线（T03）。
- 不替 Sun 定 minimind 的三个待定（意图集 11 vs 13、台词字段、日志形状）与
  LoRA 训练本身；本计划只负责把规则教练这个老师做到极致。
- PIX-32/33 的统一验收仍等 Sun 自己做，不在本计划内。

## 附录：清单逐条对照总表

每一条清单项 → 接线位置 → 本计划任务 → 验收点（局内检测）。

### A. §1 决策类型

| 清单项 | 接线位置 | 任务 | 验收点 |
|---|---|---|---|
| Default 意图 | RuleCoachCore 回合开始/事件默认 | 已有，T10 打分制覆盖 | 日志 intent=Default 出现 |
| RushA/RushB | RuleCoachCore（仅 0 胜连败或 TimeLow）→ T10 打分制扩展触发条件 | T10 | 30 局中出现；场景可触发 |
| SplitA/SplitB | RuleCoachCore 回合开始（新增分支）+ @ 表 | T12、T04 | 30 局中各 ≥1 次 |
| StackA/StackB | RuleCoachCore 下包后 T；CT 回合开始（新增） | 已有（T）+ T13（CT） | 日志 intent=StackA/B |
| RetakeA/RetakeB | RuleCoachCore 下包后 CT + @ 表 | 已有 + T21、T04 | 下包后 CT 日志 intent=Retake |
| Save | RuleCoachCore 经济崩 + @ 表 | 已有 + T04 | 经济崩场景 buyPlan=Eco 且 intent=Save |
| RotateA/RotateB | CoachTryRotate + kIntents + schema | T09、已有转点机制 | 日志 templateKey 含 Rotate 表段 |
| FakeThenOther | RuleCoachCore 回合开始（新增分支）+ branchTarget | T12、T06 | 30 局中 ≥1 次 |
| target MID | RuleCoachCore Default 变体 | T11 | 场景 target=MID ≥1 次 |
| target A/B/空 | 已有 | — | 日志 target 字段 |
| Tempo 三档 | RuleCoachCore 各分支 + T16 减员变速 | 已有 + T16 | 日志 tempo 随事件变化 |
| BuyPlan 五档 | DecideBuyPlan + 跨回合状态机 | 已有 + T18、T19 | 场景逐档可触发 |
| CoachRole 5 角色 | CoachApplyPlan 映射 + BotRole 扩展 | T26 | 日志 role=Lurk/Awper 出现 |
| HoldStyle 4 档 | 规则槽位补 hold + 锚点放开 | T23、T24 | 日志 hold≠Default 出现 |
| SlotOrder 9 字段 | 规则手写槽位 / 表槽位 | T23、T01 | 日志槽位 9 字段齐全 |
| 事件 12 种 | CoachOnEvent + RuleCoachCore 分支 | 已有 + T17（HumanDied）、T19（MatchPoint） | 每种事件触发后日志有对应计划 |
| TeamPlan 17 字段 | CoachThink/ApplyPlan/RecordRound | T01、T06、T07、T08 | 日志含全部 17 字段 |

### B. §2 数据源

| 清单项 | 接线位置 | 任务 | 验收点 |
|---|---|---|---|
| tactics ini（@ 段） | TacticsTable::Load | T04 | 每图 18 段全加载 |
| tactics ini（look/anchor/perception） | CoachApplyPlan / 规则槽位 | T23、T24 | 日志含 look/anchor |
| 战术手册 _common | RuleCoachCore / DecideBuyPlan / Sanitize | T10、T18、T19、T21、T28 | 手册规则逐条有代码落点（T37 汇总） |
| 42 张战术卡 | RuleCoachCore 分支条件 | T12、T13 | 卡片 when 条件可判定（T31/T32） |
| TacticalIntel heat | RuleCoachCore 回合开始 | T15 | 场景主攻冷侧 |
| RoundMemo（己方） | RuleCoachCore（已有部分）+ T13 敌方字段 | T13 | 日志含敌方习惯字段 |
| CoachState 全部字段 | 各任务 | T05、T07、T18、T01 | 日志含 streak/预算/买档 |
| 摘要 14 组 | RuleCoachCore 打分输入 | T10、T14、T15 | 日志 signals 含对应字段 |
| 经济数据 | DecideBuyPlan + 状态机 | T18 | 场景逐条可触发 |
| Tuning 9 旋钮 | 各任务 | T05、T06、T07、T27 | 旋钮逐个有代码读点 |
| 调度与守卫 | 已有（KEEP/sticky/T 锁/预算/TTL） | T01（日志标记）、T05 | 日志含守卫触发标记 |
| 小队执行数据 | CoachApplyPlan / Squad | T22、T25 | 分槽与护栏日志 |
| 游戏层可接数据 | 各任务 | T20（钳子）、T32（存活数）、T31（站位） | 场景可触发 |

### C. §4 只给 LLM 的 17 项 → 规则教练

| # | 数据 | 任务 | 验收点 |
|---|---|---|---|
| 1 | 我方站位分布 | T10 | 打分输入含站位分布 |
| 2 | 道具明细 | T14 | 烟 ≥1 才道具开点 |
| 3 | 敌枪构成 | T14 | 敌狙多 → 慢打 |
| 4 | 敌分布 heat 全量 | T15 | 主攻冷侧 |
| 5 | 敌联系人 | T14 | TimeLow 按最新接触定侧 |
| 6 | 近局习惯 | T13 | CT 读敌方习惯 |
| 7 | 近 3 回合结构化行 | T10 | 连续同 intent+target 失败 → 换点 |
| 8 | 上条指令 lastOrder | T14 | 不来回横跳 |
| 9 | 参考笔记 | T14 | 关键词触发换侧 |
| 10 | 经济差值 | T18 | 我穷敌更穷 → 强起 |
| 11 | 合法闭集 | T10 | 穷局不发禁集意图 |
| 12 | _common 全部规则 | T10/T18/T19/T21/T28 | 逐条有落点 |
| 13 | 42 张卡 when+槽位 | T12/T13 | 条件可判定 |
| 14 | advisory 21 条 | T34 + T21 | 逐条分类 |
| 15 | few-shot 2 条/图 | T34 | 覆盖 5 类意图 |
| 16 | 槽位 look/hold/util | T23 | 日志含非默认字段 |
| 17 | 锚点 anchor | T24 | 日志含 anchor 坐标 |

### D. §5 未接线 19 项

| # | 项 | 任务 | 验收点 |
|---|---|---|---|
| 1 | @Target 模板 14 段/图 | T04 | 18 段全加载；@ 段命中 |
| 2 | changesThisRound 预算 | T05 | 第 3 次改意图被拦截 |
| 3 | hasBranch 时间差打法 | T06 | hasBranch=true 且分支触发 |
| 4 | sameSideStreak + Limit=3 | T07 | 连续 3 回合后换侧 |
| 5 | Sanitize 复用于规则 | T08 | 消毒改计划有日志 |
| 6 | Rotate 进表 + 进 schema | T09 | 转点走表；schema 含 rotate |
| 7 | Lurk/Awper 真角色 | T26 | 日志 role 出现；行为抽查 |
| 8 | LossStreak 连败奖金 | T18 | 连败 ≥2 → Eco 存钱 |
| 9 | 敌方习惯记忆 | T13 | 敌方主攻侧字段；CT 重防 |
| 10 | MID 控中意图 | T11 | target=MID 出现 |
| 11 | 赛点分支 | T19 | 赛点 Force + Fast |
| 12 | 钳子感知回防 | T20 | 有钳 5s / 无钳 11s |
| 13 | 按能力分槽 | T22 | AWP 分 Awper；钱少分 Save |
| 14 | 跨回合经济状态机 | T18 | Force 输 → Eco；输满起 → eco→满起 |
| 15 | 回防分工模板 | T21 | 1 拆 + 2 掩护 + 2 控路 |
| 16 | 敌枪/道具/联系人进打分 | T10、T14 | 打分输入含三类信号 |
| 17 | 消毒的 CT Stack 检查 | T08 | 无情报不赌点 |
| 18 | templateDefault 覆盖链 | T04、T24 | @ 段加载后覆盖生效 |
| 19 | RoleTransitionSec 3.0s | T27 | 换角色后倍率 3s 插值过渡 |

### E. §6 数据质量检查清单

| 清单项 | 任务 | 验收点 |
|---|---|---|
| 拆包 5 秒（三处过期） | T28 | 三处一致且与代码阈值一致 |
| "最后 6 秒才强拆"与 11s/5s 不一致 | T28 | 手册与代码对齐 |
| CT C4 掉落 StackA 硬编码 | T29 | 按实际侧发 Stack |
| reason 60 字符 vs 20 汉字 | T30 | 取统一标准 |
| ttl 三处语义 | T30 | 写明三者关系 |
| 经济"中"档 2600 边界只在代码里 | T30 | 写进手册 |
| "已控"无信号（12/42 条） | T31 | 新增区域控制度量 |
| "敌A≤1/敌B≥3"无人数（约 20 条） | T32 | 新增分侧敌人数 |
| "上回合主攻"两种语义（CT 无法判定） | T13 | 敌方习惯字段 |
| "敌经济穷/富"规则层不读 | T18 | DecideBuyPlan 有敌均值（已读，RuleCoachCore 接入） |
| "购买=eco"规则层不读 buyPlan | T18 | RuleCoachCore 读 buyPlan |
| util 覆盖（只在 rush/retake 卡） | T23 | Split 卡道具开点 |
| hold/look 覆盖 | T23、T24 | 规则槽位含 look/hold |
| few-shot 只有 2 种 | T34 | 覆盖 5 类意图 |
| advisory 21 条未分类 | T34 | 逐条分类 |
| [areas] 6 键映射退化 | T33 | 三图返回真实区域 |
| 规则能发意图 6 类（缺 Split/Fake/MID） | T12、T11 | 30 局中三类均出现 |
| CT 回合开始决策多样性 1 | T13 | CT 按敌方习惯变阵 |
| 角色多样性 5→3 | T26 | 真角色 |
| 槽位多样性（look/hold/util 空） | T23 | 日志含非默认字段 |
| §6 已核实三项（移速倍率/角色优先级/情报喂入） | T26、T25、T15 | 复核命令通过；窄窗口护栏 |
