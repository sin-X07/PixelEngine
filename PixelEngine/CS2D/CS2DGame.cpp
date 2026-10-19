#include "CS2DGame.hpp"
#include "TMJParser.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>

CS2DGame::CS2DGame()
    : rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())) {
    world_ = std::make_unique<CS2DWorld>();
    std::random_device rd;
    rng_.seed(rd());
}

bool CS2DGame::LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer) {
    renderer_ = renderer;

    // 1. Parse TMJ
    CS2DMapData mapData;
    if (!TMJParser::Parse(resolvedTMJPath, mapData)) {
        return false;
    }

    // 2. Build world
    if (!world_->Build(mapData)) {
        return false;
    }

    // 3. Set renderer
    if (renderer_) {
        std::string mapDir;
        const auto slashPos = resolvedTMJPath.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            mapDir = resolvedTMJPath.substr(0, slashPos);
        }
        renderer_->SetWorld(world_.get());
    }

    // Clear stale BOTs
    bots_.clear();
    botExecutors_.clear();
    bullets_.clear();
    playerKills_ = 0;
    playerDeaths_ = 0;

    SpawnPlayer();
    for (int i = 0; i < ACTIVE_BOTS; ++i) {
        Player bot;
        SpawnBOT(bot, i % 2 != 0);
        bots_.push_back(bot);
        botExecutors_.emplace_back();
    }

    std::cout << "[CS2DGame] Map loaded, player + " << bots_.size() << " BOTs ready" << std::endl;
    return true;
}

void CS2DGame::Reset() {
    bullets_.clear();
    bots_.clear();
    botExecutors_.clear();
    playerKills_  = 0;
    playerDeaths_ = 0;
    botRespawnTimer_ = 0.0f;
    SpawnPlayer();
    for (int i = 0; i < ACTIVE_BOTS; ++i) {
        Player bot;
        SpawnBOT(bot, i % 2 != 0);
        bots_.push_back(bot);
        botExecutors_.emplace_back();
    }
}

// ========= Spawn point allocation =========

bool CS2DGame::PickRandomSpawn(float& outX, float& outY) {
    const auto& spawns = world_->GetSpawnPoints();
    if (spawns.empty()) {
        outX = world_->GetMapPixelW() * 0.5f;
        outY = world_->GetMapPixelH() * 0.5f;
        return false;
    }
    constexpr float HITBOX = 6.0f;
    std::uniform_int_distribution<size_t> pick(0, spawns.size() - 1);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto& sp = spawns[pick(rng_)];
        if (!world_->CheckRectCollision(sp.x - HITBOX * 0.5f, sp.y - HITBOX * 0.5f,
                                        HITBOX, HITBOX)) {
            outX = sp.x;
            outY = sp.y;
            return true;
        }
    }
    outX = spawns[0].x;
    outY = spawns[0].y;
    return true;
}

void CS2DGame::SpawnPlayer() {
    player_.alive   = true;
    player_.health  = 100;
    player_.maxHealth = 100;
    player_.vx = player_.vy = 0.0f;
    player_.aimAngle = 0.0f;
    player_.shootCooldown = 0.0f;
    player_.isCT = true;
    player_.animState  = AnimState::Idle;
    player_.animTimer  = 0.0f;
    player_.deathTimer = 0.0f;
    player_.shootAnimTimer = 0.0f;
    player_.spriteKey  = 0;
    player_.weaponType = (rng_() % 2);
    player_.weaponSlot = 0;
    player_.ammo      = 30;
    player_.maxAmmo    = 30;
    player_.reloading  = false;
    player_.reloadTimer = 0.0f;
    player_.stealth    = false;

    slotAmmo_[0]    = 30;
    slotMaxAmmo_[0] = 30;
    slotAmmo_[1]    = 7;
    slotMaxAmmo_[1] = 7;

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickRandomSpawn(player_.x, player_.y);
        if (!IsEnemyNearby(player_.x, player_.y, 120.0f)) break;
    }
}

