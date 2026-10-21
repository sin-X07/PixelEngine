#pragma once
#include "../../Core/Types.hpp"

// ============================================================================
// RoundManager — 回合阶段状态机
//
//   Freeze  (冻结, 不可移动, 可购买)
//     ↓
//   Buy     (购买, 不可移动, 可购买)
//     ↓
//   Live    (交战, 可移动)
//     ↓
//   RoundEnd(结算展示)
//     ↓ 下一回合
//   Freeze ...
//
// 胜负判定由 CS2DGame 依据模式规则完成，随后调用 EndRound(winner)。
// ============================================================================

struct RoundConfig {
    float freezeTime  = 2.0f;    // 冻结（可购买）
    float buyTime     = 6.0f;    // 购买期（仍不可移动）
    float roundTime   = 90.0f;   // 交战期
    float endTime     = 4.0f;    // 回合结算展示
};

class RoundManager {
public:
    void Reset(const RoundConfig& cfg);

    // 推进计时；内部自动做阶段迁移（Live 超时只进入 RoundEnd，胜负由外部判定）
    void Update(float dt);

    RoundPhase Phase() const        { return phase_; }
    float      PhaseRemaining() const { return phaseTimer_; }
    // 本回合已进行的时间（含冻结/购买期），购买窗口按它计时
    float      Elapsed() const      { return elapsed_; }
    int        RoundNumber() const  { return roundNumber_; }
    Team       LastWinner() const   { return lastWinner_; }

    bool IsFrozen() const {
        return phase_ == RoundPhase::Freeze || phase_ == RoundPhase::Buy;
    }
    bool IsLive() const { return phase_ == RoundPhase::Live; }
    bool IsRoundEnd() const { return phase_ == RoundPhase::RoundEnd; }

    // 立即结束当前回合（外部判定出胜负后调用）
    void EndRound(Team winner);

    // Live 阶段是否已超时（供外部判定"时间到"）
    bool IsTimeUp() const { return timeUp_; }
    void ClearTimeUp()    { timeUp_ = false; }

    const RoundConfig& Config() const { return cfg_; }

private:
    void EnterPhase(RoundPhase p);

    RoundConfig cfg_;
    RoundPhase  phase_       = RoundPhase::Freeze;
    float       phaseTimer_  = 0.0f;
    int         roundNumber_ = 1;
    float       elapsed_     = 0.0f;   // 本回合累计时间
    Team        lastWinner_  = Team::None;
    bool        timeUp_      = false;
};
