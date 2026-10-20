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

// ============================================================================
// 地图载入 / 重置
// ============================================================================

bool CS2DGame::LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer) {
    renderer_ = renderer;

    CS2DMapData mapData;
    if (!TMJParser::Parse(resolvedTMJPath, mapData)) return false;
    if (!world_->Build(mapData)) return false;

    if (renderer_) renderer_->SetWorld(world_.get());

    ResetToNewMatch();
    return true;
}

// 开始一场全新比赛（清空比分与统计）
void CS2DGame::ResetToNewMatch() {
    bots_.clear();
    botExecutors_.clear();
    bullets_.clear();
    playerKills_ = 0;
    playerDeaths_ = 0;
    matchOverAnnounced_ = false;

    teams_.Reset();
    teams_.SetScoreToWin(8);
    round_.Reset(RoundConfig{});
    prevPhase_ = round_.Phase();

    player_.team = Team::CT;
    player_.kills = 0;
    player_.deaths = 0;
    SpawnPlayer();

    // CT 侧：玩家 + (TEAM_SIZE-1) 个 BOT；T 侧：TEAM_SIZE 个 BOT
    for (int i = 0; i < TOTAL_BOTS; ++i) {
        const Team t = (i < TEAM_SIZE - 1) ? Team::CT : Team::T;
        Player bot;
        SpawnBOT(bot, t);
        bots_.push_back(bot);
        botExecutors_.emplace_back();
    }

    std::cout << "[CS2DGame] 对局开始: " << TEAM_SIZE << "v" << TEAM_SIZE
              << " (玩家 CT + " << bots_.size() << " BOT)" << std::endl;
}

void CS2DGame::Reset() {
    ResetToNewMatch();
}

// ============================================================================
// 出生点
// ============================================================================

bool CS2DGame::PickSpawnForTeam(Team team, float& outX, float& outY) {
    const bool isCT = (team == Team::CT);
    std::vector<const CS2DWorld::SpawnPoint*> candidates = world_->SpawnsForTeam(isCT);
    if (candidates.empty()) {
        outX = world_->GetMapPixelW() * 0.5f;
        outY = world_->GetMapPixelH() * 0.5f;
        return false;
    }

    constexpr float HITBOX = 6.0f;
    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    for (int attempt = 0; attempt < 12; ++attempt) {
        const auto* sp = candidates[pick(rng_)];
        if (!world_->CheckRectCollision(sp->x - HITBOX * 0.5f, sp->y - HITBOX * 0.5f,
                                        HITBOX, HITBOX)) {
            outX = sp->x;
            outY = sp->y;
            return true;
        }
    }
    outX = candidates[0]->x;
    outY = candidates[0]->y;
    return true;
}

void CS2DGame::SpawnPlayer() {
    player_.alive      = true;
    player_.health     = 100;
    player_.maxHealth  = 100;
    player_.vx = player_.vy = 0.0f;
    player_.shootCooldown = 0.0f;
    player_.team       = Team::CT;
    player_.animState  = AnimState::Idle;
    player_.animTimer  = 0.0f;
    player_.deathTimer = 0.0f;
    player_.shootAnimTimer = 0.0f;
    player_.spriteKey  = 0;            // CT 头盔
    player_.weaponType = 1;            // M4A1
    player_.weaponSlot = 0;
    player_.ammo       = 30;
    player_.maxAmmo    = 30;
    player_.reloading  = false;
    player_.reloadTimer = 0.0f;
    player_.stealth    = false;

    slotAmmo_[0]    = 30;
    slotMaxAmmo_[0] = 30;
    slotAmmo_[1]    = 7;
    slotMaxAmmo_[1] = 7;

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(Team::CT, player_.x, player_.y);
        if (!IsEnemyNearby(player_.x, player_.y, 120.0f)) break;
    }
}

void CS2DGame::SpawnBOT(Player& bot, Team team) {
    bot.alive      = true;
    bot.health     = 100;
    bot.maxHealth  = 100;
    bot.vx = bot.vy = 0.0f;
    bot.shootCooldown = 0.0f;
    bot.team       = team;
    bot.animState  = AnimState::Idle;
    bot.animTimer  = 0.0f;
    bot.deathTimer = 0.0f;
    bot.shootAnimTimer = 0.0f;
    bot.ammo       = 30;
    bot.maxAmmo    = 30;
    bot.reloading  = false;
    bot.reloadTimer = 0.0f;
    bot.stealth    = false;
    bot.llmIntent.Reset();

    // 外观：CT 用头盔，T 用两种无头盔外观
    if (team == Team::CT) {
        bot.spriteKey = 0;
        bot.weaponType = 1;            // M4A1
    } else {
        bot.spriteKey = 1 + static_cast<int>(rng_() % 2);
        bot.weaponType = 0;            // AK47
    }
    bot.weaponSlot = 0;
    bot.aimAngle = angleDist_(rng_);

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(team, bot.x, bot.y);
        if (!IsEnemyNearby(bot.x, bot.y, 40.0f)) break;
    }
}

