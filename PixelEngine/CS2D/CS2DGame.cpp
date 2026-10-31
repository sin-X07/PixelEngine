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
    bots_.clear();
    bullets_.clear();
    bulletVisuals_.clear();
    nextBulletVisualId_ = 1;
    // 新一局/换队：开镜立即归零
    ResetScope(Presentation::ScopeResetReason::RoundRestart);
    grenades_.clear();
    smokes_.clear();
    fires_.clear();
    fireTickTimer_ = 0.0f;
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
    if (request.kind == Presentation::BuyKind::Armor) {
        if (request.armorTier < 1 || request.armorTier > 2) return BuyCode::InvalidItem;
    } else {
        const int idx = static_cast<int>(request.weapon);
        if (idx <= 0 || idx >= WeaponDB::kWeaponCount) return BuyCode::InvalidItem;
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        if (def.price <= 0) return BuyCode::InvalidItem;                    // 刀 / C4 不售卖
        if (def.slot == WeaponSlotKind::Melee) return BuyCode::InvalidItem;
    }

    // 2) 存活 → 时窗 → 区域 → 阵营（固定优先序，保证失败原因稳定可测）
    if (!player_.alive) return BuyCode::PlayerDead;
    if (!IsBuyWindowOpen()) return BuyCode::WindowClosed;
    if (!IsPlayerInBuyZone()) return BuyCode::OutsideBuyZone;
    if (request.kind == Presentation::BuyKind::Weapon &&
        !WeaponDB::TeamAllowed(request.weapon, player_.team == Team::T)) {
        return BuyCode::TeamRestricted;
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
    const Presentation::BuyRequest& request, AudioManager* audio) {
    Presentation::BuyResult result;
    result.request = request;
    result.code = EvaluatePurchase(request);      // 点击时重新判定
    result.moneyAfter = player_.money;
    result.quantityAfter = PurchaseQuantityOf(request);

    if (result.code != Presentation::BuyCode::Ok) {
        return result;                            // 失败：金额 / 装备 / 音效都不动
    }

    if (request.kind == Presentation::BuyKind::Armor) {
        const int tier  = request.armorTier;
        const int price = PurchasePriceOf(request);
        player_.money -= price;
        economy_.Clamp(player_.money);
        player_.armor = 100;
        if (tier == 2) player_.helmet = true;
        std::cout << "[Buy] " << (tier == 2 ? "护甲+头盔" : "护甲")
                  << " ($" << price << ") 余额 $" << player_.money << std::endl;
    } else {
        const WeaponDef& def = WeaponDB::Get(request.weapon);
        switch (def.slot) {
            case WeaponSlotKind::Primary:
                player_.SetSlotWeapon(0, request.weapon);
                player_.weaponSlot = 0;
                player_.LoadAmmoFromSlot();
                player_.reloading = false;
                player_.reloadTimer = 0.0f;
                playerHoldingC4_ = false;
                break;
            case WeaponSlotKind::Secondary:
                player_.SetSlotWeapon(1, request.weapon);
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
                player_.grenades[player_.grenadeCount++] = request.weapon;
                player_.SyncActiveGrenade();
                break;
        }
        player_.money -= def.price;
        economy_.Clamp(player_.money);
        std::cout << "[Buy] " << def.name << " ($" << def.price
                  << ") 余额 $" << player_.money << std::endl;
        // 购买替换了当前手持武器 → 开镜必须归零
        if (def.slot == WeaponSlotKind::Primary || def.slot == WeaponSlotKind::Secondary) {
            ResetScope(Presentation::ScopeResetReason::WeaponChanged);
        }
    }

    if (audio) audio->PlaySound(SoundEffect::EQUIP);
    result.moneyAfter = player_.money;
    result.quantityAfter = PurchaseQuantityOf(request);
    return result;
}

// 兼容包装：旧调用点保持原签名，成功只执行一次，不递归、不重复扣款。
bool CS2DGame::TryBuy(WeaponId id, AudioManager* audio) {
    Presentation::BuyRequest r;
    r.kind   = Presentation::BuyKind::Weapon;
    r.weapon = id;
    return ExecutePurchase(r, audio).code == Presentation::BuyCode::Ok;
}

// 购买护甲：tier 1=护甲($650) 2=护甲+头盔($1000)
bool CS2DGame::TryBuyArmor(int tier, AudioManager* audio) {
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
            // 换了一颗手雷也算「切到新装备」，补一声
            switchSoundWeapon_ = player_.ActiveGrenade();
            switchSoundPending_ = true;
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

    // 记录「切到投掷物之前」的槽位：最后一颗雷扔完后自动切回这里
    if (slot == 3 && player_.weaponSlot != 3) {
        player_.prevWeaponSlot = player_.weaponSlot;
    }

    // 切走前把当前武器的弹药写回它自己的槽位
    player_.SaveAmmoToSlot();

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
        player_.grenadeSlot = 0;
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
//   - 开镜时可见区是以准星为圆心的圆（渲染层用 SetScopeMask 消费），镜外用视野雾遮盖
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
    scopeLevel_      = 0;
    scopeEngagement_ = 0.0f;
    scopeEngFrom_    = scopeEngTo_ = 0.0f;
    scopeEngT_       = 1.0f;
    scopeZoom_       = 1.0f;
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

    // 0.12s 线性过渡：倍率与镜框淡入各自推进（换档时 transition 保持 1，只有倍率变）
    const float step = (Presentation::kScopeTransitionSeconds > 0.0f)
                           ? dt / Presentation::kScopeTransitionSeconds : 1.0f;
    if (scopeZoomT_ < 1.0f) scopeZoomT_ = std::min(1.0f, scopeZoomT_ + step);
    scopeZoom_ = scopeZoomFrom_ + (scopeZoomTo_ - scopeZoomFrom_) * scopeZoomT_;

    if (scopeEngT_ < 1.0f) scopeEngT_ = std::min(1.0f, scopeEngT_ + step);
    scopeEngagement_ = scopeEngFrom_ + (scopeEngTo_ - scopeEngFrom_) * scopeEngT_;
}

