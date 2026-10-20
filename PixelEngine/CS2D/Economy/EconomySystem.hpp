#pragma once
#include "../../Core/Types.hpp"
#include <algorithm>

// ============================================================================
// EconomySystem — 金钱规则（CS 风格）
//
//   - 起始金 / 上限
//   - 击杀奖励
//   - 回合胜负奖励（败方连续失败递增）
//
// 只负责计算；money 字段由 CS2DGame 持有。
// ============================================================================

struct EconomyConfig {
    int startMoney   = 800;
    int maxMoney     = 16000;
    int killReward   = 300;
    int winReward    = 3250;
    int lossBase     = 1400;   // 败方基础补偿
    int lossStep     = 500;    // 每连败额外补偿
    int lossMaxStep  = 4;      // 连败加成上限档
};

class EconomySystem {
public:
    const EconomyConfig& Config() const { return cfg_; }

    void Reset() {
        lossStreakT_  = 0;
        lossStreakCT_ = 0;
    }

    int StartMoney() const { return cfg_.startMoney; }

    // 击杀奖励
    int GrantKill() const { return cfg_.killReward; }

    // 回合结束奖励（winner: 胜方；myTeam: 本队阵营）
    // 返回应发放的金额，并维护连败计数
    int GrantRoundEnd(Team winner, Team myTeam) {
        if (myTeam == Team::None) return 0;

        int& streak = (myTeam == Team::T) ? lossStreakT_ : lossStreakCT_;

        if (winner == myTeam) {
            streak = 0;
            return cfg_.winReward;
        }

        // 败方：基础 + 连败加成
        const int bonus = std::min(streak, cfg_.lossMaxStep) * cfg_.lossStep;
        if (streak < cfg_.lossMaxStep) ++streak;
        return cfg_.lossBase + bonus;
    }

    // 夹取到合法范围
    void Clamp(int& money) const {
        money = std::clamp(money, 0, cfg_.maxMoney);
    }

private:
    EconomyConfig cfg_;
    int lossStreakT_  = 0;
    int lossStreakCT_ = 0;
};
