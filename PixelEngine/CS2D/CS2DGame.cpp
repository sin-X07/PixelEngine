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
    economy_.Reset();
    prevPhase_ = round_.Phase();

    player_.team = Team::CT;
    player_.kills = 0;
    player_.deaths = 0;
    player_.money = economy_.StartMoney();
    SpawnPlayer();
    SetupObjective();

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
// 经济 / 购买
// ============================================================================

// 玩家是否处于本方购买区（地图未定义购买区时视为全图可买）
bool CS2DGame::IsPlayerInBuyZone() const {
    const auto& zones = world_->GetBuyZones();
    if (zones.empty()) return true;

    const char* myTeam = (player_.team == Team::CT) ? "ct" : "t";
    for (const auto& z : zones) {
        // 阵营过滤：属性 team 为空或 both 时对所有阵营开放
        auto it = z.properties.find("team");
        if (it != z.properties.end()) {
            std::string tag = it->second;
            for (char& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tag != "both" && tag != myTeam) continue;
        }
        if (player_.x >= z.x && player_.x <= z.x + z.width &&
            player_.y >= z.y && player_.y <= z.y + z.height) {
            return true;
        }
    }
    return false;
}

bool CS2DGame::TryBuy(WeaponId id) {
    if (id == WeaponId::None) return false;
    if (!IsBuyPhase()) return false;
    if (!player_.alive) return false;
    if (!IsPlayerInBuyZone()) return false;

    const WeaponDef& def = WeaponDB::Get(id);
    if (def.price <= 0) return false;
    if (player_.money < def.price) return false;

    switch (def.slot) {
        case WeaponSlotKind::Primary:
            player_.weapons[0] = id;
            if (player_.weaponSlot == 0) {
                player_.ammo = def.magazine;
                player_.maxAmmo = def.magazine;
                player_.reloading = false;
                player_.reloadTimer = 0.0f;
            }
            break;
        case WeaponSlotKind::Secondary:
            player_.weapons[1] = id;
            if (player_.weaponSlot == 1) {
                player_.ammo = def.magazine;
                player_.maxAmmo = def.magazine;
                player_.reloading = false;
                player_.reloadTimer = 0.0f;
            }
            break;
        case WeaponSlotKind::Melee:
            player_.weapons[2] = id;
            break;
        case WeaponSlotKind::Grenade:
            player_.weapons[3] = id;
            player_.grenadeCount = std::min(player_.grenadeCount + 1, 3);
            break;
    }

    player_.money -= def.price;
    economy_.Clamp(player_.money);

    std::cout << "[Buy] " << def.name << " ($" << def.price
              << ") 余额 $" << player_.money << std::endl;
    return true;
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
    player_.weapons[0] = WeaponId::M4A1;
    player_.weapons[1] = WeaponId::Glock;
    player_.weapons[2] = WeaponId::Knife;
    player_.weapons[3] = WeaponId::GrenadeHE;
    player_.grenadeCount = 1;
    player_.weaponSlot = 0;

    const WeaponDef& w = WeaponDB::Get(player_.weapons[0]);
    player_.ammo    = w.magazine;
    player_.maxAmmo = w.magazine;
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

    // 外观与武器：CT 用 M4A1，T 用 AK47；副武器 Glock，近战 Knife
    if (team == Team::CT) {
        bot.spriteKey = 0;
        bot.weapons[0] = WeaponId::M4A1;
    } else {
        bot.spriteKey = 1 + static_cast<int>(rng_() % 2);
        bot.weapons[0] = WeaponId::AK47;
    }
    bot.weapons[1] = WeaponId::Glock;
    bot.weapons[2] = WeaponId::Knife;
    bot.weapons[3] = WeaponId::GrenadeHE;
    bot.grenadeCount = 1;
    bot.weaponSlot = 0;

    const WeaponDef& bw = WeaponDB::Get(bot.weapons[0]);
    bot.ammo    = bw.magazine;
    bot.maxAmmo = bw.magazine;
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
    if (slot < 0 || slot > 3) return;
    if (mode_ != MatchMode::DeathMatch && round_.IsFrozen()) return;
    if (player_.weapons[slot] == WeaponId::None) return;   // 空槽位

    const int oldSlot = player_.weaponSlot;
    if (oldSlot >= 0 && oldSlot <= 1) {
        slotAmmo_[oldSlot]    = player_.ammo;
        slotMaxAmmo_[oldSlot] = player_.maxAmmo;
    }
    player_.weaponSlot = slot;

    if (slot <= 1) {
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
    return WeaponDB::Get(player_.CurrentWeapon().id).name;
}

// ============================================================================
// 每帧更新
// ============================================================================

void CS2DGame::Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                      bool wPressed, bool aPressed, bool sPressed, bool dPressed,
                      bool shiftPressed, bool rPressed, bool interactHeld,
                      AudioManager* audio) {
    if (!world_) return;

    UpdateRoundFlow(dt, audio);

    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool ending  = (mode_ != MatchMode::DeathMatch) && round_.IsRoundEnd();
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
    UpdateGrenades(dt, audio);
    UpdateObjective(dt, interactHeld, audio);
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
        sp.weaponId   = static_cast<int>(p.CurrentWeapon().id);
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

    // 投掷物
    for (const auto& g : grenades_) {
        if (!g.active) continue;
        renderer_->DrawBullet(g.x, g.y, g.vx, g.vy);
    }

    // 烟雾云（在遮罩之前绘制，仍受视野影响但视觉上覆盖战场）
    for (const auto& s : smokes_) {
        const float t = std::clamp(s.life / 12.0f, 0.0f, 1.0f);
        renderer_->DrawSmokeCloud(s.x, s.y, s.radius,
                                  static_cast<Uint8>(200.0f * std::min(1.0f, t * 3.0f)));
    }

    if (player_.alive) {
        renderer_->DrawVisionFog(player_.x, player_.y, player_.aimAngle);
    }

    renderer_->DrawOverlay();

    // 闪光致盲（最上层）
    if (flashTimer_ > 0.0f) {
        const float k = std::clamp(flashTimer_ / 2.5f, 0.0f, 1.0f);
        renderer_->DrawFlashOverlay(static_cast<Uint8>(230.0f * k));
    }

    const WeaponDef& pw = player_.CurrentWeapon();
    renderer_->DrawWeaponStatus(static_cast<int>(pw.id), player_.ammo,
                                (pw.cls == WeaponClass::Grenade || pw.cls == WeaponClass::Melee)
                                    ? 0 : pw.magazine,
                                viewW, viewH);
}
