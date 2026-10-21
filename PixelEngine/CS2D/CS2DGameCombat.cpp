#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// 玩家移动 / 射击
// ============================================================================

void CS2DGame::UpdatePlayerMovement(float dt, bool w, bool a, bool s, bool d, bool stealth) {
    constexpr float ACCEL    = 600.0f;
    constexpr float FRICTION = 8.0f;
    const float maxSpeed = stealth ? 25.0f : 60.0f;

    const int dx = (d ? 1 : 0) - (a ? 1 : 0);
    const int dy = (s ? 1 : 0) - (w ? 1 : 0);

    const float len = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    if (len > 0.001f) {
        player_.vx += (dx / len) * ACCEL * dt;
        player_.vy += (dy / len) * ACCEL * dt;
    } else {
        player_.vx -= player_.vx * FRICTION * dt;
        player_.vy -= player_.vy * FRICTION * dt;
        if (std::abs(player_.vx) < 1.0f) player_.vx = 0.0f;
        if (std::abs(player_.vy) < 1.0f) player_.vy = 0.0f;
    }

    const float speed = std::sqrt(player_.vx * player_.vx + player_.vy * player_.vy);
    if (speed > maxSpeed) {
        player_.vx = player_.vx / speed * maxSpeed;
        player_.vy = player_.vy / speed * maxSpeed;
    }

    MoveWithCollision(player_, dt);
}

void CS2DGame::MoveWithCollision(Player& entity, float dt) {
    const float half = entity.size * 0.5f;

    const float nx = entity.x + entity.vx * dt;
    if (!world_->CheckRectCollision(nx - half, entity.y - half, entity.size, entity.size)) {
        entity.x = nx;
    } else {
        entity.vx = 0.0f;
    }

    const float ny = entity.y + entity.vy * dt;
    if (!world_->CheckRectCollision(entity.x - half, ny - half, entity.size, entity.size)) {
        entity.y = ny;
    } else {
        entity.vy = 0.0f;
    }

    entity.x = std::clamp(entity.x, 0.0f, world_->GetMapPixelW() - 1.0f);
    entity.y = std::clamp(entity.y, 0.0f, world_->GetMapPixelH() - 1.0f);
}

void CS2DGame::UpdatePlayerShooting(float dt, bool mouseDown, float worldMX, float worldMY,
                                    AudioManager* audio) {
    if (!player_.alive) return;

    player_.aimAngle = std::atan2(worldMY - player_.y, worldMX - player_.x);

    const WeaponDef& def = player_.CurrentWeapon();

    // 投掷物
    if (def.cls == WeaponClass::Grenade) {
        if (!mouseDown || player_.shootCooldown > 0.0f) return;
        if (player_.grenadeCount <= 0) return;
        ThrowGrenade(player_, player_.aimAngle);
        player_.shootCooldown = def.cooldown;
        player_.shootAnimTimer = 0.12f;
        return;
    }

    if (player_.reloading) return;
    if (player_.shootCooldown > 0.0f) player_.shootCooldown -= dt;

    const bool mouseEdge = mouseDown && !prevMouseDown_;
    if (!def.automatic) {
        if (!mouseEdge || player_.shootCooldown > 0.0f) return;
    } else {
        if (!mouseDown || player_.shootCooldown > 0.0f) return;
    }

    // ---- 近战 ----
    if (def.cls == WeaponClass::Melee) {
        for (auto& bot : bots_) {
            if (!bot.alive || bot.team == player_.team) continue;
            const float bdx = bot.x - player_.x;
            const float bdy = bot.y - player_.y;
            if (std::sqrt(bdx * bdx + bdy * bdy) < def.range) {
                if (ApplyDamage(bot, static_cast<int>(def.damage))) OnCombatantKilled(bot, audio);
            }
        }
        player_.shootCooldown = def.cooldown;
        player_.shootAnimTimer = 0.12f;
        if (audio) audio->PlaySound(SoundEffect::KATANA_SWING);
        return;
    }

    // ---- 枪械 ----
    if (player_.ammo <= 0) { StartReload(player_); return; }

    // 散布 + 多弹丸（霰弹）
    std::uniform_real_distribution<float> spreadDist(-def.spreadBase, def.spreadBase);
    for (int p = 0; p < std::max(1, def.pellets); ++p) {
        const float angle = player_.aimAngle + spreadDist(rng_);
        FireBullet(player_.x, player_.y, angle, def, -1, player_.team);
    }

    player_.ammo--;
    player_.shootCooldown = def.cooldown;
    player_.shootAnimTimer = 0.12f;

    if (audio) audio->PlaySound(FireSoundFor(def.id));
}

