# PIX-39 主菜单盐市交付记录

建模与测试于 2026-11-23 完成，2026-11-24 汇总验收。按 Sun 确认的原创沙漠小镇方向，替换主菜单的 `dust_2` 场景。上一版白模及其生成缓存已清理。本版从空场景起步，通过 Blender MCP 在实时会话中搭建、检查和导出。美术目检待 Sun 确认。

## 场景与运行时

主相机右侧为盐市交易所，青绿双扇门、黄铜拉环、铆钉、菱形陶片和门额集中在这一区域。左侧染坊的条纹雨篷、石阶与货箱构成第二层街面，屋顶蓄水塔位于后街。地面保留石板板缝，建筑、车辆与电线的投影烘入铺地，阳台和门框的投影烘入立面图集。

| 交付项 | 实测 |
|---|---|
| GLB | 3,516,432 字节，67,512 个导出顶点，19 个网格，21 种材质 |
| 贴图 | 9 张内嵌 JPEG。铺地 1024²，立面图集 768²，其余 7 张 384² |
| 风动 | 1 个 `Menu_Environment` NLA 剪辑，16 组棕榈叶、2 面染布，共 18 个旋转通道。0–120 帧，30 fps |
| 可编辑源 | 934 个场景对象。静态网格在导出副本上合并，源场景保留独立组件 |
| 可复现性 | 通过仓库生成入口在独立 Blender 进程中重建，GLB 字节一致。SHA-256 为 `643620db49a2b3db1f8de4fe9591b7a6512bc9d340d12bbcab0afa0b9ef17e82` |

加载器读取材质指定的 UV 集与内嵌底色图，按 glTF 采样参数上传 OpenGL 纹理。模型颜色与场景渐变透明度参与纹理调制。带纹理场景复用静态节点的变换结果，风动物件继续逐帧更新。场景切换与启动预览继续走原有入口。

## 活动区与菜单

盐市人物横坐标由 ±4.40 调至 ±4.92 世界单位，地面阴影、避让、点选和聚焦过渡使用同一位置。盐市主菜单标题、副标题和按钮统一以视口中线居中。

保护区校验对盐市单独使用以下游戏坐标。每帧按实际道具包围盒检查，共覆盖 121 帧。

| 区域 | X | Z | 高度 |
|---|---|---|---|
| 鸡群漫游 | −5.95 至 5.95 | −2.65 至 6.10 | 0 至 2.0 |
| 左侧人物 | −6.70 至 −3.15 | −1.75 至 1.75 | 0 至 6.5 |
| 右侧人物 | 3.15 至 6.70 | −1.75 至 1.75 | 0 至 6.5 |
| 聚焦舞台 | −5.10 至 0.30 | −2.70 至 2.70 | 0 至 6.5 |

## 统一验收

| 检查 | 结果 |
|---|---|
| Release 主程序、玩法测试、菜单美术检查工具 | CMake 构建通过。计划所列的两个 D 盘 PowerShell 包装脚本在本机缺失 |
| 默认玩法回归 | 1,843 通过，0 失败，与改动前一致 |
| 桌面体验回归 | 2,157 通过，0 失败 |
| 场景保护区 | 盐市检查 845 个道具对象。三张场景均为 0 findings |
| 菜单运行时 | 三场景切换、装扮与漫游状态保持、18 通道风动及 30/60/120 Hz 动画时钟检查通过 |
| 画面 | 生产 Engine 使用 D3D11 合成菜单 UI 与 OpenGL 实时背景。保存了主菜单、角色聚焦和俯视转场的 1920×1080 帧 |

同一台机器、同一版菜单程序和 1280×720 渲染设置，交替加载旧版和新版场景，各测三次。表中取三次结果的中位数。每轮取预热 20 帧后的 60 帧，测量菜单背景渲染调用耗时。

| 指标 | 旧版 | 盐市 |
|---|---:|---:|
| 菜单初始化 | 223.84 ms | 278.10 ms |
| 盐市场景平均帧耗时 | 3.138 ms | 3.078 ms |
| 盐市场景 P95 帧耗时 | 3.360 ms | 3.279 ms |

本机测得的平均与 P95 渲染耗时均略降。菜单初始化增加 54.25 ms（约 24%），首开观感留待目检确认。[六轮原始记录](../build-evidence/pix39/salt-exchange/final-performance.csv)

[主菜单实拍](../build-evidence/pix39/salt-exchange/game/main.png) · [角色聚焦](../build-evidence/pix39/salt-exchange/game/focus.png) · [俯视转场](../build-evidence/pix39/salt-exchange/game/overhead.png) · [白模构图](../build-evidence/pix39/salt-exchange/whitebox.png) · [Blender 场景预览](../build-evidence/pix39/salt-exchange/blender-main.png)

[编辑源文件](../tools/menu-scenes/dust_2.blend) · [运行时 GLB](../assets/models/menu/scenes/dust_2.glb) · [生成脚本](../tools/menu-scenes/create_scenes.py) · [素材来源](../assets/models/CREDITS.md)

在仓库根目录可重复执行以下检查。

```powershell
& D:/Blender/blender.exe --background --factory-startup --python-exit-code 1 --python tools/menu-scenes/create_scenes.py -- --output-dir build-evidence/pix39/reproduced
& D:/Blender/blender.exe --background --python-exit-code 1 --python tools/menu-scenes/validate_scenes.py
cmake --build build-release --target EngineApp
cmake --build build-tests --target gameplay_tests menu_art_review
.\build-tests\gameplay_tests.exe
.\build-tests\gameplay_tests.exe --experience
.\build-tests\gameplay_tests.exe --pix39-menu
.\build-tests\menu_art_review.exe assets/models/menu build-evidence/pix39/menu-review --scenes
```

