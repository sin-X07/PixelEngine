#pragma once
#include "CS2DWorld.hpp"
#include "Visibility/VisibilityQuery.hpp"
#include "Weapons/Ballistics.hpp"
#include "Teams/TeamManager.hpp"
#include "Coach/CoachTypes.hpp"
#include "Coach/TacticsTable.hpp"
#include "TacticalIntel.hpp"
#include "SoundEvents.hpp"
#include "Rounds/RoundManager.hpp"
#include "Weapons/WeaponDef.hpp"
#include "Tuning.hpp"
#include "Economy/EconomySystem.hpp"
#include "Navigation/PathFinder.hpp"
#include "../Core/Types.hpp"
#include "../Core/ActorTypes.hpp"
#include "../Core/InputCmd.hpp"
#include "../Core/SimEvents.hpp"
#include "../Core/PresentationContract.hpp"
#include "../Core/Vec2.hpp"

#include <string>
#include <chrono>
#include <map>
#include <cstdio>
#include <vector>
#include <deque>
#include <mutex>
#include <optional>
#include <functional>
#include <random>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>

// 对局模式
enum class MatchMode {
    DeathMatch,        // 自由击杀，即时重生，无回合
    TeamElimination,   // 回合制，T/CT 歼灭，先到 N 分获胜
    BombDefusal        // 爆破：T 下包 / CT 拆包
};

// 武器槽参数表（slot: 0=主武器, 1=副武器, 2=近战）
struct WeaponSlotParams {
    float damage;
    float cooldown;
    float speed;
    float range;
};

// BOT 战术分工
enum class BotRole {
    Entry,      // 突破手：最靠前，主动接敌
    Support,    // 支援：居中跟进
    Anchor,     // 断后/守点：靠后，倾向保持位置
    Lurk,       // 绕后/断后：不参与正面，游走/静步（T26）
    Awper       // 架狙：慢速、开镜优先（T26）
};

// 每名 BOT 的小队指令
struct SquadOrder {
    BotRole role  = BotRole::Support;
    ActorId focus = kInvalidActorId; // 集火目标：kInvalidActorId=无, >=0=ActorId
    int     site  = -1;    // 负责的包点索引（-1 = 无 / 中路）
    bool    hasHold = false;
    float   holdX = 0.0f;
    float   holdY = 0.0f;
    bool    canTrade = true;    // 允许补枪（教练 canTrade）
    bool    canRotate = false;  // 允许自行换点/游走（lurk）
};

// ============================================================================
// CS2DSimulation — 纯权威物理与规则仿真层
//
// 零音频、零渲染、零窗口及客户端表现状态：所有音频/视觉反馈通过 SimEventQueue 发射。
// ============================================================================
class CS2DSimulation {
public:
    enum class AnimState { Idle, Walk, Shoot, Dead };
    using HitMarkerKind = ::HitMarkerKind;

    // ---- 死斗重装配装快照 ----
    struct DmLoadout {
        WeaponId primary = WeaponId::None;
        WeaponId secondary = WeaponId::None;
        bool valid = false;
    };

    // ---- 战斗单位（真人与 BOT 统一共用）----
    struct Player {
        ActorId id = kInvalidActorId;
        ControllerKind controller = ControllerKind::Bot;
        std::uint32_t peerId = 0;

        bool IsHuman() const { return controller == ControllerKind::Human; }
        bool IsBot() const { return controller == ControllerKind::Bot; }
        bool IsActive() const { return controller != ControllerKind::Empty; }

        bool hasDefuseKit = false;
        float x       = 0.0f;
        float y       = 0.0f;
        float vx      = 0.0f;
        float vy      = 0.0f;
        float size    = Tuning::PlayerSize;
        float defuseProgress = 0.0f;
        float plantProgress = 0.0f;
        bool holdingC4 = false;
        float flashTimer = 0.0f;
        float flashPeak = 0.0f;
        float footstepStride = 0.0f;
        float footstepLastX = 0.0f;
        float footstepLastY = 0.0f;
        bool  footstepLastValid = false;
        std::uint64_t footstepCount = 0;
        int previousEquipSlot = 2;
        WeaponId previousEquipWeapon = WeaponId::Knife;
        bool prevMouseDown = false;
        bool prevInteract = false;
        bool interactHeld = false;
        bool fireHeld = false;
        float aimWorldX = 0.0f;
        float aimWorldY = 0.0f;
        float dryFireCooldown = 0.0f;
        bool switchSoundPending = false;
        WeaponId switchSoundWeapon = WeaponId::None;
        bool scoped = false;
        int scopeLevel = 0;              // 当前权威开镜档位（影响移动惩罚与散布）
        float scopeEngagement = 0.0f;    // 当前权威开镜开合程度（0..1）
        DmLoadout dmLoadout{};
        float aimAngle = 0.0f;
        float aimKick = 0.0f;
        float aimDistance = Tuning::RecoilReferenceDistance;
        float recoilRecoveryTimer = 0.0f;
        struct WeaponFireState {
            WeaponId weapon = WeaponId::None;
            unsigned shot = 0;
            float inaccuracy = 0;
            Vec2 kick{};
            void Recover(float dt) {
                if (weapon == WeaponId::None)
                    return;
                const float decay =
                    std::exp(-2.302585093f * dt / std::max(.02f, Ballistics::Get(weapon).recovery));
                kick.x *= decay;
                kick.y *= decay;
                inaccuracy *= decay;
                if (std::hypot(kick.x, kick.y) < .0001f && inaccuracy < .0001f)
                    *this = {};
            }
        };
        std::array<WeaponFireState, 5> fireStates{};
        struct EquipmentRecord {
            std::uint64_t id = 0;
            unsigned epoch = 0;
            ActorId buyer = kInvalidActorId;
            int paid = 0;
            int armorTier = 0;
            bool used = false;
            int previousArmor = 0;
            bool previousHelmet = false;
        };
        std::array<EquipmentRecord, 5> equipment{};
        std::array<EquipmentRecord, 4> grenadeEquipment{};
        EquipmentRecord kitEquipment{};
        std::vector<EquipmentRecord> armorEquipment;

