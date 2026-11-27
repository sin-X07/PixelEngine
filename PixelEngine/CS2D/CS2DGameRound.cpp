#include "CS2DGame.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// 回合流程
// ============================================================================

void CS2DGame::UpdateRoundFlow(float dt, IAudioSink* audio) {
    if (mode_ == MatchMode::DeathMatch) return;   // 死斗无回合

    // 比赛已结束：冻结回合状态机，停在最后一回合的结算画面上。
    // 之前这里仍会 round_.Update()，导致 已决→Freeze→…→Live 无限循环，
    // 而 BeginNewRound 因 MatchOver 提前返回，场上没人复活，比赛永远不结束。
    if (teams_.MatchOver()) {
        matchOverTimer_ += dt;
        return;
    }

    const RoundPhase before = round_.Phase();
    round_.Update(dt);
    const RoundPhase after = round_.Phase();

    // 结算结束 → 开新回合
    if (before == RoundPhase::Live && after == RoundPhase::Freeze) {
        BeginNewRound(audio);
    }
    // 时间不足提醒（每回合只发一次）
    if (round_.IsLive()) {
        const float left = round_.Config().roundTime - round_.Elapsed();
        if (left <= 30.0f) {
            for (Team t : {Team::T, Team::CT}) {
                if (!CoachFor(t).timeLowFired) {
                    CoachFor(t).timeLowFired = true;
                    CoachOnEvent(t, CoachEventType::TimeLow);
                }
            }
        }
        // 赛点
        if (teams_.ScoreT() == teams_.ScoreToWin() - 1 || teams_.ScoreCT() == teams_.ScoreToWin() - 1) {
            for (Team t : {Team::T, Team::CT}) {
                if (!CoachFor(t).matchPointFired) {
                    CoachFor(t).matchPointFired = true;
                    CoachOnEvent(t, CoachEventType::MatchPoint);
                }
            }
        }
    }

    prevPhase_ = after;
}

void CS2DGame::BeginNewRound(IAudioSink* audio) {
    StopObjectiveAudio(audio);
    if (teams_.MatchOver()) return;
    CoachResetRound();
    SnapshotRoundEconomy();   // 必须在任何采购之前：save/eco 看的是买枪前经济
    CoachBeginRound();      // 必须在买枪之前：save/eco 要拦得住 BotBuyLoadout
    RespawnAllForRound();
    SetupObjective();   // 每回合重置包点 / C4
    AssignSquadRoles(); // 每回合按新的相对位置重新分工
    RefreshSearchAreas();  // 搜点白名单每回合重置
    // 购买延迟：等本轮教练（尤其 LLM save/eco）先给结论，再决定是否采购。
    botBuyPending_ = true;
    botBuyDoneT_   = false;
    botBuyDoneCT_  = false;
    botBuyTimer_   = (coachLlmEnabled_ && coachLlm_) ? 7.5f : 0.0f;
    // 经济不足：按买枪前快照提示教练（eco 决策依据）
    for (Team t : {Team::T, Team::CT}) {
        if (LowEconomyAverage(t)) {
            CoachOnEvent(t, CoachEventType::EconomyLow);
        }
    }
    CoachOnEvent(Team::T,  CoachEventType::RoundStart);
    CoachOnEvent(Team::CT, CoachEventType::RoundStart);
    bombBeepTimer_ = 0.0f;
    announcement_ = "第 " + std::to_string(round_.RoundNumber()) + " 回合";
    announcementTimer_ = 2.2f;
    std::cout << "[CS2D] 第 " << round_.RoundNumber() << " 回合开始 ("
              << teams_.ScoreT() << ":" << teams_.ScoreCT() << ")" << std::endl;
}

