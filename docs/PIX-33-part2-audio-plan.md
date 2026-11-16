# 玩家体验第二部分修复实施计划（音频 G21–G26）

> 来源：2026-11-13 玩家体验审计报告 `PixelEngine-玩家体验槽点汇总.md`（81 项，基准 origin/main 98f114f）第 2 节音频 + Sun 2026-11-14 17:53（CST）对第二部分的逐项决定。
> 基准：音频现状按 2026-11-14 17:54 核实（scratch clone 97617f6）+ 2026-11-15 22:30 缺口审计（Sun 电脑工作树，88 个文件）更新：音频共 88 个文件；`tools/placeholders.json` 登记 11 个合成蜂鸣占位；切枪音的调用点已存在（CS2DGame.cpp:935 玩家、CS2DGameBots.cpp:907 BOT，2026-11-15 核实），只缺按武器的音效文件。
> 状态：**已发布 Linear PIX-33（https://linear.app/pixel-engine/issue/PIX-33/玩家体验第二部分修复实施计划音频-g21-g26-c4-拆包钳），待 Codex 实施**。G26 / G25 / G24 的文件均已生成（附录 G 待 Codex 接线）；G22 的素材问题已解决，下包/拆包声、pull_pin、switch_awp、OBSTACLE_BREAK、拆包钳机制（§10）、C4 贴图（§11）均已决；全部已决，无未决事项（C4 爆炸 2026-11-16 Sun 定：改用 `bomb_explosion.wav`，见附录 G §7 末尾）。Linear 上的 issue 描述已按 Sun 要求精简为任务清单（只写 Codex 要做的事），完整版在仓库 `docs/PIX-33-part2-audio-plan.md`。

## 决定总览（G21–G26）

| 编号 | Sun 的决定（2026-11-14 17:53） | 本计划章节 |
|---|---|---|
| G21 | 音效范围保持现状不改动，只改注释及说明文档 | A |
| G22 | 全部音频重新整理，稍后统一讨论 | B（待定） |
| G23 | 保持现有状态，不做改动 | — |
| G24 | 捡枪时补上音效，**已定：文件已生成，待 Codex 接线**（见附录 G §6） | C（已定稿，待 Codex 接线） |
| G25 | 添加阵亡音效 | D |
| G26 | 换弹音区分，**已定：每把枪一轨**（2026-11-15，25 个文件已生成，见附录 G §1） | E（已定稿，待 Codex 接线） |

Sun 对本阶段的定性：**两个问题——音效内容，音效播放时机**。本计划按这两条线写；G22 / G24 的待定项留空，等统一讨论时填；G26 已于 2026-11-15 定为每把枪一轨（见 E 节与附录 G）。**补充（Sun 2026-11-15 23:23）**：G22 / G24 说的就是素材问题，现在素材已解决，可以开始执行——G24 的捡枪音已生成（附录 G §6），G22 的换轨素材已齐（附录 F、G），只剩接线（附录 G §4 已决）。

## 两条线

### 内容线：ElevenLabs API 生成为主

**更新（2026-11-15）**：ElevenLabs API 已接通（`custom.elevenlabs`，免费档 10,000 积分），Phase 1（11 条枪声）+ Phase 2（64 条）共 75 条音效均由此生成，流程已跑通：精确英文提示词 → API 生成（0.5–3.5s）→ 裁到有效段 + 浮点域增益归一化峰值 -3 dBFS → 客观检查（单发/完整序列、峰值、不重复）→ 推入仓库。风格锚点是现成的真实枪声录音，新音效往那个风格上靠。

- **仍可用的备选**：程序合成（Python 写 WAV）适合蜂鸣、点击这类简单音；开源库现成素材（Freesound CC、Sonniss GDC CC0、Kenney CC0）适合 C4 爆炸、拆包机械声这类必须真实感的独立音效，拿回来再统一做响度对齐。
- **注意**：免费档 API 生成的是 MP3；换弹文件直接用 MP3（与现有 8 轨一致），其余转 WAV。Muse 听不见声音，生成后只做客观检查，最终由 Sun 按耳朵确认。

### 时机线：补调用点清单

现状核实（2026-11-14 17:54）：切枪、拾取、阵亡三处都是“有东西没接线”或“东西和线都没有”，见各节。时机线的验收标准是**进游戏实听**，不是只看代码接上了。

## A. G21 — 音效范围保持现状，只改注释和文档

- 审计原文：超过 100 世界单位的声音直接静音（`Audio/AudioManager.cpp:423-461 PlayChunkAt`，`MAX_RANGE = 100.0f`，`MIN_FACTOR = 0.0f`）；旁边的注释还写着“最远处仍保留 12% 音量”，代码却是 0。**更新（2026-11-15）**：当前工作树的 MAX_RANGE 是 200.0f，动手时以当前代码为准，只改注释不改数值。
- Sun 的决定：**范围不改**（审计建议的 MAX_RANGE 提到 400–600、MIN_FACTOR 改成 0.12 不采纳）。
- 改法：
1. 把 `PlayChunkAt` 附近那句误导性注释改成与代码一致的描述（最远处音量为 0，超出 MAX_RANGE 静音）。
2. 在说明文档（音频相关文档，如 `ASSET_SPEC.md` 或音频说明处，以仓库实际文件为准）补一句：当前 3D 衰减范围是刻意保持的现状，及 MAX_RANGE / MIN_FACTOR 的实际值。
- 不做：不改任何数值，不改衰减曲线。
- 验收：注释与代码一致；文档写清现状。

## B. G22 — 全部音频重新整理

