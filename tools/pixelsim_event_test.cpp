// 纯无头单元测试：验证 PIX-44 InputCmd / CS2DSimulation / SimEventQueue / CS2DClientView 拆分架构
// 零 SDL / OpenGL / AudioManager 依赖

#include "CS2D/CS2DSimulation.hpp"
#include "CS2D/CS2DClientView.hpp"
#include "Core/IAudioSink.hpp"
#include "Core/InputCmd.hpp"
#include "Core/PathUtils.hpp"
#include "Core/SimEvents.hpp"
#include "../tools/dev_replay/replay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++gPassed;
    } else {
        ++gFailed;
        std::printf("[FAIL] %s\n", name);
    }
}

std::string MapPath(const char* name = "dust_2.tmj") {
    return PathUtils::ResolveAppPath(std::string("assets/cs2d/maps/") + name);
}

class MockAudioSink : public IAudioSink {
public:
    struct SoundCall {
        SoundEffect effect = SoundEffect::CLICK;
        float volume = 1.0f;
        bool spatial = false;
        float x = 0.0f;
        float y = 0.0f;
    };

    std::vector<SoundCall> sounds;
    std::vector<WeaponId> fireSounds;
    std::vector<WeaponId> fireSoundsAt;
    std::vector<WeaponId> switchSounds;
    std::vector<WeaponId> switchSoundsAt;
    std::array<bool, 32> activeChannels{};
    std::array<bool, 32> pausedChannels{};
    std::vector<int> stoppedChannels;

    void PlaySound(SoundEffect sound, float volume = 1.0f) override {
        sounds.push_back({sound, volume, false, 0.0f, 0.0f});
    }
    void PlaySoundAt(SoundEffect sound, float wx, float wy, float /*lx*/, float /*ly*/) override {
        sounds.push_back({sound, 1.0f, true, wx, wy});
    }
    void PlayWeaponFire(WeaponId id) override { fireSounds.push_back(id); }
    void PlayWeaponFireAt(WeaponId id, float /*wx*/, float /*wy*/, float /*lx*/, float /*ly*/) override {
        fireSoundsAt.push_back(id);
    }
    void PlayWeaponSwitch(WeaponId id) override { switchSounds.push_back(id); }
    void PlayWeaponSwitchAt(WeaponId id, float /*wx*/, float /*wy*/, float /*lx*/, float /*ly*/) override {
        switchSoundsAt.push_back(id);
    }
    int PlaySoundTracked(SoundEffect sound, int preferredChannel = -1) override {
        const int ch = AllocateChannel(preferredChannel);
        sounds.push_back({sound, 1.0f, false, 0.0f, 0.0f});
        return ch;
    }
    int PlaySoundTrackedAt(SoundEffect sound, float wx, float wy, float /*lx*/, float /*ly*/,
                           int preferredChannel = -1) override {
        const int ch = AllocateChannel(preferredChannel);
        sounds.push_back({sound, 1.0f, true, wx, wy});
        return ch;
    }
    void UpdateTrackedSoundAt(int /*channel*/, SoundEffect /*sound*/, float /*wx*/, float /*wy*/,
                              float /*lx*/, float /*ly*/) override {}
    void SetChannelPaused(int channel, bool paused) override {
        if (channel >= 0 && channel < static_cast<int>(pausedChannels.size())) {
            pausedChannels[static_cast<size_t>(channel)] = paused;
        }
    }
    void StopChannel(int& channel) override {
        if (channel >= 0 && channel < static_cast<int>(activeChannels.size())) {
            activeChannels[static_cast<size_t>(channel)] = false;
            pausedChannels[static_cast<size_t>(channel)] = false;
            stoppedChannels.push_back(channel);
        }
        channel = -1;
    }

    bool HasSound(SoundEffect effect) const {
        return std::any_of(sounds.begin(), sounds.end(),
                           [&](const SoundCall& c) { return c.effect == effect; });
    }
    bool HasLocalSound(SoundEffect effect) const {
        return std::any_of(sounds.begin(), sounds.end(),
                           [&](const SoundCall& c) { return c.effect == effect && !c.spatial; });
    }
    bool HasSpatialSound(SoundEffect effect) const {
        return std::any_of(sounds.begin(), sounds.end(),
                           [&](const SoundCall& c) { return c.effect == effect && c.spatial; });
    }

private:
    int AllocateChannel(int preferred) {
        if (preferred >= 0 && preferred < static_cast<int>(activeChannels.size())) {
            activeChannels[static_cast<size_t>(preferred)] = true;
            return preferred;
        }
        for (int c = kReservedChannels; c < static_cast<int>(activeChannels.size()); ++c) {
            if (!activeChannels[static_cast<size_t>(c)]) {
                activeChannels[static_cast<size_t>(c)] = true;
                return c;
            }
        }
        return -1;
    }
};

