#pragma once

#include "ActorTypes.hpp"
#include "PresentationContract.hpp"
#include "Types.hpp"
#include "Vec2.hpp"
#include "../CS2D/Weapons/WeaponDef.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// SimEvents — CS2DSimulation → CS2DClientView 的权威仿真事件队列
//
// 纯 C++17 值类型，零 IAudioSink / SDL / OpenGL / Renderer 依赖。
// 仿真层发射客观事实事件，客户端表现层根据本地观战视角决定音效、粒子与 HUD 反馈。
// ============================================================================

enum class HitMarkerKind : std::uint8_t {
    Normal = 0,
    Armor  = 1,
    Kill   = 2
};

enum class SimEventType : std::uint8_t {
    ShotFired = 0,
    WeaponSwitched,
    ReloadStarted,
    HitConfirm,
    DamageTaken,
    KillFeed,
    BuyResult,
    ItemPickedUp,
    GrenadeEvent,
    BombEvent,
    FootstepEvent,
    RoundEvent,
    ActorSpawned,
    ScopeReset,
    RecoilImpulse
};

enum class GrenadeEventKind : std::uint8_t {
    PullPin = 0,
    Throw,
    ExplodeHE,
    ExplodeFlash,
    ExplodeSmoke,
    IgniteFire,
    ExplodeDecoy
};

enum class BombEventKind : std::uint8_t {
    PlantActive = 0,
    PlantStopped,
    Planted,
    Beep,
    DefuseActive,
    DefuseStopped,
    Defused,
    Exploded,
    StopAllObjectiveAudio
};

enum class RoundEventKind : std::uint8_t {
    RoundStart = 0,
    RoundWin,
    MatchOver,
    MatchReset
};

struct SimEvent {
    SimEventType type = SimEventType::ShotFired;

    ActorId actorId  = kInvalidActorId;
    ActorId targetId = kInvalidActorId;

    WeaponId weapon = WeaponId::None;
    Team     team       = Team::None;
    Team     victimTeam = Team::None;

    float x = 0.0f;
    float y = 0.0f;
    float dirX = 0.0f;
    float dirY = 0.0f;
    float amount = 0.0f;

    int intParam = 0;

    bool flagA = false;
    bool flagB = false;
    bool flagC = false;
    bool flagD = false;

    GrenadeEventKind grenadeKind = GrenadeEventKind::PullPin;
    BombEventKind    bombKind    = BombEventKind::Beep;
    RoundEventKind   roundKind   = RoundEventKind::RoundStart;

    Presentation::BuyRequest       buyRequest{};
    Presentation::BuyResult        buyResult{};
    Presentation::ScopeResetReason scopeReason = Presentation::ScopeResetReason::WeaponChanged;

    std::uint64_t killEventId = 0;
    std::uint64_t equipmentId = 0;
    std::string   killerName;
    std::string   victimName;
    std::string   text;
};

class SimEventQueue {
public:
    void Push(SimEvent ev) { events_.push_back(std::move(ev)); }

    const std::vector<SimEvent>& Events() const { return events_; }

    std::vector<SimEvent> Drain() {
        std::vector<SimEvent> out;
        out.swap(events_);
        return out;
    }

    void Clear() { events_.clear(); }
    bool Empty() const { return events_.empty(); }
    std::size_t Size() const { return events_.size(); }

private:
    std::vector<SimEvent> events_;
};
