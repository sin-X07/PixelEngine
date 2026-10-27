# 战术库来源说明

本目录下 `_common.txt` 与各 `<mapId>.txt` 均由公开 CS/CS2 IGL 与地图指南**改写**为 PixelEngine intent 枚举（非原文粘贴）。  
`_common.txt` 只保留与地图无关的总规则；各图文件只写区域别名、T/CT/经济模板与 few-shot。  
引擎不执行烟雾线，道具细节已压缩为「时机/意图」。查阅日期：2026-10-27（Asia/Shanghai）。

链接状态：已用 WebFetch 核对；标明「可开」或「失效/跳转」；失效者已换备用链。

---

## 通用 IGL

- ClutchLeague Academy — Building a playbook（可开）  
  https://clutchleague.gg/learn/teamplay/playbook  
  → 默认→命名 execute→预备回答；剧本宜小而人人会。

- ClutchLeague Academy — Executes, splits, pinches and fakes（可开）  
  https://clutchleague.gg/learn/attack/executes  
  → slow/fast/split/fake 选用条件，改写入 intent 选择逻辑。

- Guild Order — CS2 IGL callable round structure（可开）  
  https://guildorder.com/games/csgo/guides/igl-round-structure  
  → default / 少量 set-play / mid-round 三层；经济与战术同呼。

- myCS2Guru — map strategy / playbook lessons（沿用既有 SOURCES）  
  https://mycs2guru.com/lessons/

---

## 脚步声 / 静步

- CSStatLab — Sound Cues and Audio in CS2（可开）
  https://csstatlab.com/academy/gameplay/sound-cues
  → 跑动脚步 = 对方正在投入且短期不准备交火；近距离走路脚步 = 正在为 peek 做铺垫；
  用噪音做假、假设对手也在听。改写为 `_common.txt` 的「脚步与静步」时机。

- Meccha Wiki — Anchor / Lurk（可开）
  https://meccha.wiki/wikis/counter-strike-2/roles/anchor
  → T lurk 先等交火噪音再走；CT anchor 以拖延和活到最后为目标。
  对应引擎里 lurk/save BOT 的静步与「听到脚步再查」行为。

---

## dust_2

- GamGem — Dust 2 strats: T default, CT setup, executes（可开）  
  https://gamgem.com/counter-strike-2/strategy/dust2  
  → 2Long+Mid+Cat+Tunnel 默认；CT 2-1-2；局势时钟与 stop 条件。

- CSDB.gg — Dust 2 callouts & strategies（可开）  
  https://csdb.gg/maps/dust2/  
  → Mid/Long/Upper 决策点；plant 与 retake 路径意图。

- CS2Hype / csgo-guides 跳转页 — Dust2 guide（可开，内容为 CS2Hype 长文）  
  https://csgo-guides.com/maps/dust2  
  → T/CT 分段、经济敏感、B rush 时间窗（改写为 ttl/tempo）。

- Chilldebrand CS2 Guide — Dust II（可开）  
  https://chilldebrand.github.io/CS2-Guide/maps/dust2/  
  → offense/defense 要点；道具只取时机表，不抄线。

- Dignitas — Single/Double AWP CT setups on Dust 2（可开，旧文仍可读）  
  https://dignitas.gg/articles/single-and-double-awp-ct-side-set-ups-on-dust-2  
  → CT 站位变体与 A/B split 弱点。

- Dignitas — CT aggression notes（既有）  
  https://dignitas.gg/articles/when-and-how-to-play-aggressively-as-ct-on-dust2-inferno-and-mirage

- Profilerr — Dust 2 tips CT/T（可开）  
  https://profilerr.net/dust-2-tips-strategies-for-ct-side-and-t-side-how-to-get-better/  
  → eco stack / retake 常识改写。

区域名对齐仓库 `dust_2.tmj` 的 `site` 组子图层。

---

## ancient_ruins（公开 Ancient → 本图 site 名）

- Chilldebrand CS2 Guide — Ancient（可开）  
  https://chilldebrand.github.io/CS2-Guide/maps/ancient/index.html  
  → Mid/Donut 默认；A Main+Donut；B Cave/Ramp；道具时机表。

- BoostRoom — Ancient defaults & Mid control（可开）  
  https://boostroom.com/blog/ancient-guide-cs2-best-defaults-how-to-take-mid  
  → Mid→Donut→A split；Cave→B；CT 2-1-2 与假转。

- CSDB.gg — Ancient callouts & strategies（可开）  
  https://csdb.gg/maps/ancient/  
  → Mid 决定回合；Ramp rush vs House split；retake 几何。

