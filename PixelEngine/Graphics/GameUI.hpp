#pragma once
#include "../Core/Types.hpp"   // GameState, GameMode
#include <functional>
#include <string>

struct SaveManager;
class AudioManager;
class TextConfig;
class ImGuiUI;

// 游戏 UI 业务层：菜单界面 + 游戏内文字，全部通过 ImGui 渲染。
// 解耦纪律：不持有 Engine*；按钮动作通过 Actions 回调委托；只读数据通过 MenuCtx 传入；不直接播放音效。
class GameUI {
public:
    struct Actions {
        std::function<void()>              playClick;         // 点击音效
        std::function<void(GameState)>     goState;           // 通用状态切换
        std::function<void(GameState)>     openSettings;      // 进设置（Engine 记录 settingsReturnState）
        std::function<void(GameMode)>      startGame;         // 模式一/二、重新开始
        std::function<void()>              resumeGame;        // 暂停页"继续游戏"
        std::function<void(bool)>          confirmExit;       // 退出游戏(false)/退出桌面(true)
        std::function<void()>              quitApp;           // 确认框"老子不玩了"（按 exitToDesktop 分流）
        std::function<void()>              quitToDesktop;     // 主菜单"退出游戏"（直接退出程序）
        std::function<void()>              toggleMute;
        std::function<void(int)>           setMasterVolume;
        std::function<void(int)>           setMusicVolume;
        std::function<void(int)>           setEffectsVolume;
    };

    struct MenuCtx {
        GameState state = MAIN_MENU;
        GameState settingsReturnState = MAIN_MENU;
        GameMode  currentMode = TIME_ATTACK;   // 暂停页"重新开始"用
        bool      exitToDesktop = false;
        int       masterVolume = 0;
        int       musicVolume = 0;
        int       effectsVolume = 0;
        bool      muted = false;
        const SaveManager* saves = nullptr;
    };

    struct InGameCtx {
        GameState   state = PLAYING;
        GameMode    mode = TIME_ATTACK;
        float       gameTimer = 0.0f;
        int         score = 0;
        bool        diedByTrap = false;
        bool        diedByMonster = false;
        bool        hasNoWeaponHint = false;
        std::string hintText;
        std::string weaponName;
        int         ammo = 0;
        int         magazineSize = 0;
        bool        hasWeaponIcon = false;
    };

    void DrawMenu(const MenuCtx& ctx, const Actions& actions);
    void DrawInGame(const InGameCtx& ctx, class ImGuiUI* imGuiUI);

    // 文字样式配置注入（cfg==nullptr 时回退默认字面量）
    void SetTextConfig(TextConfig* cfg) { cfg_ = cfg; }
    void SetImGuiUI(ImGuiUI* ui) { imGuiUI_ = ui; }

private:
    TextConfig* cfg_ = nullptr;
    ImGuiUI* imGuiUI_ = nullptr;
};
