# PIX-34 第二轮体验修复验收记录

更新于 2026-11-16。14 项需求的代码实现已完成，最终验收由项目负责人确认。

## 验收版本

- Release 程序位于 [build-release/EngineApp.exe](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-release/EngineApp.exe>)。启动时需要同目录内的资源文件，保留整个 `build-release` 目录使用。
- 已按授权关闭此前占用程序文件的游戏进程，并更新原来的 Release 路径。
- 构建标识为 `0.3.0-release (8084b3f)`。标识中的提交号对应工作区基线，本次修改保存在当前工作区。
- 程序大小为 8,871,951 字节。SHA-256 为 `ACB1F5DFD6F1D2A5D4E085CDAE6484E8C859552C62F14FE81674C064CA1EC2AC`。
- BOT 默认难度采用索引 2，显示“困难”。已有存档中的自定义难度继续读取。

## 构建与自动化结果

| 验证 | 结果 | 记录 |
|---|---|---|
| 全量 gameplay_tests | 1463 项通过，0 项失败 | [完整日志](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/gameplay-tests.log>) |
| Release 构建 | EngineApp 构建成功，资源复制完成 | [构建日志](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/release-build.log>) |
| 差异空白检查 | `git diff --check` 通过 | 工作区检查 |
| Release 爆破对局 | 古代遗迹八回合结束，CT 以 8 比 0 获胜，进入结算并写入历史 | [对局日志](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/match-release.log>) |

完整对局使用规则教练和现有 `--fastround` 参数验证回合生命周期。并行拆包的边界场景通过独立的玩法测试构造。

## 14 项需求对应结果

| 编号 | 实现内容 | 已执行验证 |
|---|---|---|
| 1 | 普通文字使用所属窗口的 draw list。设置内容、历史行背景、表头、结果胶囊与文字遵循滚动裁剪。HUD 和加载提示拥有自己的全屏绘制区域 | 设置长列表实机滚动。50 条历史记录在顶部、中部和底部进行真实 ImGui 绘制。回归覆盖 HUD 绘制区域，避免默认 Debug 窗口出现 |
| 2 | 23 个动作统一使用设备类型和输入码。支持键盘及五种鼠标按钮。删除左手鼠标控件，加入开火、开镜、提示显隐及快速回切。冲突使用居中模态层 | 实机捕获鼠标右键、取消冲突、交换绑定和恢复默认。逐动作鼠标侧键测试、按住与边沿测试、界面消费点击、失焦清理、旧数字绑定迁移、左右手迁移、默认键被占用和重启持久化测试通过 |
| 3 | T 方商品列表构建时跳过拆包钳。CT 保留商品与购买检查 | T 方购买请求被拒绝、CT 购买成功及重复购买检查通过。实机购买菜单可以打开，装备区的最终人工检查列在文末 |
| 4 | 圆形雷达采用可复用离屏纹理和圆形网格裁剪。地图与点位共用等比变换，严格居中和反向旋转。背景透明度使用 alpha，尺寸上限 400。小雷达与中心雷达分别缓存纹理 | 软件渲染像素测试覆盖透明度 0、128、255，圆形角落、地图边缘居中、前方旋转到上方、关闭居中与最大尺寸。实机检查直径 400、透明度 0 的背景透出及标记保留 |
| 5 | 移动速度独立驱动分划与辅助线虚化，使用缓存高斯采样轮廓。停止后平滑恢复。辅助线仅在非开镜状态出现 | 跑动比潜行更模糊、停止恢复及镜内线条软化的像素测试通过。保存静止和移动分划对照。实机开镜后辅助线隐藏 |
| 6 | 四段准星共用浮点中心，中心点改为抗锯齿圆点。设置预览复用实际渲染函数 | 圆点中心、圆形外部像素与四段对称性测试通过。设置页和实际 HUD 已检查 |
| 7 | 方向三角形及四边同时亮起的旧红晕已退出绘制。新效果沿对应边缘展开，角落使用连续内缩网格，角向和向内渐隐，多条记录叠加并限制透明度 | 八方向真实 UI 渲染并保存图像，检查中心保持清晰。已查看边缘渐变图像。多方向实战辨识度保留人工验收 |
| 8 | C4 使用副武器姿态与尺寸。玩家和 BOT 安装 C4 时使用同一持姿规则 | 三套角色精灵、八方向 C4 渲染图已生成并检查 |
| 9 | 近战武器以角色局部右手位置附着，挥刀围绕右手位置旋转，偏移集中在 Tuning | 三套角色精灵、八方向持刀图已生成并检查。转向与姿态相关既有测试通过 |
| 10 | 设置页统一极简深色卡片、蓝灰强调色、控件状态、页签、页脚及实际渲染预览。窄比例改为单列。修正低渲染缩放下字体与控件缩放不一致 | 1280×720、1920×1080、2560×1600、3440×1440、1024×768，分别执行 100%、75%、50% 的真实 UI 绘制和几何检查。检查双列、单列及 50% 缩放图像。设置返回保存和绑定持久化通过 |
| 11 | 退出确认按来源状态显示说明和返回目标 | 实机主菜单退出确认仅显示确认标题与按钮。对局来源仍提供进度说明 |
| 12 | 玩家与每名 BOT 拥有独立进度及声音通道。HUD 选择预计剩余时间最短者。参与者失效只清理自己，统一完成判定和奖励 | 玩家加两名 BOT、有钳与无钳、玩家松键、领先者死亡、低百分比但更早完成、爆炸截止时间、同时完成稳定选人、个人奖励及回合结算单次触发测试通过。声音并行、取消互不影响、普通音效无法复用拆包保留通道测试通过 |
| 13 | 帮助改为底部居中的紧凑 HUD 提示带，装备槽合并显示。标签读取当前绑定，F1 默认切换显隐并保存偏好。菜单和购买界面按可见性规则隐藏 | 全部分辨率与缩放组合中检查提示带位于视口内。实机检查重新绑定后的鼠标中键标签、底部生命值和弹药区域，以及购买菜单显示时隐藏提示带。显隐偏好存取测试通过 |
| 14 | 上一件装备保存槽位和武器类型。Q 覆盖主副武器、近战、不同投掷物及 C4。切换统一处理历史、弹药、换弹取消、关镜和声音 | 首次 Q、手枪与近战往返、不同投掷物往返、消耗后回退、C4 收起和恢复、无效请求、丢枪及替换旧枪后的回退测试通过。拾取替换时还覆盖掉落物容器扩容，避免历史更新访问失效引用 |

