# PIX-32 素材批量接入清单

2026-11-15，原始素材基准 fd729b9。41 组武器共 82 张图，人物与描边 6 张。以下尺寸为实际交付尺寸。

| 武器 | 世界贴图 | 尺寸 | 图标 | 尺寸 |
| --- | --- | --- | --- | --- |
| Knife | knife.png | 77×1024 | knife_d.png | 256×128 |
| Glock18 | glock.png | 142×1024 | glock_d.png | 256×128 |
| USP_S | usp.png | 113×1008 | usp_d.png | 256×128 |
| P2000 | p2000.png | 130×1024 | p2000_d.png | 256×128 |
| DualBerettas | dual_berettas.png | 379×821 | dualberettas_d.png | 256×128 |
| P250 | p250.png | 166×997 | p250_d.png | 256×128 |
| FiveSeven | fn57.png | 141×928 | fn57_d.png | 256×128 |
| Tec9 | tec9.png | 95×1024 | tec9_d.png | 256×128 |
| CZ75 | cz75.png | 126×938 | cz75_d.png | 256×128 |
| Deagle | deagle.png | 145×1024 | deagle_d.png | 256×128 |
| R8Revolver | r8.png | 154×980 | r8_d.png | 256×128 |
| Zeus | zeus.png | 138×720 | zeus_d.png | 256×128 |
| MAC10 | mac-10.png | 101×1024 | mac-10_d.png | 256×128 |
| MP9 | mp9.png | 81×1024 | mp9_d.png | 256×128 |
| MP7 | mp7.png | 115×938 | mp7_d.png | 256×128 |
| MP5SD | mp5sd.png | 97×975 | mp5sd_d.png | 256×128 |
| UMP45 | ump45.png | 103×1024 | ump45_d.png | 256×128 |
| P90 | p90.png | 89×1024 | p90_d.png | 256×128 |
| GalilAR | galil.png | 75×1024 | galil_d.png | 256×128 |
| FAMAS | famas.png | 116×1024 | famas_d.png | 256×128 |
| AK47 | ak47.png | 82×1024 | ak47_d.png | 256×128 |
| M4A4 | m4a4.png | 102×1024 | m4a4_d.png | 256×128 |
| M4A1S | m4a1.png | 81×1024 | m4a1_d.png | 256×128 |
| SG553 | sg553.png | 85×981 | sg553_d.png | 256×128 |
| AUG | aug.png | 101×1024 | aug_d.png | 256×128 |
| SSG08 | ssg08.png | 61×1024 | ssg08_d.png | 256×128 |
| AWP | awp.png | 108×1024 | awp_d.png | 256×128 |
| SCAR20 | scar20.png | 83×979 | scar20_d.png | 256×128 |
| G3SG1 | g3sg1.png | 98×1024 | g3sg1_d.png | 256×128 |
| Nova | nova.png | 62×1024 | nova_d.png | 256×128 |
| XM1014 | xm1014.png | 68×997 | xm1014_d.png | 256×128 |
| SawedOff | sawedoff.png | 72×1024 | sawedoff_d.png | 256×128 |
| MAG7 | mag7.png | 93×1024 | mag7_d.png | 256×128 |
| M249 | m249.png | 87×1015 | m249_d.png | 256×128 |
| Negev | negev.png | 111×1024 | negev_d.png | 256×128 |
| GrenadeHE | grenade_he.png | 872×1024 | grenade_he_d.png | 256×128 |
| GrenadeFlash | grenade_flash.png | 836×1024 | grenade_flash_d.png | 256×128 |
| GrenadeSmoke | grenade_smoke.png | 1015×1024 | grenade_smoke_d.png | 256×128 |
| GrenadeMolotov | grenade_molotov.png | 1018×1024 | grenade_molotov_d.png | 256×128 |
| GrenadeIncendiary | grenade_incendiary.png | 1024×1017 | grenade_incendiary_d.png | 256×128 |
| GrenadeDecoy | grenade_decoy.png | 894×984 | grenade_decoy_d.png | 256×128 |

世界与图标各 41 张内容哈希均独立，引用全部存在，透明通道检查通过。

双持新图 dualberettas.png 原字节迁移至 dual_berettas.png，WeaponDef 同步改引用。旧 dual_berettas.png 由新图替换。未引用的 shootgun_d.png 与 shotgun.png 已清理，现有四种霰弹枪使用各自独立的贴图和图标。

六张人物与描边表均为 1024×1536，三种姿势使用 (0,0)、(0,1)、(1,1) 三个 512 格。投掷物与 C4 使用近战姿势。

41 张武器图标均符合 256×128 画布规范，运行时等比缩放。燃烧瓶侧视图标为 Muse 已交付的程序绘制临时版本。

人物描边按 512 格单独生成，修复全图膨胀导致的邻格碎线。