// 武器 → 枪声（近战/投掷物回退到通用音）
SoundEffect CS2DGame::FireSoundFor(WeaponId id) {
    switch (id) {
        case WeaponId::Glock:   return SoundEffect::GLOCK_FIRE;
        case WeaponId::USP:     return SoundEffect::USP_FIRE;
        case WeaponId::Deagle:  return SoundEffect::DEAGLE_FIRE;
        case WeaponId::Galil:   return SoundEffect::GALIL_FIRE;
        case WeaponId::AK47:    return SoundEffect::AK47_FIRE;
        case WeaponId::M4A1:    return SoundEffect::M4A1_FIRE;
        case WeaponId::SMG:     return SoundEffect::SMG_FIRE;
        case WeaponId::Shotgun: return SoundEffect::SHOTGUN_FIRE;
        case WeaponId::AWP:     return SoundEffect::AWP_FIRE;
        default:                return SoundEffect::SHOOT;
    }
}

// 由 WeaponDef 直接发射子弹
void CS2DGame::FireBullet(float x, float y, float angle, const WeaponDef& def,
                          int ownerIdx, Team ownerTeam) {
    Bullet b;
    b.x = x;
    b.y = y;
    b.vx = std::cos(angle) * def.bulletSpeed;
    b.vy = std::sin(angle) * def.bulletSpeed;
    b.traveled = 0.0f;
    b.maxDist  = def.range;
    b.damage   = static_cast<int>(def.damage);
    b.ownerIdx = ownerIdx;
    b.ownerTeam = ownerTeam;
    b.active   = true;
    bullets_.push_back(b);
}


// ============================================================================
// 子弹
// ============================================================================

void CS2DGame::UpdateBullets(float dt, AudioManager* audio) {
    for (auto& bullet : bullets_) {
        if (!bullet.active) continue;

        const float stepX = bullet.vx * dt;
        const float stepY = bullet.vy * dt;
        bullet.x += stepX;
        bullet.y += stepY;
        bullet.traveled += std::sqrt(stepX * stepX + stepY * stepY);

        if (bullet.traveled >= bullet.maxDist) { bullet.active = false; continue; }

        int row = 0, col = 0;
        world_->WorldToTile(bullet.x, bullet.y, row, col);
        if (world_->IsBlockingBullet(row, col)) { bullet.active = false; continue; }

        // ---- 命中 BOT ----
        for (size_t i = 0; i < bots_.size(); ++i) {
            auto& bot = bots_[i];
            if (!bot.alive || bot.team == bullet.ownerTeam) continue;

            const float dx = bullet.x - bot.x;
            const float dy = bullet.y - bot.y;
            const float hitR = bot.size * 0.5f + 2.0f;
            if (dx * dx + dy * dy >= hitR * hitR) continue;

            bullet.active = false;

            // 玩家命中反馈（无论是否致死）
            if (bullet.ownerIdx == -1) hitMarkerTimer_ = 0.18f;

            if (ApplyDamage(bot, bullet.damage)) {
                OnCombatantKilled(bot, audio);

                // 玩家击杀：计分 + 经济奖励 + 播报
                if (bullet.ownerIdx == -1) {
                    ++playerKills_;
                    ++player_.kills;
                    player_.money += economy_.GrantKill();
                    economy_.Clamp(player_.money);
                    PushKillFeed("你  ▸  击杀 " + std::string(TeamName(bot.team)) + " BOT");
                } else {
                    PushKillFeed(std::string("BOT#") + std::to_string(bullet.ownerIdx)
                                 + "  ▸  击杀 " + TeamName(bot.team) + " BOT");
                    // BOT 击杀奖励
                    if (bullet.ownerIdx >= 0 &&
                        bullet.ownerIdx < static_cast<int>(bots_.size())) {
                        bots_[bullet.ownerIdx].money += economy_.GrantKill();
                        economy_.Clamp(bots_[bullet.ownerIdx].money);
                    }
                }

                if (audio) audio->PlaySoundAt(SoundEffect::KILL, bot.x, bot.y,
                                              player_.x, player_.y);
            } else if (audio) {
                audio->PlaySoundAt(SoundEffect::HIT, bot.x, bot.y,
                                   player_.x, player_.y);
            }

            // 受击反应：血量低且当前未在撤退 → 记录威胁来源并撤退一次
            if (AiAt(i).retreatTimer <= 0.0f && bot.health > 0 &&
                bot.health <= Tuning::RetreatHealth) {
                AiAt(i).retreatTimer = Tuning::RetreatDuration;
                if (bullet.ownerIdx == -1) {
                    AiAt(i).threatX = player_.x;
                    AiAt(i).threatY = player_.y;
                } else if (bullet.ownerIdx >= 0 &&
                           bullet.ownerIdx < static_cast<int>(bots_.size())) {
                    AiAt(i).threatX = bots_[bullet.ownerIdx].x;
                    AiAt(i).threatY = bots_[bullet.ownerIdx].y;
                }
            }
            break;
        }

        // ---- 命中玩家 ----
        if (bullet.active && player_.alive && player_.team != bullet.ownerTeam) {
            const float dx = bullet.x - player_.x;
            const float dy = bullet.y - player_.y;
            const float hitR = player_.size * 0.5f + 2.0f;
            if (dx * dx + dy * dy < hitR * hitR) {
                bullet.active = false;
                damageFlashTimer_ = 0.30f;   // 玩家受伤反馈
                if (audio) audio->PlaySound(player_.armor > 0 ? SoundEffect::ARMOR_HIT
                                                              : SoundEffect::HIT);
                if (ApplyDamage(player_, bullet.damage)) {
                    PushKillFeed(std::string("BOT#") + std::to_string(bullet.ownerIdx) + "  ▸  击杀 你");
                    OnCombatantKilled(player_, audio);
                    // BOT 击杀玩家奖励
                    if (bullet.ownerIdx >= 0 &&
                        bullet.ownerIdx < static_cast<int>(bots_.size())) {
                        bots_[bullet.ownerIdx].money += economy_.GrantKill();
                        economy_.Clamp(bots_[bullet.ownerIdx].money);
                    }
                }
            }
        }
    }

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [](const Bullet& b) { return !b.active; }),
                   bullets_.end());
}