- CSBE Pro — Ancient callouts/timings（可开）  
  https://csbepro.com/counter-strike-2/ancient/  
  → 跑图时间与「干冲 Long/Mid」反例，改写入勿用条件。

区域名只用本仓库 `ancient_ruins.tmj` site 列表（Elbow/Donut/Cave/Ramp/Temple 等）。

---

## dust_1（de_dust 草案）

- Capdisk — Dust callouts & map guide（可开）  
  https://capdisk.com/csgo/maps/dust  
  → 比 dust2 紧；Mid 定全局；T split A 或 rush tunnels B；CT 叠点留 Mid。

说明: CS2 未正式带 de_dust；本图 tmj 空壳，区域名全部待建 site。

---

## cs_office（人质图草案）

- Chilldebrand CS2 Guide — Office（可开，推荐）  
  https://chilldebrand.github.io/CS2-Guide/maps/office/index.html  
  → 进攻清房+护送；防守交叉拖延；救人走廊为胜负轴。

- Clash.gg Blog — Office callouts（可开）  
  https://blog.clash.gg/callouts/office  
  → Front/Main/Garage/Paper/Kitchen 等称呼表。

- BloodyCase — CS2 Office callouts full guide（可开）  
  https://bloodycase.com/blog/cs2-office-callouts-full-guide/  
  → 人质图角色；T 守 Paper/Kitchen/Projector；CT 推 Front/Main/Garage。

- SkinLords — CS2 Office guide（**失效**：301 跳转到站首页赌博列表，勿用）  
  https://skinlords.com/blog/cs2-office-guide-strategies-to-master/  
  → 已改用 chilldebrand + bloodycase + clash.gg。

- Profilerr — Office callouts（检索可见，作 callout 备份）  
  https://profilerr.net/cs-office-callouts/

说明: 引擎无人质 intent；已用 A≈人质区、B/MID≈车库/走廊 的 hack 映射并写入 UNCERTAINTIES。

---

## mirage（公开 Mirage → 本图 site 名）

- GamGem — Mirage strats: T default, CT setup, executes（可开）  
  https://gamgem.com/counter-strike-2/strategy/mirage  
  → Mid 默认(B_Apts+Top_Mid+Underpass+Ramp)；CT 2-1-2；Mid 决策树与 A/B execute 时机改写。

- CSDB.gg — Mirage callouts & strategies（可开）  
  https://csdb.gg/maps/mirage/  
  → Mid 定回合；A=Ramp/Palace 同步；B=Apps+Market 隔离；retake 走 CT/Jungle 或 Market。

- Chilldebrand CS2 Guide — Mirage（可开）  
  https://chilldebrand.github.io/CS2-Guide/maps/mirage/  
  → offense/defense 要点；道具只取时机表（Window/Jungle/Market），不抄线。

- BoostRoom — Mirage smokes 时机意图摘要（可开；线细节未写入 playbook）  
  https://boostroom.com/blog/mirage-smokes-guide-cs2-20-essential-lineups-for-every-side  
  → Window/Ticket/Stairs/Market 组合用途，压缩为「进点前挡视线」。

- CSGO-Guides / CS2Hype 跳转页 — Mirage guide（可开，内容为 CS2Hype 长文）  
  https://csgo-guides.com/maps/mirage  
  → T/CT 分段、经济敏感、B rush 时间窗（改写为 ttl/tempo）。

- bo3.gg — Mirage advanced strategies（检索可开，作 CT setup 备份）  
  https://bo3.gg/articles/guide-to-mirage  
  → CT 锚点与旋转纪律；与 GamGem 2-1-2 互证。

区域名对齐仓库 `mirage.tmj` 的 `site` 组子图层（含 Marcket/Lader_Room/Bulcony 等拼写）。

---

## 改写原则（各图共用）

1. intent 闭集：default / rush_a / rush_b / split_a / split_b / stack_a / stack_b / retake_a / retake_b / save / fake  
2. 不写像素投掷线；只写何时挡视线/闪进点/拖延  
3. slots.area 必须是该图 site 层名（dust1/office 为草案名并标待确认）  
4. 禁止大段粘贴原文；局势表与 few-shot 为引擎可调用短条目

---

## 经济把控（附录）

阈值与四档+反eco 文案改写自公开 IGL/经济指南（详见仓库外 `playbook-research/ECONOMY_BRIEF.md` / `ECONOMY_SOURCES.md`）。  
引擎差异：`EconomySystem` 胜利时连败清零（非 CS2 胜后降一档）；digest 已输出人均$/连败/有枪/敌经济/购买档。

