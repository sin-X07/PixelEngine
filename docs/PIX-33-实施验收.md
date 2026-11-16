# PIX-33 实施与统一验收记录

日期为 2026-11-16。依据 Linear PIX-33 与 `docs/PIX-33-part2-audio-plan.md` 实施。

## 逐项交付

| 任务 | 实施结果 |
| --- | --- |
| 1 换弹 | 33 把可换弹枪械分别映射到独立轨道，保留指定的 8 条代表文件，新增 25 条枚举、加载与查找 |
| 2 切枪 | 按 WeaponId 优先查询，全部 switch 文件均有引用，AWP 使用 switch_awp.wav，保留类别兜底 |
| 3 阵亡 | 玩家统一死亡入口播放 DEATH，覆盖枪击、手雷、火区与 C4 死亡 |
| 4 拾取 | 自动与手动拾取调用均传入音频管理器，成功取得物品后播放 PICKUP |
| 5 过程音 | 玩家下包 3 秒、持钳拆包 4 秒、无钳拆包 10 秒，BOT 拆包使用空间音频。松开、离开、死亡、成功、回合结束时停止对应通道 |
| 6 确认音 | 加载 bomb_planted.mp3 与 bomb_defused.mp3 |
| 7 C4 爆炸 | 使用独立 bomb_explosion.wav |
| 8 插销 | ThrowGrenade 校验成功后播放，玩家使用本地音频、BOT 使用空间音频 |
| 9 拆包钳 | BuyKind::DefuseKit，$400，CT 专用，重复购买拒绝，死亡和跨回合保留，新比赛清除。买菜单含图标及拆包 4 秒说明，状态 HUD 显示小钳子 |
| 10 C4 图 | WeaponDef 接入 c4.png 与 c4_d.png |
| 11 G21 | 修正衰减注释，ASSET_SPEC 补充 200 世界单位、最小因子 0 的说明 |

详细计划要求 T 方购买钳子返回 InvalidItem，购买校验按此实现。拆包者更换时进度清零，避免玩家与 BOT 继承彼此的进度。过程音使用预留通道 1、2，防止停止过期通道号时误停其他音效。

## 已执行验证

- `cmake --build build-release -j 8` 通过，产物为 `build-release/EngineApp.exe`。
- `cmake --build build-tests --target gameplay_tests -j 8` 通过。
- `build-tests/gameplay_tests.exe` 得到 1341 项通过、0 项失败。日志为 `build-tests/pix33-regression.log`。
- 新增测试覆盖钳子扣款、重复购买、T 方限制、死亡及跨回合保留、4 秒及 10 秒拆包阈值、松开清零、换弹音加载、事件音播放与停止。
- 音频映射引用的 147 个不同素材路径均存在，全部 switch_*.wav 均已引用。音频初始化与解码测试通过。
- 最终 release 启动并完成 CT 对局初始化，`build-release/pix33-smoke-errors.log` 为空。启动检查后已关闭测试进程。
- `git diff --check` 通过。

## 待完成的人工验收

桌面工具启动请求超时，随后未返回游戏窗口，当前无法完成游戏内视觉操作与实听。音频自动测试使用 SDL dummy 设备，以下听感与可视交互项目尚未签验。

- 各类枪械换弹以及 2–3 把切枪，含 AWP。
- 被 BOT 击杀、捡枪、投掷时的插销与出手听感。
- 按住 E 下包及拆包，中途松开、离开、成功时的音效表现。
- C4 爆炸听感。
- 实际买菜单购买钳子，核对 $400、说明文字、图标及 HUD 标记。

改动保留在当前工作区，供 Sun 统一过目。工作区原有素材和 PIX-32 修改已保留。
