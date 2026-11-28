#include "CS2DSimulation.hpp"
#include "MapCatalog.hpp"
#include "TMJParser.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>

namespace {
const char* const kBotNicknames[] = {
    "冷面", "余火", "老K", "铁壁", "风火",
    "白鹰", "雷鹰", "飞鹰", "铁血"
};
constexpr int kBotNicknameCount =
    static_cast<int>(sizeof(kBotNicknames) / sizeof(kBotNicknames[0]));
} // namespace

CS2DSimulation::CS2DSimulation()
    : rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())) {
    world_ = std::make_unique<CS2DWorld>();
    std::random_device rd;
    rng_.seed(rd());
    actors_.resize(1);
    actors_[0].id = 0;
    actors_[0].controller = ControllerKind::Human;
    actors_[0].team = desiredPlayerTeam_;
    localActorId_ = 0;
    EnsureBotAi();
}

CS2DSimulation::Player* CS2DSimulation::FindActor(ActorId id) {
    if (id < 0 || id >= static_cast<ActorId>(actors_.size())) return nullptr;
    return &actors_[static_cast<size_t>(id)];
}

const CS2DSimulation::Player* CS2DSimulation::FindActor(ActorId id) const {
    if (id < 0 || id >= static_cast<ActorId>(actors_.size())) return nullptr;
    return &actors_[static_cast<size_t>(id)];
}

ActorId CS2DSimulation::ActorIdOf(const Player& actor) const {
    if (actors_.empty()) return kInvalidActorId;
    const Player* base = actors_.data();
    const Player* ptr  = &actor;
    if (ptr < base || ptr >= base + actors_.size()) return kInvalidActorId;
    return static_cast<ActorId>(ptr - base);
}

void CS2DSimulation::SetLocalActorId(ActorId id) {
    if (id >= 0 && id < static_cast<ActorId>(actors_.size())) {
        localActorId_ = id;
    }
}

bool CS2DSimulation::SetActorController(ActorId id, ControllerKind kind,
                                        std::uint32_t peerId, const std::string& name) {
    Player* actor = FindActor(id);
    if (!actor) return false;
    actor->controller = kind;
    actor->peerId = peerId;
    if (!name.empty()) actor->name = name;
    if (kind == ControllerKind::Human && id < static_cast<ActorId>(actorAi_.size())) {
        actorAi_[static_cast<size_t>(id)].defusing = false;
        actorAi_[static_cast<size_t>(id)].hasPlanHold = false;
    }
    return true;
}

void CS2DSimulation::SetActorScopeLevel(ActorId id, int level) {
    if (Player* actor = FindActor(id)) {
        actor->scopeLevel = std::clamp(level, 0, Presentation::kScopeLevelCount - 1);
        actor->scoped = (actor->scopeLevel > 0);
    }
}

void CS2DSimulation::SetActorScopeView(ActorId id, int level, float engagement) {
    if (Player* actor = FindActor(id)) {
        actor->scopeLevel = std::clamp(level, 0, Presentation::kScopeLevelCount - 1);
        actor->scopeEngagement = std::clamp(engagement, 0.0f, 1.0f);
        actor->scoped = (actor->scopeLevel > 0);
    }
}

void CS2DSimulation::SetActorAimTarget(ActorId id, float x, float y) {
    Player* actor = FindActor(id);
    if (!actor) return;
    actor->aimWorldX = x;
    actor->aimWorldY = y;
    if (actor->alive) {
        actor->aimAngle = std::atan2(y - actor->y, x - actor->x);
        actor->aimDistance = std::max(std::hypot(x - actor->x, y - actor->y), 0.001f);
    }
}

// ============================================================================
// 地图载入 / 重置
// ============================================================================

bool CS2DSimulation::LoadMapHeadless(const std::string& resolvedTMJPath, bool resetMatch,
                                     float visionRadius) {
    CS2DMapData mapData;
    if (!TMJParser::Parse(resolvedTMJPath, mapData)) return false;
    if (!world_->Build(mapData)) return false;

    visibility_.SetWorld(world_.get());
    visibility_.SetDistanceLimited(false);
    visibility_.SetVisionRadius(visionRadius);

    pathFinder_.Build(*world_, Tuning::PathAgentRadius);
    std::cout << "[PathFinder] 网格 " << pathFinder_.Width() << "x" << pathFinder_.Height()
              << " 已构建" << std::endl;

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

    {
        const std::filesystem::path mapPath(resolvedTMJPath);
        const std::filesystem::path tacticsPath =
            mapPath.parent_path().parent_path().parent_path() / "config" / "tactics" / (mapId_ + ".ini");
        tactics_.Load(tacticsPath.string(), mapId_);
    }
    if (tactics_.loaded) {
        std::cout << "[Tactics] 已加载战术表: " << mapId_
                  << " areas=" << tactics_.areas.size()
                  << " slotSets=" << tactics_.slots.size() << std::endl;
        int bad = 0;
        for (const auto& [alias, area] : tactics_.areas) {
            float ax = 0.0f, ay = 0.0f;
            if (!AreaCenterByName(area, ax, ay)) {
                ++bad;
                std::cerr << "[Tactics] 区域不存在: " << alias << " -> " << area
                          << "（检查 " << mapId_ << ".ini 的 [areas]）" << std::endl;
            }
        }
        if (bad == 0) std::cout << "[Tactics] 区域校验通过" << std::endl;
    }

    CoachWarmupPlaybook();

    if (resetMatch) {
        ResetToNewMatch();
    } else {
        actors_.assign(1, Player{});
        actors_[0].id = 0;
        actors_[0].controller = ControllerKind::Human;
        actors_[0].team = desiredPlayerTeam_;
        actors_[0].alive = false;
        localActorId_ = 0;
        EnsureBotAi();
        bullets_.clear();
        bulletVisuals_.clear();
        nextBulletVisualId_ = 1;
        SimEvent sr;
        sr.type = SimEventType::ScopeReset;
        sr.actorId = localActorId_;
        sr.scopeReason = Presentation::ScopeResetReason::MatchExit;
        EmitSimEvent(std::move(sr));
    }
    intel_.Build(world_.get(), clockSec_);
    return true;
}