- 审计原文：下包、拆包、回合胜负、脚步全是合成蜂鸣音；`tools/placeholders.json` 登记的 11 个占位：下包 / 拆包 / 炸弹哔声、回合开始 / 胜 / 负、脚步、护甲受击、闪光 / 烟雾、连 `fx_explosion` 本身也是占位；C4 爆炸还复用手雷爆炸音（`CS2DGameObjective.cpp:322` 直接播 `GRENADE_EXPLODE`）。
- Sun 的决定：全部音频重新整理。**补充（Sun 2026-11-15 23:23）**：G22 说的就是素材问题——现在素材已解决：11 个占位 Sun 自己找完了；Phase 1（11 条重复枪声）+ Phase 2（64 条换弹/切枪/枪声/阵亡音）+ 捡枪音（附录 G §6）全部生成并推入 `assets/audio/`，新文件统一 44.1kHz 立体声、峰值 -3 dBFS，命名沿用现有约定（`reload_<武器>.mp3`、`switch_<武器>.wav`、`sfx_<作用>.wav`）。
- 还剩的接线工作（Codex，见附录 G）：换弹/切枪按武器查表（§1、§2）、阵亡音调用点（§3）、捡枪音调用点（§6）、下包/拆包声（§7）、pull_pin 与 switch_awp（§8）、拆包钳机制（§10）、C4 贴图接线（§11）、G21 注释（§5）。
- 已决（Sun 2026-11-15 23:28）：下包/拆包过程音按下开始、松开停止（§7）；pull_pin / switch_awp 按字面意思接线（§8）；OBSTACLE_BREAK 已删除（§9，Muse 已做并编译通过）。
- 不做：不改已确认的真实录音（除已替换的 16 轨外）；不改文件名改到代码引用对不上。

## C. G24 — 捡枪补音效（已定稿，待 Codex 接线）

- 审计原文：捡枪、捡拆弹器静音；`SoundEffect::PICKUP` / `OBSTACLE_BREAK` 只在 `Audio/AudioManager.hpp:23,27` 的枚举里存在，`ChunkFor`（`AudioManager.cpp:328-368`）没有它们的 case（走 `default: return nullptr`），且全仓库没有任何调用点。
- Sun 的决定（2026-11-15 23:23）：G24 就是素材问题，素材已解决，可以开始执行。
- 音效文件已生成：`sfx_pickup.wav`（捡枪时金属咔哒 + 布料摩擦，0.304s，峰值 -3 dBFS，2026-11-15，见附录 G §6）。
- 改法（Codex，见附录 G §6）：`SoundEffect::PICKUP` 已有枚举值，只需 `loadChunk` + `ChunkFor` 加 case + 在拾取成功处加调用点——调用点已定位：`CS2DGameCombat.cpp:1116` `TryPickupDrops`，在 `if (taken) d.id = WeaponId::None;` 处加 `audio->PlaySound(SoundEffect::PICKUP)`（玩家拾取；BOT 拾取如有单独逻辑，另议）。
- `OBSTACLE_BREAK`：已删除（Sun 2026-11-15 23:28 决定，Muse 已删枚举成员并编译通过，见附录 G §9）。
- 验收：进游戏捡枪能听到拾取音；`build-release` 编译通过。

## D. G25 — 添加阵亡音效

- 审计原文：玩家阵亡没有任何专属音效；玩家被击杀只走 `OnCombatantKilled`（`CS2DGameCombat.cpp:791-806`，无播音调用），死亡前最后一声是中弹的 `HIT` / `ARMOR_HIT`；`KILL` 音只在玩家击杀 BOT 时播（`CS2DGameCombat.cpp:376,873`）。
- Sun 的决定：**添加音效**（本部分里唯一一项已拍板的新增内容）。
- 改法：
1. 音效文件已生成：`sfx_death.wav`（倒地闷响 + 一声轻呼气，无人声，0.54s，2026-11-15，见附录 G §3）。音色是 Muse 猜的设计，Sun 尚未按耳朵确认；若他不喜欢，换文件内容即可，调用点不变。
2. 在 `OnCombatantKilled` 的玩家阵亡分支加播放调用（行号按当前工作树核实，2026-11-14 记的行号已过时）；与 G53 死亡画面（1.5–2s 延迟切观战）的时间配合在实施时核对——音效播在死亡瞬间，不等延迟。
3. 注意：手雷、火区、C4 炸死的阵亡音与枪杀一致播一轨即可，不另做区分（与 G53 的兜底文案逻辑分开，音效不分死因）。
- 验收：进游戏被 BOT 击杀能听到阵亡音，且与击杀 BOT 时的 `sfx_kill` 明显不同。

## E. G26 — 换弹音区分（已定稿，待 Codex 接线）

- 审计原文：换弹只有 8 轨复用，6 把 SMG 全用 `reload_galil.mp3`（`ReloadSoundFor` 在 `CS2DGameCombat.cpp:969`，2026-11-15 核实）。
- Sun 的决定（2026-11-15 22:57）：**每把枪一轨，全部一次性生成**。25 个新文件已生成并推入 `assets/audio/`（`reload_<武器>.mp3`，时长≈对应武器 `reloadTime`，见附录 G §1 完整表）。
- 改法（Codex）：`SoundEffect` 新增 25 个枚举成员 + `loadChunk` + `ChunkFor` case；`ReloadSoundFor` 每个 `WeaponId` 返回自己的成员（建议改成按武器查表，仿照 `kWeaponFireAssets`）；不动换弹时长与 gameplay 逻辑。
- 验收：进游戏每类武器（手枪 / SMG / 步枪 / 狙 / 霰弹 / 机枪）各换弹一次，能听出区别；`build-release` 编译通过。

## 明确不做

- G23：对局无 BGM 是设计使然（`Engine.cpp:1201` `StartMatchWithMap` 里 `StopMusic()`，回主菜单才续播；3 首 BGM 只在菜单响），保持现有状态，不做改动。
- G21：不改 MAX_RANGE / MIN_FACTOR 数值。
- G22 / G24：在统一讨论之前不先换轨、不先写合成脚本、不先定拾取音。G26 已定稿，不在此列。

