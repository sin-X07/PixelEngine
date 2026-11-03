#pragma once
// ============================================================================
// conflicts —— AI 决策冲突检查（开发期，纯外挂）
//
// 只读 CS2DGame 的公开状态，每 tick 检查"自相矛盾"，不做任何写操作。
//
// 默认启用的规则（低噪声，能指向真问题）：
//   * defusing_moving       拆包中却在明显移动（>60 px/s）
//   * defusing_unplanted    交战期里拆一个没下包的包（结算/重置瞬间不算）
//   * path_broken           寻路索引越界 / 空路径有索引 / 目标与路径终点相差 >96px
//   * dual_goal             小队 hold 与教练 plan hold 同时存在且相距 >160px
//
// 默认关闭（噪声大，需显式开启）：
//   * duplicate_hold        两名同队 BOT 的计划驻守点几乎重合（StackA 等战术本来就会这样）
//
// 去重：同一 (回合, BOT, 规则) 每回合只报一次，避免 72k tick 里刷几万行。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace dev_replay {

struct ConflictRuleStat {
    std::string name;
    int   count = 0;          // 回合-实例数（去重后）
    uint64_t firstTick = 0;
};

class ConflictChecker {
public:
    // enableNoisy：显式开启 duplicate_hold 这类高噪声规则
    explicit ConflictChecker(std::vector<std::string> ignore = {},
                             bool enableNoisy = false)
        : enableNoisy_(enableNoisy) {
        for (const auto& ig : ignore) ignore_[ig] = true;
        rules_ = {
            {"defusing_moving", {}},
            {"defusing_unplanted", {}},
            {"path_broken", {}},
            {"dual_goal", {}},
            {"duplicate_hold", {}},
        };
    }

    void Check(const CS2DGame& game, uint64_t tick, std::FILE* out) {
        const auto& bots = game.GetBOTs();
        const auto& squad = game.GetSquadOrders();
        const bool planted = game.GetBombTimer() > 0.0f;
        const bool live = game.GetRoundPhase() == RoundPhase::Live;
        const int round = game.GetRoundNumber();

        for (size_t i = 0; i < bots.size(); ++i) {
            const auto& b = bots[i];
            if (!b.alive) continue;
            const auto& ai = game.AiAt(i);

            const float speed = std::sqrt(b.vx * b.vx + b.vy * b.vy);
            if (ai.defusing && speed > 60.0f) {
                Emit(tick, round, static_cast<int>(i), "defusing_moving", speed, out);
            }
            if (ai.defusing && !planted && live) {
                Emit(tick, round, static_cast<int>(i), "defusing_unplanted", 0.0f, out);
            }

            // 寻路一致性：goal 改主意后到重新寻路之间会有几帧"旧路径 + 新目标"，
            // 这是正常过渡；连续 30 tick（0.5s）都对不上才算真的坏。
            bool mismatch = false;
            if (ai.pathIdx < 0 || ai.pathIdx > static_cast<int>(ai.path.size())) mismatch = true;
            else if (ai.path.empty() && ai.pathIdx != 0) mismatch = true;
            else if (!ai.path.empty() && ai.pathIdx < static_cast<int>(ai.path.size())) {
                const auto& end = ai.path.back();
                const double dx = end.first - ai.goalX;
                const double dy = end.second - ai.goalY;
                if (std::sqrt(dx * dx + dy * dy) > 96.0) mismatch = true;
            }
            if (mismatch) {
                ++pathMismatch_[i];
                if (pathMismatch_[i] >= 30) {
                    Emit(tick, round, static_cast<int>(i), "path_broken",
                         static_cast<float>(pathMismatch_[i]), out);
                }
            } else {
                pathMismatch_[i] = 0;
            }

            if (i < squad.size() && squad[i].hasHold && ai.hasPlanHold) {
                const double dx = squad[i].holdX - ai.planHoldX;
                const double dy = squad[i].holdY - ai.planHoldY;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d > 160.0) {
                    Emit(tick, round, static_cast<int>(i), "dual_goal",
                         static_cast<float>(d), out);
                }
            }
        }

        if (!enableNoisy_ || ignore_["duplicate_hold"]) return;
        for (size_t i = 0; i < bots.size(); ++i) {
            if (!bots[i].alive) continue;
            const auto& ai = game.AiAt(i);
            if (!ai.hasPlanHold) continue;
            for (size_t j = i + 1; j < bots.size(); ++j) {
                if (!bots[j].alive || bots[j].team != bots[i].team) continue;
                const auto& aj = game.AiAt(j);
                if (!aj.hasPlanHold) continue;
                const double dx = ai.planHoldX - aj.planHoldX;
                const double dy = ai.planHoldY - aj.planHoldY;
                if (std::sqrt(dx * dx + dy * dy) < 24.0) {
                    Emit(tick, round, static_cast<int>(i), "duplicate_hold", 0.0f, out);
                }
            }
        }
    }

    const std::vector<ConflictRuleStat>& Rules() const { return rules_; }

    int Total() const {
        int n = 0;
        for (const auto& r : rules_) n += r.count;
        return n;
    }

    void PrintSummary() const {
        std::printf("[conflicts] 回合-实例数 = %d", Total());
        bool any = false;
        for (const auto& r : rules_) {
            if (r.count == 0) continue;
            if (!any) { std::printf("（"); any = true; } else std::printf("，");
            std::printf("%s×%d @tick%llu", r.name.c_str(), r.count,
                        static_cast<unsigned long long>(r.firstTick));
        }
        if (any) std::printf("）");
        std::printf("\n");
    }

    static void WriteHeader(std::FILE* out) { std::fprintf(out, "tick,bot,kind,value\n"); }

private:
    void Emit(uint64_t tick, int round, int bot, const char* kind, float value, std::FILE* out) {
        if (ignore_[kind]) return;
        // 同一回合同一 BOT 同一规则只报一次
        const auto key = std::make_tuple(round, bot, std::string(kind));
        if (!seen_.insert(key).second) return;

        for (auto& r : rules_) {
            if (r.name == kind) {
                if (r.count == 0) r.firstTick = tick;
                ++r.count;
                break;
            }
        }
        if (out) std::fprintf(out, "%llu,%d,%s,%.1f\n",
                              static_cast<unsigned long long>(tick), bot, kind, value);
    }

    std::vector<ConflictRuleStat> rules_;
    std::map<std::string, bool> ignore_;
    std::set<std::tuple<int, int, std::string>> seen_;
    std::map<size_t, int> pathMismatch_;   // 每 BOT 连续不一致 tick 数
    bool enableNoisy_ = false;
};

}  // namespace dev_replay
