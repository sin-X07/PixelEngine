# PixelEngine

一个仍在开发中的 C++17 / SDL2 像素风迷宫小游戏。当前优先保持代码和构建说明易于修改；功能、文件布局和依赖配置都可能继续调整。

## 当前功能

- 主菜单、模式选择、角色选择、历史记录、按键说明和音量设置页面。
- 角色系统：主菜单「选择角色」从 4 个角色（豆丁、莉娅、阿凯、老约翰）中任选其一，游戏中玩家显示为所选角色；选择持久化保存，重启后保持，也可在角色选择页按数字键 `1-4` 快捷选中。
- 限时挑战：在 30 秒内收集金币并累计分数。
- 迷宫探索：生成随机迷宫，记录通关时间。
- 地牢主题 PNG 背景、墙面、玩家和宝箱精灵。
- 游戏中按 `Esc` 暂停，可继续、重新开始、调整音量或确认放弃。
- F11 全屏切换；历史记录保存在纯文本文件中。

## 操作

- 菜单：鼠标左键；模式选择页也可按 `1` 或 `2` 开始对应模式；角色选择页可按 `1`-`4` 快捷选中对应角色。点击空白处不会播放点击音效。
- 移动：`W` / `A` / `S` / `D` 或方向键；松键后会短暂惯性滑行，反向输入会先减速再转向。
- 冲刺：按住 `Shift`，速度逐渐提升；松开后逐渐恢复。
- 聚焦：按住 `Ctrl` 扩大视野并放慢移动；松开后恢复。
- 跳跃：按 `Space` 越过矮墙和小障碍；普通墙无法跳过。迷宫模式会使用可跳过或可破坏障碍制造伪墙、捷径和假死路；矮墙和小障碍不遮挡视线。
- 全屏：`F11`。
- 暂停/继续：游戏中按 `Esc` 暂停，再按 `Esc` 或选择“继续游戏”恢复。
- 静音：`F1` 切换主音量静音；主音量、音乐和音效可在“设置”中分别调节。
- 返回：子页面按 `Esc` 返回；主菜单按 `Esc` 退出；结算页面按 `Esc` 返回模式选择。
- 结算页面：按 `Enter` 或空格返回模式选择。
- 文字调参：按 `F2` 打开/关闭实时文字调参面板（可调位置/字号档位/颜色，所见即所得）；点“保存全部”写入 `assets/config/text.ini`，下次启动自动加载。

## 文字调参

所有 ImGui 文字（游戏内 7 处 + 菜单静态文字 9 处）的位置、字号档位、颜色可在 `F2` 面板实时调整并保存到 `assets/config/text.ini`（首次运行自动生成模板）。字号取档位 `{16,18,20,22,24,30,34}` 就近值；`[global] font=` 指定字体文件，**修改后需重启**生效。新增文字 key：在 `GameUI` 调用点查表 + `TextConfig` 默认表加一行即可，面板会自动出现。

## 构建与运行

项目使用 CMake 3.16+、C++17、MinGW-w64、SDL2、SDL2_image 和 SDL2_mixer，另 vendor 了 Dear ImGui（MIT）用于文字与菜单渲染。SDL2 固定为 2.30.3、SDL2_mixer 固定为 2.6.3、SDL2_image 固定为 2.8.2，三者均为**静态构建**（SDL2_mixer 使用内置 dr_mp3/stb 后端，SDL2_image 使用内置 stb 后端解码 PNG/JPG）。所有第三方依赖通过 CMake 的 FetchContent 在首次配置时**自动下载并校验 SHA-256**，无需手工准备依赖，也无需任何本机绝对路径。

### 前置要求

在 Windows 上仅需一次性安装标准工具链：

