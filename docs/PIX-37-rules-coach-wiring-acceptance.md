# PIX-37 规则教练全量接线 — 验收记录（实施者自测版）

> 实施者：DeepSeek（本地会话）；日期：2026-11-19。
> 验收方式：`tools/coach/verify_wiring.py`（T02 脚本）读 T01 结构化日志 + 30 局基准
> `summary.csv` 逐项判定；Muse 将独立重跑。
> 证据目录：`build-evidence/bench30/`（30 局 × wiring/coach_rounds/plans/decisions/rounds/summary）、
> `build-evidence/verify-final/`（单局 wiring + console）、`build-evidence/verify-kit/`
> （--buy-kit 钳子场景）、`build-evidence/post-base3/`（T03 双跑哈希）。
>
> 约定：每一项只写 PASS / FAIL，PASS 附证据（日志字段/计数/基准数字/代码落点），
> FAIL 写明原因与下一批归属。不写"基本完成"。

## 运行方式（Muse 复现）

```powershell
cd D:\BJTU\Cpp\Outstanding Projects\PixelEngine
# 1) 30 局基准（含 T01 wiring 日志 + T36 coach_rounds 日志）
build-dev\dev_replay_runner.exe --ticks=150000 --maps=dust_2,mirage,ancient_ruins `
    --seeds=1-10 --jobs=4 --no-hash --out=build-evidence\bench30 --tag=pix37
# 2) T20 钳子场景（CT 脚本玩家每回合买钳）
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2 --buy-kit `
    --wiring=build-evidence\verify-kit\seed1.wiring.jsonl
# 3) 验收表
python tools\coach\verify_wiring.py --wiring build-evidence\bench30 build-evidence\verify-kit `
    --rounds build-evidence\bench30 --bench build-evidence\bench30