## 建议顺序

1. **附录 G 接线（G26 + 切枪 + G25 + G24）先做**：文件已全部就位，Codex 只做映射与调用点改动；另附 G21 注释小改（见附录 G §5）。
2. **A（G21）**：纯注释 + 文档，零风险，可与接线同一批提交。
3. **C4 爆炸（已决，Sun 2026-11-16）**：改用 `bomb_explosion.wav`——`DetonateBomb` 改播新音（见附录 G §7 末尾）；与接线同一批做。
4. G23 不做。

## 验证计划

- 每项改完在 Windows 上 `build-release` 编译通过，进游戏实听验收（A 除外，A 只核对注释与文档）。
- 时机线验收清单：切枪音（见下）、拾取音、阵亡音、换弹区分——进游戏逐项听一遍。
- 切枪音效：2026-11-15 已核实，调用点**已存在**（CS2DGame.cpp:935 玩家、CS2DGameBots.cpp:907 BOT），2026-11-14“全仓库无调用点”的结论已过时；缺的是按武器的音效文件——33 个新文件已生成（附录 G §2），Codex 只需把 `WeaponSwitchChunk` 改成按武器查表，无需新增调用点。

## 未决事项（已全部解决，无剩余）

- C4 爆炸是否改用 `bomb_explosion.wav`——**已决（Sun 2026-11-16）：改用**（附录 G §7 末尾）。
- 切枪音接线：已并入附录 G（按武器查表），无需单独立项。
- `pull_pin.wav` / `switch_awp.wav` 的去留、bomb_plant/defuse 的接线方式——**已决（Sun 2026-11-15 23:28）**（附录 G §7、§8）。
- 是否把本计划发到 Linear——**已发为 PIX-33（2026-11-15 23:53），本项无意义，删除**。另：Sun 2026-11-16 要求 Linear 上的 issue 描述精简为任务清单（只写 Codex 要做的事），完整版放仓库 `docs/PIX-33-part2-audio-plan.md`。

## 附录：11 个占位音频的 ElevenLabs 提示词（2026-11-14）

来源：`tools/placeholders.json` 的 `generated` 清单里 11 个音频占位（其余是图片占位，G2 批量在换）。提示词用英文（ElevenLabs 对英文描述最准），每条写明游戏内触发点和建议时长；一次生成给 4 个变体，挑最像的那个。注意：免费档只能下 MP3 且不可商用；要 WAV（48kHz）且能放进发布的游戏需 Starter（$6/月）起。脚步声建议多生成 3–4 个变体轮换播，避免单轨重复听着像打字机。

### bomb_beep.wav — C4 倒计时蜂鸣
触发：安包过程中每 0.75 秒一次（`CS2DGameObjective.cpp:91`）；安放后倒计时加速：>10 秒时每 1.0 秒、>4 秒时每 0.5 秒、最后 4 秒每 0.25 秒（`CS2DGameObjective.cpp:139-153`）。因此成品必须是单声短蜂鸣、短于 0.25 秒，原占位仅 0.06 秒。API 最短时长 0.5 秒，生成后需剪去尾部静音、保留约 0.15 秒单声。已验证（2026-11-15 试跑）：s2 单声约 0.14 秒，剪至 0.16 秒 WAV（峰值 -4.8 dB），已进仓库。
```
Single short high-pitched electronic beep, like a bomb timer warning beep, clean and sharp, no echo, no background noise
```

### bomb_defuse.wav — 拆包完成确认
触发：拆包成功时播一次（`CS2DGameObjective.cpp:225`）。建议时长 2 秒。
```
Short confirmation beeps from a bomb defusal kit, two crisp electronic beeps followed by a small mechanical click, precise and clean, no music
```

### bomb_plant.wav — 下包安放
触发：下包成功时播一次（`CS2DGameObjective.cpp:296`）。建议时长 2 秒。
```
Rapid electronic arming beeps as a bomb is planted, three quick beeps with a metallic clack at the end, tense, dry, no echo
```

### fx_explosion.wav — 爆炸（手雷 + C4 共用）
触发：手雷爆炸；C4 爆炸目前也复用这一轨（`CS2DGameObjective.cpp:322`）。建议时长 3 秒。
```
Large powerful explosion, deep booming blast with crumbling debris and a low rolling rumble tail, cinematic and heavy, no music, no voices
```

### fx_flash.mp3 — 闪光弹
触发：闪光弹引爆（`CS2DGameCombat.cpp:671`）。建议时长 2 秒。
```
Deafening flashbang pop followed by a high-pitched ringing tone fading out, sharp and disorienting, no music
```

### fx_smoke.mp3 — 烟雾弹
触发：烟雾弹释放（`CS2DGameCombat.cpp:721`）。建议时长 2 秒。
```
Pressurized gas hissing as a smoke grenade releases thick smoke, steady white-noise hiss with a soft canister pop at the start, no music
```

### round_lose.wav — 回合失败
触发：回合结束、玩家方落败（`CS2DGameRound.cpp:468`）。建议时长 2 秒。
```
Short low descending brass sting for a lost round, somber two-note fall, clean, no music bed, no voices
```

### round_start.wav — 回合开始
触发：回合开始（`CS2DGameRound.cpp:79`）。建议时长 1 秒。
```
Short bright two-note chime signaling the start of a round, crisp and attention-grabbing, like a referee whistle chime, no music
```

### round_win.wav — 回合胜利
触发：回合结束、玩家方获胜（`CS2DGameRound.cpp:468`）。建议时长 2 秒。
```
Short upbeat victory sting, bright major-key two-note chime with a light shimmer, triumphant but compact, no full music, no voices
```

