#include "CS2DSimulation.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

// ============================================================================
// 目标物：爆破（下包/拆包）—— 纯权威仿真，通过 SimEventQueue 发射 BombEvent
// ============================================================================

namespace {
constexpr float BOMB_FUSE   = 40.0f;   // 下包后倒计时
constexpr float PLANT_TIME  = 3.0f;    // T 在包点停留多久完成下包
constexpr float DEFUSE_TIME = 10.0f;   // CT 拆包耗时
} // namespace

std::vector<CS2DSimulation::ScoreRow> CS2DSimulation::BuildScoreboard() const {
    std::vector<ScoreRow> rows;
    rows.reserve(actors_.size());

    for (const auto& actor : actors_) {
        if (!actor.IsActive()) continue;
        ScoreRow r;
        r.id        = actor.id;
        r.isHuman   = actor.IsHuman();
        r.isPlayer  = (actor.id == localActorId_);
        if (!actor.name.empty()) {
            r.name = actor.name;
        } else if (r.isPlayer) {
            r.name = "你";
        } else {
            r.name = (actor.IsHuman() ? "P" : "BOT#") + std::to_string(actor.id);
        }
        r.team      = actor.team;
        r.kills     = actor.kills;
        r.teamKills = actor.teamKills;
        r.deaths    = actor.deaths;
        r.money     = actor.money;
        r.alive     = actor.alive;
        rows.push_back(r);
    }

    std::stable_sort(rows.begin(), rows.end(), [](const ScoreRow& a, const ScoreRow& b) {
        if (a.team != b.team) return static_cast<int>(a.team) > static_cast<int>(b.team);
        return a.kills > b.kills;
    });
    return rows;
}

void CS2DSimulation::SetupObjective() {
    c4EquipmentId_ = mode_ == MatchMode::BombDefusal ? nextEquipmentId_++ : 0;
    AssignBombCarrier();
    bombPlanted_   = false;
    bombTimer_     = 0.0f;
    defuseProgress_ = 0.0f;
    defuseRemaining_ = 0.0f;
    activeDefuser_ = kInvalidActorId;

    bombDropped_    = false;
    bombDropDelay_  = 0.0f;
    bombDropX_ = bombDropY_ = 0.0f;
    bombIntel_      = false;
    bombIntelAge_   = 0.0f;
    bombIntelX_ = bombIntelY_ = 0.0f;
    for (size_t i = 0; i < actors_.size(); ++i) {
        actors_[i].holdingC4 = false;
        actors_[i].plantProgress = 0.0f;
        actors_[i].defuseProgress = 0.0f;
        actors_[i].weapons[4] = WeaponId::None;
        actors_[i].equipment[4] = {};
        if (actors_[i].weaponSlot == 4) actors_[i].weaponSlot = 1;
        if (i < actorAi_.size()) actorAi_[i].defusing = false;
    }
    bombX_ = bombY_ = 0.0f;

    SimEvent ev;
    ev.type = SimEventType::BombEvent;
    ev.bombKind = BombEventKind::StopAllObjectiveAudio;
    EmitSimEvent(std::move(ev));
}

