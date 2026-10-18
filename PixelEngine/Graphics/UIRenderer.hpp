#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>

class UIRenderer {
private:
    SDL_Renderer* renderer = nullptr;
    int viewW_ = 800;
    int viewH_ = 600;

public:
    UIRenderer() = default;
    ~UIRenderer() { Clean(); }

    bool Initialize(SDL_Renderer* ren);
    void SetViewportSize(int w, int h) { viewW_ = w; viewH_ = h; }
    void Clean();

    // 游戏内图形（非文字）：HUD 红心/时间条/金币点、武器面板+图标、结算面板背景
    void DrawGameOver(GameMode currentMode, bool diedByTrap, bool diedByMonster, int score, float gameTimer);
    void DrawHUD(GameMode currentMode, float gameTimer, int score, int health, float maxTime);
    void DrawWeaponStatus(SDL_Texture* icon);
};
