# PIX-37 交付汇报（给 Sun / Muse）

> 实施者：DeepSeek（本地会话，2026-11-19）。改动全部留在工作区，**未提交、未 push**，未删除任何文件或决策。
> 完整逐项验收表：`docs/PIX-37-rules-coach-wiring-acceptance.md`。

## 0. 一句话结论

PIX-37 的 37 项任务（T01–T37）全部实施完成，代码编译通过、`gameplay_tests` 1843/1843 通过、
确定性双跑逐字节一致；自测验收表 34/34 PASS（脚本口径）。两处验收点**在自由对局中未自然触发**
（T05 预算拦截场景、T35 的 LLM 拒绝率），已如实标注，未冒充验过。

## 1. 每阶段做了什么

| 阶段 | 内容 |
|---|---|
| Phase 0 验收基建 | T01 `coach_wiring.jsonl` 结构化决策日志（每下发一条规则计划写一行，slots 9 字段/signals/guards/templateKey 全量）；T02 `tools/coach/verify_wiring.py`（34 项判定，读日志输出 PASS/FAIL 表）+ runner 新增 `--wiring/--coach-rounds/--no-hash/--buy-kit` 开关；T03 改动前基线双跑（`3d3034f413a67730`，一致）+ 改动后双跑（70270 tick，一致）。 |
| Phase 1 P0 接线 | T04 @ 模板加载（kIntents 扫 `slots.*@` 段登记）；T05 changesThisRound 自增+清零+拦截标记；T06 hasBranch（MID/Default 有情报时设分支、Fake 恒设，20s 后触发）；T07 sameSideStreak 更新/消毒复用/打分读 Limit=3；T08 Sanitize 复用于规则（反应型 critical 除外）；T09 Rotate 进 kIntents/schema/三图 ini 表段。 |
| Phase 2 决策质量 | T10 回合开始意图打分制（heat 差/连续失败换点/已控/道具/经济/streak，全确定性输入）；T11 MID 控中变体；T12 Split/Fake 条件打分；T13 RoundMemo.enemyAttackSide + CT 重防；T14 道具/敌枪/联系人/lastOrder 进规则；T15 heat 进 RoundStart；T16 soft-KEEP 减员变速；T17 HumanDied 分支。 |
| Phase 3 经济与赛点 | T18 跨回合状态机（Force 输→Eco、连败≥2 存钱、我穷敌更穷→Force）+ GetLossStreak 接线；T19 MatchPoint 分支 + 赛点强起；T20 钳子感知 mustRush 5s/11s；T21 回防分工走已校准 @ 模板（1 拆+2 掩护+2 控路）。 |
| Phase 4 槽位与执行 | T22 按能力分槽（拿狙→Awper 槽、钱少优先、难度降序）；T23 规则槽位经 @ 模板带 look/hold/util；T24 锚点放开（带 look/hold 即用锚点）；T25 RefreshRolesForTeam 护栏（缺角是有意布局时不补人）。 |
| Phase 5 执行层角色 | T26 BotRole 扩展 Lurk/Awper + 映射直通 + 决策树三处 RoleSpeedMul（0.70/0.60）；T27 RoleTransitionSec 3s 插值过渡（SetRoleWithTransition 记录旧倍率）。 |
| Phase 6 数据质量 | T28 拆包数字三处校准（有钳 4s/无钳 10s、强拆 5s/11s）；T29 C4 掉落按侧发 Stack；T30 reason 60 字符统一 + 手册补 2600 边界 + ttl 关系写明；T31 区域控制度量；T32 分侧敌人数；T33 mirage/ancient [areas] 真实区域；T34 few-shot 补到 5 类/图 + advisory 21 条分类标注。 |
| Phase 7 收口 | T35 30 局基准重跑（指标如实记录）；T36 coach_rounds.jsonl + gen/coach_version 字段；T37 验收汇总文档。 |

## 2. 构建与测试结果

| 项 | 结果 |
|---|---|
| 主程序 `build-release\EngineApp.exe` | 编译通过（0 error） |
| `gameplay_tests.exe` | **1843 通过 / 0 失败**，EXIT=0 |
| `dev_replay_runner.exe`（dev-only） | 编译通过 |
| T03 确定性 | 改动前双跑一致（final_hash `3d3034f413a67730`）；改动后双跑一致（70270 tick，`[diff] 完全一致 OK`） |

构建脚本 `D:\build.ps1` / `D:\build_tests.ps1` 在本机不存在，我直接用 cmake 命令行构建
（`cmake --build build-release/build-assetbatch-tests/build-dev`），效果等同。

## 3. 验收点通过情况（自测）