void CS2DGame::RespawnAllForRound(bool carryWeapons) {
    ++purchaseEpoch_;
    grenades_.clear();
    smokes_.clear();
    fires_.clear();
    decoys_.clear();
    for (auto& actor : actors_)
        actor.fireStates = {};
    // 新回合：开镜归零 + 枪弹视觉段清空（契约 v1 第 5、6 节）
    ResetScope(Presentation::ScopeResetReason::RoundRestart);
    bulletVisuals_.clear();
    nextBulletVisualId_ = 1;

    // 新回合必须退出观战，否则会以观战状态继续下一局
    SpectatorEnd();
    deathPresentation_ = {};
    deathCam_ = false;

    EnsureBotAi();
    for (size_t i = 0; i < actors_.size(); ++i) {
        auto& actor = actors_[i];
        if (!actor.IsActive()) continue;
        const bool survived = carryWeapons && actor.alive;

        const WeaponId w0 = actor.weapons[0];
        const WeaponId w1 = actor.weapons[1];
        const WeaponId w3 = actor.weapons[3];
        const int nade = actor.grenadeCount;
        const int armor = actor.armor;
        const bool helmet = actor.helmet;

        const auto equipment = actor.equipment;
        const auto grenadeEquipment = actor.grenadeEquipment;
        const auto kitEquipment = actor.kitEquipment;
        const auto armorEquipment = actor.armorEquipment;
        const bool hadKit = actor.hasDefuseKit;
        const int activeGrenade = actor.grenadeSlot;
        std::array<WeaponId, Player::MAX_GRENADES> grenades;
        std::copy(std::begin(actor.grenades), std::end(actor.grenades), grenades.begin());

        if (actor.IsHuman()) {
            actor.health = actor.maxHealth;
            actor.alive = true;
            actor.reloading = false;
            actor.reloadTimer = 0.0f;
            actor.deathTimer = 0.0f;
            actor.vx = actor.vy = 0.0f;
            actor.holdingC4 = false;
            actor.plantProgress = 0.0f;
            actor.plantingChannel = -1;
            actor.flashTimer = 0.0f;
            actor.flashPeak = 0.0f;
            actor.defuseProgress = 0.0f;
            actor.defuseChannel = -1;
            PickSpawnForTeam(actor.team, actor.x, actor.y);

            if (survived) {
                actor.weapons[0] = w0;
                actor.weapons[1] = w1;
                actor.weapons[2] = WeaponId::Knife;
                actor.weapons[3] = w3;
                actor.grenadeCount = nade;
                actor.armor = armor;
                actor.helmet = helmet;
                actor.weaponSlot = (w0 != WeaponId::None) ? 0 : 1;
            } else {
                actor.equipment = {};
                actor.grenadeEquipment = {};
                actor.armorEquipment.clear();
                actor.kitEquipment = {};
                actor.hasDefuseKit = false;
                actor.weapons[0] = WeaponId::None;
                actor.weapons[1] = LoadoutForTeam(actor.team).starting;
                actor.weapons[2] = WeaponId::Knife;
                actor.weapons[3] = WeaponId::None;
                actor.grenadeCount = 0;
                actor.armor = 0;
                actor.helmet = false;
                actor.weaponSlot = 1;
            }

            for (int sl = 0; sl < 5; ++sl) {
                const WeaponId wid = actor.weapons[sl];
                if (wid == WeaponId::None) actor.ClearSlotWeapon(sl);
                else actor.SetSlotWeapon(sl, wid);
            }
            if (actor.weapons[actor.weaponSlot] == WeaponId::None) actor.weaponSlot = 1;
            for (int slot = 0; slot < 5; ++slot)
                if (actor.weapons[slot] != WeaponId::None && !actor.equipment[slot].id) {
                    actor.equipment[slot].id = nextEquipmentId_++;
                    actor.equipment[slot].epoch = purchaseEpoch_;
                }
            actor.LoadAmmoFromSlot();
            actor.reloading = false;
            actor.reloadTimer = 0.0f;
        } else {
            SpawnBOT(actor, actor.team); // 先重置为手枪基准
            ResetBotAi(i);               // AI 运行时状态一并清空

            if (survived) {
                actor.equipment = equipment;
                actor.grenadeEquipment = grenadeEquipment;
                actor.kitEquipment = kitEquipment;
                actor.hasDefuseKit = hadKit;
                actor.armorEquipment = armorEquipment;
                std::copy(grenades.begin(), grenades.end(), std::begin(actor.grenades));
                actor.grenadeSlot = activeGrenade;
                actor.weapons[0] = w0;
                actor.weapons[1] = w1;
                actor.weapons[3] = w3;
                actor.grenadeCount = nade;
                actor.armor = armor;
                actor.helmet = helmet;
                actor.weaponSlot = (w0 != WeaponId::None) ? 0 : 1;
                const WeaponDef &w = WeaponDB::Get(actor.weapons[actor.weaponSlot]);
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
            }
        }
    }
    bullets_.clear();
    drops_.clear(); // 掉落物只保留到本回合结束
    lastDeathTimer_ = 0.0f;
}

