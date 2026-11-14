# PixelEngine 玩家体验槽点汇总

> 审计基准：`origin/main = 98f114f`（2026-11-13 20:02，含 13d1666“视野与瞄准方面的玩法升级”与 98f114f“击杀栏信息调整”）
> 审计方法：① 代码只读审计（`git show origin/main`，未改仓库）；② 素材实测（PIL 量图片尺寸、ffprobe 量音频）；③ Linux 实机运行（Xvfb + Mesa 软件渲染，`--autostart` 完整打完一局 8 回合，`gameplay_tests` 1019 项通过）。
> 注意：13d1666 已实装 PIX-31 视野计划的大部分内容，本报告在其之后审计；其中 G3、G10、G11、G12、G17、G18 是该提交引入的新问题或副作用。
> 优先级定义：**P0** 第一次玩就会撞上、或直接破坏核心体验；**P1** 明显影响体验、值得修；**P2** 打磨项、边缘情况。
> 总数：**81 项（P0 × 10，P1 × 38，P2 × 33）**

## 总览（P0 清单）

| # | 槽点 | 分组 |
|---|---|---|
| G1 | 三张主地图全是 1px/世界单位，1080p 下每个地图像素放大成 6×6 色块 | 画面与美术 |
| G2 | 手里拿的枪、地上掉的枪，全是单色剪影占位图 | 画面与美术 |
| G3 | 玩家视野实际上没有距离限制，100 的视野半径只管住了 BOT | 画面与美术 |
| G21 | 超过 100 世界单位的声音直接静音，整张图另一头的枪战什么都听不见 | 音频 |
| G43 | 打着打着不知道自己有多少钱，经济规划全靠猜 | HUD 与信息呈现 |
| G44 | 从背后被打死和从正面被打死，屏幕反馈一模一样 | HUD 与信息呈现 |
| G53 | 没有死亡画面，死的一瞬间直接切观战 | 流程与节奏 |
| G70 | 没有任何新手引导 / 操作说明，第一局不知道自己能按什么键 | 新手体验 |
| G71 | 买枪菜单不会自动打开，新手大概率空手打完第一个购买期 | 新手体验 |
| G77 | 启动失败对玩家是静默的：只写控制台，窗口一闪就没了，退出码还是 0 | 性能与稳定性 |

## 做得好的地方（简短肯定）

- 设置深度远超同类小项目：准星 / 瞄准线 / 雷达 / 开镜 / 雾效 / 音频分 4 个页签、可逐项调且能落盘。
- 顶部计分条信息很全（比分＋回合 / 阶段＋倒计时＋存活人数）；买枪菜单每张卡直接标“钱不够 / 已装备 / 已带满 / 阵营不符”，不用悬停猜；下包后顶部“C4 倒计时＋拆包中 %”清晰；雷达情报红点 4–6 秒淡出、飞行中手雷画爆炸范围圈，对新手友好。
- 角色精灵是真的（CT/T 各 6 帧 32×32，1100+ 色，另有描边版和 T 方 alt 版）；34 把枪每把都有独立开火音（33 个不同音频，仅 MP7/MP9 共用），时长按武器区分；81 处代码引用的音频文件在树上 81/81 全存在，零缺失引用；声像按左右位置分声道、自动武器独占通道防失声。
- 暂停的重新开始 / 退出有二次确认；结算页有两队记分板和 MVP 星标；选图、观战、买枪失败都有中文提示。
- 稳定性底子不差：Linux 实测完整打完一局 8 回合，全程日志 213 行零 error；`gameplay_tests` 1019 项全部通过。

---

## 1. 画面与美术（20 项：P0 × 3，P1 × 10，P2 × 7）

### G1（P0）地图全是马赛克，走近了墙还是糊的
- 玩家视角：这地图怎么全是马赛克，走近了墙还是糊的。
- 证据：`assets/cs2d/tilesets/dust_2.png` 880×880（`.tmj` 声明 440×440 tiles × 2px = 880×880 世界）；`mirage.png` 1168×912；`ancient_ruins.png` 976×912；死斗图 `office.png` 640×600。1080p 下 `BaseZoomForViewport` 封顶 6.0（`Visibility/VisibilityQuery.cpp:102-106`），即 1 地图像素 = 6 屏 px；要真正 1:1 需要 6 倍源图，退而求其次 2 倍（dust_2 需 1760×1760）才能明显改善。
- 建议：按 2 倍重制三张主地图（或恢复 G4 的 1600 版并重描碰撞）；短期先接受糊，但别再拿 880 当终点。

### G2（P0）我手里这把枪怎么是个色块？AK 和 M4 长得一模一样
- 玩家视角：手里拿的枪、地上掉的枪，全是单色剪影占位图。
- 证据：`CS2D/Weapons/WeaponDef.hpp:150-285` 的 41 把武器，`spritePath`（世界贴图）全部是 64×64、5–11 色、约 350 字节的纯色剪影（实测 `ak47.png` 7 色 360B、`awp.png` 7 色 369B、`knife.png` 7 色 325B），由 `tools/gen_weapon_placeholders.py` 生成（按类别填色：手枪灰、步枪绿、狙击橙……）。`DrawWeaponForCharacter`（`CS2DRenderer.cpp:772-800`）把它画在角色前方，尺寸仅 3–4.5 世界单位（18–27 屏 px），小归小，但玩家全程盯着它。
- 建议：画一套真正的俯视角武器贴图；复用 HUD 图标不合适（那是侧视图）。

### G3（P0）明明设定视野半径 100，实际一屏内的敌人只要在锥里、没墙挡，全看得见
- 玩家视角：视野半径的设定和实际看到的东西对不上，调参也不知道该信哪个数。
- 证据：`CS2DGame.cpp:45` 与 `CS2DRenderer.cpp:140` 都调了 `visibility_.SetDistanceLimited(false)`；`Visibility/VisibilityQuery.cpp:70` `if (!distanceLimited_) distFactor = 1.0f;` 直接跳过距离衰减；`Tuning.hpp` 的 `VisionRadius = 100` 只剩 BOT 在用（`CS2DGame.hpp:948` `BOT_VIEW_RADIUS`）。雾的 20 距离软边、开镜 / 边缘缩放改 `visionRadius_` 的逻辑因此全部失效（只改了个数值，没人再读它）。这是 13d1666 留下的设计自相矛盾。
- 建议：要么把玩家距离限制恢复（与 BOT 对称），要么删掉 `VisionRadius` 的“玩家与 BOT 共用”注释、承认玩家视野就是“锥形＋屏幕大小”，并按此重调 BOT 感知。

### G4（P1）高清图明明有，为什么游戏里还是糊的？
- 玩家视角（仓库视角）：1600×1600 的高清 dust_2 就躺在仓库里没人用，文件名还是中文“副本”。
- 证据：`assets/cs2d/tilesets/dust_2 - 副本.png` 1600×1600、4,141,443 字节；代码和 `.tmj` 里没有任何引用（`PixelEngine/*` 下 `git grep` 零命中）。`dust_2_prev.tmj` 声明的世界是 800×800 tiles × 2px = 1600，说明它曾是正式地图，后来世界缩回 880 时没删也没恢复。
- 建议：要么把它扶正为 `dust_2.png` 并按 1600 世界重描碰撞（工作量大），要么删掉；别留一个带空格 + 中文的 4MB 文件在仓库里。

### G5（P1）mirage 地图边缘外面那圈景色看着怪怪的，和地图对不上
- 玩家视角：mirage 地图边缘外围景色与地图错位。
- 证据：`assets/cs2d/perimeters/mirage_scene.png` 2848×2336，而 `mirage.png` 是 1168×912；dust_2 / ancient 的 scene 都是“地图 + 512”（880→1392、976→1488），只有 mirage 是“2 倍地图 + 512”（2336+512）。加载代码 `CS2DRenderer.cpp:421-425` 算 `scaleX = GetImageW()/mapW = 1168/1168 = 1`，没有任何针对 mirage 的 0.5 换算，于是 scene 按 2848×2336 世界单位画出，边缘带向外伸 840 单位（别的图是 256），地面循环砖 `perimeterTileW_` 也是 256 单位（应为 128）。`tools/map-perimeters/README.md` 写的“加载时根据原图和世界尺寸换算”在代码里不存在。注意：`map_perimeter_review` 工具对三图报 PASS，但它只校验加载与渲染不报错，不校验比例；接缝处仍需进游戏截图确认。
- 建议：用 `bake.py` 按 1680×1424（1168+512）重烘 mirage scene；或在 `LoadPerimeter` 里对 mirage 做 0.5 缩放。

### G6（P1）买枪菜单里一排彩色小棍子，分不清谁是谁
- 玩家视角：买枪菜单 / HUD 里的武器图标，29/41 也还是占位图。
- 证据：41 个 `*_d.png` 图标里 29 个是 64×32 或 128×32、5–11 色、350–650 字节的色块（`m4a4_d.png` 5 色、`ak47_d.png` 11 色、`awp_d.png` 8 色）。真正画过的只有约 12 个：6 颗手雷（128×128，778–1600 色）、`deagle_d.png`（1271 色）、`knife_d.png`（505 色）、`galil_d.png`（63 色）、`fn57_d.png` / `shootgun_d.png`（512×512，约 830 色）、`mag7_d.png`（512×384，7980 色）。
- 建议：按手雷图标的质量标准补齐其余 29 个图标。