void CS2DSimulation::ResetToNewMatch() {
    {
        SimEvent stopObj;
        stopObj.type = SimEventType::BombEvent;
        stopObj.bombKind = BombEventKind::StopAllObjectiveAudio;
        EmitSimEvent(std::move(stopObj));

        SimEvent resetEv;
        resetEv.type = SimEventType::RoundEvent;
        resetEv.roundKind = RoundEventKind::MatchReset;
        EmitSimEvent(std::move(resetEv));

        SimEvent sr;
        sr.type = SimEventType::ScopeReset;
        sr.actorId = localActorId_;
        sr.scopeReason = Presentation::ScopeResetReason::RoundRestart;
        EmitSimEvent(std::move(sr));
    }
    bullets_.clear();
    bulletVisuals_.clear();
    nextBulletVisualId_ = 1;
    grenades_.clear();
    smokes_.clear();
    fires_.clear();
    decoys_.clear();
    fireTickTimer_ = 0.0f;
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

    actors_.assign(1, Player{});
    localActorId_ = 0;
    Player& local = actors_[0];
    local.id = 0;
    local.controller = ControllerKind::Human;
    local.team = desiredPlayerTeam_;
    local.kills = 0;
    local.teamKills = 0;
    local.deaths = 0;
    local.money = economy_.StartMoney();
    EnsureBotAi();
    ResetFootstepStride();
    SpawnPlayer(local);
    SetupObjective();

    nextBotNameIndex_ = 0;
    const Team mySide    = LocalActor().team;
    const Team enemySide = TeamManager::Opponent(mySide);
    actors_.reserve(static_cast<size_t>(TOTAL_BOTS + 1));
    for (int i = 0; i < TOTAL_BOTS; ++i) {
        const Team t = (i < TEAM_SIZE) ? enemySide : mySide;
        Player bot;
        bot.id = static_cast<ActorId>(actors_.size());
        bot.controller = ControllerKind::Bot;
        SpawnBOT(bot, t);
        actors_.push_back(std::move(bot));
    }
    EnsureBotAi();
    for (size_t i = 0; i < actors_.size(); ++i) {
        if (actors_[i].IsBot()) {
            actorAi_[i].difficulty = tactics_.botProfile / 2;
        }
    }

    int nT = 0, nCT = 0;
    for (const auto& a : actors_) {
        if (!a.IsActive()) continue;
        (a.team == Team::T ? nT : nCT)++;
    }
    std::cout << "[CS2DGame] 对局开始: " << TEAM_SIZE << "v" << TEAM_SIZE
              << " 玩家阵营=" << (LocalActor().team == Team::T ? "T" : "CT")
              << " 实际人数 T=" << nT << " CT=" << nCT << std::endl;

    AssignBombCarrier();
    SnapshotRoundEconomy();
    AssignSquadRoles();
    CoachResetRound();
    CoachFor(Team::T).buyPlan  = DecideBuyPlan(Team::T);
    CoachFor(Team::CT).buyPlan = DecideBuyPlan(Team::CT);
    botBuyPending_ = true;
    botBuyDoneT_   = false;
    botBuyDoneCT_  = false;
    botBuyTimer_   = (coachLlmEnabled_ && coachLlm_) ? 7.5f : 0.0f;
    CoachOnEvent(Team::T,  CoachEventType::RoundStart);
    CoachOnEvent(Team::CT, CoachEventType::RoundStart);
    for (size_t i = 0; i < actors_.size(); ++i) {
        if (!actors_[i].IsBot()) continue;
        std::cout << "  " << actors_[i].name << " (" << TeamName(actors_[i].team) << ") 角色: "
                  << (actorSquad_[i].role == BotRole::Entry ? "突破"
                      : actorSquad_[i].role == BotRole::Anchor ? "断后" : "支援")
                  << std::endl;
    }
}

void CS2DSimulation::RestartMatchWithTeam(Team t) {
    desiredPlayerTeam_ = t;
    ResetToNewMatch();
}

void CS2DSimulation::Reset() {
    for (auto& a : actors_) a.dmLoadout = DmLoadout{};
    ResetToNewMatch();
}

// ============================================================================
// 经济 / 购买
// ============================================================================

bool CS2DSimulation::IsActorInBuyZone(ActorId actorId) const {
    const Player* actor = FindActor(actorId);
    if (!actor || !world_) return false;
    const auto& zones = world_->GetBuyZones();
    if (zones.empty()) return true;

    const char* myTeam = (actor->team == Team::CT) ? "ct" : "t";
    for (const auto& z : zones) {
        auto it = z.properties.find("team");
        if (it != z.properties.end()) {
            std::string tag = it->second;
            for (char& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tag != "both" && tag != myTeam) continue;
        }
        if (actor->x >= z.x && actor->x <= z.x + z.width &&
            actor->y >= z.y && actor->y <= z.y + z.height) {
            return true;
        }
    }
    return false;
}

bool CS2DSimulation::IsBuyWindowOpen() const {
    if (mode_ == MatchMode::DeathMatch) return true;
    if (teams_.MatchOver()) return false;
    return round_.Elapsed() <= Tuning::BuyWindowSeconds;
}

int CS2DSimulation::PurchasePriceOf(const Presentation::BuyRequest& request) const {
    if (request.kind == Presentation::BuyKind::DefuseKit) return 400;
    if (request.kind == Presentation::BuyKind::Armor) {
        if (request.armorTier == 1) return 650;
        if (request.armorTier == 2) return 1000;
        return 0;
    }
    const int idx = static_cast<int>(request.weapon);
    if (idx <= 0 || idx >= WeaponDB::kWeaponCount) return 0;
    return WeaponDB::Get(request.weapon).price;
}

int CS2DSimulation::PurchaseQuantityOf(const Player& actor, const Presentation::BuyRequest& request) const {
    if (request.kind == Presentation::BuyKind::DefuseKit) return actor.hasDefuseKit ? 1 : 0;
    if (request.kind == Presentation::BuyKind::Armor) {
        if (actor.armor >= 100 &&
            (request.armorTier == 1 || actor.helmet)) {
            return 1;
        }
        return 0;
    }
    const WeaponDef& def = WeaponDB::Get(request.weapon);
    switch (def.slot) {
        case WeaponSlotKind::Primary:   return (actor.weapons[0] == request.weapon) ? 1 : 0;
        case WeaponSlotKind::Secondary: return (actor.weapons[1] == request.weapon) ? 1 : 0;
        case WeaponSlotKind::Melee:     return (actor.weapons[2] == request.weapon) ? 1 : 0;
        case WeaponSlotKind::Grenade:   return actor.CountGrenade(request.weapon);
    }
    return 0;
}