void TestInputCmdRoundTrip() {
    InputCmd cmd;
    cmd.actorId = 3;
    cmd.tick = 42;
    cmd.Set(InputButton::MoveUp, true);
    cmd.Set(InputButton::MoveRight, true);
    cmd.Set(InputButton::Fire, true);
    cmd.Set(InputButton::RelativeAim, true);
    cmd.hasAimWorld = true;
    cmd.aimX = 120.5f;
    cmd.aimY = -45.25f;
    cmd.switchSlot = 1;
    cmd.buyWeapon = static_cast<std::uint8_t>(WeaponId::AK47);
    cmd.buyArmorTier = 2;
    cmd.buyKit = true;

    const ActorInput ai = cmd.ToActorInput();
    Check(ai.moveUp && !ai.moveLeft && !ai.moveDown && ai.moveRight,
          "InputCmd -> ActorInput preserves movement flags");
    Check(ai.fire && !ai.reload && !ai.stealth && !ai.interact,
          "InputCmd -> ActorInput preserves action flags");
    Check(ai.hasAimWorld && ai.aimIsRelative &&
          std::abs(ai.aimWorldX - 120.5f) < 1e-4f && std::abs(ai.aimWorldY + 45.25f) < 1e-4f,
          "InputCmd -> ActorInput preserves relative aim target");

    const InputCmd rt = InputCmd::FromActorInput(3, ai, 42);
    Check(rt.Has(InputButton::MoveUp) && rt.Has(InputButton::MoveRight) &&
          rt.Has(InputButton::Fire) && rt.Has(InputButton::RelativeAim),
          "ActorInput -> InputCmd restores button bitmask");

    const dev_replay::InputFrame frame = dev_replay::FromInputCmd(cmd);
    const InputCmd cmd2 = dev_replay::ToInputCmd(frame, 3);
    Check(frame.w && frame.d && frame.mouseDown &&
          cmd2.tick == 42 &&
          cmd2.Has(InputButton::MoveUp) && cmd2.Has(InputButton::Fire) &&
          cmd2.switchSlot == 1 &&
          cmd2.buyWeapon == static_cast<std::uint8_t>(WeaponId::AK47) &&
          cmd2.buyArmorTier == 2 && cmd2.buyKit,
          "InputCmd <-> dev_replay::InputFrame round-trip preserves actions and buy commands");
}