void CS2DGame::SpawnBOT(Player& bot, bool isCT) {
    bot.alive   = true;
    bot.health  = 100;
    bot.maxHealth = 100;
    bot.vx = bot.vy = 0.0f;
    bot.shootCooldown = 0.0f;
    bot.isCT = isCT;
    bot.animState  = AnimState::Idle;
    bot.animTimer  = 0.0f;
    bot.deathTimer = 0.0f;
    bot.shootAnimTimer = 0.0f;
    bot.ammo       = 30;
    bot.maxAmmo    = 30;
    bot.reloading  = false;
    bot.reloadTimer = 0.0f;
    bot.stealth    = false;
    bot.spriteKey   = isCT ? 0 : (1 + static_cast<int>(rng_() % 2));
    bot.weaponType  = (rng_() % 2);
    bot.weaponSlot  = static_cast<int>(rng_() % 2);
    bot.llmIntent.Reset();

    bot.aimAngle = angleDist_(rng_);
    for (int attempt = 0; attempt < 20; ++attempt) {
        PickRandomSpawn(bot.x, bot.y);
        const float pdx = player_.x - bot.x;
        const float pdy = player_.y - bot.y;
        const float pdist2 = pdx * pdx + pdy * pdy;
        if (pdist2 < 120.0f * 120.0f) continue;
        if (!IsEnemyNearby(bot.x, bot.y, 120.0f)) break;
    }
}

// ========= Weapon slot switch =========

void CS2DGame::SwitchWeaponSlot(int slot) {
    if (slot < 0 || slot > 2) return;
    const int oldSlot = player_.weaponSlot;
    if (oldSlot == 0 || oldSlot == 1) {
        slotAmmo_[oldSlot]    = player_.ammo;
        slotMaxAmmo_[oldSlot] = player_.maxAmmo;
    }
    player_.weaponSlot = slot;
    if (slot == 0 || slot == 1) {
        player_.ammo    = slotAmmo_[slot];
        player_.maxAmmo = slotMaxAmmo_[slot];
    } else {
        player_.ammo    = 1;
        player_.maxAmmo = 1;
    }
    player_.reloading = false;
    player_.reloadTimer = 0.0f;
    static const char* kSlotNames[] = { "main", "sidearm", "melee" };
    std::cout << "[CS2D] Switched to " << kSlotNames[slot] << std::endl;
}

std::string CS2DGame::GetWeaponName() const {
    switch (player_.weaponSlot) {
        case 0: return (player_.weaponType == 0) ? "AK-47" : "M4A1";
        case 1: return "Deagle";
        case 2: return "Knife";
        default: return "Rifle";
    }
}

// ========= Per-frame update =========

void CS2DGame::Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                       bool wPressed, bool aPressed, bool sPressed, bool dPressed,
                       bool shiftPressed, bool rPressed,
                       AudioManager* audio) {
    if (!world_) return;

    player_.stealth = shiftPressed;

    if (rPressed && !player_.reloading) {
        if ((player_.weaponSlot == 0 || player_.weaponSlot == 1) && player_.ammo < player_.maxAmmo) {
            StartReload(player_);
        }
    }

    if (player_.alive) {
        UpdatePlayerMovement(dt, wPressed, aPressed, sPressed, dPressed, player_.stealth);
        UpdatePlayerShooting(dt, mouseLeftDown, mouseWorldX, mouseWorldY, audio);
        UpdateReload(player_, dt);
    } else {
        botRespawnTimer_ -= dt;
        if (botRespawnTimer_ <= 0.0f) {
            SpawnPlayer();
        }
    }

    UpdateBOTs(dt, audio);
    UpdateBullets(dt, audio);

    // Animation state machine
    auto updateAnim = [](Player& p) {
        if (!p.alive) {
            p.animState = AnimState::Dead;
        } else if (p.shootAnimTimer > 0.0f) {
            p.animState = AnimState::Shoot;
        } else if (std::abs(p.vx) > 5.0f || std::abs(p.vy) > 5.0f) {
            p.animState = AnimState::Walk;
        } else {
            p.animState = AnimState::Idle;
        }
    };
    if (player_.alive) {
        if (player_.shootAnimTimer > 0.0f) player_.shootAnimTimer -= dt;
    } else {
        player_.deathTimer += dt;
    }
    player_.animTimer += dt;
    updateAnim(player_);
    for (auto& bot : bots_) {
        if (bot.shootAnimTimer > 0.0f) bot.shootAnimTimer -= dt;
        if (!bot.alive) bot.deathTimer += dt;
        bot.animTimer += dt;
        updateAnim(bot);
        UpdateReload(bot, dt);
    }
    prevMouseDown_ = mouseLeftDown;
}

