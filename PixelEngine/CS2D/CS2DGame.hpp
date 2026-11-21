#pragma once
#include "CS2DWorld.hpp"
#include "Visibility/VisibilityQuery.hpp"
#include "CS2DRenderer.hpp"
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
#include "../Core/PresentationContract.hpp"
#include <SDL2/SDL.h>
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

class AudioManager;

// 对局模式
enum class MatchMode {
    DeathMatch,        // 自由击杀，即时重生，无回合
    TeamElimination,   // 回合制，T/CT 歼灭，先到 N 分获胜
    BombDefusal        // 爆破：T 下包 / CT 拆包
};

// 武器 → 枪声（近战/投掷物回退到通用音）
enum class SoundEffect;

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
    int     focus = -2;    // 集火目标：-2=无, -1=玩家, >=0=BOT 索引
    int     site  = -1;    // 负责的包点索引（-1 = 无 / 中路）
    bool    hasHold = false;
    float   holdX = 0.0f;
    float   holdY = 0.0f;
    bool    canTrade = true;    // 允许补枪（教练 canTrade）
    bool    canRotate = false;  // 允许自行换点/游走（lurk）
};

// ============================================================================
// CS2DGame — CS2D 对局逻辑
// ============================================================================
class CS2DGame {
public:
    enum class AnimState { Idle, Walk, Shoot, Dead };

    // ---- 战斗单位（玩家与 BOT 共用）----
    struct Player {
        bool hasDefuseKit = false;
        float x       = 0.0f;
        float y       = 0.0f;
        float vx      = 0.0f;
        float vy      = 0.0f;
        float size    = Tuning::PlayerSize;
        float defuseProgress = 0.0f;
        int defuseChannel = -1;
        float aimAngle = 0.0f;
        float aimKick = 0.0f;
        float aimDistance = Tuning::RecoilReferenceDistance;
        float recoilRecoveryTimer = 0.0f;
        struct WeaponFireState {
            WeaponId weapon = WeaponId::None;
            unsigned shot = 0;
            float inaccuracy = 0;
            SDL_FPoint kick{};
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
            int buyer = -1;
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