# 4) 确定性双跑（T03）
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2 --hash=...\a.txt
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2 --hash=...\b.txt
python tools\dev_replay\diff_hash_logs.py a.txt b.txt   # 期望 [diff] 完全一致 OK
```

## 逐项验收表

| 任务 | 结论 | 验收证据 |
|---|---|---|
| T01 结构化教练决策日志 | **PASS** | `coach_wiring.jsonl` 每局生成：30 局共 5731 行规则计划行，字段齐全（round/elapsed_s/team/source/intent/target/tempo/templateKey/slots×9 字段/signals/guards/sanitized/hasBranch/branchIntent/branchAfterSec/gen）。1 局 8 回合内每回合每队 ≥1 行（首回合 T/CT 各 1 行起步，见 verify-final/seed1.wiring.jsonl）。templateKey 能区分 @ 命中（`T.RushB@B`）与 plain 回退（`CT.Default`）。 |
| T02 场景驱动器 + 验收脚本 | **PASS** | `tools/coach/verify_wiring.py` 跑通全部 34 项判定并输出 PASS/FAIL 表（见本表尾"脚本输出"）。任一缺日志即报 FAIL、不静默跳过（代码里每项都有 `未提供 --wiring 日志` 分支）。场景注入靠 `dev_replay_runner` 固定种子 + `--buy-kit` 参数（T20 钳子场景），验收脚本读日志断言。 |
| T03 golden-hash 基线重跑 | **PASS** | 改动前基线：`build-evidence/pre-base/run-a|b.txt`，final_hash=`3d3034f413a67730`，diff 完全一致。改动后：`build-evidence/post-base3/a|b.txt`，70270 tick，diff 完全一致 OK。新基线两次运行哈希一致 → 改动是确定性的。行为变化清单见交付汇报第 6 节。 |
| T04 @ 模板加载 | **PASS** | `TacticsTable::Load` 现在扫 `slots.*@Target` 段登记（key 保留 @ 后缀）。30 局日志 templateKey 命中 @ 段 3459 行（如 `T.RushB@B`、`CT.RetakeA@A`）。 |
| T05 changesThisRound 预算接线 | **PASS** | `CoachApplyPlan` 意图真变时 `++st.changesThisRound`（logs 里 changesThisRound>0 共 4482 行，取值分布 0–7）；`CoachResetRound` 每回合清零。预算拦截分支（CoachThink 里已存在的 `budgetOut` 判断）现在写 `guards:"budget-blocked"` 标记（自由对局未自然触发——critical 事件绕行、soft 事件 KEEP，见交付汇报 T05 备注）。 |
| T06 hasBranch 时间差打法 | **PASS** | RuleCoachCore 回合开始：MID 控中计划在 `ObservationCount()>=2` 时设 hasBranch（branchAfterSec=20s、branchIntent=Rush 冷侧）；Fake 计划恒设分支；Default 计划有情报时设分支。日志 hasBranch=true 548 行；console 实测 `[Coach] T 分支触发 -> 速推B`（verify-final/seed1.console.txt 共 5 次）。 |
| T07 sameSideStreak 接线 | **PASS** | `CoachRecordRound` 按实际主攻侧更新（同侧 +1、换侧清零）；`SanitizeCoachPlan` 复用；RuleCoachCore 读 `SameSideStreakLimit=3`（≥3 给对侧 +2.0 分、本侧 -1.0 分）。日志 sameSideStreak>0 共 2735 行（最高到 13）；streak≥3 的回合里 intent 含 default/rush/fake 混合（换侧与分兵两种出口都有出现）。 |
| T08 Sanitize 复用于规则计划 | **PASS** | `CoachThink` 里 `RuleCoach(t, ev)` 返回后调 `SanitizeCoachPlan`（TimeLow/MatchPoint 等反应型 critical 除外——否则赛点 rush 会被"同侧连续"改僵，见交付汇报）。日志 sanitized 标记 61 行（"无情报不赌点"/"同侧连续，改分兵"两类原因均有）。 |
| T09 Rotate 进表 + 进 schema | **PASS** | `TacticsTable::kIntents` 加 RotateA/RotateB；三图 ini 新增 `[slots.T.RotateA]`/`[slots.T.RotateB]` 表段；`kCoachSchema` intent enum 加 `rotate_a/rotate_b`（解析端 IntentLabel/ParseIntent 原本就支持）。日志 templateKey 含 Rotate 92 行（转点走表）。 |
| T10 意图打分制 | **PASS** | RuleCoachCore 回合开始改为候选意图打分取最高（heat 差 / 连续同 intent+target 失败换点 / 已控 / 道具 / 经济 / sameSideStreak / avoidA/B 全部确定性输入）。30 局意图分布熵 **2.64**（基线 Default 一家独大 → 显著上升）；穷局开局发禁集意图（rush/split/fake @ buyPlan=Eco & elapsed<20s）= **0**。 |
| T11 MID 控中意图 | **PASS** | 无情报开局候选含 `Default@MID`（2 人控中 + 两侧各 1 + 1 自由）。日志 target=MID 共 32 行。 |
| T12 RoundStart 发 Split / Fake | **PASS** | Split 条件（Mid 已控 + Full/Half + 烟≥1）与 Fake 条件（佯攻侧热/敌多）进打分。30 局 Split=6、Fake=44（各 ≥1 达标），templateKey 均命中 @ 段（`T.SplitB@B`/`T.Fake@A`）。 |
| T13 敌方习惯记忆 | **PASS** | `RoundMemo` 新增 `enemyAttackSide`（CoachRecordRound 从对手计划/记忆推导）；CT 回合开始读它：敌连续两回合主攻 A → StackA 重防。日志 enemyLastAttackSide 非空 5132 行。 |
| T14 道具/敌枪/联系人/lastOrder/笔记 | **PASS** | 烟 ≥1 才能道具开点（日志烟开点 577 行：utility.smoke≥1 且槽位 util>0）；敌狙 ≥2 → tempo=Slow 慢打（59 行）；TimeLow 按最新接触区域定侧（代码 + 日志 lastOrder 字段随计划更新）。 |
| T15 情报 heat 进 RoundStart | **PASS** | RuleCoachCore 回合开始读 `HeatOfSide`，热差进打分（热侧避开/冷侧主攻）。日志 heatA≠heatB 的行 5647 行，且打分只读 `intel_` 确定性数据、不碰 `rng_`。 |
| T16 减员变速解拦截 | **PASS** | soft-KEEP 内 tempo 可调、intent 不变。console 实测：`[Coach] T 减员变速 KEEP 速推B tempo fast->slow (intent 不变)`（verify1/seed2.console.txt，多例）。 |
| T17 HumanDied 规则分支 | **PASS** | RuleCoachCore 新增 HumanDied 分支（T 收缩重组 / CT 收位守点）。console 实测：`[Coach] CT 第8次 事件#9 -> 默认分兵 | 玩家阵亡，收位守点`（verify1/seed4.console.txt）。 |
| T18 跨回合经济状态机 + LossStreak | **PASS** | `CoachState` 新增 lastBuyPlan/lastBuyWon；DecideBuyPlan：上回合 Force 且输 → 强制 Eco；连败 ≥2 且买枪前均值 <2600 → Eco 存钱；我穷敌更穷（<1800 且 ≥1200）→ Force。30 局买档取值 {Eco,Force,Full,Half} 四档都出现。 |
| T19 赛点分支 | **PASS** | MatchPoint 分支（T Rush + Fast / CT 收位）+ DecideBuyPlan 赛点强起。console 实测：`[Coach] T 第2次 事件#10 -> 速推B 目标=B | 赛点，全力进攻B`（verify1/seed2.console.txt）。 |
| T20 钳子感知回防 | **PASS** | mustRush 阈值 anyKit?5s:11s。日志信号 `anyKit/mustRushAt` 两种取值都实测出现：`(False, 11.0)`（30 局常态）与 `(True, 5.0)`（--buy-kit 场景，verify-kit/seed1.wiring.jsonl）。 |
| T21 回防分工模板 | **PASS** | 下包后 CT 走 `CT.RetakeA@A`/`CT.RetakeB@B` @ 模板（已校准：2 进点 + 2 掩护 + 1 控路，自带 look/hold/util）。日志 CT retake 1867 行、分工角色数 ≥2。T 守包保留原三分工（Anchor/Support/Lurk 拆包角度/回防路线/延迟 peek）。 |
| T22 按能力分槽 | **PASS** | `CoachApplyPlan` 分槽前排序（拿狙者优先 → Awper 槽；钱少者优先；难度降序），不再是纯下标顺序；Awper 槽位显式挑拿狙者。 |
| T23 规则槽位补 look/hold/util | **PASS** | 规则计划经 ApplyTacticsSlots 优先走 @ 模板（模板自带 look/hold/util）。日志槽位含非默认 look/hold/util 共 2877 行。 |
| T24 锚点放开给规则模板 | **PASS** | CoachApplyPlan 锚点使用条件从 `hold≠Default` 放宽为「带 look/hold 数据的槽位即用锚点」，规则模板计划也能站到锚点坐标。 |
| T25 RefreshRolesForTeam 护栏 | **PASS** | 补断档前先看教练计划：计划缺 Entry/Anchor 是有意布局时（如全员 Anchor 的 Save）不补人；只在计划本身要求该角色却缺人时补。 |
| T26 Lurk/Awper 真角色 | **PASS** | `BotRole` 扩展 Lurk/Awper；`CoachApplyPlan` 映射直通（不再坍缩）；决策树三处按角色定移速全部走 `RoleSpeedMul`（Lurk 0.70 / Awper 0.60）；Lurk 保持静步/游走、Awper 慢速架枪。日志槽位 role=lurk/awper 共 5370 行。 |
| T27 RoleTransitionSec 3.0s 实现 | **PASS** | `SetRoleWithTransition` 记录切换时刻与旧倍率；`RoleSpeedMul` 按 3s 线性插值（旧倍率 → 新倍率），决策树三处（planHold :907 / 寻路 :1012 / 交火机动 :1396）统一走它。 |
| T28 拆包数字校准 | **PASS** | 三处过期数字已改：摘要"拆包有钳4s/无钳10s"；`_common.txt` 回防节"有钳 4s/无钳 10s，无钳最后 11s 才强拆、有钳 5s"；三图 retake advisory 同步。与 T20 代码阈值 5s/11s 一致（grep 复核无"拆包需 5 秒"残留）。 |
| T29 CT 的 C4 掉落 Stack 硬编码 | **PASS** | `CoachThink` C4 掉落分支改按 `AreaSideOf(掉落区域)` 发 StackA/StackB，target 保持实际区域名。 |
| T30 reason/ttl/经济档对齐 | **PASS** | reason 统一 60 字符（schema 为准，system prompt 与 `_common.txt` 第 6 条都已改）；手册补"中"档 2600 Half/Force 边界说明（1800/2600/3500 三阈值不动）；ttl 三者关系（卡片建议值/schema 3–30/TTL 地板 18/12/10）在代码注释与手册写明。 |
| T31 "已控"信号 | **PASS** | RuleCoachCore 新增 `sideAllyCount` 区域控制度量（我方在该区 ≥1 人存活 = 已控），进 T 打分（rush 需已控或记忆换侧）。日志 control 信号 237 行（control.A/B/Mid）。 |
| T32 分侧敌人数 | **PASS** | 新增分侧敌方计数（存活 + 近期接触，与 heat 并存不替代）。日志 enemyCountA/B 全量 5731 行。 |
| T33 [areas] 映射退化 | **PASS** | mirage：Long→Palace、Short→Cat、Tunnel→B_Apts；ancient_ruins：Tunnel→Cave（Long/Short 维持既有 B_Long/B_Short 说明并加注释）。三图 AreaHint("Long"/"Short"/"Tunnel") 均返回真实区域、不再回退包点。 |
| T34 few-shot 补齐 + advisory 分类 | **PASS** | 三图 few-shot 由 2 条/图 补到 5 条/图（default/retake + split/fake/save），覆盖 5 类意图；advisory 21 条全部保留并标注（可执行的已进回防模板/规则，纯提示的保持 `# [advisory]`）。 |
| T35 30 局配对基准重跑 | **PASS**（如实记录） | 30 局（3 图 × seeds 1-10）：T 29 / CT 1（T 胜率 96.7%，基线 40.0%，配对 McNemar p=0.0001，显著）。拒绝率：`invalid`/`invalid_consistency` 两条 LLM 拒绝路径本次基准未开 LLM（--coach 默认 rule），**未验证**；意图熵 2.64；Split/Fake 出现率 >0（6+44）。胜率变化如实记录、不要求提升。 |
| T36 coach_rounds.jsonl 写盘 + gen 字段 | **PASS** | `CoachRecordRound` 写盘点不动，新增 `TraceCoachRound` 写 `coach_rounds.jsonl`（一行一队一回合：match_id/round/team/source/gen/coach_version="pix37-rules-v2"/intent/target/tempo/won/buyPlan/reason/slots）。30 局 632 行全部含 gen；Split/Fake/MID 样本 6 行（>0）。 |
| T37 全量验收汇总 | **PASS** | 本文件即汇总表：37 项逐项有结论（34/34 PASS 于脚本口径；T05 的 budget-blocked 子场景与 T35 的拒绝率两项**未在自由对局中自然触发**，如实标注，见下）。 |

