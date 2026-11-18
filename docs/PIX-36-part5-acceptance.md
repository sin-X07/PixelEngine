# PIX-36 玩家体验第五部分修复 — 验收记录

> 计划原文：`docs/PIX-36-part5-experience-plan.md`（Linear PIX-36，GitHub #59）
> 基线：提交 `fbae230` + 工作区修改
> 实施：Codex 2026-11-18
> 本记录只写实测结果；依赖 Sun 目检/耳检的项明确标注为「待 Sun 确认」。

## 一、回归总览

| 回归 | 通过 | 失败 | 说明 |
|---|---|---|---|
| 主程序编译（`build-release`） | 通过 | 0 | `cmake --build` EXIT=0，`EngineApp.exe` 重新链接成功（9,099,913 字节） |
| gameplay_tests 默认模式 | **1751** | **0** | `build-tests/PIX-36-default-final.log` |
| gameplay_tests `--experience`（交互会话，真实桌面 + D3D11） | **2065** | **0** | `build-tests/PIX-36-experience.log`；D3D11 与 HUD 矩阵全部分支执行并通过 |

默认模式与 `--experience` 均实现 0 失败——本轮**没有**再出现既往记录里的「无显示会话建窗失败」（本次 `--experience` 在有桌面的交互会话中运行，窗口、D3D11 渲染器与 HUD 矩阵全部真实创建成功）。

本轮新增 PIX-36 专项断言 **108 条**，全部通过。

## 二、逐项验收

### 1. 开镜倍率 1.5× / 3×

| 项 | 结果 |
|---|---|
| 档位表 | `PresentationContract.hpp`：`kScopeZoomLevels = { 1.0f, 1.5f, 3.0f }`，`kScopeLevelCount = 3` |
| 死代码 | `kScopeZoomMultiplier` 已删除（全仓无引用） |
| 右键循环 | 关 → 1.5 → 3 → 关；6 把带镜枪（SSG 08 / AWP / SCAR-20 / G3SG1 / SG 553 / AUG）逐把断言 level 1 = 1.5×、level 2 = 3× 全部通过 |
| 倍率实测（软件） | 1.5× → 1.48826（误差 **0.78%**）；3× → 2.99553（误差 **0.15%**） |
| 倍率实测（D3D11） | 1.5× → 1.50362（误差 **0.24%**）；3× → 3.0002（误差 **0.007%**） |
| README | 玩法清单「1.5×、2×」已更正为「1.5×、3×」，并补注镜内外玩家同步放大 |

四种测量均 ≤1%，满足计划 §1.3。证据：`build-evidence/experience/scope-magnification-{software,d3d11}.csv`、`engine/scope-1.5x.png`、`engine/scope-3x.png`。

### 2. 每把枪各自的子弹速度（游戏性分档）

逐项改 `WeaponDef.hpp` 的 `bulletSpeed` 列，共 **33 把枪械**（计划写「34 把」，实际计数为 33：霰弹 4 + 手枪 10 + 冲锋枪 6 + 步枪 7 + 机枪 2 + 狙击 4）；Zeus、投掷物、C4、刀不动。

| 类别 | 武器 | 新值 |
|---|---|---|
| 狙击 | AWP | 5600 |
| 狙击 | SSG 08 / SCAR-20 / G3SG1 | 4800 |
| 步枪 | M4A4 / M4A1-S | 3400 |
| 步枪 | SG 553 / AUG | 3300 |
| 步枪 | AK-47 | 3200 |
| 步枪 | Galil AR / FAMAS | 3100 |
| 机枪 | M249 / Negev | 3300 / 3100 |
| 冲锋枪 | MP5-SD / MP7 / P90 | 2500 |
| 冲锋枪 | MP9 / MAC-10 | 2400 |
| 冲锋枪 | UMP-45 | 2300 |
| 手枪 | Desert Eagle / R8 | 2500 |
| 手枪 | Five-SeveN | 2200 |
| 手枪 | USP-S / P2000 / P250 / Tec-9 | 2100 |
| 手枪 | Glock-18 / Dual Berettas / CZ75 | 2000 |
| 霰弹 | Nova / XM1014 / Sawed-Off / MAG-7 | 1600 |
| 不动 | Zeus | 4000 |
| 不动 | 全部投掷物 | 840 |

验收：
- 档位 spot-check 每类抽样断言表值（防位置初始化错位）。
- 排序与真实初速一致：AWP 最快、M4A4 > AK-47、Deagle > Glock-18、步枪 > 手枪，全部通过。
- **高速命中回归**：AWP 5600 px/s 对 1600 px 外目标开火，扫掠检测正常登记命中（无跨帧穿透漏检），30 帧内命中。
- 实机模拟校验：单帧位移 = 表值/60（验证表值真的用于发射）。
- 弹道视觉段仍在（狙击开火没有变成「无弹道瞬移」）。
- 默认回归与 `--experience` 全过，BOT 闪避/命中测试未受影响。

