# PixelEngine 文字系统改造总计划（合并版：RENDER_UNIFY 已完成 ✓ + TEXT_TWEAKER 待执行）

> **文档说明**：本文档由 `RENDER_UNIFY_PLAN.md`（文字机制统一，**已执行完成**）与 `TEXT_TWEAKER_PLAN.md`（实时文字调参，**待执行**）合并而来，为文字系统的**唯一权威计划**。
> **第 1 节**：已完成工作（代码已核实），AI 只读了解、**不得回退**。
> **第 2 节起**：待执行工作，AI 按 TP0→TP4 顺序执行，每步编译验证后进入下一步。
> 原两份独立文档的历史版本可废弃（本文档取代之）。

---

# 第 1 节：已完成 —— 文字机制统一（RENDER_UNIFY P0-P6，代码已核实）

## 1.1 完成确认清单（2026-10 代码核实）

| 步骤 | 内容 | 核实证据 |
|---|---|---|
| P0 | ImGuiUI 多字号档位 `{16,18,20,22,24,30,34}` + `GetFont(size)` 就近取档 | `Graphics/ImGuiUI.hpp`：`fontSizes/fonts` 数组 + `GetFont(int) const` |
| P1 | `GameUI` 类新建，`RenderImGuiMenus` 迁出（Actions 11 回调 / MenuCtx / InGameCtx） | `Graphics/GameUI.hpp` 完整结构；Engine.cpp L17 创建、L489 `gameUI->DrawMenu(ctx, BuildActions())` |
| P2 | ImGui 帧每帧外提（Begin/EndFrame 每帧一次） | Engine.cpp L476-477/507 |
| P3 | 游戏内文字迁入 `GameUI::DrawInGame` | GameUI.cpp L168-205（字面量与基线表吻合） |
| P4 | 死代码 + TTF 管线删除 | UIRenderer.hpp 仅剩 3 个图形方法（DrawGameOver/DrawHUD/DrawWeaponStatus）；全库无 `TTF_` 残留 |
| P5 | CMake 移除 SDL2_ttf/FreeType | CMakeLists.txt 无 ttf/freetype 引用 |
| P6 | README 更新 | README.md L29/L56-57 已写明 ImGui 渲染、GameUI/ImGuiUI 职责 |

**Engine.cpp 行数：619 → 521（-98 行）**。原 v2 验证项"≤500"差 21 行——TP3 接线会增约 30 行，此指标降级为可选项，不再作为验收门。

## 1.2 当前代码形态（AI 基线认知）

### 1.2.1 文字绘制入口（统一后，唯一机制 = ImGui）

| 入口 | 文件 | 说明 |
|---|---|---|
| `GameUI::DrawMenu(const MenuCtx&, const Actions&)` | `Graphics/GameUI.cpp` | 全部菜单界面：按钮 + 静态文字（`SetCursorPosX/Y` + `TextUnformatted`，默认 20 档） |
| `GameUI::DrawInGame(const InGameCtx&, ImGuiUI*)` | `Graphics/GameUI.cpp` | 游戏内文字：`DrawImGuiText` / `DrawImGuiTextCentered`（内部 `GetFont(size)`→PushFont→`ForegroundDrawList::AddText`→PopFont） |
| `ImGuiUI::GetFont(int size)` | `Graphics/ImGuiUI.cpp` | 档位 `{16,18,20,22,24,30,34}` 就近取档 |
| `UIRenderer`（3 方法） | `Graphics/UIRenderer.cpp` | **纯图形**：HUD 红心/时间条/金币点、武器面板+图标、结算面板背景（SDL2 绘制） |

### 1.2.2 架构（解耦契约，TP 步骤必须维持）

- `GameUI` **不持有 `Engine*`**；按钮动作经 `Actions` 回调委托；只读数据经 `MenuCtx`/`InGameCtx` 每帧传入；不直接播放音效（`Actions::playClick`）。
- Engine 每帧组装 `MenuCtx`/`InGameCtx` 调用 `gameUI->DrawMenu/DrawInGame`；ImGui 帧由 Engine 统一开合（L476-507）。

## 1.3 文字基线表（合并后唯一权威；坐标已按 GameUI.cpp 逐行核实）

### 1.3.1 游戏内文字（GameUI.cpp `DrawInGame` L168-205；key 供 TP2 参数化使用）

