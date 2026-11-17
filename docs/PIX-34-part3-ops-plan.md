# 玩家体验第三部分：操作与交互（G27–G42）实施计划

> 状态：已定稿，待发 Linear PIX-34。Sun 2026-11-16 11:05 逐项定稿。
> 项目：3D 素材与玩法扩展。Backlog，指派 Sun。Codex 实施。
> 核实基准：本计划所有行号均为 2026-11-16 自 Sun 电脑工作区现取代码核实（`D:\BJTU\Cpp\Outstanding Projects\PixelEngine`），不是审计时的旧行号；凡审计引用与现状不符处，均已在文中标明“现状已变”。
> 完整版在仓库 `docs/PIX-34-part3-ops-plan.md`；Linear PIX-34 只放精简任务清单。

## 决定总览（Sun 2026-11-16）

| 项 | 优先级 | Sun 的决定 | 本计划的处理 |
|---|---|---|---|
| G27 买枪菜单不暂停，挑枪时站着挨打 | P1 | 不做改动 | 无任务，记录在案 |
| G28 购买区无标记，区外按 B 一片灰 | P1 | 只在购买区可以召唤出购买面板，顺手删除在购买区外关于购买面板的相关内容 | 门禁 + 删除区外相关 UI |
| G29 近战 360° 无朝向 | P1 | 按照计划修改（±60° 朝向扇形） | 玩家 + BOT 近战都加扇形 |
| G30 手雷扔不远、无落点预览 | P1 | 投掷规则之前已改过，落点 = 投掷时准星位置；仔细检查当前代码逻辑，保持更改后的规则 | 核实并保持，不改规则 |
| G31 玩家下包 3 秒、BOT 2 秒 | P1 | 统一 BOT 和玩家下包时间都为 3 秒，先不加进度条 | 统一为 3 秒；BOT 下包进度条暂不加 |
| G32 BOT 反应延迟 = 原地罚站 | P1 | 分析原因，解决 | 原因已定位（见 §G32），只锁开火不锁移动 |
| G33 BOT 受击一帧转身 | P1 | 接受建议 | 受击转向走 maxTurn 平滑 |
| G34 键位全写死，改不了 | P1 | 接受建议；为所有键位提供自定义功能，确保所有键位都能正常工作 | 设置新增“键位”页，全量可绑 |
| G35 主菜单 Esc 直接退出游戏 | P1 | 接受建议 | 主菜单 Esc 无动作；退出走确认页 |
| G36 死斗也要选阵营，选阵营时 Esc 没反应 | P1 | 死斗保留选阵营；在选阵营界面加暂停界面接口，Esc 可正常暂停 | Esc → 暂停菜单；死斗选阵营保留 |
| G37 买不到拆包钳、包点不分 A/B | P2 | 已在 PIX-33 中完成 | 无任务（钳子机制 + 4/10 秒拆包已在代码里） |
| G38 F6 固定 100 单位前视，无提示 | P2 | 取消固定 100 单位前视机制，删除相关内容 | 整个机制删除，不是加提示 |
| G39 被集火死都不知道 | P2 | 不添加人数上限；优化受击方向提示 | 不加集火上限；做方向提示（先接线再加方向） |
| G40 难度无调节接口；后坐力恢复乘了 FOV 倍率 | P2 | 设置界面先加调节接口；剩余相关逻辑由 Muse 补充完善，之后发现问题再着重解决 | 难度接口 + 后坐力语义修正（见 §G40） |
| G41 进对局悄悄切英文输入法 | P2 | 接受建议 | 治本：按键改走 scancode；只在真需要输入时切换；启动时还原残留切换 |
| G42 切页 0.9 秒黑场 | P2 | Sun 已将 0.9 改为 0.3 | 无任务（Sun 已改，见 §G42） |

## 建议顺序

1. **G30 核实**（无改动，顺手做，半小时）→ 2. **G38 删除前视**（先做，F6 从键位表里退出）→ 3. **G29、G31**（小改动，各半天内）→ 4. **G34 键位系统**（地基，G41 依赖它）→ 5. **G35、G36**（菜单/暂停流程）→ 6. **G41 输入法**（依赖 G34 的 scancode 迁移）→ 7. **G32、G33**（BOT 行为，需进游戏实测）→ 8. **G40 难度接口 + 后坐力语义修正** → 9. **G28 购买区门禁、G39 受击方向提示**（收尾，G39 与首批 G44 有重叠，见文末说明）

---

## A. 已完成 / 不做（无 Codex 任务）

