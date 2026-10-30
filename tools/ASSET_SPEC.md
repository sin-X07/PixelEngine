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
| `ak47.png` | AK-47 (T) | 朝上 | 90° |
| `m4a4.png` | M4A4 (CT) | 朝上 | 90° |
| `m4a1.png` | M4A1-S (CT) | 朝上 | 90° |
| `galil.png` | Galil AR (T) | 朝上 | 90° |
| `famas.png` | FAMAS (CT) | 朝上 | 90° |
| `sg553.png` | SG 553 (T) | 朝上 | 90° |
| `aug.png` | AUG (CT) | 朝上 | 90° |
| `awp.png` | AWP | 朝上 | 90° |
| `ssg08.png` | SSG 08 | 朝上 | 90° |
| `scar20.png` | SCAR-20 (CT) | 朝上 | 90° |
| `g3sg1.png` | G3SG1 (T) | 朝上 | 90° |
| `nova.png` | Nova | 朝上 | 90° |
| `xm1014.png` | XM1014 | 朝上 | 90° |
| `sawedoff.png` | Sawed-Off (T) | 朝上 | 90° |
| `mag7.png` | MAG-7 (CT) | 朝上 | 90° |
| `m249.png` | M249 | 朝上 | 90° |
| `negev.png` | Negev | 朝上 | 90° |
| `mac-10.png` | MAC-10 (T) | 朝上 | 90° |
| `mp9.png` | MP9 (CT) | 朝上 | 90° |
| `mp7.png` | MP7 | 朝上 | 90° |
| `mp5sd.png` | MP5-SD | 朝上 | 90° |
| `ump45.png` | UMP-45 | 朝上 | 90° |
| `p90.png` | P90 | 朝上 | 90° |
| `deagle.png` | 沙鹰 | **朝右** | **0°** |
| `glock.png` | Glock-18（T 起始手枪） | **朝右** | **0°** |
| `usp.png` | USP-S（CT 起始手枪） | **朝右** | **0°** |
| `p2000.png` | P2000（CT 起始手枪备选） | **朝右** | **0°** |
| `dualberettas.png` | Dual Berettas | **朝右** | **0°** |
| `p250.png` | P250 | **朝右** | **0°** |
| `fn57.png` | Five-SeveN (CT) | **朝右** | **0°** |
| `tec9.png` | Tec-9 (T) | **朝右** | **0°** |
| `cz75.png` | CZ75-Auto | **朝右** | **0°** |
| `r8.png` | R8 Revolver | **朝右** | **0°** |
| `zeus.png` | Zeus x27 | **朝右** | **0°** |
| `knife.png` | 匕首 | 朝上 | 90° |
| `grenade_he.png` | 高爆手雷 | 朝上 | 90° |
| `grenade_flash.png` | 闪光弹 | 朝上 | 90° |
| `grenade_smoke.png` | 烟雾弹 | 朝上 | 90° |
| `grenade_molotov.png` | 燃烧瓶 (T) | 朝上 | 90° |
| `grenade_incendiary.png` | 燃烧弹 (CT) | 朝上 | 90° |
| `grenade_decoy.png` | 诱饵弹 | 朝上 | 90° |
| `c4.png` | C4 炸弹 | 朝上 | 90° |
| `defuse_kit.png` | 拆弹器 | 朝上 | 90° |

> **占位素材说明**：上表中新增的 CS2 武器（标 `(T)` / `(CT)` 及部分通用枪）
> 当前由 `tools/gen_weapon_placeholders.py` 生成的**占位图**填充。
> 直接用真实素材覆盖同名文件即可，无需改代码。
> 重新生成缺失项：`python tools/gen_weapon_placeholders.py`
> 检查还缺哪些：`python tools/list_missing_sprites.py`

---

## 3. 状态栏 / 掉落物图标（`_d` 后缀）

**命名约定：同一个目录，用后缀区分「用在什么状态」。**

| 用途 | 文件 | 字段 | 尺寸建议 |
|---|---|---|---|
| 人物手持（世界内） | `assets/cs2d/sprites/<name>.png` | `WeaponDef::spritePath` | 64×64，见第 2 节朝向 |
| HUD 状态栏 + 掉落武器 | `assets/cs2d/sprites/<name>_d.png` | `WeaponDef::iconPath` | 75×64，透明背景 |

