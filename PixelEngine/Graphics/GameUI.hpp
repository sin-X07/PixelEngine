#pragma once
#include "../Core/Types.hpp"
#include <functional>
#include <string>
#include <vector>

class ImGuiUI;

// ============================================================================
// GameUI — ImGui 业务层（菜单 + 游戏内 HUD 文字）
//
// 解耦纪律：不持有 Engine*；动作通过 Actions 回调委托；只读数据通过 Ctx 传入。
// ============================================================================
class GameUI {
public:
    struct Actions {
        std::function<void()>              playClick;         // 点击音效
        std::function<void(GameState)>     goState;           // 状态切换
        std::function<void(GameState)>     openSettings;      // 进设置（记录返回态）
        std::function<void()>              startMatch;        // 重新开始（沿用上一模式）
        std::function<void(int)>           startMatchMode;    // 按模式开始 (MatchMode 枚举值)
        std::function<void()>              resumeGame;        // 暂停页继续
        std::function<void(bool)>          confirmExit;       // 退出游戏(false) / 退出桌面(true)
        std::function<void()>              quitApp;           // 确认框"退出"
        std::function<void()>              quitToDesktop;     // 主菜单退出程序
        std::function<void()>              toggleMute;
        std::function<void(int)>           setMasterVolume;
        std::function<void(int)>           setMusicVolume;
        std::function<void(int)>           setEffectsVolume;
        std::function<void(float)>         setMouseSensitivity;
        std::function<void(int)>           setTargetFPS;
        // 准星
        std::function<void(Uint8,Uint8,Uint8,Uint8)> setCrosshairColor;
        std::function<void(float)>  setCrosshairThickness;
        std::function<void(bool)>   setCrosshairCenterDot;
        std::function<void(float)>  setCrosshairLength;
        std::function<void(float)>  setCrosshairGap;
        // 雷达
        std::function<void(bool)>   setRadarPlayerCentered;
        std::function<void(bool)>   setRadarRotateWithPlayer;
        std::function<void(bool)>   setRadarBlendBg;
        std::function<void(int)>    setRadarBgAlpha;
        std::function<void(int)>    setRadarHudSize;
        std::function<void(float)>  setRadarMapZoom;
        // 显示
        std::function<void(int,int,int)> setResolution;
        std::function<void(bool)>        setFullscreen;
    };

    struct MenuCtx {
        GameState state = MAIN_MENU;
        GameState settingsReturnState = MAIN_MENU;
        bool      exitToDesktop = false;

        int       masterVolume = 0;
        int       musicVolume = 0;
        int       effectsVolume = 0;
        bool      muted = false;
        float     mouseSensitivity = 1.0f;
        int       targetFPS = 60;

        // 准星
        Uint8 crosshairR = 255, crosshairG = 255, crosshairB = 255, crosshairA = 200;
        float crosshairThickness = 1.5f;
        bool  crosshairCenterDot = true;
        float crosshairLength    = 12.0f;
        float crosshairGap       = 3.0f;

        // 雷达
        bool  radarPlayerCentered   = true;
        bool  radarRotateWithPlayer = false;
        bool  radarBlendBg          = false;
        int   radarBgAlpha          = 160;
        int   radarHudSize          = 120;
        float radarMapZoom          = 0.005f;

        // 显示
        int   resW = 800, resH = 600;
        int   resIdx = 0;
        bool  isFullscreen = false;

        // 玩家累计战绩（主菜单展示）
        int   statKills = 0;
        int   statDeaths = 0;
    };

    struct InGameCtx {
        GameState   state = PLAYING;
        std::string weaponName;
        int         ammo = 0;
        int         magazineSize = 0;
        bool        hasNoWeaponHint = false;
        std::string hintText;
        int         totalPlayers = 0;
        int         kills = 0;
        int         deaths = 0;

        // 战局
        int   scoreT = 0;
        int   scoreCT = 0;
        int   roundNumber = 0;
        int   roundRemainingSec = 0;
        int   aliveT = 0;
        int   aliveCT = 0;
        int   money = 0;
        bool  matchOver = false;
        bool  matchWinnerIsCT = false;

        // 目标物
        int   objective = 0;          // 0=无 1=爆破 2=人质
        bool  bombPlanted = false;
        float bombTimer = 0.0f;
        float defuseProgress = 0.0f;
        int   hostagesRescued = 0;
        int   hostagesTotal = 0;
        float rescueProgress = 0.0f;

        // 击杀播报 / 记分板
        std::vector<std::string> killFeed;
        struct ScoreRowView {
            std::string name;
            int  team = 0;
            int  kills = 0;
            int  deaths = 0;
            int  money = 0;
            bool alive = true;
            bool isPlayer = false;
        };
        std::vector<ScoreRowView> scoreRows;
        bool showScoreboard = false;
    };

    void DrawMenu(const MenuCtx& ctx, const Actions& actions);
    void DrawInGame(const InGameCtx& ctx, ImGuiUI* imGuiUI);

    // ---- 买枪菜单 ----
    struct BuyItem {
        int         weaponId = 0;
        std::string name;
        int         price = 0;
        bool        affordable = false;
        bool        owned = false;
        int         group = 0;      // 0=主武器 1=副武器 2=近战 3=投掷物
    };
    struct BuyMenuCtx {
        std::vector<BuyItem> items;
        int         money = 0;
        bool        canBuy = false;   // 是否在购买期 + 购买区内
        std::string hint;
    };
    void DrawBuyMenu(const BuyMenuCtx& ctx, const std::function<void(int)>& onBuy);

    void SetImGuiUI(ImGuiUI* ui) { imGuiUI_ = ui; }

private:
    ImGuiUI* imGuiUI_ = nullptr;
};