### G27（P1）买枪菜单不暂停 —— 不做改动
- 现状：冻结 2 秒 + 购买期 6 秒（`CS2D/Rounds/RoundManager.hpp:21-22`），购买窗 15 秒（`Tuning.hpp:65` `BuyWindowSeconds = 15.0f`）；`Engine.cpp` 的对局 `Update` 不因买枪菜单打开而停；15 秒一到菜单自动关闭（`Engine.cpp:863-864`）。
- 决定：Sun 说不做改动。保持现状，不收购买窗、不加“正在暴露”提示。
- 记录在案即可，不要顺手改。

### G37（P2）拆包钳 —— 已在 PIX-33 中完成
- 现状已核实：`CS2DGameObjective.cpp:226` 拆包耗时已是“有钳 4.0 秒 / 无钳 10.0 秒”（`player_.hasDefuseKit` 分支），买枪菜单 $400 CT 专用钳子已在 PIX-33 交接里。无任务。
- 注意：审计原文的“拆包永远 5 秒 / DEFUSE_TIME = 5.0f”已过时，现状 `DEFUSE_TIME = 10.0f`（`CS2DGameObjective.cpp:14`，无钳默认值）。

### G42（P2）切页 0.9 秒黑场 —— Sun 已改
- 现状已核实：`Engine.cpp:1638` `transitionDuration_ = 0.3f`，Sun 已把 0.9 改成 0.3。无任务。
- 注意：审计提到的另外两处耗时（进图三段约 1.2 秒、比赛结束 1.2 秒沉淀）是独立计时，不属于本次 G42 的改动范围，不动。

---

## B. 核实并保持（G30，无行为改动）

### G30（P1）投掷规则 —— 仔细检查后确认：新规则已在代码里，保持，不改
Sun 要求仔细检查当前代码逻辑。检查结果如下（`CS2DGameCombat.cpp` 现取）：

**当前规则（已改过，与 Sun 说的一致）：落点 = 投掷时准星的位置。** 实现链：
1. `ThrowGrenade(Player&, float aimAngle, AudioManager*, float targetDistance = -1.0f)`（声明 `CS2DGame.hpp:1294`，定义 `CS2DGameCombat.cpp:455`）。
2. 玩家调用点 `CS2DGameCombat.cpp:138` 不传 `targetDistance`，走默认分支：`distance = hypot(aimWorldX_ - thrower.x, aimWorldY_ - thrower.y)`，即准星距离（`:473-476`）。
3. 出手速度 `speed = clamp(NadeFriction * distance, NadeThrowSpeedMin, NadeThrowSpeedMax)` = `clamp(2.0 × 准星距离, 360, 920)`（`Tuning.hpp:59-60`）。
4. 飞行摩擦是指数衰减（`UpdateGrenades`，`g.vx -= g.vx * NadeFriction * dt`），代码注释写明“`v0/friction ≈ 总投掷距离`”——初速 2d、摩擦 2.0，总飞行距离 ≈ d，即准星距离。落点就是准星位置（无墙阻挡时）。
5. BOT 调用点同样走这套公式：`CS2DGameBots.cpp:518` 传 `sqrt(d2)`（到目标距离）、`:927` 传 `hypot(aimX - bot.x, aimY - bot.y)`，没有旧规则残留。
6. `Tuning.hpp:122-123` 的 `NadeThrowMin/Max = 180/460` 未作废，仍在 BOT 投掷决策门限用（`CS2DGameBots.cpp:924`）。

**两个核实点（只核实，不改）：**
- 引信 1.4 秒（`g.fuse = 1.4f`）会截断最远投掷：460 距离的雷 1.4 秒内约飞 432（指数衰减尾巴），落点比准星近约 28 单位。这是物理结果不是 BUG，验收时按此预期比对，不要“修”它。
- 落点预警已存在：速度降到 400 以下时广播一次落点（`g.landingWarned`，`CS2DGameCombat.cpp:598-601`），附近敌人会躲落点。

**任务（G30）：** Codex 通读上述调用链确认无旧规则残留；进游戏实测：准星指 200 / 400 处各扔一颗 HE，落点与准星误差在 30 单位内即通过；把结论写进提交说明。**不加落点预览**（Sun 没要），不改速度/摩擦/引信任何数值。

---

## C. 小改动

