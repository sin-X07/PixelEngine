# Windows AI（孙铭轩）交接说明 — 合并契约 v1 执行

本文件是 PIX-12 / PIX-13 / PIX-14 / PIX-15 / PIX-18 的交接记录：改了什么、怎么验证、还剩什么、
UI 方（shijin）需要接着做什么。契约正文见 [docs/AI-MERGE-CONTRACT-v1.md](AI-MERGE-CONTRACT-v1.md)。

## 0. 基线与环境

| 项 | 值 |
| --- | --- |
| 契约版本 | v1（本仓库 `docs/AI-MERGE-CONTRACT-v1.md`，与 Linear 附件一致） |
| base SHA | `d4a10555f83a6259a5dff0cd5a3ee97f31443dab`（= 远端 main，开工时核对未前进） |
| head SHA | `b8a19f7`（分支 `feat/pix18-contract-baseline`，已推送） |
| PR | <https://github.com/sin-X07/PixelEngine/pull/20>（目标 `main`，待评审合并） |
| 系统 | Windows 10 Home China 26H2，`10.0.26300`，x86_64 |
| 编译器 | MinGW-w64 g++ 15.2.0（x86_64-win32-seh-rev0，UCRT） |
| 构建工具 | CMake 4.3.2，生成器 `MinGW Makefiles` |
| 显示 | 窗口 2560x1600（内部逻辑画布 1728x1080，`renderScale=100%`） |

## 1. 构建（干净目录，完整命令）

依赖通过 FetchContent 拉取；本机无外网时用仓库已有的 `build-release/_deps` 源码目录：

```powershell
cd <repo>
cmake -S PixelEngine -B build/win-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
  "-DFETCHCONTENT_SOURCE_DIR_SDL2=$PWD/build-release/_deps/sdl2-src" `
  "-DFETCHCONTENT_SOURCE_DIR_SDL2_MIXER=$PWD/build-release/_deps/sdl2_mixer-src" `
  "-DFETCHCONTENT_SOURCE_DIR_SDL2_IMAGE=$PWD/build-release/_deps/sdl2_image-src"
cmake --build build/win-release --parallel 8
# 产物：build/win-release/EngineApp.exe（POST_BUILD 会把 assets 复制到 exe 旁）
```

结果：`[100%] Built target EngineApp`，无 error、无新增 warning。

## 2. 自动化测试（本次新增）

```powershell
cmake -S PixelEngine -B build/tests -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_GAMEPLAY_TESTS=ON `
  "-DFETCHCONTENT_SOURCE_DIR_SDL2=$PWD/build-release/_deps/sdl2-src" `
  "-DFETCHCONTENT_SOURCE_DIR_SDL2_MIXER=$PWD/build-release/_deps/sdl2_mixer-src" `
  "-DFETCHCONTENT_SOURCE_DIR_SDL2_IMAGE=$PWD/build-release/_deps/sdl2_image-src"
