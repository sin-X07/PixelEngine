#include <ctime>
#include <cstdlib>
#include <windows.h>
#include <filesystem>
#include "Engine.hpp"
#include "Core/PathUtils.hpp"

int main(int argc, char* argv[]) {
    // 设置 Windows 控制台的 UTF-8 编码以支持中文输出
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    // 以可执行文件所在目录为基准定位资源与存档，
    // 使游戏无论从哪个目录启动都能正确运行（免安装 / 打包友好）。
    if (argc > 0 && argv[0] && argv[0][0] != '\0') {
        std::error_code ec;
        const std::filesystem::path exeDir =
            std::filesystem::absolute(argv[0], ec).parent_path();
        PathUtils::SetBaseDir(exeDir.string());
    }

    srand(static_cast<unsigned int>(time(nullptr)));
    Engine engine;

    // --autostart：跳过菜单直接进入对局（调试 / 回归验证用）
    bool autostart = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::string(argv[i]) == "--autostart") autostart = true;
    }

    if (engine.Initialize("CS2D Pixel", 800, 600, autostart)) {
        engine.Run();
    }

    engine.Clean();
    return 0;
}
