# PixelEngine 主页面 3D 场景重做 — 给 Codex 的提示词

> 以下全文可直接粘贴给 Codex。背景与调查结论在文末附录（附录是给 Sun 的参考，不是给 Codex 的额外指令；Codex 按正文执行即可）。
>
> 任务一句话：把主页面背景的 dust_2 场景，换成基于 CS 真实场景（de_dust2 中路）的精美 3D 建模，用 MCP 连接 Blender 重做；只动 dust_2 一个场景，其他场景、角色、菜单 UI、加载器一律不动。
>
> **2026-11-23 20:39 更正（Sun 亲口纠正，上一版写错了两处）**：
> - Blender **已经装好了**：`D:\Blender\blender.exe`，5.2.3 LTS，已验证 `blender --version` 能跑。之前说「没装」是错的——它装在 D 盘，不在 Program Files、也不在 PATH 里，所以上一轮排查漏了。
> - Codex 的 MCP **已经配置好了**：`~\.codex\config.toml` 里已有 `[mcp_servers.blender]`（mcp-for-blender，127.0.0.1:9876）。之前说「仓库里没有任何 MCP 配置」也是错的——MCP 配置在 Codex 自己的配置目录里，不在仓库里。
> - **贴图现在允许加**：Sun 已批准，为保证背景精美，可以给加载器新增贴图支持（见第 3、4、6 节）。上一版「不要用贴图、那是第二阶段」的限制作废。

---

## 1. 先读现状（不要猜，按这些文件为准）

- 主菜单背景是**实时 3D 渲染**，不是图片/视频：`PixelEngine/Graphics/Menu3D.cpp` + `PixelEngine/Graphics/Menu3DModel.hpp`。`Menu3DModel` 是轻量 glTF 2.0 加载器：读 `.gltf/.glb`，解析 skin/骨骼/动画，CPU 蒙皮，OpenGL 固定管线绘制**纯色低模，无贴图**。只支持「单场景、TRS 节点、LINEAR/STEP 动画、无纹理」的装饰模型。
- 三个场景轮换：`ancient_ruins` / `dust_2` / `mirage`，切换时 0.5 秒交叉淡入淡出；场景预览图由 `CaptureScenePreviews()` 在启动时实时抓拍（不要再去找旧的 `*.png` 预览，那些是废弃的）。
- 当前 dust_2 场景实测数据（`assets/models/menu/scenes/dust_2.glb`，1,034,692 字节）：19 个网格、30 个 primitive、12 种纯色材质、约 56,682 顶点、**0 张贴图、0 个纹理**、1 个风动剪辑（18 个通道）。
- 场景来源：`tools/menu-scenes/create_scenes.py`，**程序化生成**——整个场景就是立方体/圆柱/球体拼出来的：盒子房子、拱门、板条箱、铁桶、棕榈叶、一辆盒子拼的废弃汽车。源文件 `tools/menu-scenes/dust_2.blend`，导出到 `assets/models/menu/scenes/dust_2.glb`。这就是 Sun 说的「太简单」：一眼看去全是方盒子，没有真实 CS 地点的辨识度。
- 相机是固定的：主相机 eye `(0, 1.05, 8.0)`、look `(0, 0.95, 0)`（`Menu3D.cpp` 里的 `kMainCamY/kMainCamZ/kMainLookY`）；另有 focus 相机（2.00, 8.6）和一个俯视扫过镜头。Blender 预览相机在 `(0,3,12)`、28mm 镜头。场景必须按这个固定机位取景。
- 角色模型（`assets/models/menu/ct.glb`、`t.glb`、`chicken.glb`）和配件（`assets/models/custom/`）与本次任务无关，不要碰。

## 2. 场景选定：de_dust2 中路（Mid）

只重做 dust_2 这一个场景。选**中路**，不选 A 大/B 点，原因：

1. 中路是 CS 里最具代表性的点位——中路双门、Xbox、猫道，玩过 CS 的人一眼就能认出来，A 大/B 点在小窗里辨识度低得多。
2. 构图天然适合现有相机：中路是横向展开的墙面 + 纵深的通道，正好铺满 Z=-3 以后的背景带，角色站在前面不挡关键元素。
3. 元素密度高：双门、Xbox、猫道楼梯、沙袋、木箱、拱门全挤在一段可视范围里，小场景也能做出丰富度。

场景里必须有的元素（按重要性排序）：

