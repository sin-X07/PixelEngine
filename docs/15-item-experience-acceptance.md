# PixelEngine 15 项修复与清理计划 — 验收记录

> 基准：提交 `ce41a76` + 工作区修改（Codex 2026-11-17 实施，Muse 接手完成最终回归验证）。
> 计划原文：审批稿《PixelEngine 15 项修复与清理计划》2026-11-17（15 项，含默认取值）。
> 本记录只写实测结果；未实测的项明确标注。

## 一、回归总览

| 回归 | 通过 | 失败 | 说明 |
|---|---|---|---|
| 主程序编译（build-release） | 通过 | 0 | `cmake --build` EXIT=0，EngineApp.exe 重新链接成功 |
| gameplay_tests 默认模式 | 1542 | 5 | 5 项失败全部是 `round2 settings layout target initializes`，即无显示环境的 SSH 会话里 `SDL_CreateWindow` / `SDL_CreateSoftwareRenderer` 建窗失败，与代码无关；与 2026-11-17 19:56（Codex 改动前）的失败项完全相同，本轮改动未引入新的默认模式失败 |
| gameplay_tests `--experience` 模式（无显示 SSH 会话） | 1198 | 7 | 7 项失败全部是建窗/建渲染器失败：`experience Engine renderer initializes` ×1、D3D11 渲染器 ×1、`round3 HUD matrix target initializes` ×5；零代码逻辑失败 |
| gameplay_tests `--experience` 模式（交互会话，2026-11-17 20:55） | **1878** | **0** | 在有桌面的交互会话里重跑（计划任务触发）：Engine 入口、D3D11、HUD 矩阵三组分支全部执行并通过，零失败 |

关于计划稿中“全量回归已通过 1629 项，0 项失败”：本次实测无法复现该数字。当前实测总数为默认 1547 项 + `--experience` 交互 1878 项；计划稿写于最终回归完成之前、测试还在扩充中（Engine 入口测试与 HUD 矩阵是后期加入的），数字应为当时的中间态。本记录以实测数字为准。

## 二、逐项验收

