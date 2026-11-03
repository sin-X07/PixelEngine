#pragma once
// ============================================================================
// metrics —— 回合级 / 整局指标（开发期基准用，纯外挂）
//
// 除了"谁赢了几回合"，还尽量量化 BOT 的决策质量：
//   * 移动质量：原地不动占比、发呆占比、路径效率（直线 / 实际移动）
//   * 交火质量：双方造成的血量伤害、首杀方是否赢下回合
//   * 目标质量：下包/拆包瞬间包点附近的人数
//   * 经济质量：回合结束时双方平均余额、本回合买计划档位
//
// 全部通过 CS2DGame 的公开 getter / Player 字段计算，不写游戏状态。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dev_replay {

struct RoundRow {
    int   round = 0;
    Team  winner = Team::None;
    int   ticks = 0;
    int   killsT = 0;
    int   killsCT = 0;
    bool  plant = false;
    bool  defuse = false;
    int   firstBloodTick = -1;       // 相对回合开始
    Team  firstBloodTeam = Team::None;
    bool  fbWinnerMatched = false;   // 首杀方是否赢下本回合
    int   aliveTEnd = 0;
    int   aliveCTEnd = 0;
    int   moneyTStart = 0;
    int   moneyCTStart = 0;

    // ---- 决策质量 ----
    float stillRatioT = 0.0f;    // 原地不动（且没在交战）时间占比
    float stillRatioCT = 0.0f;
    float idleRatioT = 0.0f;     // 无目标 / 无驻守点 / 无路径 —— "不知道干什么"
    float idleRatioCT = 0.0f;
    float pathEffT = 0.0f;       // 直线位移 / 实际移动距离（1 = 最直）
    float pathEffCT = 0.0f;
    int   damageT = 0;           // T 对 CT 造成的血量伤害
    int   damageCT = 0;
    int   moneyTEnd = 0;         // 回合结束时队伍平均余额
    int   moneyCTEnd = 0;
    int   buyPlanT = 0;          // 买计划档位（BuyPlan 枚举）
    int   buyPlanCT = 0;
    int   plantNearT = 0;        // 下包瞬间 220px 内 T 人数
    int   defuseNearCT = 0;      // 拆包开始瞬间 220px 内 CT 人数
};

class RoundTracker {
public:
    void Begin(const CS2DGame& game, uint64_t tick) {
        roundStartTick_ = tick;
        lastRoundNumber_ = game.GetRoundNumber();
        scoreT_ = game.GetScoreT();
        scoreCT_ = game.GetScoreCT();
        killsStartT_ = KillsOf(game, Team::T);
        killsStartCT_ = KillsOf(game, Team::CT);
        plant_ = false;
        defuse_ = false;
        firstBlood_ = -1;
        firstBloodTeam_ = Team::None;
        moneyT_ = game.GetRoundStartEconomy(Team::T);
        moneyCT_ = game.GetRoundStartEconomy(Team::CT);
        buyPlanT_ = static_cast<int>(game.GetBuyPlan(Team::T));
        buyPlanCT_ = static_cast<int>(game.GetBuyPlan(Team::CT));
        plantNear_ = 0;
        defuseNear_ = 0;
        lastAliveT_ = AliveOf(game, Team::T);
        lastAliveCT_ = AliveOf(game, Team::CT);

        for (int i = 0; i < 2; ++i) {
            teamTicks_[i] = 0;
            teamStill_[i] = 0;
            teamIdle_[i] = 0;
            teamTravel_[i] = 0.0;
            damageTaken_[i] = 0;
        }
        prevHealth_.clear();
        for (const auto* p : AllPlayers(game)) prevHealth_.push_back(p->health);

        acc_.clear();
        acc_.reserve(game.GetBOTs().size());
        for (const auto& b : game.GetBOTs()) {
            BotAccum a;
            a.lastX = b.x;
            a.lastY = b.y;
            a.startX = b.x;
            a.startY = b.y;
            acc_.push_back(a);
        }
        started_ = true;
    }