void CS2DSimulation::UpdateObjective(float dt) {
    if (mode_ != MatchMode::BombDefusal || !round_.IsLive()) {
        for (size_t i = 0; i < actors_.size(); ++i) {
            const bool hadPlant = actors_[i].plantProgress > 0.0f;
            const bool hadDefuse = actors_[i].defuseProgress > 0.0f;
            actors_[i].plantProgress = 0.0f;
            actors_[i].defuseProgress = 0.0f;
            if (i < actorAi_.size()) actorAi_[i].defusing = false;
            if (hadPlant) {
                SimEvent ev;
                ev.type = SimEventType::BombEvent;
                ev.bombKind = BombEventKind::PlantStopped;
                ev.actorId = actors_[i].id;
                EmitSimEvent(std::move(ev));
            }
            if (hadDefuse) {
                SimEvent ev;
                ev.type = SimEventType::BombEvent;
                ev.bombKind = BombEventKind::DefuseStopped;
                ev.actorId = actors_[i].id;
                EmitSimEvent(std::move(ev));
            }
        }
        activeDefuser_ = kInvalidActorId;
        defuseProgress_ = defuseRemaining_ = 0.0f;
        return;
    }

    if (!bombPlanted_) {
        for (size_t i = 0; i < actorAi_.size(); ++i) actorAi_[i].defusing = false;

        Player* carrier = (!bombDropped_ && world_) ? FindActor(bombCarrier_) : nullptr;
        const bool canCarrierPlant = carrier && carrier->IsActive() && carrier->alive &&
                                     carrier->team == Team::T &&
                                     world_->FindBombSiteAt(carrier->x, carrier->y) != nullptr;
        const bool wantPlant = canCarrierPlant &&
                               (carrier->IsHuman() ? carrier->interactHeld : true);

        for (auto& actor : actors_) {
            if (&actor != carrier || !wantPlant) {
                const bool wasPlanting = actor.plantProgress > 0.0f;
                actor.plantProgress = 0.0f;
                if (wasPlanting) {
                    SimEvent ev;
                    ev.type = SimEventType::BombEvent;
                    ev.bombKind = BombEventKind::PlantStopped;
                    ev.actorId = actor.id;
                    EmitSimEvent(std::move(ev));
                }
            }
        }

        if (wantPlant && carrier) {
            if (carrier->IsHuman()) {
                if (carrier->plantProgress == 0.0f) {
                    SimEvent ev;
                    ev.type = SimEventType::BombEvent;
                    ev.bombKind = BombEventKind::PlantActive;
                    ev.actorId = carrier->id;
                    ev.x = carrier->x;
                    ev.y = carrier->y;
                    EmitSimEvent(std::move(ev));
                }
            } else {
                bombX_ = carrier->x;
                bombY_ = carrier->y;
            }
            const bool wasBot = carrier->IsBot();
            carrier->plantProgress += dt;
            if (carrier->plantProgress >= PLANT_TIME) {
                const float px = carrier->x;
                const float py = carrier->y;
                carrier->plantProgress = 0.0f;
                PlantBomb(px, py);
                if (wasBot) {
                    CoachOnEvent(Team::T,  CoachEventType::DefuseAborted);
                    CoachOnEvent(Team::CT, CoachEventType::DefuseAborted);
                }
            }
        }
        return;
    }

    // ---- 已下包：倒计时 + 拆包 ----
    for (auto& actor : actors_) {
        const bool wasPlanting = actor.plantProgress > 0.0f;
        actor.plantProgress = 0.0f;
        if (wasPlanting) {
            SimEvent ev;
            ev.type = SimEventType::BombEvent;
            ev.bombKind = BombEventKind::PlantStopped;
            ev.actorId = actor.id;
            EmitSimEvent(std::move(ev));
        }
    }

    const float stepTime = std::min(std::max(0.0f, dt), std::max(0.0f, bombTimer_));

    {
        bombBeepTimer_ -= dt;
        const float interval = (bombTimer_ > 10.0f) ? 1.0f
                             : (bombTimer_ > 4.0f)  ? 0.5f
                             : 0.25f;
        if (bombBeepTimer_ <= 0.0f) {
            bombBeepTimer_ = interval;
            EmitSound(SoundEventType::BombBeep, bombX_, bombY_, Team::T);
            SimEvent ev;
            ev.type = SimEventType::BombEvent;
            ev.bombKind = BombEventKind::Beep;
            ev.x = bombX_;
            ev.y = bombY_;
            EmitSimEvent(std::move(ev));
        }
    }

    const ActorId previousLeader = activeDefuser_;
    activeDefuser_ = kInvalidActorId;
    defuseProgress_ = defuseRemaining_ = 0.0f;
    ActorId winner = kInvalidActorId;
    float firstFinish = std::numeric_limits<float>::infinity();
    float bestRemaining = std::numeric_limits<float>::infinity();

    auto updateDefuser = [&](Player& actor, bool eligible) {
        if (!eligible) {
            const bool wasDefusing = actor.defuseProgress > 0.0f;
            actor.defuseProgress = 0.0f;
            if (wasDefusing) {
                SimEvent ev;
                ev.type = SimEventType::BombEvent;
                ev.bombKind = BombEventKind::DefuseStopped;
                ev.actorId = actor.id;
                EmitSimEvent(std::move(ev));
            }
            return;
        }
        if (actor.hasDefuseKit && dt > 0.0f) actor.kitEquipment.used = true;
        const float duration = actor.hasDefuseKit ? 4.0f : DEFUSE_TIME;
        const float finishIn = (1.0f - actor.defuseProgress) * duration;
        const bool starting = actor.defuseProgress <= 0.0f;
        if (starting) {
            const std::string area = world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
            CoachOnEvent(Team::T,  CoachEventType::DefuseStarted, area);
            CoachOnEvent(Team::CT, CoachEventType::DefuseStarted, area);
        }
        {
            SimEvent ev;
            ev.type = SimEventType::BombEvent;
            ev.bombKind = BombEventKind::DefuseActive;
            ev.actorId = actor.id;
            ev.x = actor.x;
            ev.y = actor.y;
            ev.flagA = actor.hasDefuseKit;
            EmitSimEvent(std::move(ev));
        }
        actor.defuseProgress = std::min(1.0f, actor.defuseProgress + stepTime / duration);
        if (finishIn <= stepTime && finishIn < firstFinish) {
            winner = actor.id;
            firstFinish = finishIn;
        }
        const float remaining = (1.0f - actor.defuseProgress) * duration;
        const bool sameTime = std::abs(remaining - bestRemaining) < .0001f;
        if (remaining < bestRemaining - .0001f || (sameTime && actor.id == previousLeader)) {
            bestRemaining = remaining;
            activeDefuser_ = actor.id;
            defuseProgress_ = actor.defuseProgress;
            defuseRemaining_ = remaining;
        }
    };

    EnsureBotAi();
    for (size_t i = 0; i < actors_.size(); ++i) {
        auto& actor = actors_[i];
        if (!actor.IsActive()) continue;
        bool eligible = false;
        if (actor.IsHuman()) {
            eligible = actor.alive && actor.team == Team::CT && actor.interactHeld &&
                       IsInDefuseRange(actor.x, actor.y);
            actorAi_[i].defusing = false;
        } else {
            eligible = BotCanDefuse(i, actor);
            actorAi_[i].defusing = eligible;
            if (eligible) actor.vx = actor.vy = 0.0f;
        }
        updateDefuser(actor, eligible);
    }

    if (winner != kInvalidActorId) {
        for (size_t i = 0; i < actors_.size(); ++i) {
            actors_[i].defuseProgress = 0.0f;
            actors_[i].plantProgress = 0.0f;
            if (i < actorAi_.size()) actorAi_[i].defusing = false;
        }
        activeDefuser_ = kInvalidActorId;
        defuseProgress_ = defuseRemaining_ = 0.0f;
        bombPlanted_ = false;
        bombTimer_ = 0.0f;
        EmitSound(SoundEventType::BombDefused, bombX_, bombY_, Team::CT);
        {
            SimEvent stopEv;
            stopEv.type = SimEventType::BombEvent;
            stopEv.bombKind = BombEventKind::StopAllObjectiveAudio;
            EmitSimEvent(std::move(stopEv));

            SimEvent defEv;
            defEv.type = SimEventType::BombEvent;
            defEv.bombKind = BombEventKind::Defused;
            defEv.actorId = winner;
            defEv.x = bombX_;
            defEv.y = bombY_;
            EmitSimEvent(std::move(defEv));
        }
        if (Player* successful = FindActor(winner)) {
            successful->money += economy_.GrantDefuse();
            economy_.Clamp(successful->money);
            const bool kitWin = successful->hasDefuseKit;
            std::cout << "[Defuse] #" << winner << " success kit=" << (kitWin ? 1 : 0)
                      << " dur=" << (kitWin ? 4 : 10) << std::endl;
        }
        ApplyRoundResult(Team::CT);
        round_.EndRound(Team::CT);
    } else {
        bombTimer_ -= dt;
        if (bombTimer_ <= 0.0f) DetonateBomb();
    }
}

