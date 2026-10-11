#include "PhysicsSystem.hpp"

bool PhysicsSystem::CheckCollision(float px, float py, float pSize,
                                    float wx, float wy, float wW, float wH) {
    return px < (wx + wW) &&
           (px + pSize) > wx &&
           py < (wy + wH) &&
           (py + pSize) > wy;
}

bool PhysicsSystem::CheckRectCollision(float ax, float ay, float aw, float ah,
                                        float bx, float by, float bw, float bh) {
    return ax < (bx + bw) &&
           (ax + aw) > bx &&
           ay < (by + bh) &&
           (ay + ah) > by;
}

bool PhysicsSystem::CheckObstacleCollision(float px, float py, float pSize, const WorldMap& worldMap) const {
    for (const auto& lowWall : worldMap.GetLowWalls()) {
        if (CheckCollision(px, py, pSize, lowWall.x, lowWall.y, lowWall.w, lowWall.h)) {
            return true;
        }
    }
    for (const auto& obstacle : worldMap.GetSmallObstacles()) {
        if (CheckCollision(px, py, pSize, obstacle.x, obstacle.y, obstacle.w, obstacle.h)) {
            return true;
        }
    }
    return false;
}

bool PhysicsSystem::PerformAttackHitCheck(float playerX, float playerY, float playerSize,
                                          Direction attackDir, WorldMap& worldMap,
                                          ParticleSystem& particleSystem) {
    float hitX = playerX;
    float hitY = playerY;
    float hitW = playerSize;
    float hitH = playerSize;
    const float reach = 18.0f;

    if (attackDir == DIR_UP) {
        hitX = playerX - 3.0f;
        hitY = playerY - reach;
        hitW = playerSize + 6.0f;
        hitH = reach;
    } else if (attackDir == DIR_DOWN) {
        hitX = playerX - 3.0f;
        hitY = playerY + playerSize;
        hitW = playerSize + 6.0f;
        hitH = reach;
    } else if (attackDir == DIR_LEFT) {
        hitX = playerX - reach;
        hitY = playerY - 3.0f;
        hitW = reach;
        hitH = playerSize + 6.0f;
    } else if (attackDir == DIR_RIGHT) {
        hitX = playerX + playerSize;
        hitY = playerY - 3.0f;
        hitW = reach;
        hitH = playerSize + 6.0f;
    }

    bool hitOccurred = false;
    const int tileSize = worldMap.GetTileSize();
    auto& levelGrid = worldMap.GetLevelGrid();
    const int mapHeight = worldMap.GetMapHeightTiles();
    const int mapWidth = worldMap.GetMapWidthTiles();

    // 1. 检查小障碍物 (木质箱子路障)
    auto& smallObstacles = worldMap.GetSmallObstacles();
    for (auto it = smallObstacles.begin(); it != smallObstacles.end(); ) {
        if (CheckRectCollision(hitX, hitY, hitW, hitH, it->x, it->y, it->w, it->h)) {
            particleSystem.EmitDebrisBurst(it->x + it->w * 0.5f, it->y + it->h * 0.5f, 26, {194, 111, 58, 255}); // 棕色木屑

            int gridCol = it->x / tileSize;
            int gridRow = it->y / tileSize;
            if (gridRow >= 0 && gridRow < mapHeight && gridCol >= 0 && gridCol < mapWidth) {
                if (levelGrid[gridRow][gridCol] == 'o') {
                    levelGrid[gridRow][gridCol] = '.';
                }
            }

            it = smallObstacles.erase(it);
            hitOccurred = true;
        } else {
            ++it;
        }
    }

    // 2. 检查矮墙 (砖石路障)
    auto& lowWalls = worldMap.GetLowWalls();
    for (auto it = lowWalls.begin(); it != lowWalls.end(); ) {
        if (CheckRectCollision(hitX, hitY, hitW, hitH, it->x, it->y, it->w, it->h)) {
            particleSystem.EmitDebrisBurst(it->x + it->w * 0.5f, it->y + it->h * 0.5f, 32, {140, 150, 155, 255}); // 砖石青灰石屑

            int gridCol = it->x / tileSize;
            int gridRow = it->y / tileSize;
            if (gridRow >= 0 && gridRow < mapHeight && gridCol >= 0 && gridCol < mapWidth) {
                if (levelGrid[gridRow][gridCol] == 'H') {
                    levelGrid[gridRow][gridCol] = '.';
                }
            }

            it = lowWalls.erase(it);
            hitOccurred = true;
        } else {
            ++it;
        }
    }

    return hitOccurred;
}

int PhysicsSystem::CheckTrapCollision(float playerX, float playerY, float playerSize, const WorldMap& worldMap) const {
    const auto& traps = worldMap.GetTraps();
    for (size_t i = 0; i < traps.size(); ++i) {
        const auto& trap = traps[i];
        const float trapInset = 3.0f;
        if (CheckCollision(playerX, playerY, playerSize,
                           trap.x + trapInset, trap.y + trapInset,
                           trap.size - trapInset * 2.0f, trap.size - trapInset * 2.0f)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int PhysicsSystem::CheckCoinPickup(float playerX, float playerY, float playerSize, const WorldMap& worldMap) const {
    const auto& coins = worldMap.GetCoins();
    const float coinSize = worldMap.GetCoinSize();
    for (size_t i = 0; i < coins.size(); ++i) {
        if (CheckCollision(playerX, playerY, playerSize, coins[i].x, coins[i].y, coinSize, coinSize)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
