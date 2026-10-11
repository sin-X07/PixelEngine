#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>
#include <cstdint>

class InputManager {
private:
    bool upPressed = false;
    bool downPressed = false;
    bool leftPressed = false;
    bool rightPressed = false;

    Direction playerDir = DIR_DOWN;

    bool quitRequested = false;
    bool attackRequested = false;
    bool pauseToggleRequested = false;
    bool fullscreenToggleRequested = false;
    bool muteToggleRequested = false;
    bool enterOrSpaceRequested = false;
    bool digit1Requested = false;
    bool digit2Requested = false;

    // 鼠标状态（逻辑视口坐标）
    int mouseLogicalX = 0;
    int mouseLogicalY = 0;
    bool mouseLeftDown = false;
    bool mouseLeftClicked = false;
    bool mouseLeftReleased = false;

    // Windows 键盘布局接管
    std::uintptr_t originalKeyboardLayout = 0;
    bool keyboardLayoutCaptured = false;
    bool englishKeyboardLayoutActivated = false;

public:
    InputManager() = default;
    ~InputManager() { RestoreOriginalKeyboardLayout(); }

    void ProcessEvents(SDL_Renderer* renderer, GameState currentState);
    void ResetPerFrameActions();

    void SwitchToEnglishKeyboardLayout();
    void RestoreOriginalKeyboardLayout();

    // 状态查询
    bool IsUpPressed() const { return upPressed; }
    bool IsDownPressed() const { return downPressed; }
    bool IsLeftPressed() const { return leftPressed; }
    bool IsRightPressed() const { return rightPressed; }
    Direction GetPlayerDir() const { return playerDir; }

    bool IsQuitRequested() const { return quitRequested; }
    bool IsAttackRequested() const { return attackRequested; }
    bool IsPauseToggleRequested() const { return pauseToggleRequested; }
    bool IsFullscreenToggleRequested() const { return fullscreenToggleRequested; }
    bool IsMuteToggleRequested() const { return muteToggleRequested; }
    bool IsEnterOrSpaceRequested() const { return enterOrSpaceRequested; }
    bool IsDigit1Requested() const { return digit1Requested; }
    bool IsDigit2Requested() const { return digit2Requested; }

    int GetMouseX() const { return mouseLogicalX; }
    int GetMouseY() const { return mouseLogicalY; }
    bool IsMouseLeftDown() const { return mouseLeftDown; }
    bool IsMouseLeftClicked() const { return mouseLeftClicked; }
    bool IsMouseLeftReleased() const { return mouseLeftReleased; }

    void ResetMovementKeys() {
        upPressed = downPressed = leftPressed = rightPressed = false;
    }
};