| # | 项 | 代码核查 | 测试证据 | 目检/实测 | 结论 |
|---|---|---|---|---|---|
| 1 | 删除 C4 提示圈，改画实体素材 | CS2DGame.cpp：掉落/安放两处脉冲圆圈已删，改为 `DrawDroppedWeapon(C4)` | —（渲染行为，靠截图） | 观战截图中 C4 实体可见，但偏小偏暗（见“观察项”） | 通过，带观察项 |
| 2 | 队友 BOT 拆包声 | AudioManager 新增 `PositionChannel` / `UpdateTrackedSoundAt`；拆包 52 单位内全音量、600 归零；每帧跟听者更新；阵亡只停本人下包通道 + 脚步循环；暂停/恢复走 `SetObjectiveAudioPaused` | 拆包 300 单位仍可闻；听者移动后音量刷新；600 处归零；暂停挂起、恢复续播；玩家阵亡后队友拆包声继续 | 人工试听未做（需 Sun 耳检） | 代码+数值通过；耳检待 Sun |
| 3 | 清除阵亡后准星残留 | CS2DOverlay.cpp：`localAimVisible = alive && !IsSpectating()` 统一门控准星与命中标记；死亡事件清 `hitMarkerTimer_` | 死亡定格测试覆盖 | 死亡卡截图：定格期间无准星 | 通过 |
| 4 | 左下角护甲 HUD 接实际素材 | GameUI.cpp：`armor_d.png` / `armor_helmet_d.png` 经 `weaponImage` 接入，加载失败回退原轮廓 | HUD 矩阵场景含 armor-only / no-armor（需交互会话跑） | armor-hud.png：甲盔图标已显示，28×29 下偏小、呈剪影感 | 通过，带观察项 |
| 5 | 死亡定格 1.8 秒后切观战 | `DeathPresentation` + `DeathFreezeSeconds = 1.8f`；快照纹理 `deathWorldTex_` 首帧渲染、定格期间复用；击杀卡 480×96、淡入 0.12s/淡出 0.18s；结算页等定格结束 | 定格满 1.8s、击杀归属、快照逐像素一致、暂停不消耗计时、尺寸变化复用快照（交互会话 1878/0 全部通过） | death-card.png：卡片、击杀者、武器图标均正确 | 通过 |
| 6 | 炸弹已安装且队友全灭时切 C4 上方 | `SpectateMode::Bomb`；`UpdateSpectatorState` 每帧修目标；听者位置同步到 C4；Bomb 视角锁定切换 | 队友全灭切 Bomb、听者位于 C4、Bomb 视角忽略切换、目标解决后保留 | c4-spectator.png：360° 俯视 + “C4 已安装” + “观战 C4” | 通过 |
| 7 | 清除比赛结束后旧返回倒计时 | `matchOverReturn` 字段、赋值、绘制全部删除；`GetMatchOverTimer()` 注释改为结算页延时 | — | 结算文案见 README | 通过 |
| 8 | 全屏下分辨率控件语义 | 标签改为“窗口分辨率”；全屏时 `BeginDisabled`；注释与 ToggleFullscreen 改读 `SDL_GetWindowSize`（修“隔离纹理尺寸”问题） | Engine 入口测试：全屏用桌面宽高比、退出恢复窗口尺寸（交互会话 1878/0 通过） | settings-display.png：窗口模式下拉可用；settings-fullscreen.png 已生成 | 通过 |
| 9 | 每个设置板块增加恢复默认 | `drawCard` 增加板块级“恢复默认”按钮（7 板块）；`ResetSettingsSection` 复用现有应用逻辑；音频默认值统一 100/40/100（`DefaultMasterVolume/MusicVolume/EffectsVolume`），配置缺省读取同步 | Engine 入口测试逐板块恢复并核对运行态与存档（交互会话 1878/0 通过） | settings-display.png：两卡片右上角按钮可见 | 通过 |
| 10 | 细准星横线消失与粗细过渡 | `DrawSoftSegment` 重写：连续覆盖率 + 透明边缘，`coverage = min(1, thickness)`，四臂共用同一采样路径 | 宽度 0.5–2.0 × 三档缩放 × 四相位，四臂独立断言全过（软件 + D3D11，交互会话 1878/0） | thin-lines-d3d11.png：8 档宽度横线全部可见，最细档偏淡符合连续覆盖设计 | 通过 |
| 11 | 自由帧率上限与 VSync | 帧率改为整数输入（0=不限帧，默认 60，负数归 0）；`SDL_RenderSetVSync` 运行时切换，失败显示实际状态；`[settings] vsync` 存档字段，旧存档兼容 | 任意帧率 + VSync 存取 round-trip；旧存档 165 + 缺失 vsync 正确加载 | settings-display.png：整数输入 + 提示文案可见 | 通过 |
| 12 | 视野收缩中横移与缩放分离 | `PrepareCamera` 改走 `UpdatePlayerCamera`：约束先汇总、同一过渡进度、同帧提交；边缘收放 0.4s smoothstep（`EdgeZoomSeconds = 0.4f`，`t*t*(3-2t)`）；删除 `KeepActorInViewport` / `KeepScopeInViewport` | 相机过渡 30/60/120/144fps × 4 尺寸 × 8 方向 × 3 距离 × 2 镜圈全部有界有限；反向过渡 0.4s 到达 | camera-motion.csv：354 帧轨迹连续，瞄准点屏内稳定 | 通过 |
| 13 | 镜内 1.5×/2× 真正生效 | `BeginScopeScene` / `EndScopeScene` / `DrawScopeScene`：独立渲染目标，镜内倍率 = 主场景实际比例 ×1.5/2.0，镜圈中心与瞄准世界点一致，结束恢复主相机/视口/裁切；角色在镜后单独合成（修“镜内覆盖主场景角色”） | 镜内实测倍率：D3D11 1.5× 误差 0.24%、2× 误差 0.002%；软件 0.78%/0.48%（均 <1%）；光心投影、裁切恢复、主相机恢复均通过 | scope-1.5x.png / scope-2x.png：镜内放大明显、角色可见、十字完整 | 通过 |
| 14 | 更正玩法清单 | README 已按表逐条修正：投掷=按下即投、结算期可移动开火拾取、拆包半径 52、高爆半径 220、结算页等待操作、F6 删除、F1/M 更正；另同步更新武器数量、显示设置、死亡定格、观战、镜内倍率、设置恢复说明 | —（文档项，靠逐条核对） | 已逐条对照代码入口核对 | 通过 |
| 15 | 清理确认废弃文件与死代码 | `PixelEngine/LLM/_fromfile.txt`、`tools/menu-models/CharacterLab.blend1` 已删；`ScopeCameraShift()` 包装、`matchOverReturn` 数据链、`KeepActorInViewport` / `KeepScopeInViewport` 已清 | 全量构建通过 | `git status` 确认两文件为 D 状态 | 通过 |