### sfx_armor_hit.wav — 子弹命中现代防弹衣
触发：玩家穿防弹衣中弹（`CS2DGameCombat.cpp:406`）。注意：是现代防弹背心（凯夫拉 + 陶瓷插板），不是金属护甲——声音是闷重的一声闷响 + 插板短促的碎裂感，不是金属脆响。建议时长 1 秒。
```
Dull heavy thud of a bullet striking a modern bulletproof vest, dense Kevlar fabric absorbing the impact with a short low crack of the ceramic plate, muted and weighty, dry, no echo, no metallic ring
```

### sfx_footstep.mp3 — 脚步声
触发：玩家和 BOT 移动时每 0.32–0.34 秒播一次（`CS2DGame.cpp:950-1051`）。单步即可，游戏按步频重复。建议时长 1 秒，多生成 3–4 个变体轮换。
```
Single crisp footstep on hard concrete floor, short shoe scuff with a soft thud, dry and close, no echo, no background noise
```

### bomb_planting.wav（新增）— 下包过程音，含输密码
下包要在包点站满 3.0 秒才完成（`CS2DGameObjective.cpp:93`，`plantProgress_ >= 3.0f`），所以这段按 3 秒做，一次播完正好盖住整个过程；与 `bomb_plant.wav`（下包成功时播一次的确认音）是两轨，不要混用。
```
Sequence of electronic keypad beeps as a bomb code is entered, distinct button presses one after another, then a mechanical arming clack and a low confirming buzz at the end, tense and precise, dry, no music, no voices
```

### bomb_defusing_kit.wav（新增）— 拆包过程音，有拆包钳子，4 秒
### bomb_defusing_nokit.wav（新增）— 拆包过程音，无拆包钳子，10 秒
注意：代码现状是拆包固定 `DEFUSE_TIME = 5.0f`（`CS2DGameObjective.cpp:14`），且全仓库没有“拆包钳子”这个物品或持有状态（只有一张 `defuse_kit.png` 占位图）。4 秒/10 秒的区分要落地，需先有钳子机制（能捡/能买 + 持有判断）+ 按持有分支拆包时间，否则两段音效没地方挂；G24 的捡钳子音效是同一件事的前置。

有钳子（4 秒）：
```
Professional bomb disposal kit at work, crisp wire snips and precise tool clicks in quick succession, steady electronic beeps, efficient and confident, ending with a soft disarm click as the device powers down, dry, no music, no voices
```

无钳子（10 秒）：
```
Improvised bomb defusal by hand, slow careful work: fumbling with wires, tentative scraping and twisting sounds, long pauses, a nervous electronic beep now and then, tense and quiet, ending with a final wire snip and the device powering down, dry, no music, no voices
```

### voice_bomb_planted.wav / voice_bomb_defused.wav（新增）— CS 式对讲机语音播报
这两句是**人声播报**，不是音效：ElevenLabs 里走 Text-to-Speech（不是 Sound Effects），文本就是提示词本身；对讲机味道不在嗓音模型里，在后处理——带通 300–3400Hz + 轻失真/压缩 + 首尾各一个 squelch 咔哒声（可用 ffmpeg/sox 或 Python 直接套，Muse 可代做）。游戏里目前下包/拆包成功只播 `BOMB_PLANT`/`BOMB_DEFUSE` 蜂鸣，这两句是新增播报轨，可与蜂鸣叠播。选音：ElevenLabs Voice Library 里搜 military radio / radio announcer 类男声，沉稳冷静款。

下包成功：
```
Bomb has been planted.
```
（语气：冷静、权威、军用电台口令节奏，语速平稳不拖）

拆包成功：
```
Bomb has been defused.
```
（同一把嗓音，语气稍松、收尾下沉）

---

## 附录 F：Phase 1 枪声替换（已完成 2026-11-15）

**背景**：音频缺口审计发现 9 组重复枪声（20 把武器），11 个文件需要替换。只换文件内容，文件名与映射不变，零代码改动。

**提示词模板**（每条 0.5s，1 个版本；三要素：单发 + 干声 + 武器性格）：

| 文件 | 武器（保留的对照） | 提示词 |
|---|---|---|
| fire_negev.wav | Negev（AK47） | Single light machine gun shot, deep heavy booming report with a long low rumble and sustained power, 5.56mm LMG, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_aug.wav | AUG（MAC-10） | Single bullpup assault rifle gunshot, sharp punchy crack with a tight mid-range body and clean attack, 5.56mm rifle, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_famas.wav | FAMAS（M4A4） | Single bullpup assault rifle gunshot, high-pitched snappy crack with a quick metallic ring and fast decay, distinctive French FAMAS rifle tone, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_g3sg1.wav | G3SG1（M249） | Single automatic sniper rifle gunshot, deep powerful boom with a sharp heavy crack and long low tail, 7.62mm precision rifle, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_xm1014.wav | XM1014（Nova） | Single semi-automatic shotgun blast, heavy close-range boom with a deep thump and short sharp crack, 12 gauge, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_sawedoff.wav | Sawed-Off（Nova） | Single short-barrel shotgun blast, massive raw booming bang with a wide low-end thump and explosive attack, sawn-off barrel, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_mag7.wav | MAG-7（Nova） | Single compact pump shotgun blast, sharp punchy bang with a tight crack and quick decay, small close-quarters shotgun, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_mp9.wav | MP9（MP7） | Single compact submachine gun shot, light snappy pop with a short crisp crack, 9mm SMG, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_p90.wav | P90（P2000） | Single submachine gun shot, sharp high-pitched crack with a quick buzzing tail and fast attack, futuristic compact P90 SMG, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_scar20.wav | SCAR-20（SG553） | Single designated marksman rifle gunshot, heavy precise crack with a deep resonant boom and clean decay, 7.62mm DMR, one shot only, dry recording, no echo, no reverb, no background noise |
| fire_ump45.wav | UMP-45（USP-S） | Single submachine gun shot, deep heavy dull thump with a low booming body, large-caliber .45 ACP SMG, one shot only, dry recording, no echo, no reverb, no background noise |