void TestHeadlessSimulationAndEvents() {
    CS2DSimulation sim;
    sim.SetMatchMode(MatchMode::BombDefusal);
    sim.SetRngSeed(12345);
    const bool loaded = sim.LoadMapHeadless(MapPath(), true);
    Check(loaded, "CS2DSimulation loads map purely headless without SDL/Renderer");
    if (!loaded) return;

    // 清除初始 Reset 发出的事件，准备测试多真人 Step(InputCmd)
    (void)sim.DrainEvents();

    // 开启两位真人（Actor 0 = CT, Actor 1 = T），移到开阔地点
    sim.SetLocalActorId(0);
    auto& actors = sim.GetActors();
    Check(actors.size() >= 2, "Simulation has at least two active actor slots");
    if (actors.size() < 2) return;

    sim.SetActorController(1, ControllerKind::Human, 1001, "RemoteT");
    // 禁用其他 BOT 并关闭 freeze/buy phase 等待，聚焦多真人权威交互
    RoundConfig rc;
    rc.freezeTime = 0.0f;
    rc.buyTime = 0.0f;
    sim.SetRoundConfig(rc);
    for (size_t i = 2; i < actors.size(); ++i) {
        actors[i].alive = false;
    }

    auto& p0 = actors[0];
    auto& p1 = actors[1];
    p0.team = Team::CT;
    p1.team = Team::T;
    p0.alive = true;
    p1.alive = true;
    p0.money = 8000;
    p1.money = 8000;
    p0.x = 500.0f; p0.y = 500.0f;
    p1.x = 532.0f; p1.y = 500.0f; // 近距离无遮挡对射
    // 构造无墙体、空购买区的开阔世界（buyZones为空时任意位置允许购买）
    CS2DMapData openMap;
    openMap.mapWidth = 40;
    openMap.mapHeight = 40;
    openMap.tileWidth = 32;
    openMap.tileHeight = 32;
    sim.GetWorld().Build(openMap);
    sim.Visibility().SetWorld(&sim.GetWorld());

    // Step 1: 两个真人同时通过 InputCmd 买枪 & 朝空旷方向移动 45 步（避免两人互撞挡住位移）
    p0.x = 300.0f; p0.y = 500.0f;
    p1.x = 700.0f; p1.y = 500.0f;
    InputCmd c0;
    c0.actorId = 0;
    c0.Set(InputButton::MoveDown, true);
    c0.buyWeapon = static_cast<std::uint8_t>(WeaponId::M4A4);
    c0.hasAimWorld = true;
    c0.aimX = p1.x;
    c0.aimY = p1.y;

    InputCmd c1;
    c1.actorId = 1;
    c1.Set(InputButton::MoveRight, true);
    c1.buyWeapon = static_cast<std::uint8_t>(WeaponId::AK47);
    c1.hasAimWorld = true;
    c1.aimX = p0.x;
    c1.aimY = p0.y;

    for (int t = 0; t < 45; ++t) {
        if (t > 0) {
            c0.buyWeapon = 0;
            c1.buyWeapon = 0;
        }
        sim.Step(1.0f / 60.0f, {c0, c1});
    }

    const auto buyAndMoveEvents = sim.DrainEvents();
    int buyCount = 0;
    bool hasFootstep = false;
    for (const auto& ev : buyAndMoveEvents) {
        if (ev.type == SimEventType::BuyResult && ev.buyResult.code == Presentation::BuyCode::Ok) {
            ++buyCount;
        }
        if (ev.type == SimEventType::FootstepEvent) {
            hasFootstep = true;
        }
    }
    Check(buyCount == 2, "CS2DSimulation::Step emits BuyResult events for multiple human InputCmds");
    Check(hasFootstep, "CS2DSimulation::Step emits FootstepEvent during movement");

    // Step 2: 贴脸开火命中并击杀（P0 打 P1，再额外触发回合结算）
    p1.health = 25;
    p1.armor = 0;
    p1.x = p0.x + 28.0f;
    p1.y = p0.y;
    p0.vx = p0.vy = 0.0f;
    p0.shootCooldown = 0.0f;

    InputCmd fire0;
    fire0.actorId = 0;
    fire0.Set(InputButton::Fire, true);
    fire0.hasAimWorld = true;
    fire0.aimX = p1.x;
    fire0.aimY = p1.y;

    InputCmd idle1;
    idle1.actorId = 1;

    bool sawShot = false;
    bool sawHit = false;
    bool sawKill = false;
    bool sawRoundWin = false;
    CS2DClientView view;
    MockAudioSink audio;

    for (int step = 0; step < 30 && p1.alive; ++step) {
        const ActorInput fireAi = fire0.ToActorInput();
        view.BeginTick(1.0f / 60.0f, sim, &fireAi);
        sim.Step(1.0f / 60.0f, {fire0, idle1});
        const auto evs = sim.DrainEvents();
        for (const auto& ev : evs) {
            if (ev.type == SimEventType::ShotFired && ev.actorId == 0) sawShot = true;
            if (ev.type == SimEventType::HitConfirm && ev.actorId == 0 && ev.targetId == 1) sawHit = true;
            if (ev.type == SimEventType::KillFeed && ev.targetId == 1 && ev.actorId == 0) sawKill = true;
            if (ev.type == SimEventType::RoundEvent && ev.roundKind == RoundEventKind::RoundWin) {
                sawRoundWin = true;
            }
        }
        view.ConsumeEvents(evs, sim, &audio);
        view.EndTick(1.0f / 60.0f, sim, true);
    }

    Check(sawShot, "CS2DSimulation emits ShotFired when InputCmd::Fire triggers");
    Check(sawHit, "CS2DSimulation emits HitConfirm on bullet impact");
    Check(sawKill && !p1.alive, "CS2DSimulation emits KillFeed when enemy dies");
    Check(sawRoundWin, "CS2DSimulation emits RoundWin when last T dies");
    Check(view.GetHitMarkerKind() == HitMarkerKind::Kill && view.GetHitMarkerTimer() > 0.0f,
          "CS2DClientView sets red Kill hit marker from SimEvent::HitConfirm");
    Check(!audio.fireSounds.empty() && audio.HasSound(SoundEffect::KILL),
          "CS2DClientView routes ShotFired & HitConfirm to MockAudioSink");
    Check(audio.HasLocalSound(SoundEffect::ROUND_WIN) && !view.GetAnnouncement().empty(),
          "CS2DClientView plays ROUND_WIN and sets victory announcement for CT viewer");
}