        SDL_FPoint BallisticKick() const {
            const auto &state = fireStates[weaponSlot];
            return state.weapon == CurrentWeapon().id ? state.kick : SDL_FPoint{};
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
        SDL_FPoint EffectiveAimVector() const {
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
        std::string name;              // BOT 昵称（玩家实体不使用）
        bool  alive   = true;
        float shootCooldown = 0.0f;

        // 精灵
        AnimState animState = AnimState::Idle;
        float     animTimer = 0.0f;
        int       weaponSlot = 0;          // 活动槽位 0=主 1=副 2=近战 3=投掷 4=C4
        int       prevWeaponSlot = 1;      // 切到投掷物之前持有的槽位（投掷物用完后自动切回）
        int       spriteKey  = 0;
        float     deathTimer = 0.0f;
        float     shootAnimTimer = 0.0f;

        // 武器（按槽位）
        WeaponId  weapons[5] = { WeaponId::None, WeaponId::USP_S, WeaponId::Knife, WeaponId::None, WeaponId::None };

        // ---- 投掷物背包 ----
        // 一局最多装备 4 个；闪光可带 2 个，其它每种 1 个；每个只能用一次
        static constexpr int MAX_GRENADES = 4;
        WeaponId  grenades[MAX_GRENADES] = { WeaponId::None, WeaponId::None,
                                             WeaponId::None, WeaponId::None };
        int       grenadeCount = 0;        // 已装备数量（0..MAX_GRENADES）
        WeaponId lastGun = WeaponId::None;
        int       grenadeSlot  = 0;        // 当前选中的投掷物下标

        // ---- 投掷物背包操作 ----
        WeaponId ActiveGrenade() const {
            if (grenadeCount <= 0 || grenadeSlot < 0 || grenadeSlot >= MAX_GRENADES) {
                return WeaponId::None;
            }
            return grenades[grenadeSlot];
        }
        // 同类型数量（用于购买上限判定）
        int CountGrenade(WeaponId id) const {
            int n = 0;
            for (int i = 0; i < grenadeCount; ++i) if (grenades[i] == id) ++n;
            return n;
        }
        // 把当前选中项同步到 weapons[3]，供渲染与 CurrentWeapon() 使用
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
        // 丢弃当前选中的投掷物（用掉后调用）
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

        // 弹药：活动槽位（缓存值，切换武器时与 ammoSlot 互相同步）
        int       ammo      = 30;
        int       maxAmmo   = 30;
        // 备弹（弹匣之外的存量）。换弹从备弹扣取，耗尽后无法再换弹。
        int       reserve   = 0;

        // ---- 每个槽位武器自己的弹药（根治「切枪后弹夹变 1 发」）----
        int       ammoSlot[5]    = { 0, 12, 0, 0, 0 };
        int       maxAmmoSlot[5] = { 0, 12, 0, 0, 0 };
        // 每个槽位自己的备弹
        int       reserveSlot[5] = { 0, 36, 0, 0, 0 };

        // 备弹总容量：弹匣容量的三倍
        static int ReserveCapacity(const WeaponDef& d) {
            // 宙斯：CS2 中仅 1 发、无备弹、不能换弹（下回合自动补满）
            if (d.cls == WeaponClass::ZeusGun) return 0;
            return (d.magazine > 0) ? d.magazine * 3 : 0;
        }

        // 装载武器到槽位，并按该武器弹匣容量填满（备弹按满配）
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

        // 装载武器到槽位，沿用指定的弹匣/备弹（捡起地上的枪时用）
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

        // 清空槽位（丢弃 / 被拿走）
        void ClearSlotWeapon(int slot) {
            if (slot < 0 || slot > 4) return;
            weapons[slot] = WeaponId::None;
            equipment[slot] = {};
            fireStates[slot] = {};
            ammoSlot[slot] = 0;
            maxAmmoSlot[slot] = 0;
            reserveSlot[slot] = 0;
        }

        // 把活动槽位的实时弹药写回该槽
        void SaveAmmoToSlot() {
            if (weaponSlot < 0 || weaponSlot > 4) return;
            if (weapons[weaponSlot] == WeaponId::None) return;
            ammoSlot[weaponSlot] = ammo;
            maxAmmoSlot[weaponSlot] = maxAmmo;
            reserveSlot[weaponSlot] = reserve;
        }

        // 从活动槽位读出弹药到实时值
        void LoadAmmoFromSlot() {
            if (weaponSlot < 0 || weaponSlot > 4) return;
            if (weapons[weaponSlot] == WeaponId::None) {
                ammo = 1;
                maxAmmo = 1;
                reserve = 0;
                return;
            }
            const auto weaponClass=WeaponDB::Get(weapons[weaponSlot]).cls;
            if (weaponClass!=WeaponClass::Melee && weaponClass!=WeaponClass::Grenade && weaponClass!=WeaponClass::Bomb) lastGun=weapons[weaponSlot];
            ammo = ammoSlot[weaponSlot];
            maxAmmo = maxAmmoSlot[weaponSlot];
            reserve = reserveSlot[weaponSlot];
        }
        bool      reloading = false;
        float     reloadTimer = 0.0f;

        bool      stealth   = false;

        // 战局数据
        int       money  = 800;
        int       kills  = 0;
        int       deaths = 0;
        int       armor  = 0;        // 护甲值（0..100）
        bool      helmet = false;    // 头盔：提高吸收比例

        // 当前活动武器定义
        const WeaponDef& CurrentWeapon() const {
            const int s = (weaponSlot < 0 || weaponSlot > 4) ? 0 : weaponSlot;
            return WeaponDB::Get(weapons[s]);
        }
    };

    // ========================================================================
    // BOT 运行时状态（与 Player 分离，玩家实体不再背这些字段）
    // 与 bots_ 按下标一一对应，统一由 EnsureBotAi() 保证长度
    // ========================================================================
    // BOT 交战姿态（决策树每帧推导，执行层只做物理与开火）
    enum class BotStance {
        None = 0,       // 无交战
        Aggressive,     // 强攻：贴近压制
        Defensive,      // 固守：保持距离周旋
        Retreat,        // 撤退：拉开距离，压制射击
        MoveToCover     // 借掩体接近
    };
    // BOT 交战时的侧向机动偏好（由姿态决定，移动仍走寻路 / 碰撞检测）
    enum class BotManeuver {
        None = 0,
        Strafe,         // 侧向拉枪（强攻/固守周旋）
        BackOff         // 后撤拉开距离（撤退）
    };

    // ========================================================================
    // BOT 运行时状态（与 Player 分离，玩家实体不再背这些字段）
    // 与 bots_ 按下标一一对应，统一由 EnsureBotAi() 保证长度
    // ========================================================================
    struct BotRuntime {
        int difficulty = 2;
        float settleTimer = 0.0f;
        int perceivedTarget = -2;
        float dodgeX = 0.0f, dodgeY = 0.0f;
        bool wasMoving = false;
        struct Alert { int enemyId = -2; float perceivedAt = 0.0f, expireAt = 0.0f, lastPerceivedAt = 0.0f; };
        std::array<Alert, 3> alerts{};
        void Perceive(int id, float now, float delay) {
            for (auto& alert : alerts) {
                if (alert.enemyId == id) {
                    alert.lastPerceivedAt = now;
                    return;
                }
            }
            auto slot = std::find_if(alerts.begin(), alerts.end(), [](const Alert& a) { return a.enemyId == -2; });
            if (slot == alerts.end()) {
                slot = std::max_element(alerts.begin(), alerts.end(), [](const Alert& a, const Alert& b) { return a.expireAt < b.expireAt; });
                if (slot->expireAt < now + delay) return;
            }
            *slot = {id, now, now + delay, now};
        }
        bool Ready(int id, float now) const {
            for (const auto& alert : alerts) if (alert.enemyId == id && now >= alert.expireAt) return true;
            return false;
        }
        BotStance   stance   = BotStance::None;    // 本帧姿态（决策树推导）
        BotManeuver maneuver = BotManeuver::None;  // 本帧机动偏好
        float     rotateTimer = 0.0f;               // lurk 换点计时

        // 寻路
        std::vector<std::pair<float, float>> path;
        int   pathIdx     = 0;
        float repathTimer = 0.0f;
        float goalX       = 0.0f;
        float goalY       = 0.0f;

        // 受击撤退
        float retreatTimer = 0.0f;
        float threatX      = 0.0f;
        float threatY      = 0.0f;
        float lastHitTimer = 0.0f;   // 最近受击：拆包时据此判断是否放弃拆包反击
        float defuseCooldown = 0.0f; // 放弃拆包后的冷却，避免立刻又贴回去

        // 状态标记
        float blindTimer     = 0.0f;   // 闪光致盲剩余秒数
        bool  defusing       = false;  // 正在拆包
        float heardLogCooldown = 0.0f; // 听声日志节流（调试用）
        float reactionLogCooldown = 0.0f; // 听声决策日志节流（调试用）

        // ---- 听觉：本 BOT 最近听到的声音（由 CS2DGame::EmitSound 写入）----
        // 只保留每种类型最新的一条，最多 kMaxHeard 条；超过 SoundMemory 丢弃。
        std::vector<HeardSound> heard;
        // 听到敌对声音后的警戒朝向（没有可见目标时朝这里转）
        float alertTimer = 0.0f;
        float alertX = 0.0f, alertY = 0.0f;
        float grenadeCooldown = 0.0f;  // 投掷冷却
        bool  tradePushed     = false; // 本回合是否已补过枪
        bool  scoped = false; // BOT scopes while tracking a visible target with a scoped weapon
        bool  hadTarget       = false; // 上一帧是否看得见敌人（决定是否原地扫描）
        float scanPhase       = 0.0f;  // 扫视相位（行走时左右小幅摆头）
        float scanBase        = 0.0f;  // 原地扫视的中心朝向
        float wanderX         = 0.0f;  // 巡逻目标（缓存，避免每帧重掷导致抖动）
        float wanderY         = 0.0f;
        float wanderTimer     = 0.0f;  // >0 时沿用缓存目标
        float detourX = 0.0f, detourY = 0.0f;   // 卡住时的绕行方向（单位向量）
        float detourTimer = 0.0f;               // >0 时沿 detour 方向走
        bool  scanBaseValid   = false;
        // 脚步：按实际位移累计（与玩家同一套）。累计满 Tuning::FootstepStride 触发一步，
        // 余数保留，长期精确；死亡/重生/传送由 ResetFootstepStride() 清零。
        float footstepStride  = 0.0f;  // 已累计的位移（px）
        float footstepLastX   = 0.0f;  // 上一帧位置（算 |Δpos|）
        float footstepLastY   = 0.0f;
        bool  footstepLastValid = false;

        // 卡墙检测
        float lastX = 0.0f, lastY = 0.0f;
        float stuckTimer = 0.0f;

        // ---- 目标（唯一真源）----
        // 教练计划与小队轮转都可能给出驻守点；两者统一经 SetBotGoal() 写入，
        // 来源优先级 Coach > Squad，避免两套目标互相覆盖（曾长期产生 dual_goal 冲突）。
        enum class GoalSource : unsigned char { None = 0, Squad = 1, Coach = 2 };
        GoalSource goalSource = GoalSource::None;
        bool  hasPlanHold = false;
        float planHoldX = 0.0f, planHoldY = 0.0f;
        std::string goalArea;

        // ---- 交火机动记忆（防前后抖动 / 侧拉乱换向）----
        int   moveLatch = 0;             // +1 前进 / -1 后退 / 0 无
        float moveLatchTimer = 0.0f;     // 方向保持剩余时间
        float strafeSide = 1.0f;         // 侧拉方向：+1 / -1
        float strafeSideTimer = 0.0f;    // 侧拉方向保持剩余时间
        int   prevLatch = 0;             // 上一帧的前进/后退方向（度量翻转用）
        float assistCooldown = 0.0f;     // 支援前压冷却
        std::string searchArea;          // 正在搜的区域名
        float searchTimer = 0.0f;        // 在该区域的确认计时
        float lookAngle = -99.0f;        // 守点时应该看的方向（弧度，tactics [look]）
        // 计划卡槽位的可执行字段（到位后的站姿 / 投掷）
        int   holdStyle = 0;             // 0 默认 1 架点 2 侧身 3 被动
        int   utilKind  = 0;             // 0 无 1 烟 2 闪 3 火 4 雷
        float utilX = 0.0f, utilY = 0.0f;
        bool  utilDone = true;

        // ---- 决策 trace 的上一帧快照（仅在开启 trace 时写入，不影响玩法）----
        int   traceTarget   = -2;    // 上一帧锁定的敌人（-2 无）
        int   traceStance   = 0;
        int   traceManeuver = 0;
        int   traceGoalSrc  = 0;
        float traceGoalX    = 0.0f, traceGoalY = 0.0f;
        bool  traceDefusing = false;
    };

    struct Bullet {
        std::uint64_t equipmentId=0;
        SDL_FPoint sourcePosition{};
        float x  = 0.0f;
        float y  = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float traveled = 0.0f;
        float maxDist  = 1600.0f;
        int   damage   = 25;
        int   ownerIdx  = -1;        // -1=玩家, >=0=BOT索引
        Team  ownerTeam = Team::None;
        // 发射该子弹的武器：击杀奖励按武器类型发放（照抄 CS2）
        WeaponId weapon = WeaponId::None;
        bool  active   = true;
    };

    // 投掷物
    struct Grenade {
        std::uint64_t equipmentId=0;
        float    x = 0.0f, y = 0.0f;
        float    vx = 0.0f, vy = 0.0f;
        float    fuse = 1.5f;
        WeaponId type = WeaponId::GrenadeHE;
        Team     team = Team::None;
        bool     active = true;
        int owner = -2;
        WeaponId imitate = WeaponId::None;
        bool     landingWarned = false;   // 落点预警已广播（只发一次）
        int      wallBounces = 0;         // 撞墙反弹次数
        float    spin = 0.0f;             // 飞行旋转角（度）
        float    bounceHearCooldown = 0.0f; // 撞墙发声节流
    };

    // 烟雾云（遮挡视线）
    // 燃烧弹 / 燃烧瓶留下的火区
    struct Decoy {
        float x=0,y=0,life=15,nextShot=0;
        int shot=0;
        int owner=-2;
        Team team=Team::None;
        WeaponId weapon=WeaponId::Glock18;
    };
    struct FireZone {
        std::uint64_t equipmentId=0;
        float x = 0.0f, y = 0.0f;
        int owner = -2;
        WeaponId weapon = WeaponId::GrenadeMolotov;
        std::vector<SDL_FPoint> cells;
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
        float life = 12.0f;        // 剩余寿命
        float maxLife = 12.0f;     // 初始寿命（用于淡出）
        float age = 0.0f;          // 已存在时间（用于爆开 + 向外扩张）
        float expandTime = 1.6f;   // 从中心扩散到 full radius 的时间
        float seed = 0.0f;         // 每团烟的随机种子（形状/旋转）
    };

    // 击杀播报条目
    struct KillFeedEntry {
        std::uint64_t equipmentId=0;
        SDL_FPoint sourcePosition{};
        std::uint64_t id=0;
        float timestamp=0;
        std::string text;
        float life = 4.0f;
        std::string killer, victim;
        Team killerTeam = Team::None, victimTeam = Team::None;
        WeaponId weapon = WeaponId::None;
        bool personal = false;
    };
    struct DeathPresentation {
        KillFeedEntry event;
        float remaining = 0.0f;
    };
    static constexpr float DeathFreezeSeconds = 1.8f;
    bool IsDeathFreeze() const { return !player_.alive && deathPresentation_.remaining > 0.0f; }
    const DeathPresentation& GetDeathPresentation() const { return deathPresentation_; }

    // 记分板行
    struct ScoreRow {
        std::string name;
        Team team = Team::None;
        int  kills = 0;
        int  deaths = 0;
        int  money = 0;
        bool alive = true;
        bool isPlayer = false;
    };

    CS2DGame();
    ~CS2DGame() = default;

    bool LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer,
                 bool resetMatch = true);
    void Reset();

    // 选阵营阶段：只画地图，不生成玩家/BOT。
    void RenderMapPreview(int viewW, int viewH, float centerX, float centerY, float zoom,
                          bool clampToMap = true);
    void GetTeamSpawnCenter(Team team, float& outX, float& outY) const;

    // 当前地图 id（与 MapCatalog / coach/<mapId>.txt 对齐）
    const std::string& GetMapId() const { return mapId_; }
    const TacticsTable& Tactics() const { return tactics_; }

    // ---- 行为计数（开发期度量，不影响玩法）----
    struct DebugCounters {
        int intentChanges = 0;    // 队伍意图变更次数
        int roleSwitches  = 0;    // BOT 角色变更次数
        int assistPushes  = 0;    // 支援前压次数
        int nadeThrows    = 0;    // 成功投掷
        int nadeSkips     = 0;    // 因落点不可行放弃
        int searchVisits  = 0;    // 搜点次数（到达未搜区域）
        int llmPlans      = 0;    // LLM 计划被采纳
        int llmAdvisory   = 0;    // 顾问模式：只记录不执行
        int llmLate       = 0;    // 迟到丢弃（决策期间局面已变）
        int renewals      = 0;    // 计划到期续期次数
    };
    const DebugCounters& Counters() const { return counters_; }
    DebugCounters& MutableCounters() { return counters_; }

