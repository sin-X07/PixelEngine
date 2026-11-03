# baseline —— 确定性基准（开发期）

本目录存放"当前基线"的**汇总**（每局一行）。用途：改动 BOT 逻辑或数值后，
用同一套参数重跑，再用 `compare_benchmark.py` 看是否触发告警。

## 生成

```bash
cmake --build build-dev --target dev_replay_runner
build-dev/dev_replay_runner.exe --ticks=150000 \
    --maps=dust_2,mirage,ancient_ruins --seeds=1-10 \
    --out=build-evidence/baseline-gen --tag=$(git rev-parse --short HEAD)
cp build-evidence/baseline-gen/summary.csv tools/dev_replay/baseline/summary.csv
```

- `--ticks=150000`（≈41 分钟游戏时间）：足够打到 8 胜并留出余量；比赛结束会提前停止。
- 30 局（3 图 × 10 种子）在开发机约 **16 秒**。

## 比较

```bash
build-dev/dev_replay_runner.exe ... --out=build-evidence/current --tag=my-branch
python tools/dev_replay/compare_benchmark.py \
    tools/dev_replay/baseline/summary.csv build-evidence/current/summary.csv
```

告警阈值（`compare_benchmark.py` 内置）：

| 指标 | 阈值 |
| --- | --- |
| 胜场（T 或 CT） | 变化 ≥ 3 局 |
| 平均回合时长 | 变化 ≥ 20% |
| 平均回合数 | 变化 ≥ 2 |

**告警只代表"需要人看"，不代表退化**；阈值内也不代表"绝对强度"——只能同基线相对比较。

## 纪律

1. **基线必须与代码版本绑定**：`summary.csv` 首行的 `tag=` 是产出它的 commit。
   改完代码、确认行为符合预期后，才更新基线（并在 PR 里说明"基线已刷新"）。
2. **参数必须一致**：`ticks / viewport / aimTurn / aimErr / reaction` 任一变化，
   `compare_benchmark.py` 会先告警"参数不同"，这种比较通常没有意义。
3. **同机比较**：跨机器/跨编译器的浮点差异不在保证范围内。
4. 目录里的 `*.hash.txt` 不建议入库（30 局 × 3.5MB ≈ 100MB）；需要定位分歧时临时生成即可。

## 当前基线（首次）

| 项 | 值 |
| --- | --- |
| 局数 | 30（dust_2 / mirage / ancient_ruins × seed 1–10） |
| 胜场 | T 13 / CT 17 |
| 平均回合数 | 13.4 |
| 平均回合时长 | ≈4274 tick（71 秒） |
| 下包 / 拆包（每局） | 10.8 / 8.3 |
| 脚本玩家 K/D | 7.18 |

> 胜场偏向与地图有关（dust_2 偏 CT、mirage 偏 T），这是 BOT/地图的攻防不对称，
> 不是脚本玩家造成的（把脚本玩家近似关掉，dust_2 仍是 CT 8:5）。
