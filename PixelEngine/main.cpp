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
    // --fastround：使用短回合时长（配合 --autostart 验证回合状态机）
    // --mode=N  ：0=死斗 1=团队歼灭 2=爆破 3=人质
    bool autostart = false;
    bool fastRound = false;
    int  startMode = 1;
    bool coachLlm = true;   // --coach=rule 关闭 LLM 教练（用于对比）
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        const std::string a(argv[i]);
        if (a == "--autostart") autostart = true;
        if (a == "--fastround") fastRound = true;
        if (a.rfind("--mode=", 0) == 0) {
            try { startMode = std::stoi(a.substr(7)); } catch (...) {}
        }
        if (a.rfind("--coach=", 0) == 0) {
            coachLlm = (a.substr(8) != "rule");
        }
    }

    if (engine.Initialize("CS2D Pixel", 800, 600, autostart, fastRound, startMode, coachLlm)) {
        engine.Run();
    }

    engine.Clean();
    return 0;
}
