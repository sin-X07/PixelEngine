# 主菜单灰尘与背景建筑验收

2026-11-25 · Blender 5.2.3 LTS · Windows / NVIDIA GeForce RTX 5060 Laptop GPU

## 灰尘与背景建筑

| 验收项 | 结果 | 验证 |
|---|---|---|
| A1 现状诊断 | PASS | 盐市原效果是 100 个同尺度矩形和 12 个扁平尘团。商旅院走原来的 35 个小矩形分支。原代码对每个场景分别采用固定半径、颜色和透明度，颗粒横向取模循环，深度集中在窄带，使用无灯光的纯色绘制。保留游戏原图和 Blender 原场景相机画面。 |
| A2 真实几何、柔边和光照分区 | PASS | 两张图以 Blender Geometry Nodes 实例化并实现化为 42 顶点实心颗粒。按三个深度档调整粒径、亮度和速度。太阳与可视射线筛选发射位置，各场景共用一种暖色透明材质，覆盖率由纹理控制。 |
| A3 4 秒循环 | PASS | 保留 18 条原风动通道，灰尘平移关键帧并入唯一的 `Menu_Environment`，0–120 帧、30 FPS。GLB 二进制复核每条灰尘通道首尾一致。 |
| A4 旧实现替换 | PASS | 两场均在旧特效代码运行时先取得新颗粒入镜证据，再移除旧灰尘。场景 GLB 半透明网格参与遮挡测试和颜色混合，绕过深度写入及实心阴影深度，避免尘粒投出斑点。雨历庭保留原 35 个粒子。 |
| B1 背景审计 | PASS | 各场景逐对象记录位置、相机距离、材质、贴图、顶点与待补细节。 |
| B2 中模提质 | PASS | 盐市补入窗洞、拱券、窄木窗、烟囱和棚檐。商旅院补入带窗格的马蹄拱、屋檐和穹顶收边。雨历庭保留阶台轮廓，补入石砌接缝、凹龛和层间檐带。 |
| B3 贴图与遮蔽 | PASS | 复用本场景前景贴图的表面变化，生成背景墙、木窗、饰边、屋面和遮篷底色。高度场局部遮蔽乘入底色图，新增背景材质无需额外贴图通道。 |
| B4 空气透视 | PASS | 沿用工作区上一轮修复后的逐场景雾参数与 PBR 渲染路径。游戏抓拍中远景明度对比低于前景，窗洞和檐口仍可读。 |
| A5/B5 资产与运行验证 | PASS | 三场景各自核验。覆盖网格、全循环位置、活动净空、菜单状态和玩法回归。 |
| 实机观感 | 待 Sun 目检 | 在正常主菜单确认灰尘疏密、转场以及远景细节和雾的层次。 |

盐市灰尘在本机 Blender 后台制作。MCP 连通后，盐市背景、商旅院灰尘与背景、雨历庭背景均在 Blender MCP 中制作。Eevee 对照使用与游戏一致的相机位置、朝向与纵向视场。Blender 和 OpenGL 使用各自的灯光与色调映射。

## 资产实测

| 场景 | 新灰尘数（近 / 中 / 远） | 灰尘动画通道 | 审计背景对象 / 新增细节件 | 顶点（前 → 后） | GLB 字节（前 → 后） | 材质（前 → 后） | 最大贴图 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 盐市 | 216（80 / 88 / 48） | 27 | 32 / 522 | 124,985 → 201,681 | 7,107,116 → 12,039,696 | 21 → 27 | 1024×1024 |
| 靛蓝商旅院 | 256（80 / 88 / 88） | 32 | 21 / 1106 | 149,724 → 307,513 | 7,386,284 → 14,846,452 | 15 → 21 | 768×768 |
| 雨历庭 | 原程序粒子 35 | 0 | 18 / 702 | 147,166 → 237,908 | 6,839,692 → 10,971,668 | 14 → 19 | 768×768 |

灰尘半径为 0.020–0.059 m，最大位移约 0.136 m，实测首尾位置差为 0。四秒内每隔一秒用评估后的静态场景几何检查太阳射线，盐市最少 213/216 可直达太阳，商旅院为 256/256。新增背景底色纹理为 512²，灰尘覆盖率纹理为 64×32。

