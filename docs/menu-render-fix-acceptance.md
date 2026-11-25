# 主菜单 3D 渲染质量修复验收

2026-11-24 · Windows · OpenGL 4.6 兼容上下文，NVIDIA GeForce RTX 5060 Laptop GPU · Blender 5.2.3 LTS

## 逐项验收

| 项目 | 结果 | 证据与边界 |
|---|---|---|
| T1 输出分辨率 | PASS | 2560×1600 输出按等比上限渲染 1728×1080。尺寸切换到 1440×900 时，主纹理和三个预览同步更新。 |
| T2 各向异性过滤 | PASS | 驱动支持时，上限 8×。盐市与庭院斜视地面可对照 P0 前后截图。 |
| T3 sRGB 底色 | PASS | 彩色贴图按 sRGB 解码，粗糙度、金属度、AO 和法线按线性数据采样。盐市色样见下文。 |
| T4 补光 | PASS | 三场景保留独立日光及天光参数，冷色补光提高暗部可读性。 |
| T5 固定管线镜面 | PASS | 金属度和粗糙度控制镜面强度和指数。固定管线回退的高光按顶点插值。 |
| T6 雾 | PASS | 雨历庭 19–43，盐市 15–36，商旅院 18–39。雾色随各场景地平线色和转场变化。 |
| T7 烘焙 AO | PASS | 三场景地面用 Cycles 烘焙 768² AO。回退底色在线性空间乘入 AO。最终 PBR 资产以原底色和独立遮挡通道呈现。 |
| T8 PBR-lite | PASS | GLSL 1.20 读取金属度、粗糙度和 AO，并用 ACES 近似映射。兼容 sRGB 帧缓冲在线性合成后编码一次。故意注入 GLSL 语法错误，日志记录真实驱动编译失败并回退，48 张菜单检查图全部生成。 |
| T9 法线 | PASS | Blender GLB 原有贴图中提取高频细节，给每场景三种石材、木材或织物生成切线空间法线图。UV 切线含镜像手性与退化三角形处理。T8/T9 截图可独立对照。 |
| T10 方向光阴影 | PASS | 2048² 单张深度图覆盖主菜单可见庭院。斜率偏移、3×3 采样和图边淡出已启用。T9/T10 可独立对照拱廊及树下阴影。 |
| T11 半球环境光 | PASS | 场景天光按世界法线 Y 在天色、地色之间混合，AO 只调制环境项。T10/T11 可独立对照。 |
| 实机画面终验 | 待 Sun 目检 | 查看三场景全输出对照和生产菜单实拍，确认斜视地纹、金属高光、阴影边缘及暗部层次。 |

[逐阶段截图与日志](../build-evidence/menu-render-fix) · [故障回退日志](../build-evidence/menu-render-fix/final-fallback/menu.log) · [生产菜单实拍](../build-evidence/menu-render-fix/production)

## 性能与程序校验

独立场景检查在 1280×720 输出下运行。时间为菜单渲染调用的 P95，跳过 20 帧预热后取 60 帧。

| 阶段 | 初始化 ms | 雨历庭 P95 ms | 盐市 P95 ms | 商旅院 P95 ms | 菜单检查 | 场景校验 | 玩法回归 |
|---|---:|---:|---:|---:|---|---|---|
| 修改前 | 453.66 | 4.74 | 7.96 | 5.90 | 48 张 | 0 项 | — |
| P0 | 393.18 | 4.63 | 6.91 | 5.85 | 48 张 | 0 项 | 1843 通过，0 失败 |
| P1 | 397.10 | 4.21 | 5.08 | 4.64 | 48 张 | 0 项 | 1843 通过，0 失败 |
| P2 | 380.83 | 5.48 | 5.22 | 5.46 | 48 张 | 0 项 | 1843 通过，0 失败 |
| P3 最终 | 456.11 | 4.80 | 3.89 | 4.61 | 48 张 | 0 项 | 1843 通过，0 失败 |

P3 首轮检查的初始化为 666.65 ms。最终版并行解析三个场景，按实际绘制路径上传贴图，并直接上传预览读回结果。原始首轮和最终日志均已保留。T8、T9、T10 用独立构建生成各 48 张图，T11 使用最终版，四份构建的菜单状态、角色活动区域和植被动画检查均通过。本机实际运行的玩法测试计数为 1843/0。