void CS2DSimulation::PlantBomb(float x, float y) {
    for (size_t i = 0; i < actors_.size(); ++i) {
        actors_[i].defuseProgress = 0.0f;
        actors_[i].plantProgress = 0.0f;
        if (i < actorAi_.size()) actorAi_[i].defusing = false;
    }
    activeDefuser_ = kInvalidActorId;
    defuseProgress_ = defuseRemaining_ = 0.0f;

    bombPlanter_ = bombCarrier_;
    bombCarrier_ = kInvalidActorId;
    for (auto& actor : actors_) {
        actor.holdingC4 = false;
        actor.plantProgress = 0.0f;
        actor.weapons[4] = WeaponId::None;
        actor.equipment[4] = {};
        if (actor.weaponSlot == 4) EquipFallbackWeaponFor(actor);
    }
    bombPlanted_ = true;
    bombX_ = x;
    bombY_ = y;

    {
        const int bonus = economy_.GrantPlant();
        for (auto& actor : actors_) {
            if (!actor.IsActive() || actor.team != Team::T) continue;
            actor.money += bonus;
            economy_.Clamp(actor.money);
        }
        std::cout << "[Economy] C4 下包 → T 全员 +$" << bonus << std::endl;
    }

    {
        const std::string plantArea = world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
        CoachOnEvent(Team::T,  CoachEventType::BombPlanted, plantArea);
        CoachOnEvent(Team::CT, CoachEventType::BombPlanted, plantArea);
        std::cout << "[Coach] BombPlanted area="
                  << (plantArea.empty() ? "(empty)" : plantArea) << std::endl;
    }
    bombTimer_ = BOMB_FUSE;
    bombBeepTimer_ = 0.0f;
    defuseProgress_ = 0.0f;
    EmitSound(SoundEventType::BombPlanted, bombX_, bombY_, Team::T);
    std::cout << "[CS2D] C4 已安装! " << static_cast<int>(BOMB_FUSE) << " 秒后爆炸" << std::endl;

    SimEvent stopEv;
    stopEv.type = SimEventType::BombEvent;
    stopEv.bombKind = BombEventKind::StopAllObjectiveAudio;
    EmitSimEvent(std::move(stopEv));

    SimEvent ev;
    ev.type = SimEventType::BombEvent;
    ev.bombKind = BombEventKind::Planted;
    ev.actorId = bombPlanter_;
    ev.x = bombX_;
    ev.y = bombY_;
    EmitSimEvent(std::move(ev));
}