| key | 来源（GameUI.cpp 行） | 现状字面量 |
|---|---|---|
| `weapon_status` | L175-176 | x=`640+(icon?50:12)`（anchored=1），y=549，size=18，色 `{230,238,235}`；panel=`{640,536,150,54}` |
| `no_weapon_hint` | L180 | x=居中（`(800-w)/2`），y=512，size=22，色 `{245,200,90}` |
| `gameover_fail_title` | L185/188 | "失  败"，x=355，y=216，size=30，色 `{245,80,80}` |
| `gameover_fail_subtitle` | L186/189 | x=305，y=256，size=18，色 `{240,140,140}`（文案两种：被怪物击杀/触发致命陷阱） |
| `gameover_end_title` | L191 | "本局结束"，x=334，y=216，size=30，色 `{247,211,130}` |
| `gameover_result` | L202 | x=285，y=280，size=24，色 `{230,238,235}` |
| `gameover_hint` | L203 | "Enter / 空格：返回模式选择"，x=248，y=350，size=18，色 `{134,222,190}` |

### 1.3.2 菜单静态文字（GameUI.cpp `DrawMenu`；全部 `TextUnformatted`，默认 20 档）

| key | 文案（GameUI.cpp 行） | 现状字面量 |
|---|---|---|
| `menu_main_title` | "PIXEL ENGINE"（L54-56） | x=350，y=96，size=20 |
| `menu_history_title` | "历史记录"（L70-72） | x=0，y=120，size=20 |
| `menu_history_mode1` | "模式一  限时挑战"（L74-75） | x=60，size=20（y 随标题流） |
| `menu_history_mode2` | "模式二  迷宫探索"（L76-77） | x=460，size=20 |
| `menu_controls_title` | "操作说明"（L105-107） | x=0，y=120，size=20 |
| `menu_controls_lines` | 5 行操作说明（L109-118） | **x=80；y 为 ImGui 自然流（仅 L105 设 y=120 后自动换行）**；size=20 |
| `menu_settings_title` | "设置"（L122-124） | x=0，y=120，size=20 |
| `menu_exit_title` | "确认退出"（L150-152） | x=0，y=200，size=20 |
| `menu_exit_note` | "本次游戏数据将不会被保存"（L153-155） | x=0，y=270，size=20 |

> 修正说明（相对旧计划）：① 菜单文字行号已从迁移前 Engine.cpp 旧值更新为 **GameUI.cpp 实际行号**；② `menu_controls_lines` 旧计划写"行距 36"不准确——现状是连续 `SetCursorPosX(80)` + `TextUnformatted` 的 **ImGui 自然行距**，参数化只动 x（y 保持自然流）；③ 菜单文字默认色为 `ImGuiCol_Text`（默认主题纯白），参数化用 `TextColored` 显式指定 `{255,255,255,255}`。

---

# 第 2 节：待执行 —— 实时文字调参（TEXT_TWEAKER TP0-TP4）

## 2.0 前置状态（已确认满足）

第 1 节全部完成（多字号 / GameUI / DrawInGame / 死代码与 TTF 清理），本计划**无前置阻塞**，可直接执行。

## 2.1 目标与范围

**目标**：让**所有 ImGui 渲染的文字**（游戏内 7 处 + 菜单静态文字）的位置、字号档位、颜色可在运行时通过 ImGui 面板实时调整（所见即所得），并可保存到配置文件、下次启动自动加载。不再"改一次代码编译一次"。

**可调范围**：

| 文字 | 归属 | 实时性 |
|---|---|---|
| 武器状态文字 | `GameUI::DrawInGame` | 位置/颜色/字号档位 **实时** |
| 无武器提示 | `GameUI::DrawInGame` | 同上 |
| 结算 5 处文字 | `GameUI::DrawInGame` | 同上 |
| 菜单静态文字（9 个 key，见 1.3.2） | `GameUI::DrawMenu` | 位置/颜色/字号档位 **实时** |
| 全局字体文件（`text.ini` `[global] font=`） | 启动时加载进 ImGui atlas | **仅启动生效，修改需重启**（ImGui atlas 机制限制） |

**明确不在范围**：
- 按钮布局/按钮文字（`ImGui::Button` 的 label 与位置）——交互控件，另行规划。
- 游戏逻辑、战斗、地图、粒子、音频——一律不动。
- 引入任何第三方库——禁止（ini 用标准库手写解析，沿用 `SaveManager` 风格）。