**后处理**：裁到单发 burst（前后留 10/20ms）+ 归一化峰值 -3 dBFS。**验收**：单发（无内部静音间隔）、峰值 -3 dB、无削波、与全部现有枪声最大互相关 0.02–0.34、与保留对照 < 0.04。

**生成记录**：首轮 7/11 通过；4 条（AUG/FAMAS/XM1014/MAG7）首版峰值超满刻度，重生成一次通过；MAG-7 共 3 次尝试（削波、双发、削波），第 3 版用浮点域增益修平后通过。教训：峰值超 1.0 不等于波形削顶（先看峰顶是否平坦），单声道混音测得的峰值比立体声低约 3 dB，以 volumedetect 为准。

**落地**：2026-11-15 22:53 全部 11 个文件推入 `assets/audio/`，原文件备份至 `D:\PixelEngineBuild-Archive\audio-20261115`，SHA256 校验 BAD=0。Sun 尚未按耳朵确认。

**后续**：Phase 2（25 条换弹 + 33 条切枪 + 5 条削波枪声 + 1 条阵亡音）已于 2026-11-15 完成生成并推入仓库，见附录 G。

---

## 附录 G：Phase 2 接线交接（给本地 Codex，2026-11-15）

**已完成（Muse）**：65 个音效文件已全部生成并推入 `assets/audio/`（SHA256 校验全过）。其中 5 条是替换（零代码改动，已生效）；60 条是新增文件，**尚未接线，游戏里还听不到**，需要下面的代码改动。

**已替换（直接生效，无需改代码）**：fire_ssg08.wav、fire_m4a4.wav、fire_dualberettas.wav、fire_ak47.wav、fire_m249.wav（原文件备份在 `D:\PixelEngineBuild-Archive\audio-20261115`）。

### 1. 换弹：`ReloadSoundFor` 改为按武器查表（CS2DGameCombat.cpp:969）

现状：8 个枚举值服务 33 把武器。新增 25 个文件（均为 `.mp3`，时长≈对应武器的 `reloadTime`，已按 `WeaponDef.hpp` 核对）：

| 武器 | 新文件 | 替换自 |
|---|---|---|
| P250 | reload_p250.mp3 | RELOAD_GLOCK |
| Five-SeveN | reload_fn57.mp3 | RELOAD_GLOCK |
| Tec-9 | reload_tec9.mp3 | RELOAD_GLOCK |
| CZ75-Auto | reload_cz75.mp3 | RELOAD_GLOCK |
| Dual Berettas | reload_dualberettas.mp3 | RELOAD_GLOCK |
| P2000 | reload_p2000.mp3 | RELOAD_USP |
| R8 Revolver | reload_r8.mp3 | RELOAD_DEAGLE |
| MAC-10 | reload_mac10.mp3 | RELOAD_GALIL |
| MP9 | reload_mp9.mp3 | RELOAD_GALIL |
| MP7 | reload_mp7.mp3 | RELOAD_GALIL |
| MP5-SD | reload_mp5sd.mp3 | RELOAD_GALIL |
| UMP-45 | reload_ump45.mp3 | RELOAD_GALIL |
| P90 | reload_p90.mp3 | RELOAD_GALIL |
| SG 553 | reload_sg553.mp3 | RELOAD_AK47 |
| M249 | reload_m249.mp3 | RELOAD_AK47 |
| Negev | reload_negev.mp3 | RELOAD_AK47 |
| M4A4 | reload_m4a4.mp3 | RELOAD_M4A1 |
| AUG | reload_aug.mp3 | RELOAD_M4A1 |
| FAMAS | reload_famas.mp3 | RELOAD_GALIL |
| SSG 08 | reload_ssg08.mp3 | RELOAD_AWP |
| SCAR-20 | reload_scar20.mp3 | RELOAD_AWP |
| G3SG1 | reload_g3sg1.mp3 | RELOAD_AWP |
| XM1014 | reload_xm1014.mp3 | RELOAD_SHOTGUN |
| Sawed-Off | reload_sawedoff.mp3 | RELOAD_SHOTGUN |
| MAG-7 | reload_mag7.mp3 | RELOAD_SHOTGUN |

改动：`SoundEffect` 新增 25 个枚举成员 + `AudioManager::Initialize` 里 `loadChunk` + `ChunkFor` 里 case；`ReloadSoundFor` 每个 `WeaponId` 返回自己的成员。建议顺手改成按武器查表（仿照 `kWeaponFireAssets`），而不是继续加 case。保留的 8 个旧文件仍由 Glock18 / USP-S / Deagle / Galil AR / AK-47 / M4A1-S / AWP / Nova 使用。

### 2. 切枪：`WeaponSwitchChunk` 改为按武器查表（AudioManager.cpp:70）

现状：`kWeaponSwitchAssets` 按 `WeaponClass` 查表，9 个文件服务 42 把武器。新增 33 个文件（均为 `.wav`）：