cmake --build build/tests --parallel 8 --target gameplay_tests
build/tests/gameplay_tests.exe    # 工作目录 = 仓库根
```

结果：`===== 结果: 119 通过, 0 失败 =====`（`tools/buy_scope_test.cpp`）。
覆盖内容：

- 购买：全部 `BuyCode`（InvalidItem / PlayerDead / WindowClosed / OutsideBuyZone / TeamRestricted /
  InsufficientFunds / InventoryFull / ItemLimitReached / AlreadyEquipped / Ok）、
  `Evaluate` 无副作用、非法 enum 不越界、成功只扣一次、失败快照不变、同枪第二次 AlreadyEquipped、
  闪光 2 个 / 其它 1 个 / 总数 4 个边界、护甲 tier1/tier2 语义、`TryBuy/TryBuyArmor` 兼容包装。
- 开镜（契约 v1.1）：`supported` 随武器变化、右键边沿只切一次、三档循环 关→1.5x→2x→关、
  每档 0.12s 过渡、换档时镜框不重播淡入、`targetZoomMultiplier` 随档位、`ResetScope` 幂等、
  切枪 / 换弹 / 输入被挡 / 阵亡归零。
- 圆形可见区：圆心=1、圆内=1、圆外=0、软边 0..1 渐变、与朝向无关（不含扇形）、半径 0 一律不可见。
- 伤害/护甲（v1.2）：AWP 基伤 >100、对满甲 112 进血（一枪死）、AK 对满甲 28/4、无甲全额、穿透率越界 clamp、护甲不足不降伤。
- 开枪自动关镜（v1.2）：默认开枪即关；延迟关镜开启时按住保持、松手关。
- 迷雾透明度（v1.2）：100%→160、50%→80、0%→0、越界 clamp。
- 枪弹视觉段：真实模拟段生成、0.08s 寿命清理、重复 Render 无副作用、512 上限。

> 说明：`OutsideBuyZone` 未单独构造用例（需要把小鸡… 玩家移出购买区并重算），
> 代码路径由 `EvaluatePurchase` 的顺序保证；`WindowClosed` 用 20 秒模拟通过。

## 3. Windows 人工验收（截图留证）

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/win_accept.ps1
# 产物：01-main-menu.png、02-ingame-hud.png、03-buy-menu.png、06-settings.png、
#       04-scope-15x.png、05-scope-20x.png + *.out 日志
```

脚本做的三件必要的事：`SetProcessDPIAware()`（否则截图只有逻辑坐标大小的裁切）、
把光标移动到窗口中心（DPI 缩放下鼠标常停在窗口左上角，右键会落在窗口外）、
开镜前用 `MOUSEEVENTF_MOVE` 把准星挪离屏幕中心（验证圆形可见区以准星为圆心）。

已核对（截图可见）：

| 截图 | 结论 |
| --- | --- |
| `01-main-menu.png` | 主菜单 + 实时 3D 背景：T/CT 角色与 3 只小鸡正常渲染 |
| `02-ingame-hud.png` | 对局 HUD：雷达、回合/比分、血量护甲、武器栏正常 |
| `03-buy-menu.png` | 购买菜单按 `availability` 禁用：`USP-S $200 (已装备)`、`AWP $4750 (已装备)` 灰显，其余可买 |
| `04-scope-15x.png` | AWP 右键第 1 档：圆形可见区（圆心=准星，非屏幕中心）、镜外浓雾、1.5×、镜筒圆环 + 中心准星 |
| `05-scope-20x.png` | 右键第 2 档：同一圆形内明显更放大（2×），镜外仍被雾遮盖，HUD 不受影响 |
| `06-settings.png` | 设置页新增「视野 / 开镜」卡片：迷雾透明度 100%、狙击枪延迟关镜开关 |

运行日志（`build-evidence/*.out`）关键行：

```
[Viewport] window=2560x1600 output=2560x1600 logical=1728x1080 renderScale=100%
[Menu3D] 小鸡已就绪: 3 只, 动画=walk, 模型高=1.843 单位
[Menu3D] glTF 角色已加载: T=Idle CT=Idle 小鸡=3
[Debug] 调试装备: AWP (金钱 $16000)
[Viewport]/[Menu3D]/[Renderer] 均无 error
```

## 4. 逐 Issue 交付

### PIX-18 契约基础（本任务）

新增：

- `PixelEngine/Core/PresentationContract.hpp`：`BuyKind/BuyRequest/BuyCode/BuyResult/ScopeView/
  ScopeResetReason/BulletVisualSegment` + 固定参数（1.5 倍、0.12s、0.08s、512 段）。
  只 include `Types.hpp` / `WeaponDef.hpp` / `<cstdint>`。
- `PixelEngine/Graphics/WeaponIconCache.{hpp,cpp}`：契约规定的三函数（Initialize/Get/Shutdown），
  非 owning renderer、缓存失败占位、`Shutdown` 早于 `SDL_DestroyRenderer`（`Engine::Clean` 已保证）。
