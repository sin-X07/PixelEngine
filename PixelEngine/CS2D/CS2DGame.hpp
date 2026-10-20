#pragma once
#include "CS2DWorld.hpp"
#include "CS2DRenderer.hpp"
#include "Teams/TeamManager.hpp"
#include "Rounds/RoundManager.hpp"
#include "Weapons/WeaponDef.hpp"
#include "Economy/EconomySystem.hpp"
#include "../Core/Types.hpp"
#include "../LLM/BotIntent.hpp"
#include "../LLM/BotBehaviorExecutor.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <random>
#include <memory>
#include <algorithm>

class AudioManager;

// 对局模式（Phase 2：死斗 / 团队歼灭；Phase 5 追加爆破与人质）
enum class MatchMode {
    DeathMatch,        // 自由击杀，即时重生，无回合
    TeamElimination    // 回合制，T/CT 歼灭，先到 N 分获胜
};

// 武器槽参数表（slot: 0=主武器, 1=副武器, 2=近战）
struct WeaponSlotParams {
    float damage;
    float cooldown;
    float speed;
    float range;
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
        float size    = 6.0f;
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
        WeaponId  weapons[4] = { WeaponId::M4A1, WeaponId::Glock, WeaponId::Knife, WeaponId::None };
        int       grenadeCount = 0;        // 当前投掷物数量

        // 弹药（活动槽位）
        int       ammo      = 30;
        int       maxAmmo   = 30;
        bool      reloading = false;
        float     reloadTimer = 0.0f;
        static constexpr float RELOAD_TIME = 2.0f;

        bool      stealth   = false;

        // 战局数据
        int       money  = 800;
        int       kills  = 0;
        int       deaths = 0;

        // LLM 战略意图
        BotIntent llmIntent;

        // 当前活动武器定义
        const WeaponDef& CurrentWeapon() const {
            const int s = (weaponSlot < 0 || weaponSlot > 3) ? 0 : weaponSlot;
            return WeaponDB::Get(weapons[s]);
        }
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

    CS2DGame();
    ~CS2DGame() = default;

    bool LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer);
    void Reset();

    void Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                bool wPressed, bool aPressed, bool sPressed, bool dPressed,
                bool shiftPressed, bool rPressed,
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

    // 玩家是否处于闪光致盲中（0..1 强度）
    float GetFlashAmount() const { return (flashTimer_ > 0.0f) ? 1.0f : 0.0f; }

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
    bool IsBuyPhase()    const {
        return mode_ == MatchMode::TeamElimination &&
               (round_.Phase() == RoundPhase::Freeze || round_.Phase() == RoundPhase::Buy);
    }
    // 玩家当前是否处于购买区内（地图未定义购买区时视为全图可买）
    bool IsPlayerInBuyZone() const;
    // 尝试购买；成功返回 true（已扣款并装备）
    bool TryBuy(WeaponId id);

    // ---- 武器 ----
    void SwitchWeaponSlot(int slot);
    std::string GetWeaponName() const;

    // BOT 视野半径（LLM 状态采样用）
    static constexpr float BOT_VIEW_RADIUS = 50.0f;

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
    float flashTimer_ = 0.0f;      // 玩家闪光致盲剩余秒数

    int playerKills_  = 0;
    int playerDeaths_ = 0;

    MatchMode  mode_ = MatchMode::TeamElimination;
    TeamManager teams_;
    RoundManager round_;
    EconomySystem economy_;
    RoundPhase  prevPhase_ = RoundPhase::Freeze;
    bool        matchOverAnnounced_ = false;

    std::vector<BotBehaviorExecutor> botExecutors_;

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
    void UpdateBullets(float dt, class AudioManager* audio);
    void UpdateGrenades(float dt, class AudioManager* audio);
    void ThrowGrenade(Player& thrower, float aimAngle);
    void ExplodeGrenade(const Grenade& g, class AudioManager* audio);
    bool SegmentBlockedBySmoke(float x1, float y1, float x2, float y2) const;
    void RespawnBOT(Player& bot);

    void MoveWithCollision(Player& entity, float dt);
    void FireBullet(float x, float y, float angle, const WeaponDef& def,
                    int ownerIdx, Team ownerTeam);

    bool BotCanSeePlayer(const Player& bot) const;
    bool PlayerCanSeeBot(const Player& bot) const;

    void StartReload(Player& p);
    void UpdateReload(Player& p, float dt);

    bool IsEnemyNearby(float x, float y, float safeRadius) const;

    // 回合流程
    void UpdateRoundFlow(float dt, class AudioManager* audio);
    void BeginNewRound();
    void RespawnAllForRound();
    void CheckWinConditions(class AudioManager* audio);
    void ApplyRoundResult(Team winner);
    void GrantRoundEndMoney(Team winner);   // 发放回合金钱并重置本回合装备
    void OnCombatantKilled(Player& victim, class AudioManager* audio);
};