// ========= Player movement =========

void CS2DGame::UpdatePlayerMovement(float dt, bool w, bool a, bool s, bool d, bool stealth) {
    constexpr float ACCEL   = 600.0f;
    constexpr float FRICTION = 8.0f;
    float maxSpeed = stealth ? 25.0f : 60.0f;

    int dx = (d ? 1 : 0) - (a ? 1 : 0);
    int dy = (s ? 1 : 0) - (w ? 1 : 0);

    float len = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    if (len > 0.001f) {
        player_.vx += (dx / len) * ACCEL * dt;
        player_.vy += (dy / len) * ACCEL * dt;
    } else {
        player_.vx -= player_.vx * FRICTION * dt;
        player_.vy -= player_.vy * FRICTION * dt;
        if (std::abs(player_.vx) < 1.0f) player_.vx = 0.0f;
        if (std::abs(player_.vy) < 1.0f) player_.vy = 0.0f;
    }

    float speed = std::sqrt(player_.vx * player_.vx + player_.vy * player_.vy);
    if (speed > maxSpeed) {
        player_.vx = player_.vx / speed * maxSpeed;
        player_.vy = player_.vy / speed * maxSpeed;
    }

    MoveWithCollision(player_, dt);
}

void CS2DGame::MoveWithCollision(Player& entity, float dt) {
    const float halfSize = entity.size * 0.5f;

    float newX = entity.x + entity.vx * dt;
    if (!world_->CheckRectCollision(newX - halfSize, entity.y - halfSize,
                                     entity.size, entity.size)) {
        entity.x = newX;
    } else {
        entity.vx = 0.0f;
    }

    float newY = entity.y + entity.vy * dt;
    if (!world_->CheckRectCollision(entity.x - halfSize, newY - halfSize,
                                     entity.size, entity.size)) {
        entity.y = newY;
    } else {
        entity.vy = 0.0f;
    }

    entity.x = std::clamp(entity.x, 0.0f, world_->GetMapPixelW() - 1.0f);
    entity.y = std::clamp(entity.y, 0.0f, world_->GetMapPixelH() - 1.0f);
}

// ========= Weapon slot params =========
static WeaponSlotParams GetSlotParams(int slot) {
    switch (slot) {
        case 1:  return { 35.0f, 0.40f, 700.0f, 700.0f };
        case 2:  return { 50.0f, 0.50f, 900.0f, 45.0f  };
        default: return { 25.0f, 0.12f, 600.0f, 800.0f };
    }
}

// ========= Player shooting =========

