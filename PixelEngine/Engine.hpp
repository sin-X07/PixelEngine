#pragma once
#include "Core/Types.hpp"
#include "Audio/AudioManager.hpp"
#include "Input/InputManager.hpp"
#include "World/WorldMap.hpp"
#include "World/PhysicsSystem.hpp"
#include "World/Monster.hpp"
#include "World/Weapon.hpp"
#include "World/Player.hpp"
#include "World/CombatSystem.hpp"
#include "World/EnemyManager.hpp"
#include "Graphics/ParticleSystem.hpp"
#include "Graphics/RenderSystem.hpp"
#include "Graphics/UIRenderer.hpp"
#include "Graphics/ImGuiUI.hpp"
#include "Graphics/GameUI.hpp"
#include "Graphics/TextConfig.hpp"
#include "Graphics/TextTweakerUI.hpp"
#include "Storage/SaveManager.hpp"
#include "MazeGenerator.hpp"
#include "CS2D/CS2DRenderer.hpp"
#include "CS2D/CS2DGame.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <random>
#include <vector>
#include <string>

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
    std::unique_ptr<ImGuiUI> imGuiUI;
    std::unique_ptr<GameUI> gameUI;
    std::unique_ptr<TextConfig> textConfig;
    std::unique_ptr<TextTweakerUI> textTweaker;
    std::unique_ptr<SaveManager> saveManager;
    std::unique_ptr<CombatSystem> combatSystem;
    std::unique_ptr<EnemyManager> enemyManager;

    // CS2D 模式子系统（完全独立于模式一/二）
    std::unique_ptr<CS2DRenderer> cs2dRenderer;
    std::unique_ptr<CS2DGame>     cs2dGame;

    // CS2D 模式输入状态（HandleInput 采集 → Update 消费）
    bool cs2dMouseDown = false;
    float cs2dWorldMouseX = 0.0f;
    float cs2dWorldMouseY = 0.0f;
    bool cs2dKeyW = false, cs2dKeyA = false, cs2dKeyS = false, cs2dKeyD = false;

    // 核心时钟与状态机
    const float MS_PER_UPDATE = 16.6666f; // 固定逻辑更新间隔（约 60 FPS，单位毫秒）
    GameState currentState = MAIN_MENU;
    GameState settingsReturnState = MAIN_MENU;
    GameMode currentMode = TIME_ATTACK;
    bool exitToDesktop = false;
    uint32_t gameOverStartTime = 0;
    bool tweakerOpen = false;

    // 游戏性数据
    float gameTimer = TIME_ATTACK_DURATION;
    int score = 0;
    int selectedRole = 0; // 当前选中角色（索引对应 Core/Roles.hpp 的 GetRoles()）

    // 玩家实体（位置/速度/生命/受伤/无敌/回血）
    Player player;

    std::random_device rd;
    std::mt19937 gen{rd()};

    void HandleInput();
    void StartGame(GameMode mode);
    void SelectRole(int index); // 更新选中角色并同步渲染/存档
    GameUI::Actions BuildActions();

    // 战斗与武器（已迁入 CombatSystem；DamagePlayer 保留为受击/死亡效果编排）
    void DamagePlayer(int amount);

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
