#include "RoundManager.hpp"

void RoundManager::Reset(const RoundConfig& cfg) {
    cfg_ = cfg;
    roundNumber_ = 1;
    lastWinner_  = Team::None;
    timeUp_      = false;
    EnterPhase(RoundPhase::Freeze);
}

void RoundManager::EnterPhase(RoundPhase p) {
    phase_ = p;
    if (p == RoundPhase::Freeze) elapsed_ = 0.0f;   // 新回合开始，购买计时归零
    switch (p) {
        case RoundPhase::Freeze:   phaseTimer_ = cfg_.freezeTime; break;
        case RoundPhase::Buy:      phaseTimer_ = cfg_.buyTime;    break;
        case RoundPhase::Live:     phaseTimer_ = cfg_.roundTime;  timeUp_ = false; break;
        case RoundPhase::RoundEnd: phaseTimer_ = cfg_.endTime;    break;
        case RoundPhase::Warmup:
        default:                   phaseTimer_ = 0.0f;            break;
    }
}

void RoundManager::Update(float dt) {
    // 结算阶段结束后进入下一回合
    if (phase_ == RoundPhase::RoundEnd) {
        phaseTimer_ -= dt;
        if (phaseTimer_ <= 0.0f) {
            ++roundNumber_;
            lastWinner_ = Team::None;
            EnterPhase(RoundPhase::Freeze);
        }
        return;
    }

    elapsed_ += dt;          // 计入本回合用时（购买窗口按此判定）
    phaseTimer_ -= dt;
    if (phaseTimer_ > 0.0f) return;

    switch (phase_) {
        case RoundPhase::Freeze:
            EnterPhase(RoundPhase::Buy);
            break;
        case RoundPhase::Buy:
            EnterPhase(RoundPhase::Live);
            break;
        case RoundPhase::Live:
            // 时间到：只标记，具体胜负（守方胜/平局）由模式规则决定
            phaseTimer_ = 0.0f;
            timeUp_ = true;
            break;
        default:
            break;
    }
}

void RoundManager::EndRound(Team winner) {
    if (phase_ == RoundPhase::RoundEnd) return;   // 避免重复结算
    lastWinner_ = winner;
    timeUp_ = false;
    EnterPhase(RoundPhase::RoundEnd);
}
