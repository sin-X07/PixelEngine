# Linear 核对与 macOS 自动验收（2026-11-04）

基线：a3ab7cb（origin/main）。独立 worktree，不覆盖已有未提交修改。

- PIX-24：PR #34 已合并，更新 Done。
- PIX-25/26/27：PR #40 已合并，包含原 #36/#38，更新 Done。
- PIX-7/8/9/10/14/16/17：实现已合并，但用户确认尚未人工验收，保留 In Review。
- PIX-11/15：保留 In Progress，多分辨率、焦点、视觉与同 SHA 双平台证据不齐。
- PIX-20：用户决定固定世界半径；本分支保持旧 1728×1080 基准约 189.8 世界单位，不修改雾绘制和开镜 mask 算法。

验证：Release 全量构建通过（EngineApp、gameplay_tests、dev_replay_runner、两个 UI 输入测试）。玩法测试 188 通过、0 失败。UI ID 与 SDL 坐标回归通过。seed=7，600 ticks，1280×720 与 2560×1600 的逐 tick 哈希一致（仅头部视口元数据不同）。

发现并修复：dev_replay_runner 在 macOS 未链接 OpenGL framework，导致 Menu3D 符号链接失败。

实际启动：主菜单日志确认三只小鸡 walk 动画、T/CT 模型、GL 2.1 Metal 背景与音频加载成功。当前 Computer Use 无法绑定独立 EngineApp 可执行文件（Invalid app），因此未将鼠标交互、画面观感、焦点切换等项目标为通过。Windows 未执行。
