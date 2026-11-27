#include "CS2DGame.hpp"
#include "MapCatalog.hpp"
#include <filesystem>
#include "TMJParser.hpp"
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
    actors_.resize(1);
    actors_[0].id = 0;
    actors_[0].controller = ControllerKind::Human;
    actors_[0].team = desiredPlayerTeam_;
    localActorId_ = 0;
    EnsureBotAi();
}

CS2DGame::Player* CS2DGame::FindActor(ActorId id) {
    if (id < 0 || id >= static_cast<ActorId>(actors_.size())) return nullptr;
    return &actors_[static_cast<size_t>(id)];
}

const CS2DGame::Player* CS2DGame::FindActor(ActorId id) const {
    if (id < 0 || id >= static_cast<ActorId>(actors_.size())) return nullptr;
    return &actors_[static_cast<size_t>(id)];
}

ActorId CS2DGame::ActorIdOf(const Player& actor) const {
    if (actors_.empty()) return kInvalidActorId;
    const Player* base = actors_.data();
    const Player* ptr  = &actor;
    if (ptr < base || ptr >= base + actors_.size()) return kInvalidActorId;
    return static_cast<ActorId>(ptr - base);
}

void CS2DGame::SetLocalActorId(ActorId id) {
    if (id >= 0 && id < static_cast<ActorId>(actors_.size())) {
        localActorId_ = id;
    }
}

