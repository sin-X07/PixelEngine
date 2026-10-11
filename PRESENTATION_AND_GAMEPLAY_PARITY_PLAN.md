# PixelEngine 重构后呈现与玩法回归计划

## 0. 任务目标与基线

将当前已模块化的项目恢复到 GitHub 原版 `sin-X07/PixelEngine` 的**视觉呈现、镜头行为、UI 风格与操作手感**，同时保留重构后的模块边界，并保留模式一新增的陷阱玩法。

- 原版基线：Git 提交 `42444f6`（`origin/main`，仓库首页：<https://github.com/sin-X07/PixelEngine>）。
- 当前实现：工作目录中的模块化版本；不要用原版的 1800+ 行 `Engine.cpp` 覆盖当前模块。
- 原版 `assets/maps/map1.txt` 含 3 个固定 `C` 宝箱；这是地图素材事实，但模式一开局必须将其替换为 3 个安全的随机宝箱位置。
- “一致”指原版已有页面、镜头和操作的观感/数值一致；下文明确列出的陷阱例外必须保留。

## 1. 不可回退的保留项（模式一陷阱）

以下内容不是原版功能，但属于当前版本必须保留的增量。不得删除、改成普通可破坏障碍，或在回归过程中绕过它们。

1. `Core/Types.hpp` 中的 `TrapType`、`Trap`、`ScoreRecord::success`。
2. `WorldMap::GenerateTrapsForTimeAttack`、`IsChokepointTile` 与 `CheckMapConnectivity`：模式一开始时生成地刺/老鼠夹；不得在狭口、玩家出生区或使地图不可达的位置生成。
3. `PhysicsSystem::CheckTrapCollision`：陷阱不可被 `PerformAttackHitCheck` 销毁；攻击仍只销毁 `H` 矮墙和 `o` 小障碍。
4. 陷阱绘制、`player_death.wav`、死亡震屏和血沫粒子。
5. 陷阱死亡的结算内容：显示“失败”和死亡说明；`SaveManager` 写入失败标志，历史页保留“成功/失败”与失败色。
6. 为保证死亡特效正常，`ParticleSystem::Update(dt)` 必须继续在 `GAMEOVER` 状态推进，直至粒子和震屏自然结束。
7. 模式一开局清除地图文本中的固定 `C`，使用现有安全候选算法随机生成恰好 3 个宝箱；生成结果不得与出生点、墙体、可破坏障碍或陷阱重叠。

除上述例外外，不要新增其他改玩法的机制、数值或 UI 设计。

## 2. 已核实的差异与修复要求

| 优先级 | 范围 | 原版基线行为 | 当前差异 | 必须实施的回归 |
|---|---|---|---|---|
| P0 | 菜单观感 | 主菜单/模式选择使用地牢地板作背景，再叠加 `RGBA(12,23,31,145)` 半透明暗层，顶部 `y=104` 有金色横线。 | `UIRenderer::DrawMenuPage` 使用完全不透明的纯色背景；`RenderSystem::DrawDungeonBackground` 也忽略 `overlayAlpha`。 | 恢复地牢背景、半透明暗层和横线；保持 UI 在背景之后绘制。 |
| P0 | 按钮观感 | 外框颜色：普通 `(55,75,85)`、悬停 `(245,190,90)`；内部缩进 3 px，填充 `(24,35,43)`；字体 24 px。 | 当前是扁平填充、描边和 20 px 字体。 | `UIRenderer::DrawButton` 按原版数值恢复边框+内嵌样式和字号。 |
| P0 | 镜头边界 | 相机中心跟随玩家，但在 `[0, mapSize - viewportSize]` 范围内 clamp；震屏位移在缩放后除以 zoom。 | `RenderSystem::UpdateCamera` 不再 clamp，边缘会越出地图；震屏量纲也不同。 | 使用原版 clamp 公式，再追加震屏偏移 `shake / CurrentCameraZoom()`。 |
| P0 | Shift/ Ctrl 手感 | Shift 以 `sprintBlend` 平滑加速至 2 倍；Ctrl 在 0.3 秒内平滑切换，速度过渡到 `MAX_SPEED/3`，镜头从 `2.0` 拉远至 `1.0`，视野半径/软边各扩大至 2 倍。 | 当前 Shift、Ctrl 的**速度**是瞬时切换；Ctrl 反而轻微放大到 `2.16`，视野仅 1.35 倍。 | 恢复第 4 节的精确公式；不要改变 `InputManager` 的按键事件边界。 |
| P1 | 模式一初始宝箱 | 原版加载 `map1.txt` 后使用 3 个固定 `C`。 | 当前会清空固定宝箱并随机生成 3 个。 | **保持当前随机开局设计**：清空固定 `C` 后以安全候选算法生成恰好 3 个随机宝箱；陷阱必须在宝箱已生成后再生成，且不得覆盖宝箱。 |
| P1 | 结算背景 | 原版 `GAMEOVER` 的模式一金币不再绘制，世界被遮罩与结算卡覆盖。 | 当前 `RenderSystem::DrawWorld` 在 `GAMEOVER` 仍绘制金币。 | 为 `DrawWorld` 增加 `drawCoins` 参数（或在内部传入状态）；仅 `PLAYING` 时绘制金币。陷阱仍可在结算背景保留，以满足“陷阱玩法保留”。 |
| P1 | 历史页排版 | 原版模式一记录文本 x=36、进度条 x=194；模式二维持 x=426/x=584。 | 当前为给成功/失败标签留位，模式一文本和进度条移至 x=24/x=176。 | 恢复原版文本/进度条坐标；将成功/失败放在 x=120 左右，不挤压原版进度条。 |
| P1 | 结算页排版 | 通用面板、标题和“Enter / 空格”提示使用原版位置（提示 y=350）。 | 当前普通成功态的文案和垂直位置有轻微改动。 | 恢复原版成功态文案/坐标；仅死亡失败态使用新增红色“失败”文案。 |
| P2 | 悬停音效 | 原版用 `label + rect` 作为悬停身份，切换同名按钮也会触发一次。 | 当前只以 label 判断。 | 在 `UIRenderer::DrawButton` 以标签加矩形坐标建立 hover key；继续由 `Engine::Render` 集中播放一次音效，避免每帧重复播放。 |

