# 确定性哈希校验 · 操作手册

> 面向"改完 BOT 逻辑，想知道是变好还是变差"的日常使用。
> 工具源码在 `tools/dev_replay/`（外挂，可整目录删除）；本文只讲怎么用。

---

## 0. 先搞清楚它能回答什么

| 它能回答 | 它不能回答 |
| --- | --- |
| 同一份代码 + 同一种子，两次运行是否**逐 tick 完全一致** | 玩家"手感"好不好 |
| 改动后行为**从哪里开始**和基线不一样（精确到 tick） | 人类对手下的真实强度 |
| 改动后**指标**（胜率/回合/移动/交火/目标）有没有变 | 战术"意图"是否漂亮（比如交叉火力） |
| 决策有没有**自相矛盾**（冲突检查） | 跨机器/跨编译器的浮点一致性 |

**最重要的一条**：哈希值本身**不是**变好/变差的判据。代码一改哈希必然变——那是预期。
哈希只用来保证"同一输入必然同一结果"，判断好坏靠第 5 节的指标。

---

## 1. 一次性准备（构建 dev 版）

```powershell
cd 'D:\BJTU\Cpp\Outstanding Projects\PixelEngine'

cmake -S PixelEngine -B build-dev -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
      -DBUILD_DEV_REPLAY_TOOLS=ON `
      -DFETCHCONTENT_SOURCE_DIR_SDL2="$PWD\build-release\_deps\sdl2-src" `
      -DFETCHCONTENT_SOURCE_DIR_SDL2_MIXER="$PWD\build-release\_deps\sdl2_mixer-src" `
      -DFETCHCONTENT_SOURCE_DIR_SDL2_IMAGE="$PWD\build-release\_deps\sdl2_image-src"

cmake --build build-dev --parallel 8
```

产物：
- `build-dev\dev_replay_runner.exe` —— 无头跑仿真、算哈希、出指标
- `build-dev\EngineApp.exe` —— 带 `--record` 的 dev 版游戏（真人录制用）

> 离线环境必须带三个 `FETCHCONTENT_SOURCE_DIR_*`，指向已有 `build-release\_deps`，否则 CMake 会尝试联网。

只跑回归测试的话不需要 dev 包：`cmake --build build-tests --target gameplay_tests`。

---

## 2. 流程 A：改完 BOT 逻辑，判断变好还是变差（主力流程）

### A-1 改代码前，确认基线是最新的

```powershell
# 基线在 tools/dev_replay/baseline/summary.csv（已入库，tag=产出它的 commit）
Get-Content tools\dev_replay\baseline\summary.csv -TotalCount 2
```

第一行 `# PXBENCH1 tag=...` 就是基线对应的 commit。**如果 tag 和你当前 base 不一致**
（比如别人合了新东西），先按第 6 节重刷基线，否则对比没有意义。

### A-2 跑当前代码（60 局，约 30 秒）

```powershell
build-dev\dev_replay_runner.exe --ticks=150000 `
  --maps=dust_2,mirage,ancient_ruins --seeds=1-20 `
  --out=build-evidence\bench-current --tag=$(git rev-parse --short HEAD)
```

输出目录里每局有 `hash.txt`（~1 MB）、`rounds.csv`、以及总的 `summary.csv`。
**日常只需要 `summary.csv`**，其余可直接删。

### A-3 与基线比较

```powershell
python tools\dev_replay\compare_benchmark.py `
  tools\dev_replay\baseline\summary.csv `
  build-evidence\bench-current\summary.csv
```

正常输出长这样：

```
[compare] T 胜率 45.0% -> 46.7% (+1.7pp)   [T 27/60 -> 28/60]
[compare] 平均回合数                  13.3 ->       13.4   (+0.10)
[compare] 平均回合时长(tick)           4281 ->       4290   (+9.00)
...
[compare] ---- 决策质量 ----
[compare] 原地不动占比                 35.1% ->       34.8%   (-0.30)
[compare] 发呆占比                   10.2% ->        9.8%   (-0.40)
[compare] 路径效率                   0.63 ->       0.65   (+0.02)
[compare] 每回合伤害                   792 ->        810   (+18.00)
...
[compare] OK：未触发阈值（不代表绝对强度，只说明与基线接近）
```