        Vec2 BallisticKick() const {
            const auto &state = fireStates[weaponSlot];
            return state.weapon == CurrentWeapon().id ? state.kick : Vec2{};
        }
        float BallisticInaccuracy() const {
            const auto &state = fireStates[weaponSlot];
            return state.weapon == CurrentWeapon().id ? state.inaccuracy : 0;
        }
        void ApplyWeaponRecoil(float scope) {
            equipment[weaponSlot].used = true;
            const auto id = CurrentWeapon().id;
            auto &state = fireStates[weaponSlot];
            if (state.weapon != id) {
                state = {};
                state.weapon = id;
            }
            const auto step = Ballistics::Recoil(id, state.shot++, scope);
            state.kick.x += step.x;
            state.kick.y += step.y;
            const float length = std::hypot(state.kick.x, state.kick.y);
            if (length > .45f) {
                state.kick.x *= .45f / length;
                state.kick.y *= .45f / length;
            }
            const auto &p = Ballistics::Get(id);
            state.inaccuracy += p.fire[id == WeaponId::Negev && state.shot >= 8 ? 1 : scope >= .5f ? 1 : 0];
        }
        void RecoverBallistics(float dt) {
            for (auto &state : fireStates)
                state.Recover(dt);
        }
        Vec2 EffectiveAimVector() const {
            const auto kick = BallisticKick();
            const float x = std::cos(aimAngle) + kick.x, y = std::sin(aimAngle) + kick.y;
            return {x * std::cos(aimKick) - y * std::sin(aimKick),
                    x * std::sin(aimKick) + y * std::cos(aimKick)};
        }
        float EffectiveAim() const {
            const auto direction = EffectiveAimVector();
            return std::atan2(direction.y, direction.x);
        }
        float RecoilAngle() const {
            return std::remainder(EffectiveAim() - aimAngle, Tuning::Pi * 2);
        }
        float RecoilOffsetX() const {
            return (EffectiveAimVector().x - std::cos(aimAngle)) * aimDistance;
        }
        float RecoilOffsetY() const {
            return (EffectiveAimVector().y - std::sin(aimAngle)) * aimDistance;
        }
        void ApplyRecoil(float degrees) {
            const float remaining = std::remainder(-Tuning::Pi * 0.5f - EffectiveAim(), Tuning::Pi * 2.0f);
            const float step = std::min(std::max(degrees, 0.0f) * Tuning::DegreesToRadians, std::abs(remaining));
            aimKick = std::remainder(aimKick + std::copysign(step, remaining), Tuning::Pi * 2.0f);
            recoilRecoveryTimer = Tuning::RecoilRecoveryDelay;
        }
        int   health  = 100;
        int   maxHealth = 100;
        Team  team    = Team::CT;
        std::string name;
        bool  alive   = true;
        float shootCooldown = 0.0f;

        // 精灵动画状态（仿真驱动行进/射击/倒地状态机）
        AnimState animState = AnimState::Idle;
        float     animTimer = 0.0f;
        int       weaponSlot = 0;          // 活动槽位 0=主 1=副 2=近战 3=投掷 4=C4
        int       prevWeaponSlot = 1;      // 切到投掷物之前持有的槽位
        int       spriteKey  = 0;
        float     deathTimer = 0.0f;
        float     shootAnimTimer = 0.0f;

        // 武器（按槽位）
        WeaponId  weapons[5] = { WeaponId::None, WeaponId::USP_S, WeaponId::Knife, WeaponId::None, WeaponId::None };

        // ---- 投掷物背包 ----
        static constexpr int MAX_GRENADES = 4;
        WeaponId  grenades[MAX_GRENADES] = { WeaponId::None, WeaponId::None,
                                             WeaponId::None, WeaponId::None };
        int       grenadeCount = 0;
        WeaponId  lastGun = WeaponId::None;
        int       grenadeSlot  = 0;

        WeaponId ActiveGrenade() const {
            if (grenadeCount <= 0 || grenadeSlot < 0 || grenadeSlot >= MAX_GRENADES) {
                return WeaponId::None;
            }
            return grenades[grenadeSlot];
        }
        int CountGrenade(WeaponId id) const {
            int n = 0;
            for (int i = 0; i < grenadeCount; ++i) if (grenades[i] == id) ++n;
            return n;
        }
        void SyncActiveGrenade() {
            if (grenadeCount <= 0) {
                grenades[0] = WeaponId::None;
                grenadeCount = 0;
                grenadeSlot  = 0;
                weapons[3]   = WeaponId::None;
                return;
            }
            if (grenadeSlot >= grenadeCount) grenadeSlot = grenadeCount - 1;
            if (grenadeSlot < 0) grenadeSlot = 0;
            weapons[3] = grenades[grenadeSlot];
        }
        void ConsumeActiveGrenade() {
            if (grenadeCount <= 0) { SyncActiveGrenade(); return; }
            for (int i = grenadeSlot; i < grenadeCount - 1; ++i) {
                grenades[i] = grenades[i + 1];
                grenadeEquipment[i] = grenadeEquipment[i + 1];
            }
            grenades[grenadeCount - 1] = WeaponId::None;
            grenadeEquipment[grenadeCount - 1] = {};
            --grenadeCount;
            SyncActiveGrenade();
        }
        bool SelectGrenade(WeaponId id) {
            for (int i = 0; i < grenadeCount; ++i) {
                if (grenades[i] == id) {
                    grenadeSlot = i;
                    SyncActiveGrenade();
                    return true;
                }
            }
            return false;
        }

        int ammo      = 30;
        int maxAmmo   = 30;
        int reserve   = 0;
        int ammoSlot[5]    = { 0, 12, 0, 0, 0 };
        int maxAmmoSlot[5] = { 0, 12, 0, 0, 0 };
        int reserveSlot[5] = { 0, 36, 0, 0, 0 };

        static int ReserveCapacity(const WeaponDef& d) {
            if (d.cls == WeaponClass::ZeusGun) return 0;
            return (d.magazine > 0) ? d.magazine * 3 : 0;
        }

        void SetSlotWeapon(int slot, WeaponId id) {
            if (slot < 0 || slot > 4) return;
            weapons[slot] = id;
            if (id == WeaponId::None) {
                ammoSlot[slot] = 0;
                maxAmmoSlot[slot] = 0;
                reserveSlot[slot] = 0;
                return;
            }
            const WeaponDef& d = WeaponDB::Get(id);
            const int mag = (d.magazine > 0) ? d.magazine : 1;
            ammoSlot[slot] = mag;
            maxAmmoSlot[slot] = mag;
            reserveSlot[slot] = ReserveCapacity(d);
        }

        void SetSlotWeaponWithAmmo(int slot, WeaponId id, int magAmmo, int reserveAmmo) {
            if (slot < 0 || slot > 4) return;
            weapons[slot] = id;
            if (id == WeaponId::None) {
                ammoSlot[slot] = 0;
                maxAmmoSlot[slot] = 0;
                reserveSlot[slot] = 0;
                return;
            }
            const WeaponDef& d = WeaponDB::Get(id);
            const int cap = (d.magazine > 0) ? d.magazine : 1;
            ammoSlot[slot]    = std::max(0, std::min(magAmmo, cap));
            maxAmmoSlot[slot] = cap;
            reserveSlot[slot] = std::max(0, std::min(reserveAmmo, ReserveCapacity(d)));
        }

        void ClearSlotWeapon(int slot) {
            if (slot < 0 || slot > 4) return;
            weapons[slot] = WeaponId::None;
            equipment[slot] = {};
            fireStates[slot] = {};
            ammoSlot[slot] = 0;
            maxAmmoSlot[slot] = 0;
            reserveSlot[slot] = 0;
        }

        void SaveAmmoToSlot() {
            if (weaponSlot < 0 || weaponSlot > 4) return;
            if (weapons[weaponSlot] == WeaponId::None) return;
            ammoSlot[weaponSlot] = ammo;
            maxAmmoSlot[weaponSlot] = maxAmmo;
            reserveSlot[weaponSlot] = reserve;
        }

