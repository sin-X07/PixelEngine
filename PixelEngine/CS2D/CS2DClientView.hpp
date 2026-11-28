#pragma once
#include "CS2DSimulation.hpp"
#include "../Core/PresentationContract.hpp"
#include "../Core/SimEvents.hpp"
#include "../Core/IAudioSink.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

// ============================================================================
// CS2DClientView — 纯表现层状态、观战系统与 SimEvent 音频/反馈路由器
//
// 从 CS2DSimulation 彻底解耦：只负责相机插值、开镜过渡、命中/受伤 HUD 反馈、
// 死亡展示、观战视角切换与 IAudioSink* 音频触发（含多目标拆包保留通道）。
// ============================================================================
class CS2DClientView {
public:
    enum class SpectateMode { None = 0, Follow, Free, Bomb };

    struct DeathPresentation {
        CS2DSimulation::KillFeedEntry event;
        float remaining = 0.0f;
    };
    static constexpr float DeathFreezeSeconds = 1.8f;

    struct ActorView {
        float x = 0.0f, y = 0.0f;
        bool alive = false;
        float aimAngle = 0.0f, animTimer = 0.0f;
        CS2DSimulation::AnimState animState = CS2DSimulation::AnimState::Idle;
    };

    CS2DClientView();

    // ---- 每帧流水线 ----
    void BeginTick(float dt, CS2DSimulation& sim, const ActorInput* localInput);
    void ConsumeEvents(const std::vector<SimEvent>& events, const CS2DSimulation& sim, IAudioSink* audio);
    void EndTick(float dt, const CS2DSimulation& sim, bool canMove);
    void UpdateMotionFeedback(float dt, const CS2DSimulation& sim);
    void OnLocalActorSpawned(const CS2DSimulation& sim);
    void OnNewMatchReset(const CS2DSimulation& sim, IAudioSink* audio);

    // ---- 音频 / 目标通道控制 ----
    void SetAudioSink(IAudioSink* audio) { audioSink_ = audio; }
    IAudioSink* GetAudioSink() const { return audioSink_; }
    void SetObjectiveAudioPaused(bool paused, IAudioSink* audio = nullptr);
    void StopObjectiveAudio(IAudioSink* audio = nullptr);
    static SoundEffect ReloadSoundFor(WeaponId id);

    // ---- 死亡展示 / 反馈只读访问 ----
    bool IsDeathFreeze(const CS2DSimulation& sim) const {
        return !sim.LocalActor().alive && deathPresentation_.remaining > 0.0f;
    }
    const DeathPresentation& GetDeathPresentation() const { return deathPresentation_; }
    bool IsDeathCam() const { return deathCam_; }

    HitMarkerKind GetHitMarkerKind() const { return hitMarkerKind_; }
    float GetHitMarkerTimer() const { return hitMarkerTimer_; }
    float GetDryFireHintTimer() const { return dryFireHintTimer_; }
    const std::array<Presentation::DamageDirection, 4>& GetDamageDirections() const { return damageDirections_; }
    float GetDamageFlashTimer() const { return damageFlashTimer_; }
    const std::string& GetAnnouncement() const { return announcement_; }
    float GetAnnouncementTimer() const { return announcementTimer_; }

    // ---- 移动扩散 / 镜头与准星 ----
    float GetAimMotionBlur() const { return aimMotionBlur_; }
    float GetCrosshairBloom(const CS2DSimulation& sim) const {
        return sim.LocalActor().alive ? crosshairBloom_ : 0.0f;
    }
    float GetMouseSensitivity() const { return mouseSensitivity_; }
    void  SetMouseSensitivity(float s) { mouseSensitivity_ = std::clamp(s, 0.1f, 5.0f); }
    unsigned PlayerSpawnVersion() const { return playerSpawnVersion_; }

    void SetAimTarget(CS2DSimulation& sim, float x, float y, bool followsPlayer = false);
    ActorView GetActorView(const CS2DSimulation& sim, const CS2DSimulation::Player& actor) const;
    float GetRenderAimX(const CS2DSimulation& sim) const;
    float GetRenderAimY(const CS2DSimulation& sim) const;
    float GetRenderAimAngle(const CS2DSimulation& sim) const;
    bool  AimFollowsPlayer() const { return aimFollowsPlayer_; }

    bool EdgeZoomRequested() const { return edgeZoomRequested_; }
    float EdgeZoomAmount() const { return edgeZoom_; }
    void SetEdgeZoomRequested(bool requested) {
        edgeZoomRequested_ = requested;
        cameraDistanceTier_ = requested ? 1 : 0;
    }
    void SetCameraDistanceTier(int tier) {
        cameraDistanceTier_ = std::clamp(tier, 0, 2);
        edgeZoomRequested_ = tier > 0;
    }
    bool CameraPrepared() const { return cameraPrepared_; }
    bool CameraScopeTransitionComplete() const { return cameraScopeSettled_; }
    static float ScopeCameraShiftForZoom(float zoom);

    // ---- 开镜 ----
    void UpdateScopeInput(CS2DSimulation& sim, bool rightClicked, bool gameplayInputAllowed);
    void TickScope(float dt, CS2DSimulation& sim);
    Presentation::ScopeView GetScopeView(const CS2DSimulation& sim) const;
    Presentation::ScopeView GetRenderScopeView(const CS2DSimulation& sim) const;
    void ResetScope(const CS2DSimulation& sim, Presentation::ScopeResetReason reason);
    static bool IsScopeSupported(WeaponId id);