退出码 `0` = 无告警，`1` = 有告警（脚本里可直接用）。

### A-4 判读（关键）

| 看到的现象 | 结论 |
| --- | --- |
| 质量指标 2 项以上同向变好，且冲突没增加 | 大概率变好 |
| 胜率↑ 但发呆/冲突也↑ | **可疑**：拿错误换数据，去流程 C 看具体回合 |
| 全部指标几乎不动 | 改动大概率没生效（确认新分支真的走到了） |
| 回合时长↓ 20%+ | 先分清是"进攻更快"还是"双方更快送死"（看下包率与击杀数） |

**只看胜率是错的**：60 局样本下 ±15pp 都在噪声范围。

---

## 3. 流程 B：只改"可能影响确定性"的东西（双跑校验）

适用：动了随机调用、浮点运算顺序、容器遍历、新增状态字段、时间相关逻辑。

```powershell
# 同一个种子跑两遍
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2 `
  --hash=build-evidence\run-a.txt
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2 `
  --hash=build-evidence\run-b.txt

python tools\dev_replay\diff_hash_logs.py build-evidence\run-a.txt build-evidence\run-b.txt
```

期望输出：

```
[diff] baseline: 72114 tick, current: 72114 tick
[diff] 完全一致 OK
```

**出现不一致 = 你引入了不确定性，先修这个**，别去看胜率。常见原因见第 7 节。

> 建议同时跑 2–3 个种子（1、2、3）和 2 张地图，覆盖不同 RNG 路径。

---

## 4. 流程 C：定位"从哪个 tick 开始不一样"

前提：你有基线的 `hash.txt`（或改代码前的 `hash.txt`）。

```powershell
# 1) 找首个不一致 tick
python tools\dev_replay\diff_hash_logs.py baseline.hash.txt current.hash.txt
# → [diff] 首个不一致 tick=18342
#        baseline=...
#        current =...

# 2) 在分叉点附近导出双方状态
build-dev\dev_replay_runner.exe --ticks=20000 --seed=1 --map=dust_2 `
  --dump=build-evidence\cur.dump

# 3) 对比 dump（每行一个 tick，字段同第 5 节说明）
#    与基线时期导出的 dump 逐行比，找第一个不同的字段
```

dump 一行包含：`tick, 回合, 阶段, 玩家x,y, 弹药, 金钱, 存活, bot0坐标/血量,
双方教练计划(intent/gen/target/tempo), bot0 姿态/机动/驻守点, 子弹/手雷/烟/火数量, 玩家朝向`。

**问题要落到具体一句**：*"在 tick 18342，3 号 BOT 的目标点从 A_Site 变成了 Mid"*。
这比"胜率掉了 2%"有用得多。

### 更彻底的复现：输入日志

```powershell
# 脚本玩家整局录制（带 hash，可严格校验）
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2 `
  --journal=build-evidence\s.journal --hash=build-evidence\rec.txt

# 回放（会自动比对日志里的 final_hash）
build-dev\dev_replay_runner.exe --replay=build-evidence\s.journal `
  --hash=build-evidence\rep.txt
