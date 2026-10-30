# 2D 贴图来源与授权

## 投掷物图标（`grenade_*.png` / `grenade_*_d.png`）

- 素材：**FPS Icons (51 high res icons)** by David Stenfors（da_st）
- 来源：https://opengameart.org/content/fps-icons-51-high-res-icons
- 作者站点：https://www.davidstenfors.com
- 授权：**CC-BY 3.0**（使用时需署名并提供作者站点链接）
- 使用与修改：
  - 用 `hand_grenade_icon_1`、`flashbang_icon_2`、`smoke_grenade_icon_1` 分别做
    高爆雷 / 闪光弹 / 烟雾弹；
  - 燃烧弹、燃烧瓶、诱饵弹没有对应造型，使用 `hand_grenade_icon_4`、
    `smoke_grenade_icon_2`、`remote_icon_1` 做色相调整区分；
  - 统一裁剪、缩放为 64×64（世界/手持）与 128×128（掉落物/HUD），
    并加上主色、左上高光、右下压暗和深色描边。
- 生成脚本：`tools/gen_grenade_sprites.py`
- 原始压缩包：`prev_assets/grenade_icons/FPS Icons.zip`

## 其他角色 / 武器贴图

`ct_player.png`、`t_player.png`、`weapon_*` / `*_d.png` 等为项目原有素材。