    // ---- 决策 trace（开发期）：把 BOT 的决策变化写成 CSV，用于解释"为什么这么走" ----
    // 只在显式开启时写文件；关闭时零开销、零行为影响。
    // ---- 教练计划 trace（评估 LLM vs 规则）：每条下发的计划写一行 ----
    void EnablePlanTrace(const std::string& path);
    void ClosePlanTrace();
    void TraceCoachPlan(Team t);
    void TraceCoachAdvisory(Team t, const TeamPlan& plan);   // 顾问计划（只记录）
    // ---- T01 结构化教练决策日志（JSONL）：每次下发规则计划写一行 ----
    void EnableWiringLog(const std::string& path);
    void CloseWiringLog();
    void TraceWiring(Team t, const char* guard, const char* sanitized, const char* templateKey);
    // ---- T36 coach_rounds.jsonl：每回合每队写一行（老师数据，写盘点不动）----
    void EnableCoachRoundsLog(const std::string& path);
    void CloseCoachRoundsLog();
    void TraceCoachRound(Team t);
    // ---- 角色速度倍率（含 T27 过渡插值）----
    float RoleSpeedMul(size_t botIdx, BotRole role);
    // 换角色时记录旧倍率与切换时刻（T27：3 秒插值过渡起点）
    void SetRoleWithTransition(size_t botIdx, BotRole role);
    // 计划自洽：target 与槽位是否匹配（A/B 至少 2 个槽位在同侧；MID 至少 1 个）
    char AreaSideOf(const std::string& area) const;   // 区域属于哪一侧 A/B/M/?
    bool PlanTargetConsistent(Team t, const TeamPlan& plan) const;
    // 用战术表的 [slots.*] 模板覆盖计划槽位（下包/回防用），返回是否覆盖成功
    bool ApplySlotTemplate(Team t, TeamPlan& plan);
    // ---- 决策对比日志：同一决策点上「规则计划 / LLM 计划 / 最终执行」----
    void EnableDecisionLog(const std::string& path);
    void CloseDecisionLog();
    void TraceCoachDecision(Team t, const char* status);
    std::FILE* decisionLog_ = nullptr;
    std::FILE* planTrace_ = nullptr;
    std::FILE* wiringLog_ = nullptr;   // T01：结构化教练决策日志（JSONL）
    std::FILE* coachRoundsLog_ = nullptr;   // T36：老师数据（一行一队一回合）
    std::string planTraceKey_;   // 去重：同一条计划只写一次
    std::string planTraceAdvKey_; // 顾问计划去重键

    void EnableDecisionTrace(const std::string& path);
    void TraceBotDecision(size_t i, const Player* target);   // 内部：有变化才写一行
    void CloseDecisionTrace();
    // 当前地图使用的战术库 id（MapCatalog.playbook 可覆盖；空时同 mapId_）
    const std::string& GetPlaybookId() const { return playbookId_; }

    void Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                bool wPressed, bool aPressed, bool sPressed, bool dPressed,
                bool shiftPressed, bool rPressed,
                bool interactHeld,
                class AudioManager* audio = nullptr);

    void Render(int viewW = 800, int viewH = 600);

    // ---- 访问器 ----
    CS2DWorld&       GetWorld()       { return *world_; }
    // 视线/视野几何（玩法层与之共用同一实现，不再依赖渲染器）
    VisibilityQuery&       Visibility()       { return visibility_; }
    const VisibilityQuery& Visibility() const { return visibility_; }
    const CS2DWorld& GetWorld() const { return *world_; }
    CS2DRenderer*    GetRenderer()    { return renderer_; }

    Player&          GetPlayer()      { return player_; }
    const Player&    GetPlayer() const { return player_; }

    // 仅回归测试使用：走真实死亡流程（OnCombatantKilled）杀死玩家，
    // 用来验证「死斗 = 原地死亡镜头、其它模式 = 观察者」这类模式相关的镜头行为。
    void DebugKillPlayerForTest() {
        if (player_.alive) OnCombatantKilled(player_, nullptr, nullptr);
    }

    std::vector<Player>&       GetBOTs()       { return bots_; }
    const std::vector<Player>& GetBOTs() const { return bots_; }
    const std::vector<Bullet>& GetBullets() const { return bullets_; }
    // 枪弹视觉段（契约 v1 第 6 节）：玩法层生成的唯一数据源，UI 只绘制。
    const std::vector<Presentation::BulletVisualSegment>& GetBulletVisuals() const {
        return bulletVisuals_;
    }
    const std::vector<Grenade>& GetGrenades() const { return grenades_; }
    const std::vector<SmokeCloud>& GetSmokes() const { return smokes_; }
    // 世界某点被烟雾遮蔽的程度 0..1（0=完全看得见，1=完全被烟盖住）。
    // 与烟雾自身的稀疏程度匹配：中心浓、边缘淡、消散时整体变淡。
    // 渲染层用它给人物做「进烟渐隐 / 出烟渐显」。
    float SmokeOpacityAt(float x, float y) const;
    const std::vector<FireZone>& GetFires() const { return fires_; }
    const std::vector<KillFeedEntry>& GetKillFeed() const { return killFeed_; }
    std::vector<ScoreRow> BuildScoreboard() const;

    // 玩家是否处于闪光致盲中（0..1 强度）
    float GetFlashAmount() const { return (flashTimer_ > 0.0f) ? 1.0f : 0.0f; }

    // 命中 / 受伤反馈计时（秒，>0 表示需要绘制）
    void RequestSpectateToggle() { spectateTogglePending_ = true; }
    void QuickSwitchWeapon();
    float GetAimMotionBlur() const { return aimMotionBlur_; }
    // 当前是否允许移动（回合冻结 / 结算展示等阶段为 false）。
    // 表现层用它决定「移动模糊」是否该生效：不能动的时候瞄准线不该发虚。
    bool IsMovementAllowed() const {
        if (!player_.alive) return false;
        return mode_ == MatchMode::DeathMatch || !round_.IsFrozen();
    }
    enum class HitMarkerKind { Normal, Armor, Kill };
    HitMarkerKind GetHitMarkerKind() const { return hitMarkerKind_; }
    float GetHitMarkerTimer()   const { return hitMarkerTimer_; }
    float GetDryFireHintTimer() const { return dryFireHintTimer_; }
    float GetReloadProgress() const {
        const auto* observed = GetSpectatedPlayer();
        const auto& actor = observed ? *observed : player_;
        if (!actor.alive || !actor.reloading || actor.CurrentWeapon().reloadTime <= 0) return 0;
        return std::clamp(1.0f - actor.reloadTimer / actor.CurrentWeapon().reloadTime, 0.0f, 1.0f);
    }
    using DamageDirection = Presentation::DamageDirection;
    const std::array<DamageDirection, 4>& GetDamageDirections() const { return damageDirections_; }
    float GetDamageFlashTimer() const { return damageFlashTimer_; }

    // 敌人情报（团队共享；id: -1=玩家, >=0=BOT 索引）
    struct EnemyContact {
        int   id  = -1;
        float x   = 0.0f;
        float y   = 0.0f;
        float age = 0.0f;
    };

    // ---- 供 LLM 提示词读取的战术态势 ----
    const std::vector<SquadOrder>& GetSquadOrders() const { return squad_; }
    const std::vector<EnemyContact>& GetContactsOfTeam(Team t) const { return ContactsOf(t); }
    float GetLastDeathX() const { return lastDeathX_; }
    float GetLastDeathY() const { return lastDeathY_; }
    float GetLastDeathTimer() const { return lastDeathTimer_; }
    Team  GetLastDeathTeam() const { return lastDeathTeam_; }
    // 屏幕中央提示（空字符串表示不显示）
    const std::string& GetAnnouncement() const { return announcement_; }
    float GetAnnouncementTimer() const { return announcementTimer_; }
    int   GetBombSiteCount() const {
        return world_ ? static_cast<int>(world_->GetBombSites().size()) : 0;
    }
    bool  GetBombSiteCenter(int index, float& cx, float& cy) const {
        if (!world_) return false;
        const auto& sites = world_->GetBombSites();
        if (index < 0 || index >= static_cast<int>(sites.size())) return false;
        cx = sites[index].cx;
        cy = sites[index].cy;
        return true;
    }

    // ---- BOT 运行时状态：与 bots_ 同下标（AI 层与 LLM 回调用） ----
    void EnsureBotAi();
    BotRuntime&       AiAt(size_t i);
    const BotRuntime& AiAt(size_t i) const;
    size_t AiIndexOf(const Player& p) const;
    void   ResetBotAi(size_t i);

    // ---- 目标唯一写入入口 ----
    // 所有"该去哪守着"的写入（教练计划 / 小队轮转 / 游走换点）都必须走这里：
    // 来源优先级 Coach > Squad，低优先级不会覆盖高优先级；同时同步 squad_ 的镜像字段，
    // 保证 hasPlanHold/planHold 与 hasHold/hold 永远一致。
    void SetBotGoal(size_t i, float x, float y, BotRuntime::GoalSource src, bool force = false);
    void ClearBotGoal(size_t i);