1. **中路双门**：绿色铁皮双门 + 门框 + 上方横梁，门缝透一点光；门板要有铆钉/把手/磨损质感（可用贴图，也可用材质色差）。
2. **Xbox**：中路标志性的跳箱木箱，木纹用深浅两种木色错开表现，边角做倒角磨损。
3. **猫道楼梯**：通往 A 小的阶梯 + 扶手 + 平台，台阶要有高度差和侧面挡板。
4. **A 小拱门**：石质拱门 + 门洞后的暗色通道。
5. **沙袋墙 + 木箱堆 + 铁桶**：中路两侧的掩体，沙袋要做出一排排的弧形堆叠感，不要一个方盒子了事。
6. **两侧房屋立面**：窗户（百叶窗、木板封窗、破窗）、阳台、屋顶线、烟囱/通风管、墙面裂缝与剥落（用色块表现）。
7. **地面**：石板路 + 沙地 + 裂缝、沙堆、散落的碎石。
8. **细节道具**：棕榈（带风动）、废弃汽车（比现在盒子车精细：车身弧线、车窗、轮胎、锈迹色块）、电线杆 + 电线、墙上海报/涂鸦（色块表现）、路灯、垃圾桶。
9. **远景**：屋顶剪影 + 天空底色做背景板，不用建整座城。

## 3. 视觉规格

- **风格**：与现有菜单一致的扁平低模（flat-shaded low-poly）打底，几何密度和细节量要明显高于现在；**贴图现在允许用**（Sun 2026-11-23 已批准）：关键表面（门板、木箱、墙面、地面）可以用贴图做质感，配合几何 + 倒角（bevel）+ 材质色差。贴图只为背景精美服务，不要喧宾夺主、不要把场景做成写实风。
- **光照**：暖色沙漠阳光，长影方向一致；Blender 预览沿用现有做法——Area 灯（圆盘、能量 1700、尺寸 12）、世界底色 `(0.23, 0.25, 0.28)`；主色调沙黄 + 石灰 + 绿门 + 锈红。
- **氛围**：午后沙漠小镇，干燥、明亮、略带风沙感。风动：棕榈叶、旗帜、篷布轻微摆动（0–120 帧循环）。
- **镜头**：按现有主相机取景，场景主体放在 Z=-3 以后的背景带；同时兼顾 focus 相机和俯视镜头，不要只在一个角度好看。

## 4. 技术规格

- **Blender 5.2.3 LTS**（`assets/models/CREDITS.md` 注明的制作版本）。**已装好，不用再装**：`D:\Blender\blender.exe`，已验证 `blender --version` 能跑（不在 PATH 里，调用时用完整路径）。
- **贴图支持是本次任务的一部分**（Sun 已批准的新增配置）：扩展 `Menu3DModel`，加 UV 解析、贴图加载、纹理管线，让带贴图的 GLB 能在主菜单画出来。只加新能力，不改现有相机参数、加载逻辑、0.5 秒切换、预览抓拍（见第 6 节规则 2）。
- **源文件**：`tools/menu-scenes/dust_2.blend`（改这里）。**生成脚本**：`tools/menu-scenes/create_scenes.py`——把新场景的建造逻辑写进脚本，保证可重复生成，**不要只改 .blend 不改脚本**（现在三个场景都是脚本生成的，这是项目的规矩）。
- **导出**：`assets/models/menu/scenes/dust_2.glb`；导出设置沿用现有：GLB 格式、动画用 NLA、强制采样、不导出相机/灯光；导出前按材质合并网格。**贴图要一起打进 GLB**（嵌入式，不用外挂文件），贴图单张 ≤ 2048px。
- **预算**：顶点 ≤ 150,000（现在约 56,682）；GLB ≤ 8 MB（含贴图；原来纯色版约 1.0 MB、预算 2.5 MB，贴图放进来后放宽到 8 MB）；材质 ≤ 24 种（现在 12 种）。主菜单帧率不低于改动前。
- **风动动画**：保持恰好 **1 个剪辑**，名 `Menu_Environment`，0–120 帧、30fps 循环。`create_scenes.py` 里有断言要求 18 个通道——如果增减风动叶片，同步更新断言里的通道数，并在交付说明里写明改了多少。
- **坐标约定**：Blender Z 为高度，Blender -Y 对应游戏 Z；地砖顶面高度 0，基础地面顶面 -0.02（防共面闪烁）。
- **校验**：`blender --background --python tools/menu-scenes/validate_scenes.py`，必须 0 findings 才能算完。

## 5. MCP + Blender 操作流程