- 脚本口径 **34/34 PASS**（T01–T36，T37 即汇总文档）。
- 关键局内证据（全部来自真实对局日志，非推断）：
  - T04：templateKey 命中 @ 段 3459 行；T06：hasBranch 548 行 + console `分支触发 -> 速推B` 5 次；
  - T11：target=MID 32 行；T12：Split=6 / Fake=44；
  - T14：烟开点 577 行、敌狙慢打 59 行；T16：console `减员变速 KEEP ... tempo fast->slow (intent 不变)`；
  - T17：console `事件#9 -> ... 玩家阵亡，收位守点`；
  - T19：console `事件#10 -> 速推B | 赛点，全力进攻B`；
  - T20：日志 anyKit/mustRushAt 两种取值 `(False,11.0)` 与 `(True,5.0)` 均实测出现（后者用 `--buy-kit` 场景）；
  - T26：槽位 role=lurk/awper 5370 行；T31：control 信号 237 行；T32：enemyCountA/B 5731 行；
  - T36：coach_rounds.jsonl 632 行全含 gen，Split/Fake/MID 样本 6 行。

## 4. 没通过 / 没验证到的（如实）

1. **T05 "第 3 次改意图被预算拦截"在自由对局未自然触发**：接线与计数已验证（changesThisRound 0–7 自增、拦截分支已写 guards 标记），但 30 局中 budget-blocked 出现 0 次——非 critical 事件先被 soft-KEEP / T 18s 锁 / IntentMinHold 拦住，critical 事件又绕过预算，这是既有调度结构的结果。Muse 可用场景注入复核。
2. **T35 拒绝率（23%→10%）未验证**：该口径是 LLM 回包拒绝率（V2 口径），本次 30 局基准跑的是 `--coach=rule`（未开 LLM）。跑 LLM 组需要 llama-server，超出本次范围。
3. **T09 "LLM 开出转点"加分项未验证**（同上，未开 LLM）。
4. **T22/T24/T26/T27 的行为级位置抽查未做**：代码落点与日志字段已齐，逐 tick 遥测（BOT 实际走到锚点/倍率渐变曲线/绕后轨迹）未做，可用 `--trace=` decision trace 复核。

## 5. 30 局基准数字（T35，如实记录）

- 30 局（dust_2/mirage/ancient_ruins × seeds 1-10，150000 ticks，规则教练）：
  **T 29 胜 / CT 1 胜（T 胜率 96.7%）**；平均回合数 10.5；下包 9.1/局、拆包 5.7/局。
- 配对基线（同 30 局 map/seed）：基线 T 14/30（46.7%），McNemar 双尾 p=0.0001（显著）。
- 意图分布熵：**2.64**（基线 Default 一家独大）；Split=6、Fake=44、MID=32；穷局开局发禁集意图=0。
- AI 冲突/局：0（与基线持平）。
- **T 胜率大涨是真实行为变化，不是验收目标**（计划明确"胜率不要求提升，只要求记录 + McNemar"）。
  方向分析（供 Sun 决策，不自行调平衡）：CT 重防依赖 T13 记忆两回合才成型、回防走 @ 模板后
  CT 中远距离控制变弱、T 侧 rush 触发面扩大（T10 打分）叠加 T19 赛点强起，共同推高 T 胜率。
  平衡调参（如 CT stack 触发阈值、rush 打分权重）不在本次任务范围，建议 Sun 定方向后再调。

## 6. 行为变化清单（T03 要求）

1. 规则教练回合开始从"只读己方胜负计数"改为意图打分制 → Default 不再是唯一开局意图。
2. T 开局新增 MID 控中 / Split / Fake 出口；CT 开局按敌方习惯与情报变阵（StackA/B 重防）。
3. 下包后 CT 回防从"全员 Entry@包点"改为 @ 模板分工（1 拆+2 掩护+2 控路）。
4. mustRush 阈值按钳子 5s/11s 两档。
5. Lurk/Awper 不再坍缩为 Entry/Support，速度倍率从 3 档变 5 档（含 3s 过渡）。
6. 买档从纯快照变为带跨回合状态（禁连强/连败存钱/赛点强起）。
7. 消毒与变更预算正式生效于规则路。
以上每一条都会改变回放哈希——这正是"旧基线失效是预期的"，新基线双跑一致证明改动是确定性的。

## 7. 交接说明

- 改动文件：18 个文件 +981/-80 行（清单见验收文档末尾）；新增 `tools/coach/verify_wiring.py`。
- 证据目录：`build-evidence/bench30`、`verify-final`、`verify-kit`、`pre-base`、`post-base3`（可整体删除重建）。
- dev 工具改动（PXINPUT4 加 buyKit 列、--no-hash/--buy-kit 开关）仅在 `tools/dev_replay/` 与
  runner 内，游戏二进制不含回放/日志代码（全部 nullptr 时零开销，哈希已验证）。
- minimind 三个待定（意图集 11 vs 13、台词字段、日志形状）与 LoRA 训练**未替 Sun 定**；
  coach_rounds.jsonl 只做了写盘 + coach_version 版本区分。
