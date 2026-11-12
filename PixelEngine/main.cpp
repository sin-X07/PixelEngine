#include <ctime>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>
#include "Engine.hpp"
#include "Core/BuildInfo.hpp"
#include <iostream>
#include "Core/PathUtils.hpp"
#ifdef _WIN32
#include "LLM/LLMBotController.hpp"
#endif

int main(int argc, char* argv[]) {
    // 设置 Windows 控制台的 UTF-8 编码以支持中文输出
#ifdef _WIN32
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
#endif

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
    // --mode=N  ：0=死斗 1=团队歼灭 2=爆破
    bool autostart = false;
    bool fastRound = false;
    int  startMode = 1;
    // LLM 教练默认值：自动检测。
    //   - 检测到 llm_bot 的 llama-server 与模型 → 默认开启（本机/内测包开箱即用）
    //   - 没有模型/显卡的机器 → 默认关闭，避免每次进图都卡在"加载战术服务器"
    // 显式参数优先：--coach=llm 强制开，--coach=rule 强制关。
    bool coachLlm = false;
    bool llmAutoAvailable = false;
#ifdef _WIN32
    {
        const std::string cfgPath = PathUtils::ResolveAppPath("assets/config/llm.ini");
        const LLMConfig cfg = LLMConfig::FromFile(cfgPath);
        const std::string exePath = cfg.serverExePath.empty()
                                        ? std::string()
                                        : PathUtils::ResolveAppPath(cfg.serverExePath);
        const std::string modelPath = cfg.modelPath.empty()
                                          ? std::string()
                                          : PathUtils::ResolveAppPath(cfg.modelPath);
        std::error_code ec1, ec2;
        const bool haveExe = !exePath.empty() && std::filesystem::exists(exePath, ec1);
        const bool haveModel = !modelPath.empty() && std::filesystem::exists(modelPath, ec2);
        coachLlm = cfg.autoLaunch && haveExe && haveModel;
        llmAutoAvailable = coachLlm;
    }
#endif
    std::string startMap;   // --map=<id> 指定地图（kMapCatalog 的 id）
    bool openMapSelect = false;  // --mapselect 直接打开地图选择界面（调试）
    bool openHistory = false;    // --history 直接打开历史记录（调试）
    std::string sideArg;        // --side=t|ct 指定玩家阵营（调试）
    std::string loadoutArg;     // --loadout=<武器名/编号> 进入对局后直接装备（调试）
    bool openBuyMenu = false;   // --buymenu 进入对局后自动打开购买菜单（调试）
    bool openSettings = false;  // --settings 直接打开设置页（验收截图用）
    std::string recordPath;     // --record=<path> 录制输入日志（dev 构建）
    bool killPlayer = false;    // --killplayer 进对局 2s 后杀死玩家（验收死亡镜头用）
    bool coachExplicit = false;
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
            coachExplicit = true;
        }
        if (a.rfind("--map=", 0) == 0) startMap = a.substr(6);
        if (a == "--mapselect") openMapSelect = true;
        if (a == "--history") openHistory = true;
        if (a.rfind("--side=", 0) == 0) {
            sideArg = a.substr(7);
        }
        if (a.rfind("--loadout=", 0) == 0) {
            loadoutArg = a.substr(10);
        }
        if (a == "--buymenu") openBuyMenu = true;
        if (a == "--settings") openSettings = true;
        if (BuildInfo::kDevTools && a == "--killplayer") killPlayer = true;
        if (a.rfind("--record=", 0) == 0) recordPath = a.substr(9);
    }

    std::cout << "[Build] " << BuildInfo::Display() << std::endl;
    if (coachLlm) {
        std::cout << "[Build] LLM 教练: 开启"
                  << (coachExplicit ? "（--coach=llm）" : "（自动检测到 llama-server 与模型）")
                  << std::endl;
    } else {
        std::cout << "[Build] LLM 教练: 关闭"
                  << (coachExplicit ? "（--coach=rule）"
                                    : "（未检测到 llama-server/模型；可用 --coach=llm 强制开启）")
                  << std::endl;
    }
    engine.SetDebugLoadout(loadoutArg);
    engine.SetDebugBuyMenu(openBuyMenu);
    engine.SetDebugKillPlayer(killPlayer);
#ifdef PIXEL_DEV_REPLAY
    if (!recordPath.empty()) engine.SetRecordPath(recordPath);
#endif

    if (engine.Initialize(("CS2D Pixel " + BuildInfo::Display()).c_str(), 800, 600, autostart, fastRound, startMode, coachLlm, startMap, openMapSelect, openHistory, sideArg, openSettings)) {
        engine.Run();
    }

    engine.Clean();
    return 0;
}
