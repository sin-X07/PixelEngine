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

// ============================================================================
// A* 路径跟随
// ============================================================================

bool CS2DGame::FollowPathTo(Player& bot, float destX, float destY, float dt) {
    if (!pathFinder_.IsBuilt()) return false;

    // 目标明显移动 / 定时 / 路径走完 → 重算
    const float gdx = destX - bot.goalX;
    const float gdy = destY - bot.goalY;
    const bool goalMoved = (gdx * gdx + gdy * gdy) > 70.0f * 70.0f;

    bot.repathTimer -= dt;
    const bool exhausted = (bot.pathIdx >= static_cast<int>(bot.path.size()));
    if (goalMoved || exhausted || bot.repathTimer <= 0.0f) {
        bot.goalX = destX;
        bot.goalY = destY;
        bot.repathTimer = 0.8f;
        bot.path.clear();
        bot.pathIdx = 0;
        if (!pathFinder_.FindPath(bot.x, bot.y, destX, destY, bot.path)) {
            bot.path.clear();
            return false;   // 无路可走 → 由调用方直线兜底
        }
    }

    if (bot.path.empty()) return false;

    // 跳过已到达的路点
    while (bot.pathIdx < static_cast<int>(bot.path.size())) {
        const auto& p = bot.path[bot.pathIdx];
        const float dx = p.first - bot.x;
        const float dy = p.second - bot.y;
        if (dx * dx + dy * dy <= 8.0f * 8.0f) { ++bot.pathIdx; continue; }
        break;
    }
    if (bot.pathIdx >= static_cast<int>(bot.path.size())) {
        bot.path.clear();
        return false;
    }

    const auto& p = bot.path[bot.pathIdx];
    const float dx = p.first - bot.x;
    const float dy = p.second - bot.y;
    const float d  = std::sqrt(dx * dx + dy * dy);
    if (d < 0.001f) return false;

    bot.vx = dx / d * 60.0f;
    bot.vy = dy / d * 60.0f;
    MoveWithCollision(bot, dt);
    bot.aimAngle = std::atan2(dy, dx);

    // 被墙卡住（速度被碰撞清零）→ 立刻失效当前路径，下一帧重算
    if (bot.vx == 0.0f && bot.vy == 0.0f) {
        bot.path.clear();
        bot.pathIdx = 0;
        bot.repathTimer = 0.0f;
    }
    return true;
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

        // ---- 目标引导：A* 寻路推进 ----
        {
            bool hasDest = false;
            float destX = 0.0f, destY = 0.0f;

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
                        if (d2 < bestD2) { bestD2 = d2; destX = cx; destY = cy; hasDest = true; }
                    }
                } else if (bot.team == Team::CT && bombPlanted_) {
                    destX = bombX_; destY = bombY_;      // 前往拆包
                    hasDest = true;
                }
            } else if (mode_ == MatchMode::HostageRescue && bot.team == Team::CT) {
                // 最近的未救出人质
                float bestD2 = 1e18f;
                for (const auto& h : hostages_) {
                    if (h.rescued) continue;
                    const float dx = h.x - bot.x, dy = h.y - bot.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) { bestD2 = d2; destX = h.x; destY = h.y; hasDest = true; }
                }
            }

            // 无任务目标时，把最近敌人当作推进目标（远距离用路径接近）
            if (!hasDest && target) {
                destX = target->x;
                destY = target->y;
                hasDest = true;
            }

            if (hasDest) {
                // 近距离且可见 → 交给战术执行器（冲刺/绕侧手感更好）
                const bool closeCombat = canSee && targetDist < 180.0f;
                if (!closeCombat && FollowPathTo(bot, destX, destY, dt)) {
                    continue;   // 本帧移动已由寻路接管
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