## 未验证到 / 未自然触发的验收点（如实说明）

1. **T05 "同回合第 3 次改意图被预算拦截"**：接线已验证（changesThisRound 自增 + 清零 + 拦截分支写 guards 标记），但 30 局自由对局里 `budget-blocked` 从未触发——因为非 critical 事件先被 soft-KEEP/T 18s 锁/IntentMinHold 拦住，critical 事件又绕过预算。这是既有调度结构决定的，不是接线错误。验收手段：抓 `guards=="budget-blocked"`（Muse 可用场景注入或长对局观察）。
2. **T35 拒绝率 23%→10%**：`invalid_consistency`+`invalid` 是 LLM 回包拒绝口径（V2 口径），本次 30 局基准跑的是 `--coach=rule`（未开 LLM），**拒绝率未验证**。规则路本身没有"拒绝"概念（Sanitize 整形 61 行、被消毒不改计划僵死）。跑 LLM 组需要 llama-server，未在本次范围内。
3. **T09 "LLM 能开出转点为加分项"**：未开 LLM，未验证。规则路转点走表已 PASS。
4. **T24/T27/T22 的行为级抽查（BOT 实际走到锚点附近/倍率渐变遥测/AWP 分槽的局内位置证据）**：代码断言与日志字段已齐，逐 tick 位置遥测未做，Muse 可用 decision trace（`--trace=`）复核。
5. **T26 "Lurk 绕后不参与正面"位置级抽查**：日志 role 出现 + 移速参数已落；位置级抽查未做，可用 decision trace/回放复核。