1. Blender 已装好（`D:\Blender\blender.exe`，5.2.3 LTS），先确认 `D:\Blender\blender.exe --version` 能跑。
2. 接 MCP：**客户端已配置好**，`~\.codex\config.toml` 里已有 `[mcp_servers.blender]`（mcp-for-blender，127.0.0.1:9876），不用重新配。先验证 Blender 侧的 MCP 插件已启用、能连通（最小操作：新建一个立方体并改名），通了再开始建模。**如果 MCP 连不上，改用仓库现有流程**：`blender --background --python tools/menu-scenes/create_scenes.py` 生成，不要卡在 MCP 上。
3. 在 Blender 里按第 2、3 节建模，每完成一个大件（双门、Xbox、猫道、房屋立面）就导出一次 GLB 验证，不要攒到最后。
4. 跑校验：`blender --background --python tools/menu-scenes/validate_scenes.py`，0 findings。
5. 编译主程序（`D:\build.ps1`），进游戏看主菜单，截图；再跑 `D:\build_tests.ps1`（gameplay_tests）。
6. 交付：改动留工作区，**不提交、不 push**，由 Sun 过目后再定。写交付说明：改了什么、顶点数/体积、校验结果、截图。

## 6. 不可破坏的规则（逐条，违反即返工）

1. **只动 dust_2 一个场景**：`mirage`、`ancient_ruins`、角色模型（ct/t/chicken）、武器、配件一律不动；**不删除任何现有模型、材质、动画、决策或数据**（Sun 的不删除原则；PIX-37 计划首条硬规则：不删任何数据）。
2. **加载器只加贴图能力，不动其他**：`Menu3DModel` / `Menu3D.cpp` 的相机参数（`kMainCamY=1.05`、`kMainCamZ=8.0`、`kMainLookY=0.95`）、加载逻辑、0.5 秒切换、预览抓拍逻辑一律不动；**本次任务允许给加载器加贴图支持**（解析 UV、加载贴图、纹理管线，见第 4 节）——这是 Sun 2026-11-23 批准的新增配置，以保证背景精美为目的。只加新能力，不重构加载器。
3. **不动菜单 UI 与交互**：`GameUI.cpp` 的主菜单按钮、布局、流程原样；场景只是背景。
4. **保护区**：所有实体道具保持在 **Z=-3 以后**；角色活动区 **X=-6.3..6.3、Z=-2.7..6.1、高度 0..6.5 内 120 帧全程无任何道具进入**（`validate_scenes.py` 会逐帧查，`create_scenes.py` 开头也有写）。
5. **确定性红线**：本次是美术工作，不涉及随机数；如需任何随机性，走独立的 `rng_humanizer_`，**绝不碰全局 `rng_`**；不改回放哈希工具（见 `docs/PIX-37-deepseek-brief.md`、`docs/PIX-37-rules-coach-wiring-plan.md`）。
6. **不用 Valve 原版资产**：不从 CS / CS2 / CS:GO 里反编译、提取、复制任何地图、贴图、模型；场景是「按 CS 中路的样子重新建模」，不是把 Valve 的地图文件拿来用；新用到的任何第三方素材都要在 `assets/models/CREDITS.md` 里写清来源与授权。
7. **授权**：只用 CC0 / CC-BY 或自制内容；授权不明的素材不用。网上的 CS 地图模型（Sketchfab 的 de_dust2、cs16forge 的 .bsp）都是 Valve 地图的衍生品，**不要直接拿来用**（调查结论见附录）。
8. **性能**：GLB ≤ 2.5 MB、顶点 ≤ 150k；主菜单帧率不低于改动前；不给游戏对局内增加任何负担（菜单场景只在主菜单加载）。
9. **验收**：视觉改动必须附进游戏截图，由 **Sun 目检通过才算完**（项目发行标准）；他没点头之前不算完。

## 7. 验收标准

- `validate_scenes.py` 0 findings；`create_scenes.py` 可重复生成同一 GLB。
- 进游戏主菜单截图：中路场景一眼可认是 CS dust2；三个场景切换正常；角色、按钮、文字无遮挡。
- Sun 目检通过。
- `gameplay_tests` 通过数不减少；主菜单加载时间不明显变长。

## 8. 贴图支持（已批准，本次任务内完成，不再是第二阶段）

- 扩展 `Menu3DModel`：解析 UV、加载贴图、走纹理管线（见第 4 节）。贴图单张 ≤ 2048px，打进 GLB，不外挂文件。
- 贴图来源：自制优先；第三方贴图只用 CC0 / CC-BY，来源与授权写进 `assets/models/CREDITS.md`；**绝不用 Valve 的原版贴图**（见第 6 节规则 6）。
- 验收时除第 7 节外，另查：带贴图的 GLB 在主菜单正常显示、无紫块/无贴图丢失；三个场景切换正常；主菜单帧率不低于改动前。

