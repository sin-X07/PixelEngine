#include "CS2DGame.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

// ============================================================================
// 目标物：爆破（下包/拆包）与人质营救
// ============================================================================

namespace {
constexpr float BOMB_FUSE        = 40.0f;   // 下包后倒计时
constexpr float PLANT_TIME       = 2.0f;    // T 在包点停留多久完成下包
constexpr float DEFUSE_TIME      = 5.0f;    // CT 拆包耗时
constexpr float RESCUE_TIME      = 2.5f;    // 营救一名人质耗时
constexpr float INTERACT_RADIUS  = 55.0f;   // 交互半径
constexpr float HOSTAGE_RADIUS   = 45.0f;
} // namespace

int CS2DGame::GetHostagesRescued() const {
    int n = 0;
    for (const auto& h : hostages_) if (h.rescued) ++n;
    return n;
}

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
        r.name   = "BOT#" + std::to_string(i);
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
    hostages_.clear();

    // C4 携带者：爆破模式且玩家在 T 侧 → 由玩家携带
    plantProgress_ = 0.0f;
    // C4 携带者：每回合重新分配（T 侧玩家约 60% 概率拿到，其余给某个 T BOT）
    bombCarrier_ = -2;
    if (mode_ == MatchMode::BombDefusal) {
        if (player_.team == Team::T) {
            std::vector<int> tBots;
            for (size_t i = 0; i < bots_.size(); ++i) {
                if (bots_[i].team == Team::T) tBots.push_back(static_cast<int>(i));
            }
            if (tBots.empty() || (rng_() % 100) < 60) {
                bombCarrier_ = -1;                       // 玩家携带
            } else {
                bombCarrier_ = tBots[rng_() % tBots.size()];  // 某个 T BOT 携带
            }
        }
    }
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
    rescueProgress_ = 0.0f;
    hostageNearIdx_ = -1;
    bombX_ = bombY_ = 0.0f;

    if (mode_ != MatchMode::HostageRescue) return;

    // 优先使用地图 hostge 对象；缺失则在地图四角放 4 个占位人质
    const auto& objs = world_->GetHostages();
    if (!objs.empty()) {
        for (const auto& o : objs) {
            hostages_.push_back({ o.x, o.y, false });
        }
    } else {
        const float w = world_->GetMapPixelW();
        const float h = world_->GetMapPixelH();
        hostages_.push_back({ w * 0.30f, h * 0.30f, false });
        hostages_.push_back({ w * 0.70f, h * 0.30f, false });
        hostages_.push_back({ w * 0.30f, h * 0.70f, false });
        hostages_.push_back({ w * 0.70f, h * 0.70f, false });
        std::cout << "[CS2D] 地图无人质对象, 使用 4 个默认人质点" << std::endl;
    }

    std::cout << "[CS2D] 人质模式: 共 " << hostages_.size() << " 名人质" << std::endl;
}