Presentation::BuyCode CS2DSimulation::EvaluatePurchaseFor(
    ActorId actorId, const Presentation::BuyRequest& request) const {
    using Presentation::BuyCode;
    const Player* actorPtr = FindActor(actorId);
    if (!actorPtr) return BuyCode::PlayerDead;
    const Player& actor = *actorPtr;

    if (request.kind != Presentation::BuyKind::Weapon &&
        request.kind != Presentation::BuyKind::Armor &&
        request.kind != Presentation::BuyKind::DefuseKit) return BuyCode::InvalidItem;
    if (request.kind == Presentation::BuyKind::Armor) {
        if (request.armorTier < 1 || request.armorTier > 2) return BuyCode::InvalidItem;
    } else if (request.kind == Presentation::BuyKind::Weapon) {
        const int idx = static_cast<int>(request.weapon);
        if (idx <= 0 || idx >= WeaponDB::kWeaponCount) return BuyCode::InvalidItem;
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        if (def.price <= 0) return BuyCode::InvalidItem;
        if (def.slot == WeaponSlotKind::Melee) return BuyCode::InvalidItem;
    }

    if (request.kind == Presentation::BuyKind::DefuseKit && actor.team != Team::CT)
        return BuyCode::InvalidItem;

    if (!actor.alive) return BuyCode::PlayerDead;
    if (!IsBuyWindowOpen()) return BuyCode::WindowClosed;
    if (!IsActorInBuyZone(actorId)) return BuyCode::OutsideBuyZone;
    if (request.kind == Presentation::BuyKind::Weapon &&
        !WeaponDB::TeamAllowed(request.weapon, actor.team == Team::T)) {
        return BuyCode::TeamRestricted;
    }
    if (request.kind == Presentation::BuyKind::Weapon &&
        WeaponDB::RequiresLoadoutSelection(request.weapon) &&
        !WeaponDB::IsWeaponInLoadout(LoadoutForTeam(actor.team), request.weapon)) {
        return BuyCode::NotInLoadout;
    }

    {
        const bool isGrenade =
            request.kind == Presentation::BuyKind::Weapon &&
            WeaponDB::Get(request.weapon).slot == WeaponSlotKind::Grenade;
        if (!isGrenade && PurchaseQuantityOf(actor, request) > 0) {
            return BuyCode::AlreadyEquipped;
        }
    }

    if (request.kind == Presentation::BuyKind::Weapon) {
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        if (def.slot == WeaponSlotKind::Grenade) {
            if (actor.grenadeCount >= Player::MAX_GRENADES) return BuyCode::InventoryFull;
            const int limit = (request.weapon == WeaponId::GrenadeFlash) ? 2 : 1;
            if (actor.CountGrenade(request.weapon) >= limit) {
                return BuyCode::ItemLimitReached;
            }
        }
    }

    const int price = PurchasePriceOf(request);
    if (price <= 0) return BuyCode::InvalidItem;
    if (actor.money < price) return BuyCode::InsufficientFunds;

    return BuyCode::Ok;
}

Presentation::BuyResult CS2DSimulation::ExecutePurchaseFor(
    ActorId actorId, const Presentation::BuyRequest& request) {
    Presentation::BuyResult result;
    result.request = request;
    Player* actorPtr = FindActor(actorId);
    if (!actorPtr) {
        result.code = Presentation::BuyCode::PlayerDead;
        return result;
    }
    Player& actor = *actorPtr;
    result.code = EvaluatePurchaseFor(actorId, request);
    result.moneyAfter = actor.money;
    result.quantityAfter = PurchaseQuantityOf(actor, request);

    if (result.code != Presentation::BuyCode::Ok) {
        return result;
    }

    Player::EquipmentRecord receipt;
    receipt.id = nextEquipmentId_++;
    receipt.epoch = purchaseEpoch_;
    receipt.buyer = actorId;
    receipt.paid = PurchasePriceOf(request);
    receipt.armorTier = request.kind == Presentation::BuyKind::Armor ? request.armorTier : 0;
    receipt.previousArmor = actor.armor;
    receipt.previousHelmet = actor.helmet;
    if (request.kind == Presentation::BuyKind::DefuseKit) {
        actor.kitEquipment = receipt;
        actor.money -= 400;
        actor.hasDefuseKit = true;
    } else if (request.kind == Presentation::BuyKind::Armor) {
        actor.armorEquipment.push_back(receipt);
        const int tier  = request.armorTier;
        const int price = PurchasePriceOf(request);
        actor.money -= price;
        economy_.Clamp(actor.money);
        actor.armor = 100;
        if (tier == 2) actor.helmet = true;
        std::cout << "[Buy] " << (tier == 2 ? "护甲+头盔" : "护甲")
                  << " ($" << price << ") 余额 $" << actor.money << std::endl;
    } else {
        const int oldSlot = actor.weaponSlot;
        const WeaponId oldWeapon = actor.CurrentWeapon().id;
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        switch (def.slot) {
            case WeaponSlotKind::Primary:
                DropWeaponToGround(actor, 0, actor.x, actor.y);
                actor.SetSlotWeapon(0, request.weapon);
                actor.equipment[0] = receipt;
                actor.fireStates[0] = {};
                actor.weaponSlot = 0;
                actor.LoadAmmoFromSlot();
                actor.reloading = false;
                actor.reloadTimer = 0.0f;
                actor.holdingC4 = false;
                break;
            case WeaponSlotKind::Secondary:
                DropWeaponToGround(actor, 1, actor.x, actor.y);
                actor.SetSlotWeapon(1, request.weapon);
                actor.equipment[1] = receipt;
                actor.fireStates[1] = {};
                actor.weaponSlot = 1;
                actor.LoadAmmoFromSlot();
                actor.reloading = false;
                actor.reloadTimer = 0.0f;
                actor.holdingC4 = false;
                break;
            case WeaponSlotKind::Melee:
                actor.weapons[2] = request.weapon;
                break;
            case WeaponSlotKind::Grenade:
                actor.grenadeEquipment[actor.grenadeCount] = receipt;
                actor.grenades[actor.grenadeCount++] = request.weapon;
                actor.SyncActiveGrenade();
                break;
        }
        if (def.slot == WeaponSlotKind::Primary || def.slot == WeaponSlotKind::Secondary) {
            actor.previousEquipSlot = oldSlot;
            actor.previousEquipWeapon = oldWeapon;
            actor.switchSoundWeapon = request.weapon;
            actor.switchSoundPending = true;
        }
        actor.money -= def.price;
        economy_.Clamp(actor.money);
        std::cout << "[Buy] " << def.name << " ($" << def.price
                  << ") 余额 $" << actor.money << std::endl;
        if (def.slot == WeaponSlotKind::Primary || def.slot == WeaponSlotKind::Secondary) {
            actor.scopeLevel = 0;
            actor.scopeEngagement = 0.0f;
            actor.scoped = false;
            SimEvent sr;
            sr.type = SimEventType::ScopeReset;
            sr.actorId = actorId;
            sr.scopeReason = Presentation::ScopeResetReason::WeaponChanged;
            EmitSimEvent(std::move(sr));
            if (mode_ == MatchMode::DeathMatch) {
                actor.dmLoadout.primary   = actor.weapons[0];
                actor.dmLoadout.secondary = actor.weapons[1];
                actor.dmLoadout.valid     = true;
            }
        }
    }

    result.moneyAfter = actor.money;
    result.quantityAfter = PurchaseQuantityOf(actor, request);

    SimEvent buyEv;
    buyEv.type = SimEventType::BuyResult;
    buyEv.actorId = actorId;
    buyEv.x = actor.x;
    buyEv.y = actor.y;
    buyEv.buyRequest = request;
    buyEv.buyResult = result;
    EmitSimEvent(std::move(buyEv));

    return result;
}