| `<name>` | 对应武器 |
|---|---|
| `knife` | 匕首 |
| `glock` | Glock |
| `usp` | USP |
| `deagle` | Deagle |
| `galil` | Galil |
| `ak47` | AK-47 |
| `m4a1` | M4A1 |
| `shotgun` | 霰弹枪 |
| `awp` | AWP |
| `grenade_he` / `grenade_flash` / `grenade_smoke` | 三种投掷物 |

> `_d` 图标会被等比缩放进 44×44 方框（HUD）和约 9 个世界单位（掉落物），
> 所以**不要为了适配而预先拉伸**，保持原始比例即可。
> `iconPath` 留空也能跑：会回退用 `<name>.png`，但两套用途不同建议补齐。

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

**命名约定**：`<分组>_<对象>.<ext>`，全小写 + 下划线，同一分组内不再混用旧名字。

| 前缀 | 含义 |
|---|---|
| `ui_` | 界面（悬停 / 点击） |
| `music_` | 背景音乐 |
| `fire_` | 枪声，**每把可射击武器一个文件** |
| `reload_` | 换弹 |
| `melee_` | 近战 |
| `throw_` | 投掷出手 |
| `fx_` | 爆炸 / 闪光 / 烟雾 |
| `bomb_` | C4 下包 / 拆包 / 滴答 |
| `round_` | 回合事件 |
| `sfx_` | 命中 / 击杀 / 脚步 / 购买等通用反馈 |

> **枪声必须是单发素材。** 每射出一颗子弹都会调用一次
> `AudioManager::PlayWeaponFire`；素材里若有第二声枪响，就会变成「打一发、响两枪」。
> 连发/全自动录音不能直接放进 `fire_*.wav`，必须裁出单发（含自然尾音）。

### 5.1 枪声 `fire_<weapon>.wav`

每把枪一个文件（44.1kHz / 16bit / 单声道），由 `AudioManager.cpp` 的
`kWeaponFireAssets` 表按 `WeaponId` 绑定。当前部分武器复用同一段实录（不同裁剪长度
或低通），后续替换单个文件即可单独换音色，无需改代码。

| 文件 | 武器 | 素材 |
|---|---|---|
| `fire_glock18.wav` | Glock-18 | Glock-19X |
| `fire_usp_s.wav` | USP-S | HK USP45 |
| `fire_p2000.wav` | P2000 | SIG P220 |
| `fire_dualberettas.wav` | Dual Berettas | Colt 1911 |
| `fire_p250.wav` | P250 | Vektor CP1 |
| `fire_fiveseven.wav` | Five-SeveN | Glock-20SF |
| `fire_tec9.wav` | Tec-9 | ASP Pistol |
| `fire_cz75.wav` | CZ75-Auto | SIG P226 Legion |
| `fire_deagle.wav` | Desert Eagle | Desert Eagle |
| `fire_r8revolver.wav` | R8 Revolver | S&W M&P 9 |
| `fire_zeus.wav` | Zeus x27 | 合成电击声 |
| `fire_mac10.wav` | MAC-10 | Colt Model 733 |
| `fire_mp9.wav` | MP9 | HK MP7（消音） |
| `fire_mp7.wav` | MP7 | HK MP7（消音） |
| `fire_mp5sd.wav` | MP5-SD | HK MP7（消音 + 低通） |
| `fire_ump45.wav` | UMP-45 | HK USP45 |
| `fire_p90.wav` | P90 | SIG P220 |
| `fire_galilar.wav` | Galil AR | Saiga MK-106 |
| `fire_famas.wav` | FAMAS | Colt M4A1 SOCOM |
| `fire_ak47.wav` | AK-47 | SIG 716 |
| `fire_m4a4.wav` | M4A4 | Colt M4A1 SOCOM |
| `fire_m4a1s.wav` | M4A1-S | Colt M4A1 SOCOM（低通） |
| `fire_sg553.wav` | SG 553 | FN SCAR-H |
| `fire_aug.wav` | AUG | Colt Model 733 |
| `fire_ssg08.wav` | SSG 08 | Blaser Tactical 2 |
| `fire_awp.wav` | AWP | McMillan TAC-50 |
| `fire_scar20.wav` | SCAR-20 | FN SCAR-H |
| `fire_g3sg1.wav` | G3SG1 | HK G3 |
| `fire_nova.wav` | Nova | Remington 870 |
| `fire_xm1014.wav` | XM1014 | Remington 870 |
| `fire_sawedoff.wav` | Sawed-Off | Remington 870 |
| `fire_mag7.wav` | MAG-7 | Remington 870 |
| `fire_m249.wav` | M249 | HK G3 |
| `fire_negev.wav` | Negev | SIG 716 |

