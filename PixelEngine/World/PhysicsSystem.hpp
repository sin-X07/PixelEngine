#pragma once
#include "WorldMap.hpp"
#include "../Graphics/ParticleSystem.hpp"
#include <SDL2/SDL.h>

class PhysicsSystem {
public:
    PhysicsSystem() = default;

    static bool CheckCollision(float px, float py, float pSize,
                                float wx, float wy, float wW, float wH);

    static bool CheckRectCollision(float ax, float ay, float aw, float ah,
                                    float bx, float by, float bw, float bh);

    // 障碍物阻挡判定
    bool CheckObstacleCollision(float px, float py, float pSize, const WorldMap& worldMap) const;

    // 挥剑攻击判定（击碎木箱与矮墙）
    bool PerformAttackHitCheck(float playerX, float playerY, float playerSize,
                               Direction attackDir, WorldMap& worldMap,
                               ParticleSystem& particleSystem);

    // 陷阱触碰致死判定
    int CheckTrapCollision(float playerX, float playerY, float playerSize, const WorldMap& worldMap) const;

    // 金币收集判定（返回吃到的金币索引，未吃到返回 -1）
    int CheckCoinPickup(float playerX, float playerY, float playerSize, const WorldMap& worldMap) const;
};
