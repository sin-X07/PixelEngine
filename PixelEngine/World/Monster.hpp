#pragma once
#include "../Core/Types.hpp"
#include "WorldMap.hpp"
#include <SDL2/SDL.h>
#include <vector>

// 怪物类型：不同血量与攻击方式
enum class MonsterType {
    GRUNT,    // 近战小怪
    BRUTE,    // 重型近战
    SHOOTER   // 远程怪（仅玩家解锁远程武器后刷新）
};

class Monster {
public:
    MonsterType type = MonsterType::GRUNT;

    float x = 0.0f;
    float y = 0.0f;
    float size = 12.0f;
    float speed = 120.0f;
    float attackRange = 12.0f;       // 近战为攻击距离；远程为射击距离
    float attackCooldownMax = 1.0f;
    float attackCooldown = 0.0f;
    float health = 2.0f;
    float maxHealth = 2.0f;
    int damage = 1;
    bool ranged = false;
    float bulletSpeed = 240.0f;      // 远程怪子弹速度
    bool alive = true;

    // 寻路状态
    float repathTimer = 0.0f;
    std::vector<SDL_FPoint> path;
    size_t pathIndex = 0;

    // 按中心坐标生成指定类型的怪物
    static Monster Make(MonsterType type, float centerX, float centerY);

    // 沿寻路结果向玩家移动
    void Update(float dt, float targetCenterX, float targetCenterY, const WorldMap& worldMap);

    // A* 寻路：自动避开墙体、矮墙、木箱、陷阱与宝箱（金币）
    bool RecomputePath(float targetCenterX, float targetCenterY, const WorldMap& worldMap);

private:
    bool IsNextWaypointBlocked(const WorldMap& worldMap) const;
};