## 3. 具体改动位置与实现顺序

### 阶段 A：恢复菜单与页面的视觉层次

1. 修改 `Graphics/RenderSystem.cpp`：
   - `DrawDungeonBackground(const WorldMap&, Uint8 overlayAlpha)` 在平铺地板后，当 `overlayAlpha > 0` 时绘制全屏矩形：`SDL_SetRenderDrawColor(renderer, 12, 23, 31, overlayAlpha)`，然后 `SDL_RenderFillRect`。
   - 不要让该函数负责 UI；它只负责背景与可选暗层。
2. 修改 `Engine::Render`：当状态为 `MAIN_MENU` 或 `MODE_SELECT` 时，在调用 `UIRenderer::DrawMenuPage` 前执行 `renderSystem->DrawDungeonBackground(*worldMap, 145)`。此时相机应为 `(0, 0)`。
3. 修改 `Graphics/UIRenderer.cpp`：
   - `DrawMenuPage` 不再填充不透明纯色背景；保留标题和按钮。
   - 在标题区域下绘制原版金线：颜色 `(231,176,70,255)`，从 `(0,104)` 到 `(800,104)`。
   - `DrawButton` 恢复第 2 节表格中的原版配色、3 px 内嵌与 24 px 文本居中方式。
4. 不要修改历史、设置、说明页的大背景色；这些原版本来就是纯色页面。

### 阶段 B：恢复相机、冲刺和聚焦手感

1. 在 `RenderSystem` 增加 `ResetMovementBlends()`，将 `sprintBlend` 和 `focusBlend` 都置零；`Engine::StartGame` 调用它，确保每局开局状态与原版一致。
2. 保留当前 `UpdateMovementBlends` 的渐变速度：

```cpp
sprintBlend = std::clamp(sprintBlend + (sprintHeld ? dt : -dt / 0.4f), 0.0f, 1.0f);
focusBlend = std::clamp(focusBlend + (focusHeld ? dt / 0.3f : -dt / 0.3f), 0.0f, 1.0f);
```

3. `Engine::Update` 的移动上限改回原版插值，不要直接使用布尔值：

```cpp
const float sprintMaxSpeed = MAX_SPEED * (1.0f + renderSystem->GetSprintBlend());
const float movementMaxSpeed = sprintMaxSpeed +
    (MAX_SPEED / 3.0f - sprintMaxSpeed) * renderSystem->GetFocusBlend();
```

如不希望暴露 blend 值，可让 `RenderSystem` 提供 `CurrentMovementMaxSpeed(float baseSpeed)`，但公式必须等价。

4. `CurrentCameraZoom` 必须为 `CAMERA_ZOOM - focusBlend * (CAMERA_ZOOM - 1.0f)`；`CurrentVisionRadius/SoftEdge` 都为原始常量乘 `(1.0f + focusBlend)`。Shift 不单独修改画面缩放。
5. `UpdateCamera` 先计算 `viewportWorldWidth/Height` 和 `maxCameraX/Y`，将玩家中心镜头 clamp 到地图范围，再加 `shakeOffset / zoom`。相机应在暂停、退出确认和结算时停留在最后的可见地图范围。

### 阶段 C：保留模式一随机宝箱与陷阱，同时恢复其余原版节奏

1. `Engine::StartGame(TIME_ATTACK)` 的顺序必须是：

```text
加载 map1.txt
清空由地图文本读取的固定 C
EnsureCoinCount(3, 玩家位置, 随机数生成器)
GenerateTrapsForTimeAttack(同一玩家位置, 同一随机数生成器)
切换到 PLAYING
```