void CS2DGame::UpdatePlayerShooting(float dt, bool mouseDown, float worldMX, float worldMY, AudioManager* audio) {
    if (!player_.alive) return;

    const float dx = worldMX - player_.x;
    const float dy = worldMY - player_.y;
    player_.aimAngle = std::atan2(dy, dx);

    if (player_.reloading) return;

    if (player_.shootCooldown > 0.0f) {
        player_.shootCooldown -= dt;
    }

    const bool mouseEdge = mouseDown && !prevMouseDown_;
    if (player_.weaponSlot == 1) {
        if (!mouseEdge || player_.shootCooldown > 0.0f) return;
    } else {
        if (!mouseDown || player_.shootCooldown > 0.0f) return;
    }

    const WeaponSlotParams wp = GetSlotParams(player_.weaponSlot);

    if (player_.weaponSlot == 2) {
        for (auto& bot : bots_) {
            if (!bot.alive) continue;
            const float bdx = bot.x - player_.x;
            const float bdy = bot.y - player_.y;
            const float bdist = std::sqrt(bdx * bdx + bdy * bdy);
            if (bdist < wp.range) {
                bot.health -= static_cast<int>(wp.damage);
                if (bot.health <= 0) {
                    bot.alive = false;
                    bot.health = 0;
                    bot.deathTimer = 0.0f;
                    ++playerKills_;
                    std::cout << "[CS2D] BOT killed! Total: " << playerKills_ << " kills" << std::endl;
                }
            }
        }
        player_.shootCooldown = wp.cooldown;
        player_.shootAnimTimer = 0.12f;
        if (audio) audio->PlaySound(SoundEffect::KATANA_SWING);
        return;
    }

    if (player_.ammo <= 0) {
        StartReload(player_);
        return;
    }

    FireBullet(player_.x, player_.y, player_.aimAngle, wp, -1);
    player_.ammo--;
    player_.shootCooldown = wp.cooldown;
    player_.shootAnimTimer = 0.12f;

    if (audio) {
        if (player_.weaponSlot == 0) {
            audio->PlaySound(SoundEffect::AK47_FIRE);
        } else {
            audio->PlaySound(SoundEffect::SHOOT);
        }
    }
}

void CS2DGame::FireBullet(float x, float y, float angle, const WeaponSlotParams& wp, int ownerIdx) {
    Bullet b;
    b.x = x;
    b.y = y;
    b.vx = std::cos(angle) * wp.speed;
    b.vy = std::sin(angle) * wp.speed;
    b.traveled = 0.0f;
    b.maxDist  = wp.range;
    b.damage   = static_cast<int>(wp.damage);
    b.ownerIdx = ownerIdx;
    b.active   = true;
    bullets_.push_back(b);
}

// ========= BOT AI (layered: LLM intent -> BotBehaviorExecutor -> Move/Shoot) =========

void CS2DGame::UpdateBOTs(float dt, AudioManager* audio) {
    while (botExecutors_.size() < bots_.size()) {
        botExecutors_.emplace_back();
    }

    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& bot = bots_[i];
        if (!bot.alive) continue;

        const bool canSee = BotCanSeePlayer(bot);
        const float pdx = player_.x - bot.x;
        const float pdy = player_.y - bot.y;
        const float dist = std::sqrt(pdx * pdx + pdy * pdy);

        bot.llmIntent.Tick(dt);

        BehaviorOutput out = botExecutors_[i].Execute(
            bot.llmIntent,
            bot.x, bot.y,
            player_.x, player_.y,
            canSee, dist,
            bot.ammo > 0, bot.reloading, bot.weaponSlot,
            rng_, dt);

        // Layer 1: Movement
        bot.vx = out.vx;
        bot.vy = out.vy;
        MoveWithCollision(bot, dt);

        // Layer 1: Aim
        if (canSee) bot.aimAngle = out.aimAngle;

        // Layer 1: Shoot
        if (out.shouldShoot && canSee && bot.weaponSlot < 2) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;

            if (bot.shootCooldown <= 0.0f) {
                const WeaponSlotParams wp = GetSlotParams(bot.weaponSlot);
                const float effectiveRange = wp.range * (player_.stealth ? 0.5f : 1.0f);

                if (dist < effectiveRange + 200.0f) {
                    if (bot.ammo <= 0) {
                        StartReload(bot);
                        continue;
                    }

                    float spreadAngle = 0.0f;
                    std::uniform_real_distribution<float> hitRoll(0.0f, 1.0f);
                    if (hitRoll(rng_) < 0.30f) {
                        std::uniform_real_distribution<float> jitter(-0.08f * out.spreadMul,
                                                                      0.08f * out.spreadMul);
                        spreadAngle = jitter(rng_);
                    } else {
                        std::uniform_real_distribution<float> missSpread(-0.8f * out.spreadMul,
                                                                          0.8f * out.spreadMul);
                        spreadAngle = missSpread(rng_);
                    }

                    const float aimAngle = std::atan2(pdy, pdx) + spreadAngle;
                    FireBullet(bot.x, bot.y, aimAngle, wp, static_cast<int>(i));
                    bot.ammo--;
                    bot.shootCooldown = wp.cooldown * 1.8f * out.shootCooldownMul;
                    bot.shootAnimTimer = 0.12f;

                    if (audio) {
                        if (bot.weaponSlot == 0)
                            audio->PlaySoundAt(SoundEffect::AK47_FIRE, bot.x, bot.y,
                                               player_.x, player_.y, player_.aimAngle);
                        else if (bot.weaponSlot == 1)
                            audio->PlaySoundAt(SoundEffect::SHOOT, bot.x, bot.y,
                                               player_.x, player_.y, player_.aimAngle);
                    }
                }
            }
        } else if (bot.weaponSlot == 2 && canSee && dist < 20.0f && player_.alive) {
            const WeaponSlotParams wp = GetSlotParams(2);
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;
            if (bot.shootCooldown <= 0.0f) {
                player_.health -= static_cast<int>(wp.damage * 0.33f);
                if (player_.health <= 0) {
                    player_.alive = false;
                    player_.health = 0;
                    player_.deathTimer = 0.0f;
                    ++playerDeaths_;
                    botRespawnTimer_ = BOT_RESPAWN_DELAY;
                    std::cout << "[CS2D] Player died! Total: " << playerDeaths_ << " deaths" << std::endl;
                }
                bot.shootCooldown = wp.cooldown * 1.5f * out.shootCooldownMul;
                bot.shootAnimTimer = 0.12f;
            }
        }
    }

    // BOT respawn
    for (auto& bot : bots_) {
        if (!bot.alive) {
            bot.llmIntent.Reset();
            RespawnBOT(bot);
        }
    }

    // Maintain BOT count
    while (static_cast<int>(bots_.size()) < ACTIVE_BOTS) {
        Player bot;
        bot.llmIntent.Reset();
        SpawnBOT(bot, bots_.size() % 2 != 0);
        bots_.push_back(bot);
        botExecutors_.emplace_back();
    }
}

