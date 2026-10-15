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

项目使用 CMake 3.16+、C++17、MinGW-w64、SDL2、SDL2_image 和 SDL2_mixer，另 vendor 了 Dear ImGui（MIT）用于文字与菜单渲染。SDL2_mixer 固定为 2.6.3；SDL2_image 固定为 2.8.2，静态构建并使用内置 stb 后端解码 PNG/JPG。SDL2_image 源码会在首次配置时下载并校验 SHA-256，也可通过 `PIXELENGINE_SDL2_IMAGE_SOURCE_DIR` 指向本地源码目录。引擎会尝试打开音频设备，设备不可用时仍可静音运行。背景音乐、按钮悬停/点击音效和拾取音效素材位于 `assets/audio/`。

地牢图片取自 Kenney 的 [Tiny Dungeon](https://kenney.nl/assets/tiny-dungeon) 资源包，页面标注 Creative Commons CC0；本项目使用其中的砂地、石砖墙、玩家和宝箱精灵，许可文本随素材保存在 `assets/images/dungeon/License.txt`。

当前 Windows 开发环境的源码路径和工具链路径配置在 `PixelEngine/CMakeLists.txt` 与 `.vscode/tasks.json` 中，并非跨机器通用配置。该配置预期 SDL2 源码位于仓库的 `build/_deps/sdl2-src`。SDL2_mixer 首次配置时从 GitHub 获取固定版本，验证 SHA-256 后解压到构建目录的 `_deps`；默认总超时为 300 秒、无数据活动超时为 60 秒。后续配置会复用已下载源码。

若在线下载失败，CMake 会尝试仓库中的 `build/_deps/SDL2_mixer-src` 本地源码缓存。也可以手动下载并解压 SDL_mixer 2.6.3，然后通过 `PIXELENGINE_SDL2_MIXER_SOURCE_DIR` 指定源码目录；设置有效目录后会直接使用本地源码。下载等待时间可通过 `PIXELENGINE_SDL2_MIXER_DOWNLOAD_TIMEOUT` 和 `PIXELENGINE_SDL2_MIXER_INACTIVITY_TIMEOUT` 调整。若线上与本地源码都不可用，配置会提示所需目录和参数。

在这台机器上可运行 VS Code 的 **CMake: Configure** 和 **CMake: Build** 任务。若需指定本地 mixer 源码，在配置命令中增加：

```powershell
-DPIXELENGINE_SDL2_MIXER_SOURCE_DIR=D:/path/to/SDL_mixer-release-2.6.3
```

也可在仓库根目录执行当前环境对应的命令：

```powershell
cmake -S PixelEngine -B D:/PixelEngineBuild-Local -G "MinGW Makefiles"
cmake --build D:/PixelEngineBuild-Local --target EngineApp -- -j4
```

生成的程序为 `D:/PixelEngineBuild-Local/EngineApp.exe`。在其他机器构建前，需要先调整 CMake 和 VS Code 任务中的 MinGW、依赖源码及运行时 DLL 路径。

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