    // ---- 目标物（爆破）----
    bool  IsBombPlanted()   const { return bombPlanted_; }
    float GetBombTimer()    const { return bombTimer_; }
    float GetBombX()        const { return bombX_; }
    float GetBombY()        const { return bombY_; }
    float GetDefuseProgress() const { return defuseProgress_; }
    float GetDefuseRemaining() const { return defuseRemaining_; }
    // 能否拆包：在 DefuseRadius 内，且拆包者到 C4 之间没有墙体遮挡。
    // 拆包 / BOT 资格判定 / 下包中断共用这一处几何，避免三处漂移。
    bool IsInDefuseRange(float x, float y) const {
        if (!bombPlanted_) return false;
        const float dx = x - bombX_, dy = y - bombY_;
        if (dx * dx + dy * dy > Tuning::DefuseRadius * Tuning::DefuseRadius) return false;
        // 中间存在遮挡物（挡弹瓦片）时不能拆包：隔着墙摸包不算数。
        return visibility_.HasLineOfSight(x, y, bombX_, bombY_);
    }
    // 累计触发过的步数（玩家本人）：只读统计，供回归断言与调试，不影响玩法
    std::uint64_t PlayerFootstepCount() const { return footstepCount_; }
    // 拆包进度对观看者是否可见：拆包者与观看者同阵营才可见。
    // 观看者恒为玩家本人的阵营（观战时仍是阵亡玩家自己的阵营，见 Sun 的决定）。
    // 对手侧完全不可见——不留名字、百分比、秒数或横幅位移等任何可推断的痕迹。
    bool IsDefuseVisibleToViewer() const {
        if (defuseProgress_ <= 0.0f) return false;
        const Team defuserTeam =
            activeDefuser_ == -1
                ? player_.team
                : (activeDefuser_ >= 0 && activeDefuser_ < static_cast<int>(bots_.size())
                       ? bots_[static_cast<size_t>(activeDefuser_)].team
                       : Team::None);
        if (defuserTeam == Team::None) return false;
        return defuserTeam == player_.team;
    }
    std::string GetDefuserName() const {
        if (activeDefuser_ == -1) return "你";
        if (activeDefuser_ >= 0 && activeDefuser_ < static_cast<int>(bots_.size())) return bots_[activeDefuser_].name;
        return {};
    }

    int GetPlayerKills()  const { return playerKills_; }
    int GetPlayerDeaths() const { return playerDeaths_; }
    int GetBotCount()     const { return static_cast<int>(bots_.size()); }
    void SetBotDifficulty(int difficulty) { for (auto& ai : botAi_) ai.difficulty = std::clamp(difficulty, 0, 3); }
    void SetBotProfile(size_t bot, int profile) { AiAt(bot).difficulty = std::clamp(profile, 0, 7) / 2; }
    unsigned PlayerSpawnVersion() const { return playerSpawnVersion_; }
    void SetAimTarget(float x, float y, bool followsPlayer = false);
    void PrepareCamera(int viewW, int viewH, float interpolation = 1.0f);
    struct ActorView {
        float x = 0.0f, y = 0.0f;
        bool alive = false;
        float aimAngle = 0.0f, animTimer = 0.0f;
        AnimState animState = AnimState::Idle;
    };
    ActorView GetActorView(const Player& actor) const;
    float GetRenderAimX() const;
    float GetRenderAimY() const;
    float GetRenderAimAngle() const;
    bool CameraPrepared() const { return cameraPrepared_; }
    bool CameraScopeTransitionComplete() const { return cameraScopeSettled_; }
    static float ScopeCameraShiftForZoom(float zoom);
    bool EdgeZoomRequested() const { return edgeZoomRequested_; }
    float EdgeZoomAmount() const { return edgeZoom_; }
    void SetEdgeZoomRequested(bool requested) { edgeZoomRequested_ = requested; cameraDistanceTier_ = requested ? 1 : 0; }
    void SetCameraDistanceTier(int tier) { cameraDistanceTier_=std::clamp(tier,0,2);edgeZoomRequested_=tier>0; }
    bool AimFollowsPlayer() const { return aimFollowsPlayer_; }
    float GetAimAngle()   const { return player_.EffectiveAim(); }
    // 0=静止，~0.42=静步，1=全速跑动。准星与弹道共用这一份移动扩散。
    float GetCrosshairBloom() const;
    // 开火后坐力造成的准星扩散 0..1：取当前武器弹道累积散布（inaccuracy）归一化。
    // 与移动扩散分开计算、在表现层相加，这样「边走边打」也不会互相吞掉。
    float GetRecoilBloom() const;

    // ---- 阵营 / 回合 ----
    MatchMode  GetMatchMode()   const { return mode_; }
    // 固定随机种子（确定性测试 / 无头基准 / 回放用）。正常游玩不调用。
    void       SetRngSeed(uint32_t seed) { rng_.seed(seed); }
    void       SetMatchMode(MatchMode m) { mode_ = m; }

    int  GetScoreT()      const { return teams_.ScoreT(); }
    int  GetScoreCT()     const { return teams_.ScoreCT(); }
    int  GetScoreToWin()  const { return teams_.ScoreToWin(); }

    bool IsRoundDecided() const { return round_.IsDecided(); }
    RoundPhase GetRoundPhase()      const { return round_.Phase(); }
    float      GetRoundRemaining()  const { return round_.PhaseRemaining(); }
    int        GetRoundNumber()     const { return round_.RoundNumber(); }
    Team       GetLastRoundWinner() const { return round_.LastWinner(); }
    bool       IsFrozen()           const { return round_.IsFrozen(); }

    // 覆盖回合时长配置（调试 / 测试用；同时重置回合计时）
    void SetRoundConfig(const RoundConfig& cfg) {
        round_.Reset(cfg);
        prevPhase_ = round_.Phase();
    }

    bool IsMatchOver()   const { return teams_.MatchOver(); }
    // ---- 玩家阵营（爆破模式可选 T / CT）----
    void SetPlayerTeam(Team t) { desiredPlayerTeam_ = t; }
    // 选阵营后重建首回合（地图保持已加载，不重新 LoadMap）
    void RestartMatchWithTeam(Team t);
    Team GetPlayerTeam() const { return desiredPlayerTeam_; }
    bool ActorHasC4(const Player& actor) const {
        return mode_==MatchMode::BombDefusal && !bombPlanted_ && !bombDropped_ && actor.alive &&
            ((bombCarrier_==-1 && &actor==&player_) || (bombCarrier_>=0 && bombCarrier_<static_cast<int>(bots_.size()) && &actor==&bots_[bombCarrier_]));
    }
    bool PlayerHasC4() const {
        return bombCarrier_ == -1 && player_.team == Team::T &&
               !bombPlanted_ && !bombDropped_;
    }
    // Team-only objective information: never expose the enemy carrier to CT.
    std::string GetFriendlyC4CarrierName() const {
        if (mode_ != MatchMode::BombDefusal || player_.team != Team::T ||
            bombPlanted_ || bombDropped_) return {};
        if (bombCarrier_ == -1 && player_.alive) return "你";
        if (bombCarrier_ < 0 || bombCarrier_ >= static_cast<int>(bots_.size())) return {};
        const auto& carrier = bots_[static_cast<size_t>(bombCarrier_)];
        if (!carrier.alive || carrier.team != Team::T) return {};
        return carrier.name.empty() ? "BOT#" + std::to_string(bombCarrier_) : carrier.name;
    }
    bool PlayerHoldingC4() const { return playerHoldingC4_; }
    bool IsBombDropped() const { return bombDropped_; }
    int GetBombCarrierId() const { return GetBombCarrierPos() ? bombCarrier_ : -2; }
    std::optional<SDL_FPoint> GetBombCarrierPos() const {
        if (ActorHasC4(player_)) return SDL_FPoint{player_.x, player_.y};
        if (bombCarrier_ >= 0 && bombCarrier_ < static_cast<int>(bots_.size())) {
            const auto& carrier = bots_[bombCarrier_];
            if (ActorHasC4(carrier)) return SDL_FPoint{carrier.x, carrier.y};
        }
        return std::nullopt;
    }
    float BombDropX() const { return bombDropX_; }
    float BombDropY() const { return bombDropY_; }
    bool  HasBombIntel() const { return bombIntel_; }
    float BombIntelX() const { return bombIntelX_; }
    float BombIntelY() const { return bombIntelY_; }
    void ToggleHoldC4();
    void UpdateBombItem(float dt);
    void DropC4(float x, float y);
    void EquipFallbackWeapon();   // 当前槽不可用时切到有武器的槽并读弹药
    void AssignBombCarrier();     // 每回合分配 C4 携带者（含 bot 创建后补一次）
    float GetPlantProgress() const { return plantProgress_; }