// 每帧推进目标物（仅交战期）
void CS2DGame::UpdateObjective(float dt, bool interactHeld, AudioManager* audio) {
    if (mode_ != MatchMode::BombDefusal && mode_ != MatchMode::HostageRescue) return;
    if (!round_.IsLive()) return;

    // ---------------- 爆破 ----------------
    // 玩家持包且站在包点：按住 E 安装 C4
    if (mode_ == MatchMode::BombDefusal && !bombPlanted_ && PlayerHasC4()) {
        bool inSite = false;
        for (const auto& site : world_->GetBombSites()) {
            if (player_.x >= site.x && player_.x <= site.x + site.width &&
                player_.y >= site.y && player_.y <= site.y + site.height) {
                inSite = true;
                break;
            }
        }
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
            const auto& sites = world_->GetBombSites();
            bool tInSite = false;

            if (!sites.empty()) {
                for (const auto& b : bots_) {
                    if (!b.alive || b.team != Team::T) continue;
                    for (const auto& s : sites) {
                        if (b.x >= s.x && b.x <= s.x + s.width &&
                            b.y >= s.y && b.y <= s.y + s.height) {
                            tInSite = true;
                            bombX_ = b.x;
                            bombY_ = b.y;
                            break;
                        }
                    }
                    if (tInSite) break;
                }
            }

            if (tInSite) {
                defuseProgress_ += dt;      // 复用为"下包进度"
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
            if (audio) {
                bombBeepTimer_ -= dt;
                const float interval = (bombTimer_ > 10.0f) ? 1.0f
                                     : (bombTimer_ > 4.0f)  ? 0.5f
                                     : 0.25f;
                if (bombBeepTimer_ <= 0.0f) {
                    bombBeepTimer_ = interval;
                    audio->PlaySoundAt(SoundEffect::BOMB_BEEP, bombX_, bombY_,
                                       ListenerX(), ListenerY());
                }
            }

            if (bombTimer_ <= 0.0f) {
                DetonateBomb(audio);
                return;
            }

            // CT 拆包：玩家需按住 E；BOT 只要贴近炸弹即自动拆
            bool defusing = false;

            if (player_.alive && player_.team == Team::CT && interactHeld) {
                const float dx = player_.x - bombX_;
                const float dy = player_.y - bombY_;
                if (std::sqrt(dx * dx + dy * dy) <= Tuning::DefuseRadius) defusing = true;
            }

            // BOT 拆包（玩家阵亡后也能赢）
            for (size_t bi = 0; bi < bots_.size(); ++bi) {
                auto& b = bots_[bi];
                if (!b.alive || b.team != Team::CT) continue;
                const float dx = b.x - bombX_;
                const float dy = b.y - bombY_;
                if (std::sqrt(dx * dx + dy * dy) <= Tuning::DefuseRadius) {
                    defusing = true;
                    AiAt(bi).defusing = true;      // 通知 BOT 停下原地拆包
                } else {
                    AiAt(bi).defusing = false;
                }
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
                    std::cout << "[CS2D] C4 已拆除!" << std::endl;
                    bombPlanted_ = false;
                    bombTimer_ = 0.0f;
                    defuseProgress_ = 0.0f;
                    if (audio) audio->PlaySound(SoundEffect::BOMB_DEFUSE);
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

    // ---------------- 人质 ----------------
    if (mode_ == MatchMode::HostageRescue) {
        // 找最近的可营救人质
        int   nearIdx = -1;
        float nearD   = 1e18f;
        for (size_t i = 0; i < hostages_.size(); ++i) {
            if (hostages_[i].rescued) continue;
            const float dx = player_.x - hostages_[i].x;
            const float dy = player_.y - hostages_[i].y;
            const float d  = std::sqrt(dx * dx + dy * dy);
            if (d <= HOSTAGE_RADIUS && d < nearD) { nearD = d; nearIdx = static_cast<int>(i); }
        }

        // 换目标则进度重置
        if (nearIdx != hostageNearIdx_) {
            hostageNearIdx_ = nearIdx;
            rescueProgress_ = 0.0f;
        }

        if (nearIdx >= 0 && interactHeld && player_.alive) {
            rescueProgress_ += dt / RESCUE_TIME;
            if (rescueProgress_ >= 1.0f) {
                hostages_[nearIdx].rescued = true;
                std::cout << "[CS2D] 已营救人质 " << (GetHostagesRescued())
                          << "/" << hostages_.size() << std::endl;
                rescueProgress_ = 0.0f;
                hostageNearIdx_ = -1;

                if (GetHostagesRescued() >= GetHostagesTotal()) {
                    std::cout << "[CS2D] 全部人质获救!" << std::endl;
                    ApplyRoundResult(Team::CT, audio);
                    round_.EndRound(Team::CT);
                }
            }
        } else {
            rescueProgress_ = std::max(0.0f, rescueProgress_ - dt * 0.6f);
        }
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
    std::cout << "[CS2D] C4 已安装! " << static_cast<int>(BOMB_FUSE) << " 秒后爆炸" << std::endl;
    if (audio) audio->PlaySound(SoundEffect::BOMB_PLANT);
}

void CS2DGame::DetonateBomb(AudioManager* audio) {
    bombPlanted_ = false;
    bombTimer_ = 0.0f;
    std::cout << "[CS2D] C4 爆炸! T 获胜本回合" << std::endl;

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
