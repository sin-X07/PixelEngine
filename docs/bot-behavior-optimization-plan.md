# PixelEngine BOT 行为优化计划

- 审计时间：2026-11-23 晚
- 审计对象：main @ `bd34ce4`「PIX-37」（Sun 本人 2026-11-23 21:07 提交，含之前 11 个未提交改动；审计期间工作区无其他改动）
- 审计方式：只审计、不改游戏代码、不提交、不 push、不切分支
- 文档性质：优化计划（提案），不是实施记录；每项的改法建议需 Sun 定范围后再实施
- 实施决定（Sun 2026-11-23 22:46）：本计划发 Linear（PIX-40）；由 Muse 操刀完成全部 14 项修改，按建议顺序分批实施、分批复测，最后统一验收。retake lurk 已定：改成架枪手（Sun 2026-11-23 22:51）；audit-bench30 入库仍待 Sun 定，基线文件不动。

## 结论先给

**T 的优势是系统性的，胜负手只有一条——下包。** 34 局 T 赢 32 局（94.1%）；364 回合 T 赢 74.2%。85.2% 的回合有下包，一旦下包 T 赢 85.8%，CT 回防成功仅 14.2%；没下包的回合 CT 赢 92%。难度 0–7 全档位 T 都赢。

**最优先修的三件事（P0），全是明显 bug：**

1. **T 下包后不撤出爆炸半径**——T 赢的下包回合 48.1% 以全队被自己的炸弹炸死收场。守包环半径 170px 小于爆炸半径 320px，且全仓没有任何“引信将尽撤离”的逻辑。
2. **炸弹炸死的队友算到下包者人头上**——单回合 `kills_t` 最高 9（5v5 回合不可能靠枪法打出），记分板失真。
3. **CT 回防单人拆、无掩护、常被打断**——40.6% 的下包回合 CT 全程没人开始拆包；开始拆的 184 次里 77.2% 被打断；拆包开始瞬间包点 220px 内 CT 平均不到 1 人。

其余问题集中在教练意图执行层：Split 30 局仅 2 次、Fake 1.1%、rotate 只有模板没有意图、Default 计划看向断线、燃烧弹从未被分配、情报点投掷 75% 落点校验失败——PIX-37 的 37 项接线本身没断（verify_wiring 34/34 PASS），但几条高价值战术意图在实战里几乎凑不齐触发条件。

**本计划共 14 项：P0 3 项、P1 6 项、P2 5 项。**

---

## 一、审计基线与方法

### 构建与测试基线（Sun 的电脑，2026-11-23 晚）

| 项目 | 结果 |
|---|---|
| 主程序编译 | 通过，0 错误 0 警告（注：任务简报里说的 `D:\build.ps1` / `D:\build_tests.ps1` 在他电脑上不存在，实际用 `cmake --build` 编，和 DeepSeek 当时的做法一样） |
| gameplay_tests | **1750 通过 / 6 失败**，exit=1，两次运行数字一致 |
| 6 条失败的原因 | 全是无显示环境的 SDL 渲染器初始化失败（`SDL_CreateRenderer` 在 SSH 会话 Services 会话 0 里返回 null），**不是游戏 bug**；与 11 月 19 日的 1843/0 相比，少的 93 项全部能对应到渲染器依赖的用例，无一是玩法逻辑回归。`--experience` 模式未跑（需有显示器和 GPU） |
| 确定性复现 | 同局同种子两次带哈希运行，`final_hash` 完全一致（`385f13d63a6939cd`），OK |

### 实际打的局（无头对局，脚本玩家固定 CT）

| 批次 | 局数 | 结果 |
|---|---|---|
| 基线复用 30 局（dust_2/mirage/ancient_ruins × seeds 1–10） | 30 局 / 324 回合 | T 28/30（93.3%） |
| 新跑 Batch A（dust_2 s11/s12、mirage s11/s12） | 4 局 / 40 回合 | T 4/4（8-1、8-1、8-4、8-2） |
| 难度扫测（dust_2 s11，档位 0/3/7） | 3 局 | 档位 0：T 8-5；档位 3：T 8-2；档位 7：T 8-1 |
| trace + conflicts（dust_2 s11、mirage s11，与 Batch A 同种子，哈希一致） | 2 局（复现） | 冲突只有 duplicate_hold |
| **合计** | **34 局 / 364 回合** | **T 赢 32 局（94.1%），回合 T 赢 74.2%** |

- 对局办法：`build-dev\dev_replay_runner.exe`（无头完整对局，脚本玩家 5v5）；单局约 2.5–4.5 分钟，30 局 jobs=4 约 22 分钟。游戏本体 EngineApp 在 SSH 会话里跑不起来（窗口创建失败，退出码是 0 但日志里有失败），**真人键鼠试玩本次未做**，需带显示器的会话 + `tools/win_accept.ps1`。
- 证据位置（Sun 电脑仓库内）：`build-evidence\audit-bench30\`（30 局）、`build-evidence\audit-single\`（2 局全量）、`build-evidence\audit-playtest\`（新跑 9 局）；均已被 `.gitignore` 覆盖，不污染工作区。
- 难度扫测说明：`--bot-profile` 同时作用双方全部 9 名 BOT，不是单纯的 CT 难度旋钮；曲线方向合理（档位越高回合越短、方向反转越少），但**最弱档 T 照样赢**。

### 指标口径纠正（读数之前必须先看，之前基线对比里有误读）

| 指标 | 真实含义 | 误读 |
|---|---|---|
| `defuses` | 拆包**尝试**次数（进度 >0 即记 1） | 不是拆包成功次数；真正拆包成功 ≈ CT 赢的下包回合数（30 局 40 次，成功率 23%） |
| `kills_t` / `kills_ct` | 含炸弹爆炸击杀（含队友击杀） | 不能当枪法指标用；单回合 kills_t 最高 9 就是这么来的 |
| `damage_t` / `damage_ct` | 对方承受的全部血量伤害，含炸弹/手雷/火区伤害 | CT 回合伤害更高（466 vs 379）但击杀更少，是因为 T 大量死于自己的炸弹（这部分伤害计入了 damage_ct） |
| `buy_t` / `buy_ct` | BuyPlan 枚举档位（Eco 0 / Half 1 / Force 2 / Full 3 / AntiEco 4，共 5 档） | 不是购买人数 |
| `nade_skips` | 只是情报点投掷落点校验失败的次数 | 不是“有机会没扔”；计划内 util 投掷失败不计数，真实“想扔没扔成”比 4079 更多 |
| `still` | 移动 <0.15 且无交战目标 | 下包后架枪全程计入；`idle`（无目标/无驻守点/无路径）其实很低（T 1.3%、CT 0.5%）——BOT 不是“不知道干什么”，而是“到了点就不动” |
| `money` | 队伍平均余额，**含脚本玩家** | 脚本玩家（CT）很少阵亡、很少买枪，持续囤钱，拉高了 CT 均值 |

---

## 二、P0——明显 bug / 破坏体验（3 项）

### P0-1 T 下包后不撤出 320px 爆炸半径，全队被自己的炸弹炸死

**现象与证据：**
- T 赢的下包回合 48.1% 结束时 T 全员已死（34 局合计 128/266）；dust_2-s11 R1/R2/R3/R5/R6 连续 5 回合 T 全员死于爆炸后赢。
- 下包后 T 的守包槽位全压在包点区域（stack 槽位 B_Site/A_Site 占绝对多数），等于计划本身就把人留在爆炸半径内。
- 证据：`build-evidence\audit-playtest\dust_2-s11.rounds.csv`（`alive_t_end=0` 的回合）、`build-evidence\audit-bench30\*.rounds.csv`。

**影响：** 观感极差——赢了回合但全队被自己的炸弹炸死；还污染击杀统计（见 P0-2）；T 守包本应是“活着守”，现在是“同归于尽式”胜利。

**代码位置（live 树实测，HEAD bd34ce4）：**
- `PixelEngine/CS2D/CS2DGameObjective.cpp:268-295`（`DetonateBomb`；半径 `RADIUS = 320.0f` 在 276 行；伤害 200×falloff、无视护甲在 284 行）
- `PixelEngine/CS2D/CS2DGameBots.cpp:1081-1089`（下包后 T 守包环形站位，`Tuning::BombHoldRing`）
- `PixelEngine/CS2D/Tuning.hpp:129`（`BombHoldRing = 170.0f`）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:592-609`（下包后 T 计划：Stack/Default，槽位全部压在包点区域）、`:641-653`（`BombPlanted` 事件分支：T 全员 `Anchor@siteArea`）

