#include "CS2DGame.hpp"
#include <algorithm>
#include <iostream>

CS2DGame::CS2DGame() = default;

void CS2DGame::FlushEventsToView(IAudioSink* audio) {
    auto events = DrainEvents();
    if (!events.empty()) {
        view_.ConsumeEvents(events, *this, audio);
    }
}

bool CS2DGame::LoadMapHeadless(const std::string& resolvedTMJPath, bool resetMatch,
                               float visionRadius) {
    const bool ok = CS2DSimulation::LoadMapHeadless(resolvedTMJPath, resetMatch, visionRadius);
    if (ok) {
        FlushEventsToView();
    }
    return ok;
}

void CS2DGame::Reset() {
    CS2DSimulation::Reset();
    FlushEventsToView();
}

void CS2DGame::RestartMatchWithTeam(Team t) {
    CS2DSimulation::RestartMatchWithTeam(t);
    FlushEventsToView();
}

bool CS2DGame::TryBuyFor(ActorId actorId, WeaponId id, IAudioSink* audio) {
    const bool ok = CS2DSimulation::TryBuyFor(actorId, id);
    FlushEventsToView(audio);
    return ok;
}

bool CS2DGame::TryBuyArmorFor(ActorId actorId, int tier, IAudioSink* audio) {
    const bool ok = CS2DSimulation::TryBuyArmorFor(actorId, tier);
    FlushEventsToView(audio);
    return ok;
}

Presentation::BuyResult CS2DGame::ExecutePurchaseFor(ActorId actorId,
                                                      const Presentation::BuyRequest& request,
                                                      IAudioSink* audio) {
    const auto result = CS2DSimulation::ExecutePurchaseFor(actorId, request);
    FlushEventsToView(audio);
    return result;
}

void CS2DGame::TryPickupDropsFor(ActorId actorId, bool manual, IAudioSink* audio) {
    CS2DSimulation::TryPickupDropsFor(actorId, manual);
    FlushEventsToView(audio);
}

void CS2DGame::UpdateMultiActor(float dt,
                                const std::vector<std::pair<ActorId, ActorInput>>& inputs,
                                IAudioSink* audio) {
    if (!world_) return;
    if (audio) view_.SetAudioSink(audio);

    const ActorInput* localIn = nullptr;
    for (const auto& entry : inputs) {
        if (entry.first == localActorId_) {
            localIn = &entry.second;
            break;
        }
    }

    view_.BeginTick(dt, *this, localIn);

    std::vector<std::pair<ActorId, ActorInput>> simInputs = inputs;
    if (view_.AimFollowsPlayer()) {
        for (auto& entry : simInputs) {
            if (entry.first == localActorId_) {
                entry.second.hasAimWorld   = true;
                entry.second.aimIsRelative = true;
                entry.second.aimWorldX     = view_.aimOffsetX_;
                entry.second.aimWorldY     = view_.aimOffsetY_;
                break;
            }
        }
    }

    const bool canMove = (mode_ == MatchMode::DeathMatch) || !round_.IsFrozen();
    const bool localWasAlive = LocalActor().alive;

    CS2DSimulation::StepActors(dt, simInputs);

    if (localWasAlive && LocalActor().alive && canMove) {
        view_.UpdateMotionFeedback(dt, *this);
    }

    FlushEventsToView(audio);
    view_.EndTick(dt, *this, canMove);
}

void CS2DGame::Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                      bool moveUp, bool moveLeft, bool moveDown, bool moveRight,
                      bool stealth, bool reload, bool interact, IAudioSink* audio) {
    ActorInput localInput;
    localInput.moveUp      = moveUp;
    localInput.moveLeft    = moveLeft;
    localInput.moveDown    = moveDown;
    localInput.moveRight   = moveRight;
    localInput.stealth     = stealth;
    localInput.fire        = mouseLeftDown;
    localInput.reload      = reload;
    localInput.interact    = interact;
    localInput.hasAimWorld = true;
    localInput.aimWorldX   = mouseWorldX;
    localInput.aimWorldY   = mouseWorldY;
    std::vector<std::pair<ActorId, ActorInput>> inputs;
    inputs.emplace_back(localActorId_, localInput);
    UpdateMultiActor(dt, inputs, audio);
}

