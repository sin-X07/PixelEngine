#include "CS2DGame.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

// ============================================================================
// 目标物：爆破（下包/拆包）
// ============================================================================

namespace {
constexpr float BOMB_FUSE        = 40.0f;   // 下包后倒计时
constexpr float PLANT_TIME       = 2.0f;    // T 在包点停留多久完成下包
constexpr float DEFUSE_TIME      = 5.0f;    // CT 拆包耗时
} // namespace

void CS2DGame::PushKillFeed(const std::string& text) {
    killFeed_.insert(killFeed_.begin(), KillFeedEntry{ text, 4.0f });
    if (killFeed_.size() > 6) killFeed_.resize(6);
}

// 记分板数据（玩家 + 全部 BOT，按阵营排序）
std::vector<CS2DGame::ScoreRow> CS2DGame::BuildScoreboard() const {
    std::vector<ScoreRow> rows;

    ScoreRow me;
    me.name      = "你";
    me.team      = player_.team;
    me.kills     = player_.kills;
    me.deaths    = player_.deaths;
    me.money     = player_.money;
    me.alive     = player_.alive;
    me.isPlayer  = true;
    rows.push_back(me);

    for (size_t i = 0; i < bots_.size(); ++i) {
        const auto& b = bots_[i];
        ScoreRow r;
        r.name   = b.name.empty() ? ("BOT#" + std::to_string(i)) : b.name;
        r.team   = b.team;
        r.kills  = b.kills;
        r.deaths = b.deaths;
        r.money  = b.money;
        r.alive  = b.alive;
        rows.push_back(r);
    }

    std::stable_sort(rows.begin(), rows.end(), [](const ScoreRow& a, const ScoreRow& b) {
        if (a.team != b.team) return static_cast<int>(a.team) > static_cast<int>(b.team);
        return a.kills > b.kills;
    });
    return rows;
}

// 按模式初始化目标物
void CS2DGame::SetupObjective() {
    AssignBombCarrier();
    bombPlanted_   = false;
    bombTimer_     = 0.0f;
    defuseProgress_ = 0.0f;

    // 每回合必须清掉上一局遗留的 C4 掉落 / 情报 / 拾取冷却，
    // 否则上一局的掉落点会「继承」到新回合
    bombDropped_    = false;
    bombDropDelay_  = 0.0f;
    bombDropX_ = bombDropY_ = 0.0f;
    bombIntel_      = false;
    bombIntelAge_   = 0.0f;
    bombIntelX_ = bombIntelY_ = 0.0f;
    playerHoldingC4_ = false;
    player_.weapons[4] = WeaponId::None;
    if (player_.weaponSlot == 4) player_.weaponSlot = 1;
    bombX_ = bombY_ = 0.0f;
}

