#include "CS2DGame.hpp"
#include "MapCatalog.hpp"
#include <filesystem>
#include "TMJParser.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>

namespace {
// BOT 昵称（固定表，避免每回合/每次重生换名字）。
// 字符均已在 UI 字形表中，避免显示成方框。
const char* const kBotNicknames[] = {
    "冷面", "余火", "老K", "铁壁", "风火",
    "白鹰", "雷鹰", "飞鹰", "铁血"
};
constexpr int kBotNicknameCount =
    static_cast<int>(sizeof(kBotNicknames) / sizeof(kBotNicknames[0]));
} // namespace

CS2DGame::CS2DGame()
    : rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())) {
    world_ = std::make_unique<CS2DWorld>();
    std::random_device rd;
    rng_.seed(rd());
}

// ============================================================================
// 地图载入 / 重置
// ============================================================================

bool CS2DGame::LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer,
                       bool resetMatch) {
    renderer_ = renderer;

    CS2DMapData mapData;
    if (!TMJParser::Parse(resolvedTMJPath, mapData)) return false;
    if (!world_->Build(mapData)) return false;

    if (renderer_) renderer_->SetWorld(world_.get());

    // 烘焙寻路网格（角色半径 3.5，略大于命中盒半宽）
    pathFinder_.Build(*world_, Tuning::PathAgentRadius);
    std::cout << "[PathFinder] 网格 " << pathFinder_.Width() << "x" << pathFinder_.Height()
              << " 已构建" << std::endl;

    // 解析 mapId / playbookId：优先按文件名匹配 MapCatalog，否则用文件名去扩展名。
    // 只比较 filename，避免 Windows 反斜杠路径与 catalog 里的正斜杠不匹配。
    mapSourcePath_ = resolvedTMJPath;
    mapId_.clear();
    playbookId_.clear();
    const std::string mapFile = std::filesystem::path(resolvedTMJPath).filename().string();
    for (int i = 0; i < kMapCatalogCount; ++i) {
        const std::string catalogFile =
            std::filesystem::path(kMapCatalog[i].tmj).filename().string();
        if (mapFile == catalogFile) {
            mapId_      = kMapCatalog[i].id;
            playbookId_ = MapPlaybookId(kMapCatalog[i]);
            break;
        }
    }
    if (mapId_.empty()) {
        mapId_      = std::filesystem::path(resolvedTMJPath).stem().string();
        playbookId_ = mapId_;
    }
    std::cout << "[Map] mapId=" << mapId_ << " playbook=" << playbookId_
              << " path=" << resolvedTMJPath << std::endl;

    // 预热战术库：换图即加载 + 校验区域名，不必等到第一次 LLM 请求才报错。
    CoachWarmupPlaybook();

    if (resetMatch) {
        ResetToNewMatch();
    } else {
        // 选阵营前只保留地图世界，不生成玩家和 BOT。
        bots_.clear();
        botAi_.clear();
        bullets_.clear();
        player_ = Player{};
        player_.alive = false;
    }
    intel_.Build(world_.get(), clockSec_);
    return true;
}