void CS2DGame::DebugKillPlayerForTest() {
    auto& me = LocalActor();
    if (!me.alive) return;
    Player attacker;
    attacker.id = kInvalidActorId;
    attacker.name = "测试";
    attacker.team = TeamManager::Opponent(me.team);
    OnCombatantKilled(me, view_.GetAudioSink(), &attacker, WeaponId::AK47);
}

// ============================================================================
// 测试友元兼容包装：将仿真调用与 SimEvent → CS2DClientView 刷新合二为一
// ============================================================================

void CS2DGame::UpdatePlayerMovement(Player& actor, float dt,
                                    bool w, bool a, bool s, bool d, bool stealth) {
    if (&actor == &LocalActor()) {
        SetActorScopeView(localActorId_, view_.scopeLevel_, view_.scopeEngagement_);
    }
    CS2DSimulation::UpdatePlayerMovement(actor, dt, w, a, s, d, stealth);
    if (&actor == &LocalActor()) {
        view_.UpdateMotionFeedback(dt, *this);
    }
}

void CS2DGame::UpdatePlayerShooting(Player& actor, float dt, bool mouseDown,
                                    float worldMX, float worldMY, IAudioSink* audio) {
    if (&actor == &LocalActor()) {
        SetActorScopeView(localActorId_, view_.scopeLevel_, view_.scopeEngagement_);
    }
    CS2DSimulation::UpdatePlayerShooting(actor, dt, mouseDown, worldMX, worldMY);
    FlushEventsToView(audio);
}

void CS2DGame::UpdateBullets(float dt, IAudioSink* audio) {
    CS2DSimulation::UpdateBullets(dt);
    FlushEventsToView(audio);
}

void CS2DGame::UpdateGrenades(float dt, IAudioSink* audio) {
    CS2DSimulation::UpdateGrenades(dt);
    FlushEventsToView(audio);
}

void CS2DGame::ThrowGrenade(Player& thrower, float aimAngle, IAudioSink* audio,
                            float targetDistance) {
    CS2DSimulation::ThrowGrenade(thrower, aimAngle, targetDistance);
    FlushEventsToView(audio);
}

void CS2DGame::ExplodeGrenade(const Grenade& g, IAudioSink* audio) {
    CS2DSimulation::ExplodeGrenade(g);
    FlushEventsToView(audio);
}

void CS2DGame::StartReload(Player& p, IAudioSink* audio) {
    CS2DSimulation::StartReload(p);
    FlushEventsToView(audio);
}

void CS2DGame::UpdateRoundFlow(float dt, IAudioSink* audio) {
    CS2DSimulation::UpdateRoundFlow(dt);
    FlushEventsToView(audio);
}

void CS2DGame::BeginNewRound(IAudioSink* audio) {
    CS2DSimulation::BeginNewRound();
    FlushEventsToView(audio);
}

void CS2DGame::RespawnAllForRound(bool carryWeapons) {
    CS2DSimulation::RespawnAllForRound(carryWeapons);
    FlushEventsToView();
}

void CS2DGame::CheckWinConditions(IAudioSink* audio) {
    CS2DSimulation::CheckWinConditions();
    FlushEventsToView(audio);
}

void CS2DGame::ApplyRoundResult(Team winner, IAudioSink* audio) {
    CS2DSimulation::ApplyRoundResult(winner);
    FlushEventsToView(audio);
}

void CS2DGame::UpdateObjective(float dt, IAudioSink* audio) {
    CS2DSimulation::UpdateObjective(dt);
    FlushEventsToView(audio);
}

void CS2DGame::PlantBomb(float x, float y, IAudioSink* audio) {
    CS2DSimulation::PlantBomb(x, y);
    FlushEventsToView(audio);
}

void CS2DGame::DetonateBomb(IAudioSink* audio) {
    CS2DSimulation::DetonateBomb();
    FlushEventsToView(audio);
}

void CS2DGame::OnCombatantKilled(Player& victim, IAudioSink* audio, Player* killer,
                                 WeaponId weapon, std::uint64_t equipmentId,
                                 std::optional<Vec2> sourcePosition) {
    CS2DSimulation::OnCombatantKilled(victim, killer, weapon, equipmentId, sourcePosition);
    FlushEventsToView(audio);
}

int CS2DGame::AccumulateFootstep(Player& actor, IAudioSink* audio, bool positional) {
    const int steps = CS2DSimulation::AccumulateFootstep(actor, positional);
    FlushEventsToView(audio);
    return steps;
}
