#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <string>

enum class GameAction {
    Forward, Left, Back, Right, Stealth, Reload, Interact, Drop, Buy,
    Slot1, Slot2, Slot3, Slot4, C4, Scoreboard, Spectate, Pause, Fullscreen, Mute,
    Fire, Scope, Help, QuickSwitch, TeammateXRay, Count
};
constexpr int GameActionCount = static_cast<int>(GameAction::Count);
inline const std::array<const char*, GameActionCount> ActionNames = {
    "前进", "左移", "后退", "右移", "潜行", "换弹", "互动", "丢弃", "购买",
    "主武器", "副武器", "近战", "投掷物", "C4", "记分板", "切换观战", "暂停 / 返回", "全屏", "静音",
    "开火", "开镜", "战斗按键提示", "快速切换武器", "队友透视（X 光）"
};
inline const std::array<const char*, GameActionCount> ActionIds = {
    "forward", "left", "back", "right", "stealth", "reload", "interact", "drop", "buy",
    "slot1", "slot2", "slot3", "slot4", "c4", "scoreboard", "spectate", "pause", "fullscreen", "mute",
    "fire", "scope", "help", "quick_switch", "teammate_xray"
};
constexpr SDL_Scancode DefaultTeammateXRayKey = SDL_SCANCODE_X;
struct InputBinding {
    enum class Device { None, Keyboard, Mouse };
    Device device = Device::None;
    int code = 0;
    InputBinding() = default;
    InputBinding(SDL_Scancode key) : device(key == SDL_SCANCODE_UNKNOWN ? Device::None : Device::Keyboard), code(key) {}
    static InputBinding Mouse(int button) { InputBinding result; result.device = Device::Mouse; result.code = button; return result; }
    bool operator==(const InputBinding& other) const { return device == other.device && code == other.code; }
    bool operator!=(const InputBinding& other) const { return !(*this == other); }
    bool Valid() const {
        return (device == Device::None && code == 0) ||
            (device == Device::Keyboard && code > 0 && code < SDL_NUM_SCANCODES) ||
            (device == Device::Mouse && code >= SDL_BUTTON_LEFT && code <= SDL_BUTTON_X2);
    }
    std::string Label() const {
        if (device == Device::Keyboard) return SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
        if (device == Device::Mouse) {
            const char* labels[] = { "", "鼠标左键", "鼠标中键", "鼠标右键", "鼠标侧键 1", "鼠标侧键 2" };
            if (code >= 1 && code <= 5) return labels[code];
        }
        return "未绑定";
    }
};
struct KeyBindings {
    std::array<InputBinding, GameActionCount> keys = {
        SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D,
        SDL_SCANCODE_LSHIFT, SDL_SCANCODE_R, SDL_SCANCODE_E, SDL_SCANCODE_G, SDL_SCANCODE_B,
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
        SDL_SCANCODE_TAB, SDL_SCANCODE_SPACE, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F11, SDL_SCANCODE_M,
        InputBinding::Mouse(SDL_BUTTON_LEFT), InputBinding::Mouse(SDL_BUTTON_RIGHT), SDL_SCANCODE_F1, SDL_SCANCODE_Q,
        DefaultTeammateXRayKey
    };
    InputBinding operator[](GameAction action) const { return keys[static_cast<int>(action)]; }
    int Conflict(InputBinding input, int except) const {
        if (input.device == InputBinding::Device::None) return -1;
        for (int i = 0; i < GameActionCount; ++i) if (i != except && keys[i] == input) return i;
        return -1;
    }
};