// 开始一场全新比赛（清空比分与统计）
void CS2DGame::ResetToNewMatch() {
    bots_.clear();
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

    player_.team = desiredPlayerTeam_;
    player_.kills = 0;
    player_.deaths = 0;
    player_.money = economy_.StartMoney();
    SpawnPlayer();
    SetupObjective();

    // CT 侧：玩家 + (TEAM_SIZE-1) 个 BOT；T 侧：TEAM_SIZE 个 BOT
    botAi_.clear();
    bots_.clear();
    nextBotNameIndex_ = 0;
    // 玩家所在阵营 4 个 BOT（凑满 5 人），敌方 5 个 BOT
    const Team mySide    = player_.team;
    const Team enemySide = TeamManager::Opponent(mySide);
    for (int i = 0; i < TOTAL_BOTS; ++i) {
        const Team t = (i < TEAM_SIZE) ? enemySide : mySide;
        Player bot;
        SpawnBOT(bot, t);
        bots_.push_back(bot);
        botAi_.emplace_back();          // 与 bots_ 同下标
    }

    int nT = (player_.team == Team::T) ? 1 : 0;
    int nCT = (player_.team == Team::CT) ? 1 : 0;
    for (const auto& b : bots_) (b.team == Team::T ? nT : nCT)++;
    std::cout << "[CS2DGame] 对局开始: " << TEAM_SIZE << "v" << TEAM_SIZE
              << " 玩家阵营=" << (player_.team == Team::T ? "T" : "CT")
              << " 实际人数 T=" << nT << " CT=" << nCT << std::endl;

    AssignBombCarrier();   // bot 创建后再分配一次（首次开局时 bots_ 还为空）
    SnapshotRoundEconomy();
    AssignSquadRoles();
    CoachResetRound();
    // 首回合也要先定购买档，避免 BotBuyLoadout 用到默认 Full。
    CoachFor(Team::T).buyPlan  = DecideBuyPlan(Team::T);
    CoachFor(Team::CT).buyPlan = DecideBuyPlan(Team::CT);
    // 首回合也走同一套延迟购买，等教练先给 save/eco 结论
    botBuyPending_ = true;
    botBuyDoneT_   = false;
    botBuyDoneCT_  = false;
    botBuyTimer_   = (coachLlmEnabled_ && coachLlm_) ? 7.5f : 0.0f;
    CoachOnEvent(Team::T,  CoachEventType::RoundStart);
    CoachOnEvent(Team::CT, CoachEventType::RoundStart);
    for (size_t i = 0; i < squad_.size(); ++i) {
        std::cout << "  " << bots_[i].name << " (" << TeamName(bots_[i].team) << ") 角色: "
                  << (squad_[i].role == BotRole::Entry ? "突破"
                      : squad_[i].role == BotRole::Anchor ? "断后" : "支援")
                  << std::endl;
    }
}