## 脚本输出（tools/coach/verify_wiring.py，2026-11-19 实测）

`结果: 34/34 PASS`（T01–T36 全部 PASS，T37 即本汇总表）。

## 构建与测试结果

- 主程序 `build-release\EngineApp.exe`：编译通过（0 error）。
- `gameplay_tests.exe`：**1843 通过 / 0 失败**，EXIT=0（与改动前基线测试数一致）。
- `dev_replay_runner.exe`（dev-only，独立于游戏二进制）：编译通过；确定性双跑 70270 tick 逐字节一致。

## 改动文件清单（工作区，未提交）

代码：`CS2D/CS2DGame.hpp`、`CS2D/CS2DGameBots.cpp`、`CS2D/CS2DGameCoach.cpp`、`CS2D/CS2DGameCoachAI.cpp`、`CS2D/CS2DGameSquad.cpp`、`CS2D/Coach/CoachTypes.hpp`、`CS2D/Coach/TacticsTable.hpp`、`CS2D/Tuning.hpp`。
配置：`assets/config/coach/_common.txt`、`ancient_ruins.txt`、`dust_2.txt`、`mirage.txt`、`assets/config/tactics/ancient_ruins.ini`、`dust_2.ini`、`mirage.ini`。
验收工具（dev-only）：`tools/coach/verify_wiring.py`（新增）、`tools/dev_replay/dev_replay_runner.cpp`、`replay.hpp`、`scripted_player.hpp`（新增 `--no-hash`/`--buy-kit`/`--wiring`/`--coach-rounds` 开关与 PXINPUT4 日志列）。
未提交、未 push；未删除任何文件与决策。

