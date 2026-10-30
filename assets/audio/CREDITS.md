# 音频素材授权与来源

## 枪声（`fire_*.wav`）

除 `fire_zeus.wav`（程序合成）外，全部由 **Freesound** 素材包
[“Real Gun Sounds” by areniporgen（pack 38798）](https://freesound.org/people/areniporgen/packs/38798/)
裁剪而来：只保留单发枪声（含自然尾音），统一为 44.1kHz / 16bit / 单声道，
峰值归一化到 -1 dBFS。

| 素材 | 授权 | 链接 |
|---|---|---|
| Glock-19X | CC0 1.0 | https://freesound.org/s/828786/ |
| HK USP45 | CC0 1.0 | https://freesound.org/s/734062/ |
| SIG P220（外景） | CC0 1.0 | https://freesound.org/s/712085/ |
| Colt 1911 | CC0 1.0 | https://freesound.org/s/740897/ |
| Vektor CP1 | CC0 1.0 | https://freesound.org/s/712087/ |
| Glock-20SF | CC0 1.0 | https://freesound.org/s/734055/ |
| ASP Pistol | CC0 1.0 | https://freesound.org/s/734061/ |
| SIG P226 Legion | CC0 1.0 | https://freesound.org/s/711549/ |
| Desert Eagle | CC0 1.0 | https://freesound.org/s/712310/ |
| S&W M&P 9 | **CC BY 4.0** | https://freesound.org/s/692587/ |
| Colt Model 733 | CC0 1.0 | https://freesound.org/s/704511/ |
| HK MP7（消音） | CC0 1.0 | https://freesound.org/s/732250/ |
| Saiga MK-106 | CC0 1.0 | https://freesound.org/s/696023/ |
| Colt M4A1 SOCOM | CC0 1.0 | https://freesound.org/s/737569/ |
| FN SCAR-H | CC0 1.0 | https://freesound.org/s/702225/ |
| SIG 716 | **CC BY 4.0** | https://freesound.org/s/692584/ |
| Blaser Tactical 2 | CC0 1.0 | https://freesound.org/s/692209/ |
| McMillan TAC-50 | CC0 1.0 | https://freesound.org/s/737570/ |
| HK G3 | CC0 1.0 | https://freesound.org/s/712577/ |
| Remington 870 | CC0 1.0 | https://freesound.org/s/702672/ |

### 文件 → 素材对照

| 文件 | 素材 |
|---|---|
| `fire_glock18.wav` | Glock-19X |
| `fire_usp_s.wav`、`fire_ump45.wav` | HK USP45 |
| `fire_p2000.wav`、`fire_p90.wav` | SIG P220（外景） |
| `fire_dualberettas.wav` | Colt 1911 |
| `fire_p250.wav` | Vektor CP1 |
| `fire_fiveseven.wav` | Glock-20SF |
| `fire_tec9.wav` | ASP Pistol |
| `fire_cz75.wav` | SIG P226 Legion |
| `fire_deagle.wav` | Desert Eagle |
| `fire_r8revolver.wav` | S&W M&P 9（CC BY 4.0） |
| `fire_mac10.wav`、`fire_aug.wav` | Colt Model 733 |
| `fire_mp9.wav`、`fire_mp7.wav` | HK MP7（消音） |
| `fire_mp5sd.wav` | HK MP7（消音，额外低通） |
| `fire_galilar.wav` | Saiga MK-106 |
| `fire_famas.wav`、`fire_m4a4.wav` | Colt M4A1 SOCOM |
| `fire_m4a1s.wav` | Colt M4A1 SOCOM（额外低通） |
| `fire_sg553.wav`、`fire_scar20.wav` | FN SCAR-H |
| `fire_ak47.wav`、`fire_negev.wav` | SIG 716（CC BY 4.0） |
| `fire_ssg08.wav` | Blaser Tactical 2 |
| `fire_awp.wav` | McMillan TAC-50 |
| `fire_g3sg1.wav`、`fire_m249.wav` | HK G3 |
| `fire_nova.wav`、`fire_xm1014.wav`、`fire_sawedoff.wav`、`fire_mag7.wav` | Remington 870 |
| `fire_zeus.wav` | 程序合成（白噪声 + 下滑音） |

CC BY 4.0 素材在使用时需保留署名。本项目通过本文件与
[`tools/ASSET_SPEC.md`](../../tools/ASSET_SPEC.md) 第 5 节进行署名。

## 切枪音效（`switch_*.wav`）

按武器类别取段，素材来自 **Freesound**，全部为 **CC0 1.0（公共领域）**，
裁剪为 44.1kHz / 16bit / 单声道、峰值 -1 dBFS。原始下载与候选素材见
`prev_assets/weapon_switch_sounds/`。

| 文件 | 素材 | 链接 |
|---|---|---|
| `switch_pistol.wav` | Pistol Draw Unholster by nioczkus | https://freesound.org/s/377145/ |
| `switch_smg.wav` | Rifle_Equip1 by BaggoNotes | https://freesound.org/s/784753/ |
| `switch_rifle.wav` | Generic Rifle Cock by GameWithBepis | https://freesound.org/s/561471/ |
| `switch_sniper.wav` | Barrett M82A1 Charging Handle by qubodup | https://freesound.org/s/855598/ |
| `switch_shotgun.wav` | Shotgun Pump by Dentrabert | https://freesound.org/s/673320/ |
| `switch_mg.wav` | Gun draw holster by huddwink | https://freesound.org/s/239959/ |
| `switch_knife.wav` | Knife Draw by neilsher | https://freesound.org/s/411741/ |
| `switch_grenade.wav` | Equip Item, Weapon or Armor (metallic) by harrisonlace | https://freesound.org/s/829499/ |
| `switch_c4.wav` | Equip Item, Weapon or Armor (metallic) by harrisonlace | https://freesound.org/s/829499/ |

## 主菜单音乐（`music_menu*.ogg`）

设置 → 音频 →「背景音乐」可切换 3 首，全部为 **CC0 1.0**：

| 文件 | 曲目 | 时长 | 来源 |
|---|---|---|---|
| `music_menu.ogg` | Menu Music by wipics | 66.9s | https://opengameart.org/content/menu-music-1 |
| `music_menu_cyber.ogg` | Cyberpunk Menu Music by ZaninDevelopers | 12.8s | https://opengameart.org/content/cyberpunk-menu-music |
| `music_menu_calm.ogg` | Simple menu/background music loop by polosik | 119.8s | https://opengameart.org/content/simple-menubackground-music-loop |

- 由 `Mix_LoadMUS` + `Mix_PlayMusic(-1)` 循环播放；切歌即时生效。
- 原始下载与 WAV 备选见 `prev_assets/menu_music/`。

## UI 音效（`ui_click_*.ogg` / `ui_hover.ogg`）

- 素材：**UI Audio** by Kenney（Kenney Vleugels）
- 来源：https://kenney.nl/assets/ui-audio
- 授权：CC0 1.0（公共领域，无需署名）
- 使用：
  - `ui_click_1.ogg` ← `click3.ogg`（默认，0.09s）
  - `ui_click_2.ogg` ← `click1.ogg`
  - `ui_click_3.ogg` ← `click2.ogg`
  - `ui_click_4.ogg` ← `click4.ogg`
  - `ui_click_5.ogg` ← `click5.ogg`
  - `ui_hover.ogg` ← `rollover2.ogg`（0.06s）
- 设置 → 音频 →「按钮音效」可切换 5 种点击声（切换时立即试听）。
- 原始压缩包：`prev_assets/ui_sounds/kenney_ui-audio.zip`
## 爆炸 / 匕首 / 燃烧音效（本次替换）

- 素材：**150 Combat Sound Effects** by untamedpixels（CC0 1.0 合集）
- 来源：https://opengameart.org/content/150-combat-sound-effects
- 原始压缩包：`prev_assets/combat_sfx/150gameeffectsounds.zip`
- 使用：
  - `fx_explosion.wav` ← `low_frequency_explosion_000`（高爆雷爆炸，2s）
  - `fx_decoy.wav` ← `explosion_crunch_000`（诱饵弹爆炸，0.78s）
  - `fx_ignite.wav` ← `Fire Spell Impact 1`（燃烧弹点燃）
  - `melee_knife.wav` ← `knife_slice`（匕首挥砍，0.51s）
- 授权：CC0 1.0，合集内为多位作者的 CC0 素材，无需署名（见包内 LICENSES.txt）。

## 其他音效

`reload_*`、`throw_*`、`fx_flash`、`fx_smoke`、`bomb_*`、`round_*`、`sfx_*`
为项目原有素材，本次只做了统一重命名，未改变内容。