# 期望：[replay] MATCH：输入 + 种子完整复现该局
```

真人录制（dev 版游戏）：

```powershell
build-dev\EngineApp.exe --autostart --record=build-evidence\human.journal
# 玩一局后正常退出；再回放：
build-dev\dev_replay_runner.exe --replay=build-evidence\human.journal
# 期望：[replay] NO_EXPECTED_HASH（真人日志没有 final_hash，只回放不校验）
```

---

## 5. 指标字典（判断好坏的主要依据）

| 指标 | 单位 | 变好 = | 怎么读 |
| --- | --- | --- | --- |
| 原地不动占比 `still_pct` | % | ↓ | 站着不动且没交战；太高 = 不敢动 / 卡住 |
| 发呆占比 `idle_pct` | % | ↓ | 无目标、无驻守点、无路径；"不知道干什么" |
| 路径效率 `path_eff` | 0–1 | ↑ | 寻路 episode 的直线/实际；太低 = 绕路或被墙卡 |
| 每回合伤害 `dmg_per_round` | 血量 | ↑ | 双方合计；下降 = 交火机会或命中变差 |
| 首杀转化率 `fb_win_pct` | % | ↑ | 拿到首杀后赢下该回合的比例 |
| 下包点人数 `plant_near` | 人 | ↑ | 下包瞬间 220px 内 T 人数（配合度） |
| 拆包点人数 `defuse_near` | 人 | ↑ | 拆包瞬间 220px 内 CT 人数 |
| AI 冲突/局 `ai_conflicts` | 条 | ↓ | 自相矛盾 / 双指令冲突；上升 = 退化 |
| T 胜率 | % | 参考 | 只做方向；±15pp 内算噪声 |

### 逐回合看细节

`rounds.csv` 一行一个回合，主要列：

```
round, winner, ticks, kills_t, kills_ct, plant, defuse,
first_blood_tick, first_blood_team, fb_win,
alive_t_end, alive_ct_end, money_t_start, money_ct_start,
still_t, still_ct, idle_t, idle_ct, path_eff_t, path_eff_ct,
damage_t, damage_ct, money_t_end, money_ct_end,
buy_t, buy_ct, plant_near_t, defuse_near_ct
```

`buy_*` 是买计划档位：`0=Eco 1=Half 2=Force 3=Full 4=AntiEco`。

**用法**：`summary.csv` 某项告警 → 打开那几局的 `rounds.csv` → 看是哪几回合拉低的。

---

## 6. 基线管理（最容易做错的地方）

### 什么时候刷新基线

只有**确认新行为是期望的**之后才刷新：

1. 流程 A 通过（指标方向符合预期，或变化你能解释）；
2. PR 描述里写清"改了哪个指标、变化多少、为什么这是预期"；
3. 合并到 main 后，用新 commit 重跑 60 局并覆盖基线。

### 刷新命令（一条）

```powershell
powershell -ExecutionPolicy Bypass -File tools\refresh_baseline.ps1
```

它做四件事：跑 60 局（约 30 秒）→ 旧基线备份为 `summary.prev.csv` →
覆盖 `tools\dev_replay\baseline\summary.csv` → 打印**新旧指标对照**和下一步 git 命令。

可选参数：

| 参数 | 作用 |
| --- | --- |
| `-Seeds 1-10` | 只跑 30 局（更快，但噪声更大） |
| `-Tag v0.3.0` | 把版本号写进基线头（默认当前 commit） |
| `-Runner <path>` | 用别的 runner |

真实输出示例：

```
==> 新旧基线对照
    指标             旧 tag=6a8554b 新 tag=137839a         变化
    局数                       60           60       0.00
    T 胜率                  45.0%        40.0%      -0.05
    下包/局                  10.72        10.82      +0.10
    原地不动                   35.1         30.0      -5.14
    发呆                     10.2          0.5      -9.72
    路径效率                   0.63         0.47      -0.16
    每回合伤害                   792          795      +2.45
    AI 冲突/局                1.05         0.05      -1.00
