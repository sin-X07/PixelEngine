# PixelEngine — CS2D-like 单机版

C++17 / SDL2 的俯视角 2D 战术射击游戏，玩法对标 Steam 的 **CS2D**。
玩家（CT）与 LLM 驱动的 BOT 进行 5v5 对抗。

---

## 当前玩法

| 模式 | 说明 |
|---|---|
| **团队歼灭** | 回合制 T/CT 对抗，歼灭对方全部成员或时间到守方胜 |
| **爆破** | T 在包点下包，C4 40 秒倒计时；CT 需拆包 |
| **人质营救** | CT 营救全部人质，T 需阻止 |
| **死斗** | 自由击杀，即时重生，无回合 |

### 核心系统

- **回合制**：`冻结 → 购买 → 交战 → 结算` 四阶段循环，先到 8 分获胜。
- **经济**：起始 $800；击杀 +$300；回合胜利 +$3250；失败 $1400 起、连败递增；上限 $16000。
- **买枪**：购买期按 `B` 打开菜单，需在己方购买区内且余额充足。
- **武器库**：Glock / Deagle / Galil / AK-47 / M4A1 / SMG / 霰弹枪 / AWP / 匕首 + 三种手雷
  （高爆、闪光、烟雾）。主/副/近战/投掷四个槽位。
- **手雷**：高爆范围伤害、闪光致盲、烟雾遮挡双方视线。
- **护甲**：护甲吸收 50% 伤害（配头盔 60%），护甲按吸收量消耗，耗尽后头盔失效。
  在买枪菜单「护甲」分组购买（$650 / $1000）。
- **目标物**：C4 下包/拆包、人质营救，按住 `E` 交互。
- **视野**：150px + 120° 扇形 + 墙体遮挡，与遮罩严格一致；队友始终可见。
- **LLM BOT**：BOT 由本地 `llama-server` 驱动，决策上下文包含阵营、回合阶段、比分、金钱、
  任务目标；LLM 不可用时自动回退传统行为树。

---

## 操作

| 按键 | 功能 |
|---|---|
| `W` `A` `S` `D` | 移动 |
| 鼠标左键 | 射击 / 投掷 |
| `1` `2` `3` `4` | 主武器 / 副武器 / 近战 / 投掷物 |
| `R` | 换弹 |
| `Shift` | 潜行（减速、降低被发现概率） |
| `B` | 买枪菜单（仅购买期） |
| `E` | 拆包 / 营救人质（按住） |
| `Tab` | 记分板（按住） |
| `Esc` | 暂停 |
| `F11` | 全屏 |
| `F1` | 静音 |

---

## 构建与运行

前置：CMake 3.16+、MinGW-w64（x86_64 UCRT）。SDL2 / SDL2_mixer / SDL2_image
通过 FetchContent 自动下载并校验 SHA-256。

```powershell
cmake --preset mingw-debug
cmake --build build
.\build\EngineApp.exe
```

### 调试参数

```powershell
EngineApp.exe --autostart             # 跳过菜单直接开局
EngineApp.exe --autostart --fastround # 短回合，快速验证回合状态机
EngineApp.exe --autostart --mode=2    # 0=死斗 1=歼灭 2=爆破 3=人质
```

---

## LLM BOT 配置

配置在 `assets/config/llm.ini`：

```ini
[server]
url=http://127.0.0.1:9931

[launcher]
auto_launch=1          ; 自动拉起 llama-server
exe=llm_bot\llama.cpp\llama-server.exe
model=llm_bot\models\qwen2.5-7b-instruct-q4_k_m.gguf
ngl=99
startup_wait=20
kill_on_exit=1         ; 退出时关闭本程序拉起的 server 以释放显存
```

- 模型与二进制体积大，不入 Git；运行 `.\model_setup.ps1` 自动下载。
- 退出时只关闭**本程序拉起**的 server（通过 `llm_server.pid` 记录，跨会话可回收）；
  外部手动启动的服务不会被误杀。

---

## 素材

素材规格完整契约见 [`tools/ASSET_SPEC.md`](tools/ASSET_SPEC.md)（图片尺寸、朝向、
音效清单、地图对象层约定）。

当前仓库内的部分素材是**占位文件**（带文字标签），由脚本生成：

```powershell
python tools/gen_placeholders.py         # 补齐缺失的图片/音效占位
python tools/gen_placeholder_map.py      # 生成爆破/人质占位地图
python tools/split_spawns.py <map.tmj>   # 通用出生点拆成 spawn_t / spawn_ct
```

**真实素材到位后，直接覆盖同名同路径文件即可，无需改代码。**
启动日志会汇总缺失项：`[Asset] 贴图缺失 N 项` / `[Asset] 音效缺失 N 项`。

### 地图

Tiled 导出的 `.tmj`，放在 `assets/cs2d/maps/`。对象层约定：

- `Collisions`：矩形 → 碰撞体
- `Entities`：`spawn_t` / `spawn_ct`（出生点）、`bomb_site`（`name="A"/"B"`）、
  `hostage`、`buy_zone`（属性 `team=t/ct/both`）

---

## 项目结构

```
PixelEngine/
  Engine.{hpp,cpp}          应用外壳：窗口/主循环/输入/菜单/设置
  Core/                     Types、IniFile、PathUtils
  CS2D/
    CS2DGame*.cpp           对局：主逻辑 / 回合 / 战斗 / BOT / 目标物
    Teams/TeamManager       阵营比分
    Rounds/RoundManager     回合状态机
    Economy/EconomySystem   金钱规则
    Weapons/WeaponDef       武器数据表（唯一数据源）
    Navigation/PathFinder   网格 A* 寻路（含视线拉直、卡墙自恢复）
    CS2DWorld/Renderer/Overlay/TMJParser/MiniJSON
  LLM/                      llama-server 客户端、决策调度、意图与行为执行
  Graphics/                 GameUI（ImGui 菜单/HUD）、ImGuiUI
  Audio/ Input/ Storage/
tools/                      素材规格与占位生成脚本
```
