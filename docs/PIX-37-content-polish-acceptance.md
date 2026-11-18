# PIX-37 游戏内容完善（7 项）— 验收记录

> 来源：Sun 2026-11-18 的 7 项内容完善要求
> 基线：提交 `fbae230` + PIX-36 工作区修改
> 实施：2026-11-18
> 本记录只写实测结果；视觉/听觉观感仍需 Sun 目检与耳检的项明确标注。

## 一、回归总览

| 回归 | 通过 | 失败 | 说明 |
|---|---|---|---|
| 主程序编译（`build-tests` / `build-release`） | 通过 | 0 | `cmake --build` EXIT=0 |
| gameplay_tests 默认模式 | **1806** | **0** | `build-tests/PIX37-default.log` |
| gameplay_tests `--experience`（真实桌面 + D3D11） | **2120** | **0** | `build-tests/PIX37-experience.log` |

本轮新增 PIX-37 专项断言 **50 条**，全部通过。其余 1756 / 2070 条为既有回归，无一失败。

## 二、逐项验收

### 1. 拆包范围缩小 + 中间不能有遮挡物

| 项 | 结果 |
|---|---|
| 拆包半径 | `Tuning::DefuseRadius` 由 **52 → 30** 世界单位（贴身，约一个身位多） |
| 遮挡判定 | 新增唯一判定入口 `CS2DGame::IsInDefuseRange(x,y)`：**在半径内 && 视线未被挡弹瓦片阻挡** |
| 三处复用 | 玩家拆包（`CS2DGameObjective`）、BOT 拆包资格（`BotGameBots`）、拆包中禁止开枪（`CS2DGameCombat`）全部改走同一函数，不再各写一套距离比较 |
| 契约测试 | 逐点对照 8 个探针：`IsInDefuseRange == (在半径内 && 视线通畅)`，全部成立 |
| 墙后不可拆 | 拆包者与 C4 之间立墙时判定为不可拆；视线确实被打断（断言 `!HasLineOfSight`） |
| 旧距离失效 | 旧 52 单位距离内的点位现在判为不可拆 |

README 玩法清单已同步（拆包半径、BOT 拆包音距离都由 52 改为 30）。

### 2. 火焰贴图尺寸 + 移除实时渲染

| 项 | 结果 |
|---|---|
| 单块贴图尺寸 | 由固定 32 世界单位改为**按火区半径自适应**：`tile = max(8, 0.26·r)` 并向上取 2 的倍数 |
| 面积约束 | 单块贴图面积 ≥ 圆面积的 **1/64**（`0.26r` 对应 1/47，留有余量；理论下限 `r·√(π/64) ≈ 0.2216r`）。四个半径档（30/60/90/140）逐一断言 |
| 上限约束 | 单块仍小于整个圆（不会一块贴图盖满全场） |
| 实时渲染下线 | 整段「矢量火焰舌 + 向上飘散火星」的实时绘制**已删除**；`DrawFireZone()`（纯程序化火团+火星）连同声明一并删除（全仓无调用点） |
| 唯一路径 | 火区只剩「地图贴图逻辑」一条渲染路径：`DrawFireCells` 用三张无缝火焰贴图平铺，随火区寿命淡入淡出 |

视觉证据：`build-evidence/pix37/fire-tiles.png`（可见少量大地图贴图形成的火舌，而非满屏碎块）。

### 3. 准备阶段瞄准线虚化

| 项 | 结果 |
|---|---|
| 根因 | 移动模糊 `aimMotionBlur_` 只在 `UpdatePlayerMovement` 内推进；回合冻结 / 结算阶段不调用它，模糊值一直**冻结在上一回合跑动时的残留**，瞄准线被 `DrawSoftSegment` 的 blur 参数拉宽成虚影 |
| 修复一（语义） | 新增 `CS2DGame::IsMovementAllowed()`；`CS2DOverlay` 只在真能移动时把模糊传给 `DrawAimLine`，不能动时传 0 |
| 修复二（状态） | 不可移动阶段每帧把 `aimMotionBlur_` 与 `crosshairBloom_` 指数收回 0（12/s，低于 0.002 直接归零） |
| 断言 | 人为把模糊拉到 5.0，冻结下走 40 帧后 `GetAimMotionBlur() < 0.05`、`GetCrosshairBloom() < 0.05`；解冻后 `IsMovementAllowed()` 恢复 true |

### 4. BOT 带包标志

| 项 | 结果 |
|---|---|
| 世界内 | 旧的**黄色菱形**标记（`DrawBombCarrierMarker` 的菱形几何）不再调用；改为 `DrawBackpackC4()`：把 `c4.png` 贴到人物**后背**（沿朝向反方向偏移 0.34 身位），随朝向 90° 同步旋转，尺寸 0.62 身位（小于人物，不遮挡角色本体） |
| 渲染方式 | 刻意**不用** `MaskedCopy`（那条路径按人物剪影逐像素剔除，会把背包切出方块缺口）；改用整块 `SDL_RenderCopyExF` |
| 雷达 | 携带者与掉落物都由旧的菱形/方块改为**很小**的真实 C4 图标：携带者约 7px、掉落约 10px（随 HUD 缩放）；携带者不再额外画彩色方块，掉落物保留橙色底点 + 脉动圆环 |
| 掉落 | C4 掉在地上时雷达直接显示该小图标（`DroppedBomb` 分支） |
| 素材缺失兜底 | 橙点兜底，绝不回退成旧菱形 |
| 断言 | `ActorHasC4` 识别携带者；`CollectRadarDots` 分别产出 `Carrier` 与 `DroppedBomb` 形状 |