void CS2DSimulation::DetonateBomb() {
    for (size_t i = 0; i < actors_.size(); ++i) {
        actors_[i].defuseProgress = 0.0f;
        actors_[i].plantProgress = 0.0f;
        if (i < actorAi_.size()) actorAi_[i].defusing = false;
    }
    activeDefuser_ = kInvalidActorId;
    defuseProgress_ = defuseRemaining_ = 0.0f;
    bombPlanted_ = false;
    bombTimer_ = 0.0f;
    std::cout << "[CS2D] C4 爆炸! T 获胜本回合" << std::endl;
    EmitSound(SoundEventType::Explosion, bombX_, bombY_, Team::T, kEnvironmentActorId, 3200.0f);

    constexpr float RADIUS = Tuning::BombBlastRadius;
    for (auto& p : actors_) {
        if (!p.IsActive() || !p.alive) continue;
        const float dx = p.x - bombX_;
        const float dy = p.y - bombY_;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > RADIUS) continue;
        const float f = 1.0f - d / RADIUS;
        if (ApplyDamage(p, static_cast<int>(200.0f * f), 1.0f)) {
            OnCombatantKilled(p, DamageOwner(bombPlanter_), WeaponId::C4, c4EquipmentId_, Vec2{bombX_, bombY_});
        }
    }

    {
        SimEvent stopEv;
        stopEv.type = SimEventType::BombEvent;
        stopEv.bombKind = BombEventKind::StopAllObjectiveAudio;
        EmitSimEvent(std::move(stopEv));

        SimEvent ev;
        ev.type = SimEventType::BombEvent;
        ev.bombKind = BombEventKind::Exploded;
        ev.actorId = bombPlanter_;
        ev.x = bombX_;
        ev.y = bombY_;
        EmitSimEvent(std::move(ev));
    }

    ApplyRoundResult(Team::T);
    round_.EndRound(Team::T);
}

void CS2DSimulation::AssignBombCarrier() {
    bombCarrier_ = kInvalidActorId;
    if (mode_ != MatchMode::BombDefusal) return;

    std::vector<ActorId> tHumans;
    std::vector<ActorId> tBots;
    for (const auto& a : actors_) {
        if (!a.IsActive() || a.team != Team::T) continue;
        if (a.IsHuman()) tHumans.push_back(a.id);
        else tBots.push_back(a.id);
    }

    if (tHumans.size() == 1) {
        if (tBots.empty() || (rng_() % 100) < 60) {
            bombCarrier_ = tHumans[0];
        } else {
            bombCarrier_ = tBots[rng_() % tBots.size()];
        }
    } else if (!tHumans.empty()) {
        std::vector<ActorId> allT = tHumans;
        allT.insert(allT.end(), tBots.begin(), tBots.end());
        bombCarrier_ = allT[rng_() % allT.size()];
    } else if (!tBots.empty()) {
        bombCarrier_ = tBots[rng_() % tBots.size()];
    }

    if (Player* carrier = FindActor(bombCarrier_)) {
        if (carrier->IsHuman()) {
            carrier->weapons[4] = WeaponId::C4;
            carrier->equipment[4] = {};
            carrier->equipment[4].id = c4EquipmentId_;
            carrier->equipment[4].epoch = purchaseEpoch_;
        }
    }
}