### G7（P1）P2000、P250 和双持贝瑞塔在菜单里是同一张图？
- 玩家视角：三组不同的枪共用同一个图标文件，买枪时完全无法区分。
- 证据：`p2000_d.png` = `p250_d.png` = `dualberettas_d.png`（同一 blob `c34f5770`，64×64 5 色）；`nova_d.png` = `sawedoff_d.png`（`b591f159`）；`mp5sd_d.png` = `mp9_d.png`（`7f16fae1`）。
- 建议：哪怕先给不同占位色 / 形状，也别让三把枪共用一个文件。

### G8（P1）选菜单背景那几行只有名字，连个图都没有
- 玩家视角：菜单场景的三张预览图根本不存在，选场景时只有文字没有图。
- 证据：`Graphics/Menu3D.cpp:386-387` 加载 `assets/models/menu/scenes/{ancient_ruins,dust_2,mirage}.png`，但 `assets/models/menu/scenes/` 里只有 3 个 `.glb`；`Graphics/GameUI.cpp:414-418` 在预览为 null 时直接跳过画图、不报错，`LoadModels` 里对预览缺失也没有任何日志（只有 `.glb` 失败才记）。没有任何工具生成这些 PNG（`tools/*` 里零引用）。
- 建议：从 `.glb` 渲染导出三张预览 PNG，或改成直接用 3D 场景本身当缩略图。

### G9（P1）Linux 上中文会直接变方块（实测截图：主菜单中文全是 ????）
- 玩家视角（Linux 玩家）：界面中文全是口口，根本没法玩。
- 证据：全仓库零 `.ttf` / `.otf`。`Graphics/ImGuiUI.cpp:110-122` 只在 Windows（`C:/Windows/Fonts/msyh.ttc` 等）和 macOS（PingFang 等）找系统字体；`#else` 分支列的还是 macOS 路径，Linux 上一个都找不到 → `chosen` 为空 → `AddFontDefault()`（ImGui 内置字体无 CJK），中文变方块。注释自己也承认“中文可能显示为方块”。Linux 实测（Xvfb + Mesa，见“实测验证记录”）：主菜单 6 个按钮、标题后缀、局内记分板中文全部显示为 `????`，而本机实际装有 Noto Sans CJK SC（`fc-list` 共 1875 个字体），游戏从不查找。存在 `[global] font=` 手动指定路径的后门，但默认开箱即坏。
- 建议：内置一个 CJK 字体子集（如 Noto Sans SC 常用字子集）随游戏分发，或补 Linux 字体路径（`/usr/share/fonts/...`）。

### G10（P1）准星推到屏幕边，镜头基本不缩；偶尔缩了，视野锥却从 120° 收成 30°
- 玩家视角：边缘缩放几乎触发不了，触发了反而更看不见，新露出来的屏幕区域全是灰雾。
- 证据：触发带 `EdgeZoomOutZone = 0.005f`（`Tuning.hpp:31`）：1080p 下约 1.6 世界单位 ≈ 10 屏幕像素，准星要贴到离边 10px 内才触发，退出迟滞 5 倍；`CS2DGame.cpp:1215` 锥角 `120° × (1 + (0.25 − 1) × edgeZoom_)`，满缩放时只剩 30°，与 0.7 倍缩放（`EdgeZoomOutMin`）的“看得更宽”直接打架——雾与敌人剔除共用这个锥角。这是 13d1666 的实现问题。
- 建议：触发带放宽到约 40–60 屏幕像素；边缘缩放时锥角不收或只收到约 90°，否则这功能等于“花屏看雾”。

### G11（P1）拿 AWP 开 2 倍镜，视野一点不缩，等于白嫖望远镜
- 玩家视角：开镜现在是纯赚：2 倍放大、完整 120° 视野、没有任何代价，狙打近点也没惩罚。
- 证据：13d1666 删了开镜圆形可见区与镜外 242 雾；`CS2DGame.cpp:716` 注释已改成“普通与开镜共用前方扇形视野，镜筒只负责瞄准装饰”；`CS2DRenderer.hpp` 的 `SCOPE_FOG_ALPHA` 在 98f114f 里被删。开镜只剩相机偏移＋镜筒装饰。
- 建议：至少恢复轻度的镜外压暗 / 收窄，或给开镜加移速与腰射惩罚，否则 AWP 没有对位短板。

### G12（P1）死斗里被人从背后打死，连人影都看不到
- 玩家视角：死斗的视野规则被顺手改严了：以前只看墙，现在还要在 120° 锥里。
- 证据：13d1666 把 `CS2DGame::Render` 的死斗分支从“只做墙体遮挡剔除”改成与队伍模式统一的扇形＋墙体＋烟雾剔除（`CS2DGame.cpp:1278–1290`，`friendly = !dmMode && ...`）。9 人混战＋背后无提示（G44），死斗体验变差。
- 建议：死斗恢复放宽的视野（或至少放宽到 180°），或补受击方向提示。

### G13（P1）屏幕中央一条亮青色线穿过墙和灰雾直指准星，瞄准不用看人、不用看雾
- 玩家视角：瞄准线默认开启、穿墙、画在雾上面，等于自带激光指示器，雾的意义少一半。
- 证据：`CS2D/CS2DOverlay.hpp:22` `aimLine = true` 默认开；`CS2DOverlay.cpp:183–187` 除刀 / 雷 / C4 外全武器都画；`CS2DRenderer.cpp` `DrawAimLine` 从枪口画满全屏，墙后一段用虚线继续延伸；Overlay 在雾之后渲染，线压在雾上。设置里能关，但游戏内无快捷开关、无提示。
- 建议：默认关闭或只画到第一堵墙为止（墙后不画虚线），雾内段减淡。

### G14（P2）死斗图边缘外面光秃秃的，和别的图不一样
- 玩家视角：死斗图 office 出了地图就是一片平色，没有外围景色。
- 证据：`assets/cs2d/perimeters/` 只有 dust_2 / mirage / ancient_ruins 各一对 `_scene.png` + `_ground.png`，没有 office 的；`LoadPerimeter` 找不到文件就静默返回，只剩按地图平均色填的纯色（`CS2DRenderer.cpp:106, 435-441`）。
- 建议：给 office 也烘一套，或接受现状（死斗节奏快，玩家很少看边缘）。

### G15（P2）双持贝瑞塔的真图明明画好了，游戏里还是色块
- 玩家视角（仓库视角）：仓库里有画好的真图，却因为文件名写错一个字而永远没被加载。
- 证据：`assets/cs2d/sprites/dual_berettas.png` 254×202、6802 色、46,843 字节，是画好的真图，但 `CS2D/Weapons/WeaponDef.hpp:164` 引用的是 `dualberettas.png`（无下划线）；`shootgun_d.png`（拼写错误的 shotgun，512×512，834 色）在 `PixelEngine/*` 和 `tools/*` 里零引用。两者都是孤儿文件。
- 建议：把 `dual_berettas.png` 改名接入 `WeaponDef`（或反向改引用），`shootgun_d.png` 改名或删除。

### G16（P2）office 的图块集声明了却一张图块都没用上
- 玩家视角：无直接感知，纯资源浪费。
- 证据：`office.tmj` 声明了 tileset `cs_office_tiles.png`（96×704，3 列 32px 图块，66 块），但没有任何 tilelayer；`tools/ASSET_SPEC.md:125-128` 还写着“图块集在 TMJ 中以相对路径引用”，与实际 `.tmj` 不符。渲染器会加载它但永远不画。
- 建议：要么给 office 加 tile 层用起来，要么删掉声明和文件并修正 ASSET_SPEC。

### G17（P2）主菜单中间那两个人怎么不见了，只剩个空台子
- 玩家视角：模型加载失败时菜单舞台直接是空的，没有任何兜底。
- 证据：`Graphics/Menu3D.cpp:397-403` T/CT 模型加载失败 → `tModel_.reset()`，渲染处（`Menu3D.cpp:1355-1379`）判空后直接跳过绘制；小鸡和武器同理。`Menu3DModel::Load` 本身很严谨（自己读文件解决 Windows 中文路径、校验 parse + buffers、`lastError_`），但失败只写 stderr，界面无提示、无 2D 备用图。模型不小：`ct.glb` 3,157,372 字节、`t.glb` 3,206,272 字节、`chicken.glb` 456,828 字节，场景各 1.0–1.35MB。
- 建议：加载失败时在舞台放一张 2D 角色立绘兜底，并在界面上给一行提示。

### G18（P2）两份小鸡模型 + 两份没人引用的角色源文件，占着 5MB 多
- 玩家视角：无直接感知。
- 证据：`assets/models/chickensoft/chicken.glb`（529,292 字节）全仓库零引用（只有 `tools/glb_inspect.py` 的默认参数提到它），实际加载的是 `assets/models/menu/chicken.glb`（456,828 字节）；`assets/models/quaternius/Character_Soldier.gltf`（2,343,513 字节）与 `Character_Enemy.gltf`（2,274,104 字节）游戏代码不引用（只加载了同目录的 `Guns/*.gltf` 作焦点武器），疑似 Quaternius 原始源文件。
- 建议：确认 chickensoft 那份是旧版就删；两个 Character 源文件注明用途或移出仓库。