**机制解释：** 下包后 T 的两条行为链都把人钉在爆炸半径内。教练层：`RuleCoachCore` 下包分支给 T 发 Stack 计划，4 个槽位里 3 个直接是包点区域，只有 1 个 Lurk 在远端路线、1 个 Support 在中路；`BombPlanted` 事件分支更彻底，全员 `Anchor@siteArea`。执行层：BOT 按 8 方位角排在以炸弹为圆心、半径 170px 的环上——170 < 320，**环上任何一点都在爆炸半径内**，无一例外。全仓 grep `bombTimer_` 证实：引信只出现在拆包资格判定（`CS2DGameBots.cpp:196/199/219`，CT 的 6 秒强拆窗口）和倒计时扣减（`CS2DGameObjective.cpp:215-216`）里，**没有任何代码根据 `bombTimer_` 让 T 移动、撤离或换点**。所以 T 不是“忘了撤”，而是计划层 + 执行层都没有撤离这个行为。

**改法建议：**
1. 守包环半径改到爆炸半径外：`BombHoldRing` 从 170px 提高到 320px 以上（建议 340–380px，留出余量），或按包点形状改用“包点外围架枪点”代替环形站位。
2. 加引信撤离逻辑：`bombTimer_` 小于阈值（例如 8–10 秒）时，未在架枪位的 T BOT 撤到爆炸半径外；架枪位可留到更晚再撤（例如 3–5 秒），但必须撤。
3. 教练层同步：下包后 Stack 计划的槽位区域改到包点外围（`siteArea` 外围 / 入口通道），不要把 3 个槽位直接压在包点上；`BombPlanted` 分支的 `Anchor@siteArea` 同样改到外围。
4. 注意：撤离不能变成“全员跑开把包点让给 CT”——撤出半径后仍需保持对包点的视线/架枪，撤的是位置不是防守。

**验收标准：**
- 复跑同种子 30 局基准（`audit-bench30` 同图同种子）：T 赢的下包回合里 `alive_t_end=0` 的占比从 48.1% 降到 5% 以下；理想为 0。
- 抽查单局 trace：下包后 T BOT 的站位点到炸弹的距离全部 >320px（引信最后 5 秒内仍在架枪的除外，但爆炸瞬间必须在半径外）。
- 不改动胜负结构本身：T 胜率不应因此项单独大幅波动（本项修的是“怎么赢”，不是“谁赢”）。

---

### P0-2 炸弹炸死的队友计入下包者 kills，记分板失真

**现象与证据：**
- 单回合 `kills_t` 最高 9（dust_2-s11 R3/R5，5v5 回合不可能靠枪法打出）；30 局里 190/324 回合 `kills_t` 超过 CT 实际死亡数（+1~+4）。
- 证据：`build-evidence\audit-playtest\dust_2-s11.rounds.csv`（R3/R5 的 kills_t=9）、`build-evidence\audit-bench30\*.rounds.csv`。