## 2.2 铁律（AI 必须遵守）

1. **行为等价**：无配置文件（首次运行）时，画面必须与改造前**一致**（文字内容/位置/字号档位/颜色）。所有新配置默认值 = 第 1.3 节基线表字面量。
2. **字号档位化**：字号只能取档位 `{16, 18, 20, 22, 24, 30, 34}` 之一（经 `ImGuiUI::GetFont(size)` 就近取档）。配置写目标点数（如 18），运行时自动就近取档；**禁止任意字号**。
3. **字体仅启动生效**：`[global] font=` 只在 `ImGuiUI::Initialize` 时读取并加载；运行时修改不生效直到重启；`TextTweakerUI` 中该字段只读展示，标注"需重启"。
4. **每步可编译可运行**：TP0~TP4 每步结束必须能通过 `cmake --build build2` 并正常运行游戏。
5. **ImGui 帧纪律**：`imGuiUI->BeginFrame()/EndFrame()` 成对、每帧一次、不嵌套；面板绘制与游戏内文字都只发生在两者之间、`SDL_RenderPresent` 之前。
6. **不引入新依赖**：不新增第三方库、不改 CMakeLists。
7. **容错**：配置文件缺失/损坏/字段缺失 → 回退默认值，不崩溃。
8. **GameUI 解耦纪律**：`TextConfig` 由 Engine 创建并注入 GameUI（构造或 `SetTextConfig`）；GameUI 不自行创建/保存配置；维持第 1.2.2 契约。
9. **每步独立提交**：按 TP 步粒度 commit，消息写"TPx: 摘要"。

## 2.3 设计

### 2.3.1 `TextStyle`（`Graphics/TextConfig.hpp`）

```cpp
// 一种文字的完整可调样式（ImGui 渲染）。
struct TextStyle {
    float x = 0.0f, y = 0.0f;   // 位置：DrawList 坐标（游戏内）或 SetCursorPosX/Y（菜单）
    int   size = 20;            // 字号目标点数，运行时经 GetFont(size) 就近取档 {16,18,20,22,24,30,34}
    Uint8 r = 255, g = 255, b = 255, a = 255;  // 颜色（AddText 转 IM_COL32 / TextColored）
    int   alignX = 0;           // 0=左对齐(x 生效)；1=水平居中(自动算 x)
    bool  anchored = false;     // true=位置由调用点锚点决定（面板中 X/Y 禁用）；weapon_status 用
    // 无 fontPath：字体文件走 [global] font=，仅启动生效
};
```

### 2.3.2 `TextConfig`（`Graphics/TextConfig.hpp/.cpp`）

- 成员：`std::map<std::string, TextStyle> styles;` + 默认表；`std::string globalFontPath;`。
- 接口：
  - `bool Load(const std::string& path);` / `bool Save(const std::string& path) const;`
  - `const TextStyle& Get(const std::string& key) const;` / `TextStyle& GetMutable(const std::string& key);`
  - `std::vector<std::string> Keys() const;`（按默认表声明顺序：先游戏内 7 key，后菜单 9 key）
  - `void ResetAll();`
  - `const std::string& GlobalFontPath() const;`
- 默认表：按 1.3.1/1.3.2 硬编码在 `.cpp`（16 个 key）。
- 默认路径：`PathUtils::ResolveProjectFile("../assets/config/text.ini")`；`Save` 前 `std::filesystem::create_directories`（**assets/config 当前不存在，须创建**）。

### 2.3.3 ini 文件格式（手写解析，字段顺序固定）

```ini
[global]
font=C:/Windows/Fonts/msyh.ttc   ; 启动加载，需重启生效

[weapon_status]
x=0
y=0
size=18
r=230
g=238
b=235
a=255
alignX=0
anchored=1

[menu_main_title]
x=350
y=96
size=20
r=255
g=255
b=255
a=255
alignX=0
anchored=0
```
解析规则：`[key]` 开新段；`k=v` 赋值；未知字段忽略；缺失字段用默认值；`[global]` 段的 `font=` 存入 `globalFontPath`。

### 2.3.4 `TextTweakerUI`（`Graphics/TextTweakerUI.hpp/.cpp`）