        void LoadAmmoFromSlot() {
            if (weaponSlot < 0 || weaponSlot > 4) return;
            if (weapons[weaponSlot] == WeaponId::None) {
                ammo = 1;
                maxAmmo = 1;
                reserve = 0;
                return;
            }
            const auto weaponClass = WeaponDB::Get(weapons[weaponSlot]).cls;
            if (weaponClass != WeaponClass::Melee && weaponClass != WeaponClass::Grenade &&
                weaponClass != WeaponClass::Bomb) {
                lastGun = weapons[weaponSlot];
            }
            ammo = ammoSlot[weaponSlot];
            maxAmmo = maxAmmoSlot[weaponSlot];
            reserve = reserveSlot[weaponSlot];
        }
        bool      reloading = false;
        float     reloadTimer = 0.0f;
        bool      stealth   = false;

        int       money  = 800;
        int       kills  = 0;
        int       teamKills = 0;
        int       deaths = 0;
        int       armor  = 0;
        bool      helmet = false;

        const WeaponDef& CurrentWeapon() const {
            const int s = (weaponSlot < 0 || weaponSlot > 4) ? 0 : weaponSlot;
            return WeaponDB::Get(weapons[s]);
        }
    };

    enum class BotStance {
        None = 0,
        Aggressive,
        Defensive,
        Retreat,
        MoveToCover
    };
    enum class BotManeuver {
        None = 0,
        Strafe,
        BackOff
    };

    struct BotRuntime {
        int difficulty = 2;
        float settleTimer = 0.0f;
        ActorId perceivedTarget = kInvalidActorId;
        float dodgeX = 0.0f, dodgeY = 0.0f;
        bool wasMoving = false;
        struct Alert { ActorId enemyId = kInvalidActorId; float perceivedAt = 0.0f, expireAt = 0.0f, lastPerceivedAt = 0.0f; };
        std::array<Alert, 3> alerts{};
        void Perceive(ActorId id, float now, float delay) {
            for (auto& alert : alerts) {
                if (alert.enemyId == id) {
                    alert.lastPerceivedAt = now;
                    return;
                }
            }
            auto slot = std::find_if(alerts.begin(), alerts.end(), [](const Alert& a) { return a.enemyId == kInvalidActorId; });
            if (slot == alerts.end()) {
                slot = std::max_element(alerts.begin(), alerts.end(), [](const Alert& a, const Alert& b) { return a.expireAt < b.expireAt; });
                if (slot->expireAt < now + delay) return;
            }
            *slot = {id, now, now + delay, now};
        }
        bool Ready(ActorId id, float now) const {
            for (const auto& alert : alerts) if (alert.enemyId == id && now >= alert.expireAt) return true;
            return false;
        }
        BotStance   stance   = BotStance::None;
        BotManeuver maneuver = BotManeuver::None;
        float     rotateTimer = 0.0f;

        std::vector<std::pair<float, float>> path;
        int   pathIdx     = 0;
        float repathTimer = 0.0f;
        float goalX       = 0.0f;
        float goalY       = 0.0f;

        float retreatTimer = 0.0f;
        float threatX      = 0.0f;
        float threatY      = 0.0f;
        float lastHitTimer = 0.0f;
        float defuseCooldown = 0.0f;

        bool  defusing       = false;
        float heardLogCooldown = 0.0f;
        float reactionLogCooldown = 0.0f;

        std::vector<HeardSound> heard;
        float alertTimer = 0.0f;
        float alertX = 0.0f, alertY = 0.0f;
        float grenadeCooldown = 0.0f;
        bool  tradePushed     = false;
        bool  saveGun         = false;
        bool  scoped          = false;
        bool  hadTarget       = false;
        float scanPhase       = 0.0f;
        float scanBase        = 0.0f;
        float wanderX         = 0.0f;
        float wanderY         = 0.0f;
        float wanderTimer     = 0.0f;
        float detourX = 0.0f, detourY = 0.0f;
        float detourTimer = 0.0f;
        bool  scanBaseValid   = false;

        float lastX = 0.0f, lastY = 0.0f;
        float stuckTimer = 0.0f;

        enum class GoalSource : unsigned char { None = 0, Squad = 1, Coach = 2 };
        GoalSource goalSource = GoalSource::None;
        bool  hasPlanHold = false;
        float planHoldX = 0.0f, planHoldY = 0.0f;
        std::string goalArea;

        int   moveLatch = 0;
        float moveLatchTimer = 0.0f;
        float strafeSide = 1.0f;
        float strafeSideTimer = 0.0f;
        int   prevLatch = 0;
        float assistCooldown = 0.0f;
        std::string searchArea;
        float searchTimer = 0.0f;
        float lookAngle = -99.0f;
        int   holdStyle = 0;
        int   utilKind  = 0;
        float utilX = 0.0f, utilY = 0.0f;
        bool  utilDone = true;
        bool  utilThrowFailed = false;
        float nadeFailX = 0.0f, nadeFailY = 0.0f;
        float nadeFailAt = -1e9f;

        ActorId traceTarget = kInvalidActorId;
        int   traceStance   = 0;
        int   traceManeuver = 0;
        int   traceGoalSrc  = 0;
        float traceGoalX    = 0.0f, traceGoalY = 0.0f;
        bool  traceDefusing = false;
    };

    struct Bullet {
        std::uint64_t equipmentId = 0;
        Vec2 sourcePosition{};
        float x  = 0.0f;
        float y  = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float traveled = 0.0f;
        float maxDist  = 1600.0f;
        int   damage   = 25;
        ActorId ownerId = kInvalidActorId;
        Team  ownerTeam = Team::None;
        WeaponId weapon = WeaponId::None;
        bool  active   = true;
    };

    struct Grenade {
        std::uint64_t equipmentId = 0;
        float    x = 0.0f, y = 0.0f;
        float    vx = 0.0f, vy = 0.0f;
        float    fuse = 1.5f;
        WeaponId type = WeaponId::GrenadeHE;
        Team     team = Team::None;
        bool     active = true;
        ActorId  ownerId = kInvalidActorId;
        WeaponId imitate = WeaponId::None;
        bool     landingWarned = false;
        int      wallBounces = 0;
        float    spin = 0.0f;
        float    bounceHearCooldown = 0.0f;
    };

    struct Decoy {
        float x = 0, y = 0, life = 15, nextShot = 0;
        int shot = 0;
        ActorId ownerId = kInvalidActorId;
        Team team = Team::None;
        WeaponId weapon = WeaponId::Glock18;
    };

    struct FireZone {
        std::uint64_t equipmentId = 0;
        float x = 0.0f, y = 0.0f;
        ActorId ownerId = kInvalidActorId;
        WeaponId weapon = WeaponId::GrenadeMolotov;
        std::vector<Vec2> cells;
        std::vector<uint8_t> cellCoverage;
        int cellExtent = 0;
        std::uint64_t terrainRevision = 0;
        float radius = Tuning::FireRadius;
        float life = 7.0f;
        float maxLife = 7.0f;
        float age = 0.0f;
        float seed = 0.0f;
    };

    struct SmokeCloud {
        float x = 0.0f, y = 0.0f;
        float radius = Tuning::SmokeRadius;
        float life = 12.0f;
        float maxLife = 12.0f;
        float age = 0.0f;
        float expandTime = 1.6f;
        float seed = 0.0f;
    };

