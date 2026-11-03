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