bool CS2DSimulation::TryBuyFor(ActorId actorId, WeaponId id) {
    Presentation::BuyRequest r;
    r.kind   = Presentation::BuyKind::Weapon;
    r.weapon = id;
    return ExecutePurchaseFor(actorId, r).code == Presentation::BuyCode::Ok;
}

bool CS2DSimulation::TryBuyArmorFor(ActorId actorId, int tier) {
    Presentation::BuyRequest r;
    r.kind      = Presentation::BuyKind::Armor;
    r.weapon    = WeaponId::None;
    r.armorTier = tier;
    return ExecutePurchaseFor(actorId, r).code == Presentation::BuyCode::Ok;
}

// ============================================================================
// 出生点
// ============================================================================

bool CS2DSimulation::PickSpawnForTeam(Team team, float& outX, float& outY) {
    const bool isCT = (team == Team::CT);
    std::vector<const CS2DWorld::SpawnPoint*> candidates = world_->SpawnsForTeam(isCT);
    if (candidates.empty()) {
        outX = world_->GetMapPixelW() * 0.5f;
        outY = world_->GetMapPixelH() * 0.5f;
        return false;
    }

    constexpr float HITBOX = 6.0f;
    constexpr float SPAWN_SEPARATION = 16.0f;

    auto occupied = [&](float x, float y) {
        for (const auto& u : actors_) {
            if (!u.alive) continue;
            const float dx = u.x - x;
            const float dy = u.y - y;
            if (dx * dx + dy * dy < SPAWN_SEPARATION * SPAWN_SEPARATION) return true;
        }
        return false;
    };

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

void CS2DSimulation::SpawnPlayer(Player& actor) {
    const ActorId actorId = ActorIdOf(actor);
    const bool isLocal = (actorId == localActorId_);
    if (mode_ == MatchMode::DeathMatch && isLocal)
        ++purchaseEpoch_;
    ResetFootstepStride(actor);

    actor.aimKick = 0.0f;
    actor.fireStates = {};
    actor.equipment = {};
    actor.grenadeEquipment = {};
    actor.armorEquipment.clear();
    actor.kitEquipment = {};
    actor.hasDefuseKit = false;
    actor.recoilRecoveryTimer = 0.0f;
    actor.holdingC4 = false;
    actor.plantProgress = 0.0f;
    actor.defuseProgress = 0.0f;
    actor.flashTimer = 0.0f;
    actor.flashPeak = 0.0f;
    actor.scopeLevel = 0;
    actor.scopeEngagement = 0.0f;
    actor.scoped = false;

    actor.alive = true;
    actor.health = 100;
    actor.maxHealth = 100;
    actor.vx = actor.vy = 0.0f;
    actor.shootCooldown = 0.0f;
    if (isLocal) actor.team = desiredPlayerTeam_;
    if (mode_ == MatchMode::DeathMatch)
        actor.money = 16000;
    actor.animState = AnimState::Idle;
    actor.animTimer = 0.0f;
    actor.deathTimer = 0.0f;
    actor.shootAnimTimer = 0.0f;
    actor.spriteKey = (actor.team == Team::CT) ? 0 : 1;

    const WeaponId startingPistol = LoadoutForTeam(actor.team).starting;
    actor.weapons[0] = WeaponId::None;
    actor.weapons[1] = startingPistol;
    actor.weapons[2] = WeaponId::Knife;
    actor.weapons[3] = WeaponId::None;
    actor.weapons[4] = WeaponId::None;
    actor.grenadeCount = 0;
    actor.grenadeSlot = 0;
    for (int gi = 0; gi < Player::MAX_GRENADES; ++gi)
        actor.grenades[gi] = WeaponId::None;
    actor.weaponSlot = 1;
    actor.previousEquipSlot = 2;
    actor.previousEquipWeapon = WeaponId::Knife;
    actor.armor = 0;
    actor.helmet = false;

    const WeaponDef& w = WeaponDB::Get(actor.weapons[1]);
    actor.ammo = w.magazine;
    actor.maxAmmo = w.magazine;
    actor.reserve = Player::ReserveCapacity(w);
    for (int sl = 0; sl < 5; ++sl) {
        if (actor.weapons[sl] == WeaponId::None)
            actor.ClearSlotWeapon(sl);
        else
            actor.SetSlotWeapon(sl, actor.weapons[sl]);
    }
    actor.SaveAmmoToSlot();

    if (mode_ == MatchMode::DeathMatch && actor.dmLoadout.valid) {
        const auto& teamLoadout = LoadoutForTeam(actor.team);
        const bool isT = (actor.team == Team::T);
        auto validDmWeapon = [&](WeaponId wid) {
            if (wid == WeaponId::None) return false;
            if (!WeaponDB::TeamAllowed(wid, isT)) return false;
            return !WeaponDB::RequiresLoadoutSelection(wid) ||
                   WeaponDB::IsWeaponInLoadout(teamLoadout, wid);
        };
        if (validDmWeapon(actor.dmLoadout.primary)) {
            actor.SetSlotWeapon(0, actor.dmLoadout.primary);
        }
        if (validDmWeapon(actor.dmLoadout.secondary)) {
            actor.SetSlotWeapon(1, actor.dmLoadout.secondary);
        } else {
            actor.SetSlotWeapon(1, startingPistol);
        }
        actor.weaponSlot = (actor.weapons[0] != WeaponId::None) ? 0 : 1;
        actor.LoadAmmoFromSlot();
    }

    actor.lastGun = actor.CurrentWeapon().id;
    for (int slot = 0; slot < 5; ++slot)
        if (actor.weapons[slot] != WeaponId::None) {
            actor.equipment[slot].id = nextEquipmentId_++;
            actor.equipment[slot].epoch = purchaseEpoch_;
        }
    actor.reloading = false;
    actor.reloadTimer = 0.0f;
    actor.stealth = false;

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(actor.team, actor.x, actor.y);
        if (!IsEnemyNearby(actor.x, actor.y, 120.0f, actor.team))
            break;
    }
    actor.aimDistance = Tuning::InitialAimDistance;
    actor.aimWorldX = actor.x + std::cos(actor.aimAngle) * actor.aimDistance;
    actor.aimWorldY = actor.y + std::sin(actor.aimAngle) * actor.aimDistance;

    SimEvent ev;
    ev.type = SimEventType::ActorSpawned;
    ev.actorId = actorId;
    ev.x = actor.x;
    ev.y = actor.y;
    ev.team = actor.team;
    EmitSimEvent(std::move(ev));
}