// 每帧推进目标物（仅交战期）
void CS2DGame::UpdateObjective(float dt, bool interactHeld, AudioManager* audio) {
    if (mode_ != MatchMode::BombDefusal) return;
    if (!round_.IsLive()) return;

    // ---------------- 爆破 ----------------
    // 玩家持包且站在包点：按住 E 安装 C4
    if (mode_ == MatchMode::BombDefusal && !bombPlanted_ && PlayerHasC4()) {
        const bool inSite = world_ && world_->FindBombSiteAt(player_.x, player_.y) != nullptr;
        if (inSite && interactHeld) {
            plantProgress_ += dt;
            if (audio) {
                // 安装阶段用下包音效做反馈（每 0.75 秒一次）
                static float tick = 0.0f;
                tick += dt;
                if (tick >= 0.75f) { tick = 0.0f; audio->PlaySound(SoundEffect::BOMB_BEEP); }
            }
            if (plantProgress_ >= 3.0f) {
                PlantBomb(player_.x, player_.y, audio);
                bombCarrier_ = -2;
                plantProgress_ = 0.0f;
            }
        } else {
            plantProgress_ = 0.0f;
        }
    }

    if (mode_ == MatchMode::BombDefusal) {
        if (!bombPlanted_) {
            // 未下包：清掉所有 BOT 的拆包状态
            for (size_t bi = 0; bi < bots_.size(); ++bi) AiAt(bi).defusing = false;

            // T 站在包点内累积下包进度
            // T 只有「C4 携带者」本人能在包点安装
            bool carrierInSite = false;

            if (!bombDropped_ && world_ &&
                bombCarrier_ >= 0 && bombCarrier_ < static_cast<int>(bots_.size())) {
                const Player& carrier = bots_[static_cast<size_t>(bombCarrier_)];
                if (carrier.alive && carrier.team == Team::T) {
                    if (world_->FindBombSiteAt(carrier.x, carrier.y)) {
                        carrierInSite = true;
                        bombX_ = carrier.x;
                        bombY_ = carrier.y;
                    }
                }
            }

            if (carrierInSite) {
                defuseProgress_ += dt;      // 复用为「下包进度」
                if (defuseProgress_ >= PLANT_TIME) {
                    PlantBomb(bombX_, bombY_, audio);
                    defuseProgress_ = 0.0f;
                    CoachOnEvent(Team::T,  CoachEventType::DefuseAborted);
                    CoachOnEvent(Team::CT, CoachEventType::DefuseAborted);
                }
            } else {
                defuseProgress_ = 0.0f;
            }
        } else {
            // 倒计时
            bombTimer_ -= dt;

            // C4 滴答：越接近爆炸越急促（2s → 0.25s）
            // 滴答声是 CT 定位炸弹的听觉线索，因此即使没有音频也要发事件。
            {
                bombBeepTimer_ -= dt;
                const float interval = (bombTimer_ > 10.0f) ? 1.0f
                                     : (bombTimer_ > 4.0f)  ? 0.5f
                                     : 0.25f;
                if (bombBeepTimer_ <= 0.0f) {
                    bombBeepTimer_ = interval;
                    EmitSound(SoundEventType::BombBeep, bombX_, bombY_, Team::T);
                    if (audio) {
                        audio->PlaySoundAt(SoundEffect::BOMB_BEEP, bombX_, bombY_,
                                           ListenerX(), ListenerY());
                    }
                }
            }

            if (bombTimer_ <= 0.0f) {
                DetonateBomb(audio);
                return;
            }

            // CT 拆包：玩家需按住 E；BOT 只派一个「当前安全」的人拆，
            // 其余人保持战斗/掩护，不会全部贴在炸弹上站桩。
            bool defusing = false;
            // 记录拆包者（-1=玩家, >=0=BOT索引, -2=无），拆包成功时给其个人发奖励
            int defuserIdx = -2;

            bool playerDefusing = false;
            if (player_.alive && player_.team == Team::CT && interactHeld) {
                const float dx = player_.x - bombX_;
                const float dy = player_.y - bombY_;
                if (std::sqrt(dx * dx + dy * dy) <= Tuning::DefuseRadius) {
                    defusing = true;
                    defuserIdx = -1;
                    playerDefusing = true;
                }
            }

            // BOT：只选一个安全候选（无可见敌人/最近没挨打；最后 6 秒强制拆）
            int bestBot = -2;
            float bestD2 = 1e36f;
            for (size_t bi = 0; bi < bots_.size(); ++bi) {
                auto& b = bots_[bi];
                if (!b.alive || b.team != Team::CT) {
                    AiAt(bi).defusing = false;
                    continue;
                }
                const float dx = b.x - bombX_;
                const float dy = b.y - bombY_;
                const float d2 = dx * dx + dy * dy;
                if (d2 > Tuning::DefuseRadius * Tuning::DefuseRadius) {
                    AiAt(bi).defusing = false;
                    continue;
                }
                if (!BotCanDefuse(bi, b)) continue;
                if (d2 < bestD2) { bestD2 = d2; bestBot = static_cast<int>(bi); }
            }

            if (!playerDefusing && bestBot >= 0) {
                if (!AiAt(static_cast<size_t>(bestBot)).defusing) {
                    std::cout << "[Defuse] #" << bestBot << " start" << std::endl;
                }
                defusing = true;
                defuserIdx = bestBot;
            }
            for (size_t bi = 0; bi < bots_.size(); ++bi) {
                if (bots_[bi].team != Team::CT) continue;
                AiAt(bi).defusing = (!playerDefusing && static_cast<int>(bi) == bestBot);
            }

            if (defusing) {
                if (defuseProgress_ <= 0.0f) {
                    const std::string bArea = world_ ? world_->FindAreaAt(bombX_, bombY_)
                                                     : std::string();
                    CoachOnEvent(Team::T,  CoachEventType::DefuseStarted, bArea);
                    CoachOnEvent(Team::CT, CoachEventType::DefuseStarted, bArea);
                }
                defuseProgress_ += dt / DEFUSE_TIME;
                if (defuseProgress_ >= 1.0f) {
                    std::cout << "[Defuse] #" << defuserIdx << " success" << std::endl;
                    std::cout << "[CS2D] C4 已拆除!" << std::endl;
                    bombPlanted_ = false;
                    bombTimer_ = 0.0f;
                    defuseProgress_ = 0.0f;
                    EmitSound(SoundEventType::BombDefused, bombX_, bombY_, Team::CT);
                    if (audio) audio->PlaySound(SoundEffect::BOMB_DEFUSE);

                    // ---- 拆包奖励：拆包者个人 +$300（照抄 CS2，不给全队）----
                    {
                        const int bonus = economy_.GrantDefuse();
                        if (defuserIdx == -1) {
                            player_.money += bonus;
                            economy_.Clamp(player_.money);
                            std::cout << "[Economy] 玩家拆包 +$" << bonus << std::endl;
                        } else if (defuserIdx >= 0 &&
                                   defuserIdx < static_cast<int>(bots_.size())) {
                            bots_[static_cast<size_t>(defuserIdx)].money += bonus;
                            economy_.Clamp(bots_[static_cast<size_t>(defuserIdx)].money);
                            std::cout << "[Economy] " << bots_[static_cast<size_t>(defuserIdx)].name
                                      << " 拆包 +$" << bonus << std::endl;
                        }
                    }

                    ApplyRoundResult(Team::CT, audio);
                    round_.EndRound(Team::CT);
                }
            } else {
                // 松开拆包键 / 离开包点：进度立刻清零（不给半途而废留余地）
                defuseProgress_ = 0.0f;
                // 同时清掉 BOT 的拆包标记，避免它们继续站桩
                for (auto& b : bots_) AiAt(AiIndexOf(b)).defusing = false;
            }
        }
        return;
    }
}