// BOT 经济：按 BuyPlan 分档采购（与玩家同一套价格，照抄 CS2）
// 开局只有起始手枪、无护甲；顺序：先枪、再甲、再雷
// 阵营隔离：只买本阵营可用的武器（WeaponDB::TeamAllowed）
//
// BuyPlan 语义：
//   Eco     — 不花（Save intent 也强制 Eco）
//   Half    — 半起：主武封顶 Galil/FAMAS/UMP/Nova；甲仅在主武后仍有 >=650；至多 1 雷；禁 AWP/M4/AK
//   Force   — 强起：掏空；优先甲 + SMG 或廉价步枪；禁 AWP；剩钱 1~2 雷
//   Full    — 满起：高到低步枪偏好（允许 AWP）
//   AntiEco — 反 eco overlay：优先 SMG（MAC10/MP9/MP7/UMP）+ 甲 + 轻雷；禁 AWP；已有步枪则保留
void CS2DGame::BotBuyLoadout(Player& bot) {
    // 拆包钳单价：与玩家购买价一致（CS2DGame::PurchasePriceOf: DefuseKit -> 400）。
    constexpr int kDefuseKitPrice = 400;
    BuyPlan plan = GetBuyPlan(bot.team);
    if (GetCoachPlan(bot.team).intent == PlanIntent::Save) plan = BuyPlan::Eco;
    if (plan == BuyPlan::Eco) return;  // 不花钱

    const bool isT = (bot.team == Team::T);
    WeaponId primary = bot.weapons[0];
    int primaryPaid=0;

    struct Cand { WeaponId id; int price; };

    auto buyFirstAffordable = [&](const Cand* order, size_t n) {
        if (primary != WeaponId::None) return;
        for (size_t i = 0; i < n; ++i) {
            const Cand& c = order[i];
            if (!WeaponDB::TeamAllowed(c.id, isT)) continue;
            if (bot.money >= c.price) {
                primary = c.id;
                bot.money -= c.price;
                primaryPaid=c.price;
                return;
            }
        }
    };

    auto buyArmor = [&](bool requireMoneyAfterPrimary) {
        // Half：买甲前要求主武后余额仍够 650；其它档有钱就买
        if (requireMoneyAfterPrimary && bot.money < 650) return;
        if (bot.armor >= 100 && bot.helmet) return;
        if (bot.money >= 1000) {
            bot.armor = 100; bot.helmet = true;  bot.money -= 1000;
        } else if (bot.money >= 650) {
            bot.armor = 100; bot.helmet = false; bot.money -= 650;
        }
    };

    auto buyNades = [&](int maxTotal) {
        // P3-molotov: fire grenade is team-locked (T Molotov $400 / CT
        // Incendiary $600, WeaponDB prices). Bought 2nd after HE, so
        // Full/Force/AntiEco BOTs carry one when they can afford it;
        // Half (maxTotal 1) keeps HE only. Same "leftover money" rule
        // as the other grenades (buy only if money >= price).
        const WeaponId fireGrenade = isT ? WeaponId::GrenadeMolotov : WeaponId::GrenadeIncendiary;
        const WeaponId order[4] = {
            WeaponId::GrenadeHE,
            fireGrenade,
            isT ? WeaponId::GrenadeFlash : WeaponId::GrenadeSmoke,
            isT ? WeaponId::GrenadeSmoke : WeaponId::GrenadeFlash
        };
        for (WeaponId gid : order) {
            if (!WeaponDB::TeamAllowed(gid, isT)) continue;
            const WeaponDef& gd = WeaponDB::Get(gid);
            const int limit = (gid == WeaponId::GrenadeFlash) ? 2 : 1;
            while (bot.grenadeCount < Player::MAX_GRENADES &&
                   bot.grenadeCount < maxTotal &&
                   bot.CountGrenade(gid) < limit &&
                   gd.price > 0 && bot.money >= gd.price) {
                bot.grenades[bot.grenadeCount++] = gid;
                bot.money -= gd.price;
                if (gid == fireGrenade) {
                    ++counters_.molotovBuys;
                    std::cout << "[Buy] " << (isT ? "T" : "CT") << " BOT #"
                              << ActorIdOf(bot)
                              << (isT ? " Molotov ($400)" : " Incendiary ($600)")
                              << " balance $" << bot.money << std::endl;
                }
            }
        }
        bot.SyncActiveGrenade();
    };

    // ---- 按档选主武 ----
    switch (plan) {
        case BuyPlan::Half: {
            // 封顶廉价步枪 / UMP / Nova；禁 AWP/M4/AK 等满起枪
            const Cand order[] = {
                { WeaponId::FAMAS,  2050 },
                { WeaponId::GalilAR,1800 },
                { WeaponId::MP7,    1500 },
                { WeaponId::UMP45,  1200 },
                { WeaponId::MP9,    1250 },
                { WeaponId::MAC10,  1050 },
                { WeaponId::Nova,   1050 },
            };
            buyFirstAffordable(order, sizeof(order) / sizeof(order[0]));
            buyArmor(/*requireMoneyAfterPrimary=*/true);
            buyNades(/*maxTotal=*/1);
            break;
        }
        case BuyPlan::Force: {
            // 强起：优先 SMG / 廉价步枪；禁 AWP；甲优先于贵枪
            // 先留甲预算再选枪，避免买了 Galil 却裸奔
            const bool canVest = (bot.money >= 650);
            const int armorReserve = canVest ? ((bot.money >= 1000) ? 1000 : 650) : 0;
            const int budget = bot.money - armorReserve;
            if (primary == WeaponId::None) {
                const Cand order[] = {
                    { WeaponId::FAMAS,  2050 },
                    { WeaponId::GalilAR,1800 },
                    { WeaponId::MP7,    1500 },
                    { WeaponId::MP9,    1250 },
                    { WeaponId::UMP45,  1200 },
                    { WeaponId::MAC10,  1050 },
                    { WeaponId::Nova,   1050 },
                };
                for (const auto& c : order) {
                    if (!WeaponDB::TeamAllowed(c.id, isT)) continue;
                    if (budget >= c.price && bot.money >= c.price) {
                        primary = c.id;
                        bot.money -= c.price;
                primaryPaid=c.price;
                        break;
                    }
                }
            }
            buyArmor(false);
            buyNades(/*maxTotal=*/2);
            break;
        }
        case BuyPlan::AntiEco: {
            // 反 eco：SMG 优先（刷击杀奖）；已有步枪则保留；禁 AWP
            if (primary == WeaponId::None) {
                const Cand order[] = {
                    { WeaponId::MP7,    1500 },
                    { WeaponId::MP9,    1250 },
                    { WeaponId::UMP45,  1200 },
                    { WeaponId::MAC10,  1050 },
                    { WeaponId::FAMAS,  2050 },
                    { WeaponId::GalilAR,1800 },
                    { WeaponId::M4A1S,  2900 },
                    { WeaponId::AK47,   2700 },
                    { WeaponId::M4A4,   3100 },
                };
                buyFirstAffordable(order, sizeof(order) / sizeof(order[0]));
            }
            buyArmor(false);
            buyNades(/*maxTotal=*/2);
            break;
        }
        case BuyPlan::Full:
        default: {
            // 满起：高到低（允许 AWP）
            const Cand order[] = {
                { WeaponId::AWP,    4750 },
                { WeaponId::AUG,    3300 },
                { WeaponId::M4A4,   3100 },
                { WeaponId::SG553,  3000 },
                { WeaponId::M4A1S,  2900 },
                { WeaponId::AK47,   2700 },
                { WeaponId::FAMAS,  2050 },
                { WeaponId::GalilAR,1800 },
                { WeaponId::MP7,    1500 },
                { WeaponId::UMP45,  1200 },
                { WeaponId::Nova,   1050 },
            };
            buyFirstAffordable(order, sizeof(order) / sizeof(order[0]));
            buyArmor(false);
            // P3-kit（遗留 6）：CT 专属拆包钳——仅 Full 档、主武+护甲之后、
            // 有余钱才买，不挤主武器/护甲；优先于投掷物。买一次整局有效
            // （幸存 carry，阵亡掉落可被捡起；无钳时下次 Full 再补）。
            // 拆包 4.0s vs 10.0s 见 CS2DGameObjective::UpdateObjective。
            if (!isT && !bot.hasDefuseKit && bot.money >= kDefuseKitPrice) {
                Player::EquipmentRecord receipt;
                receipt.id = nextEquipmentId_++;
                receipt.epoch = purchaseEpoch_;
                receipt.buyer = ActorIdOf(bot);
                receipt.paid = kDefuseKitPrice;
                bot.kitEquipment = receipt;
                bot.money -= kDefuseKitPrice;
                bot.hasDefuseKit = true;
                std::cout << "[Buy] CT BOT #" << receipt.buyer << " 拆包钳 ($400) 余额 $"
                          << bot.money << std::endl;
            }
            buyNades(/*maxTotal=*/Player::MAX_GRENADES);
            break;
        }
    }

    if (primary!=bot.weapons[0]) {
        DropWeaponToGround(bot,0,bot.x,bot.y);
        Player::EquipmentRecord receipt;
        receipt.id=nextEquipmentId_++;receipt.epoch=purchaseEpoch_;
        receipt.buyer=ActorIdOf(bot);receipt.paid=primaryPaid;
        bot.equipment[0]=receipt;bot.fireStates[0]={};
    }
    bot.weapons[0] = primary;
    if (primary != WeaponId::None) {
        bot.SetSlotWeapon(0, primary);
        bot.weaponSlot = 0;
        bot.LoadAmmoFromSlot();
    } else {
        bot.SetSlotWeapon(1, bot.weapons[1]);
        bot.weaponSlot = 1;
        bot.LoadAmmoFromSlot();
    }

    economy_.Clamp(bot.money);
}