| 2560×1600 输出下的渲染方案 | 3D 渲染尺寸 | 雨历庭 P95 ms | 盐市 P95 ms | 商旅院 P95 ms |
|---|---:|---:|---:|---:|
| 同版固定管线、旧尺寸对照 | 1024×576 | 3.83 | 3.75 | 3.92 |
| 保留比例的生产上限 | 1728×1080 | 6.00 | 4.98 | 6.47 |
| 原生尺寸试跑 | 2560×1600 | 10.32 | 10.31 | 9.88 |

生产上限最慢一场相对旧尺寸对照为 1.65×。原生尺寸试跑最高为 2.75×，因此保留 1920×1080 的等比包络。四轮交替启动的中位数为 452.79 ms 对 402.47 ms，增量 50.33 ms。旧尺寸对照使用同版固定管线与最终资产。1280×720 的真正修改前二进制另列在上表。生产 D3D11 菜单另通过三套主视图、角色聚焦和俯视抓拍。

[1728×1080 帧时间](../build-evidence/menu-render-fix/final-full/scene-performance.csv) · [旧尺寸对照](../build-evidence/menu-render-fix/final-low/scene-performance.csv) · [原生尺寸试跑](../build-evidence/menu-render-fix/native/scene-performance.csv) · [四轮启动日志](../build-evidence/menu-render-fix/final-init) · [最终玩法回归](../build-evidence/menu-render-fix/p3/gameplay-final.log)

## 场景资产与画面对照

| 场景 | GLB 字节 | 顶点 | 材质 | 内嵌贴图最大边 | 截图 |
|---|---:|---:|---:|---:|---|
| 雨历庭 | 6,839,692 | 147,166 | 14 | 768 px | [阶段](../build-evidence/menu-render-fix/comparison/ancient_ruins-stages.png) · [PBR 分步](../build-evidence/menu-render-fix/comparison/ancient_ruins-pbr-steps.png) · [同机位 Eevee](../build-evidence/menu-render-fix/comparison/ancient_ruins-full.png) |
| 盐市 | 7,107,116 | 124,985 | 21 | 1024 px | [阶段](../build-evidence/menu-render-fix/comparison/dust_2-stages.png) · [PBR 分步](../build-evidence/menu-render-fix/comparison/dust_2-pbr-steps.png) · [同机位 Eevee](../build-evidence/menu-render-fix/comparison/dust_2-full.png) |
| 靛蓝商旅院 | 7,386,284 | 149,724 | 15 | 768 px | [阶段](../build-evidence/menu-render-fix/comparison/mirage-stages.png) · [PBR 分步](../build-evidence/menu-render-fix/comparison/mirage-pbr-steps.png) · [同机位 Eevee](../build-evidence/menu-render-fix/comparison/mirage-full.png) |

原始 Blender 源仍在 `tools/menu-scenes/`。未烘焙和烘焙底色版 GLB 分别保存在证据目录的 `p2/unbaked` 和 `p2/baked-base`。几何、节点和动画数据逐字节核验一致，原材质数量一致，所有内嵌图片可解码。出图使用游戏相机位置、朝向和垂直视场。阶段截图为各自 1280×720 SDL 输出的渲染器抓拍，最终场景与 Eevee 原图均为 2560×1600。角色和聚光灯仅出现在游戏抓拍中。盐市右侧受光灰泥色样见测值，游戏画面较亮、较暖，差异包括模拟太阳及环境光和 ACES 近似与 Blender AgX 的映射差异。

[原资产备份](../build-evidence/menu-render-fix/p2/unbaked) · [烘焙日志](../build-evidence/menu-render-fix/p2/bake.log) · [GLB QA](../build-evidence/menu-render-fix/p3/asset-qa.log) · [游戏全输出 PNG](../build-evidence/menu-render-fix/final-full) · [Eevee 原图](../build-evidence/menu-render-fix/eevee) · [色样](../build-evidence/menu-render-fix/comparison/color-sample.log)
