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

    // 烘焙寻路网格（角色半径 3.5，略大于命中盒半宽）
    pathFinder_.Build(*world_, Tuning::PathAgentRadius);
    std::cout << "[PathFinder] 网格 " << pathFinder_.Width() << "x" << pathFinder_.Height()
              << " 已构建" << std::endl;

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
    matchOverTimer_ = 0.0f;
    killFeed_.clear();
    contactsT_.clear();
    contactsCT_.clear();

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
    botAi_.clear();
    for (int i = 0; i < TOTAL_BOTS; ++i) {
        const Team t = (i < TEAM_SIZE - 1) ? Team::CT : Team::T;
        Player bot;
        SpawnBOT(bot, t);
        bots_.push_back(bot);
        botExecutors_.emplace_back();
        botAi_.emplace_back();          // 与 bots_ 同下标
    }

    std::cout << "[CS2DGame] 对局开始: " << TEAM_SIZE << "v" << TEAM_SIZE
              << " (玩家 CT + " << bots_.size() << " BOT)" << std::endl;

    AssignSquadRoles();
    CoachResetRound();
    CoachOnEvent(Team::T,  CoachEventType::RoundStart);
    CoachOnEvent(Team::CT, CoachEventType::RoundStart);
    for (size_t i = 0; i < squad_.size(); ++i) {
        std::cout << "  BOT#" << i << " (" << TeamName(bots_[i].team) << ") 角色: "
                  << (squad_[i].role == BotRole::Entry ? "突破"
                      : squad_[i].role == BotRole::Anchor ? "断后" : "支援")
                  << std::endl;
    }
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
            slotAmmo_[0]    = def.magazine;
            slotMaxAmmo_[0] = def.magazine;
            // 买完自动装备
            player_.weaponSlot = 0;
            player_.ammo = def.magazine;
            player_.maxAmmo = def.magazine;
            player_.reloading = false;
            player_.reloadTimer = 0.0f;
            break;
        case WeaponSlotKind::Secondary:
            player_.weapons[1] = id;
            slotAmmo_[1]    = def.magazine;
            slotMaxAmmo_[1] = def.magazine;
            player_.weaponSlot = 1;
            player_.ammo = def.magazine;
            player_.maxAmmo = def.magazine;
            player_.reloading = false;
            player_.reloadTimer = 0.0f;
            break;
        case WeaponSlotKind::Melee:
            player_.weapons[2] = id;
            break;
        case WeaponSlotKind::Grenade: {
            if (player_.grenadeCount >= Player::MAX_GRENADES) return false;
            // 闪光最多 2 个，其它每种最多 1 个
            const int limit = (id == WeaponId::GrenadeFlash) ? 2 : 1;
            if (player_.CountGrenade(id) >= limit) return false;
            player_.grenades[player_.grenadeCount++] = id;
            player_.SyncActiveGrenade();
            break;
        }
    }

    player_.money -= def.price;
    economy_.Clamp(player_.money);

    std::cout << "[Buy] " << def.name << " ($" << def.price
              << ") 余额 $" << player_.money << std::endl;
    return true;
}

