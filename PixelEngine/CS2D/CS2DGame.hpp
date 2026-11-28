#pragma once
#include "CS2DSimulation.hpp"
#include "CS2DClientView.hpp"
#include "../Core/IAudioSink.hpp"
#include "../Core/PresentationContract.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class CS2DRenderer;
struct SpriteDrawParams;

// ============================================================================
// CS2DGame — 兼容单机、渲染客户端与现有测试的薄集成门面
//
// 继承自纯权威仿真 CS2DSimulation，内部聚合表现层 CS2DClientView：
//   - 每帧按 BeginTick → StepActors → ConsumeEvents → UpdateMotionFeedback → EndTick 驱动；
//   - 暴露 CS2DSimulation* / CS2DClientView* 访问器供新架构测试与多人层直接对接；
//   - 保留所有旧单机与测试调用的兼容转发方法（无行为变更）。
// ============================================================================
class CS2DGame : public CS2DSimulation {
public:
    using SpectateMode      = CS2DClientView::SpectateMode;
    using DeathPresentation = CS2DClientView::DeathPresentation;
    using ActorView         = CS2DClientView::ActorView;
    using DamageDirection   = Presentation::DamageDirection;
    static constexpr float DeathFreezeSeconds = CS2DClientView::DeathFreezeSeconds;

    CS2DGame();
    ~CS2DGame() = default;

    // ---- 分离视图访问器 ----
    CS2DSimulation&       Simulation()       { return *this; }
    const CS2DSimulation& Simulation() const { return *this; }
    CS2DClientView&       ClientView()       { return view_; }
    const CS2DClientView& ClientView() const { return view_; }

    // 将当前积压事件排空并交给表现层消费
    void FlushEventsToView(IAudioSink* audio = nullptr);