- `docs/AI-MERGE-CONTRACT-v1.md`：附件正文纳入仓库。

修改（全部调用点已迁移）：

| 文件 | 主要改动 |
| --- | --- |
| `Graphics/GameUI.hpp` | `BuyItem` 新增 `request/availability/iconPath/categoryLabel/magazine/automatic/scopeSupported`；`BuyMenuCtx.playerTeam`；`DrawBuyMenu` 改为 `std::function<BuyResult(const BuyRequest&)>`（不留 void 重载）；`InGameCtx.playerTeam/scope`；`SideSelectCtx.bombMode`；`SetWeaponIconCache` |
| `Graphics/GameUI.cpp` | `DrawBuyMenu` 新签名；禁用判定改为 `availability == Ok`；一帧最多提交一次；失败显示 `BuyCode` 中文文案；`AllowWhenDisabled` 悬停看原因 |
| `CS2D/CS2DRenderer.hpp` | `SpriteDrawParams` 新增 `team/friendly/localPlayer`；`UpdateCamera(..., float zoomMultiplier = 1.0f)`；声明 `DrawBulletVisuals` / `DrawScopeOverlay` |
| `CS2D/CS2DGame.hpp/.cpp` | `EvaluatePurchase/ExecutePurchase`、`UpdateScopeInput/GetScopeView/ResetScope/IsScopeSupported`、`GetBulletVisuals`；`TryBuy/TryBuyArmor` 改为兼容包装 |
| `CS2D/CS2DGameCombat.cpp` | `UpdateBullets` 生成视觉段（含 impact 标记）、0.08s 衰减、512 上限；死亡 / 换弹 / 丢枪处 `ResetScope` |
| `CS2D/CS2DGameRound.cpp` | 新回合 `ResetScope(RoundRestart)` + 清空视觉段 |
| `Engine.cpp/.hpp` | 右键边沿采集与每帧一次提交、`gameplayInputAllowed` 计算、暂停/设置/选边/离场 `ResetScope`、`RenderBuyMenu` 新快照与 `ExecutePurchase` 回调、`InGameCtx` 填充、图标缓存生命周期 |
| `Input/InputManager.*` | `IsMouseRightClicked()`（down 边沿，只活一帧）、`HasWindowFocus()`；失焦清 held + 边沿 |
| `PixelEngine/CMakeLists.txt` | SOURCES 增加新文件；新增 `BUILD_GAMEPLAY_TESTS` 测试目标（默认 OFF） |

缺陷修复（契约要求的行为修正，有回归用例）：

1. 旧 `TryBuy` 每次都能重复购买同一把枪并重复扣款；现在第二次返回 `AlreadyEquipped`。
2. 投掷物此前被 "已拥有" 判定误挡（叠加购买失败）；现在按 `InventoryFull` / `ItemLimitReached` 判定。
3. 购买失败原因此前只有 bool；现在 10 个失败码稳定可测，且失败路径不改金额/装备/音效。

### PIX-12 小鸡 3D 素材与资源规范

- 新增 `assets/models/chickensoft/manifest.md`：来源/许可、GLB 结构（2293 顶点 / 3916 三角形 /
  28 关节 / 无材质无贴图）、单位（1 单位 = 1 米）、轴向（+Y 上、+Z 正前）、原点（脚底 `minY≈0`）、
  AABB（高 1.843）、14 个动画的名称与时长（`walk` 0.8s 为必备基础动画）、
  `Draw(scale,yOffset,yaw)` 实测校准值与步幅/播放倍率换算。
- 结论：现有 `chicken.glb` 已包含菜单需要的 `walk/idle/run/jump/blink`，**无需新增或重导出资产**；
  未修改资产文件（避免无谓二进制变更）。
- 校验脚本：`tools/glb_inspect.py`（stdlib 解析 GLB，输出动画/节点/包围盒）。

