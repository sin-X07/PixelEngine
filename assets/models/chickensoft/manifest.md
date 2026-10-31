# chicken.glb 资源规范（PIX-12 交付）

主菜单三只装饰小鸡的模型 / 动画 / 坐标约定与实测校准值。
调用方：`Graphics/Menu3D.cpp`（LoadModels / UpdateChickens / DrawModels）、`Graphics/Menu3DModel.*`。

## 1. 文件与来源

| 项 | 值 |
| --- | --- |
| 相对路径 | `assets/models/chickensoft/chicken.glb`（全小写，Windows/macOS 一致） |
| 格式 | 二进制 glTF 2.0（GLB，1 个 JSON chunk + 1 个 BIN chunk） |
| 大小 / 校验 | 529,292 字节；`git hash-object` 见仓库提交记录 |
| 生成器 | Khronos glTF Blender I/O v4.3.47（上游原始资产，本项目未改网格/骨骼/动画） |
| 作者 / 来源 | Chickensoft / Thibaud Goiffon，<https://opengameart.org/content/chickensoft-chicken> |
| 授权 | 模型 CC BY 4.0，动画 CC0 1.0（署名见 `assets/models/CREDITS.md`） |
| 修改 | 运行时为缺少材质的 GLB 指定默认暖白/羽色（`Menu3DModel::SetSolidColor`），未改资产文件 |

## 2. 几何与坐标（实测）

- 网格：1 个 `ChickenMesh`，**2293 顶点 / 3916 三角形**，1 个 skin（28 个关节），30 个节点。
- 材质：GLB 内 **无 material、无 texture、无 image**。因此贴图/PBR 不是交付要求，
  颜色由 `Menu3DModel::SetSolidColor`（羽色）与 `SetClothingTint` 指定。
- 轴向：**+Y 向上**，**+Z 为正面**（喙 z≈+0.46，尾 z≈-0.52），+X 为左侧。
- 原点：**脚底**，`minY = -0.0034`（≈0），`maxY = 1.8401`。
- 单位：**1 单位 = 1 米**（Blender 默认，未在节点上放大）。实测 AABB：
  `x ∈ [-0.9533, 0.9533]`、`y ∈ [-0.0034, 1.8401]`、`z ∈ [-0.7623, 0.6397]`
  → 高 1.843、宽 1.907、深 1.402。
- 层级：`Rig` → (`ChickenMesh`, `DEF-spine` 骨骼链)；`Draw` 用蒙皮后的最低点贴地
  （`minY_`），因此 `yOffset=0` 时脚底正好落在地面 y=0。

## 3. 动画列表（名称区分大小写）

| 名称 | 时长 | 用途 |
| --- | --- | --- |
| **walk** | 0.8000 s | **基础动画（必须存在，循环播放）** |
| idle | 1.3333 s | 待机（姿势轮盘「待机」） |
| run | 0.5333 s | 奔跑（姿势轮盘「奔跑」） |
| jump | 0.3333 s | 跳跃（姿势轮盘「跳跃」） |
| blink | 0.2667 s | 眨眼（姿势轮盘「眨眼」） |
| beak_speak | 0.9333 s | 备用（菜单未使用） |
| fall | 0.4000 s | 备用（菜单未使用） |
| butt_slide / butt_slide_lean.L / .R | 0.3333 / 0.2667 / 0.2667 s | 备用（菜单未使用） |
| walk_lean.L / .R | 0.2667 s | 备用（菜单未使用） |
| walk_strafe.L / .R | 0.8000 s | 备用（菜单未使用） |

菜单实际引用的 5 个动画 = `walk/idle/run/jump/blink`，全部存在。
`Menu3D::LoadModels` 会对这 5 个名字逐个调用 `PlayAnimation` 并按返回值记录缺失项，
`FocusSelect` 切换姿势时若 `PlayAnimation` 返回 false 则回退 `walk`（契约 v1 第 7 节）。

## 4. Draw(scale, yOffset, yaw) 实测校准

`Menu3DModel::Draw` 内部顺序：`translate(0, yOffset - minY_*scale, 0)` → `rotateY(yaw)` → `scale(scale)`。

| 参数 | 实测取值 | 说明 |
| --- | --- | --- |
| `scale` | `clamp(0.30 * (8.6 - z) / 6.0, 0.055, 0.34)` | 透视距离补偿；`z` 为小鸡世界 z。`z=3.2` → 0.270，`z=7.0` → 0.080 |
| `yOffset` | `0.0` | 模型原点已在脚底，`Draw` 再按蒙皮最低点贴地，无需额外抬高 |
| `yaw` | `atan2(dx, dz) * 180/π` | 模型正面为 +Z；朝向由「到目标点的世界方向」推出，转身限速 220°/s |

换算到世界尺寸：`z=3.2` 时高 ≈ 0.498 世界单位，`z=7.0` 时高 ≈ 0.147 世界单位。
参考：T/CT 角色模型 `Draw(1.22, 0, 0)`，故小鸡约为角色身高的 1/4～1/5，符合「装饰小鸡」的比例。

## 5. 步幅 / 播放速率校准（避免滑步）

- 行走速度按距离缩放：`speed = 1.05 * (8.6 - z)/6.0 * variance`，`variance ∈ [0.85, 1.15]`
  （透视补偿后屏幕上三只鸡速度接近）。
- 期望步幅：一次 `walk` 循环位移 = `模型高 1.843 × scale × 1.0`（约一个身高）。
- 动画播放倍率：`rate = 0.8 / (步幅 / speed)`，由 `Menu3D::ChickenAnimTimeScale` 计算，
  并被 `Menu3D::Render` 用于 `Menu3DModel::Update(dt * rate)`。
- 实测结果：`z=3.2` 与 `z=7.0` 两端的倍率都落在 **1.29～1.75**（均值 ≈1.52），
  说明速度与缩放补偿自洽；`run` 姿势再乘 `0.8/0.5333 ≈ 1.5`。
- 该倍率是「动画相对位移」的校准值，**不改变** `UpdateChickens` 的移动边界与碰撞（菜单仅装饰）。

## 6. 加载失败降级

- `Menu3D::LoadModels` 用 `Load(path, "walk")` 的返回值判断；失败时读 `LastError()`
  打印 `[Menu3D] 小鸡模型加载失败: ...` 并清空 `chickens_`，三只鸡不绘制。
- 失败不会把未载入实例标记为 Ready，也不会阻塞主菜单（背景仍由角色/场景提供）。
- 资源缺失时 `Menu3D::DrawModels` / `DrawScene` 以 `chickens_.size()` 为上界循环，无越界。
