#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "InputManager.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

void InputManager::ResetPerFrameActions() {
    attackRequested = false;
    pauseToggleRequested = false;
    fullscreenToggleRequested = false;
    muteToggleRequested = false;
    enterOrSpaceRequested = false;
    digit1Requested = false;
    digit2Requested = false;
    digit3Requested = false;
    fRequested = false;
    tweakerToggleRequested = false;
    mouseLeftClicked = false;
    mouseLeftReleased = false;
}

void InputManager::ProcessEvents(SDL_Renderer* renderer, GameState currentState) {
    ResetPerFrameActions();

    // 更新鼠标逻辑坐标（物理→逻辑，供 UpdateAimAngle 等使用）
    int rawMouseX = 0;
    int rawMouseY = 0;
    SDL_GetMouseState(&rawMouseX, &rawMouseY);
    if (renderer) {
        float lx = 0.0f;
        float ly = 0.0f;
        SDL_RenderWindowToLogical(renderer, rawMouseX, rawMouseY, &lx, &ly);
        mouseLogicalX = static_cast<int>(lx);
        mouseLogicalY = static_cast<int>(ly);
    } else {
        mouseLogicalX = rawMouseX;
        mouseLogicalY = rawMouseY;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (eventCallback) eventCallback(event);

        if (event.type == SDL_QUIT) {
            quitRequested = true;
        }

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            mouseLeftDown = true;
            mouseLeftClicked = true;
        }
        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            mouseLeftDown = false;
            mouseLeftReleased = true;
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
                case SDLK_F11: fullscreenToggleRequested = true; break;
                case SDLK_F1: muteToggleRequested = true; break;
                case SDLK_F2: tweakerToggleRequested = true; break;
                case SDLK_ESCAPE: pauseToggleRequested = true; break;
                case SDLK_1: case SDLK_KP_1: digit1Requested = true; break;
                case SDLK_2: case SDLK_KP_2: digit2Requested = true; break;
                case SDLK_3: case SDLK_KP_3: digit3Requested = true; break;
                case SDLK_f: fRequested = true; break;
                case SDLK_RETURN: case SDLK_SPACE: enterOrSpaceRequested = true; break;

                case SDLK_UP:    if (currentState == PLAYING) { upPressed = true; playerDir = DIR_UP; } break;
                case SDLK_DOWN:  if (currentState == PLAYING) { downPressed = true; playerDir = DIR_DOWN; } break;
                case SDLK_LEFT:  if (currentState == PLAYING) { leftPressed = true; playerDir = DIR_LEFT; } break;
                case SDLK_RIGHT: if (currentState == PLAYING) { rightPressed = true; playerDir = DIR_RIGHT; } break;
            }

            switch (event.key.keysym.scancode) {
                case SDL_SCANCODE_W: if (currentState == PLAYING) { upPressed = true; playerDir = DIR_UP; } break;
                case SDL_SCANCODE_S: if (currentState == PLAYING) { downPressed = true; playerDir = DIR_DOWN; } break;
                case SDL_SCANCODE_A: if (currentState == PLAYING) { leftPressed = true; playerDir = DIR_LEFT; } break;
                case SDL_SCANCODE_D: if (currentState == PLAYING) { rightPressed = true; playerDir = DIR_RIGHT; } break;
                case SDL_SCANCODE_SPACE: if (currentState == PLAYING) { attackRequested = true; } break;
                default: break;
            }
        }

        if (event.type == SDL_KEYUP) {
            switch (event.key.keysym.sym) {
                case SDLK_UP:    upPressed = false; break;
                case SDLK_DOWN:  downPressed = false; break;
                case SDLK_LEFT:  leftPressed = false; break;
                case SDLK_RIGHT: rightPressed = false; break;
                default: break;
            }

            switch (event.key.keysym.scancode) {
                case SDL_SCANCODE_W: upPressed = false; break;
                case SDL_SCANCODE_S: downPressed = false; break;
                case SDL_SCANCODE_A: leftPressed = false; break;
                case SDL_SCANCODE_D: rightPressed = false; break;
                default: break;
            }

            if (currentState == PLAYING) {
                if (upPressed) playerDir = DIR_UP;
                else if (downPressed) playerDir = DIR_DOWN;
                else if (leftPressed) playerDir = DIR_LEFT;
                else if (rightPressed) playerDir = DIR_RIGHT;
            }
        }
    }
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
#endif
}