**影响：** 记分板/战绩失真；下包者“刷人头”；`kills_t`/`kills_ct` 不能作为枪法证据（本次审计的伤害/击杀解读已据此修正）。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/CS2DGameObjective.cpp:275-289`（爆炸伤害“不分阵营”注释在 275 行；`OnCombatantKilled(p, audio, DamageOwner(bombPlanter_), WeaponId::C4, …)` 在 285 行）
- `PixelEngine/CS2D/CS2DGameCombat.cpp:977-1058`（`OnCombatantKilled`；`++killer->kills` 在 1050-1051 行）
- `PixelEngine/CS2D/CS2DGameCombat.cpp:1054-1057`（击杀奖金：`IsHostile(victim, *killer)` 才发钱）
- 同款入口：`PixelEngine/CS2D/CS2DGameCombat.cpp:688、831`（手雷/燃烧弹伤害同样以 `DamageOwner(...)` 为 killer 调 `OnCombatantKilled`）

**机制解释：** `DetonateBomb` 对玩家 + 全部 BOT 逐个算距离伤害，死者不分敌我，统一以下包者为 killer 传进 `OnCombatantKilled`。该函数 1050 行的判定只有 `if (killer && killer != &victim)`——只排除自杀/环境伤害，**不查敌我**，于是下包者炸死自己队友也 `++killer->kills`。5v5 回合里 T 最多炸死 4 名 CT + 4 名队友 = 8，加上枪杀即可冲到 9，与试玩数据吻合。注意 1054 行发击杀奖金时是查了 `IsHostile` 的，说明“奖金不给队友击杀、计数却给”是同一函数内两行标准不一，不是全局缺敌我判定。手雷/燃烧弹走同一入口，同样会把炸死的队友算进投掷者 kills。

**改法建议：**
1. `OnCombatantKilled` 里 `++killer->kills` 加敌我判定：只有 `IsHostile(victim, *killer)` 时才计入 kills（与 1054 行奖金逻辑对齐）；队友击杀单独计数（例如 `teamKills`，用于后续可能的惩罚/统计，不删除数据）。
2. 爆炸击杀本身可保留“击杀者为下包者”的归属（用于助攻/伤害统计），但 kills 计数必须分敌我。
3. 同款入口一并修：手雷/燃烧弹（Combat.cpp:688、831）的队友击杀同样不计 kills。
4. 注意：不要改 `DamageOwner` 的伤害归属本身（伤害统计、助攻判定依赖它），只改 kills 计数的敌我判定。

**验收标准：**
- 复跑同种子 30 局基准：任意回合 `kills_t` 不超过该回合 CT 实际死亡数，`kills_ct` 不超过 T 实际死亡数；`kills_t=9` 的回合消失。
- 记分板抽查：下包者炸死队友的回合，其 kills 不增加，队友击杀单独可见（或至少不混入 kills）。
- gameplay_tests 全过（1750/6 基线，6 条渲染器失败除外）。

---

### P0-3 CT 回防：单人拆、无掩护、常被打断，40.6% 的下包回合无人拆包

**现象与证据：**
- 拆包开始瞬间包点 220px 内 CT 平均 0.87 人（T 下包瞬间平均 2.10 人护点）——T 下包有人护、CT 拆包没人架。
- 40.6% 的下包回合 CT 全程没人开始拆包（34 局合计 126/310）；开始拆的 184 次里 77.2% 被打断/失败；拆包成功率约 23%。
- CT 拿首杀也只赢 46.4%（T 拿首杀赢 88.6%），5v4 优势转化不出去。
- 证据：`build-evidence\audit-bench30\*.rounds.csv`（`defuse_near_ct`、`plant_near_t`、`defuse` 列）、`build-evidence\audit-playtest\*.rounds.csv`。

**影响：** 回防体系的核心短板，直接决定 74% 的回合胜负；是 T 93.3% 胜率的最大单一成因。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/CS2DGameBots.cpp:190-212`（`BotCanDefuse`：拆包资格判定）
- `PixelEngine/CS2D/CS2DGameBots.cpp:216-247`（`PickDefuseBot`：只指定**一名**拆包手）
- `PixelEngine/CS2D/CS2DGameBots.cpp:532-541`（拆包中：看见敌人或挨打即放弃，`defuseCooldown = 2.0f`）
- `PixelEngine/CS2D/CS2DGameBots.cpp:1090-1118`（下包后 CT 移动：拆包手去炸弹，其余人去情报点或 `DefuseCoverRing` 环）
- `PixelEngine/CS2D/Tuning.hpp:130`（`DefuseCoverRing = 180.0f`）；`PixelEngine/CS2D/Tuning.hpp:128`（`DefuseRadius = 30.0f`）
- `PixelEngine/CS2D/CS2DGame.hpp:789-795`（`IsInDefuseRange`：距 C4 ≤30px 且有视线）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:619-636`（CT 回防计划：RetakeA/B，`templateDefault = true`）
- `PixelEngine/CS2D/CS2DGameObjective.cpp:149-201`（拆包执行：多人可同时攒进度，但只有最快者算赢；`activeDefuser_` 取剩余时间最短者）
- `PixelEngine/CS2D/CS2DGameObjective.cpp:13-15,164`（`BOMB_FUSE = 40.0f`，`DEFUSE_TIME = 10.0f`，有钳 4 秒）

**机制解释（四层原因叠加）：**
1. **只定一名拆包手**：`PickDefuseBot` 按“没挨打、没冷却、看不见敌人”选最近的一名 CT，其余 CT 的目标是 `DefuseCoverRing = 180px` 的环或 520px 内的情报点——而 `DefuseRadius` 只有 30px，**架枪手站在 180px 环上时在几何上就不可能拆包**，拆包手自始至终只有一人。
2. **拆包资格极严**：`BotCanDefuse` 要求“看不见敌人且最近没挨打”，否则不拆；只有引信 ≤6 秒才进强制窗口（199 行）。
3. **拆包极易被打断**：拆包中的 BOT 一旦 `BotCanDefuse` 变 false（看见敌人或挨打），Bots.cpp:532-541 立即放弃并挂 2 秒冷却；T 方守包环（170px）+ 听拆包声扑人（Bots.cpp:624-658，`DefuseHearRadius = 600px`）使拆包手几乎必被打断。
4. **40.6% 无人拆包**：拆包手必须先走到 30px 内且有视线（`IsInDefuseRange`）；回防路线长 + `PickDefuseBot` 在非强制窗口会因“看得见敌人/在冷却”返回 -2（246 行），等于无人被派去拆。

**关于 retake lurk 分到 CT_Spawn（Sun 2026-11-23 22:51 已定：改成架枪手）：** `assets/config/tactics/dust_2.ini:489`（RetakeA：`4=Lurk|CT_Spawn|look=Cat|hold=passive`）与 `mirage.ini:455/461` 原文如此——**回防模板确实留一人在出生点被动断后，是有意设计，不是接线错误**。918 次是该模板被正常执行的结果。Sun 已定：改成架枪手，见改法建议第 6 条。

**改法建议：**
1. 回防队形改成“拆包手 + 架枪手”双人组：`PickDefuseBot` 仍定一名拆包手，但另指定一名最近的 CT 为掩护手，掩护手的目标改为拆包手附近的架枪点（能看到拆包手和主要入口），而不是 180px 环或情报点。
2. 拆包被打断后立即换人：拆包手放弃/被打断时，掩护手或最近的 CT 在冷却结束后接替拆包（而不是等 2 秒冷却 + 重新 `PickDefuseBot` 的严苛判定）；多人可同时攒进度（Objective.cpp:149-201 已支持多人同时攒），利用起来。
3. 放宽非强制窗口的拆包资格：引信 ≤15 秒时，允许“看得见敌人但敌人不在拆包手视线威胁方向”也拆；或拆包手由掩护手先清点再进。
4. 回防迟到问题：CT 回防路线与轮转速度另查（本次证据显示最后 1 名 CT 常到晚了）；可考虑回防计划提前触发（下包瞬间即全员回防，而不是等计划重发）。
5. 钳子：核查 BOT 买钳逻辑（`--buy-kit` 场景的 `audit-kit` 证据本次未分析）；无钳 10 秒 vs 40 秒引信，时间账本身很紧，钳子是次因，回防迟到是主因。
6. retake lurk 改成架枪手（Sun 2026-11-23 22:51 已定）：`dust_2.ini:489`、`mirage.ini:455/461` 的 Retake 模板里 `4=Lurk|CT_Spawn|hold=passive` 槽位改成架枪手，目标改为拆包手附近的架枪点（能看到拆包手和主要入口），不再留人在出生点被动断后；`ancient_ruins.ini` 同类槽位一并检查。

**验收标准：**
- 复跑同种子 30 局基准：下包回合 CT 回防成功率从 14.2% 提升到 30% 以上；无人拆包的下包回合占比从 40.6% 降到 20% 以下；拆包尝试失败率从 77.2% 降到 50% 以下；拆包开始瞬间包点 220px 内 CT 平均人数从 0.87 提升到 1.5 以上。
- 抽查单局 trace：下包回合里至少能看到“一人拆、一人架”的双人组队形出现。
- T 胜率应随回防改善而回落（方向性指标，与 P0-1、P1-1 联动，不单独设硬数字）。

## 三、P1——行为不合理、值得改（6 项）

### P1-1 首杀优势没有战术动作：CT 拿首杀也只赢 46.4%

**现象与证据：**
- CT 拿首杀只赢 65/140 = 46.4%（T 拿首杀赢 163/184 = 88.6%）；新 4 局 CT 首杀后输 12/18 = 66.7%。
- 证据：`build-evidence\audit-bench30\*.rounds.csv`（`first_blood_team` × `winner`）、`build-evidence\audit-playtest\*.rounds.csv`。

**影响：** 人数优势没有战术价值；CT 拿到 5v4 后仍输，要么守点被 T 强行突破，要么回防失败；与 P0-3 同源但机制不同。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/CS2DGameCoach.cpp:321-364`（`CoachThink`：`EnemyDown`/`AllyDown` 是 soft 事件；计划有效且未过期、未下包时直接 KEEP，只调 tempo）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:345-349`（`EnemyDown` → tempo Fast；`AllyDown` → tempo Slow；意图、目标、槽位一律不变）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:380-392`（T 执行锁定：目标侧确定后 18 秒内不重规划）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:684-697`（事件分支：`EnemyDown`/`AllyDown` 的规则计划也只是 Default 分兵 + 变速，无压点/控图/抱团指令——且该分支常被 342 行的 KEEP 逻辑拦截，根本走不到）
- `PixelEngine/CS2D/CS2DGameBots.cpp:1013-1014`（tempo 只影响 `TempoAggression` 与移动速度倍率，不改变目标选择）

**机制解释：** 人数优势在教练层**没有对应的战术动作**。首杀产生 `EnemyDown`/`AllyDown` 事件，但 `CoachThink` 把这两个事件定为 soft 事件：只要当前计划有效且未过期、且未下包，就直接 KEEP 原计划，仅把 tempo 改成 Fast（敌减员）或 Slow（我减员）；意图、目标侧、5 个槽位分工一个字都不改。`RuleCoachCore` 里 684-697 行倒是写了“对面减员，前压抓人”/“我方减员，收缩等机会”的分支，但那只是 Default 分兵换个 tempo，同样没有“5v4 抱团压点/控图/架枪”的新槽位——而且软事件下这段经常因 KEEP 拦截根本执行不到。T 侧还有 18 秒执行锁定（380-392 行），目标侧一旦确定，减员事件也拉不动它。执行层也一样：tempo 只进 `TempoAggression` 和移速倍率，不改目标选择。所以 CT 拿首杀后 BOT 的行为与 5v5 时完全相同：继续默认分兵，各打各的。

**改法建议：**
1. 给人数优势加战术动作：己方多 1 人时，教练发“抱团压点/控图”计划（例如 2 人前压控中、2 人架枪、1 人自由），而不是 KEEP 原计划；己方少 1 人时发“收缩/双人架枪”计划。
2. `EnemyDown`/`AllyDown` 从 soft 事件升级为“计划重评估触发器”：减员后立即重打分（不是只调 tempo），但保留 18 秒执行锁定的防抖（避免每死一人就全队换计划）。
3. 684-697 行的分支要真长出新槽位（压点/控图/抱团），而不是 Default 分兵换 tempo；否则改了等于没改。
4. 注意：不要改成“每死一人就换计划”的高频重规划——减员重评估应有冷却（例如距上次重规划 ≥5 秒）。

**验收标准：**
- 复跑同种子 30 局基准：CT 首杀后胜率从 46.4% 提升到 60% 以上；T 首杀后胜率保持 80% 以上（T 的优势来自下包链，不是人数利用，不应因此项下降）。
- 抽查 wiring：首杀后的 10 秒内，教练计划出现“压点/控图”类意图或槽位变化（而不是 KEEP 原计划只改 tempo）。

---

### P1-2 教练看向断在 Default 计划：lookAngle 全是 -99，MID 控中槽位被 plain Default 覆盖

**现象与证据：**
- 全部 26910 个 wiring 槽位的 `lookAngle` 都是 -99（未设置）；Default 计划 target 为 '-' 的 426/1014（42%）；target=MID（控中）仅 30 次（3%）。
- **纠正试玩报告的一处统计错误**：试玩组称 `lookArea` 全空是错的——实测 `build-evidence/audit-bench30/*.wiring.jsonl`（5382 行）里 2668 行 `lookArea` 非空，retake 的 1770 行全部带 `lookArea`。真正的断点在 Default 计划，不在全部意图。
- 证据：`build-evidence\audit-bench30\*.wiring.jsonl` 全量统计、`build-evidence\audit-bench30\*.plans.csv`。

**影响：** BOT 架枪方向在 Default 计划下只由底层 AI 兜底决定，教练的战术意图（看哪、架哪）传不到执行层；“默认控中换信息”的设计意图在执行层基本不存在，默认计划约等于“无目标分兵”。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/Coach/CoachTypes.hpp`（`SlotOrder`：`lookArea` 字符串、`lookAngle = -99.0f` 默认值、`util`、`utilArea`）
- `PixelEngine/CS2D/Coach/TacticsTable.hpp:205-257`（`ParseSlot`：`look=<Area>` → `lookArea`；`lookDeg=<度>` → `lookAngle`）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:1867-1881`（`CoachApplyPlan`：`so.lookAngle > -90` 则用之，否则 `so.lookArea` 非空时算区域中心方向角写入 `ai.lookAngle`）
- `PixelEngine/CS2D/CS2DGameBots.cpp:131-169`（`SetBotGoal`：先把 `ai.lookAngle` 置 -99，再查战术表 `[look.<area>]`，没有则朝敌方出生点质心——执行层有兜底朝向）
- `assets/config/tactics/dust_2.ini:15-59`（plain 模板：只有 `角色|区域`，**无任何 `look=`/`util=`**）vs `dust_2.ini:412-495`（`@` 模板：每槽都带 `look=`）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:516-529`（`ApplyTacticsSlots`：只在 `plan.templateDefault` 时用模板覆盖槽位；`@Target` 优先、plain 回退）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:960-977`（Default+MID 控中槽位构建，`templateDefault = true`）与 `:1006-1012`（其余 Default 分兵，`assign(...)`，1009 行把 `plan.target.clear()` 写死）

**机制解释（链条分三段）：**
1. **数据源头**：`lookDeg=` 在三张图的 ini 里出现 **0 次**，所以 `SlotOrder.lookAngle` 永远是默认值 -99——wiring 里 -99 全量属实，断在**数据层：没有任何模板写过 `lookDeg`**，不是代码没读（`ParseSlot` 240-241 行会读）。
2. **`lookArea` 本身是通的**：只有走 `@` 模板的意图（Rush/Split/Fake/Retake/Stack）才带 `lookArea` 并经 Coach.cpp:1869-1872 落到 `ai.lookAngle`；Default 计划的 plain 模板不带 `look=`，执行层只能靠 `SetBotGoal` 的兜底（查 `[look.<area>]` 表——三张图都有完整的 `[look.T./CT.]` 段——或朝敌方出生点质心）。
3. **MID 控中槽位被覆盖**：`RuleCoachCore` 的 MID 控中分支（960-967）明明建了“2 人控中 + 两侧各 1 人架点 + 1 人自由”的槽位且 `templateDefault = true`，但随后 `RuleCoach`（531-535 行）调 `ApplyTacticsSlots`，查 `Slots("T", "Default@MID")` 查无此模板 → 回退 plain `[slots.T.Default]` **把 5 个槽位整体替换**。实测 wiring 佐证：round 1、target=MID 的 T 计划行，槽位是 plain Default 分兵，不是 MID 控中槽位。于是“控中”只剩 target 字段一个空名头。

**改法建议：**
1. 数据层：给 plain Default 模板补 `look=`（每槽指定看向区域），或给 `Default@MID` 新建模板（`[slots.T.Default@MID]`，2 人控中 + 两侧架点 + 1 人自由，带 `look=`），让 `ApplyTacticsSlots` 查得到、不回退。
2. 代码层：`ApplyTacticsSlots` 回退时不要整体替换——没有 `Default@MID` 模板时，保留 `RuleCoachCore` 已建好的槽位，只补缺失字段；或把 MID 控中槽位的构建从 `templateDefault = true` 改成直接写入（不经过模板覆盖）。
3. `lookDeg=` 数值型字段：如需精确角度再补数据；现阶段先让 `lookArea` 在 Default 计划下也生效，不急着补 `lookDeg`。
4. 注意 Sun 的原则：只接线、不删除——`[look.<area>]` 兜底表保留，不要因为 Default 补了 `look=` 就删兜底。

**验收标准：**
- 复跑同种子 30 局基准：wiring 里 `lookAngle != -99` 的槽位占比从 0% 提升到 50% 以上；Default 计划 target 为 '-' 的占比从 42% 降到 20% 以下；target=MID 的计划行槽位与 MID 控中设计一致（2 人控中 + 两侧架点），不再是 plain 分兵。
- verify_wiring 34/34 保持 PASS。

---

### P1-3 Split / Fake / rotate 触发门槛过严：Split 30 局仅 2 次、Fake 1.1%、rotate 意图 0 次

**现象与证据：**
- 30 局 plans 5336 行里 split_b 仅 2 次、split_a 0 次；Fake 58/5336（1.1%）；wiring 里 T.RotateA 43 + T.RotateB 44 条，但 plans.csv 里 rotate 意图 0 次——轮转以模板形式存在，未进入意图层。
- 证据：`build-evidence\audit-bench30\*.plans.csv`、`*.wiring.jsonl`。

**影响：** 战术多样性缺失；PIX-37 接线的 Split/Fake/rotate 模板存在但几乎不触发；轮转行为不可追踪、不可验收。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/CS2DGameCoach.cpp:877-958`（T 意图打分：候选 Default 0.0 分、MID 0.4 分、Rush/Split/Fake 按信号加分，取最高分）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:891-918`（`sideScore`：热差 × `ScoreHeatDelta`；Rush 要求 `forcedByMemory || (ctrl && haveIntel)`；Split 要求 `controlMid && economyBoost > 0 && smokeCount >= 1`）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:920-927`（Fake：`(heatA - heatB) >= ScoreHeatDelta` 才给 0.8 分，否则 0 分不入选；目标恒为 "A"）
- `PixelEngine/CS2D/Tuning.hpp:297-304`（`SameSideStreakLimit = 3`；`ScoreHeatDelta = 1.0f`；`ScoreRepeatPenalty = 2.5f`；`ScoreSmokeMul = 1.0f`；`ScoreEconomyMul = 0.5f`）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:2192-2225`（`CanRotateNow`：9 道门）与 `:2227-2283`（`CoachTryRotate`：只改 T 的计划；`ApplySlotTemplate` 在 2264 行）；`:172-175`（每 0.75s 调一次 `CoachTryRotate(T)`）；`:2241-2243`（热差门槛 `kDelta = 1.2f`）
- `assets/config/tactics/dust_2.ini:430-447`（`[slots.T.SplitA@A]`、`[slots.T.SplitB@B]`、`[slots.T.Fake@A]` 存在）；`:47-59`（plain `[slots.T.RotateA/B]` 存在）

**机制解释：** Split/Fake 不是没模板，而是**信号门槛在实战里几乎凑不齐**。Split 要同时满足三件：中路已被控（`controlMid` = 至少 1 名队友在 Mid 侧）、经济档位 Full/Half（`economyBoost > 0`）、至少 1 颗烟（`smokeCount >= 1`）；三者交集少，得分还要压过 Default/MID。Fake 更窄：要求 `heatA - heatB >= 1.0` 才给 0.8 分——且 Fake 只写死打 A 侧，B 侧热高时也只能等 A 侧更热。Rotate 的断点在另一处：**意图只在 `CoachTryRotate` 里产生**，而 `CanRotateNow` 有 9 道门（仅限 T、未下包、剩余时间 ≥30 秒、人数不落后 2 人、距上次转点 ≥8 秒、至少 2 条情报、当前意图必须是 Default、新计划已执行 ≥12 秒、且无人进包点/C4 携带者不在包点 280px 内），最后还有热差 ≥1.2 才转。门一多，plans.csv 里 rotate 意图自然为 0。

**改法建议：**
1. Split：三件套改成“满足两件即候选”（例如控中 + 有烟即可，不强制经济档位；或经济 Full 时放宽控中要求）；得分上给 Split 单独的基础分，不让它总被 Default/MID 压住。
2. Fake：热差门槛从 1.0 降到 0.5–0.7（需 Sun 定数值，或先按 0.6 试）；Fake 目标不要写死 A 侧，改为打热度低的一侧（佯攻的本意就是打对方不注意的一侧）。
3. Rotate：9 道门逐项复核——“至少 2 条情报”“新计划已执行 ≥12 秒”“热差 ≥1.2”三道最可疑，先放宽到“至少 1 条情报”“执行 ≥6 秒”“热差 ≥0.8”试；转点意图要写进 plans.csv（现在只有模板没有意图，无法验收）。
4. 数值调整走 `tactics.ini` / `Tuning.hpp` 的数据层，不要改打分结构；调完用同种子 30 局基准复测，看触发率变化。

**验收标准：**
- 复跑同种子 30 局基准：Split 出现次数从 2 次提升到 20 次以上；Fake 占比从 1.1% 提升到 5% 以上；plans.csv 里 rotate 意图从 0 次提升到 10 次以上。
- 战术多样性不破坏胜负结构：T 胜率不应因此项单独大幅波动（本项修的是“打法多样性”，不是“谁赢”）。

---

### P1-4 燃烧弹从未被分配；情报点投掷 75% 落点校验失败

**现象与证据：**
- wiring 槽位 util 分布：烟（1）约 2059、闪（2）约 2037、雷（4）22、火（3）**0**。
- 30 局 `nadeThrows 1337 / nadeSkips 4079`——情报点投掷 75% 落点不可行。
- 证据：`build-evidence\audit-bench30\*.wiring.jsonl`（util 分布）、`*.rounds.csv`（nadeThrows/nadeSkips）。

**影响：** 战术库里有燃烧弹但教练永远不用，道具维度少一种；BOT 频繁尝试向墙后情报点扔雷但扔不出去（1.5 秒冷却后重试），等于大量空转的投掷意图。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/Coach/TacticsTable.hpp:246-253`（`ParseSlot` 的 `util=` 解析：`molotov` → 3，解析器支持）
- `assets/config/tactics/dust_2.ini`、`mirage.ini`、`ancient_ruins.ini` 全仓 grep：`util=molotov` **0 次**；`util=` 仅出现 smoke/flash/he 三种
- `PixelEngine/CS2D/CS2DGameBots.cpp:103-130`（`PlanGrenadeThrow`：落点校验）
- `PixelEngine/CS2D/CS2DGameBots.cpp:508-524`（计划卡 util 投掷：160–460px 才试，失败**不记 skip**）
- `PixelEngine/CS2D/CS2DGameBots.cpp:994-1009`（情报点投掷：无目标 + 有情报 + 在 180–460px 内才试；失败则 1.5s 冷却 + `++counters_.nadeSkips`）
- `PixelEngine/CS2D/Tuning.hpp:142-144`（`NadeThrowMin = 180`、`NadeThrowMax = 460`、`NadeCooldown = 7.0f`）；`:269-270`（`NadeValidAngleStep = 15°`、`NadeValidTries = 5`）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:1876-1881`（`ai.utilKind = so.util`；`utilArea` → `ai.utilX/utilY`）

**机制解释：** util=3 是**纯数据缺失**：解析器支持 `molotov`，执行层 `ai.utilKind` 照单全收，但三张图的战术表里没有任何一个槽位写 `util=molotov`——只有 smoke/flash/he，所以教练永远发不出火。情报点投掷 75% 失败是校验本身太严：`PlanGrenadeThrow` 要求沿投掷方向、在标称落点处**同时**满足三点——落点在图内、无碰撞、有从 BOT 到落点的直线视线（124 行 `HasLineOfSight`）；失败只允许左右偏转 15°/30° 各试一次（共 5 次）。情报点恰恰是“有情报、无视野”的点——目标常在墙后，落点视线十有八九被墙挡，于是每 1.5 秒重试一次、每次记 1 个 skip，4079 次就是这么攒出来的。另注意计划内 util 投掷（508-524 行）失败时静默跳过、不记 skip，所以真实“想扔没扔成”比 4079 更多。

**改法建议：**
1. 数据层：三张图的战术表里给合适的槽位补 `util=molotov`（例如守包/封路槽位），先让火能被分配；数值走 ini，不动代码。
2. 情报点投掷先算可达性再起意图：起意图前先用 `PlanGrenadeThrow` 的校验预筛落点，不可达就换落点（例如情报点附近的开阔点）或直接不起意图，不要每 1.5 秒重试攒 skip。
3. 落点校验放宽：允许“落点本身无视线、但抛物线可达”的情况（手雷是抛过去的，不是平射过去的）；或增加偏转尝试次数（5 次 → 8–10 次，角度步进 15° 不变）。
4. 计划内 util 投掷失败也要计数（与情报点 skip 分开记），否则“想扔没扔成”的总数永远未知。

**验收标准：**
- 复跑同种子 30 局基准：wiring 里 util=3（火）出现次数从 0 提升到 50 次以上；`nadeSkips` 从 4079 降到 1500 以下（降幅 60%+）；`nadeThrows` 不下降。
- 抽查单局：情报点投掷不再出现“同一 BOT 对同一情报点连续多次 skip”的空转。

---

### P1-5 驻守点重合与移动抖动：duplicate_hold、方向反转 690 次/回合

**现象与证据：**
- 同队多名 BOT 驻守点重合（<24px，几乎站同一点）：dust_2-s11 36 次、mirage-s11 54 次 duplicate_hold，约每回合 4 次，多发于回合开始时刻。
- 方向反转平均 690 次/回合（中位数 567，p75 894，最高单回合 7471）；低难度档位更严重（档位 0 总计 16932 次 vs 档位 7 仅 5888 次）。
- 证据：`build-evidence\audit-playtest\dust_2-s11.conflicts.csv`、`mirage-s11.conflicts.csv`、`*.rounds.csv`（dir_reversals）。

**影响：** 交叉火力失效、一点被穿/被闪全队遭殃；手感上像“叠罗汉”和“前后前后”的抖动感；低难度下更明显。

**代码位置（live 树实测）：**
- `tools/dev_replay/conflicts.hpp:106-121`（duplicate_hold：同队两 BOT 的 `planHoldX/Y` 距离 <24px 即记一次）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:1834-1864`（`CoachApplyPlan`：同区域多槽位都取 `AreaCenterByName(so.area)` 的同一个中心点作为 `SetBotGoal` 目标；只有 `useAnchor` 时才取锚点）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:641-659`（`BombPlanted`/回防事件分支：全员同一 `siteArea`）
- `PixelEngine/CS2D/CS2DGameSquad.cpp:527-552`（`AssignSquadRoles` 非动态分支：T 按 `tIdx % siteCount` 轮流分包点——5 名 T、2 个包点必有 3 人同点；CT 按 `ctIdx % (siteCount+1)` 分 A/B/中路）
- `PixelEngine/CS2D/CS2DGameBots.cpp:1081-1089`（下包后 T 环形站位：8 方位角 × 170px）与 `:1113-1114`（CT 掩护环 180px）——**实际移动目标是散开的，`planHold` 不是**
- `PixelEngine/CS2D/CS2DGameBots.cpp:256-276`（`FollowPathTo`：到达死区 `kArriveR = 28.0f`，进入即停）
- `PixelEngine/CS2D/CS2DGameSquad.cpp:713-745`（`AccumulateSeparation`：分离力死区 `SEP_RADIUS × 0.55`；`Tuning.hpp:103`：`SepRadius = 52.0f` → 死区 28.6px）
- `PixelEngine/CS2D/CS2DGameBots.cpp:284-296`（重寻路：每 `RepathInterval = 0.8s` 或目标移动 >140px 即重算）
- `PixelEngine/CS2D/CS2DGameBots.cpp:1355-1386`（`moveLatch` 防抖只覆盖侧拉机动，不覆盖寻路）

**机制解释：** duplicate_hold 首先是**计划层粒度问题**：`planHoldX/Y` 只由 `SetBotGoal` 写，而 `CoachApplyPlan` 对同一区域的多个槽位传的是同一个区域中心；事件分支全员同一 `siteArea` 更甚；回合开始 `AssignSquadRoles` 把 5 名 T 按 `tIdx % 2` 分到 2 个包点中心，3 人同点是算术必然。所以冲突计数器量的是“计划驻守点”，不是 BOT 的实际站位——实际站位有环形散开和分离力在推。真正的同点站桩只发生在目标点本身重合且分离力推不开时。方向反转有三处合谋：一是**到达死区 28px 与分离死区 28.6px 几乎重合**——BOT 走进 28px 圈即停，队友分离力在 28.6px 内把它推出圈外，下一帧又走回来，来回震荡；二是**重寻路每 0.8 秒**，目标稍动或路径走完就重算，短距离上反复换向；三是 `moveLatch` 防抖只管交火侧拉，不管寻路。低难度反转更多与难度映射有关：难度 0 时反应 2.5 秒、FOV 半径 ×0.8，目标感知更迟、更多时间处在“无目标寻路/扫描”状态，抖动基数更大。

**改法建议：**
1. 计划层散开：`CoachApplyPlan` 对同一区域的多个槽位不要都取区域中心——按槽位序号在区域内取偏移点（例如区域中心 ±40px 的环形散开，或复用已有的 8 方位角散开逻辑）；`AssignSquadRoles` 的 `tIdx % siteCount` 改成“同包点多 BOT 时取包点内不同锚点”。
2. 死区错开：到达死区 28px 与分离死区 28.6px 几乎重合是震荡主因——把分离死区改小（例如 20px）或把到达死区改大（例如 36px），让两者错开 8px 以上；数值走 `Tuning.hpp`，先试一档。
3. 重寻路节流：短距离目标（<200px）重寻路间隔从 0.8 秒放宽到 1.5 秒；或目标移动 <140px 时不重算（现有阈值是 >140px 即重算，可放宽到 >200px）。
4. `moveLatch` 防抖扩展到寻路：寻路目标切换时也加方向锁存，避免 0.8 秒内反复换向。
5. 注意：duplicate_hold 计数器量的是 `planHold` 不是实际站位——修完后要同时看实际站位（逐 tick 位置 dump）确认真的散开了，不能只看计数器归零。

**验收标准：**
- 复跑同种子 30 局基准 + trace：duplicate_hold 从约每回合 4 次降到 1 次以下；方向反转从均 690 次/回合降到 300 次以下；低档位（0 档）反转从 16932 次/局降到 8000 次以下。
- 抽查单局 trace：同区域多 BOT 的 `planHold` 点两两距离 >40px；不再出现“进圈—被推出—再进圈”的震荡序列。

---

### P1-6 T 原地不动 40%：下包前站桩与下包后架枪混在一起

**现象与证据：**
- T 原地不动占比 40%（30 局平均 still_t 0.402，CT 0.287；ancient_ruins 高达 0.459；新 4 局 0.409）。T 赢的回合 still_t 0.449，CT 赢的回合只有 0.270。
- 证据：`build-evidence\audit-bench30\*.rounds.csv`（still_t/still_ct）、`build-evidence\audit-playtest\*.rounds.csv`。

**影响：** 下包后 T 全员站桩守包（部分合理），但 40% 包含下包前的站桩——进攻方长时间不动等于把主动权让给 CT；指标本身不分阶段，无法判断多少是合理架枪、多少是真站桩。

**代码位置（live 树实测）：**
- `tools/dev_replay/metrics.hpp:175-189`（still：移动 <0.15 且无交战目标；idle：无驻守点/无路径/无目标）
- `PixelEngine/CS2D/CS2DGameBots.cpp:256-276`（到达死区 28px，进入即停）
- `PixelEngine/CS2D/CS2DGameBots.cpp:559-578`（原地扫描：无目标且静止时 ±60° 摆动，不产生位移）
- `PixelEngine/CS2D/CS2DGameBots.cpp:1081-1089`（下包后 T 环形站位，到达即停）

**机制解释：** `still` 的定义是移动 <0.15 且无交战目标——下包后 T 到达守包环即停，架枪/扫描全程不产生位移，都会被计为 still；`idle`（无驻守点/无路径）只有 1.3%，说明 BOT 不是没事干，而是“到了点就不动”。问题不在“不动”本身，而在指标不分阶段：下包前站桩（进攻方等时间流逝）和下包后架枪（守包方合理行为）被混在同一个数字里，无法定位真问题。

**改法建议：**
1. 先分阶段再修：metrics 里 `still` 按 `bombPlanted_` 切分——下包前 still 和下包后 still 分开记；下包后 still 高是合理的，下包前 still 高才是问题。
2. 下包前站桩的成因待分阶段数据出来后再定：可能的原因包括守包环到达即停、原地扫描时间过长、计划重发间隔内无新目标。
3. 架枪行为本身可加“微移”：架枪超过 N 秒无交火时，BOT 在架枪点附近小范围换位（换角度不换区域），既保持架枪又不显得站死。
4. 注意：不要为了降 still 数字而让 BOT 无意义地走动——架枪不动是正确的，修的是“该动的时候不动”。

**验收标准：**
- 先出分阶段数据：下包前 still_t 从当前（待切分）降到 25% 以下，下包后 still_t 保持 40%–50%（守包架枪是合理的）。
- 复跑同种子 30 局基准验证分阶段数字，不只看总 still。

## 四、P2——打磨项（5 项）

### P2-1 经济系统：Full 档占 80%，Eco 几乎够不着

**现象与证据：**
- 买枪档位 Full 占约 80%（T 258/324、CT 271/324 回合；Eco 仅 20/16）。
- 证据：`build-evidence\audit-bench30\*.rounds.csv`（buy_t/buy_ct 枚举档位）。

**影响：** 连败经济压力、Eco 局/长枪局区分在对局里几乎体现不出来；经济数值可能过于宽松。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/Coach/CoachTypes.hpp:32`（`enum class BuyPlan { Eco = 0, Half, Force, Full, AntiEco }`——5 档，试玩组“0–3”的说法漏了 AntiEco=4）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:2111-2146`（`DecideBuyPlan`：Full 要求 `avg >= 3500`；Half 2600–3500；Force 1800–2600；Eco 只在 `avg < 1800` 或 `ShouldSaveNow` 时）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:2168-2183`（`ShouldSaveNow`：回合 >45 秒、人数优势、T 已站包点时直接 false；最终只看回合开始平均余额 <1800）
- `PixelEngine/CS2D/CS2DGameCoach.cpp:2076-2100`（`GetRoundStartEconomy`：队伍平均余额，**含脚本玩家**）
- `PixelEngine/CS2D/Economy/EconomySystem.hpp:16-28、53-65`（胜方 +$3250；败方补偿 $1400 起，每连败 +$500，$3400 封顶；下包 T 全员 +$300；起始 $800）
- `PixelEngine/CS2D/CS2DGameRound.cpp:365-385`（Full 档购买：AWP 4750 → … → Nova 1050 依次取买得起的最贵者，另加护甲与满额投掷物）

**机制解释：** Full 占 80% 是数值结构决定的，不是档位逻辑坏了。收入端：胜方每回合 +3250，败方补偿从 1400 起每连败 +500、4 连败即达 3400 封顶；T 还稳定拿下包 +300/人。支出端：Full 档一支步枪 2700–3100，与胜方奖励 3250 基本相当。于是胜方打完一回合基本能原样满起；败方连败两三回合后补偿也爬到 2400–3400，`avg` 很难跌破 1800 的 Eco 线。回合 1 强制 Force。另有两个放大器：`GetRoundStartEconomy` 是队伍平均且含脚本玩家（CT 的脚本玩家很少阵亡、很少买枪，持续囤钱，抬高 CT 均值）；T 74% 的回合胜率使 T 的平均余额长期处在 Full 区间。结论：经济档位逻辑本身在跑（Half/Force 的阈值都在），但收支数值让 Eco 几乎够不着触发条件。

**改法建议：**
1. 数值层先调一档试效果：败方补偿封顶从 3400 降到 2900，或胜方奖励从 3250 降到 3000；Eco 线从 1800 提高到 2000。数值走 `EconomySystem.hpp` / `DecideBuyPlan`，改一处、测一次，不要多处齐改。
2. `GetRoundStartEconomy` 剔除脚本玩家（或剔除“本回合不买枪”的单位），避免囤钱抬高均值。
3. `ShouldSaveNow` 的“连败存钱”条件放宽：目前要求 `lastBuyPlan == Force` 或 `avg < 2600` 才触发，可改为连败 ≥2 即考虑存钱。
4. 注意：经济数值牵动全局平衡，改动后必须用同种子 30 局基准复测胜率、档位分布、回合时长三项，不要只看档位分布。

**验收标准：**
- 复跑同种子 30 局基准：Eco 档占比从 5% 提升到 15% 以上；Full 档从 80% 降到 65% 以下；T 胜率波动不超过 ±5pp（经济调整不应单独大幅改变胜负）。
- 抽查：连败 3 回合的队伍至少出现 1 次 Eco 局。

---

### P2-2 残局 1vX 处理与超长拖局

**现象与证据：**
- T 赢的下包回合 76.7% 以 1 名 CT 存活结束——残局常以“最后一人到晚了/不敢拆”收场。
- 26/324 回合 ≥6000 tick（约 100 秒以上），mirage 占 17 个；最长 7611 tick（约 127 秒）。新局 mirage-s11 R10 7426 tick（约 124 秒），T 下包、CT 无拆包尝试、1v1 残局拖到爆炸。
- 证据：`build-evidence\audit-bench30\*.rounds.csv`（`alive_ct_end`、回合 tick 数）、`build-evidence\audit-playtest\mirage-s11.rounds.csv`（R10）。

**影响：** 残局 1vX 几乎总是输；回合拖沓、无结论感；mirage 尤甚。

**代码位置（live 树实测，部分）：**
- `PixelEngine/CS2D/CS2DGameCoach.cpp:592-597`（下包后 T 人数落后时：Default + Slow，“等拆包再抓”——残局偏被动）
- `PixelEngine/CS2D/CS2DGameBots.cpp:190-212`（`BotCanDefuse` 的 6 秒强制窗口；残局 1v1 时拆包手常因“看得见敌人”不敢拆）
- 残局专项逻辑：代码里未找到“1vX 残局”的独立分支——残局走的是通用 Default/Retake 计划（定位组核实：无残局专项）。

**改法建议：**
1. 加残局分支：1v1/1v2 时，拆包手由“等掩护”改为“引信 ≤10 秒强制拆”（利用 6 秒强制窗口的思路，把窗口放宽到残局）；T 残局 1vX 时改为“守包点架枪等拆包声”，而不是主动找人。
2. 超长拖局：回合剩余时间 <20 秒且未下包时，T 强制转 rush 下包（不要继续架枪等时间）；CT 剩余时间 <20 秒且 T 未下包时，CT 收缩守包点。
3. 残局 1v1 的“不敢拆”问题与 P0-3 的拆包资格放宽联动修，不要单独立项重复改。

**验收标准：**
- 复跑同种子 30 局基准：≥6000 tick 的超长回合从 26 个降到 15 个以下；1v1 残局里 CT 拆包尝试率（有拆包动作的占比）从当前（待统计）提升到 50% 以上。
- 抽查 mirage-s11 R10 同类回合：不再出现“下包后 100 秒以上无人拆包拖到爆炸”。

---

### P2-3 短距离路径效率 0.23 与重寻路频率

**现象与证据：**
- 短距离移动路径效率均值 0.23（直线/实际），中距离 0.49、长距离 0.79。
- 证据：`build-evidence\audit-bench30\summary.csv`（path_eff_short 列）。

**影响：** 近点微操（架枪点微调、残局走位）效率低；配合方向反转数据看，短距离移动多为非目标性摆动。

**代码位置（live 树实测）：**
- `PixelEngine/CS2D/CS2DGameBots.cpp:284-296`（重寻路：每 `RepathInterval = 0.8s` 或目标移动 >140px 即重算）
- `tools/dev_replay/metrics.hpp:152-173`（`epTravel` 实际走的路 vs 直线距离）
- 与 P1-5 同源（28px/28.6px 共振 + 0.8s 重寻路）。

**改法建议：**
1. 与 P1-5 联动修（死区错开 + 重寻路节流），本项不单独改代码；修完后看短距离效率是否随方向反转一起改善。
2. 短距离目标（<200px）可走直线微调而不经过完整 A*（目标在视线内且无碰撞时直接走直线，省一次寻路）。
3. 验收时短距离效率单独列，不与中长距离混在一起看。

**验收标准：**
- 复跑同种子 30 局基准：path_eff_short 从 0.23 提升到 0.40 以上；中/长距离效率不下降。

---

### P2-4 分支多样性为零：hasBranch=true 的 532 条全是 rush

**现象与证据：**
- hasBranch=true 的 532 条里 branchIntent 全是 rush_a/rush_b。
- 证据：`build-evidence\audit-bench30\*.plans.csv`（hasBranch × branchIntent）。

**影响：** 分支机制存在但只会变 rush；战术变化不可见。

**代码位置（live 树实测，部分）：**
- `PixelEngine/CS2D/CS2DGameCoach.cpp:877-958`（意图打分；分支意图候选只在 Rush/Split/Fake 里取最高分，Default/MID 不参与分支）
- 定位组注：分支（branch）只会变 rush 是打分结构决定的——分支候选集里没有“转点/控中/佯攻”选项。

**改法建议：**
1. 分支候选集扩大：hasBranch 时允许分支到 rotate（转点）、fake（佯攻）、mid（控中），而不只是 rush_a/rush_b。
2. 分支触发条件与 P1-3 的 Split/Fake/rotate 门槛放宽联动修，不要单独立项重复调阈值。
3. 注意 Sun 的原则：分支类型只增加、不删除——rush 分支保留。

**验收标准：**
- 复跑同种子 30 局基准：hasBranch=true 的条目里非 rush 分支占比从 0% 提升到 30% 以上。

---

### P2-5 试玩工具链改进（不动游戏行为，只为下次审计能验得更准）

**现象与证据（本次审计的工具缺口）：**
- 脚本玩家在 mirage 上 12 局 0 杀 0 死（跟随蹲 A 点队友、零交火），等于 CT 实际 4 打 5；K/D 7.39→0.81 不能读作 BOT 变强，主因是队伍胜率翻转 + 被动跟随策略。
- 脚本玩家不用投掷物、不买钳子、不主动决策；`--buy-kit` 默认关。
- runner 不支持换边（脚本玩家固定 CT，T 方视角未验证）。
- 无交火级日志：反应时间/瞄准精度/开火换弹细节无法量化。
- 证据：`tools/dev_replay/scripted_player.hpp:108-160`（跟随最近队友 >160px 才动；600px 内有视线才开火；`NearestMate` 不挑人挑点）、`:210-211`（`--buy-kit` 默认关）、全文无投掷物逻辑。

**改法建议：**
1. 脚本玩家跟随目标改成“正在交火/正在推进的队友”，而不是“最近的队友”；跟随目标每 N 秒重评估一次，不要全程跟住同一个人。
2. 脚本玩家加三种对照策略（架点型、rush 型、静步型），分别跑对照局，测 BOT 对真人打法的反应。
3. runner 加换边选项（脚本玩家为 T），补 T 方视角。
4. 加交火级日志：开火时刻、命中率、反应延迟分布、弹药与换弹事件（用于下次审计反应/瞄准/开火换弹三项）。
5. `--buy-kit` 场景的 `audit-kit` 证据本次未分析，下次审计先分析它再决定 BOT 买钳逻辑是否要修。
6. 注意：工具链改动走 `tools/`，不碰游戏本体；随机数改动走独立的 `rng_humanizer_`，不碰全局 `rng_`。

**验收标准：**
- 下次审计时：脚本玩家在 mirage 上不再 0 杀 0 死；三种对照策略各跑 1 局同图同种子，比分/回合数有区分；交火级日志能回答“反应时间分布是多少、空仓换弹是否执行”。

---

## 五、未验证项（诚实标注，本次不要当作已验过）

| 未验证项 | 原因 | 下次怎么验 |
|---|---|---|
| 真实键鼠手感、FPS、音效实听、画面视觉 | SSH 会话无显示器，EngineApp 跑不起来 | 带显示器的会话 + `tools/win_accept.ps1`（主菜单/HUD/开镜/买枪菜单/设置页截图验收），CT 与 T 各一局、dust_2 与 mirage 各一局 |
| 反应时间 / 瞄准精度 / 开火换弹细节 | 无交火级日志 | P2-5 的交火级日志 + 真人试玩 |
| BOT 对真人架点 / rush / 静步的反应 | 脚本玩家是被动跟随者，mirage 上零交火 | P2-5 的三种对照策略脚本玩家 |
| LLM 教练 A/B | 需 llama-server；本次全部 `coach=rule`，`decisions.csv` 为空 | 先起 llama-server，再跑 LLM 组对照 |
| 钳子对回防的影响 | `--buy-kit` 场景的 `audit-kit` 证据本次未分析 | 先分析 `audit-kit`，再决定 BOT 买钳逻辑 |
| T 方视角 | runner 不支持换边 | P2-5 的换边选项 |
| 经济细节（BOT 买枪明细）、报点行为 | 无日志产物 | 交火级日志 + 报点信号日志 |
| 6 条渲染器测试失败在有显示时是否通过 | 本次只在 SSH 会话跑 | 桌面会话补跑 gameplay_tests 默认 + `--experience` |
| `defuse_near_ct` 0.87 与 77.2% 被打断的时序分布 | 需逐 tick 位置 dump | 下次审计加逐 tick 位置 dump |
| 方向反转的低难度相关性 | 难度只改反应/FOV，不直接改移动 | 分档逐 tick 速度序列验证 |
| T 原地不动 40% 的分阶段构成 | `still` 指标不分阶段 | 按 `bombPlanted_` 切分重算（P1-6） |

---

## 六、验收总则

1. **复测基准固定**：所有验收复跑同一批 30 局（dust_2/mirage/ancient_ruins × seeds 1–10，`--jobs=4 --no-hash`），与 `build-evidence\audit-bench30\` 同图同种子对比；不要换种子、不要换图，否则数字不可比。
2. **基线数字（本次审计，复测前）：** T 胜 28/30（93.3%）；回合 T 赢 74.2%；下包率 85.2%，下包后 T 赢 85.8%，CT 回防成功 14.2%；无人拆包 40.6%，拆包尝试失败 77.2%；`alive_t_end=0` 占 T 赢下包回合 48.1%；Split 2 次/30 局；Fake 1.1%；rotate 意图 0 次；util=3 火 0 次；`nadeSkips` 4079；duplicate_hold 约每回合 4 次；方向反转均 690 次/回合；Full 档约 80%。
3. **方向性目标（不设硬数字的部分）：** T 胜率向 50% 回落是方向，不是硬门槛——胜率本身受多项联动影响，单项验收以各条的验收标准为准；只有 P0 的三项有硬数字（爆炸自杀占比、kills 归属、回防三率）。
4. **回归红线：** gameplay_tests 不得新增失败（1750/6 基线，6 条渲染器失败除外）；`verify_wiring.py` 34/34 保持 PASS；确定性复现（同局同种子两次哈希一致）不得破坏。
5. **验收方式：** 每项修完后复跑 30 局基准 + 抽查单局 trace/wiring；真人试玩（带显示器会话）补验手感与画面；Sun 目检通过才算完（视觉改动附进游戏截图，音频改动由他耳检——本次计划无音频项）。

---

## 七、硬约束（Sun 的原则，实施时逐项对照）

1. **绝不删除任何决策或数据**：只提接线/修复，不提删除。Split/Fake/rotate/火（util=3）等未被调用的意图与数据，全部按“接线让它能被调用”处理；`[look.<area>]` 兜底表、rush 分支、plain 模板全部保留。
2. **随机数改动必须走独立的 `rng_humanizer_`**，不碰全局 `rng_`；回放哈希是开发期调试工具，任何提案先满足可摘除约束（编译开关隔离、单独只读模块、一行 guarded 钩子、benchmark runner 独立于游戏二进制）。
3. **已发 Linear**：按 Sun 2026-11-23 指示，本计划已发 Linear（PIX-40）；PIX-38 是 Sun 自己的，不要碰。
4. **不要动基线文件**：`tools/dev_replay/baseline/summary.csv` 仍是 tag=191caa4 的 60 局；本次的 `audit-bench30` 是新基线候选，是否覆盖入库由 Sun 定。
5. **实施范围已定**：Sun 2026-11-23 决定由 Muse 实施全部 14 项，按建议顺序 P0-1 → P0-2 → P0-3 → P1-1 → P1-2 → P1-3/P1-4 → 其余，分批实施、分批复测，最后统一验收；不要一次全改。

---

## 附录 A：证据文件索引（Sun 电脑仓库内）

| 证据 | 路径 |
|---|---|
| 30 局批量基准（151 个文件） | `build-evidence\audit-bench30\`（summary.csv + 每局 wiring/plans/decisions/coach_rounds/rounds） |
| 单局全量证据（dust_2 s1、mirage s2） | `build-evidence\audit-single\`（trace/wiring/plans/coach_rounds/conflicts/decisions） |
| 新跑 9 局 | `build-evidence\audit-playtest\`（Batch A、trace、难度扫测 diff0/diff3/diff7、logs/） |
| 钳子场景 | `build-evidence\audit-kit\`（本次未分析） |
| 构建/测试日志 | `D:\audit-build-*.log`、`D:\audit-tests-default.log`（24613 行）、`D:\build_tests_run.log`（11 月 19 日 1843/0 旧日志） |
| 试玩完整报告（沙箱） | `~/workspace/pixelengine-bot-audit-2026-11-23.md` |
| 代码定位完整报告（沙箱） | `~/workspace/pixelengine-bot-code-locations-2026-11-23.md` |
| 60 局基线（不要动） | `tools/dev_replay/baseline/summary.csv`（tag=191caa4） |

## 附录 B：本次审计纠正的试玩统计错误（已在正文各处标注，汇总于此）

1. `lookArea` 全空是错的：实测 5382 行里 2668 行非空，retake 1770/1770 非空；断点在 Default 计划，不在全部意图。
2. `BuyPlan` 是 5 档（Eco/Half/Force/Full/AntiEco），试玩组“0–3”的说法漏了 AntiEco=4。
3. `defuses` = 拆包尝试次数，不是拆包成功次数；`kills_t` 含炸弹击杀；`buy` 是枚举档位不是人数；`money` 含脚本玩家。
