# PixelEngine

一个仍在开发中的 C++17 / SDL2 像素风迷宫小游戏。当前优先保持代码和构建说明易于修改；功能、文件布局和依赖配置都可能继续调整。

## 当前功能

- 主菜单、模式选择、历史记录和按键说明页面。
- 限时挑战：在 30 秒内收集金币并累计分数。
- 迷宫探索：生成随机迷宫，记录通关时间。
- F11 全屏切换，历史记录保存在纯文本文件中。

## 操作

- 菜单：鼠标左键；模式选择页也可按 `1` 或 `2` 开始对应模式。
- 移动：`W` / `A` / `S` / `D` 或方向键。
- 全屏：`F11`。
- 返回：子页面按 `Esc` 返回主菜单；主菜单按 `Esc` 退出。
- 结算页面：按 `Enter` 或空格返回模式选择。

## 构建与运行

项目使用 CMake、C++17、MinGW-w64、SDL2、SDL2_ttf 和 FreeType。中文界面使用 Windows 系统字体。

当前 Windows 开发环境的源码路径和工具链路径配置在 `PixelEngine/CMakeLists.txt` 与 `.vscode/tasks.json` 中，并非跨机器通用配置。该配置预期 SDL2 源码位于仓库的 `build/_deps/sdl2-src`，FreeType 和 SDL2_ttf 源码位于 `D:/PixelEngineBuild-ttf/_deps/`。在这台机器上可运行 VS Code 的 **CMake: Configure** 和 **CMake: Build** 任务。

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