## 关键图像

### 设置与输入

- [设置页双列布局](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/settings-operation.png>)
- [居中的鼠标绑定冲突层](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/binding-conflict.png>)
- [键位页滚动到底部与交换后的绑定](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/settings-scroll-bottom.png>)
- [1024×768 单列布局](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/layout-1024x768-100.png>)
- [1280×720、50% 渲染缩放](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/layout-1280x720-50.png>)
- [3440×1440 超宽布局](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/layout-3440x1440-100.png>)
- [历史记录滚动到底部](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/history-2.png>)
- [主菜单退出确认](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/main-menu-exit.png>)

### 战斗视觉

- [直径 400、背景 alpha 为 0 的实机雷达](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/radar-alpha-zero-400.png>)
- [绑定更新后的底部战斗提示](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/combat-hints-remapped.png>)
- [实际开镜与辅助线隐藏](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/scope-in-game.png>)
- [静止镜内分划](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/scope-static.png>) 与 [移动镜内分划](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/scope-moving.png>)
- [方向性受击边缘渐变](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/damage-1.png>)。同目录 `damage-0.png` 至 `damage-7.png` 覆盖八方向。
- [三套角色八方向持 C4](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/c4-directions.png>)
- [三套角色八方向持刀](<D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build-evidence/pix34-round2/knife-directions.png>)

布局、历史、姿态、分划对照和八方向受击图像由自动化直接调用实际渲染入口生成。设置交互、实机雷达、战斗提示和开镜图像来自 Windows 游戏窗口。

## 人工验收仍需确认

1. 当前 Windows 自动化对 SDL 键盘输入的注入未能稳定驱动 F1 和 Q。它们的动作绑定、显隐存档和切换逻辑已通过自动化测试，实际键盘操作需人工复核。
2. 连续移动、潜行和急停时的模糊强度、挥刀连接感，以及多方向连续受击时的辨识度，需要人工操作确认。已完成静态渲染、速度变化和状态恢复的自动化验证。
3. T 与 CT 购买装备区的最终界面检查，以及玩家和多名 BOT 在普通回合时长下并行拆包的完整实机体验，需要人工确认。对应商品检查、并行计时、奖励和声音边界测试均已通过。
4. 分辨率与缩放矩阵已通过软件渲染检查。多种硬件刷新率下的连续交互效果和设置页最终美术观感，由项目负责人验收。

## 复现命令

在仓库根目录执行以下命令可复验当前工作区。

```powershell
cmake --build build-tests --target gameplay_tests -j 6
.\build-tests\gameplay_tests.exe
cmake --build build-release --target EngineApp -j 6
.\build-release\EngineApp.exe --coach=rule --settings
```

程序默认启动仍使用现有教练检测方式。验收自动化使用 `--coach=rule`，以保持测试条件可复现。
