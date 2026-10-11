#include "WorldMap.hpp"
#include "../Core/PathUtils.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <queue>
#include <cmath>

bool WorldMap::LoadLevelFromFile(const std::string& filename, float& outPlayerX, float& outPlayerY, float playerSize) {
    const std::string resolvedPath = PathUtils::ResolveProjectFile(filename);
    std::ifstream mapFile(resolvedPath);
    if (!mapFile) {
        std::cerr << "无法打开关卡地图文件：" << filename << std::endl;
        return false;
    }

    std::vector<std::string> mapRows;
    std::string line;
    while (std::getline(mapFile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (!mapRows.empty() && line.size() != mapRows.front().size()) {
            std::cerr << "关卡地图各行宽度不一致: " << filename << std::endl;
            return false;
        }
        mapRows.push_back(line);
    }

    if (mapRows.empty()) {
        std::cerr << "关卡地图为空: " << filename << std::endl;
        return false;
    }

    Clear();
    levelGrid = mapRows;
    mapHeightTiles = static_cast<int>(mapRows.size());
    mapWidthTiles = static_cast<int>(mapRows.front().size());

    for (int row = 0; row < mapHeightTiles; ++row) {
        for (int col = 0; col < mapWidthTiles; ++col) {
            const char tileType = mapRows[row][col];
            const int pixelX = col * TILE_SIZE;
            const int pixelY = row * TILE_SIZE;

            if (tileType == '#') {
                walls.push_back({pixelX, pixelY, TILE_SIZE, TILE_SIZE});
            } else if (tileType == 'H') {
                lowWalls.push_back({pixelX, pixelY, TILE_SIZE, TILE_SIZE});
            } else if (tileType == 'o') {
                const int obstacleSize = TILE_SIZE / 2;
                smallObstacles.push_back({
                    pixelX + (TILE_SIZE - obstacleSize) / 2,
                    pixelY + (TILE_SIZE - obstacleSize) / 2,
                    obstacleSize,
                    obstacleSize
                });
            } else if (tileType == 'P') {
                outPlayerX = static_cast<float>(pixelX) + (TILE_SIZE - playerSize) * 0.5f;
                outPlayerY = static_cast<float>(pixelY) + (TILE_SIZE - playerSize) * 0.5f;
            } else if (tileType == 'C') {
                coins.push_back({
                    static_cast<float>(pixelX) + (TILE_SIZE - coinSize) * 0.5f,
                    static_cast<float>(pixelY) + (TILE_SIZE - coinSize) * 0.5f
                });
            }
        }
    }

    mapFile.close();
    std::cout << "成功解析外部关卡 [" << filename << "]! " << mapWidthTiles << "x"
              << mapHeightTiles << " 格，共构建了" << walls.size() << " 面墙壁和 "
              << coins.size() << " 枚宝藏" << std::endl;
    return true;
}

void WorldMap::Clear() {
    walls.clear();
    lowWalls.clear();
    smallObstacles.clear();
    coins.clear();
    traps.clear();
}

void WorldMap::ResetCoinPosition(float playerX, float playerY, float playerSize, std::mt19937& rng) {
    std::vector<SDL_FPoint> candidates;
    for (int row = 1; row < mapHeightTiles - 1; ++row) {
        for (int col = 1; col < mapWidthTiles - 1; ++col) {
            if (levelGrid[row][col] == '#' || levelGrid[row][col] == 'H' ||
                levelGrid[row][col] == 'o') continue;

            const float x = col * TILE_SIZE + (TILE_SIZE - coinSize) * 0.5f;
            const float y = row * TILE_SIZE + (TILE_SIZE - coinSize) * 0.5f;
            const float playerCenterX = playerX + playerSize * 0.5f;
            const float playerCenterY = playerY + playerSize * 0.5f;
            const float coinCenterX = x + coinSize * 0.5f;
            const float coinCenterY = y + coinSize * 0.5f;
            const float dx = coinCenterX - playerCenterX;
            const float dy = coinCenterY - playerCenterY;
            if (dx * dx + dy * dy < 100.0f * 100.0f) continue;

            bool overlapsCoin = false;
            for (const SDL_FPoint& existing : coins) {
                const float cDx = coinCenterX - (existing.x + coinSize * 0.5f);
                const float cDy = coinCenterY - (existing.y + coinSize * 0.5f);
                if (cDx * cDx + cDy * cDy < TILE_SIZE * TILE_SIZE) {
                    overlapsCoin = true;
                    break;
                }
            }
            if (!overlapsCoin) candidates.push_back({x, y});
        }
    }

    if (candidates.empty()) return;
    std::uniform_int_distribution<size_t> chooseCandidate(0, candidates.size() - 1);
    coins.push_back(candidates[chooseCandidate(rng)]);
}

void WorldMap::EnsureCoinCount(size_t targetCount, float playerX, float playerY, float playerSize, std::mt19937& rng) {
    if (coins.size() > targetCount) coins.resize(targetCount);
    while (coins.size() < targetCount) {
        const size_t prevCount = coins.size();
        ResetCoinPosition(playerX, playerY, playerSize, rng);
        if (coins.size() == prevCount) break;
    }
}

bool WorldMap::IsChokepointTile(int row, int col, float playerX, float playerY, float playerSize) const {
    if (row < 2 || row >= mapHeightTiles - 2 || col < 2 || col >= mapWidthTiles - 2) {
        return true;
    }
    if (levelGrid[row][col] != '.') {
        return true;
    }

    const float tileCenterX = col * TILE_SIZE + TILE_SIZE * 0.5f;
    const float tileCenterY = row * TILE_SIZE + TILE_SIZE * 0.5f;
    const float pCenterX = playerX + playerSize * 0.5f;
    const float pCenterY = playerY + playerSize * 0.5f;
    const float dx = tileCenterX - pCenterX;
    const float dy = tileCenterY - pCenterY;
    if (dx * dx + dy * dy < 120.0f * 120.0f) {
        return true;
    }

    for (const auto& coin : coins) {
        const float cDx = tileCenterX - (coin.x + coinSize * 0.5f);
        const float cDy = tileCenterY - (coin.y + coinSize * 0.5f);
        if (cDx * cDx + cDy * cDy < (TILE_SIZE * 2.5f) * (TILE_SIZE * 2.5f)) {
            return true;
        }
    }

    auto isSolid = [this](int r, int c) {
        if (r < 0 || r >= mapHeightTiles || c < 0 || c >= mapWidthTiles) return true;
        char ch = levelGrid[r][c];
        return ch == '#' || ch == 'H' || ch == 'o';
    };

    if (isSolid(row - 1, col) || isSolid(row + 1, col)) return true;
    if (isSolid(row, col - 1) || isSolid(row, col + 1)) return true;

    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (isSolid(row + dr, col + dc)) {
                return true;
            }
        }
    }

    return false;
}

