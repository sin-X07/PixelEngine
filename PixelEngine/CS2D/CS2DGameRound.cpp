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

void CS2DGame::RespawnAllForRound(bool carryWeapons) {
    // 玩家：上回合存活 → 保留原有武器/护甲/投掷物；阵亡 → 回到手枪
    const WeaponDef& pistol = WeaponDB::Get(WeaponId::USP);
    const bool playerSurvived = carryWeapons && player_.alive;

    const WeaponId keepW0 = player_.weapons[0];
    const WeaponId keepW1 = player_.weapons[1];
    const WeaponId keepW3 = player_.weapons[3];
    const int keepNade  = player_.grenadeCount;
    const int keepArmor = player_.armor;
    const bool keepHelmet = player_.helmet;

    player_.health    = player_.maxHealth;
    player_.alive     = true;
    player_.reloading = false;
    player_.reloadTimer = 0.0f;
    player_.deathTimer = 0.0f;
    player_.vx = player_.vy = 0.0f;
    PickSpawnForTeam(Team::CT, player_.x, player_.y);

    if (playerSurvived) {
        player_.weapons[0] = keepW0;
        player_.weapons[1] = keepW1;
        player_.weapons[2] = WeaponId::Knife;
        player_.weapons[3] = keepW3;
        player_.grenadeCount = keepNade;
        player_.armor  = keepArmor;
        player_.helmet = keepHelmet;
        player_.weaponSlot = (keepW0 != WeaponId::None) ? 0 : 1;
    } else {
        player_.weapons[0] = WeaponId::None;
        player_.weapons[1] = WeaponId::USP;
        player_.weapons[2] = WeaponId::Knife;
        player_.weapons[3] = WeaponId::GrenadeHE;
        player_.grenadeCount = 0;
        player_.armor  = 0;
        player_.helmet = false;
        player_.weaponSlot = 1;
    }

    // 弹匣按当前槽位补满
    {
        const WeaponId cur = player_.weapons[player_.weaponSlot];
        const WeaponDef& w = (cur == WeaponId::None) ? pistol : WeaponDB::Get(cur);
        player_.ammo    = w.magazine;
        player_.maxAmmo = w.magazine;
        slotAmmo_[0]    = (player_.weapons[0] == WeaponId::None)
                            ? 0 : WeaponDB::Get(player_.weapons[0]).magazine;
        slotMaxAmmo_[0] = slotAmmo_[0];
        slotAmmo_[1]    = WeaponDB::Get(player_.weapons[1]).magazine;
        slotMaxAmmo_[1] = slotAmmo_[1];
    }

    // BOT：同样的规则
    for (auto& bot : bots_) {        const bool survived = carryWeapons && bot.alive;

        const WeaponId w0 = bot.weapons[0];
        const WeaponId w1 = bot.weapons[1];
        const WeaponId w3 = bot.weapons[3];
        const int nade  = bot.grenadeCount;
        const int armor = bot.armor;
        const bool helmet = bot.helmet;

        SpawnBOT(bot, bot.team);   // 先重置为手枪基准

        if (survived) {
            bot.weapons[0] = w0;
            bot.weapons[1] = w1;
            bot.weapons[3] = w3;
            bot.grenadeCount = nade;
            bot.armor  = armor;
            bot.helmet = helmet;
            bot.weaponSlot = (w0 != WeaponId::None) ? 0 : 1;
            const WeaponDef& w = WeaponDB::Get(bot.weapons[bot.weaponSlot]);
            bot.ammo    = w.magazine;
            bot.maxAmmo = w.magazine;
        }

        // 幸存者已持有主武器，BotBuyLoadout 只会补护甲/投掷物
        BotBuyLoadout(bot);
    }
    bullets_.clear();
    lastDeathTimer_ = 0.0f;
    for (bool& flag : tradePushed_) flag = false;
}

// BOT 经济：按余额买武器与护甲（与玩家同一套价格）
// 开局只有手枪、无护甲；这里模拟 BOT 在购买阶段的采购顺序：先枪、再甲、再雷
void CS2DGame::BotBuyLoadout(Player& bot) {
    const bool isT = (bot.team == Team::T);
    WeaponId primary = bot.weapons[0];

    // 1) 主武器
    if (primary == WeaponId::None) {
        if (bot.money >= 4750) {
            primary = WeaponId::AWP;                          bot.money -= 4750;
        } else if (bot.money >= 3100) {
            primary = isT ? WeaponId::AK47 : WeaponId::M4A1;  bot.money -= 3100;
        } else if (bot.money >= 2000) {
            primary = WeaponId::Galil;                        bot.money -= 1800;
        } else if (bot.money >= 1500) {
            primary = WeaponId::SMG;                          bot.money -= 1250;
        } else if (bot.money >= 1200) {
            primary = WeaponId::Shotgun;                      bot.money -= 1200;
        }
    }

    // 2) 护甲（买不起就先空着）
    if (bot.money >= 1000) {
        bot.armor = 100; bot.helmet = true;  bot.money -= 1000;
    } else if (bot.money >= 650) {
        bot.armor = 100; bot.helmet = false; bot.money -= 650;
    }

    // 3) 投掷物（价格取自武器表）
    const int nadePrice = WeaponDB::Get(bot.weapons[3]).price;
    if (bot.grenadeCount <= 0 && nadePrice > 0 && bot.money >= nadePrice) {
        bot.grenadeCount = 1;
        bot.money -= nadePrice;
    }

    bot.weapons[0] = primary;
    if (primary != WeaponId::None) {
        const WeaponDef& w = WeaponDB::Get(primary);
        bot.weaponSlot = 0;
        bot.ammo    = w.magazine;
        bot.maxAmmo = w.magazine;
    } else {
        const WeaponDef& p = WeaponDB::Get(bot.weapons[1]);
        bot.weaponSlot = 1;
        bot.ammo    = p.magazine;
        bot.maxAmmo = p.magazine;
    }

    economy_.Clamp(bot.money);
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

    // BOT 同样参与经济（否则永远只有初始余额）
    for (auto& bot : bots_) {
        bot.money += economy_.GrantRoundEnd(winner, bot.team);
        economy_.Clamp(bot.money);
    }

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

