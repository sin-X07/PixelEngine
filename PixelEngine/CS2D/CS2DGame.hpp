#pragma once
#include "CS2DWorld.hpp"
#include "CS2DRenderer.hpp"
#include "Teams/TeamManager.hpp"
#include "Coach/CoachTypes.hpp"
#include "TacticalIntel.hpp"
#include "SoundEvents.hpp"
#include "Rounds/RoundManager.hpp"
#include "Weapons/WeaponDef.hpp"
#include "Tuning.hpp"
#include "Economy/EconomySystem.hpp"
#include "Navigation/PathFinder.hpp"
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
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
    Anchor      // 断后/守点：靠后，倾向保持位置
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
        float x       = 0.0f;
        float y       = 0.0f;
        float vx      = 0.0f;
        float vy      = 0.0f;
        float size    = 7.0f;
        float aimAngle = 0.0f;
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
            }
            grenades[grenadeCount - 1] = WeaponId::None;
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
        bool  hadTarget       = false; // 上一帧是否看得见敌人（决定是否原地扫描）
        float scanPhase       = 0.0f;  // 扫视相位（行走时左右小幅摆头）
        float scanBase        = 0.0f;  // 原地扫视的中心朝向
        float wanderX         = 0.0f;  // 巡逻目标（缓存，避免每帧重掷导致抖动）
        float wanderY         = 0.0f;
        float wanderTimer     = 0.0f;  // >0 时沿用缓存目标
        float detourX = 0.0f, detourY = 0.0f;   // 卡住时的绕行方向（单位向量）
        float detourTimer = 0.0f;               // >0 时沿 detour 方向走
        bool  scanBaseValid   = false;
        float footstepTimer   = 0.0f;  // 脚步音节奏

        // 卡墙检测
        float lastX = 0.0f, lastY = 0.0f;
        float stuckTimer = 0.0f;

        // 教练计划指定的驻守点（hasPlanHold=true 时优先于默认目标）
        bool  hasPlanHold = false;
        float planHoldX = 0.0f, planHoldY = 0.0f;
        std::string goalArea;
    };

    struct Bullet {
        float x  = 0.0f;
        float y  = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float traveled = 0.0f;
        float maxDist  = 800.0f;
        int   damage   = 25;
        int   ownerIdx  = -1;        // -1=玩家, >=0=BOT索引
        Team  ownerTeam = Team::None;
        // 发射该子弹的武器：击杀奖励按武器类型发放（照抄 CS2）
        WeaponId weapon = WeaponId::None;
        bool  active   = true;
    };

    // 投掷物
    struct Grenade {
        float    x = 0.0f, y = 0.0f;
        float    vx = 0.0f, vy = 0.0f;
        float    fuse = 1.5f;
        WeaponId type = WeaponId::GrenadeHE;
        Team     team = Team::None;
        bool     active = true;
        bool     landingWarned = false;   // 落点预警已广播（只发一次）
        int      wallBounces = 0;         // 撞墙反弹次数
        float    spin = 0.0f;             // 飞行旋转角（度）
        float    bounceHearCooldown = 0.0f; // 撞墙发声节流
    };

    // 烟雾云（遮挡视线）
    // 燃烧弹 / 燃烧瓶留下的火区
    struct FireZone {
        float x = 0.0f, y = 0.0f;
        float radius = 58.0f;
        float life = 7.0f;
        float maxLife = 7.0f;
        float age = 0.0f;
        float seed = 0.0f;
    };
    struct SmokeCloud {
        float x = 0.0f, y = 0.0f;
        float radius = 70.0f;
        float life = 12.0f;        // 剩余寿命
        float maxLife = 12.0f;     // 初始寿命（用于淡出）
        float age = 0.0f;          // 已存在时间（用于爆开 + 向外扩张）
        float expandTime = 1.6f;   // 从中心扩散到 full radius 的时间
        float seed = 0.0f;         // 每团烟的随机种子（形状/旋转）
    };

    // 击杀播报条目
    struct KillFeedEntry {
        std::string text;
        float life = 4.0f;
    };

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
    const CS2DWorld& GetWorld() const { return *world_; }
    CS2DRenderer*    GetRenderer()    { return renderer_; }

    Player&          GetPlayer()      { return player_; }
    const Player&    GetPlayer() const { return player_; }

    std::vector<Player>&       GetBOTs()       { return bots_; }
    const std::vector<Player>& GetBOTs() const { return bots_; }
    const std::vector<Bullet>& GetBullets() const { return bullets_; }
    const std::vector<Grenade>& GetGrenades() const { return grenades_; }
    const std::vector<SmokeCloud>& GetSmokes() const { return smokes_; }
    const std::vector<FireZone>& GetFires() const { return fires_; }
    const std::vector<KillFeedEntry>& GetKillFeed() const { return killFeed_; }
    std::vector<ScoreRow> BuildScoreboard() const;

    // 玩家是否处于闪光致盲中（0..1 强度）
    float GetFlashAmount() const { return (flashTimer_ > 0.0f) ? 1.0f : 0.0f; }

    // 命中 / 受伤反馈计时（秒，>0 表示需要绘制）
    float GetHitMarkerTimer()   const { return hitMarkerTimer_; }
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

    // ---- 目标物（爆破）----
    bool  IsBombPlanted()   const { return bombPlanted_; }
    float GetBombTimer()    const { return bombTimer_; }
    float GetBombX()        const { return bombX_; }
    float GetBombY()        const { return bombY_; }
    float GetDefuseProgress() const { return defuseProgress_; }

    int GetPlayerKills()  const { return playerKills_; }
    int GetPlayerDeaths() const { return playerDeaths_; }
    int GetBotCount()     const { return static_cast<int>(bots_.size()); }
    float GetAimAngle()   const { return player_.aimAngle; }
    // 0=静止，~0.42=静步，1=全速跑动。准星与弹道共用这一份移动扩散。
    float GetCrosshairBloom() const;

    // ---- 阵营 / 回合 ----
    MatchMode  GetMatchMode()   const { return mode_; }
    void       SetMatchMode(MatchMode m) { mode_ = m; }

    int  GetScoreT()      const { return teams_.ScoreT(); }
    int  GetScoreCT()     const { return teams_.ScoreCT(); }
    int  GetScoreToWin()  const { return teams_.ScoreToWin(); }

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
    bool PlayerHasC4() const {
        return bombCarrier_ == -1 && player_.team == Team::T &&
               !bombPlanted_ && !bombDropped_;
    }
    bool PlayerHoldingC4() const { return playerHoldingC4_; }
    bool IsBombDropped() const { return bombDropped_; }
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
            // 找正在拆包的实体：玩家按住 E 或某 BOT 贴近炸弹
            if (player_.alive && player_.team == Team::CT) {
                const float dx = player_.x - bombX_;
                const float dy = player_.y - bombY_;
                if (std::sqrt(dx * dx + dy * dy) <= Tuning::DefuseRadius * 2.0f) {
                    out.who = &player_;
                    return true;
                }
            }
            for (const auto& b : bots_) {
                if (!b.alive || b.team != Team::CT) continue;
                const float dx = b.x - bombX_;
                const float dy = b.y - bombY_;
                if (std::sqrt(dx * dx + dy * dy) <= Tuning::DefuseRadius * 2.0f) {
                    out.who = &b;
                    return true;
                }
            }
            return false;
        }

        // 下包：T 携带者站在包点内且已开始累积安装进度
        // 注意：玩家下包用 plantProgress_，BOT 下包复用了 defuseProgress_，两者都要检查
        const float plantProg = std::max(plantProgress_, bombPlanted_ ? 0.0f : defuseProgress_);
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
    enum class SpectateMode { None = 0, Follow, Free };
    bool          IsSpectating() const { return specMode_ != SpectateMode::None && !player_.alive; }
    SpectateMode  GetSpectateMode() const { return specMode_; }
    int           GetSpectateTarget() const { return specTarget_; }
    // 当前被观战的 BOT（自由视角下保留最后跟随目标）；无效返回 nullptr。
    const Player* GetSpectatedPlayer() const {
        if (specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
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
    float ListenerX() const;
    float ListenerY() const;
    Team GetMatchWinner() const { return teams_.MatchWinner(); }

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
    // 比赛结束后经过的时间（用于自动返回主菜单）
    float GetMatchOverTimer() const { return matchOverTimer_; }
    // 玩家当前是否处于购买区内（地图未定义购买区时视为全图可买）
    bool IsPlayerInBuyZone() const;
    // 尝试购买；成功返回 true（已扣款并装备）
    bool TryBuy(WeaponId id, class AudioManager* audio = nullptr);
    // 购买护甲：tier 1=护甲 2=护甲+头盔
    bool TryBuyArmor(int tier, class AudioManager* audio = nullptr);
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
    bool IsCoachLlmEnabled() const { return coachLlmEnabled_; }
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
    void TryPickupDrops(bool manual);
    void DropCurrentWeapon();
    // 把指定槽位的武器丢到地面（换枪 / 被替换时用）
    void DropWeaponToGround(int slot, float x, float y);
    bool GetPlayerHelmet() const { return player_.helmet; }

    // ---- 武器 ----
    void SwitchWeaponSlot(int slot);
    std::string GetWeaponName() const;
    std::string GetWeaponNameFor(const Player& p) const;   // 观战/记分板用

    // BOT 视野半径（发现敌人并交火的距离）
    // 与玩家视野圆共用 Tuning::VisionRadius，避免两边数值漂移
    static constexpr float BOT_VIEW_RADIUS = Tuning::VisionRadius;
    // 探点距离：超过交火距离但能「看到并上报」的范围（团队情报来源）
    // 保持旧版 2 倍关系，随视野半径一起收紧
    static constexpr float BOT_SPOT_RADIUS = 2.0f * Tuning::VisionRadius;
    // 情报保留时长（秒）
    static constexpr float CONTACT_MEMORY  = 6.0f;

    // 鼠标灵敏度
    float GetMouseSensitivity() const { return mouseSensitivity_; }
    void  SetMouseSensitivity(float s) { mouseSensitivity_ = std::clamp(s, 0.1f, 5.0f); }

    // 每队人数（含玩家）
    static constexpr int TEAM_SIZE  = 5;
    static constexpr int TOTAL_BOTS = TEAM_SIZE * 2 - 1;   // 玩家占 CT 一席 → 9 BOT

private:
    std::unique_ptr<CS2DWorld> world_;
    CS2DRenderer* renderer_ = nullptr;
    std::string mapId_;            // 如 dust_2 / ancient_ruins / mirage
    std::string playbookId_;       // 战术库文件名（不含 .txt）；空则回退 mapId_
    std::string mapSourcePath_;    // LoadMap 时的 resolved TMJ 路径

    Player player_;
    float crosshairBloom_ = 0.0f;  // 平滑后的移动扩散（0=静止，1=全速跑动）
    std::vector<Player> bots_;
    int nextBotNameIndex_ = 0;      // 新 BOT 分配昵称的游标（每场重置）
    std::vector<Bullet> bullets_;
    std::vector<Grenade> grenades_;
    std::vector<SmokeCloud> smokes_;
    std::vector<FireZone>    fires_;
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
        int   llmEventType = 0;
        int   llmRoundNo = 0;
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
    bool prevInteract_ = false;   // E 键边沿检测

    void DropEquipmentOnDeath(const Player& victim);
    float flashTimer_ = 0.0f;      // 玩家闪光致盲剩余秒数
    float flashPeak_  = 0.0f;      // 本次致盲的峰值强度 0~1（越近越白）
    float footstepTimer_ = 0.0f;   // 玩家脚步声节奏
    float hitMarkerTimer_ = 0.0f;   // 命中敌人反馈
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
    float defuseProgress_ = 0.0f;  // 0..1

    std::vector<KillFeedEntry> killFeed_;

    PathFinder pathFinder_;   // 载入地图后烘焙

    void PushKillFeed(const std::string& text);

    Team  desiredPlayerTeam_ = Team::CT;
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
    float plantProgress_ = 0.0f;

    SpectateMode specMode_ = SpectateMode::None;
    int   specTarget_ = -1;
    float specFreeX_ = 0.0f, specFreeY_ = 0.0f;
    int   NextTeammateBot(int from, int dir) const;
    bool  SpectatorCanSeeEnemy(const Player& e) const;

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
    void AssignSquadRoles();            // 回合开始时分配 突破/支援/断后
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
                   int sourceIdx = -2, float radius = 0.0f);
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
    bool InViewCone(float fromX, float fromY, float aim,
                    float toX, float toY, float maxDist) const;
    // 沿 A* 路径移动；返回 true 表示本帧移动已由寻路接管
    bool FollowPathTo(Player& bot, BotRuntime& ai,
                      float destX, float destY, float dt, float speedMul = 1.0f);
    // 局部避障：累加附近单位的排斥向量（outX/outY 已归一化到 0..1 量级）
    void AccumulateSeparation(const Player& self, float& outX, float& outY) const;
    // 把期望速度与分离向量合成，并按最大速度钳制
    void ApplyAvoidance(float& vx, float& vy, float sepX, float sepY) const;
    void UpdateBullets(float dt, class AudioManager* audio);
    void UpdateGrenades(float dt, class AudioManager* audio);
    void ThrowGrenade(Player& thrower, float aimAngle, class AudioManager* audio = nullptr);
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
    bool ApplyDamage(Player& victim, int amount);
    void GrantRoundEndMoney(Team winner);   // 发放回合金钱并重置本回合装备

    // 目标物
    void SetupObjective();                  // 按模式初始化包点 / C4
    void UpdateObjective(float dt, bool interactHeld, class AudioManager* audio);
    void PlantBomb(float x, float y, class AudioManager* audio);
    void DetonateBomb(class AudioManager* audio);
    void OnCombatantKilled(Player& victim, class AudioManager* audio,
                           Player* killer = nullptr);
};