    void Sample(const CS2DGame& game, uint64_t tick) {
        if (!started_) { Begin(game, tick); return; }

        // 下包 / 拆包：用炸弹计时器（plantProgress 只在下包动作期间非零）
        if (game.GetBombTimer() > 0.0f) {
            if (!plant_) {
                plant_ = true;
                plantNear_ = CountNear(game, Team::T, game.GetBombX(), game.GetBombY(), 220.0f);
            }
            if (!defuse_ && game.GetDefuseProgress() > 0.0f) {
                defuse_ = true;
                defuseNear_ = CountNear(game, Team::CT, game.GetBombX(), game.GetBombY(), 220.0f);
            }
        }

        // 首杀：谁先产生击杀
        const int dT = KillsOf(game, Team::T) - killsStartT_;
        const int dCT = KillsOf(game, Team::CT) - killsStartCT_;
        if (firstBlood_ < 0 && (dT + dCT) > 0) {
            firstBlood_ = static_cast<int>(tick - roundStartTick_);
            firstBloodTeam_ = (dT > dCT) ? Team::T : Team::CT;
        }

        // 伤害：逐玩家血量下降累计到"被伤害方"
        {
            const auto players = AllPlayers(game);
            if (prevHealth_.size() == players.size()) {
                for (size_t i = 0; i < players.size(); ++i) {
                    const int hp = players[i]->health;
                    const int drop = std::max(0, prevHealth_[i] - hp);
                    const int t = TeamIndex(players[i]->team);
                    if (t >= 0 && drop > 0) damageTaken_[t] += drop;
                    prevHealth_[i] = hp;
                }
            }
        }

        // 结算阶段记录存活（回合切换会立刻重生，必须取切换前的值）
        if (game.GetRoundPhase() == RoundPhase::RoundEnd) {
            lastAliveT_  = AliveOf(game, Team::T);
            lastAliveCT_ = AliveOf(game, Team::CT);
        }

        // 移动质量（只统计 BOT；玩家是脚本，单独看意义不大）
        const auto& bots = game.GetBOTs();
        for (size_t i = 0; i < bots.size() && i < acc_.size(); ++i) {
            const auto& b = bots[i];
            const int ti = TeamIndex(b.team);
            if (ti < 0) continue;
            if (!b.alive) { acc_[i].lastX = b.x; acc_[i].lastY = b.y; continue; }

            const float dx = b.x - acc_[i].lastX;
            const float dy = b.y - acc_[i].lastY;
            const float moved = std::sqrt(dx * dx + dy * dy);
            acc_[i].lastX = b.x;
            acc_[i].lastY = b.y;

            const auto& ai = game.AiAt(i);
            if (!ai.path.empty()) {
                if (!acc_[i].pathing) {
                    acc_[i].pathing = true;
                    acc_[i].epStartX = acc_[i].lastX;
                    acc_[i].epStartY = acc_[i].lastY;
                    acc_[i].epTravel = 0.0;
                }
                acc_[i].epTravel += moved;
            } else if (acc_[i].pathing) {
                acc_[i].pathing = false;
                const float ex = b.x - acc_[i].epStartX;
                const float ey = b.y - acc_[i].epStartY;
                acc_[i].epStraightSum += std::sqrt(ex * ex + ey * ey);
                acc_[i].epTravelSum += std::max(acc_[i].epTravel, 0.5);
            }
            ++teamTicks_[ti];
            if (moved > 0.5f) teamTravel_[ti] += moved;   // 过滤原地微抖
            if (moved < 0.15f && !ai.hadTarget) ++teamStill_[ti];
            if (!ai.hasPlanHold && ai.path.empty() && !ai.hadTarget) ++teamIdle_[ti];
        }

        const int rn = game.GetRoundNumber();
        if (rn != lastRoundNumber_) {
            Finalize(game, tick);
            Begin(game, tick);
        }
    }

    void Finish(const CS2DGame& game, uint64_t tick) {
        if (!started_) return;
        Finalize(game, tick);
        started_ = false;
    }

