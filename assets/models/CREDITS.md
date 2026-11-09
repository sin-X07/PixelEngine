# 3D 模型来源与授权

## Quaternius — Toon Shooter Game Kit

- 文件：`quaternius/Character_Enemy.gltf`、`quaternius/Character_Soldier.gltf`
- 文件：`quaternius/Guns/AK.gltf`、`Pistol.gltf`、`Knife_1.gltf`（轮盘武器预览）
- 作者：Quaternius
- 来源：https://quaternius.com/packs/toonshootergamekit.html
- 授权：CC0 1.0（公共领域）
- 修改：仅重命名/组合为 T/CT 装饰角色与轮盘武器预览；未修改网格、骨骼或动画。

## Chickensoft — Chicken mascot

- 文件：`chickensoft/chicken.glb`
- 作者：Chickensoft / Thibaud Goiffon
- 来源：https://opengameart.org/content/chickensoft-chicken
- 授权：模型 CC BY 4.0；动画 CC0 1.0
- 署名：Chickensoft 3D Mascot Model by Chickensoft, licensed CC BY 4.0.
- 修改：在运行时为其缺少材质的 GLB 指定默认暖白色材质。

## 主菜单自定义配件（项目自制）

- 文件：`custom/cap.gltf`、`custom/glasses.gltf`、`custom/backpack.gltf`、`custom/chicken_hat.gltf`
- 来源：`tools/gen_menu_accessories.py` 程序化生成的低模几何体（纯色材质，无贴图）
- 授权：随项目发布，可自由修改
- 修改：如需调整造型，改脚本后重新生成即可

## 主菜单骨骼配饰与持续姿势衍生模型

- `menu/t.glb`、`menu/ct.glb` 基于上列 Quaternius 角色和武器几何，沿用 CC0 1.0。
- `menu/chicken.glb` 基于上列 Chickensoft / Thibaud Goiffon 模型，沿用 CC BY 4.0 署名。
- 衍生修改包括配饰、骨骼挂点、纯色材质和持续姿势，原始模型文件保留。
- 手部局部网格、独立掌骨和指节权重由 `tools/menu-models/hand_rig.py` 生成。`validate_hands.py` 检查两个人物全部武器姿势在循环内的手指表面相交。
- 可编辑源文件为 `tools/menu-models/CharacterLab.blend`，导出脚本为同目录的 `export_models.py`。
- 在仓库根目录运行 `blender --background --python tools/menu-models/export_models.py` 可重新生成三份 GLB，制作版本为 Blender 5.2.3 LTS。

## 说明

这些模型仅用于主菜单装饰。T/CT 不代表 Valve 的《反恐精英》角色，
不使用 CS2/CS:GO 的原版角色资产。