### G19（P2）雾外区域的地形、包点框、C4 标记都糊在一层灰下面，包点只能靠死记硬背
- 玩家视角：雾是 160/255 的灰色罩子，包点、已安装的 C4 都先被雾盖住再画。
- 证据：`CS2DRenderer.hpp:275` `VISION_FOG_ALPHA = 160`；`CS2DGame::Render` 里 `DrawObjectiveMarkers`、已安装 C4 圆环都在 `DrawVisionFog` 之前画（`CS2DGame.cpp:1336–1364` 标记、`DrawVisionFog` 在其后），雾盖在上面。包点标记本身只有琥珀色矩形、无 A/B 文字（`CS2DRenderer.cpp:985–1020`，渲染层无字体，做不了文字是已知限制，但可用不同颜色 / 形状区分 A/B）。
- 建议：目标标记画在雾之上（或雾对其减淡），A/B 包点用颜色＋角标形状区分。

### G20（P2）720p / 800×600 下设置界面的字小得像蚂蚁
- 玩家视角（低分辨率玩家）：低分辨率下字会小到看不清。
- 证据：字号档固定 16/18/20/22/24/30/34（`Graphics/ImGuiUI.cpp:102`），`g_uiScale = min(w/1920, h/1080)`（`Engine.cpp:643`，钳制 [0.35, 4.0]），`io.FontGlobalScale = scale`（`ImGuiUI.cpp:275`）。800×600 时 scale=0.417，正文字 16px→约 6.7px；720p 时 16px→约 10.7px。800×600 还是 `Core/Resolutions.hpp` 里提供的可选分辨率。
- 建议：给字号设可读下限，或下架 800×600 / 1024×768 这类过低档位。

---

## 2. 音频（6 项：P0 × 1，P1 × 2，P2 × 3）

### G21（P0）队友在另一边交火，我这边一点声音都没有，像在打单机
- 玩家视角：超过 100 世界单位的声音直接静音，整张图另一头的枪战你什么都听不见。
- 证据：`Audio/AudioManager.cpp:423-461 PlayChunkAt`，`MAX_RANGE = 100.0f`，`MIN_FACTOR = 0.0f`——旁边的注释还写着“最远处仍保留 12% 音量”，代码却是 0。地图宽 880，100 单位只占 11%。所有 BOT 枪声、脚步、换弹都走这条 3D 路径（`CS2DGameCombat.cpp:1502`、`CS2DGame.cpp:1006`），超出就 `vol = 0`。
- 建议：把 `MAX_RANGE` 提到 400–600，`MIN_FACTOR` 按注释改成 0.12；或按枪声类别给不同衰减半径。

### G22（P1）下包“哔”一声就没了，赢了回合也是“哔”，跟微波炉一样
- 玩家视角：下包、拆包、回合胜负、脚步全是合成蜂鸣音。
- 证据：`tools/placeholders.json` 明确把这些列为生成占位：`bomb_plant.wav` / `bomb_defuse.wav` 均为 0.2s、22050Hz 单声道；`round_start.wav` 0.25s、`round_win/lose.wav` 0.4s、`bomb_beep.wav` 0.06s、`sfx_footstep.mp3` 0.1s——ffprobe 实测全是短促单音。C4 爆炸还复用手雷爆炸音（`CS2DGameObjective.cpp:322` 直接播 `GRENADE_EXPLODE`）。
- 建议：先换下包 / 拆包 / 胜负三件套的真实音效；C4 爆炸单独录一轨。

### G23（P1）一进对局音乐就停了，全程只有枪声和脚步，安静得发慌
- 玩家视角：对局里没有任何背景音乐。
- 证据：`Engine.cpp:1201` `StartMatchWithMap` 里 `StopMusic()`，回主菜单才续播；3 首 BGM（`music_menu.ogg` 67s、`music_menu_calm.ogg` 120s、`music_menu_cyber.ogg` 仅 12.8s）只在菜单响。其中 cyber 那首 12.8 秒一循环，在菜单里重复感很明显。
- 建议：要么接受“竞技对局无 BGM”的设计并把菜单 cyber 轨换成 60s+ 的版本，要么给对局加一轨低音量环境底噪。（若属有意设计可降为 P2）

### G24（P2）地上捡了把枪，一点反馈都没有，不确定到底捡没捡到
- 玩家视角：捡枪、捡拆弹器是静音的。
- 证据：`SoundEffect::PICKUP` / `OBSTACLE_BREAK` 只在 `Audio/AudioManager.hpp:23,27` 的枚举里存在，`ChunkFor`（`AudioManager.cpp:328-368`）没有它们的 case（走 `default: return nullptr`），且全仓库没有任何调用点。
- 建议：补一个拾取音并接上调用，或删掉这两个枚举值。

### G25（P2）我死了，屏幕一黑，游戏一声不吭
- 玩家视角：玩家阵亡没有任何专属音效。
- 证据：玩家被击杀只走 `OnCombatantKilled`（`CS2DGameCombat.cpp:791-806`，无播音调用），死亡前最后一声是中弹的 `HIT` / `ARMOR_HIT`；`KILL` 音只在玩家击杀 BOT 时播（`CS2DGameCombat.cpp:376,873`）。
- 建议：加一个短促的阵亡音（或复用 `ROUND_LOSE` 的变调），与击杀音区分开。

### G26（P2）换弹音只有 8 轨复用，6 把 SMG 全用同一轨
- 玩家视角：换弹音能听但不讲究。
- 证据：换弹只有 8 轨复用（6 把 SMG 全用 `reload_galil.mp3`，`ReloadSoundFor` 在 `CS2DGameCombat.cpp:970-1016`）。
- 建议：按武器类别至少把 SMG / 步枪 / 狙的换弹音区分开。

---

## 3. 操作与交互（16 项：P1 × 10，P2 × 6）

### G27（P1）开局想慢慢挑枪，菜单一开鼠标就脱离瞄准，BOT 照样冲过来，而你原地罚站
- 玩家视角：买枪菜单不暂停游戏，15 秒购买窗有 7 秒是在交战中站着挨打。
- 证据：冻结 2 秒＋购买期 6 秒（`CS2D/Rounds/RoundManager.hpp:21-22`），期间不可移动（`CS2DGame.cpp:947–948` `canMove = !frozen`）；但购买窗是 15 秒（`Tuning.hpp:65` `BuyWindowSeconds = 15.0f`），即 Live 开始后还能买 7 秒；`Engine.cpp:1484` 的 `cs2dGame->Update` 不因买枪菜单打开而停；15 秒一到菜单自动关闭（`Engine.cpp:863–864`），挑到一半直接消失。
- 建议：要么购买窗收紧到冻结＋购买期（8 秒），要么开菜单时给玩家一个明显的“你正在暴露”提示；至少不要让菜单在 Live 期无声消失。

### G28（P1）按 B 发现全是灰的、提示“不在购买区内”，但出生点附近根本看不出购买区的边界
- 玩家视角：购买区在哪全凭猜，地图上没有任何标记。
- 证据：`CS2DGame::IsPlayerInBuyZone`（`CS2DGame.cpp:212–230`）；地图没定义购买区时全图可买（`zones.empty()` 返回 true），有定义时无任何世界内标记 / 雷达标示；失败原因只在打开菜单后才在顶部提示行 / 悬停 tooltip 里看到（`Engine.cpp` `RenderBuyMenu` 的 `bctx.hint`）。
- 建议：在出生点画购买区地面框（像包点标记那样），或在 HUD 购买期显示“你在购买区内 / 外”。

### G29（P1）拿刀乱挥，身后的 BOT 也掉血，近战完全没有“朝向”概念
- 玩家视角：刀是 360° 的：不用瞄准方向，身后的人也能砍中。
- 证据：`CS2DGameCombat.cpp:155–163` 近战只判 `距离 < def.range`（45），没有任何角度检查；对比枪械 / 子弹全是角度＋散布。
- 建议：近战加约 ±60° 朝向扇形判定。

### G30（P1）想往远处扔雷封路，雷永远落在脚边不远；扔出去前不知道落点，只能凭感觉
- 玩家视角：手雷最远只能扔 230，扔不到中路对面，且没有任何落点预览。
- 证据：投掷速度 `clamp(2 × 准星距离, 180, 460)`（`CS2DGameCombat.cpp:468`、`Tuning.hpp:49`），摩擦 2 → 落点 90–230 世界单位，约 0.7 个屏幕宽；引信固定 1.4 秒，无 HUD 倒计时；只有 HE 飞行中画爆炸圈，烟 / 闪无任何范围预览。
- 建议：上限放宽到约 300–350（或按蓄力），加落点虚线 / 圆圈预览与引信提示。

### G31（P1）自己按住 E 读条到一半被打断，BOT 队友两秒就装好了
- 玩家视角：玩家下包要 3 秒，BOT 下包只要 2 秒，常量却写着 2 秒；规则对玩家更苛刻。
- 证据：`CS2DGameObjective.cpp:13` `PLANT_TIME = 2.0f`；玩家分支 `plantProgress_ >= 3.0f`（`:93`），BOT 携带者分支用 `defuseProgress_ >= PLANT_TIME`（`:126`）；`Engine.cpp:2296` 植物进度按 `/ 3.0f` 传给 HUD。BOT 下包还复用了 `defuseProgress_`，下包期间 HUD 不显示任何进度（“拆包中”只在已安装后显示）。
- 建议：统一成 2 秒（或统一成 3 秒并改常量），BOT 下包用独立进度变量并在 HUD 显示。

