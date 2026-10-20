# 素材规格契约 (ASSET SPEC)

本文档是 **素材的唯一权威规格**。所有占位素材由 `tools/gen_placeholders.py` 与
`tools/gen_placeholder_map.py` 按本规格生成；**用户真实素材到位后，直接覆盖同名同路径文件即可，
无需修改任何代码**。

---

## 0. 快速替换流程

1. 按本文档路径与尺寸准备素材（格式不符会加载失败或变形）。
2. 直接覆盖 `assets/` 下同名文件。
3. 重新启动游戏。启动日志会打印缺失汇总：`[Asset] 贴图缺失 N 项` / `[Asset] 音效缺失 N 项`。

---

## 1. 角色精灵

| 路径 | 尺寸 | 说明 |
|---|---|---|
| `assets/cs2d/sprites/ct_player.png` | 64×96 | CT 角色序列帧 |
| `assets/cs2d/sprites/t_player.png` | 64×96 | T 角色序列帧 |
| `assets/cs2d/sprites/t_player_alt.png` | 64×94 | T 备用外观 |

- 布局：**2 列 × 3 行**，每帧 32×32（`t_player_alt` 为 32×31）。
- 帧语义：第 1 行 = 主武器姿态，第 2 行左 = 近战，第 2 行右 = 副武器，第 3 行 = 死亡。
- 透明背景；**精灵原生朝上**（渲染时自动 +90° 对齐瞄准角）。

---

## 2. 武器 / 道具世界贴图

路径：`assets/cs2d/sprites/<name>.png`

- **任意正方形 PNG**（建议 64×64）。渲染时统一缩放到固定正方形尺寸，分辨率不影响玩法。
- 透明背景。
- **原生朝向见下表**（渲染时按 `degOffset` 旋转）：

| 文件 | 武器 | 原生朝向 | degOffset |
|---|---|---|---|
| `ak47.png` | AK-47 | 朝上 | 90° |
| `m4a1.png` | M4A1 | 朝上 | 90° |
| `galil.png` | Galil | 朝上 | 90° |
| `awp.png` | AWP | 朝上 | 90° |
| `shotgun.png` | 霰弹枪 | 朝上 | 90° |
| `smg.png` | SMG | 朝上 | 90° |
| `deagle.png` | 沙鹰 | **朝右** | **0°** |
| `knife.png` | 匕首 | 朝上 | 90° |
| `grenade_he.png` | 高爆手雷 | 朝上 | 90° |
| `grenade_flash.png` | 闪光弹 | 朝上 | 90° |
| `grenade_smoke.png` | 烟雾弹 | 朝上 | 90° |
| `c4.png` | C4 炸弹 | 朝上 | 90° |
| `defuse_kit.png` | 拆弹器 | 朝上 | 90° |
| `hostage.png` | 人质 | 朝上 | 90° |

---

## 3. HUD 武器图标

路径：`assets/images/weapons/<name>_d.png`

- 建议 75×64（显示于屏幕右下 160×56 面板左侧，会等比缩放）。
- 透明背景。

| 文件 | 对应武器 |
|---|---|
| `AK47_d.png` | AK-47 |
| `M4A1_d.png` | M4A1 |
| `glock_d.png` | Glock |
| `galil_d.png` | Galil |
| `awp_d.png` | AWP |
| `shotgun_d.png` | 霰弹枪 |
| `smg_d.png` | SMG |
| `grenade_he_d.png` | 高爆手雷 |
| `grenade_flash_d.png` | 闪光弹 |
| `grenade_smoke_d.png` | 烟雾弹 |

> 说明：`Deagle` / `Knife` 的 HUD 图标直接复用 `Deagle.png` / `Knife.png`（大图自动缩放），
> 不需要单独的 `_d` 版本。

---

## 4. Tileset

| 路径 | 尺寸 | 说明 |
|---|---|---|
| `assets/cs2d/tilesets/cs_office_tiles.png` | 96×704 | cs_office 地图图块集 |

- 图块 32×32，**3 列 × 22 行 = 66 块**，`columns` 必须与 TMJ 中的 `columns` 一致。
- 图块集在 TMJ 中以相对路径 `../tilesets/cs_office_tiles.png` 引用。
- 缺失时渲染器自动回退为纯色块（`DrawTileFallback`），不崩溃。

---

## 5. 音效

路径：`assets/audio/`，格式 **wav / mp3 / ogg 均可**（SDL_mixer 解码）。