### G29（P1）近战加朝向扇形 —— 玩家 + BOT 都改
- 现状：玩家近战 `CS2DGameCombat.cpp:155-163` 只判 `距离 < def.range`（45），无角度；BOT 近战 `CS2DGameBots.cpp:1501-1512` 只判 `engageDist < meleeReachNow` + 有视线（`HasLineOfSight`），同样无角度。`meleeReach = Tuning::ai::MeleeReachBase + bot.size * Tuning::ai::MeleeReachPerSize`（`:1278-1279`）。
- 改法：两处统一加约 ±60° 朝向扇形判定——攻击者 `aimAngle` 与指向目标的方向夹角 `cos >= cos(60°) = 0.5` 才命中。扇形半角做成 `Tuning` 常量（建议 `MeleeConeHalfAngleDeg = 60.0f`），不要写死 0.5。
- 验收：站 BOT 身后贴脸挥刀不掉血；正对 60° 内挥刀命中；BOT 背对玩家时挥刀不命中。刀的挥动动画/音效（`shootAnimTimer = 0.12f`、刀风声）不受影响——挥空也照常播。

### G31（P1）下包时间统一为 3 秒 —— 先不加进度条
- 现状：`CS2DGameObjective.cpp:13` `PLANT_TIME = 2.0f`；玩家分支 `plantProgress_ >= 3.0f`（`:88`）；BOT 携带者分支复用 `defuseProgress_ >= PLANT_TIME`（`:119-120`），即 BOT 2 秒、玩家 3 秒。HUD 玩家下包进度 `Engine.cpp:2297` `GetPlantProgress() / 3.0f`。
- 改法：
  1. `PLANT_TIME` 改为 `3.0f`；玩家分支的硬编码 `3.0f` 改为 `PLANT_TIME`（两处同一常量，杜绝再次写偏）。
  2. HUD 除数 `3.0f` 保持（与新常量一致），或顺手改成 `PLANT_TIME`。
  3. **先不加进度条**：BOT 下包继续复用 `defuseProgress_` 做计时，不新增独立进度变量、不在 HUD 显示 BOT 下包进度。`GetDefuseProgress()`（`CS2DGame.hpp:615`）在 BOT 下包期间返回的是下包进度——这是已知命名陷阱，加一行注释说明，不要重构。
- 不碰：拆包时间（有钳 4 秒 / 无钳 10 秒，PIX-33 已定）；`bomb_planting.wav` 3.000 秒过程音与 3 秒阈值正好对上，不用改。
- 验收：玩家下包 3 秒完成；BOT 下包同样 3 秒完成；中途被打断/松开 E 后进度清零（现有逻辑不变）。

### G38（P2）取消固定前视机制 —— 整个机制删除
- 现状：F6 切换的 ANGLED 相机模式没有任何游戏内提示。注意审计写的是“100 单位”，现状常量是 **200**（`Tuning.hpp:40` `CameraAngledLength = 200.0f`）——以现状为准。
- 删除清单（删干净，grep 零命中才算完）：
  1. `CS2DGame.cpp:933-934`：F6 轮询 `ToggleCameraMode()`。
  2. `CS2DGame.hpp:636` `ToggleCameraMode()`、`637` `AngledCameraEnabled()`、`990` 成员 `angledCamera_`、`cameraKeyHeld_`。
  3. `CS2DGame.cpp:1213-1216`：`angledShift = CameraAngledLength / distance` 与三元 `shift`，相机只保留 `scopedShift`（开镜偏移）那条路。
  4. `Tuning.hpp:40` `CameraAngledLength`。
  5. `Engine.cpp:1393`：`cs2dOverlay.SetReferenceMode(...)` 两个 `AngledCameraEnabled()` 实参——改传固定值或随 overlay 侧清理（若 overlay 的 reference-mode 参数因此变死，一并删）。
- 验收：按 F6 无任何反应；全仓 grep `angledCamera_|CameraAngledLength|ToggleCameraMode|AngledCameraEnabled` 零命中；开镜相机偏移（`CameraLookShift` / `ScopeCameraShift`）行为不变；编译通过。
- 注意：F6 删除后不再进入 G34 的键位表，不要给它留绑定项。

---

## D. 输入系统

### G34（P1）全量键位自定义 —— 设置新增“键位”页
- 现状：所有操作键写死，无键位页。设置只有 4 个页签（`Graphics/GameUI.cpp:501-507`：操作与显示 / 准星与瞄准线 / 雷达 / 音频）。完整键位清单（现取代码核实）：
  - 事件驱动（`Input/InputManager.cpp` `ProcessEvents` 的 `SDL_KEYDOWN` switch，用 `keysym.sym`）：F11 全屏、F1 静音、Esc 暂停、1/2/3/4 武器槽（含小键盘）、B 买枪菜单。
  - 轮询（`Engine.cpp` `SDL_GetKeyboardState`）：W/A/S/D 移动（`:909-914`）、Shift 潜行（`:913`，进游戏是 `player_.stealth`，`CS2DGame.cpp:961`）、R 换弹、Tab 记分板（`:918`）、G 丢弃武器（`:900` 边沿）、5 拿起/收起 C4（`:929` 边沿 `ToggleHoldC4`）、F6 相机（`:933-934`，G38 删除，不纳入）。
  - 其他文件：Space 观战切换（`CS2DGame.cpp:877-878`）、E 互动按住下包/拆包（`Engine.cpp:1428`）。
  - 鼠标：左键开火、右键开镜（`InputManager.cpp` 只记边沿）。