    void SetDelayedScopeClose(bool on) { delayedScopeClose_ = on; }
    bool IsDelayedScopeClose() const { return delayedScopeClose_; }
    void SetScopeCrosshair(float thickness, bool dynamicSpread) {
        scopeCrossThickness_ = std::clamp(thickness, 1.0f, 8.0f);
        scopeCrossDynamic_   = dynamicSpread;
    }
    float ScopeCrosshairThickness() const { return scopeCrossThickness_; }
    bool  ScopeCrosshairDynamic()   const { return scopeCrossDynamic_; }

    // ---- 观战子系统 ----
    bool         IsSpectating(const CS2DSimulation& sim) const {
        return specMode_ != SpectateMode::None && !sim.LocalActor().alive;
    }
    SpectateMode GetSpectateMode() const { return specMode_; }
    ActorId      GetSpectateTarget() const { return specTarget_; }
    void         RequestSpectateToggle() { spectateTogglePending_ = true; }
    const CS2DSimulation::Player* GetSpectatedPlayer(const CS2DSimulation& sim) const;
    std::string GetSpectatedName(const CS2DSimulation& sim) const;

    void SpectatorBegin(const CS2DSimulation& sim);
    void SpectatorEnd();
    void UpdateSpectatorState(const CS2DSimulation& sim);
    void SpectatorToggle(const CS2DSimulation& sim);
    void SpectatorCycle(const CS2DSimulation& sim, int dir);
    void SpectatorMoveFree(const CS2DSimulation& sim, float dx, float dy, float dt);
    std::size_t CollectFreeSpectateObservers(const CS2DSimulation& sim,
                                             VisibilityQuery::Observer* out,
                                             std::size_t capacity) const;

    float ListenerX(const CS2DSimulation& sim) const;
    float ListenerY(const CS2DSimulation& sim) const;

    float GetReloadProgress(const CS2DSimulation& sim) const {
        const auto* observed = GetSpectatedPlayer(sim);
        const auto& actor = observed ? *observed : sim.LocalActor();
        if (!actor.alive || !actor.reloading || actor.CurrentWeapon().reloadTime <= 0.0f) return 0.0f;
        return std::clamp(1.0f - actor.reloadTimer / actor.CurrentWeapon().reloadTime, 0.0f, 1.0f);
    }

private:
    friend class CS2DGame;
    friend struct CS2DGameTestAccess;

    ActorId NextTeammateBot(const CS2DSimulation& sim, ActorId from, int dir) const;

    IAudioSink* audioSink_ = nullptr;
    std::array<int, kMaxActors> plantingChannels_{};
    std::array<int, kMaxActors> defuseChannels_{};

    std::vector<ActorView> previousActorViews_;
    float renderInterpolation_ = 1.0f;
    bool  viewHistoryValid_ = false;
    bool  aimFollowsPlayer_ = false;
    float aimOffsetX_ = Tuning::InitialAimDistance, aimOffsetY_ = 0.0f;
    float recoilShake_ = 0.0f, recoilShakeAge_ = 0.0f;
    unsigned playerSpawnVersion_ = 0;
    bool  cameraPrepared_ = false, cameraScopeSettled_ = false;
    bool  edgeZoomRequested_ = false;
    int   cameraDistanceTier_ = 0;
    float edgeZoom_ = 0.0f;
    float previousEdgeZoom_ = 0.0f;
    float edgeZoomFrom_ = 0.0f, edgeZoomTo_ = 0.0f, edgeZoomTime_ = 1.0f;

    float previousScopeZoom_ = 1.0f;
    float previousScopeEngagement_ = 0.0f;
    float aimMotionBlur_ = 0.0f;
    float crosshairBloom_ = 0.0f;

    bool  spectateTogglePending_ = false;
    HitMarkerKind hitMarkerKind_ = HitMarkerKind::Normal;
    float dryFireHintTimer_ = 0.0f;
    float hitMarkerTimer_ = 0.0f;
    std::array<Presentation::DamageDirection, 4> damageDirections_{};
    float damageFlashTimer_ = 0.0f;

    std::string announcement_;
    float announcementTimer_ = 0.0f;

    SpectateMode specMode_ = SpectateMode::None;
    DeathPresentation deathPresentation_;
    ActorId specTarget_ = kInvalidActorId;
    float specFreeX_ = 0.0f, specFreeY_ = 0.0f;
    bool  deathCam_ = false;

    float mouseSensitivity_ = 1.0f;
    float scopeCrossThickness_ = 2.0f;
    bool  scopeCrossDynamic_   = false;
    bool  delayedScopeClose_   = false;
    bool  scopeCloseOnRelease_ = false;

    Presentation::ScopeView scope_{};
    int   scopeLevel_ = 0;
    float scopeEngagement_ = 0.0f;
    float scopeEngFrom_ = 0.0f, scopeEngTo_ = 0.0f, scopeEngT_ = 1.0f;
    float scopeZoom_ = 1.0f;
    float scopeZoomFrom_ = 1.0f, scopeZoomTo_ = 1.0f, scopeZoomT_ = 1.0f;
};
