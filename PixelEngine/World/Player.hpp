#pragma once

// 玩家实体：位置、速度、生命、受伤/无敌/回血逻辑
class Player {
public:
    static constexpr int   MAX_HEALTH = 10;
    static constexpr float ACCEL      = 500.0f;
    static constexpr float FRICTION   = 8.0f;
    static constexpr float MAX_SPEED  = 140.0f;

    float x = 50.0f;
    float y = 50.0f;
    float size = 12.0f;
    float vx = 0.0f;
    float vy = 0.0f;

    int   health = MAX_HEALTH;
    float invulnTimer = 0.0f;
    float noHitTimer = 0.0f;
    float hpRegenTimer = 0.0f;

    bool diedByTrap = false;
    bool diedByMonster = false;

    void Reset();                                  // 重置速度/生命/计时/死亡标记（出生坐标由外部赋给 x/y）
    // 返回：0 = 无敌帧未受伤；1 = 受伤未死亡；2 = 受伤致死
    int TakeDamage(int amount);
    void UpdateTimers(float dt);                   // 无敌帧倒计时 + 脱离战斗缓慢回血
    bool IsInvulnerable() const { return invulnTimer > 0.0f; }
};
