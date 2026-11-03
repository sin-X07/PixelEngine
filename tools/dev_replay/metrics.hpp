#pragma once
// ============================================================================
// metrics —— 回合级指标（开发期基准用，纯外挂）
//
// 每 tick 采样一次只读状态，产出：
//   * 每个回合一行（胜者、时长、双方击杀、首杀 tick、下包/拆包、经济）
//   * 整局一行汇总（比分、总 tick、final hash、玩家战绩）
//
// 只依赖 CS2DGame 的公开 getter；不做任何写操作。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"

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
    int   firstBloodTick = -1;   // 相对回合开始
    int   aliveTEnd = 0;
    int   aliveCTEnd = 0;
    int   moneyTStart = 0;
    int   moneyCTStart = 0;
};

class RoundTracker {
public:
    void Begin(const CS2DGame& game, uint64_t tick) {
        roundStartTick_ = tick;
        lastRoundNumber_ = game.GetRoundNumber();
        scoreT_ = game.GetScoreT();
        scoreCT_ = game.GetScoreCT();
        killsAtStart_ = TotalKills(game);
        killsStartT_  = KillsOf(game, Team::T);
        killsStartCT_ = KillsOf(game, Team::CT);
        plant_ = false;
        defuse_ = false;
        firstBlood_ = -1;
        moneyT_ = game.GetRoundStartEconomy(Team::T);
        moneyCT_ = game.GetRoundStartEconomy(Team::CT);
        started_ = true;
    }

    void Sample(const CS2DGame& game, uint64_t tick) {
        if (!started_) { Begin(game, tick); return; }

        // 下包：炸弹计时器 > 0 即已安装（plantProgress 只在下包动作期间非零，逐帧采样会漏）
        if (game.GetBombTimer() > 0.0f) {
            plant_ = true;
            if (game.GetDefuseProgress() > 0.0f) defuse_ = true;   // 拆包必然发生在已下包之后
        }
        // 实时存活：只在结算阶段采样——回合切换瞬间会立刻重生，
        // 结算阶段才是"该回合结束时的存活人数"
        if (game.GetRoundPhase() == RoundPhase::RoundEnd) {
            lastAliveT_  = AliveOf(game, Team::T);
            lastAliveCT_ = AliveOf(game, Team::CT);
        }
        if (firstBlood_ < 0 && TotalKills(game) > killsAtStart_) {
            firstBlood_ = static_cast<int>(tick - roundStartTick_);
        }

        const int rn = game.GetRoundNumber();
        if (rn != lastRoundNumber_) {
            // 上一回合结算：按比分变化判胜者
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
            row.aliveTEnd = lastAliveT_;
            row.aliveCTEnd = lastAliveCT_;
            row.moneyTStart = moneyT_;
            row.moneyCTStart = moneyCT_;
            rows_.push_back(row);

            Begin(game, tick);
            killsStartT_ = KillsOf(game, Team::T);
            killsStartCT_ = KillsOf(game, Team::CT);
        }
    }

    // 比赛结束（或到达 tick 上限）时收尾：把最后一个未结算回合也记下来
    void Finish(const CS2DGame& game, uint64_t tick) {
        if (!started_) return;
        const int rn = game.GetRoundNumber();
        RoundRow row;
        row.round = rn;
        const int dT = game.GetScoreT() - scoreT_;
        const int dCT = game.GetScoreCT() - scoreCT_;
        row.winner = (dT > dCT) ? Team::T : (dCT > dT ? Team::CT : Team::None);
        row.ticks = static_cast<int>(tick - roundStartTick_);
        row.killsT = KillsOf(game, Team::T) - killsStartT_;
        row.killsCT = KillsOf(game, Team::CT) - killsStartCT_;
        row.plant = plant_;
        row.defuse = defuse_;
        row.firstBloodTick = firstBlood_;
        row.aliveTEnd = lastAliveT_;
        row.aliveCTEnd = lastAliveCT_;
        row.moneyTStart = moneyT_;
        row.moneyCTStart = moneyCT_;
        rows_.push_back(row);
        started_ = false;
    }

    const std::vector<RoundRow>& Rows() const { return rows_; }

    static int  KillsOf(const CS2DGame& g, Team t) {
        int sum = 0;
        if (g.GetPlayerTeam() == t) sum += g.GetPlayer().kills;
        for (const auto& b : g.GetBOTs()) if (b.team == t) sum += b.kills;
        return sum;
    }
    static int  AliveOf(const CS2DGame& g, Team t) {
        int n = 0;
        if (g.GetPlayerTeam() == t && g.GetPlayer().alive) ++n;
        for (const auto& b : g.GetBOTs()) if (b.team == t && b.alive) ++n;
        return n;
    }
    static int  TotalKills(const CS2DGame& g) {
        return KillsOf(g, Team::T) + KillsOf(g, Team::CT);
    }

private:
    bool     started_ = false;
    uint64_t roundStartTick_ = 0;
    int      lastRoundNumber_ = 0;
    int      scoreT_ = 0, scoreCT_ = 0;
    int      killsAtStart_ = 0;
    int      killsStartT_ = 0, killsStartCT_ = 0;
    bool     plant_ = false, defuse_ = false;
    int      firstBlood_ = -1;
    int      moneyT_ = 0, moneyCT_ = 0;
    int      lastAliveT_ = 0, lastAliveCT_ = 0;
    std::vector<RoundRow> rows_;
};

inline const char* TeamTag(Team t) {
    switch (t) {
        case Team::T:  return "T";
        case Team::CT: return "CT";
        default:       return "-";
    }
}

// rounds.csv：每个回合一行（baseline 的"回合级指标"部分）
inline bool WriteRoundsCsv(const std::string& path, const std::vector<RoundRow>& rows) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "round,winner,ticks,kills_t,kills_ct,plant,defuse,first_blood_tick,"
                    "alive_t_end,alive_ct_end,money_t_start,money_ct_start\n");
    for (const auto& r : rows) {
        std::fprintf(f, "%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                     r.round, TeamTag(r.winner), r.ticks, r.killsT, r.killsCT,
                     r.plant ? 1 : 0, r.defuse ? 1 : 0, r.firstBloodTick,
                     r.aliveTEnd, r.aliveCTEnd, r.moneyTStart, r.moneyCTStart);
    }
    std::fclose(f);
    return true;
}

}  // namespace dev_replay