void CS2DGame::RespawnBOT(Player& bot) {
    SpawnBOT(bot, bot.team);
}

// ============================================================================
// 武器
// ============================================================================

void CS2DGame::SwitchWeaponSlot(int slot) {
    if (slot < 0 || slot > 2) return;
    if (mode_ == MatchMode::TeamElimination && round_.IsFrozen()) return;  // 冻结期禁切换

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
}

std::string CS2DGame::GetWeaponName() const {
    switch (player_.weaponSlot) {
        case 0: return (player_.weaponType == 0) ? "AK-47" : "M4A1";
        case 1: return "Deagle";
        case 2: return "Knife";
        default: return "Rifle";
    }
}

// ============================================================================
// 每帧更新
// ============================================================================

void CS2DGame::Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                      bool wPressed, bool aPressed, bool sPressed, bool dPressed,
                      bool shiftPressed, bool rPressed,
                      AudioManager* audio) {
    if (!world_) return;

    UpdateRoundFlow(dt, audio);

    const bool frozen  = (mode_ == MatchMode::TeamElimination) && round_.IsFrozen();
    const bool ending  = (mode_ == MatchMode::TeamElimination) && round_.IsRoundEnd();
    const bool canAct  = !frozen && !ending;

    player_.stealth = shiftPressed && canAct;

    if (canAct && rPressed && !player_.reloading) {
        if ((player_.weaponSlot == 0 || player_.weaponSlot == 1) && player_.ammo < player_.maxAmmo) {
            StartReload(player_);
        }
    }

    if (player_.alive && canAct) {
        UpdatePlayerMovement(dt, wPressed, aPressed, sPressed, dPressed, player_.stealth);
        UpdatePlayerShooting(dt, mouseLeftDown, mouseWorldX, mouseWorldY, audio);
    } else {
        player_.vx = player_.vy = 0.0f;

        // 死斗模式：玩家阵亡后自动重生
        if (mode_ == MatchMode::DeathMatch && !player_.alive) {
            botRespawnTimer_ -= dt;
            if (botRespawnTimer_ <= 0.0f) SpawnPlayer();
        }
    }
    UpdateReload(player_, dt);

    if (canAct) {
        UpdateBOTs(dt, audio);
    } else {
        // 冻结/结算期：BOT 原地待命
        for (auto& bot : bots_) { bot.vx = bot.vy = 0.0f; }
    }

    UpdateBullets(dt, audio);
    CheckWinConditions(audio);

    // ---- 动画状态机 ----
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

    if (teams_.MatchOver() && !matchOverAnnounced_) {
        matchOverAnnounced_ = true;
        std::cout << "[CS2D] 比赛结束! " << TeamName(teams_.MatchWinner())
                  << " 获胜 (" << teams_.ScoreT() << ":" << teams_.ScoreCT() << ")" << std::endl;
    }
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

// 武器槽参数（Phase 3 将替换为数据驱动表）
static WeaponSlotParams GetSlotParams(int slot) {
    switch (slot) {
        case 1:  return { 35.0f, 0.40f, 700.0f, 700.0f };
        case 2:  return { 50.0f, 0.50f, 900.0f, 45.0f  };
        default: return { 25.0f, 0.12f, 600.0f, 800.0f };
    }
}

void CS2DGame::UpdatePlayerShooting(float dt, bool mouseDown, float worldMX, float worldMY,
                                    AudioManager* audio) {
    if (!player_.alive) return;

    player_.aimAngle = std::atan2(worldMY - player_.y, worldMX - player_.x);

    if (player_.reloading) return;
    if (player_.shootCooldown > 0.0f) player_.shootCooldown -= dt;

    const bool mouseEdge = mouseDown && !prevMouseDown_;
    if (player_.weaponSlot == 1) {
        if (!mouseEdge || player_.shootCooldown > 0.0f) return;
    } else {
        if (!mouseDown || player_.shootCooldown > 0.0f) return;
    }

    const WeaponSlotParams wp = GetSlotParams(player_.weaponSlot);

    // 近战
    if (player_.weaponSlot == 2) {
        for (auto& bot : bots_) {
            if (!bot.alive || bot.team == player_.team) continue;
            const float bdx = bot.x - player_.x;
            const float bdy = bot.y - player_.y;
            if (std::sqrt(bdx * bdx + bdy * bdy) < wp.range) {
                bot.health -= static_cast<int>(wp.damage);
                if (bot.health <= 0) OnCombatantKilled(bot, audio);
            }
        }
        player_.shootCooldown = wp.cooldown;
        player_.shootAnimTimer = 0.12f;
        if (audio) audio->PlaySound(SoundEffect::KATANA_SWING);
        return;
    }

    if (player_.ammo <= 0) { StartReload(player_); return; }

    FireBullet(player_.x, player_.y, player_.aimAngle, wp, -1, player_.team);
    player_.ammo--;
    player_.shootCooldown = wp.cooldown;
    player_.shootAnimTimer = 0.12f;

    if (audio) {
        if (player_.weaponSlot == 0) audio->PlaySound(SoundEffect::AK47_FIRE);
        else                         audio->PlaySound(SoundEffect::SHOOT);
    }
}

void CS2DGame::FireBullet(float x, float y, float angle, const WeaponSlotParams& wp,
                          int ownerIdx, Team ownerTeam) {
    Bullet b;
    b.x = x;
    b.y = y;
    b.vx = std::cos(angle) * wp.speed;
    b.vy = std::sin(angle) * wp.speed;
    b.traveled = 0.0f;
    b.maxDist  = wp.range;
    b.damage   = static_cast<int>(wp.damage);
    b.ownerIdx = ownerIdx;
    b.ownerTeam = ownerTeam;
    b.active   = true;
    bullets_.push_back(b);
}

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

        // 视线判定（距离 + LOS）
        bool canSee = false;
        if (targetDist <= BOT_VIEW_RADIUS) {
            canSee = !renderer_ || renderer_->HasLineOfSight(bot.x, bot.y, target->x, target->y);
        }

        bot.llmIntent.Tick(dt);

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

        if (out.shouldShoot && canSee && bot.weaponSlot < 2) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;

            if (bot.shootCooldown <= 0.0f) {
                const WeaponSlotParams wp = GetSlotParams(bot.weaponSlot);
                if (targetDist < wp.range + 200.0f) {
                    if (bot.ammo <= 0) { StartReload(bot); continue; }

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

                    const float aim = std::atan2(target->y - bot.y, target->x - bot.x) + spreadAngle;
                    FireBullet(bot.x, bot.y, aim, wp, static_cast<int>(i), bot.team);
                    bot.ammo--;
                    bot.shootCooldown = wp.cooldown * 1.8f * out.shootCooldownMul;
                    bot.shootAnimTimer = 0.12f;

                    if (audio && bot.weaponSlot == 0) {
                        audio->PlaySoundAt(SoundEffect::AK47_FIRE, bot.x, bot.y,
                                           player_.x, player_.y, player_.aimAngle);
                    }
                }
            }
        } else if (bot.weaponSlot == 2 && canSee && targetDist < 20.0f && target->alive) {
            const WeaponSlotParams wp = GetSlotParams(2);
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;
            if (bot.shootCooldown <= 0.0f) {
                if (target == &player_) {
                    player_.health -= static_cast<int>(wp.damage * 0.33f);
                    if (player_.health <= 0) OnCombatantKilled(player_, audio);
                }
                bot.shootCooldown = wp.cooldown * 1.5f * out.shootCooldownMul;
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

            bot.health -= bullet.damage;
            bullet.active = false;

            if (bot.health <= 0) {
                OnCombatantKilled(bot, audio);
                if (audio) audio->PlaySoundAt(SoundEffect::KILL, bot.x, bot.y,
                                              player_.x, player_.y, player_.aimAngle);
            } else if (audio) {
                audio->PlaySoundAt(SoundEffect::HIT, bot.x, bot.y,
                                   player_.x, player_.y, player_.aimAngle);
            }
            break;
        }

        // ---- 命中玩家 ----
        if (bullet.active && player_.alive && player_.team != bullet.ownerTeam) {
            const float dx = bullet.x - player_.x;
            const float dy = bullet.y - player_.y;
            const float hitR = player_.size * 0.5f + 2.0f;
            if (dx * dx + dy * dy < hitR * hitR) {
                player_.health -= bullet.damage;
                bullet.active = false;
                if (audio) audio->PlaySound(SoundEffect::HIT);
                if (player_.health <= 0) OnCombatantKilled(player_, audio);
            }
        }
    }

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [](const Bullet& b) { return !b.active; }),
                   bullets_.end());
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

bool CS2DGame::IsEnemyNearby(float x, float y, float safeRadius) const {
    for (const auto& bot : bots_) {
        if (!bot.alive) continue;
        const float dx = bot.x - x;
        const float dy = bot.y - y;
        if (dx * dx + dy * dy < safeRadius * safeRadius) return true;
    }
    return false;
}

// ============================================================================
// 渲染
// ============================================================================

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

    // 队友始终可见；敌人按视野剔除
    const bool applyVisionCull = player_.alive;
    for (const auto& bot : bots_) {
        if (!bot.alive && bot.deathTimer > 2.0f) continue;
        const bool friendly = (bot.team == player_.team);
        if (!friendly && applyVisionCull && !PlayerCanSeeBot(bot)) continue;
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
