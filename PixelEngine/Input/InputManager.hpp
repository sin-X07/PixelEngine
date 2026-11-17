#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>
#include <cstdint>
#include "KeyBindings.hpp"
#include <functional>

// ============================================================================
// InputManager — SDL 事件收集与查询
//
// 键盘与鼠标统一映射为动作，集中维护按住、按下和松开状态。
// 原始 SDL 事件同时转发给界面后端。
// ============================================================================

class InputManager {
private:
    bool quitRequested = false;
    std::array<bool, GameActionCount> held_{};
    std::array<bool, GameActionCount> pressed_{};
    std::array<bool, GameActionCount> released_{};
    // 鼠标状态（逻辑视口坐标）
    int mouseLogicalX = 0;
    int mouseLogicalY = 0;
    // 窗口焦点：SDL_WINDOWEVENT_FOCUS_GAINED/LOST 更新
    bool windowFocused = true;

    // 每帧原始事件回调（供 ImGui 等 UI 后端消费）
    std::function<void(const SDL_Event&)> eventCallback;

    // Windows 键盘布局接管
    std::uintptr_t originalKeyboardLayout = 0;
    bool keyboardLayoutCaptured = false;
    bool englishKeyboardLayoutActivated = false;

public:
    KeyBindings bindings;
    int captureAction = -1;
    int conflictAction = -1;
    bool captureKey = false;
    InputBinding capturedInput;
    InputBinding pendingInput;
    bool gameplayContext = false;
    bool uiConsumesMouse = true;
    bool helpRequested = false;
    InputManager();
    ~InputManager() { RestoreOriginalKeyboardLayout(); }

    void SetBindings(const KeyBindings& value) {
        if (bindings.keys != value.keys) { held_.fill(false); pressed_.fill(false); released_.fill(false); }
        bindings = value;
    }
    bool IsDown(GameAction action) const { return held_[static_cast<int>(action)]; }
    bool WasPressed(GameAction action) const { return pressed_[static_cast<int>(action)]; }
    bool WasReleased(GameAction action) const { return released_[static_cast<int>(action)]; }
    void CancelCapture() { captureKey = false; captureAction = conflictAction = -1; capturedInput = {}; pendingInput = {}; }
    void ProcessEvents(SDL_Renderer* renderer);
    void ResetPerFrameActions();

    // 设置原始事件回调（每 poll 到一个 event 时调用一次）
    void SetEventCallback(std::function<void(const SDL_Event&)> cb) { eventCallback = std::move(cb); }

    void SwitchToEnglishKeyboardLayout();
    void RestoreOriginalKeyboardLayout();

    // 状态查询
    bool IsQuitRequested() const { return quitRequested; }
    bool IsPauseToggleRequested() const { return WasPressed(GameAction::Pause); }
    bool IsFullscreenToggleRequested() const { return WasPressed(GameAction::Fullscreen); }
    bool IsMuteToggleRequested() const { return WasPressed(GameAction::Mute); }
    bool IsDigit1Requested() const { return WasPressed(GameAction::Slot1); }
    bool IsDigit2Requested() const { return WasPressed(GameAction::Slot2); }
    bool IsDigit3Requested() const { return WasPressed(GameAction::Slot3); }
    bool IsDigit4Requested() const { return WasPressed(GameAction::Slot4); }
    bool IsBuyMenuToggleRequested() const { return WasPressed(GameAction::Buy); }

    int GetMouseX() const { return mouseLogicalX; }
    int GetMouseY() const { return mouseLogicalY; }
    bool IsMouseLeftDown() const { return IsDown(GameAction::Fire); }
    bool IsMouseLeftClicked() const { return WasPressed(GameAction::Fire); }
    bool IsMouseLeftReleased() const { return WasReleased(GameAction::Fire); }
    // 契约 v1 第 4 节：右键 down 边沿（只活一帧）与窗口焦点
    bool IsMouseRightDown() const { return IsDown(GameAction::Scope); }
    bool IsMouseRightClicked() const { return WasPressed(GameAction::Scope); }
    bool HasWindowFocus() const { return windowFocused; }
};
