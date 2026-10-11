#pragma once
#include "../Core/Types.hpp"
#include "../World/WorldMap.hpp"
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <vector>
#include <string>

class RenderSystem {
private:
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* dungeonFloorTexture = nullptr;
    SDL_Texture* dungeonWallTexture = nullptr;
    SDL_Texture* playerTexture = nullptr;
    SDL_Texture* treasureTexture = nullptr;
    SDL_Texture* slashTexture = nullptr;

    bool imageInitialized = false;

    // 摄像机与视野参数
    static constexpr float CAMERA_ZOOM = 2.0f;
    static constexpr int VISION_MASK_SCALE = 4;
    static constexpr float VISION_RADIUS = 120.0f;
    static constexpr float VISION_SOFT_EDGE = 20.0f;

    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float sprintBlend = 0.0f;
    float focusBlend = 0.0f;

    SDL_Texture* visionTexture = nullptr;
    std::vector<Uint8> visionPixels;

public:
    RenderSystem() = default;
    ~RenderSystem() { Clean(); }

    bool Initialize(SDL_Renderer* ren, int screenWidth, int screenHeight);
    void Clean();

    void UpdateCamera(float playerX, float playerY, float playerSize, const WorldMap& worldMap,
                      bool isGameplayVisible, float shakeOffsetX = 0.0f, float shakeOffsetY = 0.0f);

    void UpdateMovementBlends(float dt, bool sprintHeld, bool focusHeld);
    void ResetMovementBlends();

    float CurrentCameraZoom() const;
    float CurrentVisionRadius() const;
    float CurrentVisionSoftEdge() const;
    float GetSprintBlend() const { return sprintBlend; }
    float GetFocusBlend() const { return focusBlend; }

    void DrawDungeonBackground(const WorldMap& worldMap, Uint8 overlayAlpha = 0);
    void DrawWorld(const WorldMap& worldMap, float playerX, float playerY, float playerSize,
                   bool isAttacking, Direction attackDir, float attackTimer, float attackDuration,
                   bool drawCoins);
    void DrawVisionFog(float playerX, float playerY, float playerSize, const WorldMap& worldMap);

    bool HasLineOfSight(float targetWorldX, float targetWorldY,
                        float playerCenterX, float playerCenterY,
                        const WorldMap& worldMap) const;

    void DrawTrap(const Trap& trap);

    float GetCameraX() const { return cameraX; }
    float GetCameraY() const { return cameraY; }
};
