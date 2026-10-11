#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <random>

class WorldMap {
private:
    static constexpr int TILE_SIZE = 20;
    int mapWidthTiles = 40;
    int mapHeightTiles = 30;
    float coinSize = 8.0f;

    std::vector<std::string> levelGrid;
    std::vector<SDL_Rect> walls;
    std::vector<SDL_Rect> lowWalls;
    std::vector<SDL_Rect> smallObstacles;
    std::vector<SDL_FPoint> coins;
    std::vector<Trap> traps;

public:
    WorldMap() = default;

    bool LoadLevelFromFile(const std::string& filename, float& outPlayerX, float& outPlayerY, float playerSize);
    void Clear();

    void ResetCoinPosition(float playerX, float playerY, float playerSize, std::mt19937& rng);
    void EnsureCoinCount(size_t targetCount, float playerX, float playerY, float playerSize, std::mt19937& rng);

    void GenerateTrapsForTimeAttack(float playerX, float playerY, float playerSize, std::mt19937& rng);
    bool IsChokepointTile(int row, int col, float playerX, float playerY, float playerSize) const;
    bool CheckMapConnectivity(const std::vector<Trap>& testTraps, float playerX, float playerY) const;

    // 访问器
    int GetTileSize() const { return TILE_SIZE; }
    int GetMapWidthTiles() const { return mapWidthTiles; }
    int GetMapHeightTiles() const { return mapHeightTiles; }
    float GetCoinSize() const { return coinSize; }

    const std::vector<std::string>& GetLevelGrid() const { return levelGrid; }
    std::vector<std::string>& GetLevelGrid() { return levelGrid; }

    const std::vector<SDL_Rect>& GetWalls() const { return walls; }
    const std::vector<SDL_Rect>& GetLowWalls() const { return lowWalls; }
    std::vector<SDL_Rect>& GetLowWalls() { return lowWalls; }

    const std::vector<SDL_Rect>& GetSmallObstacles() const { return smallObstacles; }
    std::vector<SDL_Rect>& GetSmallObstacles() { return smallObstacles; }

    const std::vector<SDL_FPoint>& GetCoins() const { return coins; }
    std::vector<SDL_FPoint>& GetCoins() { return coins; }

    const std::vector<Trap>& GetTraps() const { return traps; }
    std::vector<Trap>& GetTraps() { return traps; }
};