    struct KillFeedEntry {
        std::uint64_t equipmentId = 0;
        Vec2 sourcePosition{};
        std::uint64_t id = 0;
        float timestamp = 0;
        std::string text;
        float life = 4.0f;
        std::string killer, victim;
        Team killerTeam = Team::None, victimTeam = Team::None;
        WeaponId weapon = WeaponId::None;
        bool personal = false;
    };

    struct ScoreRow {
        ActorId id = kInvalidActorId;
        std::string name;
        Team team = Team::None;
        int  kills = 0;
        int  teamKills = 0;
        int  deaths = 0;
        int  money = 0;
        bool alive = true;
        bool isHuman = false;
        bool isPlayer = false;
    };

    struct DebugCounters {
        int intentChanges = 0;
        int roleSwitches  = 0;
        int assistPushes  = 0;
        int nadeThrows    = 0;
        int nadeSkips     = 0;
        int nadePlanFails = 0;
        int searchVisits  = 0;
        int llmPlans      = 0;
        int llmAdvisory   = 0;
        int llmLate       = 0;
        int renewals      = 0;
        int molotovBuys     = 0;
        int molotovThrows   = 0;
        int utilKindThrows  = 0;
        int utilKindMismatch= 0;
        int fireDamage      = 0;
        int fireTicks       = 0;
    };

    struct EnemyContact {
        ActorId id  = kInvalidActorId;
        float   x   = 0.0f;
        float   y   = 0.0f;
        float   age = 0.0f;
    };

    struct SoundMaker {
        const Player* who   = nullptr;
        float         x     = 0.0f;
        float         y     = 0.0f;
        bool          isPlant = false;
    };

    struct RefundQuote { int amount = 0; int quantity = 0; std::string reason; };

    struct ArmorDamageResult {
        int healthDamage = 0;
        int armorLoss = 0;
    };

    CS2DSimulation();
    ~CS2DSimulation() = default;

    bool LoadMapHeadless(const std::string& resolvedTMJPath, bool resetMatch = true,
                         float visionRadius = Tuning::VisionRadius);
    void Reset();

    // ---- 权威事件队列 ----
    const std::vector<SimEvent>& PeekEvents() const { return simEvents_.Events(); }
    std::vector<SimEvent> DrainEvents() { return simEvents_.Drain(); }
    void ClearEvents() { simEvents_.Clear(); }

    // ---- 标准单帧输入步进与多 Actor 步进 ----
    void Step(float dt, const std::vector<InputCmd>& cmds);
    void StepActors(float dt, const std::vector<std::pair<ActorId, ActorInput>>& inputs);

    // 同步单机/客户端开镜与准星输入到权威实体
    void SetActorScopeLevel(ActorId id, int level);
    void SetActorScopeView(ActorId id, int level, float engagement);
    void SetActorAimTarget(ActorId id, float x, float y);

    void GetTeamSpawnCenter(Team team, float& outX, float& outY) const;
    const std::string& GetMapId() const { return mapId_; }
    const TacticsTable& Tactics() const { return tactics_; }

    const DebugCounters& Counters() const { return counters_; }
    DebugCounters& MutableCounters() { return counters_; }

    void EnablePlanTrace(const std::string& path);
    void ClosePlanTrace();
    void TraceCoachPlan(Team t);
    void TraceCoachAdvisory(Team t, const TeamPlan& plan);
    void EnableWiringLog(const std::string& path);
    void CloseWiringLog();
    void TraceWiring(Team t, const char* guard, const char* sanitized, const char* templateKey);
    void EnableCoachRoundsLog(const std::string& path);
    void CloseCoachRoundsLog();
    void TraceCoachRound(Team t);
    float RoleSpeedMul(size_t botIdx, BotRole role);
    void SetRoleWithTransition(size_t botIdx, BotRole role);
    char AreaSideOf(const std::string& area) const;
    bool PlanTargetConsistent(Team t, const TeamPlan& plan) const;
    bool ApplySlotTemplate(Team t, TeamPlan& plan);
    void EnableDecisionLog(const std::string& path);
    void CloseDecisionLog();
    void TraceCoachDecision(Team t, const char* status);
    void EnableDecisionTrace(const std::string& path);
    void TraceBotDecision(size_t i, const Player* target);
    void CloseDecisionTrace();
    const std::string& GetPlaybookId() const { return playbookId_; }

    // ---- 世界与几何访问器 ----
    CS2DWorld&             GetWorld()       { return *world_; }
    const CS2DWorld&       GetWorld() const { return *world_; }
    VisibilityQuery&       Visibility()       { return visibility_; }
    const VisibilityQuery& Visibility() const { return visibility_; }

    // ---- 统一 Actor 槽位模型 ----
    std::vector<Player>&       GetActors()       { return actors_; }
    const std::vector<Player>& GetActors() const { return actors_; }
    Player*       FindActor(ActorId id);
    const Player* FindActor(ActorId id) const;
    ActorId       ActorIdOf(const Player& actor) const;
    ActorId       GetLocalActorId() const { return localActorId_; }
    void          SetLocalActorId(ActorId id);
    Player&       LocalActor() {
        if (auto* p = FindActor(localActorId_)) return *p;
        return actors_.front();
    }
    const Player& LocalActor() const {
        if (const auto* p = FindActor(localActorId_)) return *p;
        return actors_.front();
    }
    bool SetActorController(ActorId id, ControllerKind kind,
                            std::uint32_t peerId = 0, const std::string& name = {});

    Player&       GetPlayer()       { return LocalActor(); }
    const Player& GetPlayer() const { return LocalActor(); }

    const std::vector<Bullet>& GetBullets() const { return bullets_; }
    const std::vector<Presentation::BulletVisualSegment>& GetBulletVisuals() const { return bulletVisuals_; }
    const std::vector<Grenade>& GetGrenades() const { return grenades_; }
    const std::vector<SmokeCloud>& GetSmokes() const { return smokes_; }
    float SmokeOpacityAt(float x, float y) const;
    const std::vector<FireZone>& GetFires() const { return fires_; }
    const std::vector<KillFeedEntry>& GetKillFeed() const { return killFeed_; }
    std::vector<ScoreRow> BuildScoreboard() const;

    float GetFlashAmount() const { return (LocalActor().flashTimer > 0.0f) ? 1.0f : 0.0f; }
    void QuickSwitchWeaponFor(ActorId actorId);
    bool IsMovementAllowed() const {
        if (!LocalActor().alive) return false;
        return mode_ == MatchMode::DeathMatch || !round_.IsFrozen();
    }
    float GetReloadProgressFor(ActorId id) const {
        const Player* p = FindActor(id);
        if (!p || !p->alive || !p->reloading || p->CurrentWeapon().reloadTime <= 0.0f) return 0.0f;
        return std::clamp(1.0f - p->reloadTimer / p->CurrentWeapon().reloadTime, 0.0f, 1.0f);
    }