- 唯一公开方法：`void Draw(TextConfig& config);`（须在 ImGui 帧内调用）。
- 面板内容：
  - 顶部：配置文件路径（只读）、"保存全部"、"重置全部"按钮、说明（"F2 关闭面板"）。
  - `[global]` 区：字体文件（只读展示 + "需重启生效"标注）。
  - 每 key 一行（折叠头 = key 名）：X/Y `SliderFloat`（0~800/0~600；`anchored` 时 `BeginDisabled`）、字号 `SliderInt`（16~34，步进 2，取档位值）、颜色 `ColorEdit4`、行尾"重置本项"。
  - "保存全部" → `config.Save(...)`（路径由 Engine 注入或 TextConfig 记住上次 Load/Save 路径）。

### 2.3.5 `GameUI` 接入 TextConfig（对齐现状）

- `GameUI.hpp` 新增：`void SetTextConfig(TextConfig* cfg);`、`void SetImGuiUI(ImGuiUI* ui);`、私有成员 `TextConfig* cfg_ = nullptr; ImGuiUI* imGuiUI_ = nullptr;`。
  - `SetImGuiUI` 理由：现状 `DrawInGame` 已直接接收 `ImGuiUI*`（保持兼容），但 `DrawMenu` 无 ImGuiUI 访问，菜单文字 PushFont 需要它。
- `GameUI.cpp`：
  - `DrawInGame`：所有 `DrawImGuiText`/`DrawImGuiTextCentered` 调用改查表：
    ```cpp
    const TextStyle& s = cfg_ ? cfg_->Get("weapon_status") : /*1.3.1 默认*/;
    float dx = s.anchored ? computedX : s.x;         // anchored 用调用点计算位置
    DrawImGuiText(imGuiUI, line.c_str(), dx, s.y, s.r, s.g, s.b, s.a, s.size);
    ```
    - `no_weapon_hint`：`alignX==1` 时保持 `DrawImGuiTextCentered`（内部已居中，y 用 s.y、色/字号用 s）；或将居中逻辑改为查表后 `CalcTextSize`。
  - `DrawMenu`：静态文字改查表 + PushFont + TextColored：
    ```cpp
    const TextStyle& s = cfg_ ? cfg_->Get("menu_controls_lines") : /*1.3.2 默认*/;
    ImGui::SetCursorPosX(s.x);                       // 多行 key 只动 x，y 保持自然流（现状 L109-118）
    ImGui::PushFont(imGuiUI_->GetFont(s.size));
    ImGui::TextColored(ImVec4(s.r/255.f, s.g/255.f, s.b/255.f, s.a/255.f), "操作说明");
    ImGui::PopFont();
    ```
    - 单行标题类 key（`menu_main_title`/`menu_*_title`/`menu_exit_note`）：`SetCursorPosX(s.x); SetCursorPosY(s.y);` 后同法绘制。
- **回退规则**：`cfg_ == nullptr` 时全部走现状字面量路径（铁律 1）。

### 2.3.6 `Engine` 接线

- `Engine.hpp` 新增成员：`std::unique_ptr<TextConfig> textConfig; std::unique_ptr<TextTweakerUI> textTweaker; bool tweakerOpen = false;`
- 构造函数：创建 `textConfig`、`textTweaker`；`gameUI->SetTextConfig(textConfig.get())`；`gameUI->SetImGuiUI(imGuiUI.get())`。
- `Initialize`：
  - `textConfig->Load(PathUtils::ResolveProjectFile("../assets/config/text.ini"));`（无文件→默认；**首次运行后立即 Save 生成模板**，即 `assets/config/` 首次创建）。
  - `[global] font=`：若 `GlobalFontPath()` 非空，在 `ImGuiUI::Initialize` 之前调用 `imGuiUI->SetFontPath(path)`（ImGuiUI 新增 `void SetFontPath(const std::string&)`：字体加载前设置，替换默认三选一逻辑中同路径的优先加载）。
- `InputManager.hpp/.cpp`：新增 `bool IsTweakerToggleRequested() const;`（SDLK_F2，仿 F11/F1：InputManager.cpp L60-61 同段加 case）。
- `HandleInput`：`if (inputManager->IsTweakerToggleRequested()) tweakerOpen = !tweakerOpen;`
- `Render()`（当前帧结构 L476-507 内）：在 `gameUI->DrawMenu/DrawInGame` 之后、`imGuiUI->EndFrame()` 之前：
  ```cpp
  if (tweakerOpen && textTweaker) textTweaker->Draw(*textConfig);
  ```