| 武器 | 新文件 | 武器 | 新文件 |
|---|---|---|---|
| USP-S | switch_usp.wav | M4A1-S | switch_m4a1.wav |
| P2000 | switch_p2000.wav | SG 553 | switch_sg553.wav |
| Dual Berettas | switch_dualberettas.wav | AUG | switch_aug.wav |
| P250 | switch_p250.wav | SSG 08 | switch_ssg08.wav |
| Five-SeveN | switch_fn57.wav | SCAR-20 | switch_scar20.wav |
| Tec-9 | switch_tec9.wav | G3SG1 | switch_g3sg1.wav |
| CZ75-Auto | switch_cz75.wav | XM1014 | switch_xm1014.wav |
| Desert Eagle | switch_deagle.wav | Sawed-Off | switch_sawedoff.wav |
| R8 Revolver | switch_r8.wav | MAG-7 | switch_mag7.wav |
| Zeus x27 | switch_zeus.wav | Negev | switch_negev.wav |
| MAC-10 | switch_mac10.wav | 闪光弹 | switch_grenade_flash.wav |
| MP9 | switch_mp9.wav | 烟雾弹 | switch_grenade_smoke.wav |
| MP7 | switch_mp7.wav | 燃烧瓶 | switch_grenade_molotov.wav |
| UMP-45 | switch_ump45.wav | 燃烧弹 | switch_grenade_incendiary.wav |
| P90 | switch_p90.wav | 诱饵弹 | switch_grenade_decoy.wav |
| Galil AR | switch_galil.wav | | |
| FAMAS | switch_famas.wav | | |
| M4A4 | switch_m4a4.wav | | |

改动：`WeaponSwitchChunk` 从按 `WeaponClass` 改为按 `WeaponId` 查表（新增 33 条映射 + AWP 单独一条，见 §8）。9 个旧文件保留并分配给：匕首（switch_knife.wav）、Glock-18（switch_pistol.wav）、MP5-SD（switch_smg.wav）、AK-47（switch_rifle.wav）、AWP（switch_awp.wav，见 §8）、Nova（switch_shotgun.wav）、M249（switch_mg.wav）、高爆雷（switch_grenade.wav）、C4（switch_c4.wav）。`switch_sniper.wav` 不再分给任何武器，保留作类别兜底。调用点已存在（CS2DGame.cpp:935 玩家、CS2DGameBots.cpp:907 BOT），无需新增。

### 3. 阵亡音：新增 `sfx_death.wav`（G25）

文件已生成（倒地闷响 + 一声轻呼气，无人声，0.54s）。需新增：`SoundEffect` 成员 + `loadChunk` + `ChunkFor` case + 在玩家阵亡分支调用一次（CS2DGameCombat.cpp 玩家被击杀处，`OnCombatantKilled` 的调用点；G53 的 `lastDeathInfo_` 也计划在同一分支加，两处互不冲突，可同一批做）。**注意**：阵亡音的音色是 Muse 猜的设计（倒地闷响），Sun 尚未按耳朵确认；若他不喜欢，换文件内容即可，调用点不变。

### 4. 已决事项（Sun 2026-11-15 23:28，见 §7–§9）

- 下包/拆包过程音：按下开始播放，中途松开停止播放（§7）。
- `pull_pin.wav` / `switch_awp.wav`：按字面意思接线（§8）。
- `OBSTACLE_BREAK`：已删除（§9，Muse 已做并编译通过）。
- 全部已决，无未决事项（C4 爆炸 2026-11-16 Sun 定：改用 `bomb_explosion.wav`，见 §7 末尾）。

### 5. G21 注释小改（可与接线同一批做）

- `AudioManager.cpp` 的 `PlayChunkAt` 附近：`MIN_FACTOR = 0.0f; // 最远处仍保留 12% 音量` 这句注释与代码矛盾（代码是 0），改成与代码一致的描述（最远处音量为 0，超出 MAX_RANGE 静音）。
- 在音频说明文档（`tools/ASSET_SPEC.md` 或音频说明处，以仓库实际文件为准）补一句：当前 3D 衰减范围是刻意保持的现状。注意：当前工作树的 MAX_RANGE 是 200.0f（2026-11-15 核实），不是 97617f6 时的 100——只改注释和文档，**不改任何数值**。

### 6. 捡枪音：`sfx_pickup.wav` 接线（G24）

文件已生成并推入 `assets/audio/`（0.304s，峰值 -3 dBFS，SHA256 校验通过）。

改动（3 处，都小）：

1. `SoundEffect::PICKUP` 枚举值已存在（`AudioManager.hpp`），无需新增。
2. `AudioManager::Initialize` 里 `loadChunk("assets/audio/sfx_pickup.wav")` + `ChunkFor` 里给 `PICKUP` 加 case（目前走 `default: return nullptr`）。
3. 调用点（已定位）：`CS2DGameCombat.cpp:1116` `TryPickupDrops(bool manual)`——玩家拾取掉落物的唯一入口。在函数末尾 `if (taken) d.id = WeaponId::None;` 处加播放：
   ```cpp
   if (taken) { audio->PlaySound(SoundEffect::PICKUP); d.id = WeaponId::None; }
   ```
   注意：`TryPickupDrops` 只处理玩家拾取（开头 `if (!player_.alive) return;`）；BOT 拾取如有单独逻辑，不在本项内。手雷、主武器、副武器三类拾取都走这一个 `taken` 分支，一处调用全覆盖。

`OBSTACLE_BREAK` 不在本项内（是否接线或删枚举，等 Sun 定）。

### 7. 下包/拆包声（Sun 2026-11-15 23:28 已决）

**决定**：过程音**按下开始播放，中途松开停止播放**。即：玩家按住互动键（E）开始下包/拆包时播，松开键、离开包点、死亡或回合结束时停；下包/拆包成功时停过程音、播完成确认音。

**文件（Sun 已生成，均在 `assets/audio/`，已实测时长）**：