    const std::vector<SquadOrder>& GetSquadOrders() const { return actorSquad_; }
    const std::vector<EnemyContact>& GetContactsOfTeam(Team t) const { return ContactsOf(t); }
    float GetLastDeathX() const { return lastDeathX_; }
    float GetLastDeathY() const { return lastDeathY_; }
    float GetLastDeathTimer() const { return lastDeathTimer_; }
    Team  GetLastDeathTeam() const { return lastDeathTeam_; }
    int   GetBombSiteCount() const { return world_ ? static_cast<int>(world_->GetBombSites().size()) : 0; }
    bool  GetBombSiteCenter(int index, float& cx, float& cy) const {
        if (!world_) return false;
        const auto& sites = world_->GetBombSites();
        if (index < 0 || index >= static_cast<int>(sites.size())) return false;
        cx = sites[index].cx;
        cy = sites[index].cy;
        return true;
    }

    // ---- BOT 运行时 ----
    void EnsureBotAi();
    BotRuntime&       AiAt(size_t i);
    const BotRuntime& AiAt(size_t i) const;
    size_t AiIndexOf(const Player& p) const;
    void   ResetBotAi(size_t i);
    void   SetBotGoal(size_t i, float x, float y, BotRuntime::GoalSource src, bool force = false);
    void   ClearBotGoal(size_t i);
    void   SpreadHoldPoint(float cx, float cy, int index, float& ox, float& oy,
                           const std::vector<std::pair<float, float>>& used) const;

    // ---- 目标物（爆破）----
    bool  IsBombPlanted()    const { return bombPlanted_; }
    float GetBombTimer()     const { return bombTimer_; }
    float GetBombX()         const { return bombX_; }
    float GetBombY()         const { return bombY_; }
    float GetDefuseProgress() const { return defuseProgress_; }
    float GetDefuseRemaining() const { return defuseRemaining_; }
    ActorId GetActiveDefuser() const { return activeDefuser_; }
    bool IsInDefuseRange(float x, float y) const {
        if (!bombPlanted_) return false;
        const float dx = x - bombX_, dy = y - bombY_;
        if (dx * dx + dy * dy > Tuning::DefuseRadius * Tuning::DefuseRadius) return false;
        return visibility_.HasLineOfSight(x, y, bombX_, bombY_);
    }
    std::uint64_t PlayerFootstepCount() const { return LocalActor().footstepCount; }
    bool IsDefuseVisibleTo(Team viewerTeam) const {
        if (defuseProgress_ <= 0.0f || viewerTeam == Team::None) return false;
        const Player* defuser = FindActor(activeDefuser_);
        if (!defuser || defuser->team == Team::None) return false;
        return defuser->team == viewerTeam;
    }
    bool IsDefuseVisibleToViewer() const { return IsDefuseVisibleTo(LocalActor().team); }
    std::string GetDefuserName() const {
        const Player* defuser = FindActor(activeDefuser_);
        if (!defuser) return {};
        if (defuser->id == localActorId_) return "你";
        if (!defuser->name.empty()) return defuser->name;
        return (defuser->IsHuman() ? "P" : "BOT#") + std::to_string(defuser->id);
    }

    int GetPlayerKills()  const { return LocalActor().kills; }
    int GetPlayerDeaths() const { return LocalActor().deaths; }
    int GetActorCount()   const { return static_cast<int>(actors_.size()); }
    int GetBotCount()     const {
        int n = 0;
        for (const auto& a : actors_) if (a.IsBot()) ++n;
        return n;
    }
    void SetBotDifficulty(int difficulty) { for (auto& ai : actorAi_) ai.difficulty = std::clamp(difficulty, 0, 3); }
    void SetBotProfile(size_t actorIdx, int profile) { AiAt(actorIdx).difficulty = std::clamp(profile, 0, 7) / 2; }
    float GetAimAngle() const { return LocalActor().EffectiveAim(); }
    float GetRecoilBloom() const;

    // ---- 阵营 / 回合 ----
    MatchMode  GetMatchMode() const { return mode_; }
    void       SetRngSeed(uint32_t seed) { rng_.seed(seed); }
    void       SetMatchMode(MatchMode m) { mode_ = m; }

    int  GetScoreT()     const { return teams_.ScoreT(); }
    int  GetScoreCT()    const { return teams_.ScoreCT(); }
    int  GetScoreToWin() const { return teams_.ScoreToWin(); }

    bool       IsRoundDecided()     const { return round_.IsDecided(); }
    RoundPhase GetRoundPhase()      const { return round_.Phase(); }
    float      GetRoundRemaining()  const { return round_.PhaseRemaining(); }
    int        GetRoundNumber()     const { return round_.RoundNumber(); }
    Team       GetLastRoundWinner() const { return round_.LastWinner(); }
    bool       IsFrozen()           const { return round_.IsFrozen(); }

    void SetRoundConfig(const RoundConfig& cfg) {
        round_.Reset(cfg);
        prevPhase_ = round_.Phase();
    }

    bool IsMatchOver() const { return teams_.MatchOver(); }
    void SetPlayerTeam(Team t) { desiredPlayerTeam_ = t; }
    void RestartMatchWithTeam(Team t);
    Team GetPlayerTeam() const { return desiredPlayerTeam_; }

    void SetMatchLoadouts(const WeaponDB::MatchLoadouts& loadouts) {
        matchLoadouts_ = WeaponDB::SanitizeMatchLoadouts(loadouts);
    }
    const WeaponDB::MatchLoadouts& GetMatchLoadouts() const { return matchLoadouts_; }
    const WeaponDB::TeamLoadout& LoadoutForTeam(Team t) const {
        return (t == Team::T) ? matchLoadouts_.t : matchLoadouts_.ct;
    }
    bool ActorHasC4(const Player& actor) const {
        return mode_ == MatchMode::BombDefusal && !bombPlanted_ && !bombDropped_ && actor.alive &&
               bombCarrier_ != kInvalidActorId && actor.id == bombCarrier_;
    }
    bool ActorCarriesC4(const Player& actor) const {
        return bombCarrier_ != kInvalidActorId && actor.id == bombCarrier_ &&
               actor.team == Team::T && !bombPlanted_ && !bombDropped_;
    }
    bool PlayerHasC4() const { return ActorCarriesC4(LocalActor()); }
    std::string GetFriendlyC4CarrierNameFor(Team viewerTeam) const {
        if (mode_ != MatchMode::BombDefusal || viewerTeam != Team::T ||
            bombPlanted_ || bombDropped_) return {};
        const Player* carrier = FindActor(bombCarrier_);
        if (!carrier || !carrier->alive || carrier->team != Team::T) return {};
        if (carrier->id == localActorId_) return "你";
        if (!carrier->name.empty()) return carrier->name;
        return (carrier->IsHuman() ? "P" : "BOT#") + std::to_string(carrier->id);
    }
    std::string GetFriendlyC4CarrierName() const {
        return GetFriendlyC4CarrierNameFor(LocalActor().team);
    }
    bool PlayerHoldingC4() const { return LocalActor().holdingC4; }
    bool IsBombDropped() const { return bombDropped_; }
    ActorId GetBombCarrierId() const { return GetBombCarrierPos() ? bombCarrier_ : kInvalidActorId; }
    std::optional<Vec2> GetBombCarrierPos() const {
        const Player* carrier = FindActor(bombCarrier_);
        if (carrier && ActorHasC4(*carrier)) return Vec2{carrier->x, carrier->y};
        return std::nullopt;
    }
    float BombDropX() const { return bombDropX_; }
    float BombDropY() const { return bombDropY_; }
    bool  HasBombIntel() const { return bombIntel_; }
    float BombIntelX() const { return bombIntelX_; }
    float BombIntelY() const { return bombIntelY_; }
    void ToggleHoldC4For(ActorId actorId);
    void UpdateBombItem(float dt);
    void DropC4(float x, float y);
    void EquipFallbackWeaponFor(Player& actor);
    void AssignBombCarrier();
    float GetPlantProgressFor(ActorId actorId) const {
        const Player* p = FindActor(actorId);
        return p ? p->plantProgress : 0.0f;
    }
    float GetPlantProgress() const { return LocalActor().plantProgress; }