bool CS2DGame::SetActorController(ActorId id, ControllerKind kind,
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

// ============================================================================
// 地图载入 / 重置
// ============================================================================

bool CS2DGame::LoadMapHeadless(const std::string& resolvedTMJPath, bool resetMatch,
                               float visionRadius) {
    CS2DMapData mapData;
    if (!TMJParser::Parse(resolvedTMJPath, mapData)) return false;
    if (!world_->Build(mapData)) return false;

    // 玩法层自己持有一份几何查询：无渲染器（无头仿真）也能做视线/可见性判定
    visibility_.SetWorld(world_.get());
    visibility_.SetDistanceLimited(false);
    visibility_.SetVisionRadius(visionRadius);

    // 烘焙寻路网格（角色半径 3.5，略大于命中盒半宽）
    pathFinder_.Build(*world_, Tuning::PathAgentRadius);
    std::cout << "[PathFinder] 网格 " << pathFinder_.Width() << "x" << pathFinder_.Height()
              << " 已构建" << std::endl;

    // 解析 mapId / playbookId：优先按文件名匹配 MapCatalog，否则用文件名去扩展名。
    // 只比较 filename，避免 Windows 反斜杠路径与 catalog 里的正斜杠不匹配。
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

    // 每图战术表（可选）：缺失时教练回退到代码里的默认分兵与关键字扫描。
    // 战术表与地图同属 assets：<assets>/cs2d/maps/x.tmj → <assets>/config/tactics/x.ini
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
        // 校验：表里写的区域名必须在这张图上真实存在，否则该槽位会退化成"无目标"
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

    // 预热战术库：换图即加载 + 校验区域名，不必等到第一次 LLM 请求才报错。
    CoachWarmupPlaybook();

    if (resetMatch) {
        ResetToNewMatch();
    } else {
        // 选阵营前只保留地图世界，不生成玩家和 BOT。
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
        ResetScope(Presentation::ScopeResetReason::MatchExit);
    }
    intel_.Build(world_.get(), clockSec_);
    return true;
}

// 开始一场全新比赛（清空比分与统计）
void CS2DGame::ResetToNewMatch() {
    StopObjectiveAudio(nullptr);
    damageDirections_ = {};
    damageFlashTimer_ = hitMarkerTimer_ = 0.0f;
    dryFireHintTimer_ = 0.0f;
    hitMarkerKind_ = HitMarkerKind::Normal;
    bullets_.clear();
    bulletVisuals_.clear();
    nextBulletVisualId_ = 1;
    // 新一局/换队：开镜立即归零
    ResetScope(Presentation::ScopeResetReason::RoundRestart);
    grenades_.clear();
    smokes_.clear();
    fires_.clear();
    decoys_.clear();
    fireTickTimer_ = 0.0f;
    matchOverAnnounced_ = false;
    matchOverTimer_ = 0.0f;
    killFeed_.clear();
    deathPresentation_ = {};
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

    // CT 侧：玩家 + (TEAM_SIZE-1) 个 BOT；T 侧：TEAM_SIZE 个 BOT
    nextBotNameIndex_ = 0;
    // 玩家所在阵营 4 个 BOT（凑满 5 人），敌方 5 个 BOT
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

    AssignBombCarrier();   // 全员创建后再分配一次
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
    for (size_t i = 0; i < actors_.size(); ++i) {
        if (!actors_[i].IsBot()) continue;
        std::cout << "  " << actors_[i].name << " (" << TeamName(actors_[i].team) << ") 角色: "
                  << (actorSquad_[i].role == BotRole::Entry ? "突破"
                      : actorSquad_[i].role == BotRole::Anchor ? "断后" : "支援")
                  << std::endl;
    }
}

void CS2DGame::RestartMatchWithTeam(Team t) {
    desiredPlayerTeam_ = t;
    ResetToNewMatch();
}

void CS2DGame::Reset() {
    for (auto& a : actors_) a.dmLoadout = DmLoadout{};   // 新对局不沿用上一局的死斗装备
    ResetToNewMatch();
}

// ============================================================================
// 经济 / 购买
// ============================================================================

// 单位是否处于本方购买区（地图未定义购买区时视为全图可买）
bool CS2DGame::IsActorInBuyZone(ActorId actorId) const {
    const Player* actor = FindActor(actorId);
    if (!actor || !world_) return false;
    const auto& zones = world_->GetBuyZones();
    if (zones.empty()) return true;

    const char* myTeam = (actor->team == Team::CT) ? "ct" : "t";
    for (const auto& z : zones) {
        // 阵营过滤：属性 team 为空或 both 时对所有阵营开放
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

// ---------------------------------------------------------------------------
// 购买契约 v1（第 3 节）：判定 / 提交 / 兼容包装
// ---------------------------------------------------------------------------

// 只看「时窗」：死斗没有回合计时（区域与存活由调用方分别判断），
// 其余模式复用 round_.Elapsed() 与原比赛结束规则。
// 不能直接用 IsBuyPhase()：它把区域/存活/模式混在一起，会让失败原因错位。
bool CS2DGame::IsBuyWindowOpen() const {
    if (mode_ == MatchMode::DeathMatch) return true;
    if (teams_.MatchOver()) return false;
    return round_.Elapsed() <= Tuning::BuyWindowSeconds;
}

int CS2DGame::PurchasePriceOf(const Presentation::BuyRequest& request) const {
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

int CS2DGame::PurchaseQuantityOf(const Player& actor, const Presentation::BuyRequest& request) const {
    if (request.kind == Presentation::BuyKind::DefuseKit) return actor.hasDefuseKit ? 1 : 0;
    if (request.kind == Presentation::BuyKind::Armor) {
        // tier1 = 满甲即已拥有；tier2 = 满甲且有头盔才算已拥有
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

Presentation::BuyCode CS2DGame::EvaluatePurchaseFor(
    ActorId actorId, const Presentation::BuyRequest& request) const {
    using Presentation::BuyCode;
    const Player* actorPtr = FindActor(actorId);
    if (!actorPtr) return BuyCode::PlayerDead;
    const Player& actor = *actorPtr;

    // 1) 非法请求 / 非售卖项：先校验 enum，再索引 WeaponDB
    if (request.kind != Presentation::BuyKind::Weapon &&
        request.kind != Presentation::BuyKind::Armor &&
        request.kind != Presentation::BuyKind::DefuseKit) return BuyCode::InvalidItem;
    if (request.kind == Presentation::BuyKind::Armor) {
        if (request.armorTier < 1 || request.armorTier > 2) return BuyCode::InvalidItem;
    } else if (request.kind == Presentation::BuyKind::Weapon) {
        const int idx = static_cast<int>(request.weapon);
        if (idx <= 0 || idx >= WeaponDB::kWeaponCount) return BuyCode::InvalidItem;
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        if (def.price <= 0) return BuyCode::InvalidItem;                    // 刀 / C4 不售卖
        if (def.slot == WeaponSlotKind::Melee) return BuyCode::InvalidItem;
    }

    if (request.kind == Presentation::BuyKind::DefuseKit && actor.team != Team::CT)
        return BuyCode::InvalidItem;

    // 2) 存活 → 时窗 → 区域 → 阵营 → 本场配装（固定优先序，保证失败原因稳定可测）
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

    // 3) 已拥有：同一把主/副武器不再重复扣款补弹；
    //    投掷物可叠加，容量由下一步的 InventoryFull / ItemLimitReached 判定
    {
        const bool isGrenade =
            request.kind == Presentation::BuyKind::Weapon &&
            WeaponDB::Get(request.weapon).slot == WeaponSlotKind::Grenade;
        if (!isGrenade && PurchaseQuantityOf(actor, request) > 0) {
            return BuyCode::AlreadyEquipped;
        }
    }

    // 4) 投掷物容量：总数 → 同类数量
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

    // 5) 金额
    const int price = PurchasePriceOf(request);
    if (price <= 0) return BuyCode::InvalidItem;
    if (actor.money < price) return BuyCode::InsufficientFunds;

    return BuyCode::Ok;
}

Presentation::BuyResult CS2DGame::ExecutePurchaseFor(
    ActorId actorId, const Presentation::BuyRequest& request, IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    Presentation::BuyResult result;
    result.request = request;
    Player* actorPtr = FindActor(actorId);
    if (!actorPtr) {
        result.code = Presentation::BuyCode::PlayerDead;
        return result;
    }
    Player& actor = *actorPtr;
    const bool isLocal = (actorId == localActorId_);
    result.code = EvaluatePurchaseFor(actorId, request);      // 点击时重新判定
    result.moneyAfter = actor.money;
    result.quantityAfter = PurchaseQuantityOf(actor, request);

    if (result.code != Presentation::BuyCode::Ok) {
        return result;                            // 失败：金额 / 装备 / 音效都不动
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
        // 购买替换了当前手持武器 → 开镜必须归零
        if (def.slot == WeaponSlotKind::Primary || def.slot == WeaponSlotKind::Secondary) {
            if (isLocal) ResetScope(Presentation::ScopeResetReason::WeaponChanged);
            // 死斗：记住这次买到的装备，下次重生自动装配（仍可继续购买）
            if (mode_ == MatchMode::DeathMatch) {
                actor.dmLoadout.primary   = actor.weapons[0];
                actor.dmLoadout.secondary = actor.weapons[1];
                actor.dmLoadout.valid     = true;
            }
        }
    }

    if (audio) {
        if (isLocal) audio->PlaySound(SoundEffect::EQUIP);
        else audio->PlaySoundAt(SoundEffect::EQUIP, actor.x, actor.y, ListenerX(), ListenerY());
    }
    result.moneyAfter = actor.money;
    result.quantityAfter = PurchaseQuantityOf(actor, request);
    return result;
}

// 兼容包装：旧调用点保持原签名，成功只执行一次，不递归、不重复扣款。
bool CS2DGame::TryBuyFor(ActorId actorId, WeaponId id, IAudioSink* audio) {
    Presentation::BuyRequest r;
    r.kind   = Presentation::BuyKind::Weapon;
    r.weapon = id;
    return ExecutePurchaseFor(actorId, r, audio).code == Presentation::BuyCode::Ok;
}

// 购买护甲：tier 1=护甲($650) 2=护甲+头盔($1000)
bool CS2DGame::TryBuyArmorFor(ActorId actorId, int tier, IAudioSink* audio) {
    Presentation::BuyRequest r;
    r.kind      = Presentation::BuyKind::Armor;
    r.weapon    = WeaponId::None;
    r.armorTier = tier;
    return ExecutePurchaseFor(actorId, r, audio).code == Presentation::BuyCode::Ok;
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
        for (const auto& u : actors_) {
            if (!u.alive) continue;
            const float dx = u.x - x;
            const float dy = u.y - y;
            if (dx * dx + dy * dy < SPAWN_SEPARATION * SPAWN_SEPARATION) return true;
        }
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

void CS2DGame::SpawnPlayer(Player& actor) {
    const bool isLocal = (ActorIdOf(actor) == localActorId_);
    if (mode_ == MatchMode::DeathMatch && isLocal)
        ++purchaseEpoch_;
    if (isLocal) {
        ++playerSpawnVersion_;
        recoilShake_ = recoilShakeAge_ = 0.0f;
        aimFollowsPlayer_ = false;
        viewHistoryValid_ = false;
        edgeZoom_ = previousEdgeZoom_ = edgeZoomFrom_ = edgeZoomTo_ = 0.0f;
        edgeZoomTime_ = 1.0f;
        edgeZoomRequested_ = false;
        cameraDistanceTier_ = 0;
        cameraPrepared_ = cameraScopeSettled_ = false;
        SpectatorEnd();
        deathPresentation_ = {};
        deathCam_ = false; // 死斗重生：退出死亡镜头
        crosshairBloom_ = 0.0f;
        aimMotionBlur_ = 0;
    }
    ResetFootstepStride(actor);   // 重生：清零脚步累计器，避免凭空补步

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
    actor.plantingChannel = -1;
    actor.flashTimer = 0.0f;
    actor.flashPeak = 0.0f;

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
    // 开局只有起始手枪（按本场阵营配装的起始槽位）：
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
    actor.weaponSlot = 1; // 默认持手枪
    actor.previousEquipSlot = 2;
    actor.previousEquipWeapon = WeaponId::Knife;
    actor.armor = 0;
    actor.helmet = false;

    const WeaponDef &w = WeaponDB::Get(actor.weapons[1]);
    actor.ammo = w.magazine;
    actor.maxAmmo = w.magazine;
    actor.reserve = Player::ReserveCapacity(w);
    // 各槽位弹药同步（避免旧局残留）
    for (int sl = 0; sl < 5; ++sl) {
        if (actor.weapons[sl] == WeaponId::None)
            actor.ClearSlotWeapon(sl);
        else
            actor.SetSlotWeapon(sl, actor.weapons[sl]);
    }
    actor.SaveAmmoToSlot();

    // 死斗：重生自动装配「上一次购买的武器」（按本场阵营与配装校验，失效则回退起始手枪）
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
}

void CS2DGame::SpawnBOT(Player &bot, Team team) {
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
    // 首次创建时分配昵称；重生时保留原昵称。
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

    // 外观与武器：开局只有起始手枪（照抄 CS2：CT = USP-S，T = Glock-18）
    if (team == Team::CT) {
        bot.spriteKey = 0;
        bot.weapons[1] = WeaponId::USP_S;
    } else {
        bot.spriteKey = 1 + static_cast<int>(rng_() % 2);
        bot.weapons[1] = WeaponId::Glock18;
    }
    bot.weapons[0] = WeaponId::None; // 主武器需购买
    bot.weapons[2] = WeaponId::Knife;

    // 投掷物类型：T 偏进攻（闪光），CT 偏防守（烟雾）
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
    bot.grenadeCount = 0; // 需购买
    bot.grenadeSlot = 0;
    for (int gi = 0; gi < Player::MAX_GRENADES; ++gi)
        bot.grenades[gi] = WeaponId::None;
    bot.weapons[3] = WeaponId::None;
    bot.weaponSlot = 1; // 默认持手枪
    bot.armor = 0;
    bot.helmet = false;

    const WeaponDef &bw = WeaponDB::Get(bot.weapons[1]);
    bot.ammo = bw.magazine;
    bot.maxAmmo = bw.magazine;
    bot.reserve = Player::ReserveCapacity(bw);
    // 各槽位弹药初始化（BOT 的副武器/近战槽）
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

void CS2DGame::RespawnBOT(Player& bot) {
    SpawnBOT(bot, bot.team);
}

// ============================================================================
// 武器
// ============================================================================

void CS2DGame::QuickSwitchWeaponFor(ActorId actorId) {
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

void CS2DGame::SwitchWeaponSlotFor(ActorId actorId, int slot, int grenadeIndex) {
    if (slot < 0 || slot > 4) return;
    Player* actorPtr = FindActor(actorId);
    if (!actorPtr) return;
    Player& actor = *actorPtr;
    const bool isLocal = (actorId == localActorId_);

    // 目标槽校验
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

    // 记录「切到投掷物之前」的槽位：最后一颗雷扔完后自动切回这里
    if (slot == 3 && actor.weaponSlot != 3) {
        actor.prevWeaponSlot = actor.weaponSlot;
    }

    // 切走前把当前武器的弹药写回它自己的槽位
    actor.SaveAmmoToSlot();

    actor.previousEquipSlot = actor.weaponSlot;
    actor.previousEquipWeapon = actor.CurrentWeapon().id;
    if (slot == 3) actor.grenadeSlot = grenadeIndex;
    actor.weaponSlot = slot;
    actor.reloading   = false;
    actor.reloadTimer = 0.0f;
    actor.holdingC4 = (slot == 4);
    // 切枪（含投掷物自动切回）→ 开镜立即归零
    if (isLocal) ResetScope(Presentation::ScopeResetReason::WeaponChanged);

    // 切枪有金属声，附近敌人能听见（半径见 Tuning::HearSwitchRadius）
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

    // 从目标槽读取它自己的弹药
    actor.LoadAmmoFromSlot();
    actor.switchSoundWeapon = actor.weapons[slot];
    actor.switchSoundPending = true;
}

std::string CS2DGame::GetWeaponName() const {
    return GetWeaponNameFor(LocalActor());
}

std::string CS2DGame::GetWeaponNameFor(const Player& p) const {
    return WeaponDB::Get(p.CurrentWeapon().id).name;
}

// ============================================================================
// 开镜（契约 v1.1 第 4 节）：玩法层唯一状态，表现层只读快照
//   - 三档循环：关 → 1.5x → 2x → 关
//   - 普通与开镜共用前方扇形视野，镜筒只负责瞄准装饰
//   - 相机向准星偏移，保证准星下的世界点在换档时不动
// ============================================================================

bool CS2DGame::IsScopeSupported(WeaponId id) {
    if (id == WeaponId::None) return false;
    // 配置入口仍是 WeaponDef::scoped（狙 / AUG / SG553）
    return WeaponDB::Get(id).scoped;
}

void CS2DGame::ResetScope(Presentation::ScopeResetReason reason) {
    (void)reason;   // 重置原因只用于诊断；重置本身固定且幂等
    const WeaponId cur = LocalActor().CurrentWeapon().id;
    scopeCloseOnRelease_ = false;
    scopeLevel_      = 0;
    scopeEngagement_ = 0.0f;
    scopeEngFrom_    = scopeEngTo_ = 0.0f;
    scopeEngT_       = 1.0f;
    scopeZoom_       = 1.0f;
    previousScopeZoom_ = 1.0f;
    previousScopeEngagement_ = 0.0f;
    scopeZoomFrom_   = scopeZoomTo_ = 1.0f;
    scopeZoomT_      = 1.0f;

    scope_.weapon       = cur;
    scope_.supported    = IsScopeSupported(cur);
    scope_.active       = false;
    scope_.level        = 0;
    scope_.transition   = 0.0f;
    scope_.zoomMultiplier       = 1.0f;
    scope_.targetZoomMultiplier = 1.0f;
}

Presentation::ScopeView CS2DGame::GetScopeView() const {
    Presentation::ScopeView view = scope_;
    const auto& local = LocalActor();
    const WeaponId cur = local.CurrentWeapon().id;
    const int level = std::clamp(scopeLevel_, 0, Presentation::kScopeLevelCount - 1);

    view.weapon    = cur;
    view.supported = IsScopeSupported(cur);
    view.level     = level;
    view.active    = (level > 0);
    view.transition = scopeEngagement_;
    view.zoomMultiplier = scopeZoom_;
    view.targetZoomMultiplier = Presentation::kScopeZoomLevels[level];

    if (!view.supported || !local.alive) {
        view.active         = false;
        view.level          = 0;
        view.transition     = 0.0f;
        view.zoomMultiplier = 1.0f;
        view.targetZoomMultiplier = 1.0f;
    }
    return view;
}

Presentation::ScopeView CS2DGame::GetRenderScopeView() const {
    auto view = GetScopeView();
    if (viewHistoryValid_ && view.supported && LocalActor().alive) {
        view.zoomMultiplier = previousScopeZoom_ + (view.zoomMultiplier - previousScopeZoom_) * renderInterpolation_;
        view.transition = previousScopeEngagement_ + (view.transition - previousScopeEngagement_) * renderInterpolation_;
    }
    return view;
}

void CS2DGame::UpdateScopeInput(bool rightClicked, bool gameplayInputAllowed) {
    const auto& local = LocalActor();
    const WeaponId cur = local.CurrentWeapon().id;

    // 输入被挡住（暂停 / 买枪 / 选边 / 失焦 / 转场）：立即归零，且不消费边沿
    if (!gameplayInputAllowed) {
        ResetScope(Presentation::ScopeResetReason::InputBlocked);
        return;
    }
    if (!local.alive) {
        ResetScope(Presentation::ScopeResetReason::PlayerDied);
        return;
    }
    // 防御：当前武器与快照不一致（切枪 / 掉落 / 自动切回）→ 归零
    if (cur != scope_.weapon) {
        ResetScope(Presentation::ScopeResetReason::WeaponChanged);
    }
    if (local.reloading) {
        if (scopeLevel_ > 0) ResetScope(Presentation::ScopeResetReason::ReloadStarted);
        return;
    }
    // 单发狙拉栓冷却期间不能开镜（AWP 1.45s / SSG 的拉栓窗口）；连发武器不受影响
    {
        const WeaponDef& def = local.CurrentWeapon();
        if (!def.automatic && local.shootCooldown > 0.0f) return;
    }
    if (!rightClicked) return;
    if (!IsScopeSupported(cur)) return;   // 不支持的武器不误触

    // 契约 v1.1：右键循环 关 → 1.5x → 2x → 关
    const int next = (scopeLevel_ + 1) % Presentation::kScopeLevelCount;
    scope_.weapon    = cur;
    scope_.supported = true;
    scopeLevel_      = next;

    scopeZoomFrom_ = scopeZoom_;
    scopeZoomTo_   = Presentation::kScopeZoomLevels[next];
    scopeZoomT_    = 0.0f;

    scopeEngFrom_  = scopeEngagement_;
    scopeEngTo_    = (next > 0) ? 1.0f : 0.0f;
    scopeEngT_     = 0.0f;
}

void CS2DGame::TickScope(float dt) {
    const auto& local = LocalActor();
    const WeaponId cur = local.CurrentWeapon().id;
    scope_.weapon    = cur;
    scope_.supported = IsScopeSupported(cur);
    if (!local.alive) {
        ResetScope(Presentation::ScopeResetReason::PlayerDied);
        return;
    }
    if (!scope_.supported) {
        // 不支持开镜的武器：状态与过渡都必须归零
        if (scopeLevel_ > 0 || scopeZoom_ > 1.0f) {
            ResetScope(Presentation::ScopeResetReason::WeaponChanged);
        }
        return;
    }

    // 修复：换弹期间不允许保持开镜（StartReload 会主动重置，这里每帧再兜一层，
    // 覆盖"空弹自动换弹"等不经过显式 StartReload 调用的路径）
    if (local.reloading) {
        if (scopeLevel_ > 0) ResetScope(Presentation::ScopeResetReason::ReloadStarted);
        return;
    }

    // 0.12s 线性过渡：倍率与镜框淡入各自推进（换档时 transition 保持 1，只有倍率变）
    const float step = (Presentation::kScopeTransitionSeconds > 0.0f)
                           ? dt / Presentation::kScopeTransitionSeconds : 1.0f;
    if (scopeZoomT_ < 1.0f) scopeZoomT_ = std::min(1.0f, scopeZoomT_ + step);
    const float zoomEase = scopeZoomT_ * scopeZoomT_ * (3.0f - 2.0f * scopeZoomT_);
    scopeZoom_ = scopeZoomFrom_ + (scopeZoomTo_ - scopeZoomFrom_) * zoomEase;

    if (scopeEngT_ < 1.0f) scopeEngT_ = std::min(1.0f, scopeEngT_ + step);
    const float engagementEase = scopeEngT_ * scopeEngT_ * (3.0f - 2.0f * scopeEngT_);
    scopeEngagement_ = scopeEngFrom_ + (scopeEngTo_ - scopeEngFrom_) * engagementEase;
}

// 相机向准星偏移的比例：1.5x → 0.35，2x → 0.5，其余按倍率线性插值
float CS2DGame::ScopeCameraShiftForZoom(float zoom) {
    const float low = Presentation::kScopeZoomLevels[1];
    const float high = Presentation::kScopeZoomLevels[2];
    const float z = std::clamp(zoom, 1.0f, high);
    if (z <= low) {
        return Tuning::CameraLookShift + (z - 1.0f) / (low - 1.0f) *
            (Tuning::ScopeCameraShiftLow - Tuning::CameraLookShift);
    }
    return Tuning::ScopeCameraShiftLow + (z - low) / (high - low) *
        (Presentation::kScopeCameraShiftMax - Tuning::ScopeCameraShiftLow);
}

// ============================================================================
// 每帧更新
// ============================================================================

void CS2DGame::Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                      bool wPressed, bool aPressed, bool sPressed, bool dPressed,
                      bool shiftPressed, bool rPressed, bool interactHeld,
                      IAudioSink* audio) {
    ActorInput input;
    input.moveUp      = wPressed;
    input.moveLeft    = aPressed;
    input.moveDown    = sPressed;
    input.moveRight   = dPressed;
    input.stealth     = shiftPressed;
    input.fire        = mouseLeftDown;
    input.reload      = rPressed;
    input.interact    = interactHeld;
    input.aimWorldX   = mouseWorldX;
    input.aimWorldY   = mouseWorldY;
    input.hasAimWorld = true;
    UpdateMultiActor(dt, {{localActorId_, input}}, audio);
}

void CS2DGame::UpdateHumanActor(Player& actor, float dt, const ActorInput& input, IAudioSink* audio) {
    const bool isLocal = (ActorIdOf(actor) == localActorId_);
    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool canAct  = !frozen;
    const bool canMove = !frozen;

    float targetWorldX = input.hasAimWorld ? input.aimWorldX : actor.aimWorldX;
    float targetWorldY = input.hasAimWorld ? input.aimWorldY : actor.aimWorldY;

    if (isLocal && input.hasAimWorld) {
        SetAimTarget(targetWorldX, targetWorldY, aimFollowsPlayer_);
        if (aimFollowsPlayer_) {
            targetWorldX = actor.x + aimOffsetX_;
            targetWorldY = actor.y + aimOffsetY_;
        }
    }
    actor.aimWorldX = targetWorldX;
    actor.aimWorldY = targetWorldY;
    actor.interactHeld = input.interact && canAct;
    actor.fireHeld     = input.fire && canAct;

    actor.stealth = input.stealth && canAct;

    if (canAct && input.reload && !actor.reloading) {
        if ((actor.weaponSlot == 0 || actor.weaponSlot == 1) &&
            actor.ammo < actor.maxAmmo && actor.reserve > 0) {
            StartReload(actor, audio);
        }
    }

    if (actor.alive && canMove) {
        UpdatePlayerMovement(actor, dt, input.moveUp, input.moveLeft, input.moveDown, input.moveRight, actor.stealth);
        if (isLocal && aimFollowsPlayer_) {
            targetWorldX = actor.x + aimOffsetX_;
            targetWorldY = actor.y + aimOffsetY_;
            actor.aimWorldX = targetWorldX;
            actor.aimWorldY = targetWorldY;
        }
        UpdatePlayerShooting(actor, dt, input.fire, targetWorldX, targetWorldY, audio);
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

    // E 键：边沿触发主动拾取（拆包时不抢键）
    if (canAct && input.interact && !actor.prevInteract && actor.alive) {
        const bool busyDefusing = (mode_ == MatchMode::BombDefusal && bombPlanted_ &&
                                   actor.defuseProgress > 0.0f);
        if (!busyDefusing) TryPickupDropsFor(ActorIdOf(actor), true, audio);
    }
    actor.prevInteract = input.interact;
}

void CS2DGame::UpdateMultiActor(float dt, const std::vector<std::pair<ActorId, ActorInput>>& inputs,
                                IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    ++traceTick_;
    deathPresentation_.remaining = std::max(0.0f, deathPresentation_.remaining - dt);
    dryFireHintTimer_ = std::max(0.0f, dryFireHintTimer_ - dt);
    for (auto& a : actors_) {
        a.dryFireCooldown = std::max(0.0f, a.dryFireCooldown - dt);
    }

    auto findInput = [&](ActorId id) -> const ActorInput* {
        for (const auto& entry : inputs) {
            if (entry.first == id) return &entry.second;
        }
        return nullptr;
    };

    // ---- 观察者输入：空格切换视角，自由视角用 WASD（来自本地玩家输入） ----
    if (IsSpectating()) {
        if (spectateTogglePending_) SpectatorToggle();
        if (specMode_ == SpectateMode::Free) {
            if (const ActorInput* localIn = findInput(localActorId_)) {
                const float fx = (localIn->moveRight ? 1.0f : 0.0f) - (localIn->moveLeft ? 1.0f : 0.0f);
                const float fy = (localIn->moveDown  ? 1.0f : 0.0f) - (localIn->moveUp   ? 1.0f : 0.0f);
                SpectatorMoveFree(fx, fy, dt);
            }
        }
    }

    spectateTogglePending_ = false;
    if (!world_) return;

    EnsureBotAi();
    previousEdgeZoom_ = edgeZoom_;
    previousScopeZoom_ = scopeZoom_;
    previousScopeEngagement_ = scopeEngagement_;
    previousActorViews_.clear();
    previousActorViews_.reserve(actors_.size());
    for (const auto& actor : actors_) {
        previousActorViews_.push_back({actor.x, actor.y, actor.alive, actor.EffectiveAim(), actor.animTimer, actor.animState});
    }
    viewHistoryValid_ = true;
    clockSec_ += dt;

    if (const ActorInput* localIn = findInput(localActorId_)) {
        if (localIn->hasAimWorld) {
            SetAimTarget(localIn->aimWorldX, localIn->aimWorldY, aimFollowsPlayer_);
        }
    }
    recoilShakeAge_ += dt;
    recoilShake_ *= std::exp(-Tuning::RecoilShakeDecay * dt);

    // 开镜过渡：每帧按 dt 逼近目标档位（0.12s 线性），与输入边沿解耦
    TickScope(dt);
    for (auto& actor : actors_) {
        if (!actor.IsHuman()) continue;
        actor.RecoverBallistics(dt);
        actor.recoilRecoveryTimer = std::max(0.0f, actor.recoilRecoveryTimer - dt);
        if (actor.recoilRecoveryTimer <= 0.0f)
            actor.aimKick *= std::exp(-actor.CurrentWeapon().recoilRecoverPerSec * dt);
    }
    const float edgeTarget = !IsSpectating() ? static_cast<float>(cameraDistanceTier_) : 0.0f;
    if (edgeTarget != edgeZoomTo_) {
        edgeZoomFrom_ = edgeZoom_;
        edgeZoomTo_ = edgeTarget;
        edgeZoomTime_ = 0.0f;
    }
    edgeZoomTime_ = std::min(1.0f, edgeZoomTime_ + dt / Tuning::EdgeZoomSeconds);
    const float edgeEase = edgeZoomTime_ * edgeZoomTime_ * (3.0f - 2.0f * edgeZoomTime_);
    edgeZoom_ = edgeZoomFrom_ + (edgeZoomTo_ - edgeZoomFrom_) * edgeEase;

    // 契约 v1.2：开枪后「松手再关镜」的收尾
    if (scopeCloseOnRelease_) {
        const ActorInput* localIn = findInput(localActorId_);
        if (!localIn || !localIn->fire) {
            ResetScope(Presentation::ScopeResetReason::ShotFired);
        }
    }

    // 切枪音效：SwitchWeaponSlotFor 只挂起，这里按刚切到的武器播放
    for (auto& actor : actors_) {
        if (!actor.switchSoundPending) continue;
        actor.switchSoundPending = false;
        if (audio && actor.switchSoundWeapon != WeaponId::None) {
            if (ActorIdOf(actor) == localActorId_)
                audio->PlayWeaponSwitch(actor.switchSoundWeapon);
            else
                audio->PlayWeaponSwitchAt(actor.switchSoundWeapon, actor.x, actor.y, ListenerX(), ListenerY());
        }
        actor.switchSoundWeapon = WeaponId::None;
    }

    UpdateRoundFlow(dt, audio);

    // 团队情报：老化 + 探点上报（冻结期也推进，保证情报会过期）
    UpdateTeamContacts(dt);
    intel_.Update(dt);

    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool canMove = !frozen;

    for (auto& actor : actors_) {
        if (!actor.IsHuman()) continue;
        const ActorInput* in = findInput(actor.id);
        ActorInput idleInput;
        idleInput.hasAimWorld = false;
        UpdateHumanActor(actor, dt, in ? *in : idleInput, audio);
    }

    UpdateBombItem(dt);

    // 不能移动的阶段（回合冻结 / 结算展示）：把移动模糊与准星扩散平滑收回到 0
    if (!canMove || !LocalActor().alive) {
        const float recover = 1.0f - std::exp(-12.0f * dt);
        aimMotionBlur_ += (0.0f - aimMotionBlur_) * recover;
        crosshairBloom_ += (0.0f - crosshairBloom_) * recover;
        if (aimMotionBlur_ < 0.002f) aimMotionBlur_ = 0.0f;
        if (crosshairBloom_ < 0.002f) crosshairBloom_ = 0.0f;
    }

    if (canMove) {
        UpdateBOTs(dt, audio);
    } else {
        for (auto& actor : actors_) {
            if (actor.IsBot()) { actor.vx = actor.vy = 0.0f; }
        }
    }

    // 全员脚步声：按实际位移累计（静步不出声）
    for (auto& actor : actors_) {
        if (!actor.IsActive()) continue;
        if (!actor.alive) {
            actor.footstepStride = 0.0f;
            actor.footstepLastValid = false;
            continue;
        }
        const bool positional = !(actor.IsHuman() && ActorIdOf(actor) == localActorId_);
        AccumulateFootstep(actor, audio, positional);
    }

    CoachUpdate(dt);
    UpdateBotBuy(dt);

    UpdateBullets(dt, audio);
    UpdateGrenades(dt, audio);
    UpdateObjective(dt, audio);
    CheckWinConditions(audio);
    UpdateSpectatorState();

    // ---- 动画状态机与装弹 ----
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

    // 掉落物：保留到回合结束，已拾取的移除
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                [](const DroppedItem& d) { return d.id == WeaponId::None && !d.defuseKit; }),
                 drops_.end());
    for (auto& d : drops_) {
        d.fireState.Recover(dt);
        if (d.pickupDelay > 0.0f) d.pickupDelay -= dt;
    }
    TryPickupDropsFor(localActorId_, false, audio);

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
    for (auto& direction : damageDirections_) direction.remaining = std::max(0.0f, direction.remaining - dt);
    if (hitMarkerTimer_ > 0.0f)   hitMarkerTimer_   = std::max(0.0f, hitMarkerTimer_ - dt);
    if (damageFlashTimer_ > 0.0f) damageFlashTimer_ = std::max(0.0f, damageFlashTimer_ - dt);
    if (lastDeathTimer_ > 0.0f)   lastDeathTimer_   = std::max(0.0f, lastDeathTimer_ - dt);
    if (announcementTimer_ > 0.0f) {
        announcementTimer_ = std::max(0.0f, announcementTimer_ - dt);
        if (announcementTimer_ <= 0.0f) announcement_.clear();
    }
}

// ============================================================================
// 脚步：按实际位移触发（真人与 BOT 共用）
// ============================================================================
void CS2DGame::ResetFootstepStride() {
    for (auto& actor : actors_) ResetFootstepStride(actor);
}

void CS2DGame::ResetFootstepStride(Player& actor) {
    actor.footstepStride = 0.0f;
    actor.footstepLastValid = false;
}

int CS2DGame::AccumulateFootstep(Player& actor, IAudioSink* audio, bool positional) {
    // 潜行：不累计、不出声（潜行的听觉半径设计保持不变）
    // 死亡/传送：重置累计器，避免重生后凭空补步
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
    // 传送 / 重生（单帧位移超过 40 个身位）：不计步，直接重新取基准
    if (moved > Tuning::PlayerSize * 40.0f) {
        actor.footstepStride = 0.0f;
        return 0;
    }
    // 静止（本帧零位移）：不累计。
    if (moved <= 0.0f) return 0;
    actor.footstepStride += moved;
    if (actor.footstepStride < Tuning::FootstepStride) return 0;

    // 单帧至多触发 N 步，余数保留
    int steps = 0;
    while (actor.footstepStride >= Tuning::FootstepStride && steps < Tuning::FootstepMaxPerFrame) {
        actor.footstepStride -= Tuning::FootstepStride;
        ++steps;
    }
    if (actor.footstepStride >= Tuning::FootstepStride) actor.footstepStride = std::fmod(actor.footstepStride, Tuning::FootstepStride);
    if (steps <= 0) return 0;

    actor.footstepCount += static_cast<std::uint64_t>(steps);
    for (int i = 0; i < steps; ++i) {
        if (audio) {
            if (positional)
                audio->PlaySoundAt(SoundEffect::FOOTSTEP, actor.x, actor.y,
                                   ListenerX(), ListenerY());
            else
                audio->PlaySound(SoundEffect::FOOTSTEP);
        }
        OnFootstepSound(actor, actor.x, actor.y);
    }
    return steps;
}

// ============================================================================
// 地图视图辅助
// ============================================================================

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

void CS2DGame::SetAimTarget(float x, float y, bool followsPlayer) {
    auto& local = LocalActor();
    local.aimWorldX = x;
    local.aimWorldY = y;
    aimFollowsPlayer_ = followsPlayer;
    aimOffsetX_ = x - local.x;
    aimOffsetY_ = y - local.y;
    if (local.alive) {
        local.aimAngle = std::atan2(y - local.y, x - local.x);
        local.aimDistance = std::max(std::hypot(x - local.x, y - local.y), 0.001f);
    }
}

CS2DGame::ActorView CS2DGame::GetActorView(const Player& actor) const {
    const ActorView current{actor.x, actor.y, actor.alive, actor.EffectiveAim(), actor.animTimer, actor.animState};
    if (!viewHistoryValid_) return current;
    const ActorView* previous = nullptr;
    const ActorId id = ActorIdOf(actor);
    if (id >= 0 && static_cast<size_t>(id) < previousActorViews_.size()) {
        previous = &previousActorViews_[static_cast<size_t>(id)];
    }
    // Spawn, death and teleports snap immediately instead of sweeping through the map.
    if (!previous || previous->alive != current.alive ||
        std::hypot(current.x - previous->x, current.y - previous->y) > actor.size * 4.0f) return current;
    return {previous->x + (current.x - previous->x) * renderInterpolation_,
            previous->y + (current.y - previous->y) * renderInterpolation_, current.alive,
            previous->aimAngle + std::remainder(current.aimAngle - previous->aimAngle, Tuning::Pi * 2.0f) * renderInterpolation_,
            previous->animState == current.animState
                ? previous->animTimer + (current.animTimer - previous->animTimer) * renderInterpolation_ : current.animTimer,
            current.animState};
}

float CS2DGame::GetRenderAimAngle() const {
    const auto view = GetActorView(LocalActor());
    return std::atan2(GetRenderAimY() - view.y, GetRenderAimX() - view.x);
}
float CS2DGame::GetRenderAimX() const {
    const auto& local = LocalActor();
    return aimFollowsPlayer_ ? GetActorView(local).x + std::cos(local.aimAngle) * local.aimDistance + local.RecoilOffsetX()
                             : local.aimWorldX + local.RecoilOffsetX();
}
float CS2DGame::GetRenderAimY() const {
    const auto& local = LocalActor();
    return aimFollowsPlayer_ ? GetActorView(local).y + std::sin(local.aimAngle) * local.aimDistance + local.RecoilOffsetY()
                             : local.aimWorldY + local.RecoilOffsetY();
}

CS2DGame::RefundQuote CS2DGame::EvaluateRefundFor(ActorId actorId, const Presentation::BuyRequest &request) const {
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
        for (const auto *spawn : world_->SpawnsForTeam(actor.team == Team::CT))
            inZone = inZone || std::hypot(actor.x - spawn->x, actor.y - spawn->y) <= 80;
    }
    if (!inZone) {
        quote.reason = "返回本方出生点退款";
        return quote;
    }
    auto consider = [&](const Player::EquipmentRecord &record) {
        if (record.id && record.epoch == purchaseEpoch_ && record.buyer == actorId && !record.used) {
            ++quote.quantity;
            quote.amount = record.paid;
        }
    };
    if (request.kind == Presentation::BuyKind::DefuseKit && actor.hasDefuseKit)
        consider(actor.kitEquipment);
    if (request.kind == Presentation::BuyKind::Armor && !actor.armorEquipment.empty()) {
        const auto &record = actor.armorEquipment.back();
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

bool CS2DGame::ExecuteRefundFor(ActorId actorId, const Presentation::BuyRequest &request) {
    const auto quote = EvaluateRefundFor(actorId, request);
    if (quote.amount <= 0)
        return false;
    Player* actorPtr = FindActor(actorId);
    if (!actorPtr) return false;
    Player& actor = *actorPtr;
    const bool isLocal = (actorId == localActorId_);
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
                const auto &record = actor.grenadeEquipment[i];
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
        } else
            for (int slot = 0; slot < 3; ++slot)
                if (actor.weapons[slot] == request.weapon) {
                    actor.ClearSlotWeapon(slot);
                    actor.equipment[slot] = {};
                    actor.fireStates[slot] = {};
                }
        if (actor.weapons[actor.weaponSlot] == WeaponId::None)
            EquipFallbackWeaponFor(actor);
        actor.previousEquipSlot = 2;
        actor.previousEquipWeapon = WeaponId::Knife;
        actor.reloading = false;
        actor.reloadTimer = 0;
        if (isLocal) ResetScope(Presentation::ScopeResetReason::WeaponChanged);
    }
    actor.money += quote.amount;
    return true;
}