| 文件 | 时长 | 用途 |
|---|---|---|
| bomb_planting.wav | 3.000s | 下包过程音 |
| bomb_defusing_nopliers.wav | 10.005s | 拆包过程音（无钳子版） |
| bomb_defusing_pliers.wav | 4.000s | 拆包过程音（有钳子版）——**本次不用** |
| bomb_planted.mp3 | 1.724s | 下包完成确认音 |
| bomb_defused.mp3 | 1.802s | 拆包完成确认音 |

**为什么这样分**：玩家下包固定 3.0s（`CS2DGameObjective.cpp:93`），`bomb_planting.wav` 正好 3.000s，一次播完与下包同步；拆包时间由是否持有拆包钳决定（见 §10，Sun 2026-11-15 23:35 决定加钳子机制）：**有钳子 4.0s，配 `bomb_defusing_pliers.wav`（4.000s）；无钳子 10.0s，配 `bomb_defusing_nopliers.wav`（10.005s，播到 10s 时随拆包完成而停）**。`DEFUSE_TIME = 5.0f` 的固定值随机制改为按持有分支（§10）。

**改法（Codex）**：

1. **完成确认音（只改加载路径，调用点已存在）**：`AudioManager::Initialize` 里 `BOMB_PLANT` 的 loadChunk 路径改成 `assets/audio/bomb_planted.mp3`，`BOMB_DEFUSE` 改成 `assets/audio/bomb_defused.mp3`。调用点不用动（`PlantBomb` 末尾播 `BOMB_PLANT`；拆包成功处播 `BOMB_DEFUSE`）。旧文件 `bomb_plant.wav` / `bomb_defuse.wav` 已删除，枚举成员名不变。
2. **过程音（需新增停止能力）**：`AudioManager` 目前**没有停单个音效的接口**（`PlaySound` 返回 void，`Mix_PlayChannel` 的通道号被丢弃；只有 `StopMusic()`，且它会停掉所有通道，不能用）。Codex 需加一个最小接口，例如 `int PlaySoundTracked(SoundEffect)` 返回通道号 + `void StopChannel(int)`，并在 `CS2DGame` 里记两个通道号（`plantSoundChan_` / `defuseSoundChan_`，初始 -1）。
3. **下包过程音**：在 `UpdateObjective` 的玩家下包分支（`inSite && interactHeld`，`CS2DGameObjective.cpp:86`）——`plantProgress_` 从 0 变 >0 的第一帧起播 `bomb_planting.wav`（新枚举成员 + load + case，命名仿照现有 `BOMB_*`）；走到 `else` 分支（松开/离开，进度清零）或 `PlantBomb` 被调用时停掉该通道。BOT 携带者下包（2.0s，`PLANT_TIME`）本次不播过程音（玩家按住键的才播）。
4. **拆包过程音**：在 `UpdateObjective` 的拆包段——`defusing` 第一次变 true 时起播（按拆包者是否持钳选文件：持钳 `bomb_defusing_pliers.wav`，无钳 `bomb_defusing_nopliers.wav`），`defusing` 变 false（松开/离开/被打断，`defuseProgress_` 清零处）或拆包成功时停掉该通道。玩家拆包用 2D 播，BOT 拆包（`bestBot`）用 `PlaySoundAt` 在炸弹位置播——同一启停逻辑，只差播放方式。
5. **兜底**：回合结束或玩家死亡时，若任一过程音通道还在播，一并停掉（`UpdateObjective` 开头 `if (!round_.IsLive()) return;` 会直接返回，声音不会自己停）。

**C4 爆炸（已决，Sun 2026-11-16：改用 `bomb_explosion.wav`）**：新增 `SoundEffect::BOMB_EXPLOSION` + `loadChunk("assets/audio/bomb_explosion.wav")` + `ChunkFor` case；`DetonateBomb` 里把 `PlaySound(SoundEffect::GRENADE_EXPLODE)` 改成 `PlaySound(SoundEffect::BOMB_EXPLOSION)`。手雷爆炸音本身不动（手雷仍用 `GRENADE_EXPLODE`）。

### 8. pull_pin 与 switch_awp（Sun 2026-11-15 23:28 已决：按字面意思接线）

**pull_pin.wav（投掷物拔插销声）**：文件已在 `assets/audio/`（0.4865s），无代码引用。改动：新增 `SoundEffect::PULL_PIN` + `loadChunk("assets/audio/pull_pin.wav")` + `ChunkFor` case；调用点：`ThrowGrenade` 开头（`CS2DGameCombat.cpp:455`），两个 early return 之后、创建手雷之前加一行——玩家用 `PlaySound`，BOT 用 `PlaySoundAt`（与函数末尾投掷出手音的 if/else 分法一致）。一处调用覆盖所有投掷物类型（HE/闪光/烟雾/燃烧瓶/燃烧弹/诱饵弹）。

**switch_awp.wav（切换到 AWP 的声音）**：文件已在 `assets/audio/`（0.4865s），无代码引用。改动：在 §2 的按武器切枪查表里，AWP 单独映射到 `switch_awp.wav`（不再用 `switch_sniper.wav`）；`switch_sniper.wav` 保留作类别兜底，不再分给任何武器。其余 3 把狙（SSG 08 / SCAR-20 / G3SG1）仍用各自的 `switch_<武器>.wav`，不变。

### 9. OBSTACLE_BREAK 删除（已完成，Muse 2026-11-15 23:28）

Sun 的决定：这是很早以前的版本，现在已不需要，删除相关内容。已做：全仓库 grep（`PixelEngine/` 下全部 `.cpp/.hpp/.h`）确认 `OBSTACLE_BREAK` 只在 `AudioManager.hpp:27` 的枚举里出现一处（无 case、无调用点、无文档引用）；已删掉该枚举成员，`build-release` 重新编译通过（`[100%] Built target EngineApp`）。Codex 无需再动。

### 10. 拆包钳机制（Sun 2026-11-15 23:43 已定：买枪菜单 $400，CT 专用）