// 回合初延迟购买：先让教练（尤其 LLM）给出 save/eco，再决定是否采购。
// 旧实现是在 RespawnAllForRound 立刻买，LLM 的 save 回包到达时钱已经花完。
void CS2DGame::UpdateBotBuy(float dt) {
    if (!botBuyPending_) return;

    // 已经进入交战期还没买：强制补齐，避免 BOT 空枪出门。
    const bool force = (round_.Phase() == RoundPhase::Live);
    if (!force) {
        botBuyTimer_ -= dt;
        if (botBuyTimer_ < 0.0f) botBuyTimer_ = 0.0f;
    }

    bool anyWaiting = false;
    for (Team t : {Team::T, Team::CT}) {
        bool& done = (t == Team::T) ? botBuyDoneT_ : botBuyDoneCT_;
        if (done) continue;

        CoachState& st = CoachFor(t);

        // 教练已确定保枪：本回合不采购，余额留到下一回合。
        if (st.plan.intent == PlanIntent::Save) {
            done = true;
            std::cout << "[Buy] " << TeamName(t) << " BOT 保枪：本回合不采购" << std::endl;
            continue;
        }

        // 回合初 LLM 请求在途：等回包或最长 6 秒；超时后按规则计划采购。
        const bool waitingLlm = coachLlmEnabled_ && coachLlm_ &&
                                !st.llmAnswered && botBuyTimer_ > 0.0f && !force;
        if (waitingLlm) {
            anyWaiting = true;
            continue;
        }

        int bought = 0;
        for (auto& actor : actors_) {
            if (!actor.IsBot() || actor.team != t || !actor.alive) continue;
            BotBuyLoadout(actor);
            ++bought;
        }
        done = true;
        std::cout << "[Buy] " << TeamName(t) << " BOT 采购完成 (" << bought << " 人)"
                  << (force ? " [force]" : "") << std::endl;
    }

    if (force || (botBuyDoneT_ && botBuyDoneCT_) ||
        (!anyWaiting && botBuyTimer_ <= 0.0f)) {
        botBuyPending_ = false;
    }
}

