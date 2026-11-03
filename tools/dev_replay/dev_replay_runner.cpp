// ============================================================================
// dev_replay_runner —— 开发期确定性/回放工具（P0 骨架）
//
// 目的：在不启动渲染器与音频的前提下，用固定步长跑一整段仿真，
//       为后续的「逐 tick 哈希」「AI 冲突检查」「人类输入回放」提供载体。
//
// 本文件是**外挂工具**：只使用 CS2DGame 的 public API，不修改游戏逻辑。
// 删除方式：整个 tools/dev_replay/ 目录删除 + CMake 的 BUILD_DEV_REPLAY_TOOLS
//          选项块删除即可，游戏本体无残留。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"
#include "../../PixelEngine/Core/PathUtils.hpp"
#include "scripted_player.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct Options {
    int   ticks    = 60 * 30;      // 默认跑 30 秒（1800 tick）
    int   seed     = 1;
    std::string map = "dust_2";
    int   viewportW = 1728;         // 固定逻辑视口（16:10 下的默认逻辑尺寸）
    int   viewportH = 1080;
    float aimTurnRate = 3.0f;    // 脚本玩家转身速率 rad/s
    float aimErrorDeg = 2.5f;    // 脚本玩家瞄准误差（±度）
    int   reactionTicks = 15;
};

Options ParseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i] ? argv[i] : "";
        if (a.rfind("--ticks=", 0) == 0)      o.ticks = std::atoi(a.c_str() + 8);
        else if (a.rfind("--seed=", 0) == 0)  o.seed  = std::atoi(a.c_str() + 7);
        else if (a.rfind("--map=", 0) == 0)   o.map   = a.substr(6);
        else if (a.rfind("--aim-turn=", 0) == 0) o.aimTurnRate = static_cast<float>(std::atof(a.c_str() + 11));
        else if (a.rfind("--reaction=", 0) == 0) o.reactionTicks = std::atoi(a.c_str() + 11);
        else if (a.rfind("--aim-error=", 0) == 0) o.aimErrorDeg = static_cast<float>(std::atof(a.c_str() + 12));
        else if (a.rfind("--viewport=", 0) == 0) {
            const std::string v = a.substr(11);
            const size_t x = v.find('x');
            if (x != std::string::npos) {
                o.viewportW = std::atoi(v.substr(0, x).c_str());
                o.viewportH = std::atoi(v.substr(x + 1).c_str());
            }
        }
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = ParseArgs(argc, argv);

    CS2DGame game;
    game.SetMatchMode(MatchMode::BombDefusal);
    game.SetRngSeed(static_cast<uint32_t>(opt.seed));

    const std::string mapPath =
        PathUtils::ResolveAppPath("assets/cs2d/maps/" + opt.map + ".tmj");
    // 关键：renderer 传 nullptr —— 玩法层的视线/可见性已经走 VisibilityQuery，
    // 不再依赖 CS2DRenderer（这是 P0 抽离的目的）。
    if (!game.LoadMap(mapPath, nullptr, true)) {
        std::fprintf(stderr, "[dev_replay] 地图加载失败: %s\n", mapPath.c_str());
        return 1;
    }
    if (!game.IsDeathMatch()) game.Reset();

    // 固定视口 → 视野半径（与渲染器同一套公式，保证判定一致）
    game.Visibility().SetVisionRadius(
        VisibilityQuery::RadiusForViewport(opt.viewportW, opt.viewportH));

    const float dt = 1.0f / 60.0f;

    int aliveBotsFirst = 0;
    for (const auto& b : game.GetBOTs()) if (b.alive) ++aliveBotsFirst;

    // 脚本玩家：第 10 个单位也由脚本驱动，保住 5v5 对称（不藏玩家、不做 4v5 旁路）
    dev_replay::ScriptedPlayer::Params sp;
    sp.turnRate = opt.aimTurnRate;
    sp.reactionTicks = opt.reactionTicks;
    sp.aimErrorDeg = opt.aimErrorDeg;
    dev_replay::ScriptedPlayer script(sp);
    for (int t = 0; t < opt.ticks; ++t) {
        const auto in = script.Think(game, dt);
        game.Update(dt, in.mouseDown, in.aimX, in.aimY,
                    in.w, in.a, in.s, in.d,
                    in.shift, in.reload, false,
                    nullptr);
    }

    int aliveBotsLast = 0;
    for (const auto& b : game.GetBOTs()) if (b.alive) ++aliveBotsLast;

    std::printf("[dev_replay] map=%s seed=%d viewport=%dx%d ticks=%d\n",
                opt.map.c_str(), opt.seed, opt.viewportW, opt.viewportH, opt.ticks);
    std::printf("[dev_replay] bots=%zu alive(first)=%d alive(last)=%d  visionRadius=%.2f\n",
                game.GetBOTs().size(), aliveBotsFirst, aliveBotsLast,
                game.Visibility().VisionRadius());
    std::printf("[dev_replay] player kills=%d deaths=%d money=%d\n",
                game.GetPlayerKills(), game.GetPlayerDeaths(), game.GetPlayerMoney());
    std::printf("[dev_replay] score T=%d CT=%d round=%d phase=%d\n",
                game.GetScoreT(), game.GetScoreCT(),
                game.GetRoundNumber(), static_cast<int>(game.GetRoundPhase()));
    return 0;
}