### PIX-13 菜单接入

- `Menu3D::LoadModels`：加载后逐个用 `PlayAnimation` 验收 5 个姿势动画，缺失即 `<stderr>` 记名；
  再强制 `PlayAnimation("walk")`，成功/失败都有日志（失败读 `LastError` 并整体降级，不清空既有实例以外的状态）。
- `Menu3D::ApplyCustomizationPose`：`PlayAnimation` 返回 false → 回退 `walk`（契约第 7 节）。
- 新增 `ChickenWorldScale` / `ChickenAnimTimeScale`，把原先散落在两处的缩放公式收敛为一处；
  `InitChickenState` 速度改为按距离透视补偿（`1.05 * dist/6 * 0.85~1.15`），
  `Render` 里按 `walk`/`run` 的实际速度缩放动画播放速率（消除滑步）。
- 边界不变：`UpdateChickens` 的漫游范围、`kObjectCount` / `ObjectIndex`（0=T,1=CT,2..4=鸡）、
  菜单/地图选择/退出返回路径均未改动；`chickens_` 为空时所有绘制以 `size()` 为上界，不越界。

### PIX-14 右键开镜（契约 v1.1 / v1.2）

- 唯一状态在 `CS2DGame`：`scope_` + `scopeLevel_ / scopeEngagement_ / scopeZoom_`，UI 只读 `GetScopeView()` 快照。
- `UpdateScopeInput(rightClicked, gameplayInputAllowed)`：输入被挡 → `ResetScope(InputBlocked)`；
  死亡 → `PlayerDied`；武器变化 → `WeaponChanged`；换弹 → `ReloadStarted`；仅支持 `WeaponDef::scoped`
  的武器可切；**右键循环 关 → 1.5x → 2x → 关**（`level = (level+1) % 3`）。
- `TickScope` → `CS2DGame::Update` 内按 dt 推进：倍率与镜框淡入各自 0.12s，
  换档时 `transition` 保持 1（不重播淡入），`zoomMultiplier` 线性趋向 `kScopeZoomLevels[level]`。
- `CS2DRenderer::UpdateCamera`：`zoom_ = baseZoom * zoomMultiplier`（`GetZoom()` = 实际倍率，
  `UpdateCursor` 不再二次乘）；`visionRadius_` 仍按 `baseZoom` 计算，敌人知情半径不变。
- **圆形可见区（v1.1）**：`Presentation::ComputeScopeVisibility` 是唯一几何定义；
  `CS2DGame::Render` 每帧 `SetScopeMask(true, 准星世界点, kScopeCircleRadiusFrac*min(viewW,viewH)/zoom)`，
  `ComputeVisibility` 在 mask 激活时先走圆形（叠加 LOS），否则走原扇形——`DrawVisionFog` 与敌人剔除
  共用这个函数，所以雾自动变成圆形、镜外不可见（`SCOPE_FOG_ALPHA 242`）。
- **相机向准星偏移（v1.1）**：`shift = f(zoom)`（1.5x→0.35、2x→0.5），相机中心 = 玩家 + (准星−玩家)×shift；
  由于 `UpdateCursor` 用 `GetZoom()` 反解，偏移只是把灵敏度按 `1/(1-shift)` 放大，无累积漂移。
- 归零路径：切枪（含投掷物自动切回）、丢枪、购买替换当前武器、换弹开始、阵亡、新回合、
  `LoadMap`、暂停/设置/选边/失焦/转场、离开对局、每次 Update 的防御性武器比对。
- `DrawScopeOverlay(scope, centerScreenX, centerScreenY, radiusPx, viewW, viewH)`（v1.1 签名）
  在世界雾之后、闪光之前接线，圆心用「准星世界点投影」而非虚拟光标（相机被地图边界钳制也不错位）。
  **当前为功能方的最小占位实现**（圆形镜筒 4 圈暗线 + 内缘亮线 + 四向刻度 + 中心准星），
  表现与倍率刻度样式归 UI 方（PIX-10），替换时只改函数体。