---

## 附录：现成场景调查结论（给 Sun 的参考，2026-11-23 查）

结论先说：**网上没有可直接用的现成场景**。不是没有 CS 模型，而是每一个都撞上授权或格式的硬墙，所以直接出上面这份提示词。

| 候选 | 链接 | 授权 | 规模 | 结论 |
|---|---|---|---|---|
| De_Dust2_2020（Leafia dev.，Sketchfab） | https://sketchfab.com/3d-models/de-dust2-2020-d7fe8d2107ea4b6c9a3fdde6f0261f63 | CC-BY（页面注明） | 58.7k 三角面 / 51.5k 顶点，CS 1.6 整图，1,379 次下载 | ❌ 不用。CC-BY 只覆盖上传者的文件，不覆盖 Valve 的地图版权——这是 Valve 地图的衍生品，直接用进游戏是授权陷阱；且是带贴图的 PBR 模型，当前加载器画不出来；整图还得切成小场景 |
| de_dust2 - CS map（vrchris，Sketchfab） | https://sketchfab.com/3d-models/de-dust2-cs-map-056008d59eb849a29c0ab6884c0c3d87 | 页面未注明，授权不明 | 未注明 | ❌ 不用。授权不明 + 同样是 Valve 地图衍生品 + 带贴图 |
| De Dust2 V5（opapinguin，Sketchfab） | 仅看到 embed 页，未拿到模型页 | 未核实 | 未注明 | ❌ 不用。授权与格式均未核实 |
| de-dust2-map（madexc，Sketchfab） | 未展开核实 | 未核实 | 未注明 | ❌ 不用。未核实 |
| DE_Dust2 BombSite A（Joe_Y，Sketchfab） | https://sketchfab.com/3d-models/de-dust2-bombsite-a-9cca30921cde48b1a4423662daa2b16a | 未注明 | 3k 三角面 / 2k 顶点，低模 B 点 | ❌ 不用。授权未注明；且只有 B 点一个角，不够做主背景 |
| Mirage B Site - Light Version（WeeklyMozart，Sketchfab） | Sketchfab 搜索结果 | Free Standard | 73.8k 三角面，AI 生成，5 次下载 | ❌ 不用。AI 生成、采用量极小，且同样是 Valve 的 Mirage B 点衍生品 |
| cs16forge 的 .bsp 地图（如 de_dust2_2020，58.2 MB） | https://cs16forge.com/maps/de/de_dust2_2020/ | 未注明（CS 1.6 地图） | .bsp + .wad 贴图，不是 Blender 场景 | ❌ 不用。格式不对（.bsp 进不了菜单加载器），且是 Valve 地图 |
| itch.io 通用沙漠低模包（如 Low Poly Desert Oasis Pack，27 个模型，2,844 三角面；Desert Low Poly Pack 免费档 13 个资产） | https://reodesuxz.itch.io/low-poly-desert-oasis-pack 等 | 免费可商用、无需署名（页面注明） | 扁平低模、无贴图、GLB/FBX/OBJ/.blend | ⚠️ 只能当**道具参考**，不能当场景。这些是通用沙漠/西部/卡斯巴风格，不是 CS 场景；且与现有纯色管线兼容（无贴图）。如果 Codex 要借用其中个别道具（如棕榈、木箱、帐篷），必须写进 `assets/models/CREDITS.md` |

否决的三个硬原因（任一条就够了）：

1. **授权**：所有「真实 CS 场景」模型都是 Valve 地图的衍生品。Sketchfab 的 CC-BY 只管上传者那个文件的授权，不管 Valve 地图本身的版权；项目 `assets/models/CREDITS.md` 已明确「不使用 CS2/CS:GO 的原版角色资产」，地图同理。
2. **格式**：当前 `Menu3DModel` 只画纯色、无贴图。Sketchfab 的 CS 模型全是带贴图的 PBR 模型，直接导进来也显示不出来；要显示就得先升级加载器——这次任务已批准升级加载器（见正文第 4、8 节），但现成场景仍有授权与规模两道墙，所以还是重做。
3. **规模**：整图模型（58.7k 面）要切成只占 Z=-3 以后一小条背景带的 vignette，改动量不比重新建模小；现成场景的相机、光照、比例也都要重调。

所以结论是：**没有现成的，直接按正文提示词让 Codex 在 Blender 里重做**。通用沙漠包只建议作为道具造型的参考，不建议直接拼进场景——真要拼，授权写进 CREDITS.md。