    bool GetActiveSoundMaker(SoundMaker& out) const {
        if (mode_ != MatchMode::BombDefusal) return false;
        if (bombPlanted_ && defuseProgress_ > 0.0f) {
            out.x = bombX_;
            out.y = bombY_;
            out.isPlant = false;
            const Player* defuser = FindActor(activeDefuser_);
            if (defuser && defuser->alive && defuser->defuseProgress > 0.0f) {
                out.who = defuser;
                return true;
            }
            return false;
        }
        if (!bombPlanted_ && !bombDropped_) {
            const Player* carrier = FindActor(bombCarrier_);
            if (carrier && carrier->alive && carrier->team == Team::T && carrier->plantProgress > 0.0f) {
                out.who = carrier;
                out.x = carrier->x;
                out.y = carrier->y;
                out.isPlant = true;
                return true;
            }
        }
        return false;
    }

    const DmLoadout& GetDmLoadout() const { return LocalActor().dmLoadout; }
    void RememberCurrentLoadoutAsDm() {
        if (mode_ != MatchMode::DeathMatch) return;
        auto& me = LocalActor();
        me.dmLoadout.primary   = me.weapons[0];
        me.dmLoadout.secondary = me.weapons[1];
        me.dmLoadout.valid     = (me.dmLoadout.primary != WeaponId::None ||
                                  me.dmLoadout.secondary != WeaponId::None);
    }

    Team GetMatchWinner() const { return teams_.MatchWinner(); }
#ifdef PIXEL_DEV_REPLAY
    void DebugForceMatchEnd(Team winner) {
        for (int i = 0; i < teams_.ScoreToWin(); ++i) teams_.AddScore(winner);
    }
#endif

    int AliveCount(Team t) const;

    // ---- 经济 / 购买 ----
    int  GetPlayerMoney() const { return LocalActor().money; }
    int  GetRoundStartEconomy(Team t) const;
    bool IsBuyPhaseFor(ActorId actorId) const {
        const Player* p = FindActor(actorId);
        if (!p || !p->alive) return false;
        if (mode_ == MatchMode::DeathMatch) return IsActorInBuyZone(actorId);
        if (teams_.MatchOver()) return false;
        return round_.Elapsed() <= Tuning::BuyWindowSeconds;
    }
    bool  IsBuyPhase() const { return IsBuyPhaseFor(localActorId_); }
    float GetRoundElapsed() const { return round_.Elapsed(); }
    float GetMatchOverTimer() const { return matchOverTimer_; }
    bool  IsActorInBuyZone(ActorId actorId) const;
    bool  IsPlayerInBuyZone() const { return IsActorInBuyZone(localActorId_); }

    bool TryBuyFor(ActorId actorId, WeaponId id);
    bool TryBuyArmorFor(ActorId actorId, int tier);
    RefundQuote EvaluateRefundFor(ActorId actorId, const Presentation::BuyRequest& request) const;
    bool ExecuteRefundFor(ActorId actorId, const Presentation::BuyRequest& request);
    Presentation::BuyCode EvaluatePurchaseFor(ActorId actorId, const Presentation::BuyRequest& request) const;
    Presentation::BuyResult ExecutePurchaseFor(ActorId actorId, const Presentation::BuyRequest& request);

    static ArmorDamageResult ResolveArmorDamage(int amount, float armorPen, int armorPoints);

    bool IsHostileTeam(Team a, Team b) const {
        if (mode_ == MatchMode::DeathMatch) return true;
        return a != b;
    }
    bool IsHostile(const Player& a, const Player& b) const {
        return &a != &b && IsHostileTeam(a.team, b.team);
    }
    bool IsDeathMatch() const { return mode_ == MatchMode::DeathMatch; }
    int  GetPlayerArmor()     const { return LocalActor().armor; }
    int  GetPlayerHealth()    const { return LocalActor().health; }
    int  GetPlayerMaxHealth() const { return LocalActor().maxHealth; }
    static float ComputeFlashBlind(float dist);
    static float ComputeFlashIntensity(float dist);

    // ---- 教练层 ----
    const TeamPlan&    GetCoachPlan(Team t) const;
    const std::string& GetCoachReason(Team t) const;
    float              GetCoachPlanAge() const { return clockSec_ - coachPlanChangedAt_; }

    using CoachLlmFn = std::function<void(const std::string&, const std::string&, int,
                                          const std::string&,
                                          std::function<void(const std::string&)>)>;
    void SetCoachLlm(CoachLlmFn fn) { coachLlm_ = std::move(fn); }
    void SetCoachLlmEnabled(bool on) { coachLlmEnabled_ = on; }
    void SetCoachLlmRoundStartOnly(bool on) { coachLlmRoundStartOnly_ = on; }
    bool IsCoachLlmEnabled() const { return coachLlmEnabled_; }
    bool IsCoachLlmPending() const {
        return CoachFor(Team::T).llmPending || CoachFor(Team::CT).llmPending;
    }
    void PumpCoach() { CoachDrainLlm(); }
    void CoachTryLlm(Team t, const CoachEvent& ev);
    void CoachTryDebrief(Team t);
    bool ShouldSaveNow(Team t) const;
    int  GetLossStreak(Team t) const { return economy_.LossStreak(t); }
    BuyPlan GetBuyPlan(Team t) const;
    BuyPlan DecideBuyPlan(Team t) const;
    static const char* BuyPlanLabel(BuyPlan p);
    const TacticalIntel& Intel() const { return intel_; }
    bool CanRotateNow(Team t) const;
    void CoachTryRotate(Team t);
    void CoachDrainLlm();
    void CoachWarmupPlaybook();

    void TryPickupDropsFor(ActorId actorId, bool manual);
    void DropCurrentWeaponFor(ActorId actorId);
    void DropWeaponToGround(Player& actor, int slot, float x, float y);
    bool GetPlayerHelmet() const { return LocalActor().helmet; }

    void SwitchWeaponSlotFor(ActorId actorId, int slot, int grenadeIndex = -1);
    std::string GetWeaponName() const;
    std::string GetWeaponNameFor(const Player& p) const;

    static constexpr float CONTACT_MEMORY  = 6.0f;
    static constexpr int   TEAM_SIZE       = 5;
    static constexpr int   TOTAL_BOTS      = TEAM_SIZE * 2 - 1;

protected:
    friend class CS2DClientView;
    friend class CS2DGame;
    friend struct CS2DGameTestAccess;

