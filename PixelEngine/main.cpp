#include<ctime>
#include<cstdlib>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "Engine.hpp"
#include "MazeGenerator.hpp"

int main(int argc, char* argv[]) {
    // 设置 Windows 控制台的 UTF-8 编码以支持中文输出
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    
    srand(static_cast<unsigned int>(time(nullptr)));
    Engine engine;

    // 尝试初始化一个 800x600 的复古像素视窗
    if (engine.Initialize("Pixel Engine", 800, 600)) {
        engine.Run();
    }

    engine.Clean();
    return 0;
}