// 购买护甲：tier 1=护甲($650) 2=护甲+头盔($1000)
bool CS2DGame::TryBuyArmor(int tier) {
    if (tier < 1 || tier > 2) return false;
    if (!IsBuyPhase() || !player_.alive || !IsPlayerInBuyZone()) return false;

    const int price = (tier == 1) ? 650 : 1000;
    if (player_.money < price) return false;

    // 已满甲（且已带头盔，或本次只买护甲）则无需重复购买
    if (player_.armor >= 100 && (tier == 1 || player_.helmet)) return false;

    player_.money -= price;
    economy_.Clamp(player_.money);

    player_.armor = 100;
    if (tier == 2) player_.helmet = true;

    std::cout << "[Buy] " << (tier == 2 ? "护甲+头盔" : "护甲")
              << " ($" << price << ") 余额 $" << player_.money << std::endl;
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
    // 与已存在单位的期望最小间距：避免 5 人叠在同一个出生点
    constexpr float SPAWN_SEPARATION = 16.0f;

    auto occupied = [&](float x, float y) {
        auto near = [&](const Player& u) {
            if (!u.alive) return false;
            const float dx = u.x - x;
            const float dy = u.y - y;
            return dx * dx + dy * dy < SPAWN_SEPARATION * SPAWN_SEPARATION;
        };
        if (near(player_)) return true;
        for (const auto& b : bots_) if (near(b)) return true;
        return false;
    };

    // 第一轮：既要能站下、又要与他人保持间距
    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    for (int attempt = 0; attempt < 24; ++attempt) {
        const auto* sp = candidates[pick(rng_)];
        if (world_->CheckRectCollision(sp->x - HITBOX * 0.5f, sp->y - HITBOX * 0.5f,
                                       HITBOX, HITBOX)) {
            continue;
        }
        if (occupied(sp->x, sp->y)) continue;
        outX = sp->x;
        outY = sp->y;
        return true;
    }

    // 第二轮：只要求能站下（出生点拥挤时降级）
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
    // 开局只有手枪（CT = USP），无护甲无投掷物，其余靠购买
    player_.weapons[0] = WeaponId::None;
    player_.weapons[1] = WeaponId::USP;
    player_.weapons[2] = WeaponId::Knife;
    player_.weapons[3] = WeaponId::None;
    player_.grenadeCount = 0;
    player_.grenadeSlot  = 0;
    for (int gi = 0; gi < Player::MAX_GRENADES; ++gi) player_.grenades[gi] = WeaponId::None;
    player_.weaponSlot = 1;            // 默认持手枪
    player_.armor  = 0;
    player_.helmet = false;

    const WeaponDef& w = WeaponDB::Get(player_.weapons[1]);
    player_.ammo    = w.magazine;
    player_.maxAmmo = w.magazine;
    player_.reloading  = false;
    player_.reloadTimer = 0.0f;
    player_.stealth    = false;

    slotAmmo_[0]    = 0;
    slotMaxAmmo_[0] = 0;
    slotAmmo_[1]    = w.magazine;
    slotMaxAmmo_[1] = w.magazine;

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(Team::CT, player_.x, player_.y);
        if (!IsEnemyNearby(player_.x, player_.y, 120.0f, Team::CT)) break;
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

    // 外观与武器：开局只有手枪（CT = USP，T = Glock），无护甲无投掷物
    if (team == Team::CT) {
        bot.spriteKey = 0;
        bot.weapons[1] = WeaponId::USP;
    } else {
        bot.spriteKey = 1 + static_cast<int>(rng_() % 2);
        bot.weapons[1] = WeaponId::Glock;
    }
    bot.weapons[0] = WeaponId::None;      // 主武器需购买
    bot.weapons[2] = WeaponId::Knife;

    // 投掷物类型：T 偏进攻（闪光），CT 偏防守（烟雾）
    {
        const int r = static_cast<int>(rng_() % 100);
        if (team == Team::T) {
            bot.weapons[3] = (r < 50) ? WeaponId::GrenadeHE
                           : (r < 80) ? WeaponId::GrenadeFlash
                                      : WeaponId::GrenadeSmoke;
        } else {
            bot.weapons[3] = (r < 40) ? WeaponId::GrenadeHE
                           : (r < 80) ? WeaponId::GrenadeSmoke
                                      : WeaponId::GrenadeFlash;
        }
    }
    bot.grenadeCount = 0;                 // 需购买
    bot.grenadeSlot  = 0;
    for (int gi = 0; gi < Player::MAX_GRENADES; ++gi) bot.grenades[gi] = WeaponId::None;
    bot.weapons[3] = WeaponId::None;
    bot.weaponSlot = 1;                   // 默认持手枪
    bot.armor  = 0;
    bot.helmet = false;

    const WeaponDef& bw = WeaponDB::Get(bot.weapons[1]);
    bot.ammo    = bw.magazine;
    bot.maxAmmo = bw.magazine;
    bot.aimAngle = angleDist_(rng_);

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(team, bot.x, bot.y);
        if (!IsEnemyNearby(bot.x, bot.y, 60.0f, team)) break;
    }
}

void CS2DGame::RespawnBOT(Player& bot) {
    SpawnBOT(bot, bot.team);
}

// ============================================================================
// 武器
// ============================================================================