```

确认无误后：

```powershell
git add tools/dev_replay/baseline/summary.csv
git commit -m "chore(baseline): 刷新基准 tag=<sha>"
git push
```

> 注意：**基线要在行为改动合并到 main 之后、在 main 上刷新**。
> 在功能分支上刷新会得到指向临时 commit 的基线，合并后就对不上了。

### 纪律

- **不要**为了让告警消失去改 `compare_benchmark.py` 的阈值。
- **不要**用"当前代码自己跟自己比"当作回归通过——那只验证了确定性。
- 基线只保证**同机器**可比（浮点/编译器差异不在范围）。
- 脚本玩家策略（`--aim-turn/--aim-error/--reaction`）是基线的一部分，改它 = 改基线。

---

## 7. 故障排查

| 症状 | 原因 | 处理 |
| --- | --- | --- |
| `[diff] 首个不一致 tick=0` | 种子/地图/视口参数不同 | 检查两次命令的 `--seed` `--map` `--viewport` 是否一致 |
| `[diff] 头部参数不一致` | 两次跑的脚本策略不同 | 参数不同时哈希本来就不同，先对齐参数 |
| 首次不一致在几百 tick 内 | 新增/删除了一处 `rng_()` 调用 | 视觉特效消耗同一条随机流也会错位；见 README"确定性纪律" |
| 分叉发生在"改了一个浮点表达式"之后 | 浮点运算顺序变了（`a+b+c` vs `a+(b+c)`） | 保持原顺序，或确认可接受 |
| `[replay] MISMATCH` | 日志缺了某类输入（购买/切枪/开镜） | 检查录制是否覆盖该动作；浮点是否 `%.9f` 以上精度 |
| `[replay] NO_EXPECTED_HASH` | 真人录制不产哈希 | 正常；要严格校验用脚本玩家路径 |
| `地图加载失败: ...` | 资源路径不对 | 从仓库根目录运行，或确认 `assets/cs2d/maps/*.tmj` 存在 |
| `compare` 报"参数不同" | `aimTurn/aimErr/reaction/viewport` 与基线不一致 | 对齐参数，或按新参数重建基线 |
| 60 局跑得比 30 秒慢很多 | 开了 `--hash`（每局写 1.5 MB） | 日常只需 `--out`，不要 `--hash` |

---

## 8. 冲突检查单独看

```powershell
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2 `
  --conflicts=build-evidence\conflicts.csv
```

输出示例：

```
[conflicts] 回合-实例数 = 2（dual_goal×2 @tick10280）
```

规则含义：

| 规则 | 含义 |
| --- | --- |
| `defusing_moving` | 拆包中却在明显移动 |
| `defusing_unplanted` | 交战期拆一个没下包的包 |
| `path_broken` | 寻路索引/目标连续 0.5s 对不上 |
| `dual_goal` | 小队 hold 与教练 plan hold 相距 >160px |
| `duplicate_hold` | 两人抢同一点（噪声大，需 `--noisy-conflicts`） |

同一 (回合, BOT, 规则) 每回合只报一次，所以"条数上升"就是真信号。

---

## 9. 命令速查

```powershell
# 单局
build-dev\dev_replay_runner.exe --ticks=150000 --seed=1 --map=dust_2

# 单局 + 逐 tick 哈希
... --hash=out.hash.txt

# 批量 60 局（约 30 秒）
... --maps=dust_2,mirage,ancient_ruins --seeds=1-20 --out=dir --tag=SHA

# 比较
python tools\dev_replay\compare_benchmark.py baseline\summary.csv out\summary.csv

# 哈希 diff
python tools\dev_replay\diff_hash_logs.py a.txt b.txt

# 冲突检查
... --conflicts=conflicts.csv [--noisy-conflicts] [--conflict-ignore=dual_goal]

# 录制 / 回放
... --journal=s.journal --hash=rec.txt
... --replay=s.journal --hash=rep.txt

# 状态 dump（定位分叉）
... --dump=dump.txt

# 真人录制（dev 版游戏）
build-dev\EngineApp.exe --autostart --record=human.journal
```

参数默认值：`--ticks=150000`（≈41 分钟游戏时间，打到 8 胜会提前停）、
`--viewport=1728x1080`、`--aim-turn=3.0`、`--aim-error=5.0`、`--reaction=15`。

---

## 10. 日常纪律（三条就够）

1. **一次只改一处**，改完立刻跑 A 或 B；两处叠在一起，分叉定位难度平方增长。
2. **不确定就先跑 B**（双跑一致），再跑 A（指标对比）。
3. **产物即弃**：`build-evidence` 是草稿区（已 gitignore），只留 `summary.csv`；
   每次批量跑完把目录整个删掉，需要时再生成。

---

## 11. 想彻底删掉这套工具时

```powershell
rm -r tools/dev_replay
# 再删 PixelEngine/CMakeLists.txt 里 BUILD_DEV_REPLAY_TOOLS 那一段
git grep PIXEL_DEV_          # 应为 0（Engine 里的录制代码全部包在 ifdef 里）
cmake --build build-release  # 通过
cmake --build build-tests --target gameplay_tests   # 182 通过
```

生产侧唯一保留的是 `CS2D/Visibility/VisibilityQuery.*`——它让玩法层不再依赖渲染器，
是长期改进，不做工具也该有。