void CS2DSimulation::SpawnBOT(Player& bot, Team team) {
    bot.aimKick = 0.0f;
    bot.fireStates = {};
    bot.equipment = {};
    bot.grenadeEquipment = {};
    bot.armorEquipment.clear();
    bot.kitEquipment = {};
    bot.hasDefuseKit = false;
    bot.recoilRecoveryTimer = 0.0f;
    bot.alive = true;
    bot.health = 100;
    bot.maxHealth = 100;
    bot.vx = bot.vy = 0.0f;
    bot.shootCooldown = 0.0f;
    bot.team = team;
    if (bot.name.empty()) {
        bot.name = kBotNicknames[nextBotNameIndex_ % kBotNicknameCount];
        ++nextBotNameIndex_;
    }
    bot.animState = AnimState::Idle;
    bot.animTimer = 0.0f;
    bot.deathTimer = 0.0f;
    bot.shootAnimTimer = 0.0f;
    bot.ammo = 30;
    bot.maxAmmo = 30;
    bot.reloading = false;
    bot.reloadTimer = 0.0f;
    bot.stealth = false;
    bot.scopeLevel = 0;
    bot.scopeEngagement = 0.0f;
    bot.scoped = false;

    if (team == Team::CT) {
        bot.spriteKey = 0;
        bot.weapons[1] = WeaponId::USP_S;
    } else {
        bot.spriteKey = 1 + static_cast<int>(rng_() % 2);
        bot.weapons[1] = WeaponId::Glock18;
    }
    bot.weapons[0] = WeaponId::None;
    bot.weapons[2] = WeaponId::Knife;

    {
        const int r = static_cast<int>(rng_() % 100);
        if (team == Team::T) {
            bot.weapons[3] = (r < 50)   ? WeaponId::GrenadeHE
                             : (r < 80) ? WeaponId::GrenadeFlash
                                        : WeaponId::GrenadeSmoke;
        } else {
            bot.weapons[3] = (r < 40)   ? WeaponId::GrenadeHE
                             : (r < 80) ? WeaponId::GrenadeSmoke
                                        : WeaponId::GrenadeFlash;
        }
    }
    bot.grenadeCount = 0;
    bot.grenadeSlot = 0;
    for (int gi = 0; gi < Player::MAX_GRENADES; ++gi)
        bot.grenades[gi] = WeaponId::None;
    bot.weapons[3] = WeaponId::None;
    bot.weaponSlot = 1;
    bot.armor = 0;
    bot.helmet = false;

    const WeaponDef& bw = WeaponDB::Get(bot.weapons[1]);
    bot.ammo = bw.magazine;
    bot.maxAmmo = bw.magazine;
    bot.reserve = Player::ReserveCapacity(bw);
    for (int sl = 0; sl < 5; ++sl) {
        if (bot.weapons[sl] == WeaponId::None)
            bot.ClearSlotWeapon(sl);
        else
            bot.SetSlotWeapon(sl, bot.weapons[sl]);
    }
    bot.SaveAmmoToSlot();
    bot.lastGun = bot.CurrentWeapon().id;
    for (int slot = 0; slot < 5; ++slot)
        if (bot.weapons[slot] != WeaponId::None) {
            bot.equipment[slot].id = nextEquipmentId_++;
            bot.equipment[slot].epoch = purchaseEpoch_;
        }
    bot.aimAngle = angleDist_(rng_);

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(team, bot.x, bot.y);
        if (!IsEnemyNearby(bot.x, bot.y, 120.0f, team))
            break;
    }
}

void CS2DSimulation::RespawnBOT(Player& bot) {
    SpawnBOT(bot, bot.team);
}

// ============================================================================
// 武器
// ============================================================================

void CS2DSimulation::QuickSwitchWeaponFor(ActorId actorId) {
    const Player* actorPtr = FindActor(actorId);
    if (!actorPtr) return;
    const Player& actor = *actorPtr;
    int slot = actor.previousEquipSlot;
    const WeaponId weapon = actor.previousEquipWeapon;
    if (slot == 3) {
        int grenade = -1;
        for (int i = 0; i < actor.grenadeCount; ++i) if (actor.grenades[i] == weapon) grenade = i;
        if (grenade < 0) slot = 2;
        else {
            SwitchWeaponSlotFor(actorId, 3, grenade);
            return;
        }
    } else if (slot == 4) {
        if (!ActorCarriesC4(actor)) slot = 2;
    } else if (slot < 0 || slot > 2 || actor.weapons[slot] != weapon) slot = 2;
    SwitchWeaponSlotFor(actorId, slot);
}