**背景**：代码里至今没有拆包钳子机制（全仓库无 `DefuseKit` / `defuse_kit` 引用；`assets/cs2d/sprites/defuse_kit.png` 是个无人引用的占位图）。Sun 决定补上：钳子是 CT 的装备，持有者拆包更快。

**机制（Codex）**：

1. **持有状态**：给 `Player`（玩家 + BOT）加一个 `hasDefuseKit` 布尔（CT 专用；T 拿了也没用）。**买一次一直有**（照抄 CS：买了就留在身上，整局有效，死了也不掉，不按回合清零——注意护甲是另一回事：护甲跨回合保留、头盔每回合清零，钳子不跟他俩，跟 CS 的钳子走）。
2. **拆包时间分支**：`UpdateObjective` 的拆包段，`DEFUSE_TIME = 5.0f` 的固定值改为按拆包者是否持钳：**持钳 4.0s，无钳 10.0s**。`defuseProgress_ += dt / DEFUSE_TIME` 的分母随之取分支值（注意：玩家下包仍是 3.0s，不动；BOT 下包仍是 2.0s `PLANT_TIME`，不动）。
3. **过程音配对**（与 §7 联动）：持钳拆包播 `bomb_defusing_pliers.wav`（4.000s），无钳拆包播 `bomb_defusing_nopliers.wav`（10.005s）；启停逻辑同 §7（按下起播、松开/打断/成功时停）。
4. **HUD 提示**：拆包进度条旁（或玩家状态区）显示一个小钳子标记（用 `defuse_kit_d.png`），让玩家知道自己这次拆包是 4 秒还是 10 秒；买菜单里钳子那一项也要显示价格和“拆包 4 秒”的说明。
5. **v1 只做玩家**：BOT 拆包一律按无钳 10s（BOT 不买钳子）；若以后给 BOT 加，再议。

**获取方式（已定：A，买枪菜单 $400，CT 专用）**：买菜单已有护甲类（`BuyKind::Armor`，$650/$1000，`CS2DGame.cpp:247`）的先例，照抄它加一个钳子类目（新 `BuyKind`，价格 $400，限 CT 购买——T 买时返回 `BuyCode::InvalidItem`）；买菜单 UI 里加一项（图标 `defuse_kit_d.png`，说明“拆包 4 秒”）；买入时扣钱并置 `hasDefuseKit = true`，已持有时不可重复买。经济上 $400 对应开局 $800 / 击杀 +$300 / 回合胜 +$3250，第一回合买不起、第二回合起买得起，节奏合理。B（地图上捡）与 C（买+捡）不做。

**素材（已就绪，Muse 2026-11-15 23:35 已推到仓库）**：`assets/cs2d/sprites/defuse_kit.png`（俯视地面贴图，AI 生成：红柄剪钳 + 多功能钳 + 深色帆布包，已抠图）与 `defuse_kit_d.png`（买菜单/HUD 图标，AI 生成：帆布包 + 红柄剪钳，已抠图 256×128）。旧占位图已备份到 `D:\PixelEngineBuild-Archive\sprites-20261115`。

### 11. C4 贴图接线（素材已就绪，Codex 两行改动）

**现状**：C4 在 `WeaponDef` 里 `spritePath` / `iconPath` 都是 `nullptr`（`WeaponDef.hpp:288`），所以手持 C4 和掉落 C4 都没贴图可画；`assets/cs2d/sprites/c4.png` 是个 64×64 的粗糙占位图（红底白字“C4”），`defuse_kit.png` 用的也是同一张。

**素材（已就绪，Muse 2026-11-15 23:35 已推到仓库）**：

- `c4.png`（手持俯视）：**程序画的简洁版占位**，不是 AI 图——俯视 C4 的 AI 生图被生图工具拒了（同燃烧瓶侧视图），按燃烧瓶的先例先用程序版顶着：橄榄绿方块 + 计时器 + 红 LED + 红/黄/黑三根线（朝上，合俯视枪口朝上的约定），1024×1024 透明底。游戏内画出来很小，辨识度够；以后换 AI 版只需换文件。
- `c4_d.png`（掉落地面 / HUD 图标）：AI 生成：橄榄绿 C4 方块 + 计时器（灰屏 + 红 LED）+ 右侧红/黄/黑线，已抠图 256×128。

**改法（Codex）**：`WeaponDef.hpp:288` 的 C4 条目，把两个 `nullptr` 改成 `"cs2d/sprites/c4.png"` 与 `"cs2d/sprites/c4_d.png"`。其余不用动：渲染器已按 `spritePath`/`iconPath` 加载（`CS2DRenderer.cpp:226-227`），手持 C4 走 `DrawWeaponForCharacter`（槽位 4，`PrimaryDrawSize`），掉落 C4 走 `DrawDroppedWeapon`（优先用图标）。旧占位图已备份到 `D:\PixelEngineBuild-Archive\sprites-20261115`。

### 验收（Codex 做完后）

1. `build-release` 编译通过（`EngineApp`）。
2. 进游戏实听：手枪 / SMG / 步枪 / 狙 / 霰弹 / 机枪各换弹一次（能听出区别）；切枪 2–3 把（能听到按武器的切枪音，AWP 切枪是 `switch_awp.wav`）；被 BOT 击杀一次（能听到阵亡音，且与击杀音不同）；捡一把地上的枪（能听到拾取音）；扔一颗手雷（能听到拔插销声在先、出手声在后）；下包一次（按住 E 起播过程音，中途松开停，下包成功播确认音）；拆包一次（按住 E 起播过程音，松开停，拆包成功播确认音）。
3. 不提交、不推送：改动留在工作树，等 Sun 过目后他自己提交（与 G2 / G8 的规矩一致）。
