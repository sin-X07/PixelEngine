# PixelEngine

一个仍在开发中的 C++17 / SDL2 像素风迷宫小游戏。当前优先保持代码和构建说明易于修改；功能、文件布局和依赖配置都可能继续调整。

## 当前功能

- 主菜单、模式选择、历史记录、按键说明和音量设置页面。
- 限时挑战：在 30 秒内收集金币并累计分数。
- 迷宫探索：生成随机迷宫，记录通关时间。
- 游戏中按 `Esc` 暂停，可继续、重新开始、调整音量或确认放弃。
- F11 全屏切换；历史记录保存在纯文本文件中。

## 操作

- 菜单：鼠标左键；模式选择页也可按 `1` 或 `2` 开始对应模式。点击空白处不会播放点击音效。
- 移动：`W` / `A` / `S` / `D` 或方向键。
- 全屏：`F11`。
- 暂停/继续：游戏中按 `Esc` 暂停，再按 `Esc` 或选择“继续游戏”恢复。
- 静音：`F1` 切换主音量静音；主音量、音乐和音效可在“设置”中分别调节。
- 返回：子页面按 `Esc` 返回；主菜单按 `Esc` 退出；结算页面按 `Esc` 返回模式选择。
- 结算页面：按 `Enter` 或空格返回模式选择。

## 构建与运行

项目使用 CMake、C++17、MinGW-w64、SDL2、SDL2_ttf、FreeType 和 SDL2_mixer。SDL2_mixer 固定为 2.6.3，并静态链接；引擎会尝试打开音频设备，设备不可用时仍可静音运行。背景音乐、按钮悬停/点击音效和拾取音效素材位于 `assets/audio/`。

当前 Windows 开发环境的源码路径和工具链路径配置在 `PixelEngine/CMakeLists.txt` 与 `.vscode/tasks.json` 中，并非跨机器通用配置。该配置预期 SDL2 源码位于仓库的 `build/_deps/sdl2-src`，FreeType 和 SDL2_ttf 源码位于 `D:/PixelEngineBuild-ttf/_deps/`。SDL2_mixer 首次配置时从 GitHub 获取固定版本，验证 SHA-256 后解压到构建目录的 `_deps`；默认总超时为 300 秒、无数据活动超时为 60 秒。后续配置会复用已下载源码。

若在线下载失败，CMake 会尝试仓库中的 `build/_deps/SDL2_mixer-src` 本地源码缓存。也可以手动下载并解压 SDL_mixer 2.6.3，然后通过 `PIXELENGINE_SDL2_MIXER_SOURCE_DIR` 指定源码目录；设置有效目录后会直接使用本地源码。下载等待时间可通过 `PIXELENGINE_SDL2_MIXER_DOWNLOAD_TIMEOUT` 和 `PIXELENGINE_SDL2_MIXER_INACTIVITY_TIMEOUT` 调整。若线上与本地源码都不可用，配置会提示所需目录和参数。

在这台机器上可运行 VS Code 的 **CMake: Configure** 和 **CMake: Build** 任务。若需指定本地 mixer 源码，在配置命令中增加：

```powershell
-DPIXELENGINE_SDL2_MIXER_SOURCE_DIR=D:/path/to/SDL_mixer-release-2.6.3
```

也可在仓库根目录执行当前环境对应的命令：

```powershell
cmake -S PixelEngine -B D:/PixelEngineBuild-Local -G "MinGW Makefiles" -DPIXELENGINE_FREETYPE_SOURCE_DIR=D:/PixelEngineBuild-ttf/_deps/freetype-src -DPIXELENGINE_SDL2_TTF_SOURCE_DIR=D:/PixelEngineBuild-ttf/_deps/sdl2_ttf-src
cmake --build D:/PixelEngineBuild-Local --target EngineApp -- -j4
```

生成的程序为 `D:/PixelEngineBuild-Local/EngineApp.exe`。在其他机器构建前，需要先调整 CMake 和 VS Code 任务中的 MinGW、依赖源码及运行时 DLL 路径。

## 项目文件

- `PixelEngine/main.cpp`：程序入口和引擎启动。
- `PixelEngine/Engine.hpp`、`PixelEngine/Engine.cpp`：游戏状态、输入、更新、绘制、菜单和记录存取。
- `PixelEngine/MazeGenerator.hpp`：随机迷宫生成。
- `PixelEngine/map1.txt`：限时模式地图；`map2.txt`：迷宫模式地图文件，运行时会生成随机迷宫。
- `PixelEngine/CMakeLists.txt`：应用与第三方依赖的构建配置。

## 后续修改提示

新增菜单页面时，检查 `Engine.hpp` 中的状态定义，以及 `Engine.cpp` 中对应的输入命中、页面绘制和状态切换逻辑。调整游戏数据格式时，同时检查读取、保存和历史页面显示。若整理构建配置，建议将本机绝对路径改为可配置的 CMake 参数或标准依赖查找，并同步更新 `.vscode/tasks.json`。