    // ---- 地图加载 / 重置 ----
    bool LoadMapHeadless(const std::string& resolvedTMJPath, bool resetMatch = true,
                         float visionRadius = Tuning::VisionRadius);
    bool LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer,
                 bool resetMatch = true);
    void Reset();
    void RestartMatchWithTeam(Team t);

    // ---- 音频 / 目标通道控制 ----
    void SetAudioSink(IAudioSink* audio) { view_.SetAudioSink(audio); }
    void SetObjectiveAudioPaused(bool paused, IAudioSink* audio = nullptr) {
        view_.SetObjectiveAudioPaused(paused, audio);
    }
    void StopObjectiveAudio(IAudioSink* audio = nullptr) {
        view_.StopObjectiveAudio(audio);
    }
    static SoundEffect ReloadSoundFor(WeaponId id) {
        return CS2DClientView::ReloadSoundFor(id);
    }

    // ---- 每帧驱动 ----
    void UpdateMultiActor(float dt, const std::vector<std::pair<ActorId, ActorInput>>& inputs,
                          IAudioSink* audio = nullptr);
    void Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                bool moveUp, bool moveLeft, bool moveDown, bool moveRight,
                bool stealth, bool reload, bool interact,
                IAudioSink* audio = nullptr);

    // ---- 渲染接线（实现在 CS2DGameRender.cpp，不在 PixelSim 内）----
    void RenderMapPreview(int viewW, int viewH, float centerX, float centerY, float zoom,
                          bool clampToMap = false);
    void PrepareCamera(int viewW, int viewH, float interpolation = 1.0f);
    void Render(int viewW = 800, int viewH = 600);
    CS2DRenderer* GetRenderer() const { return renderer_; }

    void DebugKillPlayerForTest();

    // ---- 兼容单机便捷接口（操作本地 Actor 或桥接 IAudioSink*）----
    using CS2DSimulation::TryBuyFor;
    using CS2DSimulation::TryBuyArmorFor;
    using CS2DSimulation::ExecutePurchaseFor;
    using CS2DSimulation::TryPickupDropsFor;
    using CS2DSimulation::DropWeaponToGround;
    using CS2DSimulation::QuickSwitchWeaponFor;
    using CS2DSimulation::ToggleHoldC4For;
    using CS2DSimulation::EquipFallbackWeaponFor;
    using CS2DSimulation::SwitchWeaponSlotFor;

    void QuickSwitchWeapon() {
        CS2DSimulation::QuickSwitchWeaponFor(localActorId_);
        FlushEventsToView();
    }
    void ToggleHoldC4() {
        CS2DSimulation::ToggleHoldC4For(localActorId_);
        FlushEventsToView();
    }
    void EquipFallbackWeapon() {
        CS2DSimulation::EquipFallbackWeaponFor(LocalActor());
        FlushEventsToView();
    }
    void SwitchWeaponSlot(int slot, int grenadeIndex = -1) {
        CS2DSimulation::SwitchWeaponSlotFor(localActorId_, slot, grenadeIndex);
        FlushEventsToView();
    }
    void DropWeaponToGround(int slot, float x, float y) {
        CS2DSimulation::DropWeaponToGround(LocalActor(), slot, x, y);
    }
    void DropCurrentWeapon() {
        CS2DSimulation::DropCurrentWeaponFor(localActorId_);
        FlushEventsToView();
    }

    bool TryBuyFor(ActorId actorId, WeaponId id, IAudioSink* audio);
    bool TryBuy(WeaponId id, IAudioSink* audio = nullptr) {
        return TryBuyFor(localActorId_, id, audio);
    }
    bool TryBuyArmorFor(ActorId actorId, int tier, IAudioSink* audio);
    bool TryBuyArmor(int tier, IAudioSink* audio = nullptr) {
        return TryBuyArmorFor(localActorId_, tier, audio);
    }
    RefundQuote EvaluateRefund(const Presentation::BuyRequest& request) const {
        return CS2DSimulation::EvaluateRefundFor(localActorId_, request);
    }
    bool ExecuteRefund(const Presentation::BuyRequest& request) {
        const bool ok = CS2DSimulation::ExecuteRefundFor(localActorId_, request);
        FlushEventsToView();
        return ok;
    }
    Presentation::BuyCode EvaluatePurchase(const Presentation::BuyRequest& request) const {
        return CS2DSimulation::EvaluatePurchaseFor(localActorId_, request);
    }
    Presentation::BuyResult ExecutePurchaseFor(ActorId actorId,
                                               const Presentation::BuyRequest& request,
                                               IAudioSink* audio);
    Presentation::BuyResult ExecutePurchase(const Presentation::BuyRequest& request,
                                            IAudioSink* audio = nullptr) {
        return ExecutePurchaseFor(localActorId_, request, audio);
    }
    void TryPickupDropsFor(ActorId actorId, bool manual, IAudioSink* audio);
    void TryPickupDrops(bool manual, IAudioSink* audio = nullptr) {
        TryPickupDropsFor(localActorId_, manual, audio);
    }

    // ---- 表现层访问与控制转发 ----
    bool IsDeathFreeze() const { return view_.IsDeathFreeze(*this); }
    const DeathPresentation& GetDeathPresentation() const { return view_.GetDeathPresentation(); }
    bool IsDeathCam() const { return view_.IsDeathCam(); }

    HitMarkerKind GetHitMarkerKind() const { return view_.GetHitMarkerKind(); }
    float GetHitMarkerTimer()   const { return view_.GetHitMarkerTimer(); }
    float GetDryFireHintTimer() const { return view_.GetDryFireHintTimer(); }
    float GetReloadProgress()   const { return view_.GetReloadProgress(*this); }
    const std::array<DamageDirection, 4>& GetDamageDirections() const {
        return view_.GetDamageDirections();
    }
    float GetDamageFlashTimer() const { return view_.GetDamageFlashTimer(); }
    const std::string& GetAnnouncement() const { return view_.GetAnnouncement(); }
    float GetAnnouncementTimer() const { return view_.GetAnnouncementTimer(); }
    float GetAimMotionBlur() const { return view_.GetAimMotionBlur(); }
    float GetCrosshairBloom() const { return view_.GetCrosshairBloom(*this); }
    float GetMouseSensitivity() const { return view_.GetMouseSensitivity(); }
    void  SetMouseSensitivity(float s) { view_.SetMouseSensitivity(s); }
    unsigned PlayerSpawnVersion() const { return view_.PlayerSpawnVersion(); }

    void SetAimTarget(float x, float y, bool followsPlayer = false) {
        view_.SetAimTarget(*this, x, y, followsPlayer);
    }
    ActorView GetActorView(const Player& actor) const { return view_.GetActorView(*this, actor); }
    float GetRenderAimX() const { return view_.GetRenderAimX(*this); }
    float GetRenderAimY() const { return view_.GetRenderAimY(*this); }
    float GetRenderAimAngle() const { return view_.GetRenderAimAngle(*this); }
    bool  AimFollowsPlayer() const { return view_.AimFollowsPlayer(); }
    bool  CameraPrepared() const { return view_.CameraPrepared(); }
    bool  CameraScopeTransitionComplete() const { return view_.CameraScopeTransitionComplete(); }
    static float ScopeCameraShiftForZoom(float zoom) {
        return CS2DClientView::ScopeCameraShiftForZoom(zoom);
    }
    bool  EdgeZoomRequested() const { return view_.EdgeZoomRequested(); }
    float EdgeZoomAmount() const { return view_.EdgeZoomAmount(); }
    void  SetEdgeZoomRequested(bool requested) { view_.SetEdgeZoomRequested(requested); }
    void  SetCameraDistanceTier(int tier) { view_.SetCameraDistanceTier(tier); }

    void UpdateScopeInput(bool rightClicked, bool gameplayInputAllowed) {
        view_.UpdateScopeInput(*this, rightClicked, gameplayInputAllowed);
    }
    Presentation::ScopeView GetScopeView() const { return view_.GetScopeView(*this); }
    Presentation::ScopeView GetRenderScopeView() const { return view_.GetRenderScopeView(*this); }
    void ResetScope(Presentation::ScopeResetReason reason) { view_.ResetScope(*this, reason); }
    static bool IsScopeSupported(WeaponId id) { return CS2DClientView::IsScopeSupported(id); }
    void SetDelayedScopeClose(bool on) { view_.SetDelayedScopeClose(on); }
    bool IsDelayedScopeClose() const { return view_.IsDelayedScopeClose(); }
    void SetScopeCrosshair(float thickness, bool dynamicSpread) {
        view_.SetScopeCrosshair(thickness, dynamicSpread);
    }
    float ScopeCrosshairThickness() const { return view_.ScopeCrosshairThickness(); }
    bool  ScopeCrosshairDynamic()   const { return view_.ScopeCrosshairDynamic(); }

    bool         IsSpectating() const { return view_.IsSpectating(*this); }
    SpectateMode GetSpectateMode() const { return view_.GetSpectateMode(); }
    ActorId      GetSpectateTarget() const { return view_.GetSpectateTarget(); }
    void         RequestSpectateToggle() { view_.RequestSpectateToggle(); }
    const Player* GetSpectatedPlayer() const { return view_.GetSpectatedPlayer(*this); }
    std::string  GetSpectatedName() const { return view_.GetSpectatedName(*this); }
    void SpectatorToggle() { view_.SpectatorToggle(*this); }
    void SpectatorCycle(int dir) { view_.SpectatorCycle(*this, dir); }
    void SpectatorMoveFree(float dx, float dy, float dt) { view_.SpectatorMoveFree(*this, dx, dy, dt); }
    void SpectatorBegin() { view_.SpectatorBegin(*this); }
    void SpectatorEnd() { view_.SpectatorEnd(); }

    float ListenerX() const { return view_.ListenerX(*this); }
    float ListenerY() const { return view_.ListenerY(*this); }