void TestClientViewEventRoutingAndIsolation() {
    CS2DSimulation sim;
    sim.LoadMapHeadless(MapPath(), true);
    (void)sim.DrainEvents();

    sim.SetLocalActorId(0);
    auto& actors = sim.GetActors();
    actors[0].alive = true;
    actors[0].x = 200.0f; actors[0].y = 200.0f;
    actors[0].SetSlotWeapon(0, WeaponId::AWP);
    actors[0].weaponSlot = 0;

    CS2DClientView view;
    MockAudioSink audio;

    // 1. 开镜后发射 ShotFired(flagA=true) -> 重置开镜
    view.UpdateScopeInput(sim, true, true);
    view.TickScope(0.25f, sim);
    Check(view.GetScopeView(sim).level == 1, "CS2DClientView enters AWP scope level 1");

    {
        SimEvent shot;
        shot.type = SimEventType::ShotFired;
        shot.actorId = 0;
        shot.weapon = WeaponId::AWP;
        shot.x = 200.0f; shot.y = 200.0f;

        SimEvent scopeReset;
        scopeReset.type = SimEventType::ScopeReset;
        scopeReset.actorId = 0;
        scopeReset.scopeReason = Presentation::ScopeResetReason::ShotFired;
        scopeReset.flagA = true;
        scopeReset.flagB = false;

        SimEvent dry;
        dry.type = SimEventType::ShotFired;
        dry.actorId = 0;
        dry.flagB = true; // dry fire
        dry.flagC = true; // show HUD hint

        SimEvent hitArmor;
        hitArmor.type = SimEventType::HitConfirm;
        hitArmor.actorId = 0;
        hitArmor.targetId = 1;
        hitArmor.flagA = true;
        hitArmor.x = 260.0f; hitArmor.y = 200.0f;

        SimEvent dmg;
        dmg.type = SimEventType::DamageTaken;
        dmg.actorId = 1;
        dmg.targetId = 0;
        dmg.dirX = -1.0f;
        dmg.dirY = 0.0f;
        dmg.flagB = true;

        SimEvent remoteShot;
        remoteShot.type = SimEventType::ShotFired;
        remoteShot.actorId = 1;
        remoteShot.weapon = WeaponId::AK47;
        remoteShot.x = 340.0f; remoteShot.y = 220.0f;

        view.ConsumeEvents({shot, scopeReset, dry, hitArmor, dmg, remoteShot}, sim, &audio);
    }

    Check(view.GetScopeView(sim).level == 0,
          "CS2DClientView closes non-automatic scope on ShotFired event");
    Check(view.GetDryFireHintTimer() == 1.0f && audio.HasLocalSound(SoundEffect::DRYFIRE),
          "CS2DClientView triggers dry-fire HUD hint and local DRYFIRE audio");
    Check(view.GetHitMarkerKind() == HitMarkerKind::Armor && view.GetHitMarkerTimer() > 0.0f,
          "CS2DClientView displays Armor hit marker from HitConfirm");
    Check(view.GetDamageFlashTimer() > 0.0f,
          "CS2DClientView triggers damage vignette flash from DamageTaken");
    Check(audio.fireSounds.size() == 1 && audio.fireSounds[0] == WeaponId::AWP,
          "CS2DClientView routes local gunfire to PlayWeaponFire");
    Check(audio.fireSoundsAt.size() == 1 && audio.fireSoundsAt[0] == WeaponId::AK47,
          "CS2DClientView routes remote gunfire to spatial PlayWeaponFireAt");

    // 2. 拆包保留通道：本地与队友同时拆包，本地死亡只停止本端通道
    {
        SimEvent p0Def;
        p0Def.type = SimEventType::BombEvent;
        p0Def.bombKind = BombEventKind::DefuseActive;
        p0Def.actorId = 0;
        p0Def.x = 200.0f; p0Def.y = 200.0f;

        SimEvent p1Def;
        p1Def.type = SimEventType::BombEvent;
        p1Def.bombKind = BombEventKind::DefuseActive;
        p1Def.actorId = 1;
        p1Def.flagA = true;
        p1Def.x = 300.0f; p1Def.y = 200.0f;

        view.ConsumeEvents({p0Def, p1Def}, sim, &audio);
        Check(audio.activeChannels[2] && audio.activeChannels[3],
              "CS2DClientView allocates distinct reserved channels 2 and 3 for concurrent defusers");

        SimEvent stop0;
        stop0.type = SimEventType::BombEvent;
        stop0.bombKind = BombEventKind::DefuseStopped;
        stop0.actorId = 0;

        SimEvent kill0;
        kill0.type = SimEventType::KillFeed;
        kill0.actorId = 2;
        kill0.targetId = 0;
        kill0.weapon = WeaponId::AK47;
        kill0.killerName = "Enemy";
        kill0.victimName = "你";

        actors[0].alive = false;
        view.ConsumeEvents({stop0, kill0}, sim, &audio);
        Check(!audio.activeChannels[2] && audio.activeChannels[3],
              "DefuseStopped on local death stops channel 2 while keeping teammate channel 3 alive");
        Check(view.IsDeathFreeze(sim) && view.GetDeathPresentation().event.weapon == WeaponId::AK47,
              "CS2DClientView enters death freeze with killer attribution on KillFeed");
    }
}

} // namespace

int main() {
    TestInputCmdRoundTrip();
    TestHeadlessSimulationAndEvents();
    TestClientViewEventRoutingAndIsolation();

    std::printf("\n===== pixelsim_event_test: %d passed, %d failed =====\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