    void EmitSimEvent(SimEvent ev) { simEvents_.Push(std::move(ev)); }

    SimEventQueue simEvents_;

    std::unique_ptr<CS2DWorld> world_;
    VisibilityQuery visibility_;
    std::string mapId_;
    TacticsTable tactics_;
    std::FILE* decisionTrace_ = nullptr;
    uint64_t   traceTick_ = 0;
    DebugCounters counters_;
    std::vector<float> roleChangedAt_;
    std::vector<float> roleFromMul_;
    float roleRefreshTimer_ = 0.0f;
    int   roleAliveSnapshot_ = -1;
    std::map<std::string, float> areaSearchedAt_[2];
    void RefreshSearchAreas();
    std::string playbookId_;

    std::vector<Player> actors_;
    ActorId localActorId_ = 0;
    int nextBotNameIndex_ = 0;

    std::vector<Bullet> bullets_;
    std::vector<Grenade> grenades_;
    std::vector<SmokeCloud> smokes_;
    std::vector<FireZone>   fires_;
    std::vector<Decoy> decoys_;
    float fireTickTimer_ = 0.0f;

    struct BlastFx {
        float x = 0.0f, y = 0.0f;
        float life = 0.0f;
        float total = 1.0f;
    };
    std::vector<BlastFx> blasts_;

    struct DroppedItem {
        Player::EquipmentRecord equipment;
        Player::WeaponFireState fireState;
        bool defuseKit = false;
        WeaponId id = WeaponId::None;
        float x = 0.0f, y = 0.0f;
        float life = 30.0f;
        float pickupDelay = 0.0f;
        float rot = 0.0f;
        bool  isGrenade = false;
        int   magAmmo     = 0;
        int   reserveAmmo = 0;
        bool  hasAmmoState = false;
    };
    std::vector<DroppedItem> drops_;

    struct CoachState {
        TeamPlan plan;
        BuyPlan  buyPlan = BuyPlan::Full;
        std::deque<RoundMemo> memory;
        std::vector<CoachEvent> pending;
        float mergeWindow = 0.0f;
        int   callsThisRound = 0;
        int   gen = 0;
        std::string reason;
        int   appliedIntent = -1;
        int   changesThisRound = 0;
        int   sameSideStreak = 0;
        float lastIntentChangeSec = -100.0f;
        float lastManpowerReplanSec = -100.0f;
        BuyPlan lastBuyPlan = BuyPlan::Full;
        bool  lastBuyWon = false;

        std::vector<RoundMemo::Death> deaths;
        std::string firstContactArea;
        bool  timeLowFired = false;
        bool  matchPointFired = false;

        std::deque<std::string> notes;
        std::string lastOrder;
        std::string rotateFrom;
        std::string rotateTo;
        float       rotateAt = 0.0f;
        std::string lastAttackSide;

        float planExpiresAt = 0.0f;
        bool  llmPending = false;
        bool  llmAnswered = false;
        int   llmGen = 0;
        float llmAt = 0.0f;
        std::chrono::steady_clock::time_point llmWallAt;
        float llmLatencyWall = 0.0f;
        int   llmEventType = 0;
        int   llmRoundNo = 0;
        int   llmAliveT = 0, llmAliveCT = 0;
        TeamPlan ruleSnapshot;
        TeamPlan llmSnapshot;
        bool  hasRuleSnapshot = false;
        bool  llmBombPlanted = false;
        std::string llmPlantTarget;
        float lastThinkAt = 0.0f;
        bool  stickyLlm = false;
    };
    CoachState coachT_, coachCT_;
    int  roundStartAvgMoneyT_  = 0;
    int  roundStartAvgMoneyCT_ = 0;
    bool roundEconomySnapshotValid_ = false;

    CoachLlmFn coachLlm_;
    bool coachLlmEnabled_ = true;
    bool coachLlmRoundStartOnly_ = false;
    float coachPlanChangedAt_ = 0.0f;
    struct CoachLlmResult { Team team; int gen; std::string raw; bool debrief = false; };
    std::vector<CoachLlmResult> coachLlmResults_;
    std::mutex coachLlmMutex_;
    CoachState&       CoachFor(Team t);
    const CoachState& CoachFor(Team t) const;

    std::FILE* decisionLog_ = nullptr;
    std::FILE* planTrace_ = nullptr;
    std::FILE* wiringLog_ = nullptr;
    std::FILE* coachRoundsLog_ = nullptr;
    std::string planTraceKey_;
    std::string planTraceAdvKey_;

    void CoachUpdate(float dt);
    void CoachOnEvent(Team t, CoachEventType type, const std::string& area = std::string());
    void CoachThink(Team t, const CoachEvent& ev);
    TeamPlan RuleCoach(Team t, const CoachEvent& ev);
    TeamPlan RuleCoachCore(Team t, const CoachEvent& ev);
    void ApplyTacticsSlots(Team t, TeamPlan& plan);
    void CoachApplyPlan(Team t);
    void SanitizeCoachPlan(Team t, TeamPlan& plan);
    void CoachRecordRound(Team t, bool won);
    void CoachResetRound();
    void CoachBeginRound();
    bool LowEconomyAverage(Team t) const;
    void SnapshotRoundEconomy();
    bool AreaCenterByName(const std::string& name, float& ox, float& oy) const;
    std::string AreaHint(const std::string& intentTarget) const;
    unsigned purchaseEpoch_ = 1;
    std::uint64_t nextEquipmentId_ = 1;

    void DropEquipmentOnDeath(Player& victim);
    void ResetFootstepStride();
    void ResetFootstepStride(Player& actor);
    int  AccumulateFootstep(Player& actor, bool positional);

    bool  bombPlanted_   = false;
    float bombX_ = 0.0f, bombY_ = 0.0f;
    float bombTimer_     = 0.0f;
    float bombBeepTimer_ = 0.0f;
    float clockSec_ = 0.0f;
    float matchOverTimer_ = 0.0f;

    float lastDeathX_ = 0.0f, lastDeathY_ = 0.0f;
    Team  lastDeathTeam_ = Team::None;
    float lastDeathTimer_ = 0.0f;
    float defuseRemaining_ = 0.0f;
    float defuseProgress_ = 0.0f;

    std::vector<KillFeedEntry> killFeed_;
    std::uint64_t nextKillEventId_ = 1;

    PathFinder pathFinder_;

    Team  desiredPlayerTeam_ = Team::CT;
    WeaponDB::MatchLoadouts matchLoadouts_ = WeaponDB::DefaultMatchLoadouts();
    ActorId bombPlanter_ = kInvalidActorId;
    std::uint64_t c4EquipmentId_ = 0;
    ActorId bombCarrier_ = kInvalidActorId;
    bool  bombDropped_ = false;
    float bombDropDelay_ = 0.0f;
    float bombDropX_ = 0.0f, bombDropY_ = 0.0f;

    bool  bombIntel_ = false;
    float bombIntelX_ = 0.0f, bombIntelY_ = 0.0f;
    float bombIntelAge_ = 0.0f;
    ActorId activeDefuser_ = kInvalidActorId;

