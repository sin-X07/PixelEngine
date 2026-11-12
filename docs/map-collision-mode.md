# 地图碰撞模式：cover / center

引擎把 `Collisions` 层的矩形栅格化到 8×8px 的碰撞格，规则由**地图属性** `collisionMode` 决定。

## 两种模式

| 模式 | 规则 | 适用 |
| --- | --- | --- |
| `cover`（默认） | 矩形只要碰到某格，整格都算实心：`floor(x/tile) … floor((x+w-ε)/tile)` | 手工在 8px 网格上画的图；历史地图 |
| `center` | **移动碰撞直接用画好的矩形（像素级 AABB）**；伤害/调试用的碰撞格按"格中心"采样，薄墙（<1 tile）先补厚到 1 tile | AI 生成/高清重绘、边缘不落在 8px 网格上的图 |

`cover` 的边界向外扩（0～+8px/边），是"空气墙"的常见来源。
`center` 模式里**人物/BOT 的移动碰撞不走网格**，而是直接和 `Collisions` 矩形做像素级 AABB：
既不会外扩出空气墙，也不会内缩让人物嵌进墙里。8px 网格只用于伤害判定和调试渲染。

## 怎么设置

### 方式 1：Tiled 地图属性

`地图 → 地图属性 → 自定义属性` 新增：

```
collisionMode = center      （字符串）
```

### 方式 2：命令行写入

```powershell
python tools/map_collision_debug.py assets/cs2d/maps/xxx.tmj --set-mode center --write assets/cs2d/maps/xxx.tmj
```

同一工具还能预览两种模式：

```powershell
python tools/map_collision_debug.py assets/cs2d/maps/xxx.tmj --mode center --out build-evidence/keep/center.png
python tools/map_collision_debug.py assets/cs2d/maps/xxx.tmj --mode cover  --out build-evidence/keep/cover.png
```

## 注意

- `center` 下，宽度 <8px 的**缝**可能被两侧各吃 4px 后堵死（BOT 碰撞盒 7px）：关键通道建议 ≥12px。
- 宽度 <8px 的**墙**会被补厚到 8px，不会整面消失。
- 只有设置了属性的地图走 `center`，其他地图行为完全不变（有单测覆盖）。
- 美术本身**不需要** 8px 对齐：引擎本来就是 8px 网格，对齐的只是碰撞矩形。

## 参数：collisionMargin

只影响 `center` 模式的**碰撞网格**（伤害/调试渲染），不影响移动。

| 值 | 含义 |
| --- | --- |
| `0` | 网格不外扩（伤害判定最保守） |
| `2`（默认） | 折中 |
| `4` | 网格等价 cover |

```powershell
python tools/map_collision_debug.py assets/cs2d/maps/xxx.tmj --set-margin 2 --write assets/cs2d/maps/xxx.tmj
```
