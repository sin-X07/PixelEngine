# dev_replay —— 开发期确定性 / 回放工具（**非生产代码**）
`n> **日常怎么用：直接看 [WORKFLOW.md](WORKFLOW.md)**（完整流程、判读规则、故障排查、命令速查）。`n> 本文只讲工具结构与实现约定。

给 BOT 逻辑与平衡改动提供"可复现的退化检测"。整套工具是**外挂**：只通过
`CS2DGame` 的公开 API 读状态、只通过公开输入接口驱动，不修改游戏逻辑。

## 构建

```bash
cmake -S PixelEngine -B build-dev -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_DEV_REPLAY_TOOLS=ON
cmake --build build-dev --target dev_replay_runner
```

## 用法

```bash
# 跑一局（默认 dust_2 / seed=1 / 150k tick ≈ 41 分钟游戏时间）
build-dev/dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2

# 逐 tick 哈希日志（供 diff / baseline）
build-dev/dev_replay_runner.exe --ticks=150000 --seed=1 --hash=out/a.txt

# 对比两次运行
python tools/dev_replay/diff_hash_logs.py baseline.txt current.txt
```

参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--ticks=` | 1800 | 仿真 tick 数（60Hz） |
| `--seed=` | 1 | 随机种子（固定后同机可复现） |
| `--map=` | dust_2 | 地图 id |
| `--viewport=` | 1728x1080 | 固定逻辑视口（决定视野半径，见 PIX-20） |
| `--aim-turn=` | 3.0 | 脚本玩家转身速率 rad/s |
| `--aim-error=` | 5.0 | 脚本玩家瞄准误差（±度） |
| `--reaction=` | 15 | 发现目标到开火的 tick 数 |
| `--hash=` | 空 | 写逐 tick 哈希日志（`PXHASH1`） |

## 确定性纪律

1. 同一代码 + 同一参数 → 逐 tick 哈希必须一字节一致；不一致先修，再谈指标。
2. 改脚本玩家策略（`--aim-turn` 等）等于改基准：必须同时更新 baseline 并说明原因。
3. 新增任何 `rng_()` 调用都可能错开随机流；视觉特效若消耗同一条流，同样会改变对局。
4. 浮点用 bit-cast 进哈希；**禁止**字符串化浮点。

## 脚本玩家（第 10 个单位）

固定策略、无随机数：视线内最近敌人 → 转身速率限制 → 反应延迟 → 点射 →
瞄准误差 → 距离门限；无目标时跟随最近队友；购买期按固定顺序补装备。

**为什么需要它**：不做"藏起玩家"的 4v5 旁路，保住 5v5 对称，否则胜率对比失真。

## 删除方式（保持不深嵌）

```bash
rm -rf tools/dev_replay/
# 再删 PixelEngine/CMakeLists.txt 里的 BUILD_DEV_REPLAY_TOOLS 段
git grep PIXEL_DEV_        # 应为 0
# 重建 + gameplay_tests 通过即完成
```

生产侧唯一保留物是 `CS2D/Visibility/VisibilityQuery.*`：它让玩法层不再依赖
`CS2DRenderer`，属长期改进，不做工具也该有。

## 批量基准与比较（P2）

```bash
# 3 图 × 10 种子，约 16 秒
build-dev/dev_replay_runner.exe --ticks=150000 \
    --maps=dust_2,mirage,ancient_ruins --seeds=1-10 \
    --out=build-evidence/current --tag=$(git rev-parse --short HEAD)

# 与基线比较（胜场/回合数/回合时长/道具/玩家 K/D）
python tools/dev_replay/compare_benchmark.py \
    tools/dev_replay/baseline/summary.csv build-evidence/current/summary.csv
```

输出：

| 文件 | 内容 |
| --- | --- |
| `summary.csv` | 每局一行（首行 `# PXBENCH1 tag=... 参数`）：胜者、比分、回合数、平均回合 tick、下包/拆包、玩家 K/D、final hash |
| `<map>-s<seed>.rounds.csv` | 该局每个回合一行：胜者、时长、双方击杀、首杀 tick、存活、经济 |
| `<map>-s<seed>.hash.txt` | 逐 tick 哈希（体积大，默认只在需要定位分歧时保留） |

**注意**：赛后空闲不计入回合时长（`GetMatchWinner()` 一旦确定就停止仿真）。
`alive_t_end/alive_ct_end` 取**结算阶段**的存活数（不是下一回合重生后的值）。
## AI 决策冲突检查（P3）