    // ---- 下包 / 拆包中的「发声者」 ----
    // 供 BOT 决策树做听声扑人：返回正在下包(planting=true)或正在拆包的目标。
    // 没有人在做这两件事时返回 nullptr。
    struct SoundMaker {
        const Player* who   = nullptr;   // 执行者（可能是玩家或 BOT）
        float         x     = 0.0f;      // 发声位置
        float         y     = 0.0f;
        bool          isPlant  = false;  // true=下包中, false=拆包中
    };
    // 若有人正在下包/拆包，返回其信息。拆包优先级高于下包（同一时刻不会共存）。
    bool GetActiveSoundMaker(SoundMaker& out) const {
        if (mode_ != MatchMode::BombDefusal) return false;

        // 拆包：CT 在炸弹旁且拆包进度 > 0
        if (bombPlanted_ && defuseProgress_ > 0.0f) {
            out.x = bombX_;
            out.y = bombY_;
            out.isPlant = false;
            if (activeDefuser_ == -1 && player_.alive && player_.defuseProgress > 0) {
                out.who = &player_;
                return true;
            }
            if (activeDefuser_ >= 0 && activeDefuser_ < static_cast<int>(bots_.size()) &&
                bots_[activeDefuser_].alive && bots_[activeDefuser_].defuseProgress > 0) {
                out.who = &bots_[activeDefuser_];
                return true;
            }
            return false;
        }

        // 下包：T 携带者站在包点内且已开始累积安装进度
        const float plantProg = std::max(plantProgress_, botPlantProgress_);
        if (!bombPlanted_ && !bombDropped_ && plantProg > 0.0f &&
            bombCarrier_ >= -1) {
            if (bombCarrier_ == -1 && player_.alive && player_.team == Team::T) {
                out.who = &player_;
                out.x = player_.x;
                out.y = player_.y;
                out.isPlant = true;
                return true;
            }
            if (bombCarrier_ >= 0 &&
                bombCarrier_ < static_cast<int>(bots_.size())) {
                const Player& carrier = bots_[static_cast<size_t>(bombCarrier_)];
                if (carrier.alive && carrier.team == Team::T) {
                    out.who = &carrier;
                    out.x = carrier.x;
                    out.y = carrier.y;
                    out.isPlant = true;
                    return true;
                }
            }
        }
        return false;
    }

    // ---- 观察者（死亡后）----
    enum class SpectateMode { None = 0, Follow, Free, Bomb };
    bool          IsSpectating() const { return specMode_ != SpectateMode::None && !player_.alive; }
    SpectateMode  GetSpectateMode() const { return specMode_; }
    int           GetSpectateTarget() const { return specTarget_; }
    // 当前被观战的 BOT。自由视角下返回 nullptr：那一视角的视野是「存活队友视锥的
    // 并集」，没有单一被观战者；specTarget_ 仅保留给切回跟随时恢复目标用。
    const Player* GetSpectatedPlayer() const {
        if (specMode_ == SpectateMode::Follow && !IsDeathFreeze() &&
            specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size()) &&
            bots_[static_cast<size_t>(specTarget_)].alive) {
            return &bots_[static_cast<size_t>(specTarget_)];
        }
        return nullptr;
    }
    std::string GetSpectatedName() const {
        const Player* p = GetSpectatedPlayer();
        if (p && !p->name.empty()) return p->name;
        if (specTarget_ >= 0) return "BOT#" + std::to_string(specTarget_);
        return "-";
    }
    void SpectatorToggle();
    void SpectatorCycle(int dir);
    void SpectatorMoveFree(float dx, float dy, float dt);
    void SpectatorBegin();
    void SpectatorEnd();
    // 音效听者位置：观战时以镜头/被观战者为准
    // 死斗：记住"上一次购买的武器"，每次重生自动装配（仍可在重生后继续购买）
    struct DmLoadout {
        WeaponId primary = WeaponId::None;
        WeaponId secondary = WeaponId::None;
        bool valid = false;
    };
    const DmLoadout& GetDmLoadout() const { return dmLoadout_; }
    // 把当前武器槽记为"上一次购买的装备"（调试装备也走这里，便于验收重生装配）
    void RememberCurrentLoadoutAsDm() {
        if (mode_ != MatchMode::DeathMatch) return;
        dmLoadout_.primary   = player_.weapons[0];
        dmLoadout_.secondary = player_.weapons[1];
        dmLoadout_.valid     = (dmLoadout_.primary != WeaponId::None ||
                                dmLoadout_.secondary != WeaponId::None);
    }
    // 死斗：阵亡后原地停留（死亡镜头），不进入观察者、不切到其他 BOT
    bool IsDeathCam() const { return deathCam_; }
    bool deathCam_ = false;
    DmLoadout dmLoadout_;   // 死斗重生装配用（Reset 时清空）
    float ListenerX() const;
    float ListenerY() const;
    void SetObjectiveAudioPaused(bool paused);
    void StopObjectiveAudio(AudioManager* audio);
    Team GetMatchWinner() const { return teams_.MatchWinner(); }
#ifdef PIXEL_DEV_REPLAY
    // 开发期：直接把比分推到赛点，用于预览结算页（release 构建不存在）
    void DebugForceMatchEnd(Team winner) {
        for (int i = 0; i < teams_.ScoreToWin(); ++i) teams_.AddScore(winner);
    }
#endif

    // 各阵营存活人数
    int AliveCount(Team t) const;

    // ---- 经济 / 购买 ----
    int  GetPlayerMoney() const { return player_.money; }
    // 买枪前经济快照（team 的平均余额；给 LLM digest / 教练用）
    int  GetRoundStartEconomy(Team t) const;
    // 购买窗口：自回合开始起的前 BuyWindowSeconds 秒（覆盖冻结 + 购买 + 交战初期）
    bool IsBuyPhase() const {
        // 死斗：只有站在出生点的购买区内才能买，离开就关闭
        if (mode_ == MatchMode::DeathMatch) {
            return player_.alive && IsPlayerInBuyZone();
        }
        if (teams_.MatchOver()) return false;
        if (!player_.alive) return false;
        return round_.Elapsed() <= Tuning::BuyWindowSeconds;
    }
    float GetRoundElapsed() const { return round_.Elapsed(); }
    // 比赛结束后经过的时间（用于进入结算页）
    float GetMatchOverTimer() const { return matchOverTimer_; }
    // 玩家当前是否处于购买区内（地图未定义购买区时视为全图可买）
    bool IsPlayerInBuyZone() const;
    // 尝试购买；成功返回 true（已扣款并装备）
    bool TryBuy(WeaponId id, class AudioManager* audio = nullptr);
    // 购买护甲：tier 1=护甲 2=护甲+头盔
    bool TryBuyArmor(int tier, class AudioManager* audio = nullptr);

    // ---- 购买契约 v1（第 3 节）----
    // EvaluatePurchase：纯判定，无扣款/装备/音效副作用，供 UI 逐项显示可用性。
    struct RefundQuote { int amount = 0; int quantity = 0; std::string reason; };
    RefundQuote EvaluateRefund(const Presentation::BuyRequest& request) const;
    bool ExecuteRefund(const Presentation::BuyRequest& request);
    Presentation::BuyCode EvaluatePurchase(const Presentation::BuyRequest& request) const;
    // ExecutePurchase：点击时重新判定，Ok 才执行一次原装备/扣款与 EQUIP 音效。
    Presentation::BuyResult ExecutePurchase(const Presentation::BuyRequest& request,
                                            class AudioManager* audio = nullptr);

    // ---- 开镜契约 v1（第 4 节）----
    // Engine 每帧在 game.Update 之前调用一次；gameplayInputAllowed=false 时立即归零。
    void UpdateScopeInput(bool rightClicked, bool gameplayInputAllowed);
    // 只读快照，无消费副作用。
    Presentation::ScopeView GetScopeView() const;
    Presentation::ScopeView GetRenderScopeView() const;
    // 立即归零（幂等）：不动伤害/散布/移速。
    void ResetScope(Presentation::ScopeResetReason reason);
    static bool IsScopeSupported(WeaponId id);
    // 契约 v1.2：开镜后开枪。关闭"延迟关镜"时立即归零；
    // 开启且按着扳机（triggerHeld）时推迟到松手（见 Update 里的收尾）。
    void SetDelayedScopeClose(bool on) { delayedScopeClose_ = on; }
    bool IsDelayedScopeClose() const { return delayedScopeClose_; }
    // 契约 v1.3：开镜大十字的线宽与「移动时是否张开」（设置项，由 Engine 注入）
    void SetScopeCrosshair(float thickness, bool dynamicSpread) {
        scopeCrossThickness_ = std::clamp(thickness, 1.0f, 8.0f);
        scopeCrossDynamic_   = dynamicSpread;
    }
    float ScopeCrosshairThickness() const { return scopeCrossThickness_; }
    bool  ScopeCrosshairDynamic()   const { return scopeCrossDynamic_; }

    // 护甲结算（契约 v1.2）：纯函数，ApplyDamage 与回归测试共用
    struct ArmorDamageResult {
        int healthDamage = 0;   // 实际扣血
        int armorLoss = 0;      // 护甲点数消耗
    };
    static ArmorDamageResult ResolveArmorDamage(int amount, float armorPen, int armorPoints);

    // 敌我判定（死斗 = 真正的 FFA 混战：除自己外所有人都是敌人）。
    // 玩法层所有"是否敌对"的判断都必须走这里，不要再散落 `team == team`。
    bool IsHostileTeam(Team a, Team b) const {
        if (mode_ == MatchMode::DeathMatch) return true;
        return a != b;
    }
    bool IsHostile(const Player& a, const Player& b) const {
        return &a != &b && IsHostileTeam(a.team, b.team);
    }
    bool IsDeathMatch() const { return mode_ == MatchMode::DeathMatch; }
    int  GetPlayerArmor()  const { return player_.armor; }
    int  GetPlayerHealth()    const { return player_.health; }
    int  GetPlayerMaxHealth() const { return player_.maxHealth; }
    // 闪光致盲：距离 → 秒数 / 强度（起爆与自检共用同一份公式）
    static float ComputeFlashBlind(float dist);
    static float ComputeFlashIntensity(float dist);
    // ---- 教练层（L3）----
    const TeamPlan&   GetCoachPlan(Team t) const;
    const std::string& GetCoachReason(Team t) const;
    // 当前计划已生效多久（秒），用于 HUD 变更高亮
    float GetCoachPlanAge() const { return clockSec_ - coachPlanChangedAt_; }

