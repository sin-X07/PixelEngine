#include "CS2DGame.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// 回合流程
// ============================================================================

void CS2DGame::UpdateRoundFlow(float dt, AudioManager* audio) {
    if (mode_ == MatchMode::DeathMatch) return;   // 死斗无回合

    const RoundPhase before = round_.Phase();
    round_.Update(dt);
    const RoundPhase after = round_.Phase();

    // 结算结束 → 开新回合
    if (before == RoundPhase::RoundEnd && after == RoundPhase::Freeze) {
        BeginNewRound(audio);
    }
    prevPhase_ = after;
}

void CS2DGame::BeginNewRound(AudioManager* audio) {
    if (teams_.MatchOver()) return;
    RespawnAllForRound();
    SetupObjective();   // 每回合重置包点 / 人质
    AssignSquadRoles(); // 每回合按新的相对位置重新分工
    bombBeepTimer_ = 0.0f;
    if (audio) audio->PlaySound(SoundEffect::ROUND_START);
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
    if (mode_ == MatchMode::DeathMatch) return;
    if (!round_.IsLive()) return;
    if (round_.IsRoundEnd()) return;
    if (teams_.MatchOver()) return;

    const int aliveT  = AliveCount(Team::T);
    const int aliveCT = AliveCount(Team::CT);

    Team winner = Team::None;
    bool decided = false;

    switch (mode_) {
        case MatchMode::BombDefusal:
            // 拆包/爆炸已在 UpdateObjective 中结算
            if (aliveCT == 0 && aliveT > 0) {
                decided = true; winner = Team::T;             // CT 全灭
            } else if (aliveT == 0 && !bombPlanted_) {
                decided = true; winner = Team::CT;            // T 全灭且未下包
            } else if (round_.IsTimeUp() && !bombPlanted_) {
                decided = true; winner = Team::CT;            // 守方胜
            }
            break;

        case MatchMode::HostageRescue:
            if (aliveCT == 0 && aliveT > 0) {
                decided = true; winner = Team::T;
            } else if (aliveT == 0) {
                decided = true; winner = Team::CT;
            } else if (round_.IsTimeUp()) {
                decided = true; winner = Team::T;             // 人质模式超时由 T 守成功
            }
            break;

        case MatchMode::TeamElimination:
        default:
            if (aliveT == 0 || aliveCT == 0) {
                decided = true;
                if (aliveT == 0 && aliveCT == 0)  winner = Team::None;   // 同归于尽
                else if (aliveT == 0)             winner = Team::CT;
                else                              winner = Team::T;
            } else if (round_.IsTimeUp()) {
                decided = true; winner = Team::CT;            // 时间到守方胜
            }
            break;
    }

    if (!decided) return;

    ApplyRoundResult(winner, audio);
    round_.EndRound(winner);
}

void CS2DGame::ApplyRoundResult(Team winner, AudioManager* audio) {
    if (round_.IsRoundEnd()) return;   // 防重复结算（目标物与歼灭可能同帧触发）

    if (winner != Team::None) {
        teams_.AddScore(winner);
    }

    // 回合胜负音效（以玩家阵营视角）
    if (audio && winner != Team::None) {
        audio->PlaySound(winner == player_.team ? SoundEffect::ROUND_WIN
                                                : SoundEffect::ROUND_LOSE);
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

