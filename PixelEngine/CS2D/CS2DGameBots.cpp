#include "CS2DGame.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// BOT AI
// ============================================================================

bool CS2DGame::BotCanSeePlayer(const Player& bot) const {
    if (!player_.alive || player_.team == bot.team) return false;
    const float dx = player_.x - bot.x;
    const float dy = player_.y - bot.y;
    if (std::sqrt(dx * dx + dy * dy) > BOT_VIEW_RADIUS) return false;
    if (!renderer_) return true;
    return renderer_->HasLineOfSight(bot.x, bot.y, player_.x, player_.y);
}

bool CS2DGame::PlayerCanSeeBot(const Player& bot) const {
    if (!bot.alive) return false;
    // 烟雾遮挡
    if (SegmentBlockedBySmoke(player_.x, player_.y, bot.x, bot.y)) return false;
    if (!renderer_) return true;
    return renderer_->IsVisibleToPlayer(player_.x, player_.y, player_.aimAngle,
                                        bot.x, bot.y);
}

// 找最近的存活敌人（玩家或其它 BOT）
static const CS2DGame::Player* FindNearestEnemy(const CS2DGame::Player& self,
                                                const CS2DGame::Player& player,
                                                const std::vector<CS2DGame::Player>& bots,
                                                float& outDist) {
    const CS2DGame::Player* best = nullptr;
    float bestD2 = 1e18f;

    auto consider = [&](const CS2DGame::Player& other) {
        if (!other.alive || other.team == self.team) return;
        const float dx = other.x - self.x;
        const float dy = other.y - self.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = &other; }
    };

    consider(player);
    for (const auto& b : bots) {
        if (&b == &self) continue;
        consider(b);
    }

    outDist = std::sqrt(bestD2);
    return best;
}

void CS2DGame::UpdateBOTs(float dt, AudioManager* audio) {
    while (botExecutors_.size() < bots_.size()) botExecutors_.emplace_back();

    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& bot = bots_[i];
        if (!bot.alive) continue;

        // 目标：最近的存活敌人
        float targetDist = 0.0f;
        const Player* target = FindNearestEnemy(bot, player_, bots_, targetDist);
        if (!target) continue;

        // 视线判定（距离 + 烟雾 + LOS）
        bool canSee = false;
        if (targetDist <= BOT_VIEW_RADIUS &&
            !SegmentBlockedBySmoke(bot.x, bot.y, target->x, target->y)) {
            canSee = !renderer_ || renderer_->HasLineOfSight(bot.x, bot.y, target->x, target->y);
        }

        bot.llmIntent.Tick(dt);

        // ---- 目标引导：看不见敌人时朝任务目标推进 ----
        if (!canSee) {
            bool hasWaypoint = false;
            float wx = 0.0f, wy = 0.0f;

            if (mode_ == MatchMode::BombDefusal) {
                if (bot.team == Team::T && !bombPlanted_) {
                    // 最近的包点中心
                    const auto& sites = world_->GetBombSites();
                    float bestD2 = 1e18f;
                    for (const auto& s : sites) {
                        const float cx = s.x + s.width * 0.5f;
                        const float cy = s.y + s.height * 0.5f;
                        const float dx = cx - bot.x, dy = cy - bot.y;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < bestD2) { bestD2 = d2; wx = cx; wy = cy; hasWaypoint = true; }
                    }
                } else if (bot.team == Team::CT && bombPlanted_) {
                    wx = bombX_; wy = bombY_;          // 前往拆包
                    hasWaypoint = true;
                }
            } else if (mode_ == MatchMode::HostageRescue && bot.team == Team::CT) {
                // 最近的未救出人质
                float bestD2 = 1e18f;
                for (const auto& h : hostages_) {
                    if (h.rescued) continue;
                    const float dx = h.x - bot.x, dy = h.y - bot.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) { bestD2 = d2; wx = h.x; wy = h.y; hasWaypoint = true; }
                }
            }

            if (hasWaypoint) {
                const float dx = wx - bot.x;
                const float dy = wy - bot.y;
                const float d  = std::sqrt(dx * dx + dy * dy);
                if (d > 8.0f) {
                    bot.vx = dx / d * 60.0f;
                    bot.vy = dy / d * 60.0f;
                    bot.aimAngle = std::atan2(dy, dx);
                    MoveWithCollision(bot, dt);
                    continue;
                }
            }
        }

        BehaviorOutput out = botExecutors_[i].Execute(
            bot.llmIntent,
            bot.x, bot.y,
            target->x, target->y,
            canSee, targetDist,
            bot.ammo > 0, bot.reloading, bot.weaponSlot,
            rng_, dt);

        bot.vx = out.vx;
        bot.vy = out.vy;
        MoveWithCollision(bot, dt);

        if (canSee) bot.aimAngle = out.aimAngle;

        const WeaponDef& def = bot.CurrentWeapon();

        // 枪械 / 霰弹
        if (out.shouldShoot && canSee && def.cls != WeaponClass::Melee &&
            def.cls != WeaponClass::Grenade) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;

            if (bot.shootCooldown <= 0.0f) {
                if (targetDist < def.range + 200.0f) {
                    if (bot.ammo <= 0) { StartReload(bot); continue; }

                    // 命中倾向：30% 精确（小抖动），70% 打偏（大散布）
                    std::uniform_real_distribution<float> hitRoll(0.0f, 1.0f);
                    const bool precise = hitRoll(rng_) < 0.30f;
                    const float base = precise ? 0.08f : 0.8f;
                    std::uniform_real_distribution<float> spreadDist(
                        -base * out.spreadMul, base * out.spreadMul);

                    const float aimBase = std::atan2(target->y - bot.y, target->x - bot.x);
                    const int pellets = std::max(1, def.pellets);
                    for (int p = 0; p < pellets; ++p) {
                        FireBullet(bot.x, bot.y, aimBase + spreadDist(rng_), def,
                                   static_cast<int>(i), bot.team);
                    }
                    bot.ammo--;
                    bot.shootCooldown = def.cooldown * 1.8f * out.shootCooldownMul;
                    bot.shootAnimTimer = 0.12f;

                    if (audio && def.cls == WeaponClass::Rifle) {
                        audio->PlaySoundAt(SoundEffect::AK47_FIRE, bot.x, bot.y,
                                           player_.x, player_.y, player_.aimAngle);
                    }
                }
            }
        }
        // 近战
        else if (def.cls == WeaponClass::Melee && canSee && targetDist < 20.0f && target->alive) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;
            if (bot.shootCooldown <= 0.0f) {
                if (target == &player_) {
                    player_.health -= static_cast<int>(def.damage * 0.33f);
                    if (player_.health <= 0) OnCombatantKilled(player_, audio);
                }
                bot.shootCooldown = def.cooldown * 1.5f * out.shootCooldownMul;
                bot.shootAnimTimer = 0.12f;
            }
        }
    }

    // 死斗模式：BOT 阵亡后自动重生；团队模式由回合开始统一重生
    if (mode_ == MatchMode::DeathMatch) {
        for (auto& bot : bots_) {
            if (!bot.alive) {
                bot.llmIntent.Reset();
                RespawnBOT(bot);
            }
        }
    }
}