void CS2DSimulation::SwitchWeaponSlotFor(ActorId actorId, int slot, int grenadeIndex) {
    if (slot < 0 || slot > 4) return;
    Player* actorPtr = FindActor(actorId);
    if (!actorPtr) return;
    Player& actor = *actorPtr;

    if (slot == 4) {
        if (!ActorCarriesC4(actor)) return;
        actor.weapons[4] = WeaponId::C4;
        actor.equipment[4] = {};
        actor.equipment[4].id = c4EquipmentId_;
        actor.equipment[4].epoch = purchaseEpoch_;
    } else if (slot == 3) {
        if (actor.grenadeCount <= 0) return;
        if (grenadeIndex < 0) grenadeIndex = slot == actor.weaponSlot ?
            (actor.grenadeSlot + 1) % actor.grenadeCount : actor.grenadeSlot;
        if (grenadeIndex >= actor.grenadeCount) return;
    } else if (actor.weapons[slot] == WeaponId::None) {
        return;
    }
    if (slot == actor.weaponSlot && (slot != 3 || grenadeIndex == actor.grenadeSlot)) return;

    if (slot == 3 && actor.weaponSlot != 3) {
        actor.prevWeaponSlot = actor.weaponSlot;
    }

    actor.SaveAmmoToSlot();

    actor.previousEquipSlot = actor.weaponSlot;
    actor.previousEquipWeapon = actor.CurrentWeapon().id;
    if (slot == 3) actor.grenadeSlot = grenadeIndex;
    actor.weaponSlot = slot;
    actor.reloading   = false;
    actor.reloadTimer = 0.0f;
    actor.holdingC4 = (slot == 4);
    actor.scopeLevel = 0;
    actor.scopeEngagement = 0.0f;
    actor.scoped = false;

    {
        SimEvent sr;
        sr.type = SimEventType::ScopeReset;
        sr.actorId = actorId;
        sr.scopeReason = Presentation::ScopeResetReason::WeaponChanged;
        EmitSimEvent(std::move(sr));
    }

    EmitSound(SoundEventType::WeaponSwitch, actor.x, actor.y, actor.team, actorId);

    if (slot == 4) {
        actor.ammo = 1;
        actor.maxAmmo = 1;
        actor.reserve = 0;
        actor.switchSoundWeapon = WeaponId::C4;
        actor.switchSoundPending = true;
        return;
    }
    if (slot == 3) {
        actor.SyncActiveGrenade();
        actor.ammo = 1;
        actor.maxAmmo = 1;
        actor.reserve = 0;
        actor.switchSoundWeapon = actor.ActiveGrenade();
        actor.switchSoundPending = true;
        return;
    }

    actor.LoadAmmoFromSlot();
    actor.switchSoundWeapon = actor.weapons[slot];
    actor.switchSoundPending = true;
}

std::string CS2DSimulation::GetWeaponName() const {
    return GetWeaponNameFor(LocalActor());
}

std::string CS2DSimulation::GetWeaponNameFor(const Player& p) const {
    return WeaponDB::Get(p.CurrentWeapon().id).name;
}

// ============================================================================
// 每帧更新（Step / StepActors）
// ============================================================================

void CS2DSimulation::Step(float dt, const std::vector<InputCmd>& cmds) {
    std::vector<std::pair<ActorId, ActorInput>> actorInputs;
    actorInputs.reserve(cmds.size());

    for (const auto& cmd : cmds) {
        Player* actor = FindActor(cmd.actorId);
        if (!actor || !actor->IsHuman()) continue;

        if (cmd.Has(InputButton::Drop))        DropCurrentWeaponFor(cmd.actorId);
        if (cmd.Has(InputButton::QuickSwitch)) QuickSwitchWeaponFor(cmd.actorId);
        if (cmd.Has(InputButton::ToggleC4))    ToggleHoldC4For(cmd.actorId);
        if (cmd.switchSlot >= 0)               SwitchWeaponSlotFor(cmd.actorId, cmd.switchSlot, cmd.grenadeIndex);

        if (cmd.buyWeapon > 0 && cmd.buyWeapon < WeaponDB::kWeaponCount) {
            Presentation::BuyRequest req;
            req.kind = Presentation::BuyKind::Weapon;
            req.weapon = static_cast<WeaponId>(cmd.buyWeapon);
            ExecutePurchaseFor(cmd.actorId, req);
        }
        if (cmd.buyArmorTier > 0) {
            Presentation::BuyRequest req;
            req.kind = Presentation::BuyKind::Armor;
            req.armorTier = static_cast<int>(cmd.buyArmorTier);
            ExecutePurchaseFor(cmd.actorId, req);
        }
        if (cmd.buyKit) {
            Presentation::BuyRequest req;
            req.kind = Presentation::BuyKind::DefuseKit;
            ExecutePurchaseFor(cmd.actorId, req);
        }

        if (cmd.Has(InputButton::ScopeToggle) && actor->alive && !actor->reloading) {
            const WeaponDef& def = actor->CurrentWeapon();
            if (def.scoped && (def.automatic || actor->shootCooldown <= 0.0f)) {
                actor->scopeLevel = (actor->scopeLevel + 1) % Presentation::kScopeLevelCount;
                actor->scoped = (actor->scopeLevel > 0);
                actor->scopeEngagement = actor->scoped ? 1.0f : 0.0f;
            }
        }

        actorInputs.emplace_back(cmd.actorId, cmd.ToActorInput());
    }

    StepActors(dt, actorInputs);
}

void CS2DSimulation::UpdateHumanActor(Player& actor, float dt, const ActorInput& input) {
    const ActorId actorId = ActorIdOf(actor);
    const bool isLocal = (actorId == localActorId_);
    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool canAct  = !frozen;
    const bool canMove = !frozen;

    float targetWorldX = actor.aimWorldX;
    float targetWorldY = actor.aimWorldY;
    if (input.hasAimWorld) {
        if (input.aimIsRelative) {
            targetWorldX = actor.x + input.aimWorldX;
            targetWorldY = actor.y + input.aimWorldY;
        } else {
            targetWorldX = input.aimWorldX;
            targetWorldY = input.aimWorldY;
        }
        SetActorAimTarget(actorId, targetWorldX, targetWorldY);
    }

    actor.aimWorldX = targetWorldX;
    actor.aimWorldY = targetWorldY;
    actor.interactHeld = input.interact && canAct;
    actor.fireHeld     = input.fire && canAct;
    actor.stealth = input.stealth && canAct;

    if (canAct && input.reload && !actor.reloading) {
        if ((actor.weaponSlot == 0 || actor.weaponSlot == 1) &&
            actor.ammo < actor.maxAmmo && actor.reserve > 0) {
            StartReload(actor);
        }
    }

    if (actor.alive && canMove) {
        UpdatePlayerMovement(actor, dt, input.moveUp, input.moveLeft, input.moveDown, input.moveRight, actor.stealth);
        if (input.hasAimWorld && input.aimIsRelative) {
            targetWorldX = actor.x + input.aimWorldX;
            targetWorldY = actor.y + input.aimWorldY;
            actor.aimWorldX = targetWorldX;
            actor.aimWorldY = targetWorldY;
        }
        UpdatePlayerShooting(actor, dt, input.fire, targetWorldX, targetWorldY);
    } else {
        actor.vx = actor.vy = 0.0f;
        if (actor.alive && frozen && input.hasAimWorld) {
            actor.aimAngle = std::atan2(targetWorldY - actor.y, targetWorldX - actor.x);
            actor.stealth = false;
        }
        if (mode_ == MatchMode::DeathMatch && !actor.alive) {
            if (isLocal) {
                botRespawnTimer_ -= dt;
                if (botRespawnTimer_ <= 0.0f) SpawnPlayer(actor);
            } else {
                SpawnPlayer(actor);
            }
        }
    }

    if (canAct && input.interact && !actor.prevInteract && actor.alive) {
        const bool busyDefusing = (mode_ == MatchMode::BombDefusal && bombPlanted_ &&
                                   actor.defuseProgress > 0.0f);
        if (!busyDefusing) TryPickupDropsFor(actorId, true);
    }
    actor.prevInteract = input.interact;
}