    // 教练层 LLM 接入：由 Engine 注入（system, user, maxTokens, 回调原文）
    using CoachLlmFn = std::function<void(const std::string&, const std::string&, int,
                                          const std::string&,
                                          std::function<void(const std::string&)>)>;
    void SetCoachLlm(CoachLlmFn fn) { coachLlm_ = std::move(fn); }
    void SetCoachLlmEnabled(bool on) { coachLlmEnabled_ = on; }
    // 评估加速：只在回合开始请求 LLM（跳过下包/拆包/时间不足等事件）
    void SetCoachLlmRoundStartOnly(bool on) { coachLlmRoundStartOnly_ = on; }
    bool IsCoachLlmEnabled() const { return coachLlmEnabled_; }
    // LLM 评估用：是否还有未回包的计划请求（runner 靠它把模拟节奏压到真实决策速度）
    bool IsCoachLlmPending() const {
        return CoachFor(Team::T).llmPending || CoachFor(Team::CT).llmPending;
    }
    // 无渲染的 runner 用：只收 LLM 回包，不推进仿真（否则等待会死锁）
    void PumpCoach() { CoachDrainLlm(); }
    void CoachTryLlm(Team t, const CoachEvent& ev);
    void CoachTryDebrief(Team t);
    // 现在是否允许「保枪」：仅回合早段 + 经济崩 + 非人数优势 + 未下包
    bool ShouldSaveNow(Team t) const;
    // 经济连败档（EconomySystem：胜利时清零，非 CS2「胜后降一档」）
    int  GetLossStreak(Team t) const { return economy_.LossStreak(t); }
    BuyPlan GetBuyPlan(Team t) const;
    BuyPlan DecideBuyPlan(Team t) const;   // 按买枪前经济/连败/敌方经济分档
    static const char* BuyPlanLabel(BuyPlan p);
    // ---- 敌方威胁度（信息层）----
    const TacticalIntel& Intel() const { return intel_; }
    // 转点判定：时间/人数/冷却/证据 都满足才允许
    bool CanRotateNow(Team t) const;
    void CoachTryRotate(Team t);
    void CoachDrainLlm();
    void CoachWarmupPlaybook();   // 换图后预加载战术库并校验区域名
    // 玩家操作：主动拾取（E）/ 丢弃当前武器（G）
    void TryPickupDrops(bool manual, AudioManager* audio = nullptr);
    void DropCurrentWeapon();
    // 把指定槽位的武器丢到地面（换枪 / 被替换时用）
    void DropWeaponToGround(int slot, float x, float y);
    void DropWeaponToGround(Player& actor,int slot,float x,float y);
    bool GetPlayerHelmet() const { return player_.helmet; }

    // ---- 武器 ----
    void SwitchWeaponSlot(int slot, int grenadeIndex = -1);
    std::string GetWeaponName() const;
    std::string GetWeaponNameFor(const Player& p) const;   // 观战/记分板用

    // 情报保留时长（秒）
    static constexpr float CONTACT_MEMORY  = 6.0f;

    // 鼠标灵敏度
    float GetMouseSensitivity() const { return mouseSensitivity_; }
    void  SetMouseSensitivity(float s) { mouseSensitivity_ = std::clamp(s, 0.1f, 5.0f); }

    // 每队人数（含玩家）
    static constexpr int TEAM_SIZE  = 5;
    static constexpr int TOTAL_BOTS = TEAM_SIZE * 2 - 1;   // 玩家占 CT 一席 → 9 BOT

private:
    friend struct CS2DGameTestAccess;
    std::unique_ptr<CS2DWorld> world_;
    VisibilityQuery visibility_;   // 视线/视野判定的唯一实现（无 SDL 依赖）
    CS2DRenderer* renderer_ = nullptr;
    std::string mapId_;            // 如 dust_2 / ancient_ruins / mirage
    TacticsTable tactics_;         // 每图战术表（areas / slots），缺失则回退代码默认
    std::FILE* decisionTrace_ = nullptr;   // 决策 trace 输出（dev-only，nullptr=关闭）
    uint64_t   traceTick_ = 0;             // 当前 tick（由 Update 递增）
    DebugCounters counters_;
    // 角色动态重算（带冷却与滞回）
    std::vector<float> roleChangedAt_;   // 每个 BOT 上次换角色时刻
    std::vector<float> roleFromMul_;     // 每个 BOT 换角色那一刻的旧倍率（T27 过渡起点）
    float roleRefreshTimer_ = 0.0f;
    int   roleAliveSnapshot_ = -1;
    // 搜点白名单：每个区域最近一次被确认"已搜"的回合时刻（-100=未搜）
    std::map<std::string, float> areaSearchedAt_[2];
    void RefreshSearchAreas();           // 回合开始重置白名单
    std::string playbookId_;       // 战术库文件名（不含 .txt）；空则回退 mapId_

    Player player_;
    ActorView previousPlayerView_;
    std::vector<ActorView> previousBotViews_;
    float renderInterpolation_ = 1.0f;
    bool viewHistoryValid_ = false;
    bool aimFollowsPlayer_ = false;
    float aimOffsetX_ = Tuning::InitialAimDistance, aimOffsetY_ = 0.0f;
    float recoilShake_ = 0.0f, recoilShakeAge_ = 0.0f;
    unsigned playerSpawnVersion_ = 0;
    bool cameraPrepared_ = false, cameraScopeSettled_ = false;
    bool edgeZoomRequested_ = false;
    int cameraDistanceTier_ = 0;
    float edgeZoom_ = 0.0f;
    float previousEdgeZoom_ = 0.0f;
    float edgeZoomFrom_ = 0.0f, edgeZoomTo_ = 0.0f, edgeZoomTime_ = 1.0f;
    void DrawWorldPass(bool lens = false);
    SpriteDrawParams MakeActorDrawParams(const Player& actor, const Player* observer) const;
    float previousScopeZoom_ = 1.0f;
    float previousScopeEngagement_ = 0.0f;
    float aimMotionBlur_ = 0;
    float crosshairBloom_ = 0.0f;  // 平滑后的移动扩散（0=静止，1=全速跑动）
    std::vector<Player> bots_;
    int nextBotNameIndex_ = 0;      // 新 BOT 分配昵称的游标（每场重置）
    std::vector<Bullet> bullets_;
    std::vector<Grenade> grenades_;
    std::vector<SmokeCloud> smokes_;
    std::vector<FireZone>    fires_;
    std::vector<Decoy> decoys_;
    float fireTickTimer_ = 0.0f;

    // 手雷爆炸特效（仅用于渲染扩散圆环）
    struct BlastFx {
        float x = 0.0f, y = 0.0f;
        float life = 0.0f;      // 剩余秒数
        float total = 1.0f;     // 总时长
    };
    std::vector<BlastFx> blasts_;

    // 阵亡掉落的武器/投掷物
    struct DroppedItem {
        Player::EquipmentRecord equipment;
        Player::WeaponFireState fireState;
        bool defuseKit = false;
        WeaponId id = WeaponId::None;
        float x = 0.0f, y = 0.0f;
        float life = 30.0f;      // 30 秒后消失
        float pickupDelay = 0.0f;  // 拾取冷却（丢下后短时间内不能马上捡回）
        float rot = 0.0f;        // 平铺在地面的朝向（度）
        bool  isGrenade = false;
        // 丢弃时的弹药状态：捡起时原样恢复（弹匣内 + 备弹）
        int   magAmmo     = 0;
        int   reserveAmmo = 0;
        bool  hasAmmoState = false;   // false = 旧数据/未记录，捡起时按满配
    };
    std::vector<DroppedItem> drops_;

    // ---- 教练层状态（每队一份）----
    struct CoachState {
        TeamPlan plan;                       // 当前生效计划
        BuyPlan  buyPlan = BuyPlan::Full;    // 本回合购买档（DecideBuyPlan）
        std::deque<RoundMemo> memory;        // 最近 10 回合
        std::vector<CoachEvent> pending;     // 待处理事件
        float mergeWindow = 0.0f;            // 事件合并窗口
        int   callsThisRound = 0;            // 本回合已调用次数（上限 8）
        int   gen = 0;                       // 代际号
        std::string reason;                  // HUD 显示用
        int   appliedIntent = -1;            // 上一次真正下发的意图（度量变更次数）
        int   changesThisRound = 0;          // 本回合意图变更次数（变更预算）
        int   sameSideStreak = 0;            // 连续打同一侧的次数（防重复但不至于一次就换）
        float lastIntentChangeSec = -100.0f; // 上次意图变更时刻（变更预算用）
        BuyPlan lastBuyPlan = BuyPlan::Full; // 上一回合买档（T18 跨回合经济状态机）
        bool  lastBuyWon = false;            // 上一回合是否赢（T18）

        // 本回合统计（回合结束写入 memory）
        std::vector<RoundMemo::Death> deaths;
        std::string firstContactArea;
        bool  timeLowFired = false;
        bool  matchPointFired = false;

        // 教练笔记（最近 5 条复盘结论，滚动）
        std::deque<std::string> notes;
        std::string lastOrder;        // 上一条指令摘要（喂给下一次 prompt）
        // ---- 转点状态（信息驱动）----
        std::string rotateFrom;       // 转点前目标侧
        std::string rotateTo;         // 转点后目标侧
        float       rotateAt = 0.0f;  // 转点时刻
        std::string lastAttackSide;   // 最近一次进攻目标侧