void CS2DGame::CheckWinConditions(IAudioSink* audio) {
    if (mode_ == MatchMode::DeathMatch) return;
    if (!round_.IsLive()) return;
    if (round_.IsDecided()) return;
    if (teams_.MatchOver()) return;

    const int aliveT  = AliveCount(Team::T);
    const int aliveCT = AliveCount(Team::CT);

    Team winner = Team::None;
    bool decided = false;

    switch (mode_) {
        case MatchMode::BombDefusal:
            // 拆包/爆炸已在 UpdateObjective 中结算
            if (aliveCT == 0 && aliveT > 0) {
                decided = true; winner = Team::T;             // CT 全灭
            } else if (aliveT == 0 && !bombPlanted_) {
                decided = true; winner = Team::CT;            // T 全灭且未下包
            } else if (round_.IsTimeUp() && !bombPlanted_) {
                decided = true; winner = Team::CT;            // 守方胜
            }
            break;

        case MatchMode::TeamElimination:
        default:
            if (aliveT == 0 || aliveCT == 0) {
                decided = true;
                if (aliveT == 0 && aliveCT == 0)  winner = Team::None;   // 同归于尽
                else if (aliveT == 0)             winner = Team::CT;
                else                              winner = Team::T;
            } else if (round_.IsTimeUp()) {
                decided = true; winner = Team::CT;            // 时间到守方胜
            }
            break;
    }

    if (!decided) return;

    ApplyRoundResult(winner, audio);
    round_.EndRound(winner);
}