### G32（P1）刚露头，BOT 像卡住一样一动不动站 0.5 秒（低难度 2.5 秒），然后突然开火
- 玩家视角：BOT 的“反应延迟”实现成整只 BOT 原地罚站，看着像延迟不像反应。
- 证据：`CS2DGameBots.cpp:628、654` `if (!ai.Ready(id, clockSec_)) continue;` 直接跳过该 BOT 本帧的全部更新（含移动 / 走位）；反应时间 `BotReactionSecs = {2.5, 1.0, 0.5, 0.35}`（`Tuning.hpp:41`），默认难度 2 → 0.5 秒。被击中 / 听到声音后的警觉门也有同样的 `continue` 冻结。
- 建议：反应延迟只锁开火，不锁移动 / 转向 / 找掩体；低难度 2.5 秒的定身在玩家看来就是 BUG。

### G33（P1）绕后偷袭，BOT 一帧就转过来对着你，潜行绕后没有意义
- 玩家视角：BOT 被打中会瞬间“锁头式”转身，转向速度限制形同虚设。
- 证据：受击后 `bot.aimAngle = std::atan2(tdy, tdx)` 直接赋值（`CS2DGameCombat.cpp:400`），绕过了 `BotMaxTurnRate = 8.0 rad/s` 的逐帧转向限制（`CS2DGameBots.cpp:417、1418` 的 `maxTurn` 钳制只管正常转向）。
- 建议：受击转向也走 `maxTurn` 平滑，或至少给 0.15–0.25 秒转身过程。

### G34（P1）我是左手鼠标 / AZERTY / 笔记本键盘，WASD 不顺手却改不了
- 玩家视角：没有键位重绑定，所有操作键都是写死的；F1 静音、Tab 记分板等全靠猜。
- 证据：设置页只有 4 个页签（`Graphics/GameUI.cpp:501-507`：操作与显示 / 准星与瞄准线 / 雷达 / 音频），无键位页；移动键在 `Engine.cpp:909-914` 写死 `SDL_SCANCODE_W/A/S/D`、Shift、R、Tab，`Input/InputManager.cpp:95-110` 写死 F11 全屏 / F1 静音 / Esc 暂停 / B 买枪 / 1-4 切枪，观战空格在 `CS2DGame.cpp:877-878`，拆包 E 在 `Engine.cpp:1428`。全仓库无 `keybind / rebind / 键位` 相关设置项。
- 建议：设置里加“键位”页签，至少支持改移动 / 买枪 / 换弹 / 互动 / 记分板 / 暂停这几组，并做冲突检测；F1 改成帮助页，静音改到别处或只保留设置里的按钮。

### G35（P1）我在主菜单按 Esc 想回上一级，结果游戏直接关了
- 玩家视角：主菜单 Esc 直接退出游戏，没有任何确认；主菜单“退出游戏”按钮也是直接退出。
- 证据：`Engine.cpp:810-834`，Esc 处理的 `else` 分支（即不在对局 / 暂停 / 设置 / 结算 / 确认页 / 选图 / 历史时）就是 `isRunning = false`；`Graphics/GameUI.cpp:396` 主菜单“退出游戏”调 `quitToDesktop`，`Engine.cpp:229-233` 也是直接 `isRunning = false`。与之对比，暂停页退出要走两层确认（`GameUI.cpp:454-468`）。
- 建议：主菜单 Esc 应什么都不做（已是顶层），“退出游戏”走与暂停页同一套 `EXIT_CONFIRM` 确认页。

### G36（P1）我点的死斗（一人成军），为什么还要我选阵营？想反悔回主菜单，Esc 也没反应
- 玩家视角：死斗也要先选 T / CT，弹窗文案还写“团队协作”；选阵营期间 Esc 被吞、没有返回主菜单的路。
- 证据：`Engine.cpp:105-119` 死斗也走 `BeginMatchLoad`，`Engine.cpp:1532` 无条件 `sideChoicePending_ = true`；淡入结束后无条件弹选阵营弹窗；`Graphics/GameUI.cpp:1990-2020` 死斗下两张卡写的是“T 方 · 团队协作 / CT 方 · 团队协作”，与死斗实际玩法不符（死斗阵亡是原地死亡镜头、不观战，`CS2DGameCombat.cpp:817-819`）。Esc 在选阵营 / 转场期间被挡：`Engine.cpp:810-812`（`!IsTransitionActive() && !sidePopupOpen_ && !sideChoicePending_` 才处理 Esc）。弹窗本身也无“返回主菜单”按钮。
- 建议：死斗跳过选阵营（阵营只决定手枪 USP-S / Glock-18 的话，默认 CT 或随机即可）；爆破 / 歼灭的弹窗加“返回主菜单”按钮，Esc 在选阵营时应回到主菜单而不是无响应。

### G37（P2）想买拆包钳加速拆包：买枪菜单里没有；A 点 B 点在图上长一个样
- 玩家视角：没有拆包钳可买，拆包永远 5 秒；包点也没有 A/B 之分。
- 证据：`DEFUSE_TIME = 5.0f`（`CS2DGameObjective.cpp:16`），无任何 kit 加速分支；`Engine.cpp` `RenderBuyMenu` 只卖武器＋两档护甲。包点标记见 G19。
- 建议：买枪菜单加拆包钳（拆包 2.5 秒，照抄 CS2），A/B 包点颜色区分。

### G38（P2）按到 F6 镜头突然变成固定 100 单位前视，不知道按了什么、怎么按回去
- 玩家视角：F6 切换的 ANGLED 相机模式没有任何游戏内提示。
- 证据：`CS2DGame.cpp:922–923` 每帧轮询 `SDL_SCANCODE_F6` 切 `ToggleCameraMode()`；HUD、提示行、设置外无任何模式指示。
- 建议：HUD 角落显示当前相机模式（跟随 / 前视），或把 F6 从硬编码改成可配置按键并在操作提示里写出来。

### G39（P2）一露头三四把枪同时响起，死都不知道是被集火了
- 玩家视角：集火没有上限证据，几只 BOT 同时盯你一人时毫无反制提示。
- 证据：`CS2DGameBots.cpp:856–862` `ResolveFocus` 会让多个 BOT 改打同一目标；在审计的文件里没找到攻击令牌 / 同时开火人数上限（按代码，未发现即不存在）。结合 G44（无受击方向），被集火时体验最差。
- 建议：加同时集火同一目标的人数上限（或散布惩罚），并在被多方向命中时给方向提示。

### G40（P2）难度越低的 BOT，后坐力恢复越慢——数值上是这么写的，但语义上是把两个系统抄混了
- 玩家视角：无直接感知，数值语义错误。
- 证据：`CS2DGameBots.cpp:496` `bot.aimKick *= exp(-recover * Tuning::BotFovRadiusMul[difficulty] * dt)`——恢复速度乘的是 FOV 半径倍率（0.8–0.95），不是反应 / 熟练度参数。
- 建议：换成独立的恢复倍率或直接用 `perception.settle`。

### G41（P2）打完游戏切回聊天，发现输入法变英文了
- 玩家视角：进对局会悄悄把 Windows 键盘布局切成美式英文，崩溃时不恢复，且全程不告诉玩家。
- 证据：`Engine.cpp:StartMatchWithMap` 调 `inputManager->SwitchToEnglishKeyboardLayout()`，`Input/InputManager.cpp:102-131` 用 `LoadKeyboardLayoutW(L"00000409")` 接管；恢复只在析构 / `Clean()`（`InputManager.hpp:约 40`、`Engine.cpp:约 740`），崩溃 / 强杀不恢复；UI 里无任何相关提示。
- 建议：只在需要输入的场合切布局，或进对局前在加载页注明“已切换为英文键盘布局，退出后恢复”；崩溃恢复可通过启动时检测并还原。

### G42（P2）我就进设置看一眼，回来还要黑一下，点一下等一下
- 玩家视角：每次切页都有 0.9 秒黑场淡入淡出，连“设置→返回”也要等，感觉很拖。
- 证据：`Engine.cpp:1630-1638` `BeginTransition` 固定 `transitionDuration_ = 0.9f`，所有普通页面切换都走同一套黑场软切；加上进图三段约 1.2s、比赛结束 1.2s 沉淀（`Engine.cpp:1502`），一次“开始→开打”要过约 2.1s 动画。
- 建议：设置 / 历史 / 暂停这类轻页面改用 0.25–0.3s 淡入淡出或直接切换，0.9s 只保留主菜单↔对局这种重切换。

---

## 4. HUD 与信息呈现（10 项：P0 × 2，P1 × 5，P2 × 3）

### G43（P0）想攒钱买 AWP，屏幕上根本找不到余额
- 玩家视角：打着打着不知道自己有多少钱，经济规划全靠猜。
- 证据：`Engine.cpp:2253` 每帧都填了 `ictx.money = status.money`，但 `Graphics/GameUI.cpp` 的 `DrawInGame`（约 2260–2643 行）画了血条、护甲、弹药、击杀栏、目标面板，唯独没画 `ctx.money`；钱只出现在买枪菜单（`GameUI.cpp:1624`“余额: $%d”）和记分板（`GameUI.cpp:2527`）。
- 建议：右下武器面板或顶部计分条旁常驻显示 `$` 余额，买枪窗口期变色提示。

