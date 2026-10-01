#pragma once
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <random>
#include <stack>
#include <string>
#include "MazeGenerator.hpp"

// 粒子属性
struct Particle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float lifetime = 0.0f;
    float maxLifetime = 0.0f;
    bool active = false;
};

class Engine {
private:
    enum GameState {
        MAIN_MENU,
        MODE_SELECT,
        HISTORY,
        CONTROLS,
        SETTINGS,
        PLAYING,
        PAUSED,
        EXIT_CONFIRM,
        GAMEOVER
    };

    bool isRunning;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* dungeonFloorTexture = nullptr;
    SDL_Texture* dungeonWallTexture = nullptr;
    SDL_Texture* playerTexture = nullptr;
    SDL_Texture* treasureTexture = nullptr;
    TTF_Font* uiFont = nullptr;
    bool ttfInitialized = false;
    bool imageInitialized = false;
    bool audioInitialized = false;
    Mix_Chunk* pickupSound = nullptr;
    Mix_Chunk* buttonHoverSound = nullptr;
    Mix_Chunk* clickSound = nullptr;
    Mix_Music* backgroundMusic = nullptr;
    std::string hoveredButtonLastFrame;
    std::string hoveredButtonThisFrame;
    int masterVolume = 100;
    int musicVolume = 25;
    int effectsVolume = 100;
    bool masterMuted = false;
    int draggingVolumeSlider = -1;
    GameState settingsReturnState = MAIN_MENU;
    bool exitToDesktop = false;

    uint32_t gameOverStartTime = 0;

    // 经典固定时间步长（Fixed Timestep）的时钟参数
    const float MS_PER_UPDATE = 16.6666f; // 固定逻辑更新间隔（约 60 FPS，单位毫秒）

    // 关卡网格化常量
    const int TILE_SIZE = 40;

    GameState currentState = MAIN_MENU;

    enum GameMode {
        TIME_ATTACK,
        MAZE_MODE
    };
    GameMode currentMode = TIME_ATTACK;

    //限时挑战属性
    float gameTimer = 30.0f;
    int score = 0;
    std::vector<int> scoreHistory;
    std::vector<float> timeHistory;
    bool historyLoaded = false;

    //玩家方块属性
    float playerX = 50.0f;
    float playerY = 50.0f;
    float playerSize = 36.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    const float ACCEL = 1500.0f;
    const float FRICTION = 8.0f;
    const float MAX_SPEED = 400.0f;

    //金币属性
    float coinX = 600.0f;
    float coinY = 150.0f;
    float coinSize = 20.0f;
    bool isCoinActive = true;

    // 粒子静态对象池
    std::vector<Particle> particlePool;
    const size_t MAX_PARTICLES = 200;
    std::random_device rd;
    std::mt19937 gen{rd()};

    //键盘按键状态
    bool upPressed = false;
    bool downPressed = false;
    bool leftPressed = false;
    bool rightPressed = false;
    bool isFullscreen = false;

    //绿色墙壁障碍物
    std::vector<SDL_Rect> walls;

    bool LoadLevelFromFile(const std::string& filename);
    void SaveScoreHistoryToFile();
    void SaveTimeHistoryToFile();
    void LoadScoreHistoryFromFile();
    void LoadTimeHistoryFromFile();
    void DrawFilledCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius);
    void EmitExplosion(float spawnX, float spawnY, int count);
    void HandleInput();
    void HandleMenuClick(int mouseX, int mouseY);
    void UpdateVolumeFromMouse(int mouseX);
    void UpdateAudioVolumes();
    void PlayClickSound();
    void StartGame(GameMode mode);
    void DrawText(const std::string& text, int x, int y, SDL_Color color, int pointSize = 24);
    void DrawButton(const SDL_Rect& rect, const std::string& label, bool hovered = false);
    void DrawMenuPage();
    void DrawHistoryPage();
    void DrawControlsPage();
    void DrawSettingsPage();
    void DrawPausePage();
    void DrawExitConfirmPage();
    void DrawDungeonBackground(Uint8 overlayAlpha);
    bool CheckCollision(float px, float py, float pSize, float wx, float wy, float wW, float wH);
    void resetCoinPosition();
    void Update(float dt);
    void Render();

public:
    Engine() : isRunning(false), window(nullptr), renderer(nullptr) {}
    bool Initialize(const char* title, int width, int height);
    void Run();
    void Clean();
    void ToggleFullscreen();
};