2. 保留 `WorldMap::GetCoins().clear()`，但它只能用于刚加载模式一地图后的固定 `C` 清理；之后调用 `EnsureCoinCount(3, ...)`。不得在陷阱生成之后或游戏进行中额外清空宝箱。
3. 保持每拾取一个金币后 `ResetCoinPosition` 的旧算法和距离限制不变。
4. 陷阱生成必须将现有金币当作不可覆盖区域，并继续以 `CheckMapConnectivity` 验证。若没有足够安全位置，允许生成少于目标数量，禁止为了凑数破坏可达性。
5. 攻击按键、攻击冷却、矮墙/木障碍破坏和刀光保持原版现有行为；陷阱不得进入可攻击对象列表。

### 阶段 D：恢复世界与结算页的绘制时机

1. 调整 `RenderSystem::DrawWorld` 的接口，例如：

```cpp
void DrawWorld(..., bool drawCoins);
```

`Engine::Render` 传入 `currentState == PLAYING`。这样暂停、退出确认和结算的金币可见性与原版一致；如希望暂停时保留金币，必须以原版实际行为为准（原版仅在 `PLAYING` 绘制）。

2. 世界绘制顺序保持原版：矮墙 → 小障碍 → 玩家 → 刀光 → 普通墙 → 宝箱 → 粒子 → 迷雾 → UI。新增陷阱插入在“小障碍后、玩家前”，使其明确可见且不遮挡玩家；这是保留陷阱的唯一必要顺序例外。
3. 成功/普通结算卡恢复原版几何、色彩和文字位置。仅当 `diedByTrap == true` 时覆盖标题和说明为失败态，且仍展示得分。
4. 保持当前“结算后粒子继续更新”的修复；这是陷阱死亡展示所需，不回退到原版的冻结行为。

### 阶段 E：输入、声音和存档的回归检查

1. 保持 SDL 逻辑坐标换算，所有按钮命中继续使用 `InputManager` 的逻辑坐标。
2. 空格攻击、F1 静音、F11 全屏、Esc 状态跳转、1/2 开始模式、鼠标音量拖动均以 GitHub 基线的功能为准。不要根据 README 中已过时的“跳跃”描述重新实现跳跃；基线代码实际已经将空格绑定为攻击。
3. 音频保留新增死亡音效；背景音乐、拾取、悬停、点击、挥剑和破坏音效的音量计算保持原版 100/25/100 默认比例及三路滑块含义。
4. `SaveManager` 保持对旧格式“仅分数一列”的兼容，旧记录默认成功；新增失败标志不得破坏模式二时间记录。

## 4. 验收清单（必须逐项通过）

### 编译与启动

- 使用 `cmake --build D:/PixelEngineBuild-Local --target EngineApp` 成功链接。
- 启动后能加载字体、地图、贴图和全部音效；缺少单个音效时仍可无声运行，不应崩溃。

### 静态画面对照

- 主菜单和模式选择页：可见地牢地板、145 alpha 暗层、金色 y=104 横线、原版双层按钮。
- 历史页：模式一条目和进度条恢复 x=36/x=194；失败标记存在但不挤压条形图。
- 设置、暂停、退出确认与结算：原版面板几何和配色一致；陷阱死亡仅额外出现失败信息。

### 操作手感对照

- 松键后惯性与反向减速不变。
- 按住 Shift：速度在约 1 秒平滑升到 2 倍；松开后约 0.4 秒回落。
- 按住 Ctrl：约 0.3 秒平滑减速，镜头由 2.0 拉远至 1.0，迷雾可见半径和软边均扩大至 2 倍；松开后平滑恢复。
- 地图四边附近相机不露出地图外区域；攻击震屏不因缩放而显得过大。

### 模式一

- 开局清除 `map1.txt` 的固定 `C`，并生成 3 个安全、互不重叠的随机宝箱位置。
- 拾取后仍按原版候选规则随机补充一个宝箱。
- 矮墙/木障碍可以攻击破坏，普通墙和陷阱不能。
- 地刺/老鼠夹均可见、不会堵死道路、不会生成在出生点或宝箱上。
- 触碰陷阱：播放死亡音效，画面震动并出现血沫；结算显示失败且历史记录标记失败。
- 倒计时自然结束：显示原版成功结算样式，历史记录标记成功。

### 模式二

- 随机迷宫仍可生成并加载；计时、终点宝箱和通关记录正常。
- 模式二不生成模式一陷阱，也不显示失败战绩标志。

## 5. 实施约束

1. 只在当前模块化目录内修改：`Graphics/`、`World/`、`Input/`、`Audio/`、`Storage/`、`Core/` 与薄 `Engine` 协调层。
2. 不恢复单体 `Engine.cpp`，不复制 GitHub 原版的大段代码来破坏架构重构。
3. 每完成一个阶段都构建一次；阶段 B 与 C 后必须进行人工游玩检查。
4. 不修改 `assets/maps/map1.txt` 中的原版宝箱布局；陷阱是运行时附加数据，不写回地图文本。
5. 若某项原版行为与陷阱保留项冲突，以第 1 节“不可回退的保留项”为准，并在提交说明中标明该例外。
