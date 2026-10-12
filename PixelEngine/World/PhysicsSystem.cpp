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
    return ax < (bx + bw) && (ax + aw) > bx && ay < (by + bh) && (ay + ah) > by;
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

std::vector<BrokenObstacle> PhysicsSystem::PerformAttackHitCheck(float playerX, float playerY, float playerSize,
                                                                 Direction attackDir, WorldMap& worldMap) {
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

    return BreakObstaclesInRect(hitX, hitY, hitW, hitH, worldMap);
}

std::vector<BrokenObstacle> PhysicsSystem::BreakObstaclesInRect(float x, float y, float w, float h,
                                                                WorldMap& worldMap) {
    std::vector<BrokenObstacle> broken;
    const int tileSize = worldMap.GetTileSize();
    auto& levelGrid = worldMap.GetLevelGrid();
    const int mapHeight = worldMap.GetMapHeightTiles();
    const int mapWidth = worldMap.GetMapWidthTiles();

    // 1. 小障碍物 (木质箱子路障)
    auto& smallObstacles = worldMap.GetSmallObstacles();
    for (auto it = smallObstacles.begin(); it != smallObstacles.end(); ) {
        if (CheckRectCollision(x, y, w, h, it->x, it->y, it->w, it->h)) {
            broken.push_back({*it, false});

            int gridCol = it->x / tileSize;
            int gridRow = it->y / tileSize;
            if (gridRow >= 0 && gridRow < mapHeight && gridCol >= 0 && gridCol < mapWidth) {
                if (levelGrid[gridRow][gridCol] == 'o') {
                    levelGrid[gridRow][gridCol] = '.';
                }
            }

            it = smallObstacles.erase(it);
        } else {
            ++it;
        }
    }

    // 2. 矮墙 (砖石路障)
    auto& lowWalls = worldMap.GetLowWalls();
    for (auto it = lowWalls.begin(); it != lowWalls.end(); ) {
        if (CheckRectCollision(x, y, w, h, it->x, it->y, it->w, it->h)) {
            broken.push_back({*it, true});

            int gridCol = it->x / tileSize;
            int gridRow = it->y / tileSize;
            if (gridRow >= 0 && gridRow < mapHeight && gridCol >= 0 && gridCol < mapWidth) {
                if (levelGrid[gridRow][gridCol] == 'H') {
                    levelGrid[gridRow][gridCol] = '.';
                }
            }

            it = lowWalls.erase(it);
        } else {
            ++it;
        }
    }

    return broken;
}

bool PhysicsSystem::BulletPassesAndBreaks(float x, float y, WorldMap& worldMap,
                                          std::vector<BrokenObstacle>& outBroken) {
    const int tileSize = worldMap.GetTileSize();
    const auto& levelGrid = worldMap.GetLevelGrid();
    const int mapHeight = worldMap.GetMapHeightTiles();
    const int mapWidth = worldMap.GetMapWidthTiles();

    const int c = static_cast<int>(x / tileSize);
    const int r = static_cast<int>(y / tileSize);
    if (r >= 0 && r < mapHeight && c >= 0 && c < mapWidth) {
        if (levelGrid[r][c] == '#') {
            return true; // 命中地形墙，子弹停止
        }
    }

    // 破坏命中的可破坏障碍并继续飞行
    outBroken = BreakObstaclesInRect(x - 1.5f, y - 1.5f, 3.0f, 3.0f, worldMap);
    return false;
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