### G44（P0）正打着突然掉血，红边一闪，完全不知道子弹从哪来，转身都不知道往哪转
- 玩家视角：从背后被打死和从正面被打死，屏幕反馈一模一样。
- 证据：受伤反馈只有全屏均匀红边（`CS2DRenderer.cpp` `DrawDamageVignette`，`CS2DGameCombat.cpp:417` `damageFlashTimer_ = 0.30f`），无方向弧 / 指示器；游戏内无任何声音方向可视化。结合 120° 视野锥，背后遇袭时玩家没有任何指向性线索。
- 建议：受击方向弧（按子弹来源角度画红弧）＋短暂的击杀者方向箭头，P0 级体验缺口。

### G45（P1）杀了人只看到“你 击杀 XXX”，被谁杀、用什么枪杀、是不是爆头，杀完就忘
- 玩家视角：击杀栏只有一句话白字，谁用什么杀的、爆头与否全不知道。
- 证据：`CS2DGameObjective.cpp:18` 击杀栏是纯文本、寿命 4 秒、上限 6 条；`GameUI.cpp:2439–2456` 全部用同一白色（235,235,235）绘制，无武器图标、无颜色区分敌我 / 自己；子弹命中判定是圆形半径（`CS2DGameCombat.cpp:305–315`），全程无命中部位概念，`ApplyDamage` 不区分爆头。
- 建议：击杀栏至少加武器名、击杀者 / 被击杀者阵营色、自己相关行高亮；想做爆头先得在弹道里加命中部位。

### G46（P1）开枪命中只闪一下白色 X，打了多少血、打没打中护甲，全凭感觉
- 玩家视角：没有伤害数字 / 飘字，打中没打中只靠一个 0.18 秒的小白叉。
- 证据：命中标记 `hitMarkerTimer_ = 0.18f`（`CS2DGameCombat.cpp:340`），击杀与非击杀同一个白色 X（`CS2DOverlay.cpp` 命中反馈段、`CS2DRenderer.cpp:1540` `DrawHitMarker` 纯白）；全仓库无伤害飘字 / 伤害数字实现；击杀只在远处播一个位置音（`CS2DGameCombat.cpp:375–378` `KILL` 音按位置播放，远了可能听不见）。
- 建议：命中标记击杀变红 / 变大并延长，补伤害数字（可先只做自己造成的伤害），击杀给一个非位置性的确认音。

### G47（P1）弹匣打空按住扳机：没声、没字、枪也不动，新手会以为键坏了
- 玩家视角：换弹只有一行“换弹中...”，子弹打空且无备弹时按扳机毫无反应。
- 证据：换弹提示只有 `Engine.cpp:2240–2241` 的 `hintText = "换弹中..."`，画在固定 y=512 像素处（`GameUI.cpp:2579`，不随分辨率缩放居中）；无换弹进度条 / 圆环。空仓时：`CS2DGameCombat.cpp:171–180` 只有备弹 >0 才播 CLICK 声，`StartReload` 在 `reserve <= 0` 时直接 return（`CS2DGameCombat.cpp:943`），0/0 状态下扣扳机零反馈。
- 建议：换弹加进度条；备弹为 0 且弹匣为空时扣扳机显示“没有备弹”文字＋空仓声。

### G48（P1）想按 Tab 看大地图，只弹出记分板，大雷达怎么按都不出来
- 玩家视角：Tab 中央大雷达是死功能，按 Tab 永远打不开。
- 证据：`CS2D/CS2DOverlay::UpdateTabToggle`（`CS2DOverlay.cpp:154–155`，按 Tab 边沿切换 `centerRadarOn`）在 origin/main 全仓库没有任何调用点（只有定义与声明）；Tab 按住只出记分板（`Engine.cpp:917–919` `showScoreboard_ = keyTab`）。中央雷达绘制代码（`CS2DOverlay.cpp:260`）因此不可达。
- 建议：把中央雷达改绑到别的键（如 M）或在记分板里加开关，并删掉或接通 `UpdateTabToggle`。

### G49（P1）暂停想看一眼比分和第几回合，结果暂停页只有 5 个按钮
- 玩家视角：暂停页看不到任何对局信息，想看比分得先回游戏按 Tab。
- 证据：`GameUI.cpp:431-455` 暂停页只有标题“已暂停”+ 继续 / 重新开始 / 设置 / 退出游戏 / 退出桌面，没有比分、回合数、金钱、目标状态。比分只能在对局中按住 Tab 看（`Engine.cpp:916-919`）。
- 建议：暂停页顶部加一行“比分 T x : x CT · 第 n 回合 · 模式 / 地图”，成本很低。

### G50（P2）看雷达只知道队友在哪，不知道 A/B 点在哪、队友朝哪看、拿包的是哪个绿点
- 玩家视角：雷达只有圆点：不标包点、不标朝向、不标谁拿着 C4，死斗里干脆全空。
- 证据：雷达点位只有队友绿点＋6 秒内情报红点＋掉落 C4 橙点（`CS2DOverlay.cpp:224–245`，红点 6 秒过期）；`CS2DRenderer.cpp` `DrawRadarHUD` 的点位全是无朝向实心圆；死斗直接跳过全部点位（`CS2DOverlay.cpp:224` `if (!dmMode)`）。C4 携带者只在底部文字写名字（`GameUI.cpp` “C4 携带者：”），雷达上无法对应。
- 建议：雷达加包点字母、队友朝向小三角、C4 携带者特殊标记；死斗至少显示最近的敌人方向或保持全空但说明原因。

### G51（P2）听到枪声不知道从哪边来，只能凭音量猜
- 玩家视角：声音系统只有耳朵没有眼睛：狙击枪声全图可闻，但屏幕上没有任何声音方向提示。
- 证据：听觉半径极大（狙击 950，超地图 880，`Tuning.hpp` `HearGunshotSniper = 950.0f`），脚步 130、下包完成 1400 全图；但游戏内无任何声音方向 / 距离可视化（无声源指示圈、无“听到脚步”提示）。
- 建议：加短暂的声音方向指示（边缘弧光或雷达上的声源闪点），至少对近处脚步与下包 / 拆包声做提示。

### G52（P2）“回合胜利”都打出来了，对面 BOT 还在开枪，死一个还计一次阵亡
- 玩家视角：回合结算 4 秒里所有人还能继续开枪杀人，击杀还计分。
- 证据：`CS2D/Rounds/RoundManager.hpp:24` `endTime = 4.0f`；`CS2DGame.cpp:947–965` 结算期 `canMove` 仍为 true，`UpdatePlayerMovement` 与 `UpdatePlayerShooting` 照常跑，BOT 也是；击杀奖励 / 计数在 `UpdateBullets` 里不分阶段发放。
- 建议：结算期冻结开火（只允许移动参观），或结算期击杀不计 K/D 与金钱。

---

## 5. 流程与节奏（9 项：P0 × 1，P1 × 5，P2 × 3）

### G53（P0）屏幕一黑就变成队友视角了，死因全靠右上角一行 4 秒就消失的小字
- 玩家视角：没有死亡画面：死的一瞬间直接切观战，连谁杀的你都来不及看。
- 证据：玩家阵亡立即 `SpectatorBegin()`（`CS2DGameCombat.cpp` `OnCombatantKilled` 玩家分支、`CS2DGameSpectate.cpp:26–33`）；唯一死因信息是击杀栏 “X 击杀 你”（`CS2DGameCombat.cpp:427`），4 秒消失（`CS2DGameObjective.cpp:18`），无武器、无伤害量、无死亡回放 / 击杀镜头。
- 建议：死后 1.5–2 秒定格死亡画面（击杀者、武器、伤害、存活队友数），再切观战。

### G54（P1）每回合开打前都要罚站 8 秒，回合结束还要再等 4 秒，节奏很拖
- 玩家视角：每回合 8 秒冻结 + 4 秒结算都不能跳过，打满 15 回合要站 2 分钟。
- 证据：`CS2D/Rounds/RoundManager.hpp:18-22`：`freezeTime 2.0 / buyTime 6.0 / roundTime 90.0 / endTime 4.0`；`RoundManager.cpp:28-58` 阶段只按计时自动迁移，无跳过、提前开始、“准备就绪”入口。先到 8 分胜（`CS2DGame.cpp:140` `teams_.SetScoreToWin(8)`），打满约 15 回合即约 120 秒纯冻结 + 60 秒结算等待。实测佐证：`--fastround` 下每回合约 11 秒，其中 8 秒是冻结，真正交火只有约 3 秒。
- 建议：买完枪的玩家按空格 / 按钮可提前结束本回合冻结（全员就绪或单机直接进 Live）；回合结算 4s 允许按键跳过。注意：不要动 90s 回合时长本身。

### G55（P1）队友死完了，画面卡在自己尸体位置，底下写“观战：-”，不知道在等什么、还要等多久
- 玩家视角：队友全死光时，镜头停在尸体上，只显示“观战：-”。
- 证据：`SpectatorBegin` 找不到存活队友时 `specTarget_ = -1`（`CS2DGameSpectate.cpp:31–32`）；`GetSpectatedName()` 返回 `"-"`（`CS2DGame.hpp:783`）；观战 HUD 没有“等待下一回合”文案与回合剩余时间。
- 建议：无人可观战时显示“队友全部阵亡 · 等待下回合”＋回合倒计时，镜头给存活敌人以外的中立视角或直接黑场提示。