bool WorldMap::CheckMapConnectivity(const std::vector<Trap>& testTraps, float playerX, float playerY) const {
    std::vector<std::vector<bool>> blocked(mapHeightTiles, std::vector<bool>(mapWidthTiles, false));
    for (int r = 0; r < mapHeightTiles; ++r) {
        for (int c = 0; c < mapWidthTiles; ++c) {
            if (levelGrid[r][c] == '#' || levelGrid[r][c] == 'H' || levelGrid[r][c] == 'o') {
                blocked[r][c] = true;
            }
        }
    }
    for (const auto& t : testTraps) {
        int tr = static_cast<int>(t.y / TILE_SIZE);
        int tc = static_cast<int>(t.x / TILE_SIZE);
        if (tr >= 0 && tr < mapHeightTiles && tc >= 0 && tc < mapWidthTiles) {
            blocked[tr][tc] = true;
        }
    }

    int startR = static_cast<int>(playerY / TILE_SIZE);
    int startC = static_cast<int>(playerX / TILE_SIZE);
    if (startR < 0 || startR >= mapHeightTiles || startC < 0 || startC >= mapWidthTiles) return false;
    if (blocked[startR][startC]) return false;

    std::vector<std::vector<bool>> visited(mapHeightTiles, std::vector<bool>(mapWidthTiles, false));
    std::queue<std::pair<int, int>> q;
    q.push({startR, startC});
    visited[startR][startC] = true;

    const int dr[] = {-1, 1, 0, 0};
    const int dc[] = {0, 0, -1, 1};

    while (!q.empty()) {
        auto [currR, currC] = q.front();
        q.pop();

        for (int i = 0; i < 4; ++i) {
            int nr = currR + dr[i];
            int nc = currC + dc[i];
            if (nr >= 0 && nr < mapHeightTiles && nc >= 0 && nc < mapWidthTiles) {
                if (!blocked[nr][nc] && !visited[nr][nc]) {
                    visited[nr][nc] = true;
                    q.push({nr, nc});
                }
            }
        }
    }

    for (const auto& coin : coins) {
        int cr = static_cast<int>(coin.y / TILE_SIZE);
        int cc = static_cast<int>(coin.x / TILE_SIZE);
        if (cr >= 0 && cr < mapHeightTiles && cc >= 0 && cc < mapWidthTiles) {
            if (!visited[cr][cc]) return false;
        }
    }

    for (int r = 1; r < mapHeightTiles - 1; ++r) {
        for (int c = 1; c < mapWidthTiles - 1; ++c) {
            if (levelGrid[r][c] == '.' && !blocked[r][c]) {
                if (!visited[r][c]) {
                    return false;
                }
            }
        }
    }

    return true;
}

void WorldMap::GenerateTrapsForTimeAttack(float playerX, float playerY, float playerSize, std::mt19937& rng) {
    traps.clear();
    std::vector<std::pair<int, int>> candidates;

    for (int r = 1; r < mapHeightTiles - 1; ++r) {
        for (int c = 1; c < mapWidthTiles - 1; ++c) {
            if (!IsChokepointTile(r, c, playerX, playerY, playerSize)) {
                candidates.push_back({r, c});
            }
        }
    }

    if (candidates.empty()) return;

    std::shuffle(candidates.begin(), candidates.end(), rng);

    const size_t targetTrapCount = 8;
    std::vector<Trap> acceptedTraps;

    for (const auto& pos : candidates) {
        if (acceptedTraps.size() >= targetTrapCount) break;

        const float wx = pos.second * TILE_SIZE + 2.0f;
        const float wy = pos.first * TILE_SIZE + 2.0f;
        const float trapSize = 16.0f;

        bool tooClose = false;
        for (const auto& existing : acceptedTraps) {
            float distSq = (existing.x - wx) * (existing.x - wx) + (existing.y - wy) * (existing.y - wy);
            if (distSq < (TILE_SIZE * 3.5f) * (TILE_SIZE * 3.5f)) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        TrapType type = (acceptedTraps.size() % 2 == 0) ? TRAP_SPIKES : TRAP_BEAR_TRAP;
        std::vector<Trap> testList = acceptedTraps;
        testList.push_back({wx, wy, trapSize, type});

        if (CheckMapConnectivity(testList, playerX, playerY)) {
            acceptedTraps.push_back({wx, wy, trapSize, type});
        }
    }

    traps = acceptedTraps;
    std::cout << "模式一陷阱生成完毕！共放置 " << traps.size() << " 个致命陷阱（地刺/老鼠夹），空间可通过性保证 100% 畅通！" << std::endl;
}