| 场景 | 同机位四图对照 | Blender 视口 | 资产与程序日志 |
|---|---|---|---|
| 盐市 | [Blender 与游戏前后画面](../build-evidence/menu-dust-bg/dust_2/comparison.png) · [移除旧实现前的新颗粒](../build-evidence/menu-dust-bg/dust_2/dust/runtime-dust-proof.png) | [旧版](../build-evidence/menu-dust-bg/dust_2/before/viewport.png) · [新版](../build-evidence/menu-dust-bg/dust_2/final/viewport.png) | [审计](../build-evidence/menu-dust-bg/dust_2/final/background-authoring.json) · [网格 QA](../build-evidence/menu-dust-bg/dust_2/final/qa.json) · [GLB](../build-evidence/menu-dust-bg/dust_2/final/asset-verification.log) · [净空](../build-evidence/menu-dust-bg/dust_2/final/validate.log) · [回归](../build-evidence/menu-dust-bg/dust_2/final/gameplay.log) |
| 靛蓝商旅院 | [Blender 与游戏前后画面](../build-evidence/menu-dust-bg/mirage/comparison.png) · [移除旧实现前的新颗粒](../build-evidence/menu-dust-bg/mirage/dust/runtime-dust-proof.png) | [旧版](../build-evidence/menu-dust-bg/mirage/before/viewport.png) · [新版](../build-evidence/menu-dust-bg/mirage/final/viewport.png) | [审计](../build-evidence/menu-dust-bg/mirage/final/background-authoring.json) · [网格 QA](../build-evidence/menu-dust-bg/mirage/final/qa.json) · [GLB](../build-evidence/menu-dust-bg/mirage/final/asset-verification.log) · [净空](../build-evidence/menu-dust-bg/mirage/final/validate.log) · [回归](../build-evidence/menu-dust-bg/mirage/final/gameplay.log) |
| 雨历庭 | [Blender 与游戏前后画面](../build-evidence/menu-dust-bg/ancient_ruins/comparison.png) | [旧版](../build-evidence/menu-dust-bg/ancient_ruins/before/viewport.png) · [新版](../build-evidence/menu-dust-bg/ancient_ruins/final/viewport.png) | [审计](../build-evidence/menu-dust-bg/ancient_ruins/final/background-authoring.json) · [网格 QA](../build-evidence/menu-dust-bg/ancient_ruins/final/qa.json) · [GLB](../build-evidence/menu-dust-bg/ancient_ruins/final/asset-verification.log) · [净空](../build-evidence/menu-dust-bg/ancient_ruins/final/validate.log) · [回归](../build-evidence/menu-dust-bg/ancient_ruins/final/gameplay.log) |

各场景的 backup 目录保留本轮前的源文件和资产。QA 的预期警告包括浮动尘粒、重复铺贴坐标、平面多边形，以及组装件内部的接触面。接受的警告可在各场景 JSON 中逐条查看。三场景均保留 Blender MCP 视口抓拍和同机位 Eevee 对照。每张游戏全画幅原图均为 2560×1600，菜单 3D 纹理为 1728×1080。

## 性能与完整回归

测试以 2560×1600 SDL 输出运行，3D 纹理为 1728×1080。旧版二进制和三份备份资产与新版交替运行三轮。每场取 20 帧预热之后的 60 个菜单渲染调用。表中为三轮中位数，单位 ms。Blender 会话同时保持打开，原始日志保留各轮波动。

| 指标 | 实施前 | 实施后 | 增量 |
|---|---:|---:|---:|
| 菜单初始化 | 573.65 | 849.55 | +275.90 |
| 盐市平均 | 13.83 | 16.71 | +2.88 |
| 盐市 P95 | 17.45 | 22.14 | +4.68 |
| 靛蓝商旅院平均 | 14.68 | 16.93 | +2.26 |
| 靛蓝商旅院 P95 | 18.55 | 18.46 | -0.09 |
| 雨历庭平均 | 13.10 | 14.94 | +1.84 |
| 雨历庭 P95 | 17.34 | 18.42 | +1.09 |

- 三场景每次发布后的净空均为 0 项。最终检查覆盖 1607、1449、1953 个网格对象，顺序为雨历庭、盐市、商旅院。
- 盐市和商旅院的灰尘阶段与建筑阶段、雨历庭建筑阶段的 gameplay_tests 均为 **1843 通过、0 失败**。本机基线同为 1843/0。
- menu_art_review 每场发布后通过，共生成 48 个视图。故意注入 GLSL 语法错误后的固定管线回退也完成 48 个视图。生产 D3D11 路径通过三场景主视图、聚焦和俯视抓拍。

[三轮帧时间汇总](../build-evidence/menu-dust-bg/bench/summary.csv) · [雨历庭及全套最终菜单检查](../build-evidence/menu-dust-bg/ancient_ruins/final/menu.log) · [固定管线回退](../build-evidence/menu-dust-bg/fallback/menu.log) · [生产 D3D11 抓拍](../build-evidence/menu-dust-bg/production) · [Blender 视口汇总](../build-evidence/menu-dust-bg/viewport-index.png)

## 文件与目检

- 游戏资产为三份场景 GLB，编辑源为 tools/menu-scenes 下的三份 BLEND。复现脚本为 menu_dust.py、menu_background.py 和 upgrade_menu_scenes.py，沿用 salt_market.py 的合并导出入口。
- C++ 改动涉及 Menu3D 的旧灰尘分支、Menu3DModel 的透明网格深度和阴影处理，以及 menu_art_review 的环境动画检查。HEAD 为 bd34ce4，交付为工作区改动。
- Sun 目检时，盐市看绿门前和檐下的颗粒及后街拱窗，商旅院看中央空隙中的慢速颗粒、屋脊和风塔后方的拱窗，雨历庭看树后阶台的石缝、凹龛和层间檐带。每张图可顺带确认俯视转场和远景雾层。