- 改法：
  1. 设置加第 5 个页签“键位”（`sections[]`、`bodies[]` 各加一项，`for (int i = 0; i < 4; ++i)` 改为 5；新页沿用现有 `drawCard` 行布局）。
  2. 绑定以 **SDL_Scancode** 存（不要存 `keysym.sym`——scancode 与键盘布局无关，G41 的治本也依赖它）。结构进 `GameSettings`（`Storage/SaveManager.hpp:16`，`scopeSensitivity` 同处），随 `cs2d_save.ini` 持久化（`MutableSettings()` + 退出统一落盘，`Engine.cpp:255`）。
  3. 绑定项（全部可改）：移动 W/A/S/D、潜行 Shift、换弹 R、互动 E、丢弃 G、买枪 B、武器槽 1/2/3/4、C4 键 5、记分板 Tab、观战 Space、暂停 Esc、全屏 F11、静音（默认改 M，见下）、左右手鼠标切换（开火/开镜互换——G34 原文点名“左手鼠标”）。
  4. 捕获 UI：点某一项 → 显示“按任意键…” → 录入 scancode；Esc 取消捕获（捕获中 Esc 不触发暂停）；冲突检测：新键已被占用时提示并提供“交换 / 取消”；恢复默认按钮。
  5. 接线：`InputManager` 的事件 switch 与全部轮询点改读绑定表，不再出现硬编码 scancode/sym（F6 除外，已随 G38 删除）。鼠标左右手切换在开火/开镜边沿处生效。
  6. F1 改用途：F1 不再是静音，改为**帮助页**（列出当前全部键位，复用键位页数据）；静音默认键改为 M（可重绑）；设置音频页保留静音开关按钮。
- 验收：逐项重绑后对应功能跟随新键；WASD 改方向键后移动/潜行/换弹/互动/丢枪/买枪/切槽/C4/记分板/观战/暂停/全屏/静音逐项可用；冲突提示出现且交换后两键都工作；重启游戏绑定保留；左手模式下左键开镜、右键开火；F1 打开帮助页且不再静音。

### G35（P1）主菜单 Esc 与退出确认
- 现状：`Engine.cpp:810-834`，Esc 处理的 `else` 分支（不在对局/暂停/设置/结算/确认页/选图/历史时）就是 `isRunning = false`；主菜单“退出游戏”调 `quitToDesktop`（`Graphics/GameUI.cpp:396`），`Engine.cpp:229-233` 也是直接 `isRunning = false`。与之对比，暂停页退出要走两层确认（`GameUI.cpp:454-468` 的 `EXIT_CONFIRM`）。
- 改法：
  1. 主菜单按 Esc：什么都不做（已是顶层）。
  2. 主菜单“退出游戏”：走与暂停页同一套 `EXIT_CONFIRM` 确认页。
  3. 确认页的返回目标要参数化：现状 `EXIT_CONFIRM` 选“返回游戏”固定回 `PAUSED`（`GameUI.cpp:467`），从主菜单进确认页时“返回”必须回 `MAIN_MENU`；`Engine.cpp:830-831` 的“确认页按 Esc 回 PAUSED”同样按返回目标处理（从主菜单进来的 Esc 回主菜单）。
- 验收：主菜单 Esc 无反应；主菜单“退出游戏”弹确认，选返回回主菜单，选退出才退出；暂停页退出流程不变；结算页 Esc 回主菜单不变。