- [CMake](https://cmake.org/download/) 3.16 或更高（确保 `cmake` 在 PATH 中）。
- [MinGW-w64](https://winlibs.com/)（x86_64、UCRT 运行时；确保 `gcc`/`g++`/`mingw32-make` 在 PATH 中）。
- 可选：VS Code + [C/C++ 扩展](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) + [CMake Tools 扩展](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)，用于 IDE 内配置/构建/调试。

### 命令行构建（零配置）

```powershell
# 在仓库根目录执行：配置 + 编译
cmake --preset mingw-debug
cmake --build build
```

生成的程序为 `build/EngineApp.exe`，`assets/` 会自动复制到 exe 旁，直接双击或命令行运行即可：

```powershell
.\build\EngineApp.exe
```

### 使用 VS Code（零配置）

1. 用 VS Code 打开仓库根目录。
2. 安装 C/C++ 与 CMake Tools 扩展。
3. 在底部状态栏选择编译器（CMake Tools 会自动识别 MinGW），选择 `mingw-debug` 预设并构建。
4. 按 `F5` 直接进入 GDB 调试并运行游戏。

`IntelliSense`、`F5` 调试、构建目录均已通过 `.vscode/` 与 `PixelEngine/CMakePresets.json` 配置为相对路径，clone 到任意位置开箱即用。

### 依赖与离线构建

依赖源码在首次配置时从官方地址下载并用 SHA-256 校验；若需离线或使用本地源码，可在配置时指向已解压的源码目录：

```powershell
cmake --preset mingw-debug `
  -DFETCHCONTENT_SOURCE_DIR_SDL2=D:/path/to/SDL-release-2.30.3 `
  -DFETCHCONTENT_SOURCE_DIR_SDL2_MIXER=D:/path/to/SDL_mixer-release-2.6.3 `
  -DFETCHCONTENT_SOURCE_DIR_SDL2_IMAGE=D:/path/to/SDL_image-release-2.8.2
```

引擎会尝试打开音频设备，设备不可用时仍可静音运行。背景音乐、按钮悬停/点击音效和拾取音效素材位于 `assets/audio/`。

地牢图片取自 Kenney 的 [Tiny Dungeon](https://kenney.nl/assets/tiny-dungeon) 资源包，页面标注 Creative Commons CC0；本项目使用其中的砂地、石砖墙、玩家和宝箱精灵，许可文本随素材保存在 `assets/images/dungeon/License.txt`。

## 项目文件

- `PixelEngine/main.cpp`：程序入口和引擎启动。
- `PixelEngine/Engine.hpp`、`PixelEngine/Engine.cpp`：游戏状态、输入、更新、绘制、菜单协调与记录存取。
- `PixelEngine/Core/Roles.hpp`：角色表（名字/贴图路径/简介）与角色选择页卡片布局。
- `PixelEngine/Graphics/GameUI.hpp/.cpp`：菜单界面 + 游戏内文字（ImGui 渲染，动作通过回调委托）。
- `PixelEngine/Graphics/ImGuiUI.hpp/.cpp`：Dear ImGui 基础设施（多字号中文字体、帧边界）。
- `PixelEngine/Graphics/TextConfig.hpp/.cpp`：文字样式配置（ini 读写，16 个 key）。
- `PixelEngine/Graphics/TextTweakerUI.hpp/.cpp`：F2 实时调参面板。
- `PixelEngine/Graphics/UIRenderer.hpp/.cpp`：游戏内图形（HUD 红心/时间条/金币点、武器面板、结算面板背景）。
- `PixelEngine/MazeGenerator.hpp/.cpp`：随机生成含环路、扩展死路和可通行主路线的编织迷宫。
- `PixelEngine/map1.txt`：限时模式地图；`map2.txt`：迷宫模式地图文件，运行时会生成随机迷宫。
- `role_saves.txt`（运行时生成于工作目录）：当前选中角色索引存档。
- `PixelEngine/CMakeLists.txt`：应用与第三方依赖的构建配置。

## 后续修改提示

新增菜单页面时，检查 `Engine.hpp` 中的状态定义，以及 `Engine.cpp` 中对应的输入命中、页面绘制和状态切换逻辑。调整游戏数据格式时，同时检查读取、保存和历史页面显示。若整理构建配置，建议将本机绝对路径改为可配置的 CMake 参数或标准依赖查找，并同步更新 `.vscode/tasks.json`。