void CS2DGame::SwitchWeaponSlot(int slot) {
    grenadeCharging_ = false;   // 切枪打断蓄力
    if (slot < 0 || slot > 3) return;

    // 投掷物槽：按 4 在已装备的投掷物之间循环切换
    if (slot == 3) {
        if (player_.grenadeCount <= 0) return;
        if (player_.weaponSlot != 3) {
            player_.weaponSlot = 3;                 // 先切到投掷物
        } else {
            player_.grenadeSlot = (player_.grenadeSlot + 1) % player_.grenadeCount;
        }
        player_.SyncActiveGrenade();
        player_.ammo    = 1;
        player_.maxAmmo = 1;
        player_.reloading = false;
        player_.reloadTimer = 0.0f;
        return;
    }

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

    clockSec_ += dt;


    UpdateRoundFlow(dt, audio);

    // 团队情报：老化 + 探点上报（冻结期也推进，保证情报会过期）
    UpdateTeamContacts(dt);

    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool ending  = (mode_ != MatchMode::DeathMatch) && round_.IsRoundEnd();
    const bool canAct  = !frozen && !ending;
    const bool canMove = !frozen;   // 结算展示期仍可移动/拾取，只是不能开火

    player_.stealth = shiftPressed && canAct;

    if (canAct && rPressed && !player_.reloading) {
        if ((player_.weaponSlot == 0 || player_.weaponSlot == 1) && player_.ammo < player_.maxAmmo) {
            StartReload(player_, audio);
        }
    }

    if (player_.alive && canMove) {
        UpdatePlayerMovement(dt, wPressed, aPressed, sPressed, dPressed, player_.stealth);
        if (!ending) {
            UpdatePlayerShooting(dt, mouseLeftDown, mouseWorldX, mouseWorldY, audio);
        }
    } else {
        player_.vx = player_.vy = 0.0f;

        // 准备阶段（冻结）：允许转向瞄准，但不允许移动与开火
        if (player_.alive && frozen) {
            player_.aimAngle = std::atan2(mouseWorldY - player_.y,
                                          mouseWorldX - player_.x);
            player_.stealth = false;
        }

        // 死斗模式：玩家阵亡后自动重生
        if (mode_ == MatchMode::DeathMatch && !player_.alive) {
            botRespawnTimer_ -= dt;
            if (botRespawnTimer_ <= 0.0f) SpawnPlayer();
        }
    }
    UpdateReload(player_, dt);

    if (canMove) {
        UpdateBOTs(dt, audio);   // 结算展示期也允许移动站位，但内部已禁止开火
    } else {
        // 冻结/结算期：BOT 原地待命
        for (auto& bot : bots_) { bot.vx = bot.vy = 0.0f; }
    }

    // BOT 脚步声：与玩家同一套节奏，按 3D 位置播放（静步的 BOT 不出声）
    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& b = bots_[i];
        auto& bai = AiAt(i);
        if (!b.alive) { bai.footstepTimer = 0.0f; continue; }
        bai.footstepTimer -= dt;
        const float spd = std::sqrt(b.vx * b.vx + b.vy * b.vy);
        if (spd > 5.0f && bai.footstepTimer <= 0.0f) {
            bai.footstepTimer = 0.34f + static_cast<float>(i % 5) * 0.02f;
            if (audio) {
                audio->PlaySoundAt(SoundEffect::FOOTSTEP, b.x, b.y, player_.x, player_.y);
            }
        }
    }

    // E 键：边沿触发主动拾取（拆包/救人时不抢键）
    if (interactHeld && !prevInteract_ && player_.alive) {
        const bool busyDefusing = (mode_ == MatchMode::BombDefusal && bombPlanted_ &&
                                   defuseProgress_ > 0.0f);
        if (!busyDefusing) TryPickupDrops(true);
    }
    prevInteract_ = interactHeld;

    CoachUpdate(dt);

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

    // 掉落物：保留到回合结束（不再按时间消失），已拾取的移除
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                [](const DroppedItem& d) { return d.id == WeaponId::None; }),
                 drops_.end());
    TryPickupDrops(false);

    // 爆炸特效寿命
    for (auto& fx : blasts_) fx.life -= dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(),
                                 [](const BlastFx& b) { return b.life <= 0.0f; }),
                  blasts_.end());

    // 击杀播报寿命
    for (auto& e : killFeed_) e.life -= dt;
    killFeed_.erase(std::remove_if(killFeed_.begin(), killFeed_.end(),
                                   [](const KillFeedEntry& e) { return e.life <= 0.0f; }),
                    killFeed_.end());

    // 命中 / 受伤反馈衰减
    if (hitMarkerTimer_ > 0.0f)   hitMarkerTimer_   = std::max(0.0f, hitMarkerTimer_ - dt);
    if (damageFlashTimer_ > 0.0f) damageFlashTimer_ = std::max(0.0f, damageFlashTimer_ - dt);
    if (lastDeathTimer_ > 0.0f)   lastDeathTimer_   = std::max(0.0f, lastDeathTimer_ - dt);
    if (announcementTimer_ > 0.0f) {
        announcementTimer_ = std::max(0.0f, announcementTimer_ - dt);
        if (announcementTimer_ <= 0.0f) announcement_.clear();
    }

    // 玩家脚步声（潜行时不出声）
    if (audio && player_.alive && !player_.stealth) {
        footstepTimer_ -= dt;
        const float spd = std::sqrt(player_.vx * player_.vx + player_.vy * player_.vy);
        if (spd > 5.0f && footstepTimer_ <= 0.0f) {
            footstepTimer_ = 0.32f;
            audio->PlaySound(SoundEffect::FOOTSTEP);
        }
    }
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

    // 目标物标记（包点范围）：画在角色之下、遮罩之上，保证始终可辨
    // 地面掉落物（用 _d 图标）
    for (const auto& d : drops_) {
        if (d.id == WeaponId::None) continue;
        renderer_->DrawDroppedWeapon(static_cast<int>(d.id), d.x, d.y, clockSec_);
    }

    // 已安装的 C4：醒目的红色脉冲标记 + 拆包有效范围
    if (bombPlanted_) {
        const float pulse = 0.5f + 0.5f * std::sin(clockSec_ * 6.0f);
        renderer_->DrawCircleOutline(bombX_, bombY_, 18.0f + 4.0f * pulse,
                                     255, 70, 60, static_cast<Uint8>(170 + 70 * pulse));
        renderer_->DrawCircleFilled(bombX_, bombY_, 5.0f + 1.5f * pulse,
                                    255, 90, 70, 220);
        // 拆包有效范围（很小）
        renderer_->DrawCircleOutline(bombX_, bombY_, Tuning::DefuseRadius,
                                     255, 170, 90, 130);
    }

    // 手雷：飞行中显示爆炸范围，爆炸后播放扩散圆环
    for (const auto& g : grenades_) {
        if (!g.active || g.type != WeaponId::GrenadeHE) continue;
        const float pulse = 0.5f + 0.5f * std::sin(clockSec_ * 12.0f);
        renderer_->DrawCircleFilled(g.x, g.y, Tuning::NadeBlastRadius,
                                    210, 90, 40, static_cast<Uint8>(18 + 16 * pulse));
        renderer_->DrawCircleOutline(g.x, g.y, Tuning::NadeBlastRadius,
                                     240, 130, 60, static_cast<Uint8>(120 + 80 * pulse));
    }
    for (const auto& fx : blasts_) {
        const float t = 1.0f - (fx.total > 0.0f ? fx.life / fx.total : 0.0f);  // 0→1
        const float r = Tuning::NadeBlastRadius * (0.35f + 0.65f * t);
        const Uint8 a = static_cast<Uint8>(200.0f * (1.0f - t));
        renderer_->DrawCircleFilled(fx.x, fx.y, r, 255, 190, 90, static_cast<Uint8>(a / 3));
        renderer_->DrawCircleOutline(fx.x, fx.y, r, 255, 220, 150, a);
    }

    renderer_->DrawObjectiveMarkers(clockSec_);

    if (player_.alive) {
        renderer_->DrawVisionFog(player_.x, player_.y, player_.aimAngle);
    }

    renderer_->DrawOverlay();

    // 闪光致盲（最上层）：致盲期间完全丧失视野，最后 0.4s 快速淡出
    if (flashTimer_ > 0.0f) {
        // 末尾 0.4 秒淡出，整体再乘以本次距离决定的峰值强度
        const float fade = std::clamp(flashTimer_ / 0.4f, 0.0f, 1.0f);
        const float peak = std::clamp(flashPeak_ > 0.0f ? flashPeak_ : 1.0f, 0.0f, 1.0f);
        renderer_->DrawFlashOverlay(static_cast<Uint8>(255.0f * fade * peak));
    }

    const WeaponDef& pw = player_.CurrentWeapon();
    renderer_->DrawWeaponStatus(static_cast<int>(pw.id), player_.ammo,
                                (pw.cls == WeaponClass::Grenade || pw.cls == WeaponClass::Melee)
                                    ? 0 : pw.magazine,
                                viewW, viewH);
}