protected:
    friend struct CS2DGameTestAccess;

    void DrawWorldPass(bool lens = false);
    SpriteDrawParams MakeActorDrawParams(const Player& actor, const Player* observer) const;

    // 兼容现有白盒测试的包装方法（内部委托 CS2DSimulation 并刷新 SimEvent 到 view_）
    void UpdateSpectatorState() { view_.UpdateSpectatorState(*this); }
    std::size_t CollectFreeSpectateObservers(VisibilityQuery::Observer* out,
                                             std::size_t capacity) const {
        return view_.CollectFreeSpectateObservers(*this, out, capacity);
    }
    void TickScope(float dt) { view_.TickScope(dt, *this); }

    void SpawnPlayer(Player& actor) {
        CS2DSimulation::SpawnPlayer(actor);
        FlushEventsToView();
    }
    void SpawnPlayer() { SpawnPlayer(LocalActor()); }

    void UpdatePlayerMovement(Player& actor, float dt, bool w, bool a, bool s, bool d, bool stealth);
    void UpdatePlayerShooting(Player& actor, float dt, bool mouseDown, float worldMX, float worldMY,
                              IAudioSink* audio = nullptr);
    using CS2DSimulation::UpdateBOTs;
    void UpdateBOTs(float dt, IAudioSink* audio) {
        CS2DSimulation::UpdateBOTs(dt);
        FlushEventsToView(audio);
    }
    void UpdateBullets(float dt, IAudioSink* audio = nullptr);
    void UpdateGrenades(float dt, IAudioSink* audio = nullptr);
    void ThrowGrenade(Player& thrower, float aimAngle, IAudioSink* audio = nullptr,
                      float targetDistance = -1.0f);
    void ExplodeGrenade(const Grenade& g, IAudioSink* audio = nullptr);
    void StartReload(Player& p, IAudioSink* audio = nullptr);
    void UpdateRoundFlow(float dt, IAudioSink* audio = nullptr);
    void BeginNewRound(IAudioSink* audio = nullptr);
    void RespawnAllForRound(bool carryWeapons = true);
    void CheckWinConditions(IAudioSink* audio = nullptr);
    void ApplyRoundResult(Team winner, IAudioSink* audio = nullptr);
    void UpdateObjective(float dt, IAudioSink* audio = nullptr);
    void PlantBomb(float x, float y, IAudioSink* audio = nullptr);
    void DetonateBomb(IAudioSink* audio = nullptr);
    void OnCombatantKilled(Player& victim, IAudioSink* audio,
                           Player* killer = nullptr, WeaponId weapon = WeaponId::None,
                           std::uint64_t equipmentId = 0,
                           std::optional<Vec2> sourcePosition = std::nullopt);
    int  AccumulateFootstep(Player& actor, IAudioSink* audio, bool positional);

    CS2DClientView view_;
    CS2DRenderer*  renderer_ = nullptr;
};