        // LLM 请求在途
        float planExpiresAt = 0.0f;   // 计划时效（到期回到默认）
        bool  llmPending = false;
        bool  llmAnswered = false;   // 本轮请求已返回（含失败/超时），购买延迟用
        int   llmGen = 0;
        float llmAt = 0.0f;
        std::chrono::steady_clock::time_point llmWallAt;   // 发出请求的墙钟时刻
        float llmLatencyWall = 0.0f;                       // 回包墙钟延迟（秒）
        int   llmEventType = 0;
        int   llmRoundNo = 0;
        int   llmAliveT = 0, llmAliveCT = 0;   // 发请求时的存活数（时效性判断）
        TeamPlan ruleSnapshot;          // 发请求那一刻的规则计划
        TeamPlan llmSnapshot;           // LLM 解析后的计划（未整形前的原意）
        bool  hasRuleSnapshot = false;
        bool  llmBombPlanted = false;
        float lastThinkAt = 0.0f;
        // true = plan from LLM; RuleCoach must not overwrite until expiry/critical.
        bool  stickyLlm = false;
    };
    CoachState coachT_, coachCT_;
    // 回合开始、任何采购之前的经济快照：save/eco 必须基于「买得起什么」，
    // 不能因为刚买完枪余额变低就误判为穷局。
    int  roundStartAvgMoneyT_  = 0;
    int  roundStartAvgMoneyCT_ = 0;
    bool roundEconomySnapshotValid_ = false;

    CoachLlmFn coachLlm_;
    bool coachLlmEnabled_ = true;
    bool coachLlmRoundStartOnly_ = false;   // 评估加速：只回合开始请求 LLM
    float coachPlanChangedAt_ = 0.0f;
    struct CoachLlmResult { Team team; int gen; std::string raw; bool debrief = false; };
    std::vector<CoachLlmResult> coachLlmResults_;
    std::mutex coachLlmMutex_;
    CoachState&       CoachFor(Team t);
    const CoachState& CoachFor(Team t) const;

    void CoachUpdate(float dt);
    void CoachOnEvent(Team t, CoachEventType type, const std::string& area = std::string());
    void CoachThink(Team t, const CoachEvent& ev);
    TeamPlan RuleCoach(Team t, const CoachEvent& ev);
    // 规则教练的原始实现；外面套一层战术表覆盖（见 CS2DGameCoach.cpp）
    TeamPlan RuleCoachCore(Team t, const CoachEvent& ev);
    void ApplyTacticsSlots(Team t, TeamPlan& plan);
    void CoachApplyPlan(Team t);
    void SanitizeCoachPlan(Team t, TeamPlan& plan);   // 防单点/防重复的兜底整形
    void CoachRecordRound(Team t, bool won);
    void CoachResetRound();
    // 回合开始、买枪之前先出计划（save/eco 必须在购买前生效）
    void CoachBeginRound();
    bool LowEconomyAverage(Team t) const;
    void SnapshotRoundEconomy();   // 买枪前经济快照（save/eco 依据）
    bool AreaCenterByName(const std::string& name, float& ox, float& oy) const;
    std::string AreaHint(const std::string& intentTarget) const;
    unsigned purchaseEpoch_ = 1;
    std::uint64_t nextEquipmentId_ = 1;
    bool prevInteract_ = false;   // E 键边沿检测

    void DropEquipmentOnDeath(Player& victim);
    float flashTimer_ = 0.0f;      // 玩家闪光致盲剩余秒数
    float flashPeak_  = 0.0f;      // 本次致盲的峰值强度 0~1（越近越白）
    // 玩家脚步：按实际位移累计（与 BOT 同一套，见 BotRuntime::footstepStride）
    float footstepStride_ = 0.0f;      // 已累计的位移（px）
    float footstepLastX_ = 0.0f;       // 上一帧位置（算 |Δpos|）
    float footstepLastY_ = 0.0f;
    bool  footstepLastValid_ = false;
    std::uint64_t footstepCount_ = 0;  // 已触发的步数（统计用）
    void  ResetFootstepStride();
    // 返回本帧触发的步数（0..Tuning::FootstepMaxPerFrame），供玩法与回归共用同一实现。
    int   AccumulateFootstep(Player& actor, float& stride, float& lastX, float& lastY,
                             bool& lastValid, AudioManager* audio, bool positional);
    bool spectateTogglePending_ = false;
    int previousEquipSlot_ = 2;
    WeaponId previousEquipWeapon_ = WeaponId::Knife;
    HitMarkerKind hitMarkerKind_ = HitMarkerKind::Normal;
    float dryFireHintTimer_ = 0.0f;
    float dryFireCooldown_ = 0.0f;
    float hitMarkerTimer_ = 0.0f;   // 命中敌人反馈
    std::array<DamageDirection, 4> damageDirections_{};
    float damageFlashTimer_ = 0.0f; // 玩家受伤反馈

    // 目标物状态
    bool  bombPlanted_   = false;
    float bombX_ = 0.0f, bombY_ = 0.0f;
    float bombTimer_     = 0.0f;   // 倒计时（<=0 爆炸）
    float bombBeepTimer_ = 0.0f;   // C4 滴答音节奏
    float clockSec_ = 0.0f;        // 累计运行时间（用于呼吸类动画）
    float matchOverTimer_ = 0.0f;  // 比赛结束后累计时间

    // 屏幕中央提示（回合开始 / 回合胜负）
    std::string announcement_;
    float announcementTimer_ = 0.0f;

    // 最近一次阵亡点（用于队友补枪）
    float lastDeathX_ = 0.0f, lastDeathY_ = 0.0f;
    Team  lastDeathTeam_ = Team::None;
    float lastDeathTimer_ = 0.0f;
    float botPlantProgress_ = 0.0f;
    float defuseRemaining_ = 0.0f;
    float defuseProgress_ = 0.0f;  // 0..1

    std::vector<KillFeedEntry> killFeed_;
    std::uint64_t nextKillEventId_=1;

    PathFinder pathFinder_;   // 载入地图后烘焙


    Team  desiredPlayerTeam_ = Team::CT;
    int bombPlanter_ = -2;
    std::uint64_t c4EquipmentId_=0;
    int   bombCarrier_ = -2;      // -2 无人, -1 玩家, >=0 BOT 索引
    bool  bombDropped_ = false;    // C4 掉在地上
    float bombDropDelay_ = 0.0f;   // 丢下后 1 秒内不能被捡回（否则原地立刻捡起，像「丢不掉」）
    float bombDropX_ = 0.0f, bombDropY_ = 0.0f;
    bool  playerHoldingC4_ = false; // 玩家把 C4 拿在手上（按 5）

    // 切枪音效：SwitchWeaponSlot 没有 AudioManager 参数（内部切换/按键都可能调用），
    // 先把「刚切到的武器」挂起，由 Update(audio) 统一播放。
    bool     switchSoundPending_ = false;
    WeaponId switchSoundWeapon_ = WeaponId::None;

    // CT 对「掉落 C4」的情报（像敌人情报一样会过期）
    bool  bombIntel_ = false;
    float bombIntelX_ = 0.0f, bombIntelY_ = 0.0f;
    float bombIntelAge_ = 0.0f;
    int plantingChannel_ = -1;
    int activeDefuser_ = -2;
    float plantProgress_ = 0.0f;

    SpectateMode specMode_ = SpectateMode::None;
    DeathPresentation deathPresentation_;
    void UpdateSpectatorState();
    int   specTarget_ = -1;
    float specFreeX_ = 0.0f, specFreeY_ = 0.0f;
    int   NextTeammateBot(int from, int dir) const;
    // 自由视角的视野观察者 = 存活队友（含玩家本人以外的己方角色）。
    // 返回实际写入数量；为 0 时调用方回退到「按自己」。
    std::size_t CollectFreeSpectateObservers(VisibilityQuery::Observer* out,
                                            std::size_t capacity) const;

    int playerKills_  = 0;
    int playerDeaths_ = 0;

    MatchMode  mode_ = MatchMode::TeamElimination;
    TeamManager teams_;
    RoundManager round_;
    EconomySystem economy_;
    RoundPhase  prevPhase_ = RoundPhase::Freeze;
    bool        matchOverAnnounced_ = false;

    // BOT 运行时状态（与 bots_ 同下标）
    std::vector<BotRuntime> botAi_;

    // 小队协同状态（索引与 bots_ 对齐）
    std::vector<SquadOrder> squad_;
    float squadFocusTimer_ = 0.0f;

    // 团队情报（按阵营分开）
    TacticalIntel intel_;
    float lastRotateAt_ = -999.0f;   // 转点冷却（全局，按队伍用 CoachState 更细也行）
    float rotateCheckTimer_ = 0.0f;   // 转点评估节流
    std::vector<EnemyContact> contactsT_;
    std::vector<EnemyContact> contactsCT_;

    float botRespawnTimer_  = 0.0f;
    static constexpr float BOT_RESPAWN_DELAY = 3.0f;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> angleDist_{0.0f, 6.283185f};

    float mouseSensitivity_ = 1.0f;
    bool  prevMouseDown_    = false;
    // 契约 v1.3：开镜大十字设置
    float scopeCrossThickness_ = 2.0f;
    bool  scopeCrossDynamic_   = false;
    // 契约 v1.2：开枪自动关镜
    bool  delayedScopeClose_ = false;     // 设置项：开启后按住扳机可推迟关镜
    bool  scopeCloseOnRelease_ = false;   // 已开过枪、等松手再关镜