    const std::vector<RoundRow>& Rows() const { return rows_; }

    static int KillsOf(const CS2DGame& g, Team t) {
        int sum = 0;
        if (g.GetPlayerTeam() == t) sum += g.GetPlayer().kills;
        for (const auto& b : g.GetBOTs()) if (b.team == t) sum += b.kills;
        return sum;
    }
    static int AliveOf(const CS2DGame& g, Team t) {
        int n = 0;
        if (g.GetPlayerTeam() == t && g.GetPlayer().alive) ++n;
        for (const auto& b : g.GetBOTs()) if (b.team == t && b.alive) ++n;
        return n;
    }

private:
    struct BotAccum {
        float lastX = 0.0f, lastY = 0.0f;
        float startX = 0.0f, startY = 0.0f;
        // 寻路 episode：path 从非空到变空算一段，用"直线 / 实际"衡量绕路程度
        bool  pathing = false;
        float epStartX = 0.0f, epStartY = 0.0f;
        double epTravel = 0.0;
        double epStraightSum = 0.0;
        double epTravelSum = 0.0;
    };

    static int TeamIndex(Team t) {
        if (t == Team::T) return 0;
        if (t == Team::CT) return 1;
        return -1;
    }

    static std::vector<const CS2DGame::Player*> AllPlayers(const CS2DGame& g) {
        std::vector<const CS2DGame::Player*> v;
        v.push_back(&g.GetPlayer());
        for (const auto& b : g.GetBOTs()) v.push_back(&b);
        return v;
    }

    static int CountNear(const CS2DGame& g, Team team, float x, float y, float radius) {
        int n = 0;
        auto hit = [&](const CS2DGame::Player& p) {
            if (!p.alive || p.team != team) return;
            const float dx = p.x - x;
            const float dy = p.y - y;
            if (dx * dx + dy * dy <= radius * radius) ++n;
        };
        hit(g.GetPlayer());
        for (const auto& b : g.GetBOTs()) hit(b);
        return n;
    }

    static int AvgMoney(const CS2DGame& g, Team t) {
        int sum = 0, n = 0;
        auto add = [&](const CS2DGame::Player& p) {
            if (p.team != t) return;
            sum += p.money;
            ++n;
        };
        add(g.GetPlayer());
        for (const auto& b : g.GetBOTs()) add(b);
        return n > 0 ? sum / n : 0;
    }

    void Finalize(const CS2DGame& game, uint64_t tick) {
        RoundRow row;
        row.round = lastRoundNumber_;
        const int dT = game.GetScoreT() - scoreT_;
        const int dCT = game.GetScoreCT() - scoreCT_;
        row.winner = (dT > dCT) ? Team::T : (dCT > dT ? Team::CT : Team::None);
        row.ticks = static_cast<int>(tick - roundStartTick_);
        row.killsT = KillsOf(game, Team::T) - killsStartT_;
        row.killsCT = KillsOf(game, Team::CT) - killsStartCT_;
        row.plant = plant_;
        row.defuse = defuse_;
        row.firstBloodTick = firstBlood_;
        row.firstBloodTeam = firstBloodTeam_;
        row.fbWinnerMatched = (firstBloodTeam_ != Team::None && row.winner == firstBloodTeam_);
        row.aliveTEnd = lastAliveT_;
        row.aliveCTEnd = lastAliveCT_;
        row.moneyTStart = moneyT_;
        row.moneyCTStart = moneyCT_;
        row.damageT = damageTaken_[1];   // T 造成的伤害 = CT 承受
        row.damageCT = damageTaken_[0];
        row.moneyTEnd = AvgMoney(game, Team::T);
        row.moneyCTEnd = AvgMoney(game, Team::CT);
        row.buyPlanT = buyPlanT_;
        row.buyPlanCT = buyPlanCT_;
        row.plantNearT = plantNear_;
        row.defuseNearCT = defuseNear_;

        auto ratio = [](int a, int b) {
            return b > 0 ? static_cast<float>(a) / static_cast<float>(b) : 0.0f;
        };
        row.stillRatioT = ratio(teamStill_[0], teamTicks_[0]);
        row.stillRatioCT = ratio(teamStill_[1], teamTicks_[1]);
        row.idleRatioT = ratio(teamIdle_[0], teamTicks_[0]);
        row.idleRatioCT = ratio(teamIdle_[1], teamTicks_[1]);

        // 路径效率：只统计"寻路 episode"，避免把原地微抖算成绕路
        double straight[2] = {0.0, 0.0};
        double traveled[2] = {0.0, 0.0};
        const auto& bots = game.GetBOTs();
        for (size_t i = 0; i < bots.size() && i < acc_.size(); ++i) {
            const int ti = TeamIndex(bots[i].team);
            if (ti < 0) continue;
            straight[ti] += acc_[i].epStraightSum;
            traveled[ti] += acc_[i].epTravelSum;
        }
        auto eff = [](double st, double tr) {
            if (tr < 1.0) return 0.0f;
            return static_cast<float>(std::min(1.0, st / tr));
        };
        row.pathEffT = eff(straight[0], traveled[0]);
        row.pathEffCT = eff(straight[1], traveled[1]);

        rows_.push_back(row);
    }