void CS2DGame::PlantBomb(float x, float y, AudioManager* audio) {
    // C4 已安装：从玩家手里/槽位里移除
    bombCarrier_ = -2;
    playerHoldingC4_ = false;
    player_.weapons[4] = WeaponId::None;
    if (player_.weaponSlot == 4) EquipFallbackWeapon();
    bombPlanted_ = true;
    // Set coords before events so FindAreaAt is not fed stale/zero bomb position.
    bombX_ = x;
    bombY_ = y;

    // ---- 下包奖励：T 全员每人 +$300（照抄 CS2）----
    {
        const int bonus = economy_.GrantPlant();
        if (player_.team == Team::T) {
            player_.money += bonus;
            economy_.Clamp(player_.money);
        }
        for (auto& bot : bots_) {
            if (bot.team != Team::T) continue;
            bot.money += bonus;
            economy_.Clamp(bot.money);
        }
        std::cout << "[Economy] C4 下包 → T 全员 +$" << bonus << std::endl;
    }

    {
        const std::string plantArea = world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
        CoachOnEvent(Team::T,  CoachEventType::BombPlanted, plantArea);
        CoachOnEvent(Team::CT, CoachEventType::BombPlanted, plantArea);
        std::cout << "[Coach] BombPlanted area="
                  << (plantArea.empty() ? "(empty)" : plantArea) << std::endl;
    }
    bombTimer_ = BOMB_FUSE;
    bombBeepTimer_ = 0.0f;
    defuseProgress_ = 0.0f;
    // 下包完成是全图可闻的事件：所有单位收到「炸弹在这里」
    EmitSound(SoundEventType::BombPlanted, bombX_, bombY_, Team::T);
    std::cout << "[CS2D] C4 已安装! " << static_cast<int>(BOMB_FUSE) << " 秒后爆炸" << std::endl;
    if (audio) audio->PlaySound(SoundEffect::BOMB_PLANT);
}

void CS2DGame::DetonateBomb(AudioManager* audio) {
    bombPlanted_ = false;
    bombTimer_ = 0.0f;
    std::cout << "[CS2D] C4 爆炸! T 获胜本回合" << std::endl;
    EmitSound(SoundEventType::Explosion, bombX_, bombY_, Team::T, -2, 1600.0f);

    // 爆炸范围伤害（不分阵营）
    constexpr float RADIUS = 160.0f;
    auto damageAt = [&](Player& p) {
        if (!p.alive) return;
        const float dx = p.x - bombX_;
        const float dy = p.y - bombY_;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > RADIUS) return;
        const float f = 1.0f - d / RADIUS;
        if (ApplyDamage(p, static_cast<int>(200.0f * f))) {
            OnCombatantKilled(p, audio);
        }
    };
    damageAt(player_);
    for (auto& b : bots_) damageAt(b);

    if (audio) audio->PlaySound(SoundEffect::GRENADE_EXPLODE);

    ApplyRoundResult(Team::T, audio);
    round_.EndRound(Team::T);
}


// C4 携带者：每回合重新分配
//   玩家在 T 侧：60% 玩家，40% 随机 T BOT
//   玩家在 CT 侧：随机 T BOT（否则 T 方无人能下包）
void CS2DGame::AssignBombCarrier() {
    bombCarrier_ = -2;
    if (mode_ != MatchMode::BombDefusal) return;

    std::vector<int> tBots;
    for (size_t i = 0; i < bots_.size(); ++i) {
        if (bots_[i].team == Team::T) tBots.push_back(static_cast<int>(i));
    }

    if (player_.team == Team::T) {
        if (tBots.empty() || (rng_() % 100) < 60) {
            bombCarrier_ = -1;
        } else {
            bombCarrier_ = tBots[rng_() % tBots.size()];
        }
    } else if (!tBots.empty()) {
        bombCarrier_ = tBots[rng_() % tBots.size()];
    }
}