### 5.2 其他音效

| 文件 | 用途 |
|---|---|
| `ui_hover.ogg` | 按钮悬停 |
| `ui_click_1.ogg` … `ui_click_5.ogg` | 按钮点击（设置里可切换 5 种） |
| `music_menu.ogg` / `music_menu_cyber.ogg` / `music_menu_calm.ogg` | 主菜单音乐（设置里可切换 3 首，循环 BGM） |
| `reload_glock.mp3` | Glock 换弹 |
| `reload_usp.mp3` | USP 换弹 |
| `reload_deagle.mp3` | Deagle 换弹 |
| `reload_galil.mp3` | Galil 换弹 |
| `reload_ak47.mp3` | AK-47 换弹 |
| `reload_m4a1.mp3` | M4A1 换弹 |
| `reload_shotgun.mp3` | 霰弹枪换弹 |
| `reload_awp.mp3` | AWP 换弹 |
| `melee_knife.wav` | 匕首攻击 |
| `throw_generic.mp3` | 投掷出手（通用兜底） |
| `throw_he.mp3` | 高爆雷出手 |
| `throw_flash.mp3` | 闪光弹出手 |
| `throw_smoke.mp3` | 烟雾弹出手 |
| `fx_explosion.wav` | 手雷爆炸 |
| `fx_flash.mp3` | 闪光弹爆响 |
| `fx_smoke.mp3` | 烟雾弹释放 |
| `bomb_plant.wav` | C4 下包 |
| `bomb_defuse.wav` | C4 拆包 |
| `bomb_beep.wav` | C4 滴答 |
| `round_start.wav` | 回合开始 |
| `round_win.wav` | 回合胜利 |
| `round_lose.wav` | 回合失败 |
| `sfx_hit.wav` | 命中 |
| `sfx_kill.mp3` | 击杀 |
| `sfx_armor_hit.wav` | 护甲受击 |
| `sfx_footstep.mp3` | 脚步 |
| `sfx_equip.mp3` | 购买装备 |

**切枪音效**（`switch_*.wav`，按武器类别，切到该类任意武器时播放）：

| 文件 | 适用武器类别 |
|---|---|
| `switch_knife.wav` | 近战（匕首） |
| `switch_pistol.wav` | 手枪 / 电击枪 |
| `switch_smg.wav` | 冲锋枪 |
| `switch_rifle.wav` | 步枪 |
| `switch_sniper.wav` | 狙击枪 |
| `switch_shotgun.wav` | 霰弹枪 |
| `switch_mg.wav` | 机枪 |
| `switch_grenade.wav` | 投掷物（手雷槽） |
| `switch_c4.wav` | C4 |

### 5.3 素材来源与授权

新的枪声来自 Freesound 素材包 **“Real Gun Sounds” by areniporgen**（pack 38798）。
其中 `SIG 716` 与 `S&W M&P 9` 两段为 **CC BY 4.0**，其余为 **CC0**。

切枪音效来自 Freesound 的若干 **CC0** 素材（手枪拔出 / 拉栓 / 拉机柄 /
霰弹枪上膛 / 匕首出鞘 / 金属装备），逐条来源、链接与授权见
`assets/audio/CREDITS.md`，原始下载文件在 `prev_assets/weapon_switch_sounds/`。

缺失时 `AudioManager::loadChunk` 返回空指针，播放静默，**不会崩溃**；启动日志会打印
`[Audio] 缺失: <路径>`，并在最后汇总 `[Asset] 音效缺失 N 项`。

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

支持**两种**写法，可混用。语义来源优先级：**对象自身的 `type` > 所在图层名**。

**写法 A：单一 `Entities` 层，靠对象 `type` 区分**

| type | 几何 | 额外字段 | 含义 |
|---|---|---|---|
| `spawn_t` | 点 | — | T 阵营出生点 |
| `spawn_ct` | 点 | — | CT 阵营出生点 |
| `spawnpoint` | 点 | — | 通用出生点（无阵营区分时使用） |
| `bomb_site` | 矩形 | `name = "A"` 或 `"B"` | 爆破点 |
| `buy_zone` | 矩形 | 属性 `team = t / ct / both` | 购买区 |

