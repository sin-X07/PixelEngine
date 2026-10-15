#pragma once
#include "../Core/Types.hpp"
#include "../Core/Roles.hpp"
#include "../World/WorldMap.hpp"
#include "../World/Monster.hpp"
#include "../World/Weapon.hpp"
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <array>
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

    // 怪物 / 武器 / 陷阱 / 特效贴图（素材缺失时为 nullptr，绘制回退纯色）
    SDL_Texture* gruntTexture = nullptr;
    SDL_Texture* bruteTexture = nullptr;
    SDL_Texture* shooterTexture = nullptr;
    SDL_Texture* daggerTexture = nullptr;
    SDL_Texture* katanaTexture = nullptr;
    SDL_Texture* pistolTexture = nullptr;
    SDL_Texture* rifleTexture = nullptr;
    SDL_Texture* spikesTexture = nullptr;
    SDL_Texture* bearTrapTexture = nullptr;
    SDL_Texture* bulletTexture = nullptr;
    SDL_Texture* bloodTexture = nullptr;

    // 可选角色贴图（与 Core/Roles.hpp 的 GetRoles() 顺序一一对应；缺失为 nullptr，绘制回退纯色）
    std::array<SDL_Texture*, ROLE_COUNT> roleTextures{};
    int activeRole = 0;

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
    float MaxVisionRadius() const { return VISION_RADIUS * 2.0f; }
    float GetSprintBlend() const { return sprintBlend; }
    float GetFocusBlend() const { return focusBlend; }

    void DrawDungeonBackground(const WorldMap& worldMap, Uint8 overlayAlpha = 0);
    // 主菜单/模式选择的视差循环动画背景（替代 DrawDungeonBackground）
    void DrawAnimatedMenuBackground(const WorldMap& worldMap, float timeSeconds);
    void DrawWorld(const WorldMap& worldMap, float playerX, float playerY, float playerSize,
                   bool isAttacking, float attackAngle, float attackTimer, float attackDuration,
                   bool drawCoins);
    void DrawVisionFog(float playerX, float playerY, float playerSize, const WorldMap& worldMap);

    void DrawMonsters(const std::vector<Monster>& monsters);
    void DrawBullets(const std::vector<Bullet>& bullets);
    void DrawWeaponDrops(const std::vector<WeaponDrop>& drops);

    bool HasLineOfSight(float targetWorldX, float targetWorldY,
                        float playerCenterX, float playerCenterY,
                        const WorldMap& worldMap) const;

    void DrawTrap(const Trap& trap);

    SDL_Texture* GetWeaponIconTexture(WeaponType type) const;
    SDL_Texture* GetBloodTexture() const { return bloodTexture; }

    // 角色系统：切换当前角色（越界钳制）与取贴图
    void SetActiveRole(int index) { activeRole = std::clamp(index, 0, ROLE_COUNT - 1); }
    int GetActiveRole() const { return activeRole; }
    SDL_Texture* GetRoleTexture(int index) const;
    // 角色选择页：把指定角色立绘按比例居中画进给定屏幕区域（贴图缺失时画占位色块）
    void DrawRolePreview(int index, const SDL_FRect& area);

    float GetCameraX() const { return cameraX; }
    float GetCameraY() const { return cameraY; }
};