视觉证据：`build-evidence/pix37/carrier-backpack.png`（四个朝向从后背到身侧依次转动）、`build-evidence/pix37/radar-c4-icons.png` 与放大版 `radar-c4-icons-zoom.png`（可辨认的小 C4 图标，无菱形）。

### 5. 取消人物移动晃动

| 项 | 结果 |
|---|---|
| 根因 | `DrawCharacter` 在 `animState == Walk` 时按 `sin(animTimer·12)` 给角色加了 `WALK_BOB_AMPLITUDE = 1.2` 世界单位的纵向偏移，而瞄准线锚在真实世界坐标上 —— 两者不同步，表现为人跟着晃 |
| 修复 | 删除行走 bobbing（`yOff` 恒为 0），同时删掉已无用的 `WALK_BOB_AMPLITUDE` 常量（避免留死代码） |
| 断言 | 逐像素量角色包围盒：Idle 与 Walk 的 5 个相位（含多个 `sin` 极值）**高度完全相等** |

### 6. 烟雾视觉范围 + 人物渐隐渐显

| 项 | 结果 |
|---|---|
| 视觉/判定错位根因 | 判定用 `SmokeEffectiveRadius`（140 半径），渲染也用同一个半径，但渲染是「多团烟拼出的不规则形状 + 外圈 softer」，玩家看到的烟明显比判定大，站在烟里却完全不透明 |
| 扩大渲染 | 新增 `Tuning::SmokeVisualRadiusMul = 1.32`：**只**放大渲染半径，判定半径不变（视线遮挡逻辑不动） |
| 人物渐隐 | 新增 `CS2DGame::SmokeOpacityAt(x,y)` 返回 0..1 连续遮蔽度：径向衰减（内侧 62% 全浓）+ 生长淡入 + 消散淡出，与 `DrawSmokeCloud` 的 fadeIn/fadeOut 完全同口径 |
| 应用位置 | `MakeActorDrawParams` 统一写入 `sp.alpha`（最浓保留约 12% 可见度，不做「凭空消失」），BOT 与玩家、镜内镜外全部自动生效 |
| 渐显 | 烟雾消散时 `SmokeOpacityAt` 同步下降 → 角色自动渐显 |
| 断言 | 中心 > 0.95；视觉半径 > 判定半径；判定半径内侧仍非 0；视觉半径外为 0；中心比边缘浓；消散（life 12→0.35→0.02）遮蔽度单调下降且末段 < 0.2；刚生成（age 0.05）也淡入；无烟时为 0 |

实测数值：`gameplayR = 140.0`，`visualR = 184.8`。

### 7. 暂停 → 继续 的变黑过渡

| 项 | 结果 |
|---|---|
| 根因 | `resumeGame`（按钮"继续"）与 Esc 继续分支都**直接**把 `currentState` 改成 `PLAYING`，绕过了 `BeginTransition`；而 `BeginTransition` 只在 `renderedState != lastRenderedState_` 时由 `Render()` 自动触发，PAUSED→PLAYING 因为某些路径下状态记账已同步而不触发，画面上就是「一黑到底然后突然出现」 |
| 修复 | 两处恢复路径都显式 `BeginTransition(PAUSED, PLAYING)`，与其它页面切换共用同一套黑场软切（含压黑保持段 + 淡入段） |
| 断言 | 按钮恢复后 `currentState == PLAYING`、`transitionKind_ == Fade`、`transitionDuration_ > 0`、`transitionElapsed_ == 0`；Esc 分支同一入口同样进入 Fade |

## 三、证据索引

`build-evidence/pix37/`：

| 文件 | 内容 |
|---|---|
| `carrier-backpack.png` | 带包 BOT 后背 C4，四个朝向（5× 放大便于目检） |
| `radar-c4-icons.png` / `radar-c4-icons-zoom.png` | 雷达上的小 C4 图标（携带者 / 掉落 / 普通友军）与放大版 |
| `fire-tiles.png` | 火焰地图贴图（单块加大后的火区） |
| `smoke-fade.png` | 烟雾中的角色按遮蔽度逐级变淡 |

日志：`build-tests/PIX37-default.log`、`build-tests/PIX37-experience.log`。

## 四、需 Sun 确认

1. **目检**：`build-evidence/pix37/` 下四张图 —— 尤其火焰观感（是否还需要更亮的火心/更大块）与带包背影（比例与位置是否符合预期）。
2. **实测手感**：拆包半径 30 是否合适（贴身但够用）；烟雾渐隐在实机动态下是否自然。
3. **进游戏实测**：暂停→继续的黑场节奏（0.3 秒）是否符合预期。

## 五、说明与观察项

- **拆包半径的可调性**：半径从 52 收到 30 是较大幅度的收紧（面积约为原来的 1/3）。数值集中在 `Tuning::DefuseRadius`，实机若觉得过严可直接调回。
- **烟雾视觉倍率**：`SmokeVisualRadiusMul = 1.32` 让渲染比判定大 32%。这个值决定「看起来有烟」比「真的挡视线」多出多少；若目检觉得边缘太散可下调。
- **火焰贴图下限**：`tile = max(8, 0.26·r)` 中的 8 世界单位是为小半径火区准备的下限；火区半径固定 60 时实际 tile ≈ 16 世界单位（对应单块面积约圆面积的 1/47）。
- **`DrawFireZone` 已删除**：确认全仓无调用点后移除，属于本项「去掉实时渲染逻辑」的必然结果。
- **测试隔离修复**：新增的恢复过渡用例会调用 `ResizeLogicalViewport`，它会改写全局 `g_uiScale`；用例已在结束时恢复原值，避免污染后续 HUD/雷达断言。