| 文件 | 用途 |
|---|---|
| `ak47.wav` | AK-47 射击 |
| `m4a1.mp3` | M4A1 射击 |
| `galir.wav` | Galil 射击 |
| `awp.wav` | AWP 射击 |
| `deagle.mp3` | 沙鹰射击 |
| `glock.wav` | Glock 射击 |
| `shotgun.wav` | 霰弹枪射击 |
| `smg.wav` | SMG 射击 |
| `katana.mp3` | 近战挥砍 |
| `dagger_attack.wav` | 匕首攻击 |
| `ak47_reload.mp3` | AK-47 换弹 |
| `m4a1_reload.mp3` | M4A1 换弹 |
| `galir_reload.mp3` | Galil 换弹 |
| `awp_reload.mp3` | AWP 换弹 |
| `deagle_reload.wav` | 沙鹰换弹 |
| `glock_reload.ogg` | Glock 换弹 |
| `shotgun_load.mp3` | 霰弹枪装填 |
| `shotgun_loadBullet.mp3` | 霰弹枪单发装弹 |
| `hit.wav` | 命中 |
| `kill.wav` | 击杀 |
| `player_hurt.ogg` | 玩家受伤 |
| `armor_hit.wav` | 护甲受击 |
| `grenade_explode.wav` | 手雷爆炸 |
| `flashbang.wav` | 闪光弹爆响 |
| `smoke.wav` | 烟雾弹释放 |
| `bomb_plant.wav` | C4 下包 |
| `bomb_defuse.wav` | C4 拆包 |
| `bomb_beep.wav` | C4 滴答 |
| `round_start.wav` | 回合开始 |
| `round_win.wav` | 回合胜利 |
| `round_lose.wav` | 回合失败 |
| `footstep.wav` | 脚步 |
| `menu.wav` | 主菜单音乐 |
| `button.ogg` | 按钮悬停 |
| `click.ogg` | 按钮点击 |
| `pickup.wav` | 拾取 |

缺失时 `AudioManager::loadChunk` 返回空指针，播放静默，**不会崩溃**。

---

## 6. 地图 (TMJ / Tiled JSON)

路径：`assets/cs2d/maps/<name>.tmj`

- 格式：Tiled 导出的 **JSON (.tmj)**。
- 建议尺寸：80×75 ~ 100×100 tiles，`tilewidth = tileheight = 8`。
- 地图像素尺寸 = `width*8 × height*8`（相机以它为边界）。

### 6.1 图层约定

| 图层名（不区分大小写） | 类型 | 作用 |
|---|---|---|
| `ground` | tilelayer | 地面 |
| `walls` | tilelayer | 墙体视觉 |
| `decorations` | tilelayer | 装饰 |
| `overlay` | tilelayer | 顶层覆盖（绘制在角色之上） |
| `Collisions` | objectgroup | **矩形对象 → 碰撞体（完全阻挡）** |
| `Entities`（或 `objects`） | objectgroup | 出生点 / 目标物 / 购买区 |
| 任意 imagelayer | imagelayer | 背景图 |

### 6.2 对象层契约（**重要：制作 5v5 地图请严格遵循**）

`Entities` 层中每个对象的 `type` 字段：

| type | 几何 | 额外字段 | 含义 |
|---|---|---|---|
| `spawn_t` | 点 | — | T 阵营出生点 |
| `spawn_ct` | 点 | — | CT 阵营出生点 |
| `spawnpoint` | 点 | — | 通用出生点（无阵营区分时使用） |
| `bomb_site` | 矩形 | `name = "A"` 或 `"B"` | 爆破点 |
| `hostage` | 点或矩形 | — | 人质 |
| `buy_zone` | 矩形 | 属性 `team = t / ct / both` | 购买区 |

`Collisions` 层中**所有矩形对象**一律视为完全阻挡，不区分 `type`。

### 6.3 示例（最小可用 Entities 层）

```json
{
  "name": "Entities",
  "type": "objectgroup",
  "visible": true,
  "objects": [
    { "type": "spawn_t",  "x": 80,  "y": 600, "width": 0, "height": 0 },
    { "type": "spawn_ct", "x": 720, "y": 200, "width": 0, "height": 0 },
    { "type": "bomb_site", "name": "A", "x": 640, "y": 560, "width": 96, "height": 96 },
    { "type": "bomb_site", "name": "B", "x": 96,  "y": 96,  "width": 96, "height": 96 },
    { "type": "buy_zone", "x": 40, "y": 560, "width": 120, "height": 80,
      "properties": [ { "name": "team", "type": "string", "value": "t" } ] }
  ]
}
```

---

## 7. 素材来源原则

- **占位素材**：由脚本生成，带文字标签与区分色，用于保证「设计先于素材」。
- **真实素材**：直接覆盖同名文件，无需改代码、无需改配置。
- 占位生成记录保存在 `tools/placeholders.json`；`--force` 只重建清单内的占位，绝不覆盖清单外的真实素材。