    MatchMode   mode_ = MatchMode::TeamElimination;
    TeamManager teams_;
    RoundManager round_;
    EconomySystem economy_;
    RoundPhase  prevPhase_ = RoundPhase::Freeze;
    bool        matchOverAnnounced_ = false;

    std::vector<BotRuntime> actorAi_;
    std::vector<SquadOrder> actorSquad_;
    float squadFocusTimer_ = 0.0f;

    TacticalIntel intel_;
    float lastRotateAt_ = -999.0f;
    float rotateCheckTimer_ = 0.0f;
    std::vector<EnemyContact> contactsT_;
    std::vector<EnemyContact> contactsCT_;

    float botRespawnTimer_  = 0.0f;
    static constexpr float BOT_RESPAWN_DELAY = 3.0f;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> angleDist_{0.0f, 6.283185f};

    int  PurchaseQuantityOf(const Player& actor, const Presentation::BuyRequest& request) const;
    int  PurchasePriceOf(const Presentation::BuyRequest& request) const;
    bool IsBuyWindowOpen() const;

    std::vector<Presentation::BulletVisualSegment> bulletVisuals_;
    std::uint64_t nextBulletVisualId_ = 1;
    void AddBulletVisual(const Bullet& b, float fromX, float fromY,
                         float toX, float toY, bool impact);

    void ResetToNewMatch();
    void SpawnPlayer(Player& actor);
    void SpawnBOT(Player& bot, Team team);
    bool PickSpawnForTeam(Team team, float& outX, float& outY);
    void UpdateHumanActor(Player& actor, float dt, const ActorInput& input);
    void UpdatePlayerMovement(Player& actor, float dt, bool w, bool a, bool s, bool d, bool stealth);
    void UpdatePlayerShooting(Player& actor, float dt, bool mouseDown, float worldMX, float worldMY);
    void UpdateBOTs(float dt);
    bool NudgeOutOfStuck(Player& bot, BotRuntime& ai);
    bool FindCoverPoint(const Player& bot, float threatX, float threatY,
                        float& outX, float& outY) const;
    bool EnemySpawnCentroid(Team myTeam, float& outX, float& outY) const;
    bool OwnSpawnCentroid(Team myTeam, float& outX, float& outY) const;
    bool SaveGunRetreatPoint(size_t i, float& outX, float& outY) const;

    void AssignSquadRoles(bool dynamic = false);
    void RefreshRolesForTeam(Team t);
    void UpdateSquadFocus(float dt);
    const Player* ResolveFocus(const SquadOrder& order) const;

    void UpdateTeamContacts(float dt);
    void OnFootstepSound(const Player& source, float x, float y);
    void ReportContact(Team team, ActorId id, float x, float y);
    const EnemyContact* FindNearestContact(Team team, float x, float y) const;
    std::vector<EnemyContact>&       ContactsOf(Team t);
    const std::vector<EnemyContact>& ContactsOf(Team t) const;

    void EmitSound(SoundEventType type, float x, float y, Team team,
                   ActorId sourceId = kEnvironmentActorId, float radius = 0.0f,
                   WeaponId hazardWeapon = WeaponId::None, float dangerDelay = 0);
    const HeardSound* NewestHeard(size_t botIdx, SoundEventType type,
                                  float maxAge = Tuning::SoundMemory) const;
    bool HasHeard(size_t botIdx, SoundEventType type,
                  float maxAge = Tuning::SoundMemory) const {
        return NewestHeard(botIdx, type, maxAge) != nullptr;
    }
    void AgeHeardSounds(BotRuntime& ai, float dt);
    const Player* FindVisibleEnemy(const Player& self, float& outDist) const;
    bool BotCanDefuse(size_t i, const Player& bot) const;
    ActorId PickDefuseBot() const;
    ActorId PickCoverBot(ActorId defuser) const;
    bool CanSeePoint(const Player& observer, float x, float y) const;
    int ActorPoseSlot(const Player& actor) const {
        const bool planting = actor.plantProgress > 0.0f && ActorHasC4(actor);
        return planting ? 4 : actor.weaponSlot;
    }
    struct BodyVisibility { float fraction = 0; Vec2 aim{}; };
    struct BodyVisibilityCache {
        const Player* observer = nullptr;
        const Player* target = nullptr;
        std::array<float, 15> signature{};
        BodyVisibility result;
    };
    mutable std::vector<BodyVisibilityCache> bodyVisibilityCache_;
    mutable float bodyVisibilityClock_ = -1;
    mutable std::uint64_t bodyVisibilityTerrain_ = 0;
    BodyVisibility QueryBodyVisibility(const Player& observer, const Player& target) const;
    bool CanSeeActor(const Player& observer, const Player& target) const;
    bool FollowPathTo(Player& bot, BotRuntime& ai,
                      float destX, float destY, float dt, float speedMul = 1.0f);
    void AccumulateSeparation(const Player& self, float& outX, float& outY) const;
    void ApplyAvoidance(float& vx, float& vy, float sepX, float sepY) const;
    void UpdateBullets(float dt);
    void UpdateGrenades(float dt);
    void ThrowGrenade(Player& thrower, float aimAngle, float targetDistance = -1.0f);
    bool PlanGrenadeThrow(const Player& bot, float tx, float ty, float& outAngle) const;
    float ExpectedBlastDamage(float x, float y, const Player& target) const;
    void RebuildFireCells(FireZone& fire) const;
    bool FireContains(const FireZone& fire, float x, float y) const;
    Player* DamageOwner(ActorId id) { return FindActor(id); }
    void ExplodeGrenade(const Grenade& g);
    bool SegmentBlockedBySmoke(float x1, float y1, float x2, float y2) const;
    void RespawnBOT(Player& bot);

    void MoveWithCollision(Player& entity, float dt);
    void FireBullet(float x, float y, float angle, const WeaponDef& def,
                    ActorId ownerId, Team ownerTeam);

    bool BotCanSeePlayer(const Player& bot) const;
    bool PlayerCanSeeBot(const Player& bot) const;

    void StartReload(Player& p);
    void UpdateReload(Player& p, float dt);

    bool IsEnemyNearby(float x, float y, float safeRadius, Team myTeam) const;

    void UpdateRoundFlow(float dt);
    void BeginNewRound();
    void RespawnAllForRound(bool carryWeapons = true);
    void BotBuyLoadout(Player& bot);
    void UpdateBotBuy(float dt);

    bool  botBuyPending_ = false;
    bool  botBuyDoneT_   = false;
    bool  botBuyDoneCT_  = false;
    float botBuyTimer_   = 0.0f;
    void CheckWinConditions();
    void ApplyRoundResult(Team winner);
    bool ApplyDamage(Player& victim, int amount, float armorPen = 1.0f);
    void GrantRoundMoney(Team winner);

    void SetupObjective();
    void UpdateObjective(float dt);
    void PlantBomb(float x, float y);
    void DetonateBomb();
    void OnCombatantKilled(Player& victim,
                           Player* killer = nullptr, WeaponId weapon = WeaponId::None,
                           std::uint64_t equipmentId = 0, std::optional<Vec2> sourcePosition = std::nullopt);
};