void CS2DSimulation::StepActors(float dt, const std::vector<std::pair<ActorId, ActorInput>>& inputs) {
    ++traceTick_;
    for (auto& a : actors_) {
        a.dryFireCooldown = std::max(0.0f, a.dryFireCooldown - dt);
    }

    if (!world_) return;

    EnsureBotAi();
    clockSec_ += dt;

    auto findInput = [&](ActorId id) -> const ActorInput* {
        for (const auto& entry : inputs) {
            if (entry.first == id) return &entry.second;
        }
        return nullptr;
    };

    if (const ActorInput* localIn = findInput(localActorId_)) {
        if (localIn->hasAimWorld) {
            const auto& local = LocalActor();
            const float ax = localIn->aimIsRelative ? (local.x + localIn->aimWorldX) : localIn->aimWorldX;
            const float ay = localIn->aimIsRelative ? (local.y + localIn->aimWorldY) : localIn->aimWorldY;
            SetActorAimTarget(localActorId_, ax, ay);
        }
    }

    for (auto& actor : actors_) {
        if (!actor.IsHuman()) continue;
        actor.RecoverBallistics(dt);
        actor.recoilRecoveryTimer = std::max(0.0f, actor.recoilRecoveryTimer - dt);
        if (actor.recoilRecoveryTimer <= 0.0f)
            actor.aimKick *= std::exp(-actor.CurrentWeapon().recoilRecoverPerSec * dt);
    }

    for (auto& actor : actors_) {
        if (!actor.switchSoundPending) continue;
        actor.switchSoundPending = false;
        if (actor.switchSoundWeapon != WeaponId::None) {
            SimEvent ev;
            ev.type = SimEventType::WeaponSwitched;
            ev.actorId = ActorIdOf(actor);
            ev.weapon = actor.switchSoundWeapon;
            ev.x = actor.x;
            ev.y = actor.y;
            EmitSimEvent(std::move(ev));
        }
        actor.switchSoundWeapon = WeaponId::None;
    }

    UpdateRoundFlow(dt);
    UpdateTeamContacts(dt);
    intel_.Update(dt);

    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool canMove = !frozen;

    for (auto& actor : actors_) {
        if (!actor.IsHuman()) continue;
        const ActorInput* in = findInput(actor.id);
        ActorInput idleInput;
        idleInput.hasAimWorld = false;
        UpdateHumanActor(actor, dt, in ? *in : idleInput);
    }

    UpdateBombItem(dt);

    if (canMove) {
        UpdateBOTs(dt);
    } else {
        for (auto& actor : actors_) {
            if (actor.IsBot()) { actor.vx = actor.vy = 0.0f; }
        }
    }

    for (auto& actor : actors_) {
        if (!actor.IsActive()) continue;
        if (!actor.alive) {
            actor.footstepStride = 0.0f;
            actor.footstepLastValid = false;
            continue;
        }
        const bool positional = !(actor.IsHuman() && ActorIdOf(actor) == localActorId_);
        AccumulateFootstep(actor, positional);
    }

    CoachUpdate(dt);
    UpdateBotBuy(dt);

    UpdateBullets(dt);
    UpdateGrenades(dt);
    UpdateObjective(dt);
    CheckWinConditions();

    auto updateAnim = [](Player& p) {
        if (!p.alive) {
            p.animState = AnimState::Dead;
        } else if (p.shootAnimTimer > 0.0f) {
            p.animState = AnimState::Shoot;
        } else if (std::abs(p.vx) > 10.0f || std::abs(p.vy) > 10.0f) {
            p.animState = AnimState::Walk;
        } else {
            p.animState = AnimState::Idle;
        }
    };

    for (auto& actor : actors_) {
        if (!actor.IsActive()) continue;
        if (actor.alive) {
            if (actor.shootAnimTimer > 0.0f) actor.shootAnimTimer -= dt;
        } else {
            actor.deathTimer += dt;
        }
        actor.animTimer += dt;
        updateAnim(actor);
        UpdateReload(actor, dt);
        if (mode_ == MatchMode::DeathMatch) {
            actor.money = 16000;
        }
        if (actor.IsHuman()) {
            const ActorInput* in = findInput(actor.id);
            actor.prevMouseDown = in ? in->fire : false;
        }
    }

    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                [](const DroppedItem& d) { return d.id == WeaponId::None && !d.defuseKit; }),
                 drops_.end());
    for (auto& d : drops_) {
        d.fireState.Recover(dt);
        if (d.pickupDelay > 0.0f) d.pickupDelay -= dt;
    }
    TryPickupDropsFor(localActorId_, false);

    for (auto& fx : blasts_) fx.life -= dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(),
                                 [](const BlastFx& b) { return b.life <= 0.0f; }),
                  blasts_.end());

    for (auto& e : killFeed_) e.life -= dt;
    killFeed_.erase(std::remove_if(killFeed_.begin(), killFeed_.end(),
                                   [](const KillFeedEntry& e) { return e.life <= 0.0f; }),
                    killFeed_.end());
}

// ============================================================================
// 脚步
// ============================================================================

void CS2DSimulation::ResetFootstepStride() {
    for (auto& actor : actors_) ResetFootstepStride(actor);
}

void CS2DSimulation::ResetFootstepStride(Player& actor) {
    actor.footstepStride = 0.0f;
    actor.footstepLastValid = false;
}