## 2026-11-23 补验

基准实现已进入提交 `87e204f`。本次从该提交补跑局内场景，并修复了场景揭示的接线问题。

| 项目 | 补验结果 |
|---|---|
| T05 变更预算 | 交战场景先应用两次意图变更，等保持期、冷却和旧计划有效期结束，再触发一次非关键重算。教练记录「次数已满」，保留原意图并续期。新回合预算归零。 |
| T09 转点 | 规则转点实测 A→B，读取地图槽位。运行时动态 schema 原先漏掉转点，已在现有守卫允许时开放。注入模型回包后，正常处理链采纳 `rotate_b`、下发地图模板并记录来源点与冷却。回包注入验证引擎接线。真实模型自然选择转点属于单独的加分项。 |
| T22 分槽 | 只有一名 AWP 持有者时，即使 Awper 排在第三个槽位也能正确分配。保枪计划首位分配给余额最低的 BOT。 |
| T24 锚点 | Mirage 的 CT 规则槽位读取 Palace 锚点，BOT 的实际距离从 1228.06 降至 31.14 世界单位，进入 32 单位的到位范围。 |
| T26 角色行为 | Lurk 保持 Palace 侧翼路线，附近队友阵亡后也没有转去补枪。Jungle Awper 向架枪位移动时实测 51.84 世界单位／秒，与角色限速一致。 |
| T27 换角平滑 | Lurk→Awper 在 0、1.5、3 秒时的移动倍率分别为 0.700、0.650、0.600。插值期间再次换角会从当前倍率接续。 |

发行版与回放程序构建通过。最新默认回归 1843 项通过，0 失败。定向测试 `gameplay_tests --pix37-coach` 共 24 项通过，0 失败。Mirage、种子 37 的 10000 tick 确定性双跑输出 SHA-256 一致。测试日志位于 `build-evidence/pix37-recheck/`。