### 3. 观战自由视角：队友视锥并集 + 修朝向 bug

| 项 | 结果 |
|---|---|
| 多观察者接口 | `VisibilityQuery::ComputeVisibilityUnion(Observer*, count, x, y)` 返回各观察者最大值；`kMaxObservers = 8` |
| 单观察者一致性 | 单观察者并集与标量路径**逐位相同**（断言 `single == union1`），无行为漂移 |
| 自由视角观察者 | `CS2DGame::CollectFreeSpectateObservers` 收集全部存活同阵营 BOT |
| 朝向 bug | `GetSpectatedPlayer()` 自由模式返回 `nullptr`；观察者列表与旧跟随目标朝向**无关**（断言并集结果不随 `specTarget_` 变更） |
| 雾与可见性同源 | `DrawWorldPass` 用同一个 `visibilityAt` 判定，雾走 `DrawVisionFogUnion`；烟雾遮挡按各观察者单独判断（任一观察者连线无烟即不算被挡） |
| 死代码 | `SpectatorCanSeeEnemy` 已删除（定义 + 声明） |
| 跟随 / C4 模式 | 行为不变，现有测试全过 |
| 观战 HUD | 自由模式显示「自由视角 · 队友共享视野」；`GetSpectatedPlayer()` 为 null 时 Engine 自动回落到阵亡玩家本人（冻结态）状态 |
| 雾性能 | 4 观察者并集单像素代价 = 单观察者的 **1.93×**（默认）/ **2.47×**（`--experience`），远低于 4× 上界——`ComputeVisibilityUnion` 在 `vis >= 1.0f` 时短路 |

几何层断言：并集 = 各观察者最大值、两侧同时点亮、超出所有半径则全暗、空列表全雾；游戏层断言：队友朝向相反两人都被收录、观察者数量与最后跟随目标无关。

### 4. 脚步声按移动距离触发（玩家与 BOT 同一套）

| 项 | 结果 |
|---|---|
| 步幅 | `Tuning::FootstepStride = 28.0f`（= 2 × PlayerSize），`FootstepMaxPerFrame = 2` |
| 触发方式 | 按**实际位移**累计（非输入速度），满步幅触发一步，**余数保留**（`stride -= 步幅`） |
| 步数测试 | 走 560 px（20 个步幅）实测步数与 `距离/28` 误差 ≤1 步 |
| 单帧上限 | 一次瞬移 5 个步幅只触发 ≤2 步（防卡顿连响） |
| 卡墙 | 有速度、无位移 → **0 步**（顶墙不响） |
| 潜行 | 不累计、累计器保持 0 |
| 开镜变慢 | 同一时间窗内开镜行走距离 < 不开镜的 85%（步频随减速变慢） |
| BOT 同一套 | 每只 BOT 各自累计器，步频 ≈ 自身移速/28；阵亡重置 |
| 视听同频 | 一声脚步 = 一条听觉事件（同一循环内触发） |
| 循环退役 | `FOOTSTEP_LOOP` 枚举、`CHANNEL_FOOTSTEP_LOOP`、`footstepLoopSound`、`Play/StopFootstepLoop()` 全部删除；`Mix_ReserveChannels` 恢复为 **12**；`assets/audio/sfx_footstep_loop.wav` 已删除 |

**单步音（任务 8）**：`assets/audio/sfx_footstep.mp3` 实测 **0.110 秒**（目标 0.10–0.12 秒）、**峰值 −3.0 dBFS**（目标 −3 dBFS）、44.1 kHz 立体声、单步干净无尾音。客观指标全部达标。

### 5. 拆包进度仅队友可见（只剩秒数）

| 项 | 结果 |
|---|---|
| 新增字段 | `ictx.defuseVisibleToViewer`，由 `CS2DGame::IsDefuseVisibleToViewer()` 按拆包者与观看者阵营填充 |
| 队友侧文案 | 「拆包中 · 剩余 X.X 秒」——**无名字、无百分比** |
| 对手侧 | 整行不绘制；「C4 已安装」横幅纵位置按**可见的**拆包状态切换，对手侧恒定不动（无布局侧信道） |
| 观战 | 阵亡玩家按**自己阵营**判断（CT 阵亡看得到，T 阵亡看不到）——断言通过 |
| 拆包声 | 未改动 |

截图证据（1920×1080）：
- `hud-defusing-ally-1920x1080-100.png`：横幅「C4 已安装」+ 下一行「拆包中 · 剩余 1.5 秒」
- `hud-defusing-enemy-1920x1080-100.png`：**只有**「C4 已安装」，横幅位置与队友侧一致

两张对照图视觉语言统一（沿用现有字体/字号/颜色/位置），无新增样式。

### 6. 开镜时玩家在镜内同步放大

