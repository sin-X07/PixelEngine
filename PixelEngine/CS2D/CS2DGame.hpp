#pragma once
#include "CS2DWorld.hpp"
#include "CS2DRenderer.hpp"
#include "Teams/TeamManager.hpp"
#include "Coach/CoachTypes.hpp"
#include "Rounds/RoundManager.hpp"
#include "Weapons/WeaponDef.hpp"
#include "Tuning.hpp"
#include "Economy/EconomySystem.hpp"
#include "Navigation/PathFinder.hpp"
#include "../Core/Types.hpp"
#include "../LLM/BotIntent.hpp"
#include "../LLM/BotBehaviorExecutor.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <random>
#include <memory>
#include <algorithm>

class AudioManager;

// 对局模式
enum class MatchMode {
    DeathMatch,        // 自由击杀，即时重生，无回合
    TeamElimination,   // 回合制，T/CT 歼灭，先到 N 分获胜
    BombDefusal,       // 爆破：T 下包 / CT 拆包
    HostageRescue      // 人质：CT 营救全部人质
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
        bool  alive   = true;
        float shootCooldown = 0.0f;

        // 精灵
        AnimState animState = AnimState::Idle;
        float     animTimer = 0.0f;
        int       weaponSlot = 0;          // 活动槽位 0=主 1=副 2=近战 3=投掷
        int       spriteKey  = 0;
        float     deathTimer = 0.0f;
        float     shootAnimTimer = 0.0f;

        // 武器（按槽位）
        WeaponId  weapons[4] = { WeaponId::None, WeaponId::USP, WeaponId::Knife, WeaponId::GrenadeHE };

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