void CS2DGame::RestartMatchWithTeam(Team t) {
    desiredPlayerTeam_ = t;
    ResetToNewMatch();
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

bool CS2DGame::TryBuy(WeaponId id, AudioManager* audio) {
    if (id == WeaponId::None) return false;
    if (!IsBuyPhase()) return false;
    if (!player_.alive) return false;
    if (!IsPlayerInBuyZone()) return false;

    const WeaponDef& def = WeaponDB::Get(id);
    if (def.price <= 0) return false;
    // 阵营隔离（照抄 CS2）：本阵营不能购买的枪一律拒绝
    if (!WeaponDB::TeamAllowed(id, player_.team == Team::T)) return false;
    if (player_.money < def.price) return false;

    switch (def.slot) {
        case WeaponSlotKind::Primary:
            player_.SetSlotWeapon(0, id);
            player_.weaponSlot = 0;
            player_.LoadAmmoFromSlot();
            player_.reloading = false;
            player_.reloadTimer = 0.0f;
            playerHoldingC4_ = false;
            break;
        case WeaponSlotKind::Secondary:
            player_.SetSlotWeapon(1, id);
            player_.weaponSlot = 1;
            player_.LoadAmmoFromSlot();
            player_.reloading = false;
            player_.reloadTimer = 0.0f;
            playerHoldingC4_ = false;
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
    if (audio) audio->PlaySound(SoundEffect::EQUIP);
        return true;
}

// 购买护甲：tier 1=护甲($650) 2=护甲+头盔($1000)
bool CS2DGame::TryBuyArmor(int tier, AudioManager* audio) {
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
    if (audio) audio->PlaySound(SoundEffect::EQUIP);
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
    SpectatorEnd();

    player_.alive      = true;
    player_.health     = 100;
    player_.maxHealth  = 100;
    player_.vx = player_.vy = 0.0f;
    crosshairBloom_ = 0.0f;
    player_.shootCooldown = 0.0f;
    player_.team       = desiredPlayerTeam_;
    if (mode_ == MatchMode::DeathMatch) player_.money = 16000;
    player_.animState  = AnimState::Idle;
    player_.animTimer  = 0.0f;
    player_.deathTimer = 0.0f;
    player_.shootAnimTimer = 0.0f;
    player_.spriteKey  = (desiredPlayerTeam_ == Team::CT) ? 0 : 1;
    // 开局只有起始手枪（照抄 CS2）：
    //   CT = USP-S，T = Glock-18。无护甲无投掷物，其余靠购买。
    player_.weapons[0] = WeaponId::None;
    player_.weapons[1] = (desiredPlayerTeam_ == Team::CT) ? WeaponId::USP_S
                                                          : WeaponId::Glock18;
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
    player_.reserve = Player::ReserveCapacity(w);
    // 各槽位弹药同步（避免旧局残留）
    for (int sl = 0; sl < 5; ++sl) {
        if (player_.weapons[sl] == WeaponId::None) player_.ClearSlotWeapon(sl);
        else                                       player_.SetSlotWeapon(sl, player_.weapons[sl]);
    }
    player_.SaveAmmoToSlot();
    player_.reloading  = false;
    player_.reloadTimer = 0.0f;
    player_.stealth    = false;

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(player_.team, player_.x, player_.y);
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
    // 首次创建时分配昵称；重生时保留原昵称。
    if (bot.name.empty()) {
        bot.name = kBotNicknames[nextBotNameIndex_ % kBotNicknameCount];
        ++nextBotNameIndex_;
    }
    bot.animState  = AnimState::Idle;
    bot.animTimer  = 0.0f;
    bot.deathTimer = 0.0f;
    bot.shootAnimTimer = 0.0f;
    bot.ammo       = 30;
    bot.maxAmmo    = 30;
    bot.reloading  = false;
    bot.reloadTimer = 0.0f;
    bot.stealth    = false;

    // 外观与武器：开局只有起始手枪（照抄 CS2：CT = USP-S，T = Glock-18）
    if (team == Team::CT) {
        bot.spriteKey = 0;
        bot.weapons[1] = WeaponId::USP_S;
    } else {
        bot.spriteKey = 1 + static_cast<int>(rng_() % 2);
        bot.weapons[1] = WeaponId::Glock18;
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
    bot.reserve = Player::ReserveCapacity(bw);
    // 各槽位弹药初始化（BOT 的副武器/近战槽）
    for (int sl = 0; sl < 5; ++sl) {
        if (bot.weapons[sl] == WeaponId::None) bot.ClearSlotWeapon(sl);
        else                                   bot.SetSlotWeapon(sl, bot.weapons[sl]);
    }
    bot.SaveAmmoToSlot();
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
    if (slot < 0 || slot > 4) return;

    // 同一槽位重复按：投掷物在多个道具间轮换
    if (slot == player_.weaponSlot) {
        if (slot == 3 && player_.grenadeCount > 1) {
            player_.grenadeSlot = (player_.grenadeSlot + 1) % player_.grenadeCount;
            player_.SyncActiveGrenade();
        }
        return;
    }

    // 目标槽校验
    if (slot == 4) {
        if (!PlayerHasC4()) return;
        player_.weapons[4] = WeaponId::C4;
    } else if (slot == 3) {
        if (player_.grenadeCount <= 0) return;
    } else if (player_.weapons[slot] == WeaponId::None) {
        return;
    }

    // 切走前把当前武器的弹药写回它自己的槽位
    player_.SaveAmmoToSlot();

    player_.weaponSlot = slot;
    player_.reloading   = false;
    player_.reloadTimer = 0.0f;
    playerHoldingC4_ = (slot == 4);

    if (slot == 4) {
        player_.ammo = 1;
        player_.maxAmmo = 1;
        player_.reserve = 0;
        return;
    }
    if (slot == 3) {
        player_.grenadeSlot = 0;
        player_.SyncActiveGrenade();
        player_.ammo = 1;
        player_.maxAmmo = 1;
        player_.reserve = 0;
        return;
    }

    // 从目标槽读取它自己的弹药
    player_.LoadAmmoFromSlot();
}

std::string CS2DGame::GetWeaponName() const {
    return GetWeaponNameFor(player_);
}

std::string CS2DGame::GetWeaponNameFor(const Player& p) const {
    return WeaponDB::Get(p.CurrentWeapon().id).name;
}

// ============================================================================
// 每帧更新
// ============================================================================

void CS2DGame::Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                      bool wPressed, bool aPressed, bool sPressed, bool dPressed,
                      bool shiftPressed, bool rPressed, bool interactHeld,
                      AudioManager* audio) {
    // ---- 观察者输入：空格切换视角，左右键切换队友，自由视角用 WASD ----
    if (IsSpectating()) {
        static bool prevSpace = false, prevLmb = false, prevRmb = false;
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        const bool spaceNow = ks && ks[SDL_SCANCODE_SPACE];
        if (spaceNow && !prevSpace) SpectatorToggle();
        prevSpace = spaceNow;

        int mx = 0, my = 0;
        const Uint32 mbtn = SDL_GetMouseState(&mx, &my);
        const bool lmb = (mbtn & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        const bool rmb = (mbtn & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
        if (lmb && !prevLmb) SpectatorCycle(-1);
        if (rmb && !prevRmb) SpectatorCycle(+1);
        prevLmb = lmb;
        prevRmb = rmb;

        if (specMode_ == SpectateMode::Free) {
            const float fx = (dPressed ? 1.0f : 0.0f) - (aPressed ? 1.0f : 0.0f);
            const float fy = (sPressed ? 1.0f : 0.0f) - (wPressed ? 1.0f : 0.0f);
            SpectatorMoveFree(fx, fy, dt);
        }
    }

    if (!world_) return;

    clockSec_ += dt;

    UpdateRoundFlow(dt, audio);

    // 团队情报：老化 + 探点上报（冻结期也推进，保证情报会过期）
    UpdateTeamContacts(dt);
    intel_.Update(dt);

    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool ending  = (mode_ != MatchMode::DeathMatch) && round_.IsRoundEnd();
    const bool canAct  = !frozen && !ending;
    const bool canMove = !frozen;   // 结算展示期仍可移动/拾取，只是不能开火

    player_.stealth = shiftPressed && canAct;

    if (canAct && rPressed && !player_.reloading) {
        if ((player_.weaponSlot == 0 || player_.weaponSlot == 1) &&
            player_.ammo < player_.maxAmmo && player_.reserve > 0) {
            StartReload(player_, audio);
        }
    }

    if (player_.alive && canMove) {
        UpdatePlayerMovement(dt, wPressed, aPressed, sPressed, dPressed, player_.stealth);
        // 结算展示期与回合中完全一致：可移动、可开枪、可投掷
        UpdatePlayerShooting(dt, mouseLeftDown, mouseWorldX, mouseWorldY, audio);
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
    UpdateBombItem(dt);


    if (canMove) {
        UpdateBOTs(dt, audio);

    } else {        // 暂停 / 结算冻结：BOT 原地待命
        for (auto& bot : bots_) { bot.vx = bot.vy = 0.0f; }
    }

    // BOT 脚步声：与玩家同一套节奏，按 3D 位置播放（静步的 BOT 不出声）
    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& b = bots_[i];
        auto& bai = AiAt(i);
        if (!b.alive) { bai.footstepTimer = 0.0f; continue; }
        bai.footstepTimer -= dt;
        const float spd = std::sqrt(b.vx * b.vx + b.vy * b.vy);
        if (!b.stealth && spd > 5.0f && bai.footstepTimer <= 0.0f) {
            bai.footstepTimer = 0.34f + static_cast<float>(i % 5) * 0.02f;
            if (audio) {
                audio->PlaySoundAt(SoundEffect::FOOTSTEP, b.x, b.y, ListenerX(), ListenerY());
            }
            OnFootstepSound(b, b.x, b.y);
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
    UpdateBotBuy(dt);

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

    // 死斗：经济恒定 16000（买完立即补满）
    if (mode_ == MatchMode::DeathMatch) {
        player_.money = 16000;
        for (auto& b2 : bots_) b2.money = 16000;
    }

    prevMouseDown_ = mouseLeftDown;

    // 掉落物：保留到回合结束（不再按时间消失），已拾取的移除
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                [](const DroppedItem& d) { return d.id == WeaponId::None; }),
                 drops_.end());
    for (auto& d : drops_) {
        if (d.pickupDelay > 0.0f) d.pickupDelay -= dt;
    }
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
    if (player_.alive && !player_.stealth) {
        footstepTimer_ -= dt;
        const float spd = std::sqrt(player_.vx * player_.vx + player_.vy * player_.vy);
        if (spd > 5.0f && footstepTimer_ <= 0.0f) {
            footstepTimer_ = 0.32f;
            if (audio) audio->PlaySound(SoundEffect::FOOTSTEP);
            OnFootstepSound(player_, player_.x, player_.y);
        }
    }
}

// ============================================================================
// 渲染
// ============================================================================

void CS2DGame::RenderMapPreview(int viewW, int viewH,
                                float centerX, float centerY, float zoom) {
    if (!renderer_ || !world_) return;
    renderer_->SetViewportSize(viewW, viewH);
    renderer_->SetPreviewCamera(centerX, centerY, zoom);
    renderer_->DrawWorld();
}

void CS2DGame::GetTeamSpawnCenter(Team team, float& outX, float& outY) const {
    outX = world_ ? world_->GetMapPixelW() * 0.5f : 0.0f;
    outY = world_ ? world_->GetMapPixelH() * 0.5f : 0.0f;
    if (!world_) return;
    const auto spawns = world_->SpawnsForTeam(team == Team::CT);
    if (spawns.empty()) return;
    float sx = 0.0f, sy = 0.0f;
    for (const auto* s : spawns) { sx += s->x; sy += s->y; }
    outX = sx / static_cast<float>(spawns.size());
    outY = sy / static_cast<float>(spawns.size());
}

void CS2DGame::Render(int viewW, int viewH) {
    if (!renderer_ || !world_) return;

    // 观察者：镜头跟随队友或自由移动
    float camX = player_.x, camY = player_.y;
    if (IsSpectating()) {
        if (specMode_ == SpectateMode::Free) {
            camX = specFreeX_;
            camY = specFreeY_;
        } else if (specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
            camX = bots_[static_cast<size_t>(specTarget_)].x;
            camY = bots_[static_cast<size_t>(specTarget_)].y;
        }
    }
    renderer_->UpdateCamera(camX, camY, viewW, viewH);
    renderer_->DrawWorld();

    // 烟雾画在地面层：角色、掉落物、目标标记都在它之上。
    // 视觉上不再"盖死"一切；但 SegmentBlockedBySmoke 仍然阻挡视线与敌人剔除。
    for (const auto& s : smokes_) {
        const float t = std::clamp(s.life / 12.0f, 0.0f, 1.0f);
        renderer_->DrawSmokeCloud(s.x, s.y, s.radius,
                                  static_cast<Uint8>(150.0f * std::min(1.0f, t * 3.0f)));
    }

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
    const bool applyVisionCull = player_.alive || IsSpectating();
    for (const auto& bot : bots_) {
        if (!bot.alive && bot.deathTimer > 2.0f) continue;
        const bool friendly = (bot.team == player_.team);
        if (!friendly && applyVisionCull) {
            if (IsSpectating()) {
                if (!SpectatorCanSeeEnemy(bot)) continue;
            } else if (!PlayerCanSeeBot(bot)) {
                continue;
            }
        }
        SpriteDrawParams sp;
        fillParams(bot, sp);
        renderer_->DrawCharacter(sp);
    }

    if (!IsSpectating() && (player_.alive || player_.deathTimer <= 2.0f)) {
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

    // 目标物标记（包点范围）：画在角色之下、遮罩之上，保证始终可辨
    // 地面掉落物（用 _d 图标）
    for (const auto& d : drops_) {
        if (d.id == WeaponId::None) continue;
        renderer_->DrawDroppedWeapon(static_cast<int>(d.id), d.x, d.y, d.rot);
    }

    // 已安装的 C4：醒目的红色脉冲标记 + 拆包有效范围
    // 掉在地上的 C4：T 方始终可见；CT 只在「有人看到过」的情报期内可见
    if (bombDropped_) {
        bool visible = (player_.team == Team::T) || bombIntel_ || IsSpectating();
        if (!visible && player_.alive && renderer_) {
            visible = renderer_->IsVisibleToPlayer(player_.x, player_.y, player_.aimAngle,
                                                    bombDropX_, bombDropY_) &&
                      !SegmentBlockedBySmoke(player_.x, player_.y, bombDropX_, bombDropY_);
        }
        if (visible) {
            const float pulse = 0.5f + 0.5f * std::sin(clockSec_ * 5.0f);
            renderer_->DrawCircleOutline(bombDropX_, bombDropY_,
                                         10.0f + 4.0f * pulse,
                                         255, 170, 60, static_cast<Uint8>(150 + 90 * pulse));
            renderer_->DrawCircleOutline(bombDropX_, bombDropY_, 4.0f,
                                         255, 220, 140, 230);
        }
    }

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