### G36（P1）死斗保留选阵营；选阵营界面 Esc 可暂停
- 现状与 Sun 的决定：死斗**保留**选阵营功能（不跳过）。全部模式都走选阵营：`Engine::BeginMatchLoad`（`Engine.cpp:1528`）无条件 `sideChoicePending_ = true`（`:1532`），死斗经 `startMatchMode`（`:114-115`，office 图）同样进这个流程。选阵营期间 Esc 被吞：`Engine.cpp:810-812` 要求 `!sidePopupOpen_ && !sideChoicePending_` 才处理暂停；`Engine.cpp:847-854` 在此期间锁死对局输入。
- 改法：
  1. Esc 在 `sidePopupOpen_ || sideChoicePending_` 期间改为打开暂停菜单（`PAUSED`），而不是无响应。暂停菜单就用现有那套（返回游戏 / 设置 / 重新开始 / 退出游戏 / 退出桌面）。
  2. “返回游戏”回到选阵营界面（恢复弹窗状态，不直接进 PLAYING）；“退出游戏”回主菜单；“重新开始”回到选阵营起点。
  3. 暂停期间冻结选阵营流程计时：`UpdateSideSpawnTransition`（`Engine.cpp:1021-1042`）与 `SideFadeAlpha` 用的是墙钟 `SDL_GetTicks64()`（FullMap → ZoomToSpawn 900ms → DarkHold 350ms → Reveal 650ms，`Reveal` 结束才 `currentState = PLAYING`，`:1036`）——暂停时要把 `sidePhaseStartMs_` 快照/顺延，否则暂停菜单盖着的时候流程会在底下走完。
  4. 渲染层级：暂停菜单盖在地图预览/阵营卡片之上；暂停期间鼠标保持绝对模式（选阵营时本来就是 `SDL_FALSE`，`FinalizeSideChoice`）。
- 不做：死斗选阵营弹窗的文案（“团队协作”）本次不动——Sun 只要求保留功能 + 加暂停接口，文案不在决定里。
- 验收：任一模式进选阵营后按 Esc 弹出暂停菜单；返回游戏回到选阵营卡片；退出游戏回主菜单；暂停 5 秒再返回，流程计时没有偷跑（仍停在原阶段）；死斗仍先选阵营再开局。

### G41（P2）输入法切换 —— 治本 + 收尾
- 现状：`Engine.cpp:1224` `StartMatchWithMap` 里无条件调 `inputManager->SwitchToEnglishKeyboardLayout()`；恢复只在 `Engine.cpp:732`（`Clean`）与 `InputManager` 析构（`InputManager.hpp:49` `RestoreOriginalKeyboardLayout`）——崩溃/强杀不恢复。`InputManager.cpp:96-131`：先记原布局（`GetKeyboardLayout`），再 `LoadKeyboardLayoutW(L"00000409")`。
- 根因：事件驱动键用的是 `keysym.sym`（`InputManager.cpp` 的 `SDL_KEYDOWN` switch），sym 随键盘布局变——这才是每局开局必须切英文布局的原因。轮询键用 scancode，本来就不受布局影响。
- 改法（Sun 接受的建议，落法如下）：
  1. **治本**：`InputManager` 的事件 switch 改走 `event.key.keysym.scancode`（与 G34 绑定表同一套 scancode），消除对局内切布局的需要。这是 G41 的主改动，与 G34 同一批做。
  2. 只在真正需要输入的场合切换：全仓核查对局内文本输入点（如有聊天/控制台输入框），只在输入框获焦时切换、失焦时恢复；若核查后确认对局内无文本输入，删除 `Engine.cpp:1224` 的调用。
  3. 崩溃恢复：切换时把原布局 + “已切换未恢复”标记写到存档旁的小文件（`cs2d_save.ini` 同目录）；启动时若标记仍在且当前布局仍是 00000409，调 `RestoreOriginalKeyboardLayout()` 还原并清标记。
  4. 兜底（仅当 1 暂缓时）：保留开局切换，但在加载页注明“已切换为英文键盘布局，退出后恢复”，并仍做 3。
- 验收：中文输入法激活状态下进对局，B/1-4/Esc 不经布局切换照常工作（做了 1 的情况下）；任务管理器强杀对局中游戏 → 重启 → 键盘布局已恢复；正常退出布局恢复如初。

---

## E. BOT 行为

### G32（P1）反应延迟只锁开火，不锁移动 —— 原因已定位
**原因分析（Sun 要求先分析，结论如下）：**
- 反应延迟本身不是 BUG，锁的位置错了。`CS2DGameBots.cpp` 每 BOT 更新循环里有两处 `if (!ai.Ready(id, clockSec_)) continue;`（`:620` 看见敌人分支、`:646` 听声分支）。`continue` 直接跳过该 BOT 本帧的**全部**后续更新——移动、寻路、转向、找掩体、开火全停，只剩循环前半段的撤退找掩体（`:600-606`）还能跑。于是 BOT 在反应时间内表现为整只定住：低难度 2.5 秒、默认难度 2 是 0.5 秒（`Tuning.hpp:48` 旧值；实际反应时间来自 `tactics_.perception[difficulty].reaction`，见下）。
- 反应时间的真正来源：`CS2DGameBots.cpp:484` `perception = tactics_.perception[clamp(ai.difficulty, 0, 3)]`，`Perceive(id, clockSec_, perception.reaction)`（`:619`、`:645`）记录感知时刻，`Ready()` 比较当前时间。注意两点现状：① 当前工作区 `assets/config/tactics/` 目录不存在（按图战术表缺失，`tactics_.loaded` 为假，走代码默认）；② `Tuning.hpp:48` 的 `BotReactionSecs = {2.5, 1.0, 0.5, 0.35}` 在现取代码里**零处引用**（除定义外）——疑似已死常量，Codex 核实后删除或注明它是默认值的来源。
- 对照：躲闪分支（`:492`）的 `Ready(-4, ...)` 用法是反的——就绪才去躲闪点，说明 `Ready` 本是“能不能行动”的门，不是“整帧跳过”的理由。