### G56（P1）打了 5 回合有事退出，历史记录里什么都没留下，像没打过一样
- 玩家视角：中途退出 / 重新开始的对局完全不进历史记录；“未完成”这个结果码是死代码。
- 证据：`Engine::RecordMatchHistory(bool abandoned)`（`Engine.cpp:2620` 起，`abandoned` 时记 `draw`）全仓库唯一调用点是比赛正常结束 `Engine.cpp:1506` 的 `RecordMatchHistory(false)`；暂停退出 / 重开、点窗口 X（`Engine.cpp:806` `SDL_QUIT → isRunning=false`）都不记。`Storage/MatchHistory.hpp:17-20` 的 `result` 字段和 `ResultLabel` 里的“未完成”因此永远用不上。
- 建议：退出 / 重开时也记一条（结果标“未完成”，比分记当时比分），或至少在退出确认页如实写“本次对局不会记入历史”。

### G57（P1）比赛结束画面写着“X 秒后返回主菜单”，我等了半天还在结算页
- 玩家视角：“X 秒后返回主菜单”是过期文案：实际永远不会自动回主菜单。
- 证据：`Engine.cpp:2261` 给 `matchOverReturn = 8.0f - GetMatchOverTimer()`，`GameUI.cpp:2397-2407` 据此显示“%d 秒后返回主菜单”；但 `Engine.cpp:1498-1515` 的实际流程是：比赛结束留 1.2s（`kSettleDelay`）后切到 `MATCH_END` 结算页并停在那里等玩家点“返回主菜单 / 再来一局”（`GameUI.cpp:2187-2200`），没有任何自动回主菜单的逻辑。
- 建议：删掉这行倒计时，或改成“正在结算…”；结算页本身停留等玩家操作是对的。

### G58（P1）打完想换张图，得先回主菜单重走一遍选图 + 加载 + 选阵营
- 玩家视角：结算页没有“换图 / 换模式”，“再来一局”还要再选一次阵营。
- 证据：`GameUI.cpp:2185-2200` 结算页只有“返回主菜单”和“再来一局”两个按钮；“再来一局”调 `actions.startMatch` 即 `RestartCurrentMatch`（`Engine.cpp:105`），而 `RestartCurrentMatch`（`Engine.cpp:1149-1175`）会重新置 `sideChoicePending_ = true` 并弹选阵营弹窗。
- 建议：结算页加“换地图”按钮；“再来一局”默认沿用上局阵营直接开打，阵营想换再去暂停页换。

### G59（P2）死斗里死了干等，不知道 1 秒还是 5 秒后复活
- 玩家视角：死亡后等待没有倒计时数字；死斗死后只写“阵亡 · 等待重生”，不告诉你几秒后复活。
- 证据：死斗重生延迟 `BOT_RESPAWN_DELAY = 3.0f`（`CS2DGame.hpp:1191`，`CS2DGameCombat.cpp:874`），但提示只有文字无倒计时（`GameUI.cpp:2582-2586`）；非死斗死亡进观战（`CS2DGameCombat.cpp:817-819` `SpectatorBegin()`），回合结束前无任何跳过 / 提前结束入口，观战操作只在死后才显示（`GameUI.cpp:2600-2620`）。
- 建议：死斗重生提示改成“阵亡 · 3 秒后重生”带倒计时；观战时加“回合剩余时间”显示（已有回合计时，复用即可）。

### G60（P2）点开始后黑屏，我不知道是在加载还是卡死了，想取消回主菜单也没办法
- 玩家视角：进图黑屏加载没有进度条 / 百分比，也不能取消；首次启动等 LLM 时只有一行字。
- 证据：进图三段 `Engine.hpp`：`matchLoadZoomDuration_ 0.55 / matchLoadMinHold_ 0.30 / matchLoadFadeDuration_ 0.35`，黑屏阶段 `Engine.cpp:2424-2435` 才调 `StartMatchWithMap`（含 `EnsureLlmInitialized()`），加载页只显示两行文案（`Engine.cpp:1881-1886`：“正在加载战术服务器... / 首次启动需要等待模型加载，请稍候”），无进度、无百分比、无取消；Esc 在加载 / 转场期间被挡（`Engine.cpp:810-812`）。
- 建议：加载页加进度条（地图解析 / BOT 生成 / 战术加载分步）+“取消”按钮；LLM 冷启动超时（如 30s）后给“用规则教练继续”选项。

### G61（P2）历史里有一场 0 杀的黑历史想删掉，删不了；打到 50 场后旧记录默默没了
- 玩家视角：历史记录只有列表，没有删除 / 清空 / 筛选 / 详情，50 场封顶后悄悄丢最旧的。
- 证据：`Storage/MatchHistory.hpp:15` `kMaxRecords = 50`，`MatchHistory.cpp:约 60` 超限时从头部删；`GameUI.cpp:1388-1600` 历史页只有统计条 + 表格 +“返回主菜单”，无删除 / 清空 / 筛选 / 点开详情。记录字段本身也只有时间 / 模式 / 地图 / 比分 / K/D / 回合 / 时长 / 结果（`MatchHistory.hpp:8-20`），无伤害、命中率等。
- 建议：加单条删除和清空（带确认）；50 场封顶时在页脚注明“只保留最近 50 场”。

---

## 6. 设置与引导（8 项：P1 × 3，P2 × 5）

### G62（P1）想给不懂中文的朋友玩，没地方切英文
- 玩家视角：没有语言选项，全界面只有中文。
- 证据：设置页 4 个页签无语言项；全仓库 `grep -i "language / 语言"` 在菜单 / 设置代码中无任何语言切换实现，所有文案均为硬编码中文字符串。
- 建议：加语言下拉（至少中 / 英），文案抽成表；短期先在设置页注明“仅中文”。

### G63（P1）我调完音量直接任务管理器杀进程 / 游戏闪退，再进发现设置全回去了
- 玩家视角：设置页脚写“所有改动即时生效并自动保存”，实际是退出时才落盘。
- 证据：`GameUI.cpp:约 866` 页脚文案“所有改动即时生效并自动保存”；但 `SaveManager::Save()` 只在 `Engine::Clean()`（`Engine.cpp:734`）和主菜单 3D 自定义 / 场景切换时立即调用（`Engine.cpp:152,159`），音频 `SaveAudioConfig()` 只在设置页 Esc、切 BGM / 点击音和析构时调用（`Engine.cpp:51,246,252,823`）。正常退出没问题，崩溃 / 强杀会丢本次全部设置。
- 建议：要么改文案为“退出时保存”，要么每次改动后防抖 1-2 秒写盘（设置量很小，成本可忽略）。

### G64（P1）全屏下我从 1080p 切到 720p 想提帧数，画面一点没变
- 玩家视角：分辨率下拉在全屏下是骗人的：它只改窗口大小，不改渲染分辨率。
- 证据：`Core/Resolutions.hpp:1-10` 注释自称“方案 B：分辨率 = 内部渲染分辨率，与窗口 / 桌面尺寸解耦”；但 `Engine.cpp:362-385` 的 `setResolution` 在全屏时只调 `ResizeLogicalViewport(输出尺寸)`，内部画布由“窗口实际尺寸 + 渲染缩放”推导（注释原话），分辨率选择在全屏下对渲染分辨率无实际作用。真正影响清晰度的是“渲染缩放 100/75/50%”（`GameUI.cpp:629-637`）。
- 建议：全屏时把分辨率下拉改成“全屏分辨率（改桌面模式）”或直接禁用并注明“全屏下请用渲染缩放调清晰度”；窗口模式下它改窗口大小是对的，保留。

### G65（P2）我把准星颜色、雷达大小调花了，想一键还原，找不到按钮
- 玩家视角：设置没有“恢复默认”，调乱了准星 / 雷达只能一个个滑回去。
- 证据：设置 4 个页签（`GameUI.cpp:501-507`）无任何“恢复默认 / Reset”按钮；`SaveManager` 只有读 / 写，没有 `ResetToDefaults`。
- 建议：每页签右上角加“恢复本页默认”，或页脚加“恢复全部默认”（带一次确认）。

### G66（P2）我 144Hz 显示器只能选 120，高刷屏白买了；想不限帧跑满也没得选
- 玩家视角：帧率只有 30 / 60 / 120 三档，没有不限帧 / 144 / 165 / 240，也没有垂直同步开关。
- 证据：`GameUI.cpp:约 585`（设置页“帧率上限”）：`fpsLabels[] = { "30", "60", "120" }`，无其他档位；全仓库设置代码无 VSync 开关。
- 建议：加 144 / 165 / 240 / 不限帧四档，另加垂直同步开关（默认开）。

### G67（P2）我以为是个小窗口游戏，结果一打开占了大半个屏
- 玩家视角：首次启动窗口是 1280×720，而不是 main 传的 800×600；小屏 / 低分用户第一印象就是窗口过大。
- 证据：`main.cpp` 调 `engine.Initialize(..., 800, 600, ...)`，但 `Engine::Initialize` 的 `width / height` 参数在函数体内一次都没用过（`Engine.cpp:400` 签名后无引用）；实际窗口尺寸取自存档 `resIdx`，默认 0 即 `kResolutions[0] = 1280×720`（`Core/Resolutions.hpp:14-17`、`Engine.cpp:424-426`、`Engine.cpp:626`）。
- 建议：删掉无用的 `width/height` 参数，或首次启动按桌面可用区取一个保守默认（如 1280×720 但不超过可用区的 80%）。

### G68（P2）我备份存档只拷了 cs2d_save.ini，结果音量和 BGM 选择全没了
- 玩家视角：设置被拆成两个文件（cs2d_save.ini + audio_config.ini），删 / 拷一个就丢一半。
- 证据：`Storage/SaveManager.cpp:9-11` 存档是 `cs2d_save[_dev/_beta].ini`，音频单独 `audio_config.ini`（`Engine.cpp:2443,2476`），两套读写路径、两个落盘时机。
- 建议：合并到一个存档文件（保留对旧 `audio_config.ini` 的一次性迁移读取）。

