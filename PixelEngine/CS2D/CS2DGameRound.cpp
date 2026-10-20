#include "CS2DGame.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// 回合流程
// ============================================================================

void CS2DGame::UpdateRoundFlow(float dt, AudioManager* /*audio*/) {
    if (mode_ != MatchMode::TeamElimination) return;

    const RoundPhase before = round_.Phase();
    round_.Update(dt);
    const RoundPhase after = round_.Phase();

    // 结算结束 → 开新回合
    if (before == RoundPhase::RoundEnd && after == RoundPhase::Freeze) {
        BeginNewRound();
    }
    prevPhase_ = after;
}

void CS2DGame::BeginNewRound() {
    if (teams_.MatchOver()) return;
    RespawnAllForRound();
    std::cout << "[CS2D] 第 " << round_.RoundNumber() << " 回合开始 ("
              << teams_.ScoreT() << ":" << teams_.ScoreCT() << ")" << std::endl;
}

void CS2DGame::RespawnAllForRound() {
    // 玩家
    player_.health    = player_.maxHealth;
    player_.alive     = true;
    player_.reloading = false;
    player_.reloadTimer = 0.0f;
    player_.ammo      = player_.maxAmmo;
    player_.deathTimer = 0.0f;
    player_.vx = player_.vy = 0.0f;
    PickSpawnForTeam(Team::CT, player_.x, player_.y);
    slotAmmo_[0] = 30; slotMaxAmmo_[0] = 30;
    slotAmmo_[1] = 7;  slotMaxAmmo_[1] = 7;

    // BOT
    for (auto& bot : bots_) {
        SpawnBOT(bot, bot.team);
    }
    bullets_.clear();
}

void CS2DGame::CheckWinConditions(AudioManager* audio) {
    if (mode_ != MatchMode::TeamElimination) return;
    if (!round_.IsLive()) return;
    if (teams_.MatchOver()) return;

    const int aliveT  = AliveCount(Team::T);
    const int aliveCT = AliveCount(Team::CT);

    Team winner = Team::None;
    bool decided = false;

    if (aliveT == 0 || aliveCT == 0) {
        decided = true;
        if (aliveT == 0 && aliveCT == 0)      winner = Team::None;   // 同归于尽 → 平局
        else if (aliveT == 0)                 winner = Team::CT;
        else                                  winner = Team::T;
    } else if (round_.IsTimeUp()) {
        decided = true;
        winner = Team::CT;   // 时间到：守方（CT）获胜
    }

    if (!decided) return;

    ApplyRoundResult(winner);
    round_.EndRound(winner);
}

void CS2DGame::ApplyRoundResult(Team winner) {
    if (winner != Team::None) {
        teams_.AddScore(winner);
    }

    GrantRoundEndMoney(winner);

    if (teams_.MatchOver() && !matchOverAnnounced_) {
        matchOverAnnounced_ = true;
        std::cout << "[CS2D] 比赛结束! " << TeamName(teams_.MatchWinner())
                  << " 获胜 (" << teams_.ScoreT() << ":" << teams_.ScoreCT() << ")" << std::endl;
    }
}

// 回合结算：发放金钱，并重置为下一回合的初始装备
void CS2DGame::GrantRoundEndMoney(Team winner) {
    // 玩家（CT）
    player_.money += economy_.GrantRoundEnd(winner, player_.team);
    economy_.Clamp(player_.money);

    std::cout << "[Economy] 回合结算 → 玩家 $" << player_.money << std::endl;
}

int CS2DGame::AliveCount(Team t) const {
    int n = 0;
    if (t == Team::None) return 0;
    if (player_.alive && player_.team == t) ++n;
    for (const auto& b : bots_) {
        if (b.alive && b.team == t) ++n;
    }
    return n;
}

