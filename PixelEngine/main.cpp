#include<ctime>
#include<cstdlib>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <filesystem>
#include "Engine.hpp"
#include "MazeGenerator.hpp"
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

    // 尝试初始化一个 800x600 的复古像素视窗
    if (engine.Initialize("Pixel Engine", 800, 600)) {
        engine.Run();
    }

    engine.Clean();
    return 0;
}