    // ---- 开镜状态（唯一来源，UI 只读快照；契约 v1.1 三档循环）----
    Presentation::ScopeView scope_{};
    int   scopeLevel_ = 0;                          // 0=关 / 1=1.5x / 2=2x
    float scopeEngagement_ = 0.0f;                  // 0..1 镜框/遮罩淡入淡出进度
    float scopeEngFrom_ = 0.0f, scopeEngTo_ = 0.0f, scopeEngT_ = 1.0f;
    float scopeZoom_ = 1.0f;                        // 当前实际倍率
    float scopeZoomFrom_ = 1.0f, scopeZoomTo_ = 1.0f, scopeZoomT_ = 1.0f;
    float aimWorldX_ = 0.0f, aimWorldY_ = 0.0f;     // 最近一次 Update 传入的准星世界坐标
    void TickScope(float dt);                       // 每帧按 dt 推进过渡
    // 相机向准星偏移的比例（1.5x≈0.35，2x≈0.5，按当前倍率线性）
    int  PurchaseQuantityOf(const Presentation::BuyRequest& request) const;
    int  PurchasePriceOf(const Presentation::BuyRequest& request) const;
    bool IsBuyWindowOpen() const;                          // 只看时窗（模式/回合）

    // ---- 枪弹视觉段 ----
    std::vector<Presentation::BulletVisualSegment> bulletVisuals_;
    std::uint64_t nextBulletVisualId_ = 1;
    void AddBulletVisual(const Bullet& b, float fromX, float fromY,
                         float toX, float toY, bool impact);


    // 内部方法
    void ResetToNewMatch();          // 清空比分与统计并重建所有战斗单位
    void SpawnPlayer();
    void SpawnBOT(Player& bot, Team team);
    bool PickSpawnForTeam(Team team, float& outX, float& outY);
    void UpdatePlayerMovement(float dt, bool w, bool a, bool s, bool d, bool stealth);
    void UpdatePlayerShooting(float dt, bool mouseDown, float worldMX, float worldMY, class AudioManager* audio);
    void UpdateBOTs(float dt, class AudioManager* audio);
    // 找一个能挡住威胁点视线的掩体位置（就近优先）
    // 卡墙脱困：尝试在周围找一个可行走点并侧移过去（返回是否成功）
    bool NudgeOutOfStuck(Player& bot, BotRuntime& ai);
    bool FindCoverPoint(const Player& bot, float threatX, float threatY,
                        float& outX, float& outY) const;

    // 敌方出生点质心（用于无情报时的推进方向，与地图朝向无关）
    bool EnemySpawnCentroid(Team myTeam, float& outX, float& outY) const;

    // 武器对应的换弹音效（时长与 WeaponDef::reloadTime 对齐）
    static SoundEffect ReloadSoundFor(WeaponId id);

    // ---- 小队协同 ----
    void AssignSquadRoles(bool dynamic = false);
    void RefreshRolesForTeam(Team t);   // 只补"断档"的角色，不重排全部（避免角色churn）            // 回合开始时分配 突破/支援/断后
    void UpdateSquadFocus(float dt);    // 周期性选集火目标（含掩护偏好）
    const Player* ResolveFocus(const SquadOrder& order) const;

    // ---- 团队情报共享 ----
    void UpdateTeamContacts(float dt);                 // 上报 + 老化
    void OnFootstepSound(const Player& source, float x, float y);  // 脚步声 → 附近敌对 BOT
    void ReportContact(Team team, int id, float x, float y);
    const EnemyContact* FindNearestContact(Team team, float x, float y) const;
    std::vector<EnemyContact>&       ContactsOf(Team t);
    const std::vector<EnemyContact>& ContactsOf(Team t) const;

    // ---- 听觉（声音传播）----
    // 在声源位置发一次声：半径内的所有存活 BOT 都会收到一条 HeardSound；
    // 敌对声音同时把听者的警戒朝向指向声源。radius 为 0 时用 SoundRadius(type)。
    void EmitSound(SoundEventType type, float x, float y, Team team,
                   int sourceIdx = -2, float radius = 0.0f, WeaponId hazardWeapon = WeaponId::None, float dangerDelay = 0);
    // 某个 BOT 最近听到的指定类型声音（只按 age 过滤）；没有返回 nullptr
    const HeardSound* NewestHeard(size_t botIdx, SoundEventType type,
                                  float maxAge = Tuning::SoundMemory) const;
    bool HasHeard(size_t botIdx, SoundEventType type,
                  float maxAge = Tuning::SoundMemory) const {
        return NewestHeard(botIdx, type, maxAge) != nullptr;
    }
    // 听觉信息老化（每帧、每 BOT）
    void AgeHeardSounds(BotRuntime& ai, float dt);
    // 找一个「真实可见」的敌人（距离 + 烟雾 + LOS）
    const Player* FindVisibleEnemy(const Player& self, float& outDist) const;
    // 拆包安全判定：最近受击 / 可见敌人时先放弃拆包反击；炸弹最后 6 秒强制拆。
    bool BotCanDefuse(size_t i, const Player& bot) const;
    // 下包后指定一个 CT 拆包手：优先安全、离炸弹最近；其余 CT 清点/掩护。
    int  PickDefuseBot() const;
    // 目标是否落在以 self 朝向为中心的视野扇形内（玩家/BOT 共用几何）
    bool CanSeePoint(const Player& observer, float x, float y) const;
    int ActorPoseSlot(const Player &actor) const {
        const bool planting = &actor == &player_ ? plantProgress_ > 0
                                                 : botPlantProgress_ > 0 && bombCarrier_ >= 0 &&
                                                       bombCarrier_ < static_cast<int>(bots_.size()) &&
                                                       &actor == &bots_[bombCarrier_];
        return planting ? 4 : actor.weaponSlot;
    }
    struct BodyVisibility { float fraction = 0; SDL_FPoint aim{}; };
    struct BodyVisibilityCache {
        const Player* observer=nullptr;
        const Player* target=nullptr;
        std::array<float,15> signature{};
        BodyVisibility result;
    };
    mutable std::vector<BodyVisibilityCache> bodyVisibilityCache_;
    mutable float bodyVisibilityClock_=-1;
    mutable std::uint64_t bodyVisibilityTerrain_=0;
    BodyVisibility QueryBodyVisibility(const Player& observer, const Player& target) const;
    bool CanSeeActor(const Player& observer, const Player& target) const;
    // 沿 A* 路径移动；返回 true 表示本帧移动已由寻路接管
    bool FollowPathTo(Player& bot, BotRuntime& ai,
                      float destX, float destY, float dt, float speedMul = 1.0f);
    // 局部避障：累加附近单位的排斥向量（outX/outY 已归一化到 0..1 量级）
    void AccumulateSeparation(const Player& self, float& outX, float& outY) const;
    // 把期望速度与分离向量合成，并按最大速度钳制
    void ApplyAvoidance(float& vx, float& vy, float sepX, float sepY) const;
    void UpdateBullets(float dt, class AudioManager* audio);
    void UpdateGrenades(float dt, class AudioManager* audio);
    void ThrowGrenade(Player& thrower, float aimAngle, class AudioManager* audio = nullptr, float targetDistance = -1.0f);
    // 投掷前校验：落点可行走 + 有视线 + 距离合理；失败则尝试偏移角度，全失败就不扔。
    bool PlanGrenadeThrow(const Player& bot, float tx, float ty, float& outAngle) const;
    float ExpectedBlastDamage(float x,float y,const Player& target) const;
    void RebuildFireCells(FireZone& fire) const;
    bool FireContains(const FireZone& fire,float x,float y) const;
    Player* DamageOwner(int index) { return index == -1 ? &player_ : index >= 0 && index < static_cast<int>(bots_.size()) ? &bots_[index] : nullptr; }
    void ExplodeGrenade(const Grenade& g, class AudioManager* audio);
    bool SegmentBlockedBySmoke(float x1, float y1, float x2, float y2) const;
    void RespawnBOT(Player& bot);

    void MoveWithCollision(Player& entity, float dt);    void FireBullet(float x, float y, float angle, const WeaponDef& def,
                    int ownerIdx, Team ownerTeam);

    bool BotCanSeePlayer(const Player& bot) const;
    bool PlayerCanSeeBot(const Player& bot) const;

    void StartReload(Player& p, class AudioManager* audio = nullptr);
    void UpdateReload(Player& p, float dt);

    // 半径内是否存在「敌对阵营」的存活单位（队友不计入）
    bool IsEnemyNearby(float x, float y, float safeRadius, Team myTeam) const;

    // 回合流程
    void UpdateRoundFlow(float dt, class AudioManager* audio);
    void BeginNewRound(class AudioManager* audio = nullptr);
    // carryWeapons: 上一回合存活者保留武器（false=全部重置，用于新开一局）
    void RespawnAllForRound(bool carryWeapons = true);
    void BotBuyLoadout(Player& bot);   // BOT 按经济买枪与护甲
    void UpdateBotBuy(float dt);        // 回合初延迟购买：等教练 save/eco 决策

    // 回合初 BOT 购买延迟：让 LLM 先给出 save/eco，再决定买不买
    bool  botBuyPending_ = false;
    bool  botBuyDoneT_   = false;
    bool  botBuyDoneCT_  = false;
    float botBuyTimer_   = 0.0f;
    void CheckWinConditions(class AudioManager* audio);
    void ApplyRoundResult(Team winner, class AudioManager* audio = nullptr);
    // 统一伤害入口：先扣护甲再扣血。返回 true 表示本次伤害致死。
    bool ApplyDamage(Player& victim, int amount, float armorPen = 1.0f);
    void GrantRoundMoney(Team winner);   // 发放回合金钱并重置本回合装备

    // 目标物
    void SetupObjective();                  // 按模式初始化包点 / C4
    void UpdateObjective(float dt, bool interactHeld, class AudioManager* audio);
    void PlantBomb(float x, float y, class AudioManager* audio);
    void DetonateBomb(class AudioManager* audio);
    void OnCombatantKilled(Player& victim, class AudioManager* audio,
                           Player* killer = nullptr, WeaponId weapon = WeaponId::None,
                           std::uint64_t equipmentId=0, std::optional<SDL_FPoint> sourcePosition=std::nullopt);
};