### PIX-15 Windows 验收

见第 2、3 节的实际执行结果。未执行/未验证项：

- 720p / 1080p / 16:10 / 窗口↔全屏切换：**未验证**（本轮只在 2560x1600 全屏下测试）。
- 购买菜单点击后的扣款/装备全链路：**未人工点击验证**（逻辑由自动化用例覆盖，
  UI 点击路径与 Mac 侧共用同一回调）。
- LLM 教练链路：本轮统一 `--coach=rule`，**未验证**真实 LLM（与 Mac 侧的差异另计）。
- Mac 侧（PIX-11）证据：**未提供**，双平台完成需等 shijin 的同一集成 SHA 结果。
- 帧率/性能（开镜时的 streaming 纹理与雾重算）：**未测量**。

## 5. 调试开关（本轮新增，仅验收用）

| 参数 | 作用 |
| --- | --- |
| `--loadout=<武器名或编号>` | 进入对局后直接装备并把金钱设为 $16000（如 `--loadout=AWP`） |
| `--buymenu` | 进入对局后自动打开一次购买菜单（购买窗口内） |

两者都只在显式传参时生效，正常启动没有影响；`--autostart/--mode/--side/--coach` 为原有开关。
另加一次性诊断日志 `[Viewport] window=... output=... logical=...`，便于排查 DPI/缩放问题。

## 6. 交给 shijin（UI 方）的待办

1. **DrawScopeOverlay**（PIX-10）：替换 `CS2DRenderer::DrawScopeOverlay` 的函数体；
   入参 `ViewW/ViewH` 已是逻辑画布坐标（1728x1080），不要用 Retina 物理像素；不要做命中/相机计算。
2. **DrawBulletVisuals**（PIX-17）：替换占位实现；按 `remaining/duration` 淡出、宽度 1.5~3 逻辑像素、
   长度上限 24 逻辑像素、按阻挡瓦片裁剪；数据源只用 `GetBulletVisuals()`（Render 不消费、不计时）。
3. **购买菜单**（PIX-6/7）：禁用与原因以 `BuyItem.availability` 为准，`iconPath` 走 `WeaponIconCache::Get`，
   不要每帧 `IMG_LoadTexture`；失败码文案可复用 `GameUI.cpp` 里的 `BuyCodeText`（如需换成 UI 文案请改那里）。
4. **InGameCtx.scope / playerTeam**：可用于 HUD 提示（如武器栏显示 1.5×），不要维护第二套开镜状态。
5. **TeamVisualStyle**（PIX-6）：`SpriteDrawParams` 已带 `team/friendly/localPlayer`，可直接用于阵营标识。
6. 合并注意：`CS2DRenderer.cpp` 只按函数改（`UpdateCamera` 归功能方，其余表现函数归 UI 方）；
   `GameUI.cpp` 的 `DrawBuyMenu` 已改签名，冲突时按函数整合。

## 7. 未完成 / 风险

- **PR 已开、未合并**：<https://github.com/sin-X07/PixelEngine/pull/20>，按 `docs/pr-workflow.md`
  等评审通过后再合并，不直接推 main。PR 内提交拆分：契约文档 / 接口基础+购买+开镜+枪弹 /
  小鸡资产规范与菜单校准 / 测试与验收脚本。
- 契约第 8 节要求"基础 PR 合并后双方基于同一 SHA 开发"；当前 shijin 尚未基于本 SHA 提交，
  `DrawScopeOverlay` / `DrawBulletVisuals` / `WeaponIconCache` 仍处于占位或最小实现阶段，
  因此 PIX-14 的"UI 消费方"验收要等 PIX-10 完成。
- `WeaponIconCache` 已接入 Engine 生命周期，但购买菜单还没有绘制图标（`DrawBuyMenu` 仍是文字按钮，
  属 UI 排版范围，本轮未改布局）。
