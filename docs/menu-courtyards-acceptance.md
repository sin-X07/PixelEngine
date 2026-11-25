# 远古遗迹与 Mirage 主菜单背景验收

2026-11-24。两套场景均从白模开始，经 Blender 5.2.3 LTS 的实时 MCP 会话建模、上色和导出。两张图分别占用原有的 `ancient_ruins` 与 `mirage` 菜单位。最终美术目检待 Sun 确认。

## 实景参考与原创构图

**远古遗迹 · 雨历庭**取意于 Ancient 的 A 区石庭，保留湿润石材、树影与考古建筑的环境特征。庭院原创设计以右侧悬空历盘为主体，玉色雨滴悬在中央，盘沿分布刻度和内指针。左侧残塔与折阶向后收，板根树、藤根和不同叶型的蕨类分出近、中、远层。铺地上的双环呼应历盘，俯视镜头可看见集水槽。参考画面见 [Ancient A 区石庭](https://www.hltv.org/news/42449/temple-to-a-main-angle-removed-as-ancient-gets-new-shaders-nighttime-version-in-update)。

**Mirage · 靛蓝商旅院**取意于 Mirage A 区宫殿阳台，取其高处木构、檐下阴影和沙色街墙。原创庭院由左侧风塔和右侧三跨拱廊组成，近景有釉陶摊和八角水池。拱廊上分别建了车木栏杆、八角花砖、垂毯和黄铜吊灯；右上方的蓝釉穹顶以十二条金属肋收边。参考位置见 [Valve 的 Mirage A 区阳台更新记录](https://www.counter-strike.net/newsentry/674006995886407685)。

## 交付资产

| 指标 | 雨历庭 | 靛蓝商旅院 |
|---|---:|---:|
| 可编辑场景对象 | 839 | 746 |
| GLB 导出顶点 | 83,482 | 76,854 |
| GLB 字节 | 3,156,172 | 3,006,308 |
| 导出网格 / 材质 | 19 / 13 | 19 / 15 |
| 内嵌底色图 | 7 张 JPEG | 7 张 JPEG |
| 风动通道 | 18 片凤梨叶 | 16 片棕榈叶、2 张织毯 |

两套资产各含一张 768×768 铺地图及六张 256×256 表面贴图。投影按各自建筑与植被的几何形状烘入铺地。网格、花纹和纹理均由项目自制。两套风动均为单个 `Menu_Environment` 剪辑，第 0–120 帧、30 fps。贴图在 GLB 中内嵌，在可编辑源中也已打包。详见[素材记录](../assets/models/CREDITS.md)。

| 场景 | 可编辑源 | 游戏资产 | 白模与游戏内实拍 |
|---|---|---|---|
| 雨历庭 | [Blender](../tools/menu-scenes/ancient_ruins.blend) | [GLB](../assets/models/menu/scenes/ancient_ruins.glb) | [白模](../build-evidence/menu-pair/ancient_ruins/blockout.png) · [主菜单](../build-evidence/menu-pair/ancient_ruins/game/main.png) · [角色聚焦](../build-evidence/menu-pair/ancient_ruins/game/focus.png) · [俯视转场](../build-evidence/menu-pair/ancient_ruins/game/overhead.png) |
| 靛蓝商旅院 | [Blender](../tools/menu-scenes/mirage.blend) | [GLB](../assets/models/menu/scenes/mirage.glb) | [白模](../build-evidence/menu-pair/mirage/blockout.png) · [主菜单](../build-evidence/menu-pair/mirage/game/main.png) · [角色聚焦](../build-evidence/menu-pair/mirage/game/focus.png) · [俯视转场](../build-evidence/menu-pair/mirage/game/overhead.png) |

## 统一验收

- 实机截图取自生产 Engine 的 D3D11 菜单合成和 OpenGL 场景渲染，输出为 1920×1080。主菜单保留中央按钮与两侧角色位置。场景专用日光、天空和菜单遮罩随画面调整。
- 活动区沿用这两个场景的现有游戏坐标范围，X −6.3 至 6.3，Z −2.7 至 6.1，高度 0 至 6.5。逐帧覆盖 0–120 帧，雨历庭检查 835 个道具，靛蓝商旅院检查 742 个，均为 0 项冲突。盐市亦为 0 项。[校验日志](../build-evidence/menu-pair/clearance.log)
- Release 主程序、玩法回归与菜单检查工具均由 CMake 构建。默认回归 1,843 通过、0 失败，与改动前一致。桌面体验回归 2,157 通过、0 失败。[玩法回归](../build-evidence/menu-pair/gameplay.log) · [桌面体验回归](../build-evidence/menu-pair/experience.log)
- 菜单检查覆盖三场景切换、装扮与漫游状态、30/60/120 Hz 动画时钟、所有风动通道以及 48 个视图。生产菜单测试另验两张场景均成功产生预览，再取三机位实拍。[菜单检查](../build-evidence/menu-pair/benchmark-new.log) · [实机截图检查](../build-evidence/menu-pair/game.log)
- 独立 Blender 进程分别按两场景和完整三场景重建。MCP 导出、两次重建与 Release 包中的对应 GLB 字节一致。雨历庭 SHA-256 为 `2146a4a010da6fac2c0c7a3e094ff4ff43032cf40d88100b7bd01dd2f0d11fba`，靛蓝商旅院为 `dcbbbad2579c39c398c90899eb32b5bb4b4351c55fc3f3c3b2ddade46530deff`。

同一机器、同一版 `menu_art_review`，三张菜单背景同时初始化，盐市固定使用 PIX-39 版本，只替换另两份 GLB。以 1280×720 交替运行旧资产和新资产，各三次。渲染计时跳过 20 帧预热，再取 60 帧，每一列取三轮中位数。

| 指标 | 旧资产 | 新资产 |
|---|---:|---:|
| 整个菜单初始化 | 279.99 ms | 321.24 ms |
| 雨历庭平均渲染耗时 / P95 | 3.108 / 3.342 ms | 2.801 / 3.012 ms |
| 靛蓝商旅院平均渲染耗时 / P95 | 3.138 / 3.321 ms | 2.840 / 3.023 ms |

本机两张背景的渲染耗时均下降，整个菜单初始化增加 41.25 ms（约 15%）。以上是菜单渲染调用计时，首开观感与最终画面留待目检。[六轮原始记录](../build-evidence/menu-pair/performance.csv)

## 复跑

在仓库根目录执行。下列导出命令把副本写到证据目录。

```powershell
& D:/Blender/blender.exe --background --factory-startup --python-exit-code 1 --python tools/menu-scenes/create_scenes.py -- --themes ancient_ruins mirage --output-dir build-evidence/menu-pair/reproduced
& D:/Blender/blender.exe --background --python-exit-code 1 --python tools/menu-scenes/validate_scenes.py
cmake --build build-release --target EngineApp
cmake --build build-tests --target gameplay_tests menu_art_review
.\build-tests\gameplay_tests.exe
.\build-tests\gameplay_tests.exe --experience
.\build-tests\gameplay_tests.exe --menu-pair
.\build-tests\menu_art_review.exe assets/models/menu build-evidence/menu-pair/benchmark-new --scenes
```

