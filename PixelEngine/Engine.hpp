#pragma once
#include "Core/Types.hpp"
#include "Audio/AudioManager.hpp"
#include "Input/InputManager.hpp"
#include "World/WorldMap.hpp"
#include "World/PhysicsSystem.hpp"
#include "Graphics/ParticleSystem.hpp"
#include "Graphics/RenderSystem.hpp"
#include "Graphics/UIRenderer.hpp"
#include "Storage/SaveManager.hpp"
#include "MazeGenerator.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <random>

class Engine {
private:
    bool isRunning = false;
    bool cleaned = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool isFullscreen = false;

    // 子系统大类
    std::unique_ptr<AudioManager> audioManager;
    std::unique_ptr<InputManager> inputManager;
    std::unique_ptr<WorldMap> worldMap;
    std::unique_ptr<PhysicsSystem> physicsSystem;
    std::unique_ptr<ParticleSystem> particleSystem;
    std::unique_ptr<RenderSystem> renderSystem;
    std::unique_ptr<UIRenderer> uiRenderer;
    std::unique_ptr<SaveManager> saveManager;

    // 核心时钟与状态机
    const float MS_PER_UPDATE = 16.6666f; // 固定逻辑更新间隔（约 60 FPS，单位毫秒）
    GameState currentState = MAIN_MENU;
    GameState settingsReturnState = MAIN_MENU;
    GameMode currentMode = TIME_ATTACK;
    bool exitToDesktop = false;
    int draggingVolumeSlider = -1;
    uint32_t gameOverStartTime = 0;

    // 游戏性数据
    float gameTimer = 30.0f;
    int score = 0;
    bool diedByTrap = false;

    // 玩家物理属性
    float playerX = 50.0f;
    float playerY = 50.0f;
    float playerSize = 12.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    const float ACCEL = 500.0f;
    const float FRICTION = 8.0f;
    const float MAX_SPEED = 140.0f;

    // 战斗系统
    bool isAttacking = false;
    float attackTimer = 0.0f;
    const float ATTACK_DURATION = 0.18f;
    const float ATTACK_COOLDOWN = 0.28f;
    float attackCooldownTimer = 0.0f;
    Direction attackDir = DIR_DOWN;
    bool attackHitRegistered = false;

    std::random_device rd;
    std::mt19937 gen{rd()};

    void HandleInput();
    void HandleMenuClick(int mouseX, int mouseY);
    void UpdateVolumeFromMouse(int mouseX);
    void PerformAttack();
    void StartGame(GameMode mode);

    void Update(float dt);
    void Render();

public:
    Engine();
    ~Engine();

    bool Initialize(const char* title, int width, int height);
    void Run();
    void Clean();
    void ToggleFullscreen();
};