void CS2DGame::ApplyRoundResult(Team winner, IAudioSink* audio) {
    StopObjectiveAudio(audio);
    if (round_.IsDecided()) return;   // 防重复结算（目标物与歼灭可能同帧触发）

    if (winner != Team::None) {
        teams_.AddScore(winner);
    CoachRecordRound(Team::T,  winner == Team::T);
    CoachRecordRound(Team::CT, winner == Team::CT);
    }

    const Team viewerTeam = LocalActor().team;
    // 回合胜负音效（以本地玩家阵营视角）
    if (audio && winner != Team::None && winner == viewerTeam) {
        audio->PlaySound(SoundEffect::ROUND_WIN);
    }
    if (winner != Team::None) {
        announcement_ = (winner == viewerTeam) ? "回合胜利" : "回合失败";
        announcementTimer_ = 2.4f;
    }

    GrantRoundMoney(winner);

    if (teams_.MatchOver() && !matchOverAnnounced_) {
        matchOverAnnounced_ = true;
        std::cout << "[CS2D] 比赛结束! " << TeamName(teams_.MatchWinner())
                  << " 获胜 (" << teams_.ScoreT() << ":" << teams_.ScoreCT() << ")" << std::endl;
    }
}

// 回合结算：发放金钱，并重置为下一回合的初始装备
void CS2DGame::GrantRoundMoney(Team winner) {
    for (auto& actor : actors_) {
        if (!actor.IsActive()) continue;
        actor.money += economy_.GrantRoundResult(winner, actor.team);
        economy_.Clamp(actor.money);
    }

    std::cout << "[Economy] 回合结算 → 玩家 $" << LocalActor().money << std::endl;
}

int CS2DGame::AliveCount(Team t) const {
    if (t == Team::None) return 0;
    int n = 0;
    for (const auto& a : actors_) {
        if (a.IsActive() && a.alive && a.team == t) ++n;
    }
    return n;
}