        // 弹药（活动槽位）
        int       ammo      = 30;
        int       maxAmmo   = 30;
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
            const int s = (weaponSlot < 0 || weaponSlot > 3) ? 0 : weaponSlot;
            return WeaponDB::Get(weapons[s]);
        }
    };

    // ========================================================================
    // BOT 运行时状态（与 Player 分离，玩家实体不再背这些字段）
    // 与 bots_ 按下标一一对应，统一由 EnsureBotAi() 保证长度
    // ========================================================================
    struct BotRuntime {
        BotIntent llmIntent;                       // 本帧姿态（由计划+感知推导）
        int       stance = 0;                      // 最近一次姿态（诊断用）
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

        // 状态标记
        float blindTimer     = 0.0f;   // 闪光致盲剩余秒数
        bool  defusing       = false;  // 正在拆包
        float grenadeCooldown = 0.0f;  // 投掷冷却
        bool  tradePushed     = false; // 本回合是否已补过枪
        bool  hadTarget       = false; // 上一帧是否看得见敌人（决定是否原地扫描）
        float scanPhase       = 0.0f;  // 扫视相位（行走时左右小幅摆头）
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
    };

    // 烟雾云（遮挡视线）
    struct SmokeCloud {
        float x = 0.0f, y = 0.0f;
        float radius = 70.0f;
        float life = 12.0f;
    };

    // 人质
    struct Hostage {
        float x = 0.0f, y = 0.0f;
        bool  rescued = false;
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

    bool LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer);
    void Reset();

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
        cx = sites[index].x + sites[index].width * 0.5f;
        cy = sites[index].y + sites[index].height * 0.5f;
        return true;
    }

    // ---- BOT 运行时状态：与 bots_ 同下标（AI 层与 LLM 回调用） ----
    void EnsureBotAi();
    BotRuntime&       AiAt(size_t i);
    const BotRuntime& AiAt(size_t i) const;
    size_t AiIndexOf(const Player& p) const;
    void   ResetBotAi(size_t i);

    // ---- 目标物（爆破 / 人质）----
    bool  IsBombPlanted()   const { return bombPlanted_; }
    float GetBombTimer()    const { return bombTimer_; }
    float GetBombX()        const { return bombX_; }
    float GetBombY()        const { return bombY_; }
    float GetDefuseProgress() const { return defuseProgress_; }
    int   GetHostagesTotal()   const { return static_cast<int>(hostages_.size()); }
    int   GetHostagesRescued() const;
    float GetRescueProgress()  const { return rescueProgress_; }

    int GetPlayerKills()  const { return playerKills_; }
    int GetPlayerDeaths() const { return playerDeaths_; }
    int GetBotCount()     const { return static_cast<int>(bots_.size()); }
    float GetAimAngle()   const { return player_.aimAngle; }

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
    Team GetMatchWinner() const { return teams_.MatchWinner(); }

    // 各阵营存活人数
    int AliveCount(Team t) const;

    // ---- 经济 / 购买 ----
    int  GetPlayerMoney() const { return player_.money; }
    // 购买窗口：自回合开始起的前 BuyWindowSeconds 秒（覆盖冻结 + 购买 + 交战初期）
    bool IsBuyPhase() const {
        if (mode_ == MatchMode::DeathMatch) return false;
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
    bool TryBuy(WeaponId id);
    // 购买护甲：tier 1=护甲 2=护甲+头盔
    bool TryBuyArmor(int tier);
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
    void CoachDrainLlm();
    // 玩家操作：主动拾取（E）/ 丢弃当前武器（G）
    void TryPickupDrops(bool manual);
    void DropCurrentWeapon();
    bool GetPlayerHelmet() const { return player_.helmet; }

    // ---- 武器 ----
    void SwitchWeaponSlot(int slot);
    std::string GetWeaponName() const;

    // BOT 视野半径（发现敌人并交火的距离）
    // 玩家视野圆半径 120px；BOT 同样取 120px
    static constexpr float BOT_VIEW_RADIUS = 120.0f;
    // 探点距离：超过交火距离但能「看到并上报」的范围（团队情报来源）
    static constexpr float BOT_SPOT_RADIUS = 240.0f;
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

    Player player_;
    std::vector<Player> bots_;
    std::vector<Bullet> bullets_;
    std::vector<Grenade> grenades_;
    std::vector<SmokeCloud> smokes_;

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
        bool  isGrenade = false;
    };
    std::vector<DroppedItem> drops_;

    // ---- 教练层状态（每队一份）----
    struct CoachState {
        TeamPlan plan;                       // 当前生效计划
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

        // LLM 请求在途
        float planExpiresAt = 0.0f;   // 计划时效（到期回到默认）
        bool  llmPending = false;
        int   llmGen = 0;
        float llmAt = 0.0f;
        int   llmEventType = 0;
        int   llmRoundNo = 0;
        bool  llmBombPlanted = false;
        float lastThinkAt = 0.0f;
    };
    CoachState coachT_, coachCT_;

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
    void CoachRecordRound(Team t, bool won);
    void CoachResetRound();
    // 回合开始、买枪之前先出计划（save/eco 必须在购买前生效）
    void CoachBeginRound();
    bool LowEconomyAverage(Team t) const;
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
    std::vector<Hostage> hostages_;
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
    float rescueProgress_ = 0.0f;  // 0..1（当前人质）
    int   hostageNearIdx_ = -1;    // 玩家当前靠近的人质索引

    std::vector<KillFeedEntry> killFeed_;

    PathFinder pathFinder_;   // 载入地图后烘焙

    void PushKillFeed(const std::string& text);

    int playerKills_  = 0;
    int playerDeaths_ = 0;

    MatchMode  mode_ = MatchMode::TeamElimination;
    TeamManager teams_;
    RoundManager round_;
    EconomySystem economy_;
    RoundPhase  prevPhase_ = RoundPhase::Freeze;
    bool        matchOverAnnounced_ = false;

    std::vector<BotBehaviorExecutor> botExecutors_;

    // BOT 运行时状态（与 bots_ 同下标）
    std::vector<BotRuntime> botAi_;

    // 小队协同状态（索引与 bots_ 对齐）
    std::vector<SquadOrder> squad_;
    float squadFocusTimer_ = 0.0f;

    // 团队情报（按阵营分开）
    std::vector<EnemyContact> contactsT_;
    std::vector<EnemyContact> contactsCT_;

    float botRespawnTimer_  = 0.0f;
    static constexpr float BOT_RESPAWN_DELAY = 3.0f;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> angleDist_{0.0f, 6.283185f};

    float mouseSensitivity_ = 1.0f;
    bool  prevMouseDown_    = false;

    int   slotAmmo_[2]    = {30, 7};
    int   slotMaxAmmo_[2] = {30, 7};

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

    // 找到武器对应的枪声（供玩家与 BOT 共用）
    static SoundEffect FireSoundFor(WeaponId id);
    // 武器对应的换弹音效（时长与 WeaponDef::reloadTime 对齐）
    static SoundEffect ReloadSoundFor(WeaponId id);

    // ---- 小队协同 ----
    void AssignSquadRoles();            // 回合开始时分配 突破/支援/断后
    void UpdateSquadFocus(float dt);    // 周期性选集火目标（含掩护偏好）
    const Player* ResolveFocus(const SquadOrder& order) const;

    // ---- 团队情报共享 ----
    void UpdateTeamContacts(float dt);                 // 上报 + 老化
    void ReportContact(Team team, int id, float x, float y);
    const EnemyContact* FindNearestContact(Team team, float x, float y) const;
    std::vector<EnemyContact>&       ContactsOf(Team t);
    const std::vector<EnemyContact>& ContactsOf(Team t) const;
    // 找一个「真实可见」的敌人（距离 + 烟雾 + LOS）
    const Player* FindVisibleEnemy(const Player& self, float& outDist) const;
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

    void MoveWithCollision(Player& entity, float dt);
    void FireBullet(float x, float y, float angle, const WeaponDef& def,
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
    void CheckWinConditions(class AudioManager* audio);
    void ApplyRoundResult(Team winner, class AudioManager* audio = nullptr);
    // 统一伤害入口：先扣护甲再扣血。返回 true 表示本次伤害致死。
    bool ApplyDamage(Player& victim, int amount);
    void GrantRoundEndMoney(Team winner);   // 发放回合金钱并重置本回合装备

    // 目标物
    void SetupObjective();                  // 按模式初始化包点/人质
    void UpdateObjective(float dt, bool interactHeld, class AudioManager* audio);
    void PlantBomb(float x, float y, class AudioManager* audio);
    void DetonateBomb(class AudioManager* audio);
    void OnCombatantKilled(Player& victim, class AudioManager* audio);
};