int CS2DSimulation::AccumulateFootstep(Player& actor, bool positional) {
    if (!actor.alive || actor.stealth) {
        actor.footstepStride = 0.0f;
        actor.footstepLastValid = false;
        return 0;
    }
    if (!actor.footstepLastValid) {
        actor.footstepLastX = actor.x;
        actor.footstepLastY = actor.y;
        actor.footstepLastValid = true;
        return 0;
    }
    const float dx = actor.x - actor.footstepLastX, dy = actor.y - actor.footstepLastY;
    actor.footstepLastX = actor.x;
    actor.footstepLastY = actor.y;
    const float moved = std::sqrt(dx * dx + dy * dy);
    if (moved > Tuning::PlayerSize * 40.0f) {
        actor.footstepStride = 0.0f;
        return 0;
    }
    if (moved <= 0.0f) return 0;
    actor.footstepStride += moved;
    if (actor.footstepStride < Tuning::FootstepStride) return 0;

    int steps = 0;
    while (actor.footstepStride >= Tuning::FootstepStride && steps < Tuning::FootstepMaxPerFrame) {
        actor.footstepStride -= Tuning::FootstepStride;
        ++steps;
    }
    if (actor.footstepStride >= Tuning::FootstepStride) {
        actor.footstepStride = std::fmod(actor.footstepStride, Tuning::FootstepStride);
    }
    if (steps <= 0) return 0;

    actor.footstepCount += static_cast<std::uint64_t>(steps);
    {
        SimEvent ev;
        ev.type = SimEventType::FootstepEvent;
        ev.actorId = ActorIdOf(actor);
        ev.x = actor.x;
        ev.y = actor.y;
        ev.intParam = steps;
        ev.flagA = positional;
        EmitSimEvent(std::move(ev));
    }
    for (int i = 0; i < steps; ++i) {
        OnFootstepSound(actor, actor.x, actor.y);
    }
    return steps;
}

void CS2DSimulation::GetTeamSpawnCenter(Team team, float& outX, float& outY) const {
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

CS2DSimulation::RefundQuote CS2DSimulation::EvaluateRefundFor(ActorId actorId, const Presentation::BuyRequest& request) const {
    RefundQuote quote;
    const Player* actorPtr = FindActor(actorId);
    if (!actorPtr || !actorPtr->alive) {
        quote.reason = "阵亡后无法退款";
        return quote;
    }
    const Player& actor = *actorPtr;
    if (!IsBuyWindowOpen()) {
        quote.reason = "购买时限已结束";
        return quote;
    }
    bool inZone = IsActorInBuyZone(actorId);
    if (world_ && world_->GetBuyZones().empty()) {
        inZone = false;
        for (const auto* spawn : world_->SpawnsForTeam(actor.team == Team::CT))
            inZone = inZone || std::hypot(actor.x - spawn->x, actor.y - spawn->y) <= 80;
    }
    if (!inZone) {
        quote.reason = "返回本方出生点退款";
        return quote;
    }
    auto consider = [&](const Player::EquipmentRecord& record) {
        if (record.id && record.epoch == purchaseEpoch_ && record.buyer == actorId && !record.used) {
            ++quote.quantity;
            quote.amount = record.paid;
        }
    };
    if (request.kind == Presentation::BuyKind::DefuseKit && actor.hasDefuseKit)
        consider(actor.kitEquipment);
    if (request.kind == Presentation::BuyKind::Armor && !actor.armorEquipment.empty()) {
        const auto& record = actor.armorEquipment.back();
        if (record.armorTier == request.armorTier)
            consider(record);
    }
    if (request.kind == Presentation::BuyKind::Weapon) {
        for (int slot = 0; slot < 3; ++slot)
            if (actor.weapons[slot] == request.weapon)
                consider(actor.equipment[slot]);
        for (int i = 0; i < actor.grenadeCount; ++i)
            if (actor.grenades[i] == request.weapon)
                consider(actor.grenadeEquipment[i]);
    }
    if (!quote.amount)
        quote.reason = "需为本轮本人购买且尚未使用的装备";
    else if (actor.money + quote.amount > 16000) {
        quote.amount = quote.quantity = 0;
        quote.reason = "余额无法容纳全额退款";
    }
    return quote;
}

bool CS2DSimulation::ExecuteRefundFor(ActorId actorId, const Presentation::BuyRequest& request) {
    const auto quote = EvaluateRefundFor(actorId, request);
    if (quote.amount <= 0)
        return false;
    Player* actorPtr = FindActor(actorId);
    if (!actorPtr) return false;
    Player& actor = *actorPtr;
    if (request.kind == Presentation::BuyKind::DefuseKit) {
        actor.hasDefuseKit = false;
        actor.kitEquipment = {};
    } else if (request.kind == Presentation::BuyKind::Armor) {
        const auto receipt = actor.armorEquipment.back();
        actor.armor = receipt.previousArmor;
        actor.helmet = receipt.previousHelmet;
        actor.armorEquipment.pop_back();
    } else {
        if (WeaponDB::Get(request.weapon).cls == WeaponClass::Grenade) {
            int selected = -1;
            std::uint64_t newest = 0;
            for (int i = 0; i < actor.grenadeCount; ++i) {
                const auto& record = actor.grenadeEquipment[i];
                if (actor.grenades[i] == request.weapon && record.buyer == actorId &&
                    record.epoch == purchaseEpoch_ && !record.used && record.id > newest) {
                    selected = i;
                    newest = record.id;
                }
            }
            const int previousIndex = actor.grenadeSlot;
            actor.grenadeSlot = selected;
            actor.ConsumeActiveGrenade();
            actor.grenadeSlot = std::min(previousIndex, std::max(0, actor.grenadeCount - 1));
            actor.SyncActiveGrenade();
        } else {
            for (int slot = 0; slot < 3; ++slot) {
                if (actor.weapons[slot] == request.weapon) {
                    actor.ClearSlotWeapon(slot);
                    actor.equipment[slot] = {};
                    actor.fireStates[slot] = {};
                }
            }
        }
        if (actor.weapons[actor.weaponSlot] == WeaponId::None)
            EquipFallbackWeaponFor(actor);
        actor.previousEquipSlot = 2;
        actor.previousEquipWeapon = WeaponId::Knife;
        actor.reloading = false;
        actor.reloadTimer = 0.0f;
        actor.scopeLevel = 0;
        actor.scopeEngagement = 0.0f;
        actor.scoped = false;
        SimEvent sr;
        sr.type = SimEventType::ScopeReset;
        sr.actorId = actorId;
        sr.scopeReason = Presentation::ScopeResetReason::WeaponChanged;
        EmitSimEvent(std::move(sr));
    }
    actor.money += quote.amount;
    return true;
}