- `Clean`：`if (textConfig) textConfig->Save(PathUtils::ResolveProjectFile("../assets/config/text.ini"));`（退出持久化）。

## 2.4 实施步骤

### TP0：新增 TextConfig（纯新增，不接线）

- 新建 `Graphics/TextConfig.hpp/.cpp`（2.3.1/2.3.2/2.3.3 实现；默认表逐字抄录 1.3.1/1.3.2，16 个 key）。
- 不修改任何现有文件。
- **验收**：编译通过；游戏运行行为不变。

### TP1：新增 TextTweakerUI（纯新增，未接线）

- 新建 `Graphics/TextTweakerUI.hpp/.cpp`（2.3.4 实现）。
- **验收**：编译通过；无行为变化。

### TP2：GameUI 接入 TextConfig（行为等价关键步）

- 改 `Graphics/GameUI.hpp/.cpp`：`SetTextConfig` + `SetImGuiUI` + 成员；`DrawInGame`/`DrawMenu` 查表替换（2.3.5），`cfg_==nullptr` 回退字面量。
- 改 `Engine.hpp/.cpp`：创建 `textConfig` 并 `gameUI->SetTextConfig(...)`、`gameUI->SetImGuiUI(imGuiUI.get())`。
- **验收**：**不生成 text.ini 启动**，主菜单→游戏→结算，画面与改造前一致（对照 1.3 基线）。

### TP3：Engine 接线 + F2 开关（实时生效）

- 改 `Engine.hpp/.cpp`：成员 `textTweaker/tweakerOpen`、Initialize Load + 首次 Save、`[global] font=` 传 `ImGuiUI`、Render 内面板绘制、Clean 自动保存。
- 改 `InputManager.hpp/.cpp`：F2 查询。
- **验收**：
  - 游戏中按 F2 → 面板弹出；拖 X/Y/字号档位/颜色 → 对应文字**即时变化**（游戏内 7 处 + 菜单 9 key）；再按 F2 关闭。
  - 点"保存全部" → 重启 → 调整保持。
  - 修改 `[global] font=` → **重启**后生效（运行时不变，属预期）。
  - 菜单按钮布局/交互不受影响；HUD 图形不受影响。

### TP4：收尾与文档

- 首次运行生成 `assets/config/text.ini`（含全部 16 key 与注释）。
- `README.md` 追加：F2 面板用法、如何新增文字 key（GameUI 调用点 + 1.3 表加一行 + 面板自动出现）、字体需重启说明。
- 编译告警清零；删除临时调试代码。
- **验收**：完整跑 2.5.1 清单。

## 2.5 验证与回滚

### 2.5.1 验证清单（TP4 结束时全项过）

1. 全新环境（无 text.ini）启动 → 画面与改造前一致。
2. F2 面板在 MAIN_MENU/PLAYING/PAUSED/GAMEOVER 四态均可开合。
3. 拖动 X/Y/字号档位/颜色即时生效（游戏内 7 处 + 菜单静态文字）；`anchored` key 的 X/Y 控件禁用。
4. 保存 → 重启 → 生效；`[global] font=` 重启生效。
5. 改坏 ini（删字段/乱码）→ 启动不崩溃、回退默认。
6. 回归：菜单按钮、游戏操作、结算流程、声音无异常；ImGui 帧无嵌套报错。

### 2.5.2 回滚

- 每步独立提交；TP2 出问题 → `git revert` 该步后继续。
- 最坏情况：`git checkout -- Graphics/ Engine.cpp Input/`，TP0/TP1 新文件删除即可。
- 第 1 节成果（GameUI/多字号/死代码清理）**不得回退**——调参计划建立在它之上。

---

# 第 3 节：AI 执行提示

- 根目录 `D:\BJTU\Cpp\Outstanding Projects\PixelEngine\`；构建 `cmake --build build2`（Windows，无 Bash，用 PowerShell）。
- 新增文件用 `#pragma once`；`TextConfig` 默认值与 1.3 表**逐字一致**，AI 不得"顺手改进"坐标/字号。
- 字号一律经 `ImGuiUI::GetFont(size)` 就近取档；禁止直接造任意字号字体。
- 每步先编译再继续；warning 不允许带到下一步；`PushFont/PopFont`、`Begin/End` 必须成对。
- 菜单多行 key（`menu_controls_lines`）只参数化 x；y 保持 ImGui 自然流，**不要**强行套"y=首行+行号*行距"公式。