| 项 | 结果 |
|---|---|
| 镜内画玩家 | `DrawWorldPass(true)` 中玩家与 BOT 走同一套 `MakeActorDrawParams`，`lens` 不再排除玩家 |
| 镜外保留原大小 | 镜面合成后仅当玩家在镜圈**外**（距离 > 镜圈世界半径 + 玩家半径）才贴主场景比例的那一份 |
| 无重影 | 镜内/镜外二选一，跨镜圈边缘无一大一小双影 |
| 尺寸比实测 | 1.5× → 1.46154（误差 **2.56%**）；3× → 2.94872（误差 **1.71%**），均 ≤10% |

测量方法是**真实绘制路径的像素测量**（同一角色分别画在主场景与镜内，读回像素量屏上高度），不是公式换算。证据：`build-evidence/experience/lens-player-ratio.csv`。

### 7. 阵亡后 1.8 秒内画面继续直播（删除定格快照）

| 项 | 结果 |
|---|---|
| 快照机制 | `deathWorldTex_`、`deathWorldEvent_`、`ClearDeathSnapshot()`、`DrawGameplayWorld()` 全部删除（成员 + 定义 + 4 处调用点） |
| 直播渲染 | `Engine::Render()` 在定格期直接 `cs2dGame->Render(logicalW_, logicalH_)`，世界每帧重画 |
| 镜头固定 | `PrepareCamera` 定格期提前返回不变；实测两帧相机 X/Y/zoom **完全相同** |
| 世界在动 | 相隔 0.5 秒的两帧像素**不同**（断言 + 截图 SHA256 不同） |
| 其余逻辑 | 1.8 秒时长、击杀卡、定格期 HUD、切观战、结算等待均不变 |
| 改尺寸 | 定格期改尺寸后逻辑画布/渲染器同步，且定格不提前结束 |

测试由「逐像素一致」改为直播三断言：(a) 两帧不同、(b) 相机相同、(c) 倒计时走完切观战。证据：`engine/death-live-frame-a.png`、`engine/death-live-frame-b.png`、`engine/death-card.png`。

### 8. 单步音重新生成

见第 4 项末段：`sfx_footstep.mp3` = 0.110 秒、峰值 −3.0 dBFS、单步无尾音，客观指标达标。
**需 Sun 耳检**（本人无法判断听感）。

### 9. 全量回归与证据

见「一、回归总览」。证据索引见下节。

## 三、证据索引

`build-evidence/experience/`：

| 文件 | 内容 |
|---|---|
| `scope-magnification-software.csv` | 软件渲染镜内倍率（1.5× 0.78%、3× 0.15%） |
| `scope-magnification-d3d11.csv` | D3D11 镜内倍率（1.5× 0.24%、3× 0.007%） |
| `lens-player-ratio.csv` | 镜内玩家尺寸比（1.5× 2.56%、3× 1.71%） |
| `engine/scope-1.5x.png` / `engine/scope-3x.png` | 镜内实景（含镜内玩家） |
| `engine/spectator-free-union.png` | 观战自由视角并集（两块亮区 + 「自由视角 · 队友共享视野」） |
| `engine/death-live-frame-a.png` / `-b.png` | 定格期两帧对照（像素不同、镜头相同） |
| `engine/death-card.png` | 击杀卡 |
| `engine/c4-spectator.png` | C4 观战 |
| `hud/hud-defusing-ally-*.png` | 队友侧拆包（只有剩余秒数） |
| `hud/hud-defusing-enemy-*.png` | 对手侧拆包（完全隐藏、横幅不动） |
| `camera-motion.csv` | 相机过渡轨迹（回归用） |

## 四、未闭合项（需 Sun）

1. **目检**：`engine/` 下镜内 1.5×/3×、观战并集、定格期两帧对照、`hud/` 下 T/CT 双视角拆包截图，均待 Sun 目检通过。
2. **耳检**：新单步音 `assets/audio/sfx_footstep.mp3` 的听感（节奏像走路而非节拍器、无爆音）待 Sun 耳检。
3. **进游戏实测手感**：AWP 开镜远距离命中、各档位速度差异可感知度。计划 §2.4 明确「档位数值以 Sun 实测为准，可微调，不计为返工」。

## 五、观察项（不阻塞验收，Sun 定夺）

**3× 镜内画面块状感**：把镜头放大到 3× 后，地图底图（`assets/cs2d/tilesets/ancient_ruins.png`，1952×1824）被放大到约 9 倍绘制，出现可见的方块状像素化。

- 这是**素材分辨率限制**，不是本次改动引入的回归：改动前的 2× 基线截图（`engine/scope-2x.png`，2026-11-17 20:55 产物）块状感同样明显。
- 计划 §1.4 要求「3 倍镜内画面必须清晰可用（重渲染非拉伸放大，纹理不糊）」。当前是**重渲染 + 双线性**，非拉伸放大，但底图本身分辨率不足，1.5× 清晰、3× 发糊。
- 可选后续做法（均超出本批「不做」范围，需 Sun 决定）：为 3× 档单独烘焙更高分辨率底图；或在设置里把渲染缩放与镜内倍率解耦。本批未擅自改动美术管线。
