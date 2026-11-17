#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "InputManager.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include "../Core/PathUtils.hpp"
#include "../Core/BuildInfo.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::filesystem::path LayoutMarkerPath() {
    return std::filesystem::u8path(PathUtils::ResolveWritablePath(
        std::string("cs2d_keyboard_layout") + std::string(BuildInfo::SaveSuffix()) + ".txt"));
}
}

InputManager::InputManager() {
#ifdef _WIN32
    std::ifstream marker(LayoutMarkerPath());
    std::string original;
    if (!(marker >> original) || original.size() != 8) return;
    wchar_t current[KL_NAMELENGTH]{};
    if (GetKeyboardLayoutNameW(current) && std::wstring(current) == L"00000409") {
        const std::wstring layout(original.begin(), original.end());
        if (!LoadKeyboardLayoutW(layout.c_str(), KLF_ACTIVATE)) return;
    }
    marker.close();
    std::error_code error;
    std::filesystem::remove(LayoutMarkerPath(), error);
#endif
}

void InputManager::ResetPerFrameActions() {
    pressed_.fill(false);
    released_.fill(false);
    helpRequested = false;
}

void InputManager::ProcessEvents(SDL_Renderer* renderer) {
    ResetPerFrameActions();
    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    if (renderer) {
        float lx = 0, ly = 0;
        SDL_RenderWindowToLogical(renderer, x, y, &lx, &ly);
        mouseLogicalX = static_cast<int>(lx);
        mouseLogicalY = static_cast<int>(ly);
    } else { mouseLogicalX = x; mouseLogicalY = y; }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (eventCallback) eventCallback(event);
        if (event.type == SDL_QUIT) quitRequested = true;
        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) windowFocused = true;
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                windowFocused = false;
                held_.fill(false); pressed_.fill(false); released_.fill(false);
                CancelCapture();
            }
        }
        InputBinding input;
        bool down = false;
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            if (event.key.repeat) continue;
            input = InputBinding(event.key.keysym.scancode);
            down = event.type == SDL_KEYDOWN;
        } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            input = InputBinding::Mouse(event.button.button);
            down = event.type == SDL_MOUSEBUTTONDOWN;
        } else continue;
        if (!input.Valid() || input.device == InputBinding::Device::None) continue;
        if (captureKey || conflictAction >= 0) {
            if (down && (captureKey || input == InputBinding(SDL_SCANCODE_ESCAPE))) capturedInput = input;
            held_.fill(false);
            continue;
        }
        // Widget clicks belong to UI. Other mouse buttons still support global
        // actions such as returning from a menu or toggling fullscreen.
        const bool uiMouse = input.device == InputBinding::Device::Mouse &&
            input.code == SDL_BUTTON_LEFT && !gameplayContext && uiConsumesMouse;
        for (int i = 0; i < GameActionCount; ++i) {
            if (bindings.keys[i] != input) continue;
            if (!down) { released_[i] = held_[i]; held_[i] = false; }
            else if (!uiMouse) { pressed_[i] = !held_[i]; held_[i] = true; }
        }
    }
    helpRequested = WasPressed(GameAction::Help);
}

void InputManager::SwitchToEnglishKeyboardLayout() {
#ifdef _WIN32
    if (englishKeyboardLayoutActivated) return;

    if (!keyboardLayoutCaptured) {
        const HKL currentLayout = GetKeyboardLayout(0);
        if (!currentLayout) {
            std::cerr << "无法读取当前键盘布局，继续使用现有输入布局。" << std::endl;
            return;
        }
        originalKeyboardLayout = reinterpret_cast<std::uintptr_t>(currentLayout);
        keyboardLayoutCaptured = true;
    }

    wchar_t originalName[KL_NAMELENGTH]{};
    if (!GetKeyboardLayoutNameW(originalName)) return;
    const std::wstring layoutName(originalName);
    std::ofstream marker(LayoutMarkerPath(), std::ios::trunc);
    marker << std::string(layoutName.begin(), layoutName.end());
    marker.close();
    if (!marker) return;
    if (!LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE)) {
        std::cerr << "无法激活英文（美国）键盘布局，继续使用现有输入布局。错误码: "
                  << GetLastError() << std::endl;
        return;
    }

    englishKeyboardLayoutActivated = true;
#endif
}

void InputManager::RestoreOriginalKeyboardLayout() {
#ifdef _WIN32
    if (!keyboardLayoutCaptured || !englishKeyboardLayoutActivated) return;

    const HKL originalLayout = reinterpret_cast<HKL>(originalKeyboardLayout);
    if (!ActivateKeyboardLayout(originalLayout, 0)) {
        std::cerr << "无法恢复原键盘布局。错误码: " << GetLastError() << std::endl;
        return;
    }

    englishKeyboardLayoutActivated = false;
    std::error_code error;
    std::filesystem::remove(LayoutMarkerPath(), error);
#endif
}