// 相机向准星偏移的比例：1.5x → 0.35，2x → 0.5，其余按倍率线性插值
float CS2DGame::ScopeCameraShift() const {
    const float z = std::clamp(scopeZoom_, 1.0f, 2.0f);
    if (z <= 1.5f) return (z - 1.0f) / 0.5f * 0.35f;
    const float kMax = Presentation::kScopeCameraShiftMax;
    return 0.35f + (z - 1.5f) / 0.5f * (kMax - 0.35f);
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

    // 准星世界坐标：开镜的圆形可见区与相机偏移都以它为中心
    aimWorldX_ = mouseWorldX;
    aimWorldY_ = mouseWorldY;

    // 开镜过渡：每帧按 dt 逼近目标档位（0.12s 线性），与输入边沿解耦
    TickScope(dt);

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
                                float centerX, float centerY, float zoom,
                                bool clampToMap) {
    if (!renderer_ || !world_) return;
    renderer_->SetViewportSize(viewW, viewH);
    renderer_->SetPreviewCamera(centerX, centerY, zoom, clampToMap);
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

    // 开镜是玩法层唯一状态：这里只取快照，不改状态
    const Presentation::ScopeView scope = GetScopeView();

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
    } else if (scope.active) {
        // 契约 v1.1：开镜时相机向准星偏移，准星下的世界点不随倍率漂移
        const float shift = ScopeCameraShift();
        camX += (aimWorldX_ - player_.x) * shift;
        camY += (aimWorldY_ - player_.y) * shift;
    }
    renderer_->UpdateCamera(camX, camY, viewW, viewH, scope.zoomMultiplier);

    // 契约 v1.1：开镜时可见区改为以准星为圆心的圆，镜外由视野雾遮盖。
    // 半径以「屏幕像素 / 当前倍率」换算成世界半径，保证与屏幕上的镜筒一致。
    const float scopeRadiusPx =
        Presentation::kScopeCircleRadiusFrac * static_cast<float>(std::min(viewW, viewH));
    const bool  scopeMaskOn = scope.active && scope.supported && !IsSpectating() && player_.alive;
    if (scopeMaskOn) {
        const float zoomNow = std::max(0.001f, renderer_->GetZoom());
        renderer_->SetScopeMask(true, aimWorldX_, aimWorldY_, scopeRadiusPx / zoomNow);
    } else {
        renderer_->SetScopeMask(false, 0.0f, 0.0f, 0.0f);
    }

    renderer_->DrawWorld();

    // 烟雾画在地面层（视野雾之下）：角色、掉落物、目标标记都在它之上
    for (const auto& s : smokes_) {
        renderer_->DrawSmokeCloud(s.x, s.y, s.radius, s.age, s.expandTime,
                                  s.life, s.maxLife, s.seed);
    }

    // 燃烧弹留下的火区（在地面层、角色之下）
    for (const auto& f : fires_) {
        renderer_->DrawFireZone(f.x, f.y, f.radius, f.age, f.life, f.maxLife, f.seed);
    }

    auto fillParams = [this](const Player& p, SpriteDrawParams& sp) {
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
        // 契约 v1 第 5 节：沿用 Team，不建立第二份阵营状态
        sp.team        = p.team;
        sp.friendly    = (p.team == player_.team);
        sp.localPlayer = (&p == &player_);
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

    // 枪弹视觉段：玩法层已生成的唯一数据源，UI 只绘制（契约 v1 第 6 节）
    renderer_->DrawBulletVisuals(GetBulletVisuals());

    // 投掷物
    for (const auto& g : grenades_) {
        if (!g.active) continue;
        renderer_->DrawThrownGrenade(static_cast<int>(g.type), g.x, g.y, g.spin);
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

    // 开镜覆盖层：世界雾之后、闪光覆盖之前（契约 v1.1 第 4 节）
    // 圆心用「准星世界点投影到屏幕」而不是虚拟光标，避免相机被地图边界钳制时镜筒错位
    {
        const float scopeScreenX = (aimWorldX_ - renderer_->GetCameraX()) * renderer_->GetZoom();
        const float scopeScreenY = (aimWorldY_ - renderer_->GetCameraY()) * renderer_->GetZoom();
        renderer_->DrawScopeOverlay(scope, scopeScreenX, scopeScreenY,
                                    scopeRadiusPx, viewW, viewH);
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
