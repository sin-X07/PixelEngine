#pragma once
#include "WorldMap.hpp"
#include <SDL2/SDL.h>
#include <vector>

// 被破坏的可破坏障碍（供调用方发射对应碎屑粒子）
struct BrokenObstacle {
    SDL_Rect rect;
    bool isLowWall = false; // true = 矮墙(H)；false = 木箱(o)
};

class PhysicsSystem {
public:
    PhysicsSystem() = default;

    static bool CheckCollision(float px, float py, float pSize,
                                float wx, float wy, float wW, float wH);

    static bool CheckRectCollision(float ax, float ay, float aw, float ah,
                                    float bx, float by, float bw, float bh);

    // 障碍物阻挡判定
    bool CheckObstacleCollision(float px, float py, float pSize, const WorldMap& worldMap) const;

    // 挥剑攻击判定（击碎木箱与矮墙），返回被破坏的障碍列表
    std::vector<BrokenObstacle> PerformAttackHitCheck(float playerX, float playerY, float playerSize,
                                                      Direction attackDir, WorldMap& worldMap);

    // 破坏矩形范围内所有可破坏障碍（H 矮墙 / o 木箱），不破坏地形（# 墙），返回被破坏列表
    std::vector<BrokenObstacle> BreakObstaclesInRect(float x, float y, float w, float h,
                                                     WorldMap& worldMap);

    // 子弹推进判定：命中 # 墙返回 true（应停止）；命中 H/o 则破坏并写入 outBroken，返回 false
    bool BulletPassesAndBreaks(float x, float y, WorldMap& worldMap,
                               std::vector<BrokenObstacle>& outBroken);

    // 陷阱触碰致死判定
    int CheckTrapCollision(float playerX, float playerY, float playerSize, const WorldMap& worldMap) const;

    // 金币收集判定（返回吃到的金币索引，未吃到返回 -1）
    int CheckCoinPickup(float playerX, float playerY, float playerSize, const WorldMap& worldMap) const;
};