**改法：**
1. 删掉 `:620` 与 `:646` 的 `continue`。反应等待期间 BOT 照常移动、转向、寻路、找掩体、听声更新。
2. 只锁开火：把 `Ready()` 门移到射击决策处（`shouldShoot`，约 `:1445` 附近）——未就绪时 `shouldShoot = false`，同时瞄准安定（`settleTimer`）也不推进。开火以外的行为一律放行。
3. 低难度 2.5 秒不再表现为定身：2.5 秒内 BOT 会走位/转向/找掩体，只是不能开火——玩家看到的是“它在反应”，不是“它卡住了”。
- 验收：BOT 露头后 0.5 秒（默认难度）内能看到它在移动/转身，但不开火；到点后正常开火；难度 0（2.5 秒）全程不出现原地罚站；听声扑人分支同样只锁开火；`gameplay_tests` 全过。

### G33（P1）受击转向走 maxTurn 平滑
- 现状：受击后 `bot.aimAngle = std::atan2(tdy, tdx)` 直接赋值（`CS2DGameCombat.cpp:396-401`），绕过了逐帧转向限制；门控是 `Tuning::BotTurnOnHit = 1.0f`（`Tuning.hpp:208`）。正常转向有现成钳制：`maxTurn = BotMaxTurnRate * dt`（`BotMaxTurnRate = 8.0 rad/s`，`Tuning.hpp:209`），`bot.aimAngle += clamp(delta, -maxTurn, maxTurn)`（`CS2DGameBots.cpp` 约 `:1418` 的转向段）。
- 改法：受击转向不再直接赋值，改为设一个期望朝向（或直接复用转向段的钳制逻辑），每帧按 `maxTurn` 靠过去。8 rad/s 下 180° 转身约 0.4 秒——潜行绕后重新有意义，BOT 也不会出现“一帧锁头”。
- 验收：绕后开一枪，BOT 约 0.4 秒内平滑转过来（不是一帧）；连续受击不重置转身进度（不出现来回抽搐）；`BotTurnOnHit = 0` 时仍不转身（门控保留）。

### G40（P2）BOT 难度调节接口 + 后坐力恢复语义修正
- 现状：难度**没有**调节接口——`GameUI.cpp` 全文无 `difficulty`；难度唯一来源是 `tactics_.botProfile / 2`（`CS2DGame.cpp:166`，0–7 的 profile 除 2 得 0–3 档），`SetBotProfile(bot, profile)`（`CS2DGame.cpp:620`）可单只改档。`SpawnBOT`（`CS2DGame.cpp:567`）不设难度；`RespawnBOT`（`:641`）只调 `SpawnBOT`，难度留在 `botAi_` 里不重置（`ResetBotAi` 声明 `CS2DGame.hpp:601`，Codex 核查它是否重置难度）。
- 语义错误已核实：`CS2DGameBots.cpp:487` `bot.aimKick *= exp(-recoilRecoverPerSec * Tuning::BotFovRadiusMul[clamp(ai.difficulty, 0, 3)] * dt)`——后坐力恢复速度乘的是 **FOV 半径倍率**（`{0.8, 0.85, 0.9, 0.95}`，`Tuning.hpp:46`）。而 `BotFovRadiusMul` 在全仓**只在这一处被引用**——它现在根本不是 FOV 参数，只是个借名用的恢复倍率。
- 改法（Sun：先加接口，剩余逻辑由 Muse 补充）：
  1. 设置界面加难度调节：设置 → 操作与显示（`GameUI.cpp` `settingsSection == 0` 的“操作”卡片）加一行“BOT 难度”，4 档：简单 / 普通 / 困难 / 专家（0–3，默认 2 普通）。值进 `GameSettings`（`Storage/SaveManager.hpp:16`，新增 `int botDifficulty = 2;`），随 `cs2d_save.ini` 持久化。
  2. 接线：开局时对全部 BOT 应用（`SetBotProfile(i, botDifficulty * 2)` 或等价赋值，`CS2DGame.cpp:166` 的 `tactics_.botProfile / 2` 改为以设置值为准；`tactics.ini` 的 `botProfile` 仍为首次启动无存档时的后备）；设置里改档后立即对存活 BOT 生效（同一循环）；死斗重生（`ResetBotAi` / `RespawnBOT` 路径）继承设置难度，不回落到 tactics 默认——Codex 核查并补上这一处。
  3. 后坐力语义修正：`Tuning.hpp` 新增独立的 `BotRecoilRecoverMul[]`（值沿用 `{0.8, 0.85, 0.9, 0.95}`，行为不变——低难度恢复慢、打得更散，这是难度设计本身，没错；错的只是借了 FOV 的名字），`CS2DGameBots.cpp:487` 改用它；核实 `BotFovRadiusMul` 无其他引用后删除（若发现 FOV 半径另有引用，保留并注明真实用途）。
  4. 反应/FOV 等按难度索引的表（`tactics_.perception[]` 等）本次只换源不改值；数值调优按 Sun 说的“之后发现问题后再着重解决”，本次不做。