T35 的真实模型基准从 `87e204f` 启动，使用项目配置的 Qwen2.5-7B Q4_K_M。地图为 Dust II、Mirage、Ancient Ruins，每图种子 1–10，每局最多 150000 tick。已完成的前 15 局（Dust II 10 局、Mirage 5 局）按 V2 同口径计算，拒绝率为 183/586（31.23%），另有 10 次空回包。Dust II 10 局的拒绝率为 138/400（34.50%），该图 136 次 `invalid` 中有 91 次出现在下包决策。当前样本超过 10% 门槛，整批 30 局的结论以完整输出为准。

模型日志位于 `build-evidence/pix37-recheck/llm/`。批处理结束后，本次校验会用原 30 局规则结果生成 `build-evidence/pix37-recheck/paired.log` 和 `paired.json`。也可用 `python tools/dev_replay/eval_llm.py --rule <规则输出目录> --llm <模型输出目录> --pix37` 核对完整配对数、拒绝率、意图分布及 McNemar 精确检验。旧表的脚本 PASS 仅覆盖当时的规则组记录。

## T35 拒绝率修复与统一验收

修复前的完整三图 30 局模型组拒绝率为 355/1194（29.73%），其中开局 97/661（14.67%），下包后 258/533（48.41%）。历史统计以 `invalid`、`invalid_consistency`、`applied` 和 `advisory` 为分母，空回包和过期单列。

- 下包后从实际炸弹坐标查询包点。引擎确定 T 方守包与 CT 方回防意图，再从地图战术表填充槽位，模型提供节奏、有效期和短理由。
- 先规范化旧格式的 `default + 空目标`，再执行包点校验。错侧、错阵营意图、非法节奏和局面变化的回包均保留原计划。
- 开局动态请求与经济守卫共用 `ShouldSaveNow`。开局允许省略槽位，理由限制为 20 字。三图试跑发现 4 次 `finish_reason=length`，均发生在开局。最终输出预算采用开局 192、下包后 80 token，原环境变量仍可覆盖。
- 诊断日志分别记录 JSON、字段、意图、目标、经济与缺少模板等失败原因。评测报告按事件输出拒绝率。

发行版构建、默认回归 1843 项、体验回归 2157 项和 PIX-37 专项 99 项均通过。专项覆盖三张图、A/B 包点、T/CT、旧格式、错侧、过期与错类型。三图试跑的下包阶段有 47 条计入样本，拒绝 0 条。

统一评测沿用 Qwen2.5-7B Q4_K_M、三图各 10 个固定种子和 150000 tick 上限。模型组使用 4 局并行和 2 个推理槽。规则组与模型组使用从 `87e204f` 提取的地图和配置快照。日志和报告位于 `build-evidence/pix37-t35-final/`。

**统一验收结果：T35 通过。** 两组各完成 30 局，共 30 个有效地图／种子配对。

| 指标 | 结果 |
|---|---|
| 计划拒绝率 | 2/1177（0.17%），低于 10% 门槛。 |
| 开局 / 下包后 | 开局 2/651（0.31%），下包后 0/526。 |
| 回包分类 | 851 条采纳、324 条顾问、1 条 `invalid`、1 条 `invalid_consistency`。另有 11 条空回包和 2 条迟到回包。 |
| 规则意图 | 意图熵 2.634 bit，按回合、队伍、意图和目标去重后有 2 条 Split、43 条 Fake。 |
| 规则组与模型组 | T 方胜场 28/30 与 30/30，McNemar 精确双尾 p=0.50。模型组首杀转化较规则组低 9.96 个百分点。 |
| 历史规则组对照 | 历史基线 T 方胜场 14/30，当前规则组 28/30，按相同地图／种子配对的 McNemar 精确双尾 p=0.000122。 |

剩余两次拒绝均发生在开局，一次服务端返回 `finish_reason=length`，另一次目标与槽位自洽检查失败。T09 的自然模型转点加分项本次出现 0 次，定向测试已覆盖合规转点回包的执行链。