```bash
dev_replay_runner --ticks=150000 --seed=1 --map=dust_2 --conflicts=conflicts.csv
```

默认规则（低噪声）：`defusing_moving`（拆包中明显移动）、`defusing_unplanted`
（交战期拆未下包的包）、`path_broken`（寻路索引/目标连续 0.5s 不一致）、
`dual_goal`（小队 hold 与教练 plan hold 相距 >160px）。
`duplicate_hold`（两人抢同一点）噪声大，需加 `--noisy-conflicts` 才启用。
同一 (回合, BOT, 规则) 每回合只报一次。

## 输入日志与回放（P3）

```bash
# 录制（脚本玩家；真人录制见下）
dev_replay_runner --ticks=150000 --seed=1 --journal=session.journal --hash=rec.hash.txt

# 回放（按日志重建同一局，最后与日志里的 final_hash 比对）
dev_replay_runner --replay=session.journal --hash=rep.hash.txt
python tools/dev_replay/diff_hash_logs.py rec.hash.txt rep.hash.txt
```

格式：`PXINPUT1` 头（map/seed/viewport/脚本策略/final_hash）+ 每 tick 一行
`tick mouseDown aimX aimY W A S D Shift Reload Interact buyWeapon buyArmorTier`。

**两条坑（都踩过）**：
1. 购买是"动作"，不是移动输入——不写进日志，回放会在第一个购买期分叉（tick ~481）。
2. 浮点必须全精度写（`%.9g`）：`%.3f` 的舍入会让瞄准角差 0.001 rad，几十 tick 后完全分叉。


## 真人输入录制（Engine 侧，dev 构建）

```bash
# 构建 dev 版（EngineApp 会带上 PIXEL_DEV_REPLAY）
cmake -S PixelEngine -B build-dev -DBUILD_DEV_REPLAY_TOOLS=ON
cmake --build build-dev --target EngineApp

# 录制：进对局后固定种子（20261103）并写 journal
build-dev/EngineApp.exe --autostart --record=session.journal

# 回放（真人日志没有 final_hash，只回放不校验）
build-dev/dev_replay_runner.exe --replay=session.journal --hash=played.hash.txt
```

- 录制内容：每 tick 的 `mouseDown / aimX,aimY / WASD / Shift / R / E`，以及
  **购买、切枪、开镜**三类动作（它们不是移动输入，不记就无法复现）。
- 生产构建不定义 `PIXEL_DEV_REPLAY`，`Engine` 里的录制代码整块不存在（已核对 release 编译）。
- 真人日志没有 `final_hash`（Engine 侧不算哈希），所以回放只报告 `NO_EXPECTED_HASH`；
  要严格校验，用脚本玩家录制（带 `--hash` 的 runner 路径）。

## 已验证 / 未验证

| 项 | 状态 |
| --- | --- |
| 脚本玩家录制 → 回放（dust_2、mirage） | ✅ final_hash 一致、逐 tick diff 0 行 |
| 真人录制产生 journal（dev 构建） | ✅ 可回放（206 tick） |
| 真人录制 → 回放一致性 | ⚠ 未验证（Engine 不产哈希；需人工对比或后续补哈希） |
| 冲突检查在真实对局中的噪声水平 | ⚠ 只在脚本对局上跑过（每局 0–2 条） |
## 指标字典（怎么看"BOT 变好还是变差"）

| 指标 | 单位 | 变好 = | 说明 |
| --- | --- | --- | --- |
| 原地不动占比 | % | 下降 | 站着不动且没交战；太高=不敢动/卡住 |
| 发呆占比 | % | 下降 | 无目标、无驻守点、无路径；"不知道干什么" |
| 路径效率 | 0..1 | 上升 | 寻路 episode 直线/实际；太低=绕路或被墙卡 |
| 每回合伤害 | 血量 | 上升 | 双方合计；下降说明交火机会或命中变差 |
| 首杀转化率 | % | 上升 | 拿首杀后赢下该回合的比例（领先利用能力） |
| 下包点人数 | 人 | 上升 | 下包瞬间包点内 T 人数（配合度） |
| 拆包点人数 | 人 | 上升 | 拆包瞬间包点内 CT 人数 |
| AI 冲突/局 | 条 | 下降 | 自相矛盾/双指令冲突；上升=退化 |
| T 胜率 | % | 参考 | 仅方向；±15pp 内视为噪声 |

判断规则：**两项以上同向变好、且没有明显恶化**才算变好；
胜率上升但冲突/发呆上升 = 可疑。