### G69（P2）想反馈问题，别人问我什么版本，我在游戏里找不到
- 玩家视角：菜单不显示版本号，报 bug 时玩家说不清自己是哪个版本。
- 证据：版本只打在启动控制台 `main.cpp` 的 `[Build] ...` 日志里（`BuildInfo::Display()`），主菜单 / 设置页无任何版本号、commit、渠道标识。
- 建议：主菜单右下角加一行小字版本号（如 `0.3.0-dev (98f114f)`）。

---

## 7. 新手体验（7 项：P0 × 2，P1 × 1，P2 × 4）

### G70（P0）进游戏只看到“团队歼灭 / 爆破 / 死斗”三个按钮，点进去就要打了，但 WASD、买枪、换弹、拆包这些键在哪都没人告诉我
- 玩家视角：没有任何新手引导 / 操作说明，第一局玩家不知道自己能按什么键。
- 证据：全仓库 `grep` “帮助 / 新手 / 引导 / 首次 / 操作说明 / 怎么玩”在 `Engine.cpp / GameUI.cpp / Menu3D.cpp` 均无命中；没有帮助页、没有 `GameState::HELP`（`Core/Types.hpp` 状态机只有 `MAIN_MENU / MAP_SELECT / HISTORY / SETTINGS / PLAYING / PAUSED / RESTART_CONFIRM / MATCH_END / EXIT_CONFIRM`）。唯一的按键提示是散在局内的：观战 `GameUI.cpp:2600-2620`（“[空格] 自由视角 · [WASD] 移动镜头”）、持 C4 `GameUI.cpp:2611-2627`、选图 `GameUI.cpp:1365`（“← → 切换地图 Enter 开始 Esc 返回”）。
- 建议：主菜单加一个“操作指南”按钮（或首次进入强制弹一页）：WASD 移动 / 鼠标瞄准 / B 买枪 / R 换弹 / E 安装拆包拾取 / G 丢枪 / 1-4 切枪 / Tab 记分板 / Esc 暂停 / F11 全屏 / F1 静音；买枪阶段首次出现时再在局内弹一次“B 打开购买菜单”。

### G71（P0）开局 $800、冻结不能动，我以为等开打就行，结果 8 秒过去购买期直接没了，只能拿手枪打
- 玩家视角：买枪菜单不会自动打开，新手大概率空手打完第一个 8 秒购买期。
- 证据：`CS2D/Rounds/RoundManager.hpp:18-22` 冻结 2.0s + 购买 6.0s，每回合都冻结 8s 且无任何跳过 / 提前开始的入口；`Engine.cpp:861-863` 买枪菜单只靠 `B` 键翻转（`buyMenuOpen_ = !buyMenuOpen_`），`StartMatchWithMap` 不自动打开，离开购买期还自动关闭（`Engine.cpp:863-864`）。
- 建议：每回合 Freeze 开始时自动弹出买枪菜单（至少第一回合），或在屏幕中央给“B 打开购买菜单 · 剩余 Xs”的倒计时提示；买完自动关闭即可。

### G72（P1）简报说有“人质”模式，进游戏只有死斗、团队歼灭、爆破三种可选
- 玩家视角：任务简报里的人质模式根本不存在。
- 证据：`CS2DGame.hpp:34–38` `MatchMode` 只有 `DeathMatch / TeamElimination / BombDefusal`；全仓库目标玩法代码只有爆破（`CS2DGameObjective.cpp`、`CS2DGameBomb.cpp`）。
- 建议：要么从玩法清单里去掉人质，要么立项补人质模式（含人质跟随 / 解救提示）。

### G73（P2）第一回合：屏幕一闪就开打了，买枪菜单长什么样都没看清
- 玩家视角：冻结只有 2 秒，第一局的新手根本来不及读菜单。
- 证据：`RoundManager.hpp:21` `freezeTime = 2.0f`，购买期 6 秒也动不了；两段加起来 8 秒是全部“安全购物时间”。
- 建议：首回合冻结放宽到 4–5 秒，或第一局给一次性的购买提示。（与 G54 的“每回合冻结太长”不矛盾：首回合太短不够读菜单，每回合都罚站 8 秒又太长——解法是首回合放宽 + 后续回合允许提前开始。）

### G74（P2）第一次玩，想先空图练练枪法和走位，只能直接进 5v5
- 玩家视角：没有热身 / 练习模式，第一回合就是冷启动实战。
- 证据：`Core/Types.hpp` 的 `RoundPhase` 有 `Warmup` 枚举，但 `RoundManager::Reset` 直接进 `Freeze`（`RoundManager.cpp:3-10`），`EnterPhase(Warmup)` 的计时是 0，全仓库无任何进入热身的调用。
- 建议：要么删掉无用的 `Warmup` 枚举，要么给新手一个“热身 60 秒（无 BOT / 可自由走图买枪）”开关。

### G75（P2）我玩了好几局才偶然点到人物发现能换颜色姿势武器，主菜单上完全看不出来
- 玩家视角：主菜单 3D 人物 / 小鸡可以点开自定义，但没有任何提示告诉玩家可以点。
- 证据：点击 3D 物体进自定义轮盘靠 `Engine.cpp:2154-2220` 的射线拾取；主菜单本身无任何“点击人物自定义”提示，只有进入轮盘后才有一行小字“点击右侧轮盘选项 · ESC 返回”（`GameUI.cpp:1115`）。场景切换按钮（右下角“场景 · 沙漠Ⅱ”）同样只在 3D 背景就绪时出现（`GameUI.cpp:397-400`），无说明。
- 建议：主菜单底部加一行小字提示“点击人物 / 小鸡可自定义外观”，或人物悬停时出高亮描边 + 手型光标。

### G76（P2）我想在 mirage 打团队歼灭，却发现歼灭只能打古代遗迹；死斗只能打办公室
- 玩家视角：模式 / 地图选择不对称：只有爆破能选图，歼灭和死斗的地图是写死的。
- 证据：`Engine.cpp:105-119`：主菜单“团队歼灭”直接 `BeginMatchLoad(ancient_ruins)`、“死斗”直接 `BeginMatchLoad(office)`，只有“爆破模式”进地图选择；`MapCatalog.hpp:31-53` 只有 3 张图（古代遗迹 / 沙漠Ⅱ / 荒漠迷城），办公室不在目录里，只能通过死斗进入。
- 建议：三个模式都先过同一张地图选择页（死斗默认办公室但可改）；或在模式按钮下标注默认地图名。

---

## 8. 性能与稳定性（5 项：P0 × 1，P1 × 2，P2 × 2）

### G77（P0）双击没反应 / 黑一下就退了，我根本不知道是 SDL、窗口还是显卡的问题
- 玩家视角：启动失败对玩家是静默的：只写控制台，窗口一闪就没了，退出码还是 0。
- 证据：`Engine.cpp:410-412`（SDL 初始化失败）、`:428-432`（窗口创建失败）、`:441-450`（渲染器创建失败）都只 `std::cerr << ... << SDL_GetError()` 后 `return false`；`main.cpp` 里 `Initialize` 失败后直接 `engine.Clean(); return 0;`，无弹窗、无日志文件、无非零退出码。`ImGuiUI::Initialize` 失败（`Engine.cpp:466`）连错误信息都没有。Linux 实测佐证：dummy 视频驱动下启动即退，退出码 0，日志只有 `渲染器创建失败: Couldn't find matching render driver`。
- 建议：初始化任一步失败弹 `SDL_ShowSimpleMessageBox` 说明原因并写一份 `crash/startup.log`；`main` 返回非零退出码。

### G78（P1）Linux 玩家：原样代码一个目标都编不出来
- 玩家视角（Linux 玩家）：仓库在 Linux 上完全无法编译运行。
- 证据（实测）：`Graphics/Menu3D.cpp:9` 与 `Graphics/Menu3DModel.cpp:11` 均为 `#ifdef _WIN32 … #else #include <OpenGL/gl.h>`——非 Windows 分支默认当作 macOS，Linux 上不存在该路径（实际在 `/usr/include/GL/gl.h`）；`CMakeLists.txt` 只在 `WIN32` 链 `opengl32`、`APPLE` 链 OpenGL framework，Linux 分支无任何 OpenGL 链接，但 `Menu3D.cpp` 直接调用约 30 个固定管线 GL 入口（`glBegin/glFrustum/glShadeModel/glReadPixels` 等）。在 `/tmp` 副本上做最小探测修改（两处 `#else` 改为 `#elif defined(__APPLE__)` + `#else #include <GL/gl.h>`，CMake 增加 Linux 的 `find_package(OpenGL)`）后，主游戏与 gameplay 测试均可编过。其余 Windows API 均已正确用 `#ifdef _WIN32` 隔离（`main.cpp`、`InputManager.cpp`、`PathUtils.hpp` 的 `windows.h` / `_wfopen` 均有 guard；LLM 的 WinHTTP / `CreateProcessW` / `llama-server.exe` 启动只在 Windows 编译，Linux 用 `LLMBotController_stub.cpp`）。
- 建议：修这两处（头文件分支 + CMake 的 Linux OpenGL 链接），成本很小；Windows 不受影响。

