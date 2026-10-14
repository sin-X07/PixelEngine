#pragma once
#include "../Core/Types.hpp"
#include "../Storage/SaveManager.hpp"
#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <string>

class UIRenderer {
private:
    SDL_Renderer* renderer = nullptr;
    TTF_Font* uiFont = nullptr;
    bool ttfInitialized = false;

    std::string hoveredButtonLastFrame;
    std::string hoveredButtonThisFrame;

public:
    UIRenderer() = default;
    ~UIRenderer() { Clean(); }

    bool Initialize(SDL_Renderer* ren);
    void Clean();

    void BeginFrame();
    bool DidHoverChange() const;

    void DrawText(const std::string& text, int x, int y, SDL_Color color, int pointSize = 24);
    void DrawButton(const SDL_Rect& rect, const std::string& label, bool hovered = false);

    void DrawMenuPage(GameState currentState, int mouseX, int mouseY);
    void DrawHistoryPage(const SaveManager& saveManager, int mouseX, int mouseY);
    void DrawControlsPage(int mouseX, int mouseY);
    void DrawSettingsPage(int masterVolume, int musicVolume, int effectsVolume, bool masterMuted, int mouseX, int mouseY);
    void DrawPausePage(int mouseX, int mouseY);
    void DrawExitConfirmPage(int mouseX, int mouseY);
    void DrawGameOver(GameMode currentMode, bool diedByTrap, bool diedByMonster, int score, float gameTimer);
    void DrawHUD(GameMode currentMode, float gameTimer, int score, int health, float maxTime);

    // 右下角武器状态栏：只显示当前武器；枪械显示当前弹夹内子弹数（magazineSize > 0 时）
    void DrawWeaponStatus(SDL_Texture* icon, const std::string& name, int ammo, int magazineSize);
    // 页面下方正中间的未装配提示词
    void DrawNoWeaponHint(const std::string& msg);
};