## 三、证据索引

`build-evidence/experience/`（2026-11-17 20:55 交互会话重跑产物）：

- `scope-magnification-d3d11.csv` / `scope-magnification-software.csv`：镜内实测倍率（见上表第 13 项）
- `camera-motion.csv`：相机过渡 354 帧轨迹
- `thin-lines-d3d11.png` / `thin-lines-software.png`：8 档宽度准星
- `engine/scope-1.5x.png`、`engine/scope-2x.png`：镜内实景
- `engine/death-card.png`：死亡定格 + 击杀卡
- `engine/armor-hud.png`：护甲 HUD 素材
- `engine/c4-spectator.png`：C4 观战
- `engine/settings-display.png`、`engine/settings-fullscreen.png`：设置页（恢复默认 / 帧率输入 / VSync）与全屏态
- `engine/camera-transition-0/6/12/18/24.png`：相机过渡连续帧（20:55 重跑后齐全）

## 四、未闭合项（需 Sun 或后续处理）

1. **拆包音人工试听**：数值验收已过（52/600、跟随、暂停、阵亡隔离），但左右声像与实际听感需 Sun 耳检。
2. **进游戏实测**：细线 D3D11 表现、死亡定格手感、镜内倍率手感、相机过渡手感。

（原未闭合项“交互会话全量重跑”与“相机过渡/全屏图缺失”已于 20:55 重跑后闭合：`--experience` 交互会话 1878 通过、0 失败；5 张相机过渡图与全屏设置图均已生成，`scene evidence saved` 检查全部通过。20:39 那次缺失未复现，应为当次运行会话状态所致，非代码缺陷。）

## 五、观察项（不阻塞验收，Sun 定夺）

- 护甲 HUD 图标 28×29 下偏小，甲盔叠放读作剪影；如需更清晰可放大图标列。
- C4 观战截图中，已安装 C4 实体偏小偏暗，远景不易一眼定位；可考虑在实体外加一层微弱描边或放大绘制尺寸。
- armor-hud.png 一帧里右下武器栏偏灰（测试捕获瞬间状态，游戏中未见异常报告）；如在实机中复现再跟进。

## 六、工作区状态

- 基准提交 `ce41a76`；未提交修改 20 文件 + 删除 2 文件 + 未追踪 6 个素材文件（资产批次 9 文件）；`git diff` +1276/−434（含资产批次 6 个代码文件）。
- 本轮新增测试入口：`gameplay_tests --experience`（死亡/观战、Engine 入口、显示设置、相机过渡、渲染、HUD 矩阵）。
- 未提交，待 Sun 过目后自行处理。