    bool     started_ = false;
    uint64_t roundStartTick_ = 0;
    int      lastRoundNumber_ = 0;
    int      scoreT_ = 0, scoreCT_ = 0;
    int      killsStartT_ = 0, killsStartCT_ = 0;
    bool     plant_ = false, defuse_ = false;
    int      firstBlood_ = -1;
    Team     firstBloodTeam_ = Team::None;
    int      moneyT_ = 0, moneyCT_ = 0;
    int      buyPlanT_ = 0, buyPlanCT_ = 0;
    int      plantNear_ = 0, defuseNear_ = 0;
    int      lastAliveT_ = 0, lastAliveCT_ = 0;

    int      teamTicks_[2] = {0, 0};
    int      teamStill_[2] = {0, 0};
    int      teamIdle_[2] = {0, 0};
    double   teamTravel_[2] = {0.0, 0.0};
    int      damageTaken_[2] = {0, 0};

    std::vector<int> prevHealth_;
    std::vector<BotAccum> acc_;
    std::vector<RoundRow> rows_;
};

inline const char* TeamTag(Team t) {
    switch (t) {
        case Team::T:  return "T";
        case Team::CT: return "CT";
        default:       return "-";
    }
}

inline bool WriteRoundsCsv(const std::string& path, const std::vector<RoundRow>& rows) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f,
                 "round,winner,ticks,kills_t,kills_ct,plant,defuse,first_blood_tick,"
                 "first_blood_team,fb_win,alive_t_end,alive_ct_end,money_t_start,"
                 "money_ct_start,still_t,still_ct,idle_t,idle_ct,path_eff_t,path_eff_ct,"
                 "damage_t,damage_ct,money_t_end,money_ct_end,buy_t,buy_ct,"
                 "plant_near_t,defuse_near_ct\n");
    for (const auto& r : rows) {
        std::fprintf(f,
                     "%d,%s,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,"
                     "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                     "%d,%d,%d,%d,%d,%d,%d,%d\n",
                     r.round, TeamTag(r.winner), r.ticks, r.killsT, r.killsCT,
                     r.plant ? 1 : 0, r.defuse ? 1 : 0, r.firstBloodTick,
                     TeamTag(r.firstBloodTeam), r.fbWinnerMatched ? 1 : 0,
                     r.aliveTEnd, r.aliveCTEnd, r.moneyTStart, r.moneyCTStart,
                     r.stillRatioT, r.stillRatioCT, r.idleRatioT, r.idleRatioCT,
                     r.pathEffT, r.pathEffCT, r.damageT, r.damageCT,
                     r.moneyTEnd, r.moneyCTEnd, r.buyPlanT, r.buyPlanCT,
                     r.plantNearT, r.defuseNearCT);
    }
    std::fclose(f);
    return true;
}

}  // namespace dev_replay
