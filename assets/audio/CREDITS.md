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

## 其他音效

`ui_*`、`music_*`、`reload_*`、`melee_*`、`throw_*`、`fx_*`、`bomb_*`、`round_*`、
`sfx_*` 为项目原有素材，本次只做了统一重命名，未改变内容。
