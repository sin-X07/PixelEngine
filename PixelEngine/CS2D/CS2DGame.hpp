#pragma once
#include "CS2DWorld.hpp"
#include "CS2DRenderer.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <random>
#include <memory>

// ============================================================================
// CS2DGame —— CS2D 模式游戏逻辑（俯视角射击对抗）
//   首版：单机 BOT 对战，自由击杀模式
// ============================================================================
class CS2DGame {
public:
    // ---- 玩家/BOT 实体 ----
    struct Player {
        float x       = 0.0f;
        float y       = 0.0f;
        float vx      = 0.0f;
        float vy      = 0.0f;
        float size    = 24.0f;       // 命中盒边长
        float aimAngle = 0.0f;       // 瞄准方向（弧度）
        int   health  = 100;
        int   maxHealth = 100;
        bool  isCT    = false;
        bool  alive   = true;
        float shootCooldown = 0.0f;
    };

    // ---- 子弹 ----
    struct Bullet {
        float x  = 0.0f;
        float y  = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float traveled = 0.0f;
        float maxDist  = 800.0f;
        int   damage   = 25;
        int   ownerIdx  = -1;        // -1=玩家, >=0=BOT索引
        bool  active   = true;
    };

    CS2DGame();
    ~CS2DGame() = default;

    // 初始化：加载 TMJ → 构建 CS2DWorld → 设置 CS2DRenderer
    bool LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer);

    // 重置游戏状态
    void Reset();

    // 每帧更新
    void Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                bool wPressed, bool aPressed, bool sPressed, bool dPressed);

    // 渲染
    void Render();

    // 访问器
    CS2DWorld&       GetWorld()       { return *world_; }
    const CS2DWorld& GetWorld() const { return *world_; }
    CS2DRenderer*    GetRenderer()    { return renderer_; }

    Player&          GetPlayer()      { return player_; }
    const Player&    GetPlayer() const { return player_; }

    const std::vector<Player>& GetBOTs()  const { return bots_; }
    const std::vector<Bullet>& GetBullets() const { return bullets_; }

    int GetPlayerKills()  const { return playerKills_; }
    int GetPlayerDeaths() const { return playerDeaths_; }
    int GetBotCount()     const { return static_cast<int>(bots_.size()); }

    float GetAimAngle() const { return player_.aimAngle; }

private:
    std::unique_ptr<CS2DWorld> world_;
    CS2DRenderer* renderer_ = nullptr;

    Player player_;
    std::vector<Player> bots_;
    std::vector<Bullet> bullets_;

    int playerKills_  = 0;
    int playerDeaths_ = 0;

    // BOT 计时
    float botAITimer_       = 0.0f;
    float botRespawnTimer_  = 0.0f;
    static constexpr float BOT_RESPAWN_DELAY = 3.0f;
    static constexpr int   MAX_BOTS          = 8;
    static constexpr int   ACTIVE_BOTS       = 4;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> angleDist_{0.0f, 6.283185f};

    // 内部方法
    void SpawnPlayer();
    void SpawnBOT(Player& bot, bool isCT);
    void UpdatePlayerMovement(float dt, bool w, bool a, bool s, bool d);
    void UpdatePlayerShooting(float dt, bool mouseDown, float worldMX, float worldMY);
    void UpdateBOTs(float dt);
    void UpdateBullets(float dt);
    void RespawnBOT(Player& bot);

    // 碰撞移动（分离轴推挤）
    void MoveWithCollision(Player& entity, float dt);

    // 发射子弹
    void FireBullet(float x, float y, float angle, int damage, int ownerIdx);
};