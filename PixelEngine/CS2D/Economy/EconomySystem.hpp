#pragma once
#include "../../Core/Types.hpp"
#include <algorithm>

// ============================================================================
// EconomySystem — 金钱规则（照抄 CS2 原版）
//
// 数值来源：CS2 (2024) 竞技模式
//   起始金          $800（每半场开始）
//   上限            $16000
//   胜利奖励        $3250
//   败方补偿        $1400 / $1900 / $2400 / $2900 / $3400（连败递增，$3400 封顶）
//   击杀奖励        按武器类型（步枪/手枪/机枪/手雷 $300，SMG $600（P90 $300），
//                   霰弹枪 $900，刀 $1500，狙击枪 $100，宙斯 $0）
//   下包奖励        T 全员 +$300（下包成功时）
//   拆包奖励        拆包者 +$300（仅个人，不给全队）
//
// 只负责计算；money 字段由 CS2DGame 持有。
// ============================================================================

struct EconomyConfig {
    // ---- 开局 / 上限 ----
    int startMoney   = 800;
    int maxMoney     = 16000;

    // ---- 回合胜负 ----
    int winReward    = 3250;
    int lossBase     = 1400;   // 败方基础补偿
    int lossStep     = 500;    // 每连败额外补偿
    int lossMaxStep  = 4;      // 连败加成上限档（→ 第 5 连败起 $3400）

    // ---- 目标奖励 ----
    int plantBonus   = 300;    // 下包：T 全员每人 +$300
    int defuseBonus  = 300;    // 拆包：拆包者个人 +$300
};

class EconomySystem {
public:
    const EconomyConfig& Config() const { return cfg_; }

    void Reset() {
        lossStreakT_  = 0;
        lossStreakCT_ = 0;
    }

    int StartMoney() const { return cfg_.startMoney; }

    // ---- 击杀奖励：由武器类型决定（照抄 CS2）----
    // 调用方传入武器定义里的 killReward；这里只做合法性兜底。
    int GrantKill(int weaponKillReward) const {
        return std::max(0, weaponKillReward);
    }

    // ---- 目标奖励 ----
    int GrantPlant()  const { return cfg_.plantBonus; }
    int GrantDefuse() const { return cfg_.defuseBonus; }

    // ---- 回合结束奖励 ----
    // winner: 胜方；myTeam: 本队阵营
    // 返回应发放的金额，并维护连败计数（T / CT 各自独立）
    int GrantRoundEnd(Team winner, Team myTeam) {
        if (myTeam == Team::None) return 0;

        int& streak = (myTeam == Team::T) ? lossStreakT_ : lossStreakCT_;

        if (winner == myTeam) {
            streak = 0;
            return cfg_.winReward;
        }

        // 败方：基础 + 连败加成（$1400 → $3400 封顶）
        const int bonus = std::min(streak, cfg_.lossMaxStep) * cfg_.lossStep;
        if (streak < cfg_.lossMaxStep) ++streak;
        return cfg_.lossBase + bonus;
    }

    // 当前连败档位（0..lossMaxStep），供 HUD / 调试输出
    int LossStreak(Team t) const {
        if (t == Team::T)  return lossStreakT_;
        if (t == Team::CT) return lossStreakCT_;
        return 0;
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
