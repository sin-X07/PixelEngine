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
        player_.hasDefuseKit = false;
    bots_.clear();
        botAi_.clear();
        bullets_.clear();
        bulletVisuals_.clear();
        nextBulletVisualId_ = 1;
        ResetScope(Presentation::ScopeResetReason::MatchExit);
        player_ = Player{};
        player_.alive = false;
    }
    intel_.Build(world_.get(), clockSec_);
    return true;
}

// 开始一场全新比赛（清空比分与统计）
void CS2DGame::ResetToNewMatch() {
    StopObjectiveAudio(nullptr);
    damageDirections_ = {};
    damageFlashTimer_ = hitMarkerTimer_ = 0.0f;
    dryFireHintTimer_ = dryFireCooldown_ = 0.0f;
    hitMarkerKind_ = HitMarkerKind::Normal;
    bots_.clear();
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
    playerKills_ = 0;
    playerDeaths_ = 0;
    matchOverAnnounced_ = false;
    matchOverTimer_ = 0.0f;
    killFeed_.clear();
    deathPresentation_ = {};
    ResetFootstepStride();
    contactsT_.clear();
    contactsCT_.clear();

    teams_.Reset();
    teams_.SetScoreToWin(8);
    round_.Reset(RoundConfig{});
    economy_.Reset();
    prevPhase_ = round_.Phase();

    player_.team = desiredPlayerTeam_;
    player_.kills = 0;
    player_.teamKills = 0;
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
        botAi_.emplace_back();
        botAi_.back().difficulty = tactics_.botProfile / 2;          // 与 bots_ 同下标
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
    dmLoadout_ = DmLoadout{};   // 新对局不沿用上一局的死斗装备
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

int CS2DGame::PurchaseQuantityOf(const Presentation::BuyRequest& request) const {
    if (request.kind == Presentation::BuyKind::DefuseKit) return player_.hasDefuseKit ? 1 : 0;
    if (request.kind == Presentation::BuyKind::Armor) {
        // tier1 = 满甲即已拥有；tier2 = 满甲且有头盔才算已拥有
        if (player_.armor >= 100 &&
            (request.armorTier == 1 || player_.helmet)) {
            return 1;
        }
        return 0;
    }
    const WeaponDef& def = WeaponDB::Get(request.weapon);
    switch (def.slot) {
        case WeaponSlotKind::Primary:   return (player_.weapons[0] == request.weapon) ? 1 : 0;
        case WeaponSlotKind::Secondary: return (player_.weapons[1] == request.weapon) ? 1 : 0;
        case WeaponSlotKind::Melee:     return (player_.weapons[2] == request.weapon) ? 1 : 0;
        case WeaponSlotKind::Grenade:   return player_.CountGrenade(request.weapon);
    }
    return 0;
}

Presentation::BuyCode CS2DGame::EvaluatePurchase(
    const Presentation::BuyRequest& request) const {
    using Presentation::BuyCode;

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

    if (request.kind == Presentation::BuyKind::DefuseKit && player_.team != Team::CT)
        return BuyCode::InvalidItem;

    // 2) 存活 → 时窗 → 区域 → 阵营 → 本场配装（固定优先序，保证失败原因稳定可测）
    if (!player_.alive) return BuyCode::PlayerDead;
    if (!IsBuyWindowOpen()) return BuyCode::WindowClosed;
    if (!IsPlayerInBuyZone()) return BuyCode::OutsideBuyZone;
    if (request.kind == Presentation::BuyKind::Weapon &&
        !WeaponDB::TeamAllowed(request.weapon, player_.team == Team::T)) {
        return BuyCode::TeamRestricted;
    }
    if (request.kind == Presentation::BuyKind::Weapon &&
        WeaponDB::RequiresLoadoutSelection(request.weapon) &&
        !WeaponDB::IsWeaponInLoadout(LoadoutForTeam(player_.team), request.weapon)) {
        return BuyCode::NotInLoadout;
    }


    // 3) 已拥有：同一把主/副武器不再重复扣款补弹；
    //    投掷物可叠加，容量由下一步的 InventoryFull / ItemLimitReached 判定
    {
        const bool isGrenade =
            request.kind == Presentation::BuyKind::Weapon &&
            WeaponDB::Get(request.weapon).slot == WeaponSlotKind::Grenade;
        if (!isGrenade && PurchaseQuantityOf(request) > 0) {
            return BuyCode::AlreadyEquipped;
        }
    }

    // 4) 投掷物容量：总数 → 同类数量
    if (request.kind == Presentation::BuyKind::Weapon) {
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        if (def.slot == WeaponSlotKind::Grenade) {
            if (player_.grenadeCount >= Player::MAX_GRENADES) return BuyCode::InventoryFull;
            const int limit = (request.weapon == WeaponId::GrenadeFlash) ? 2 : 1;
            if (player_.CountGrenade(request.weapon) >= limit) {
                return BuyCode::ItemLimitReached;
            }
        }
    }

    // 5) 金额
    const int price = PurchasePriceOf(request);
    if (price <= 0) return BuyCode::InvalidItem;
    if (player_.money < price) return BuyCode::InsufficientFunds;

    return BuyCode::Ok;
}

Presentation::BuyResult CS2DGame::ExecutePurchase(
    const Presentation::BuyRequest& request, IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    Presentation::BuyResult result;
    result.request = request;
    result.code = EvaluatePurchase(request);      // 点击时重新判定
    result.moneyAfter = player_.money;
    result.quantityAfter = PurchaseQuantityOf(request);

    if (result.code != Presentation::BuyCode::Ok) {
        return result;                            // 失败：金额 / 装备 / 音效都不动
    }

    Player::EquipmentRecord receipt;
    receipt.id = nextEquipmentId_++;
    receipt.epoch = purchaseEpoch_;
    receipt.buyer = 0;
    receipt.paid = PurchasePriceOf(request);
    receipt.armorTier=request.kind==Presentation::BuyKind::Armor ? request.armorTier : 0;
    receipt.previousArmor = player_.armor;
    receipt.previousHelmet = player_.helmet;
    if (request.kind == Presentation::BuyKind::DefuseKit) {
        player_.kitEquipment = receipt;
        player_.money -= 400;
        player_.hasDefuseKit = true;
    } else if (request.kind == Presentation::BuyKind::Armor) {
        player_.armorEquipment.push_back(receipt);
        const int tier  = request.armorTier;
        const int price = PurchasePriceOf(request);
        player_.money -= price;
        economy_.Clamp(player_.money);
        player_.armor = 100;
        if (tier == 2) player_.helmet = true;
        std::cout << "[Buy] " << (tier == 2 ? "护甲+头盔" : "护甲")
                  << " ($" << price << ") 余额 $" << player_.money << std::endl;
    } else {
        const int oldSlot = player_.weaponSlot;
        const WeaponId oldWeapon = player_.CurrentWeapon().id;
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        switch (def.slot) {
            case WeaponSlotKind::Primary:
                DropWeaponToGround(0,player_.x,player_.y);
                player_.SetSlotWeapon(0, request.weapon);
                player_.equipment[0] = receipt;
                player_.fireStates[0] = {};
                player_.weaponSlot = 0;
                player_.LoadAmmoFromSlot();
                player_.reloading = false;
                player_.reloadTimer = 0.0f;
                playerHoldingC4_ = false;
                break;
            case WeaponSlotKind::Secondary:
                DropWeaponToGround(1,player_.x,player_.y);
                player_.SetSlotWeapon(1, request.weapon);
                player_.equipment[1] = receipt;
                player_.fireStates[1] = {};
                player_.weaponSlot = 1;
                player_.LoadAmmoFromSlot();
                player_.reloading = false;
                player_.reloadTimer = 0.0f;
                playerHoldingC4_ = false;
                break;
            case WeaponSlotKind::Melee:
                player_.weapons[2] = request.weapon;
                break;
            case WeaponSlotKind::Grenade:
                player_.grenadeEquipment[player_.grenadeCount] = receipt;
                player_.grenades[player_.grenadeCount++] = request.weapon;
                player_.SyncActiveGrenade();
                break;
        }
        if (def.slot == WeaponSlotKind::Primary || def.slot == WeaponSlotKind::Secondary) {
            previousEquipSlot_ = oldSlot;
            previousEquipWeapon_ = oldWeapon;
            switchSoundWeapon_ = request.weapon;
            switchSoundPending_ = true;
        }
        player_.money -= def.price;
        economy_.Clamp(player_.money);
        std::cout << "[Buy] " << def.name << " ($" << def.price
                  << ") 余额 $" << player_.money << std::endl;
        // 购买替换了当前手持武器 → 开镜必须归零
        if (def.slot == WeaponSlotKind::Primary || def.slot == WeaponSlotKind::Secondary) {
            ResetScope(Presentation::ScopeResetReason::WeaponChanged);
            // 死斗：记住这次买到的装备，下次重生自动装配（仍可继续购买）
            if (mode_ == MatchMode::DeathMatch) {
                dmLoadout_.primary   = player_.weapons[0];
                dmLoadout_.secondary = player_.weapons[1];
                dmLoadout_.valid     = true;
            }
        }
    }

    if (audio) audio->PlaySound(SoundEffect::EQUIP);
    result.moneyAfter = player_.money;
    result.quantityAfter = PurchaseQuantityOf(request);
    return result;
}

// 兼容包装：旧调用点保持原签名，成功只执行一次，不递归、不重复扣款。
bool CS2DGame::TryBuy(WeaponId id, IAudioSink* audio) {
    Presentation::BuyRequest r;
    r.kind   = Presentation::BuyKind::Weapon;
    r.weapon = id;
    return ExecutePurchase(r, audio).code == Presentation::BuyCode::Ok;
}

// 购买护甲：tier 1=护甲($650) 2=护甲+头盔($1000)
bool CS2DGame::TryBuyArmor(int tier, IAudioSink* audio) {
    Presentation::BuyRequest r;
    r.kind      = Presentation::BuyKind::Armor;
    r.weapon    = WeaponId::None;
    r.armorTier = tier;
    return ExecutePurchase(r, audio).code == Presentation::BuyCode::Ok;
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
    if (mode_ == MatchMode::DeathMatch)
        ++purchaseEpoch_;
    ++playerSpawnVersion_;
    player_.aimKick = 0.0f;
    player_.fireStates = {};
    player_.equipment = {};
    player_.grenadeEquipment = {};
    player_.armorEquipment.clear();
    player_.kitEquipment = {};
    player_.hasDefuseKit = false;
    player_.recoilRecoveryTimer = 0.0f;
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
    ResetFootstepStride();   // 重生：清零脚步累计器，避免凭空补步

    player_.alive = true;
    player_.health = 100;
    player_.maxHealth = 100;
    player_.vx = player_.vy = 0.0f;
    crosshairBloom_ = 0.0f;
    aimMotionBlur_ = 0;
    player_.shootCooldown = 0.0f;
    player_.team = desiredPlayerTeam_;
    if (mode_ == MatchMode::DeathMatch)
        player_.money = 16000;
    player_.animState = AnimState::Idle;
    player_.animTimer = 0.0f;
    player_.deathTimer = 0.0f;
    player_.shootAnimTimer = 0.0f;
    player_.spriteKey = (desiredPlayerTeam_ == Team::CT) ? 0 : 1;
    // 开局只有起始手枪（按本场阵营配装的起始槽位）：
    const WeaponId startingPistol = LoadoutForTeam(desiredPlayerTeam_).starting;
    player_.weapons[0] = WeaponId::None;
    player_.weapons[1] = startingPistol;
    player_.weapons[2] = WeaponId::Knife;
    player_.weapons[3] = WeaponId::None;
    player_.weapons[4] = WeaponId::None;
    player_.grenadeCount = 0;
    player_.grenadeSlot = 0;
    for (int gi = 0; gi < Player::MAX_GRENADES; ++gi)
        player_.grenades[gi] = WeaponId::None;
    player_.weaponSlot = 1; // 默认持手枪
    previousEquipSlot_ = 2;
    previousEquipWeapon_ = WeaponId::Knife;
    player_.armor = 0;
    player_.helmet = false;

    const WeaponDef &w = WeaponDB::Get(player_.weapons[1]);
    player_.ammo = w.magazine;
    player_.maxAmmo = w.magazine;
    player_.reserve = Player::ReserveCapacity(w);
    // 各槽位弹药同步（避免旧局残留）
    for (int sl = 0; sl < 5; ++sl) {
        if (player_.weapons[sl] == WeaponId::None)
            player_.ClearSlotWeapon(sl);
        else
            player_.SetSlotWeapon(sl, player_.weapons[sl]);
    }
    player_.SaveAmmoToSlot();

    // 死斗：重生自动装配「上一次购买的武器」（按本场阵营与配装校验，失效则回退起始手枪）
    if (mode_ == MatchMode::DeathMatch && dmLoadout_.valid) {
        const auto& teamLoadout = LoadoutForTeam(player_.team);
        const bool isT = (player_.team == Team::T);
        auto validDmWeapon = [&](WeaponId wid) {
            if (wid == WeaponId::None) return false;
            if (!WeaponDB::TeamAllowed(wid, isT)) return false;
            return !WeaponDB::RequiresLoadoutSelection(wid) ||
                   WeaponDB::IsWeaponInLoadout(teamLoadout, wid);
        };
        if (validDmWeapon(dmLoadout_.primary)) {
            player_.SetSlotWeapon(0, dmLoadout_.primary);
        }
        if (validDmWeapon(dmLoadout_.secondary)) {
            player_.SetSlotWeapon(1, dmLoadout_.secondary);
        } else {
            player_.SetSlotWeapon(1, startingPistol);
        }
        player_.weaponSlot = (player_.weapons[0] != WeaponId::None) ? 0 : 1;
        player_.LoadAmmoFromSlot();
    }

    player_.lastGun = player_.CurrentWeapon().id;
    for (int slot = 0; slot < 5; ++slot)
        if (player_.weapons[slot] != WeaponId::None) {
            player_.equipment[slot].id = nextEquipmentId_++;
            player_.equipment[slot].epoch = purchaseEpoch_;
        }
    player_.reloading = false;
    player_.reloadTimer = 0.0f;
    player_.stealth = false;

    for (int attempt = 0; attempt < 20; ++attempt) {
        PickSpawnForTeam(player_.team, player_.x, player_.y);
        if (!IsEnemyNearby(player_.x, player_.y, 120.0f, Team::CT))
            break;
    }
    player_.aimDistance = Tuning::InitialAimDistance;
    aimWorldX_ = player_.x + std::cos(player_.aimAngle) * player_.aimDistance;
    aimWorldY_ = player_.y + std::sin(player_.aimAngle) * player_.aimDistance;
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

void CS2DGame::QuickSwitchWeapon() {
    int slot = previousEquipSlot_;
    const WeaponId weapon = previousEquipWeapon_;
    if (slot == 3) {
        int grenade = -1;
        for (int i = 0; i < player_.grenadeCount; ++i) if (player_.grenades[i] == weapon) grenade = i;
        if (grenade < 0) slot = 2;
        else {
            SwitchWeaponSlot(3, grenade);
            return;
        }
    } else if (slot == 4) {
        if (!PlayerHasC4()) slot = 2;
    } else if (slot < 0 || slot > 2 || player_.weapons[slot] != weapon) slot = 2;
    SwitchWeaponSlot(slot);
}

void CS2DGame::SwitchWeaponSlot(int slot, int grenadeIndex) {
    if (slot < 0 || slot > 4) return;

    // 目标槽校验
    if (slot == 4) {
        if (!PlayerHasC4()) return;
        player_.weapons[4] = WeaponId::C4;
        player_.equipment[4]={};
        player_.equipment[4].id=c4EquipmentId_;
        player_.equipment[4].epoch=purchaseEpoch_;
    } else if (slot == 3) {
        if (player_.grenadeCount <= 0) return;
        if (grenadeIndex < 0) grenadeIndex = slot == player_.weaponSlot ?
            (player_.grenadeSlot + 1) % player_.grenadeCount : player_.grenadeSlot;
        if (grenadeIndex >= player_.grenadeCount) return;
    } else if (player_.weapons[slot] == WeaponId::None) {
        return;
    }
    if (slot == player_.weaponSlot && (slot != 3 || grenadeIndex == player_.grenadeSlot)) return;

    // 记录「切到投掷物之前」的槽位：最后一颗雷扔完后自动切回这里
    if (slot == 3 && player_.weaponSlot != 3) {
        player_.prevWeaponSlot = player_.weaponSlot;
    }

    // 切走前把当前武器的弹药写回它自己的槽位
    player_.SaveAmmoToSlot();

    previousEquipSlot_ = player_.weaponSlot;
    previousEquipWeapon_ = player_.CurrentWeapon().id;
    if (slot == 3) player_.grenadeSlot = grenadeIndex;
    player_.weaponSlot = slot;
    player_.reloading   = false;
    player_.reloadTimer = 0.0f;
    playerHoldingC4_ = (slot == 4);
    // 切枪（含投掷物自动切回）→ 开镜立即归零
    ResetScope(Presentation::ScopeResetReason::WeaponChanged);

    // 切枪有金属声，附近敌人能听见（半径见 Tuning::HearSwitchRadius）
    EmitSound(SoundEventType::WeaponSwitch, player_.x, player_.y, player_.team, -1);

    if (slot == 4) {
        player_.ammo = 1;
        player_.maxAmmo = 1;
        player_.reserve = 0;
        switchSoundWeapon_ = WeaponId::C4;
        switchSoundPending_ = true;
        return;
    }
    if (slot == 3) {
        player_.SyncActiveGrenade();
        player_.ammo = 1;
        player_.maxAmmo = 1;
        player_.reserve = 0;
        switchSoundWeapon_ = player_.ActiveGrenade();
        switchSoundPending_ = true;
        return;
    }

    // 从目标槽读取它自己的弹药
    player_.LoadAmmoFromSlot();
    switchSoundWeapon_ = player_.weapons[slot];
    switchSoundPending_ = true;
}

std::string CS2DGame::GetWeaponName() const {
    return GetWeaponNameFor(player_);
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
    const WeaponId cur = player_.CurrentWeapon().id;
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
    const WeaponId cur = player_.CurrentWeapon().id;
    const int level = std::clamp(scopeLevel_, 0, Presentation::kScopeLevelCount - 1);

    view.weapon    = cur;
    view.supported = IsScopeSupported(cur);
    view.level     = level;
    view.active    = (level > 0);
    view.transition = scopeEngagement_;
    view.zoomMultiplier = scopeZoom_;
    view.targetZoomMultiplier = Presentation::kScopeZoomLevels[level];

    if (!view.supported || !player_.alive) {
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
    if (viewHistoryValid_ && view.supported && player_.alive) {
        view.zoomMultiplier = previousScopeZoom_ + (view.zoomMultiplier - previousScopeZoom_) * renderInterpolation_;
        view.transition = previousScopeEngagement_ + (view.transition - previousScopeEngagement_) * renderInterpolation_;
    }
    return view;
}

void CS2DGame::UpdateScopeInput(bool rightClicked, bool gameplayInputAllowed) {
    const WeaponId cur = player_.CurrentWeapon().id;

    // 输入被挡住（暂停 / 买枪 / 选边 / 失焦 / 转场）：立即归零，且不消费边沿
    if (!gameplayInputAllowed) {
        ResetScope(Presentation::ScopeResetReason::InputBlocked);
        return;
    }
    if (!player_.alive) {
        ResetScope(Presentation::ScopeResetReason::PlayerDied);
        return;
    }
    // 防御：当前武器与快照不一致（切枪 / 掉落 / 自动切回）→ 归零
    if (cur != scope_.weapon) {
        ResetScope(Presentation::ScopeResetReason::WeaponChanged);
    }
    if (player_.reloading) {
        if (scopeLevel_ > 0) ResetScope(Presentation::ScopeResetReason::ReloadStarted);
        return;
    }
    // 单发狙拉栓冷却期间不能开镜（AWP 1.45s / SSG 的拉栓窗口）；连发武器不受影响
    {
        const WeaponDef& def = player_.CurrentWeapon();
        if (!def.automatic && player_.shootCooldown > 0.0f) return;
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
    const WeaponId cur = player_.CurrentWeapon().id;
    scope_.weapon    = cur;
    scope_.supported = IsScopeSupported(cur);
    if (!player_.alive) {
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
    if (player_.reloading) {
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
    if (audio) audioSink_ = audio;
    ++traceTick_;
    deathPresentation_.remaining = std::max(0.0f, deathPresentation_.remaining - dt);
    dryFireHintTimer_ = std::max(0.0f, dryFireHintTimer_ - dt);
    dryFireCooldown_ = std::max(0.0f, dryFireCooldown_ - dt);
    // ---- 观察者输入：空格切换视角，左右键切换队友，自由视角用 WASD ----
    if (IsSpectating()) {
        if (spectateTogglePending_) SpectatorToggle();

        if (specMode_ == SpectateMode::Free) {
            const float fx = (dPressed ? 1.0f : 0.0f) - (aPressed ? 1.0f : 0.0f);
            const float fy = (sPressed ? 1.0f : 0.0f) - (wPressed ? 1.0f : 0.0f);
            SpectatorMoveFree(fx, fy, dt);
        }
    }

    spectateTogglePending_ = false;
    if (!world_) return;

    previousEdgeZoom_ = edgeZoom_;
    previousScopeZoom_ = scopeZoom_;
    previousScopeEngagement_ = scopeEngagement_;
    previousPlayerView_ = {player_.x, player_.y, player_.alive, player_.EffectiveAim(), player_.animTimer, player_.animState};
    previousBotViews_.clear();
    previousBotViews_.reserve(bots_.size());
    for (const auto& bot : bots_)
        previousBotViews_.push_back({bot.x, bot.y, bot.alive, bot.EffectiveAim(), bot.animTimer, bot.animState});
    viewHistoryValid_ = true;
    clockSec_ += dt;

    // 准星世界坐标：开镜的圆形可见区与相机偏移都以它为中心
    SetAimTarget(mouseWorldX, mouseWorldY, aimFollowsPlayer_);
    recoilShakeAge_ += dt;
    recoilShake_ *= std::exp(-Tuning::RecoilShakeDecay * dt);

    // 开镜过渡：每帧按 dt 逼近目标档位（0.12s 线性），与输入边沿解耦
    TickScope(dt);
    player_.RecoverBallistics(dt);
    player_.recoilRecoveryTimer = std::max(0.0f, player_.recoilRecoveryTimer - dt);
    if (player_.recoilRecoveryTimer <= 0.0f)
        player_.aimKick *= std::exp(-player_.CurrentWeapon().recoilRecoverPerSec * dt);
    const float edgeTarget = !IsSpectating() ? static_cast<float>(cameraDistanceTier_) : 0.0f;
    if (edgeTarget != edgeZoomTo_) {
        edgeZoomFrom_ = edgeZoom_;
        edgeZoomTo_ = edgeTarget;
        edgeZoomTime_ = 0.0f;
    }
    edgeZoomTime_ = std::min(1.0f, edgeZoomTime_ + dt / Tuning::EdgeZoomSeconds);
    const float edgeEase = edgeZoomTime_ * edgeZoomTime_ * (3.0f - 2.0f * edgeZoomTime_);
    edgeZoom_ = edgeZoomFrom_ + (edgeZoomTo_ - edgeZoomFrom_) * edgeEase;


    // 契约 v1.2：开枪后「松手再关镜」的收尾（延迟关镜设置开启时才可能置位）
    if (scopeCloseOnRelease_ && !mouseLeftDown) {
        ResetScope(Presentation::ScopeResetReason::ShotFired);
    }

    // 切枪音效：SwitchWeaponSlot 只挂起，这里按刚切到的武器播放
    if (switchSoundPending_) {
        switchSoundPending_ = false;
        if (audio) audio->PlayWeaponSwitch(switchSoundWeapon_);
        switchSoundWeapon_ = WeaponId::None;
    }

    UpdateRoundFlow(dt, audio);

    // 团队情报：老化 + 探点上报（冻结期也推进，保证情报会过期）
    UpdateTeamContacts(dt);
    intel_.Update(dt);

    const bool frozen  = (mode_ != MatchMode::DeathMatch) && round_.IsFrozen();
    const bool canAct  = !frozen;
    const bool canMove = !frozen;   // 已决展示窗口沿用交战行为

    player_.stealth = shiftPressed && canAct;

    if (canAct && rPressed && !player_.reloading) {
        if ((player_.weaponSlot == 0 || player_.weaponSlot == 1) &&
            player_.ammo < player_.maxAmmo && player_.reserve > 0) {
            StartReload(player_, audio);
        }
    }

    if (player_.alive && canMove) {
        UpdatePlayerMovement(dt, wPressed, aPressed, sPressed, dPressed, player_.stealth);
        if (aimFollowsPlayer_) {
            mouseWorldX = player_.x + aimOffsetX_;
            mouseWorldY = player_.y + aimOffsetY_;
            aimWorldX_ = mouseWorldX;
            aimWorldY_ = mouseWorldY;
        }
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

    // 不能移动的阶段（回合冻结 / 结算展示）：把移动模糊与准星扩散平滑收回到 0，
    // 否则准备阶段会沿用上一回合跑动残留的模糊，瞄准线看起来发虚。
    if (!canMove || !player_.alive) {
        const float recover = 1.0f - std::exp(-12.0f * dt);
        aimMotionBlur_ += (0.0f - aimMotionBlur_) * recover;
        crosshairBloom_ += (0.0f - crosshairBloom_) * recover;
        if (aimMotionBlur_ < 0.002f) aimMotionBlur_ = 0.0f;
        if (crosshairBloom_ < 0.002f) crosshairBloom_ = 0.0f;
    }


    if (canMove) {
        UpdateBOTs(dt, audio);

    } else {        // 暂停 / 结算冻结：BOT 原地待命
        for (auto& bot : bots_) { bot.vx = bot.vy = 0.0f; }
    }

    // BOT 脚步声：与玩家同一套「按实际位移累计」（静步的 BOT 不累计、不出声）
    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& b = bots_[i];
        auto& bai = AiAt(i);
        if (!b.alive) {
            bai.footstepStride = 0.0f;
            bai.footstepLastValid = false;
            continue;
        }
        AccumulateFootstep(b, bai.footstepStride, bai.footstepLastX, bai.footstepLastY,
                           bai.footstepLastValid, audio, true);
    }

    // E 键：边沿触发主动拾取（拆包/救人时不抢键）
    if (interactHeld && !prevInteract_ && player_.alive) {
        const bool busyDefusing = (mode_ == MatchMode::BombDefusal && bombPlanted_ &&
                                   player_.defuseProgress > 0.0f);
        if (!busyDefusing) TryPickupDrops(true, audio);
    }
    prevInteract_ = interactHeld;

    CoachUpdate(dt);
    UpdateBotBuy(dt);

    UpdateBullets(dt, audio);
    UpdateGrenades(dt, audio);
    UpdateObjective(dt, interactHeld, audio);
    CheckWinConditions(audio);
    UpdateSpectatorState();

    // ---- 动画状态机 ----
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
                                [](const DroppedItem& d) { return d.id == WeaponId::None && !d.defuseKit; }),
                 drops_.end());
    for (auto& d : drops_) {
        d.fireState.Recover(dt);
        if (d.pickupDelay > 0.0f) d.pickupDelay -= dt;
    }
    TryPickupDrops(false, audio);

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

    // 玩家脚步声：与 BOT 同一套「按实际位移累计」，潜行/静止/卡墙/死亡时不累计
    AccumulateFootstep(player_, footstepStride_, footstepLastX_, footstepLastY_,
                       footstepLastValid_, audio, false);
}

// ============================================================================
// 脚步：按实际位移触发（玩家与 BOT 共用）
//
// 为什么用位移而不是计时：SDL_mixer 没有变速循环接口，固定录音无法跟随移速；
// 而位移天然跟随持械倍率、开镜减速、潜行与卡墙——走满一个步幅就是一步。
// 累计器保留余数（stride -= 步幅），长期步数与距离严格对应。
// ============================================================================
void CS2DGame::ResetFootstepStride() {
    footstepStride_ = 0.0f;
    footstepLastValid_ = false;
}

int CS2DGame::AccumulateFootstep(Player& actor, float& stride, float& lastX, float& lastY,
                                bool& lastValid, IAudioSink* audio, bool positional) {
    // 潜行：不累计、不出声（潜行的听觉半径设计保持不变）
    // 死亡/传送：重置累计器，避免重生后凭空补步
    if (!actor.alive || actor.stealth) {
        stride = 0.0f;
        lastValid = false;
        return 0;
    }
    if (!lastValid) {
        lastX = actor.x;
        lastY = actor.y;
        lastValid = true;
        return 0;
    }
    const float dx = actor.x - lastX, dy = actor.y - lastY;
    lastX = actor.x;
    lastY = actor.y;
    const float moved = std::sqrt(dx * dx + dy * dy);
    // 传送 / 重生（单帧位移超过 40 个身位）：不计步，直接重新取基准
    if (moved > Tuning::PlayerSize * 40.0f) {
        stride = 0.0f;
        return 0;
    }
    // 静止（本帧零位移）：不累计。用位移而不是输入速度——卡墙有输入无位移时也不出声。
    if (moved <= 0.0f) return 0;
    stride += moved;
    if (stride < Tuning::FootstepStride) return 0;

    // 单帧至多触发 N 步，余数保留：卡顿后不会一次连响
    int steps = 0;
    while (stride >= Tuning::FootstepStride && steps < Tuning::FootstepMaxPerFrame) {
        stride -= Tuning::FootstepStride;
        ++steps;
    }
    if (stride >= Tuning::FootstepStride) stride = std::fmod(stride, Tuning::FootstepStride);
    if (steps <= 0) return 0;

    // 音步与听觉事件严格同频：一声脚步 = 一条听觉事件
    if (&actor == &player_) footstepCount_ += static_cast<std::uint64_t>(steps);
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
    aimWorldX_ = x;
    aimWorldY_ = y;
    aimFollowsPlayer_ = followsPlayer;
    aimOffsetX_ = x - player_.x;
    aimOffsetY_ = y - player_.y;
    if (player_.alive) {
        player_.aimAngle = std::atan2(y - player_.y, x - player_.x);
        player_.aimDistance = std::max(std::hypot(x - player_.x, y - player_.y), 0.001f);
    }
}

CS2DGame::ActorView CS2DGame::GetActorView(const Player& actor) const {
    const ActorView current{actor.x, actor.y, actor.alive, actor.EffectiveAim(), actor.animTimer, actor.animState};
    if (!viewHistoryValid_) return current;
    const ActorView* previous = nullptr;
    if (&actor == &player_) previous = &previousPlayerView_;
    else {
        for (size_t i = 0; i < bots_.size() && i < previousBotViews_.size(); ++i) {
            if (&actor == &bots_[i]) { previous = &previousBotViews_[i]; break; }
        }
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
    const auto view = GetActorView(player_);
    return std::atan2(GetRenderAimY() - view.y, GetRenderAimX() - view.x);
}
float CS2DGame::GetRenderAimX() const {
    return aimFollowsPlayer_ ? GetActorView(player_).x + std::cos(player_.aimAngle) * player_.aimDistance + player_.RecoilOffsetX()
                             : aimWorldX_ + player_.RecoilOffsetX();
}
float CS2DGame::GetRenderAimY() const {
    return aimFollowsPlayer_ ? GetActorView(player_).y + std::sin(player_.aimAngle) * player_.aimDistance + player_.RecoilOffsetY()
                             : aimWorldY_ + player_.RecoilOffsetY();
}

CS2DGame::RefundQuote CS2DGame::EvaluateRefund(const Presentation::BuyRequest &request) const {
    RefundQuote quote;
    if (!player_.alive) {
        quote.reason = "阵亡后无法退款";
        return quote;
    }
    if (!IsBuyWindowOpen()) {
        quote.reason = "购买时限已结束";
        return quote;
    }
    bool inZone = IsPlayerInBuyZone();
    if (world_ && world_->GetBuyZones().empty()) {
        inZone = false;
        for (const auto *spawn : world_->SpawnsForTeam(player_.team == Team::CT))
            inZone = inZone || std::hypot(player_.x - spawn->x, player_.y - spawn->y) <= 80;
    }
    if (!inZone) {
        quote.reason = "返回本方出生点退款";
        return quote;
    }
    auto consider = [&](const Player::EquipmentRecord &record) {
        if (record.id && record.epoch == purchaseEpoch_ && record.buyer == 0 && !record.used) {
            ++quote.quantity;
            quote.amount = record.paid;
        }
    };
    if (request.kind == Presentation::BuyKind::DefuseKit && player_.hasDefuseKit)
        consider(player_.kitEquipment);
    if (request.kind == Presentation::BuyKind::Armor && !player_.armorEquipment.empty()) {
        const auto &record = player_.armorEquipment.back();
        if (record.armorTier == request.armorTier)
            consider(record);
    }
    if (request.kind == Presentation::BuyKind::Weapon) {
        for (int slot = 0; slot < 3; ++slot)
            if (player_.weapons[slot] == request.weapon)
                consider(player_.equipment[slot]);
        for (int i = 0; i < player_.grenadeCount; ++i)
            if (player_.grenades[i] == request.weapon)
                consider(player_.grenadeEquipment[i]);
    }
    if (!quote.amount)
        quote.reason = "需为本轮本人购买且尚未使用的装备";
    else if (player_.money + quote.amount > 16000) {
        quote.amount = quote.quantity = 0;
        quote.reason = "余额无法容纳全额退款";
    }
    return quote;
}

bool CS2DGame::ExecuteRefund(const Presentation::BuyRequest &request) {
    const auto quote = EvaluateRefund(request);
    if (quote.amount <= 0)
        return false;
    if (request.kind == Presentation::BuyKind::DefuseKit) {
        player_.hasDefuseKit = false;
        player_.kitEquipment = {};
    } else if (request.kind == Presentation::BuyKind::Armor) {
        const auto receipt = player_.armorEquipment.back();
        player_.armor = receipt.previousArmor;
        player_.helmet = receipt.previousHelmet;
        player_.armorEquipment.pop_back();
    } else {
        if (WeaponDB::Get(request.weapon).cls == WeaponClass::Grenade) {
            int selected = -1;
            std::uint64_t newest = 0;
            for (int i = 0; i < player_.grenadeCount; ++i) {
                const auto &record = player_.grenadeEquipment[i];
                if (player_.grenades[i] == request.weapon && record.buyer == 0 &&
                    record.epoch == purchaseEpoch_ && !record.used && record.id > newest) {
                    selected = i;
                    newest = record.id;
                }
            }
            const int previousIndex = player_.grenadeSlot;
            player_.grenadeSlot = selected;
            player_.ConsumeActiveGrenade();
            player_.grenadeSlot = std::min(previousIndex, std::max(0, player_.grenadeCount - 1));
            player_.SyncActiveGrenade();
        } else
            for (int slot = 0; slot < 3; ++slot)
                if (player_.weapons[slot] == request.weapon) {
                    player_.ClearSlotWeapon(slot);
                    player_.equipment[slot] = {};
                    player_.fireStates[slot] = {};
                }
        if (player_.weapons[player_.weaponSlot] == WeaponId::None)
            EquipFallbackWeapon();
        previousEquipSlot_ = 2;
        previousEquipWeapon_ = WeaponId::Knife;
        player_.reloading = false;
        player_.reloadTimer = 0;
        ResetScope(Presentation::ScopeResetReason::WeaponChanged);
    }
    player_.money += quote.amount;
    return true;
}