// ============================================================================
// 手雷
// ============================================================================

void CS2DGame::ThrowGrenade(Player& thrower, float aimAngle) {
    if (thrower.grenadeCount <= 0) return;

    const WeaponId type = thrower.weapons[3];
    const WeaponDef& def = WeaponDB::Get(type);
    if (def.cls != WeaponClass::Grenade) return;

    Grenade g;
    g.x = thrower.x;
    g.y = thrower.y;
    // 摩擦 2.0/s → 投掷距离 ≈ 速度/2；420 约 210px
    g.vx = std::cos(aimAngle) * 420.0f;
    g.vy = std::sin(aimAngle) * 420.0f;
    g.fuse = 1.4f;
    g.type = type;
    g.team = thrower.team;
    g.active = true;
    grenades_.push_back(g);

    --thrower.grenadeCount;
}

void CS2DGame::UpdateGrenades(float dt, AudioManager* audio) {
    // 投掷物飞行 + 引信
    for (auto& g : grenades_) {
        if (!g.active) continue;

        g.fuse -= dt;
        g.x += g.vx * dt;
        g.y += g.vy * dt;

        // 地面摩擦（指数衰减；v0/friction ≈ 总投掷距离）
        g.vx -= g.vx * 2.0f * dt;
        g.vy -= g.vy * 2.0f * dt;

        // 撞墙停下
        int row = 0, col = 0;
        world_->WorldToTile(g.x, g.y, row, col);
        if (world_->IsBlockingBullet(row, col)) {
            g.vx = g.vy = 0.0f;
        }
        g.x = std::clamp(g.x, 0.0f, world_->GetMapPixelW() - 1.0f);
        g.y = std::clamp(g.y, 0.0f, world_->GetMapPixelH() - 1.0f);

        if (g.fuse <= 0.0f) {
            ExplodeGrenade(g, audio);
            g.active = false;
        }
    }
    grenades_.erase(std::remove_if(grenades_.begin(), grenades_.end(),
                                   [](const Grenade& g) { return !g.active; }),
                    grenades_.end());

    // 烟雾云寿命
    for (auto& s : smokes_) s.life -= dt;
    smokes_.erase(std::remove_if(smokes_.begin(), smokes_.end(),
                                 [](const SmokeCloud& s) { return s.life <= 0.0f; }),
                  smokes_.end());

    // 闪光致盲衰减
    if (flashTimer_ > 0.0f) flashTimer_ -= dt;
}

void CS2DGame::ExplodeGrenade(const Grenade& g, AudioManager* audio) {
    switch (g.type) {
        case WeaponId::GrenadeHE: {
            constexpr float RADIUS = 95.0f;
            constexpr float MAX_DMG = 98.0f;

            auto damageAt = [&](Player& p) {
                if (!p.alive || p.team == g.team) return;   // 无友伤
                const float dx = p.x - g.x;
                const float dy = p.y - g.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > RADIUS) return;
                const float falloff = 1.0f - d / RADIUS;
                if (ApplyDamage(p, static_cast<int>(MAX_DMG * falloff))) {
                    OnCombatantKilled(p, audio);
                }
            };

            damageAt(player_);
            for (auto& b : bots_) damageAt(b);

            if (audio) audio->PlaySound(SoundEffect::GRENADE_EXPLODE);
            break;
        }
        case WeaponId::GrenadeFlash: {
            if (audio) audio->PlaySound(SoundEffect::FLASHBANG);
            // 致盲：距离 + 视线（玩家与 BOT 一致）
            if (player_.alive && player_.team != g.team) {
                const float dx = player_.x - g.x;
                const float dy = player_.y - g.y;
                if (std::sqrt(dx * dx + dy * dy) < 220.0f) {
                    flashTimer_ = 2.5f;
                }
            }
            for (size_t bi = 0; bi < bots_.size(); ++bi) {
                auto& b = bots_[bi];
                if (!b.alive || b.team == g.team) continue;
                const float dx = b.x - g.x;
                const float dy = b.y - g.y;
                if (std::sqrt(dx * dx + dy * dy) < 220.0f) {
                    AiAt(bi).blindTimer = 2.5f;
                }
            }
            if (audio) audio->PlaySound(SoundEffect::HIT);
            break;
        }
        case WeaponId::GrenadeSmoke: {
            SmokeCloud s;
            s.x = g.x;
            s.y = g.y;
            s.radius = 70.0f;
            s.life = 12.0f;
            smokes_.push_back(s);
            if (audio) audio->PlaySound(SoundEffect::SMOKE);
            break;
        }
        default:
            break;
    }
}

