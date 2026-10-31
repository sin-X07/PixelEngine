#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>
#include <cstdint>
#include <functional>

// ============================================================================
// InputManager — SDL 事件收集与查询
//
// 只保留 CS2D 需要的输入；移动键位由 SDL_GetKeyboardState 直接查询，
// 本类负责：退出/全屏/静音/暂停、数字键切武器、鼠标状态、ImGui 事件转发。
// ============================================================================

class InputManager {
private:
    bool quitRequested = false;
    bool pauseToggleRequested = false;
    bool fullscreenToggleRequested = false;
    bool muteToggleRequested = false;
    bool digit1Requested = false;
    bool digit2Requested = false;
    bool digit3Requested = false;
    bool digit4Requested = false;
    bool buyMenuToggleRequested = false;

    // 鼠标状态（逻辑视口坐标）
    int mouseLogicalX = 0;
    int mouseLogicalY = 0;
    bool mouseLeftDown = false;
    bool mouseLeftClicked = false;
    bool mouseLeftReleased = false;
    // 右键：down 边沿，只活一帧（开镜切换用）
    bool mouseRightDown = false;
    bool mouseRightClicked = false;

    // 窗口焦点：SDL_WINDOWEVENT_FOCUS_GAINED/LOST 更新
    bool windowFocused = true;

    // 每帧原始事件回调（供 ImGui 等 UI 后端消费）
    std::function<void(const SDL_Event&)> eventCallback;

    // Windows 键盘布局接管
    std::uintptr_t originalKeyboardLayout = 0;
    bool keyboardLayoutCaptured = false;
    bool englishKeyboardLayoutActivated = false;

public:
    InputManager() = default;
    ~InputManager() { RestoreOriginalKeyboardLayout(); }

    void ProcessEvents(SDL_Renderer* renderer);
    void ResetPerFrameActions();

    // 设置原始事件回调（每 poll 到一个 event 时调用一次）
    void SetEventCallback(std::function<void(const SDL_Event&)> cb) { eventCallback = std::move(cb); }

    void SwitchToEnglishKeyboardLayout();
    void RestoreOriginalKeyboardLayout();

    // 状态查询
    bool IsQuitRequested() const { return quitRequested; }
    bool IsPauseToggleRequested() const { return pauseToggleRequested; }
    bool IsFullscreenToggleRequested() const { return fullscreenToggleRequested; }
    bool IsMuteToggleRequested() const { return muteToggleRequested; }
    bool IsDigit1Requested() const { return digit1Requested; }
    bool IsDigit2Requested() const { return digit2Requested; }
    bool IsDigit3Requested() const { return digit3Requested; }
    bool IsDigit4Requested() const { return digit4Requested; }
    bool IsBuyMenuToggleRequested() const { return buyMenuToggleRequested; }

    int GetMouseX() const { return mouseLogicalX; }
    int GetMouseY() const { return mouseLogicalY; }
    bool IsMouseLeftDown() const { return mouseLeftDown; }
    bool IsMouseLeftClicked() const { return mouseLeftClicked; }
    bool IsMouseLeftReleased() const { return mouseLeftReleased; }
    // 契约 v1 第 4 节：右键 down 边沿（只活一帧）与窗口焦点
    bool IsMouseRightDown() const { return mouseRightDown; }
    bool IsMouseRightClicked() const { return mouseRightClicked; }
    bool HasWindowFocus() const { return windowFocused; }
};
