# dev_replay —— 开发期确定性 / 回放工具（**非生产代码**）

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

真人录制（`--record`）需要 Engine 侧打点，尚未接入；当前用同一份格式记录脚本玩家，
已验证"输入 + 种子"能逐 tick 完整复现（dust_2 / mirage 各一局，final_hash 一致、diff 0 行）。