// 线段是否被任一烟雾云遮挡（用于视野判定）
bool CS2DGame::SegmentBlockedBySmoke(float x1, float y1, float x2, float y2) const {
    if (smokes_.empty()) return false;

    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float len2 = dx * dx + dy * dy;
    if (len2 < 0.0001f) return false;

    for (const auto& s : smokes_) {
        // 点到线段最短距离，与烟雾半径比较
        float t = ((s.x - x1) * dx + (s.y - y1) * dy) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
        const float px = x1 + dx * t;
        const float py = y1 + dy * t;
        const float ddx = s.x - px;
        const float ddy = s.y - py;
        if (ddx * ddx + ddy * ddy <= s.radius * s.radius) return true;
    }
    return false;
}

// ============================================================================
// 死亡处理
// ============================================================================

void CS2DGame::OnCombatantKilled(Player& victim, AudioManager* audio) {
    if (!victim.alive) return;

    victim.alive = false;
    victim.health = 0;
    victim.deathTimer = 0.0f;
    victim.vx = victim.vy = 0.0f;
    victim.reloading = false;
    ++victim.deaths;

    // 记录阵亡点：队友会尝试「补枪」（trade）
    lastDeathX_      = victim.x;
    lastDeathY_      = victim.y;
    lastDeathTeam_   = victim.team;
    lastDeathTimer_  = 3.5f;

    if (&victim == &player_) {
        ++playerDeaths_;
        if (audio) audio->PlaySound(SoundEffect::KILL);
        if (mode_ == MatchMode::DeathMatch) botRespawnTimer_ = BOT_RESPAWN_DELAY;
        std::cout << "[CS2D] 玩家阵亡 (累计 " << playerDeaths_ << ")" << std::endl;
    } else {
        // BOT 阵亡由击杀方计分；此处只需统计自身
        std::cout << "[CS2D] BOT 阵亡 (" << TeamName(victim.team) << ")" << std::endl;
    }
}

// ============================================================================
// 统一伤害入口
// ============================================================================

bool CS2DGame::ApplyDamage(Player& victim, int amount) {
    if (!victim.alive || amount <= 0) return false;

    int remaining = amount;

    // 护甲吸收：头盔提高吸收比例；护甲按吸收量等量消耗
    if (victim.armor > 0) {
        const float frac = victim.helmet ? 0.60f : 0.50f;
        int absorbed = static_cast<int>(static_cast<float>(amount) * frac + 0.5f);
        absorbed = std::min(absorbed, victim.armor);
        victim.armor -= absorbed;
        remaining -= absorbed;
        if (victim.armor <= 0) victim.helmet = false;   // 护甲耗尽，头盔失效
    }

    victim.health -= remaining;
    if (victim.health <= 0) {
        victim.health = 0;
        return true;
    }
    return false;
}

// ============================================================================
// 换弹 / 视野 / 安全距离
// ============================================================================

void CS2DGame::StartReload(Player& p) {
    p.reloading = true;
    p.reloadTimer = Player::RELOAD_TIME;
}

void CS2DGame::UpdateReload(Player& p, float dt) {
    if (!p.reloading) return;
    p.reloadTimer -= dt;
    if (p.reloadTimer <= 0.0f) {
        p.reloading = false;
        p.reloadTimer = 0.0f;
        p.ammo = p.maxAmmo;
    }
}

bool CS2DGame::IsEnemyNearby(float x, float y, float safeRadius, Team myTeam) const {
    auto near = [&](const Player& u) {
        if (!u.alive || u.team == myTeam) return false;
        const float dx = u.x - x;
        const float dy = u.y - y;
        return dx * dx + dy * dy < safeRadius * safeRadius;
    };

    if (near(player_)) return true;
    for (const auto& bot : bots_) if (near(bot)) return true;
    return false;
}