void CS2DGame::RespawnBOT(Player& bot) {
    SpawnBOT(bot, bot.isCT);
}

// ========= Reload system =========

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

// ========= BOT vision =========

bool CS2DGame::BotCanSeePlayer(const Player& bot) const {
    if (!player_.alive) return false;
    const float dx = player_.x - bot.x;
    const float dy = player_.y - bot.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > BOT_VIEW_RADIUS) return false;
    if (!renderer_) return true;
    return renderer_->HasLineOfSight(bot.x, bot.y, player_.x, player_.y);
}

bool CS2DGame::PlayerCanSeeBot(const Player& bot) const {
    if (!bot.alive) return false;
    if (!renderer_) return true;   // 无渲染器时保持旧行为
    // 距离 + 120° 扇形 + 墙体遮挡，与视野遮罩共用同一套几何
    return renderer_->IsVisibleToPlayer(player_.x, player_.y, player_.aimAngle,
                                        bot.x, bot.y);
}

// ========= Safe distance check =========

bool CS2DGame::IsEnemyNearby(float x, float y, float safeRadius) const {
    for (const auto& bot : bots_) {
        if (!bot.alive) continue;
        const float dx = bot.x - x;
        const float dy = bot.y - y;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 < safeRadius * safeRadius) return true;
    }
    return false;
}

// ========= Bullet update =========