### G79（P1）约 14.6MB 的死文件躺在仓库里（占 assets 53MB 的 27%）
- 玩家视角：无直接感知；但仓库 82MB 里近 1/5 是垃圾，克隆慢、还容易误用错文件。
- 证据（`origin/main` 实测）：`dust_1.png` 3,695,431 + `dust_1_prev.png` 822,307 + `mirage_prev.png` 5,062,777 + `cs_office_bg_prev.png` 167,637 + `dust_2 - 副本.png` 4,141,443 + `dust_2_prev.tmj` 139,549 + `cs_office_tiles.png` 16,293（见 G16）+ `chickensoft/chicken.glb` 529,292（见 G18）≈ 14.6MB，代码零引用其中任何一个。`dust_1` 相关：`dust_1.tmj`（584 字节的残桩，指向 `dust_2.png` 却声明 1280×1280，与实际 880×880 不符；世界 160×160×8=1280）与 `dust_1.png` 仍在 `origin/main` 里，`MapCatalog.hpp:28-54` 只有 ancient_ruins / dust_2 / mirage 三条——本地工作区的删除改动尚未提交，`origin/main` 里 dust_1 还活着。`dust_2_prev.tmj` 声明的 `dust_2.png` 是 1600×1600，万一哪天被误加载会直接错位。
- 建议：确认 dust_1 删除提交上去；其余 `_prev` / 副本文件删掉或移入单独的美术归档分支；`dust_2 - 副本.png` 若要留，先改名。

### G80（P2）确定性回放工具在 Linux 上不可用
- 玩家视角：无直接感知（开发工具）；但回放哈希验证这条技术线在 Linux 上断了。
- 证据（实测）：`dev_replay_runner` 链接失败：`undefined reference to 'LLMBotController::WaitIdle(int)'`——调用点 `tools/dev_replay/dev_replay_runner.cpp:359`，声明 `PixelEngine/LLM/LLMBotController.hpp:82`，唯一实现在 Windows 版 `LLM/LLMBotController.cpp:466`，Linux/macOS 桩 `LLMBotController_stub.cpp` 未实现该函数。`EngineApp` 与 `gameplay_tests` 不调用它，故不受影响。
- 建议：在 stub 里补一个空实现（返回 true / 立即返回）。

### G81（P2）无头 CI / 服务器上跑不起来主游戏
- 玩家视角（CI / 服务器）：dummy 视频驱动下无法启动。
- 证据（实测）：`SDL_VIDEODRIVER=dummy` 下 `Engine.cpp:425` 只请求 `SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED)`，非 Windows 路径无软件渲染器回退（Windows 分支才有 `SDL_HINT_RENDER_DRIVER=""` 重试）；dummy 驱动没有加速渲染器，启动即退。headless 测试不受影响。
- 建议：渲染器创建失败时回退一次软件渲染器（`SDL_RENDERER_SOFTWARE`），或至少在日志里说明原因。

---

## 实测验证记录（Linux，2026-11-13）

审计对象：`origin/main = 98f114f`（`git archive` 解到 `/tmp`，本地克隆未动；仓库只读规则已遵守）。环境：Ubuntu 24.04，g++ 13.3.0，cmake 3.28.3，4 核 / 7.7 GiB；初始无 SDL2 开发库，补装后由 FetchContent 从 libsdl.org / GitHub 下载 SDL2 2.30.3、SDL_mixer 2.6.3、SDL_image 2.8.2。

| 检查 | 结果 |
|---|---|
| CMake configure | 成功（153.9s） |
| `gameplay_tests`（原样） | 编译失败（G78 的两个原因） |
| `gameplay_tests`（/tmp 副本探测补丁后） | 成功，**1019 通过，0 失败**，退出码 0（实测两次，约 100–116 秒） |
| `EngineApp` 主游戏（补丁后） | 成功（5.4 MB；post-build 自动复制 `assets/` 到 exe 旁） |
| `ui_buy_id_test` / `ui_mouse_coordinates_test` | 成功，退出码均为 0 |
| `map_perimeter_review` | Xvfb 下退出码 0，三图全部 `MAP PERIMETER PASS`（但它不校验比例，见 G5） |
| `menu_art_review` | 成功，退出码 0，`MENU ART CAPTURES 1788` |
| `dev_replay_runner` | 链接失败（G80） |
| 主游戏 dummy 视频驱动 | 启动即退（G81） |
| 主游戏 Xvfb + Mesa llvmpipe | 可运行到主菜单，45 秒无崩溃；截图证实菜单布局、按钮、鼠标指针均渲染出来，但**全部中文显示为 `????`**（G9） |
| `--autostart --mode=1 --fastround`（团队歼灭） | 完整打完一局：地图 `ancient_ruins` 加载成功（488×456、288 个碰撞矩形、29 个战术区域、2 个包点、16 个出生点、7 个购买区）；`[CS2DRenderer] 角色精灵: CT=OK T=OK T_alt=OK \| 武器贴图 41/42 \| Bullet=OK`；8 回合全部打完，`[CS2D] 比赛结束! CT 获胜 (0:8)` → 历史已记录 → 进入结算页；全程日志 213 行，**无任何 error / fail / 崩溃**；88 秒后由 timeout 终止（结算页停留中）。历史文件内容：`2026-11-13 12:29\|elim\|ancient_ruins\|0\|8\|win\|0\|0\|8\|88`——无输入的玩家 0 杀 0 死；fastround 回合约 11 秒，日志中零击杀，应为回合时限到而非歼灭结束 |
| `--autostart --mode=1`（正常回合时长，40 秒） | 截图证实地图、雾效视野锥、玩家角色与瞄准线、左上雷达小地图、右上记分板（`T 0 : 0 CT`、`4 vs 5`、计时 `01:13`）均渲染正常；40 秒内日志出现 5 条 `[CS2D] BOT 阵亡 (T)`，BOT 会交火死亡 |
| 存档可写 | exe 旁生成 `cs2d_save.ini`、`cs2d_history.txt`、`audio_config.ini`、`imgui.ini`（第二次启动显示 `[Save] 已读取存档`） |
| “武器贴图 41/42” | 不是缺素材：第 42 项是 C4，`CS2D/Weapons/WeaponDef.hpp:290` 明确写死 `spritePath=nullptr`（设计如此）；其余 41 个声明贴图文件经逐一核对全部存在 |
| LLM 教练（Linux） | 走桩实现，`Initialize()` 返回 false，教练回退规则引擎（日志中大量 `[Coach] ... 默认分兵`）；不自动拉起 llama-server（设计如此，非缺陷） |
| 主菜单 3D 背景（Linux） | 会被跳过：`[Menu3D] 主渲染器为 OpenGL，跳过独立 3D 上下文`（`Engine.cpp:478-484` 的既定逻辑），回退静态背景 |

## 未能验证清单（诚实说明）

- **真实玩家输入流程**：菜单点击、按 `B` 买枪、开火、玩家死亡、拆包 / 下包——本机无输入注入手段（无 xdotool、无障碍层），只验证了无输入挂机（autostart）+ `gameplay_tests` 对买枪 / 开镜 / 弹道的单元级覆盖（1019 项通过）。不要把“买枪 / 开火 / 死亡”写成已验证。
- **`dev_replay_runner`**：链接失败，未运行；逐 tick 哈希、确定性复现均未验证。
- **性能**：全部渲染走 Mesa llvmpipe 软件 GL，未测 FPS；无帧率结论可写。
- **音频**：全程 `SDL_AUDIODRIVER=dummy`，只确认无报错、无阻塞，未做可听验证。
- **LLM 教练**：Linux 无 llama-server，LLM 链路未验证，只有规则教练。
- **真实 GPU / 真实显示器**：未验证；Menu3D 的 3D 菜单背景在 OpenGL 主渲染器下按设计跳过，未验证其在 Linux 上的实际表现。
- **mirage 外围接缝、地图实际观感、字体在各分辨率下的真实可读性**：以文件 + 代码为准，未经进游戏验证（G5 的几何错误是确定的，接缝视觉效果需截图确认）。

## 附录：开发流程备注（不计入 81 项）

- **98f114f“击杀栏信息调整”标题夸大**：实际用户可见变化只有一个标点——击杀栏 “你 ▸ 击杀 X” 改成 “你 击杀 X”（`CS2DGameCombat.cpp:357、366、427`，三处去掉了 `▸`）。其余是删死代码：未使用的 `DrawBullet`、开镜圆形遮罩 `SetScopeMask` / `ComputeScopeVisibility`、镜外雾 alpha 242、`GetAimWorldX/Y`、`mapSourcePath_`。结论：没有删掉任何有用的击杀栏信息——击杀栏本来就只有一句话（见 G45），这次改动没让它更薄。但两点值得注意：① 删 `SetScopeMask` 调用是安全的，只因为 13d1666 已把它变成每帧无条件关遮罩的空操作，两处提交有隐性依赖；② 一个标题叫“击杀栏信息调整”的提交混进了与击杀栏无关的清理（`mapSourcePath_`、空 err 文件），且用户可见收益只有一个标点。建议：以后这类提交请把死代码清理与行为改动分开提。
- **关于 `_v.err` / `_z.err` 的纠正**：`git show --name-status 98f114f` 显示两者都是 `D`（本次提交**删除**的），`origin/main` 的树里已不存在；磁盘上工作区根目录还剩两个 0 字节的同名文件（2026-11-02），属于未入库残留，无需在仓库里处理。