**写法 B：每类一个对象层，对象 `type` 留空**（`ancient_ruins.tmj` 采用此写法）

| 图层名 | 对象的 type | 几何 | 含义 |
|---|---|---|---|
| `spawn_t` | 空 | 点 | T 出生点 |
| `spawn_ct` | 空 | 点 | CT 出生点 |
| `bomb_site` | 空 | 矩形 | 爆破点（**可以有多层，会累加**） |
| `buy_zone` | 空 | 矩形 | 购买区（可以有多层，会累加） |

> 图层名大小写不敏感；`spawns` / `bombsites` / `buyzones` 等等价写法都认。

`Collisions` 层中**所有矩形对象**一律视为完全阻挡，不区分 `type`。

背景图：`imagelayer` 引用的图片若在运行时找不到（例如指向工程外的
`../../prev_assets/xxx.jpg`），引擎会自动按**文件名**到 `assets/cs2d/tilesets/` 下再找一次。
因此把地图用到的背景图以同名文件放进 `assets/cs2d/tilesets/` 即可，无需改地图。

### 6.3 地图缩放工具（地图太大时用）

```powershell
python tools/scale_map.py assets/cs2d/maps/ancient_ruins.tmj --dry-run   # 预览
python tools/scale_map.py assets/cs2d/maps/ancient_ruins.tmj             # 默认每条轴 ÷2（面积 ÷4）
python tools/scale_map.py <map.tmj> --div 4                              # 面积 ÷16
```

做的事：地图 `width/height`（tile 数）、所有对象的 `x/y/width/height`、
imagelayer 的 `imagewidth/imageheight` 全部除以 `--div`；`tilewidth/tileheight` 不变。
**组图层（`group`）里的子对象层会一并递归处理**，所以 Tiled 里按区域分组的
`site` 层（A_Site / Mid / B_Site …）坐标同样会被缩放。

**背景图不用重新做**——渲染时背景会被拉伸到 imagelayer 声明的尺寸，
原图保持全分辨率。脚本会额外打印缩放后最薄的墙体，若不足 1 个 tile
（8px）会提示栅格化吸附风险。

缩放后用两个校验脚本确认没有破坏可玩性：

```powershell
python tools/verify_scaled_map.py <原图.tmj> <缩放图.tmj> --div 2   # 逐项核对坐标是否恰好 ÷2
python tools/check_map_connectivity.py <缩放图.tmj>               # 出生点/包点连通性 + 可达面积
```

`check_map_connectivity.py` 复刻引擎的碰撞栅格化规则（按 `PathAgentRadius=4` 膨胀墙体），
输出实心格占比与可通行连通分量。正常结果应为「出生点 / 包点全部 `on_passable=1`
且落在同一连通分量」；若出现多个分量或有点落在实心格上，说明走廊被吸附堵死。

### 6.4 出生点拆分工具
若地图只有通用 `SpawnPoint`，双方会从同一批点出生。用脚本按空间位置自动拆分：

```powershell
python tools/split_spawns.py assets/cs2d/maps/cs_office.tmj --dry-run  # 预览
python tools/split_spawns.py assets/cs2d/maps/cs_office.tmj            # 写回
python tools/split_spawns.py <map.tmj> --axis y                        # 指定切分轴
```

默认按较长轴对半切分：左半 → `spawn_t`，右半 → `spawn_ct`（y 轴同理）。
启动日志会显示每张图的阵营出生点数量，例如 `出生点 12 (T6/CT6)`。

> `buy_zone` 缺失时视为全图可购买；建议在双方出生点附近各放一个，
> 用属性 `team = t / ct` 限定阵营。

### 6.5 弹药模型自检

武器弹药规则（备弹 = 弹匣 × 3、换弹扣备弹、捡枪沿用丢弃状态）有一套独立自检，
改动 `Player` 的弹药字段后建议跑一次：

```powershell
# 需要 SDL 头文件；从构建目录借用一份即可
g++ -std=c++17 -DSDL_MAIN_HANDLED ^
    -I PixelEngine -I <SDL2 include> -I <SDL2 include>/SDL2 ^
    tools/ammo_test.cpp -o ammo_test.exe
./ammo_test.exe
```

覆盖：备弹容量换算、换弹扣减、备弹不足/耗尽、捡枪沿用弹药、越界夹紧、
跨槽位持久化、清槽清备弹。全部输出 `[ OK ]` 即通过。

### 6.5 示例（最小可用 Entities 层）

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