void CS2DGame::UpdateBullets(float dt, AudioManager* audio) {
    for (auto& bullet : bullets_) {
        if (!bullet.active) continue;

        const float stepX = bullet.vx * dt;
        const float stepY = bullet.vy * dt;
        bullet.x += stepX;
        bullet.y += stepY;
        bullet.traveled += std::sqrt(stepX * stepX + stepY * stepY);

        if (bullet.traveled >= bullet.maxDist) {
            bullet.active = false;
            continue;
        }

        int tileCol = 0, tileRow = 0;
        world_->WorldToTile(bullet.x, bullet.y, tileRow, tileCol);
        if (world_->IsBlockingBullet(tileRow, tileCol)) {
            bullet.active = false;
            continue;
        }

        if (bullet.ownerIdx == -1) {
            for (auto& bot : bots_) {
                if (!bot.alive) continue;
                const float bdx = bullet.x - bot.x;
                const float bdy = bullet.y - bot.y;
                const float bdist2 = bdx * bdx + bdy * bdy;
                const float hitR = bot.size * 0.5f + 2.0f;
                if (bdist2 < hitR * hitR) {
                    bot.health -= bullet.damage;
                    bullet.active = false;
                    if (bot.health <= 0) {
                        bot.alive  = false;
                        bot.health = 0;
                        bot.deathTimer = 0.0f;
                        ++playerKills_;
                        if (audio) audio->PlaySoundAt(SoundEffect::KILL, bot.x, bot.y,
                                                        player_.x, player_.y, player_.aimAngle);
                        std::cout << "[CS2D] BOT killed! Total: " << playerKills_ << " kills" << std::endl;
                    } else {
                        if (audio) audio->PlaySoundAt(SoundEffect::HIT, bot.x, bot.y,
                                                        player_.x, player_.y, player_.aimAngle);
                    }
                    break;
                }
            }
        }
        if (bullet.active && bullet.ownerIdx >= 0 && player_.alive) {
            const float pdx = bullet.x - player_.x;
            const float pdy = bullet.y - player_.y;
            const float pdist2 = pdx * pdx + pdy * pdy;
            const float hitR = player_.size * 0.5f + 2.0f;
            if (pdist2 < hitR * hitR) {
                player_.health -= bullet.damage;
                bullet.active = false;
                if (audio) audio->PlaySound(SoundEffect::HIT);
                if (player_.health <= 0) {
                    player_.alive  = false;
                    player_.health = 0;
                    player_.deathTimer = 0.0f;
                    ++playerDeaths_;
                    botRespawnTimer_ = BOT_RESPAWN_DELAY;
                    if (audio) audio->PlaySound(SoundEffect::KILL);
                    std::cout << "[CS2D] Player died! Total: " << playerDeaths_ << " deaths" << std::endl;
                }
            }
        }
    }

    bullets_.erase(
        std::remove_if(bullets_.begin(), bullets_.end(),
                       [](const Bullet& b) { return !b.active; }),
        bullets_.end());
}

// ========= Render =========

void CS2DGame::Render(int viewW, int viewH) {
    if (!renderer_ || !world_) return;

    renderer_->UpdateCamera(player_.x, player_.y, viewW, viewH);
    renderer_->DrawWorld();

    auto fillParams = [](const Player& p, SpriteDrawParams& sp) {
        sp.x          = p.x;
        sp.y          = p.y;
        sp.size       = p.size;
        sp.aimAngle   = p.aimAngle;
        sp.spriteKey  = p.spriteKey;
        sp.weaponType = p.weaponType;
        sp.weaponSlot = p.weaponSlot;
        sp.animState  = static_cast<int>(p.animState);
        sp.animTimer  = p.animTimer;
        sp.deathTimer = p.deathTimer;
        sp.alpha      = 255;
    };

    // 玩家存活时按视野剔除敌人；阵亡后不绘制视野遮罩，故也跳过剔除
    const bool applyVisionCull = player_.alive;
    for (const auto& bot : bots_) {
        if (!bot.alive && bot.deathTimer > 2.0f) continue;
        if (applyVisionCull && !PlayerCanSeeBot(bot)) continue;
        SpriteDrawParams sp;
        fillParams(bot, sp);
        renderer_->DrawCharacter(sp);
    }

    if (player_.alive || player_.deathTimer <= 2.0f) {
        SpriteDrawParams sp;
        fillParams(player_, sp);
        renderer_->DrawCharacter(sp);
    }

    for (const auto& b : bullets_) {
        if (!b.active) continue;
        renderer_->DrawBullet(b.x, b.y, b.vx, b.vy);
    }

    if (player_.alive) {
        renderer_->DrawVisionFog(player_.x, player_.y, player_.aimAngle);
    }

    renderer_->DrawOverlay();
    renderer_->DrawWeaponStatus(player_.weaponSlot, player_.weaponType, viewW, viewH);
}