- 验收：菜单改难度 → 下一局 BOT 反应/散布按新难度走；对局中改难度 → 存活 BOT 当即切换；死斗 BOT 重生后仍是设置难度；重启游戏难度保留；`BotFovRadiusMul` 不再出现在后坐力恢复里。

---

## F. 反馈呈现

### G28（P1）购买面板只在购买区内召唤；删除区外相关内容
- 现状：B 键 `Engine.cpp:860-862` `buyMenuOpen_ = !buyMenuOpen_`，开面板时**不查购买区**；面板打开后才按 `bctx.canBuy = IsBuyPhase() && IsPlayerInBuyZone()`（`RenderBuyMenu`，`Engine.cpp:2501`）把全部商品置灰，并显示提示“不在购买区内”（`:2503`）。购买区判定 `CS2DGame::IsPlayerInBuyZone`（`CS2DGame.cpp:213`，地图没定义购买区时全图可买）。购买期结束自动关面板（`Engine.cpp:863-864`）保持不变。
- 改法：
  1. B 键只在 `IsBuyPhase() && IsPlayerInBuyZone()` 时打开面板；区外按 B **什么都不发生**（不开面板、不弹提示）。
  2. 删除区外相关内容：`RenderBuyMenu` 里 `canBuy == false` 的置灰分支与“不在购买区内 / 当前不是购买阶段”提示（`Engine.cpp:2501-2504`）；购买失败只在真正下单时由 `EvaluatePurchase` 的 `BuyCode::OutsideBuyZone`（`CS2DGame.cpp:303`）兜底——它是服务端防线，保留，不要删。
  3. 注意：调试用的 `debugBuyMenuPending_`（`Engine.cpp:1436`）只验收截图用，不动。
- 不做：购买区地面框/雷达标记（审计的建议，Sun 本次没定）——本次只做门禁 + 删除，不画标记。
- 验收：购买区外按 B 面板不出现；购买区内按 B 正常打开；购买期内走出购买区再按 B 不开；购买期结束后按 B 不开；区内购买流程不变。

### G39（P2）优化受击方向提示 —— 不加人数上限
- 现状核实（重要发现）：**受击方向提示目前根本不存在，而且连非方向的反馈都没接线**——
  - `hitMarkerTimer_ = 0.18f`（命中敌人反馈，`CS2DGameCombat.cpp:340`）与 `damageFlashTimer_ = 0.30f`（玩家受伤反馈，`CS2DGameCombat.cpp:417`）只在战斗里赋值、在 `CS2DGame.cpp:1098-1099` 递减；读取它们的 `GetHitMarkerTimer()` / `GetDamageFlashTimer()`（`CS2DGame.hpp:563-564`）在全仓**零调用**——两个计时器是死的，没有任何渲染消费它们。
  - `Engine.cpp` 全文无 `atan2`——没有任何方向计算。所谓“受击方向提示”现在是零。
  - 数据是现成的：命中玩家分支（`CS2DGameCombat.cpp:408-417`）当时子弹就在作用域内，有 `bullet.vx/vy`（速度）与 `bullet.ownerIdx`（开火者）——与首批 G44 核实过的数据一致。
- 改法：
  1. 先接线：`GetHitMarkerTimer()` 接到准星命中标记渲染（命中敌人时准星短暂出标记）；`GetDamageFlashTimer()` 接到受伤反馈渲染（现有红闪若另有通路，先核实；若没有，按计时器做全屏边缘红晕）。
  2. 再加方向：`damageFlashTimer_` 置位时一并记录受击方向角 = 子弹速度反向（`atan2(-bullet.vy, -bullet.vx)`，即开火者方向；等价于用 `ownerIdx` 取开火者位置算方向，Codex 选其一并写进注释）。Engine 在计时器有效期内渲染方向楔形（屏幕边缘 / 准星周围的红弧，指向来袭方向）。
  3. 多方向：0.5 秒窗口内多次受击保留多个方向（上限 4 个，按时间淡出），不要互相覆盖——被集火时能看到两个以上方向。
  4. **不添加人数上限**：不做攻击令牌、不做同时开火人数上限（明确非目标）。
- 与首批 G44 的重叠：G44（受击方向）的数据核实已在首批草稿里，UI 一直没做；G39 本次把 UI 做了。若首批之后再做 G44，直接复用本项的渲染，不要重做——Codex 在 G44 处留备注。
- 验收：背后中枪 → 屏幕后方出红楔；两侧同时中枪 → 两个楔形；命中敌人时准星出命中标记；计时器走完楔形消失；不加人数上限（4 只 BOT 可同时开火，改动前后行为一致）。

---

## 验收总表（Codex 自测后打勾）

- [ ] G28：区外按 B 无反应；区内正常；`RenderBuyMenu` 无置灰分支与“不在购买区内”文案。
- [ ] G29：玩家与 BOT 近战均为 ±60° 扇形；背后挥刀不命中。
- [ ] G30：实测落点 ≈ 准星位置（200/400 两档，误差 ≤30）；无规则改动。
- [ ] G31：玩家与 BOT 下包均为 3 秒；无 BOT 下包进度条新增。
- [ ] G32：反应等待期内 BOT 照常移动/转向/找掩体，只不开火；难度 0 不罚站。
- [ ] G33：受击转身平滑（180° 约 0.4 秒），无一帧锁头；`BotTurnOnHit = 0` 时不转身。
- [ ] G34：全部键位可重绑且生效；冲突提示；重启保留；左手鼠标；F1 帮助页。
- [ ] G35：主菜单 Esc 无动作；主菜单退出走确认页，返回目标正确。
- [ ] G36：选阵营时 Esc 出暂停菜单；返回游戏回选阵营；计时不偷跑；死斗仍选阵营。
- [ ] G38：F6 无反应；`angledCamera_|CameraAngledLength|ToggleCameraMode|AngledCameraEnabled` 全仓零命中。
- [ ] G39：受击方向楔形 + 命中标记均可见；多方向并存；无人数上限改动。
- [ ] G40：设置难度 4 档可用；开局/即时/重生三路生效；重启保留；后坐力恢复不再引用 FOV 倍率。
- [ ] G41：中文输入法下按键正常；强杀后重启布局恢复；对局内无文本输入则删除开局切换。
- [ ] 全量：`build-release` 编译通过；`gameplay_tests` 全过；进游戏打一局验证。

## 明确不做

- G27（购买窗/暴露提示）、G37（已完成）、G42（Sun 已改）无任务。
- G28 不画购买区地面框/雷达标记；G31 不加 BOT 下包进度条（以后再说）；G36 不改死斗选阵营弹窗文案；G39 不加集火人数上限/攻击令牌；G40 不调难度数值表；G30 不加落点预览、不改投掷数值。
- 本部分不碰：视野/瞄准（PIX-31 已定）、音频（PIX-33）、地图/美术（PIX-32）、回放哈希（开发期工具，另议）。

## 附录：核实方法说明

- 行号基准：2026-11-16 自 Sun 电脑工作区现取的 13 个文件（`CS2DGameCombat.cpp`、`CS2DGameBots.cpp`、`CS2DGameObjective.cpp`、`CS2DGame.cpp`、`CS2DGame.hpp`、`CS2DRenderer.cpp`、`Tuning.hpp`、`Engine.cpp`、`GameUI.cpp`、`InputManager.cpp/.hpp`、`RoundManager.hpp`、`SaveManager.hpp`）。Codex 动手前若发现行号漂移，以符号名为准。
- 与审计报告的出入（已核实）：G30 投掷规则已改（审计的旧公式已不存在）；G37 拆包已是 4/10 秒（审计的 5 秒已过时）；G38 前视常量现状是 200 不是 100；G39 两个反馈计时器是死的（审计未发现）；G40 的 `BotFovRadiusMul` 全仓只用于后坐力恢复；G32 的反应时间来自 `tactics_.perception[].reaction`，`BotReactionSecs` 零引用；`assets/config/tactics/` 目录目前不存在，战术表走代码默认。
