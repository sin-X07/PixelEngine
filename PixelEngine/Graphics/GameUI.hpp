#pragma once
#include "../Core/Types.hpp"
#include "../Storage/MatchHistory.hpp"
#include <array>
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
        std::function<void(int)>           startMatchOnMap;    // 指定地图开局（kMapCatalog 索引）
        std::function<void(int)>           chooseSide;         // 0=T, 1=CT（爆破模式）
        std::function<void()>              close3DFocus;      // 退出主菜单 3D 聚焦
        std::function<void(int,int)>       focus3DSelect;     // (category, option)
        std::function<void(int)>           selectMapChoice;    // 俯视地图浮层选择
        std::function<void(int)>           startMatchOnChoice; // 俯视地图浮层确认
        std::function<void()>              closeMapOverlay;    // 返回主菜单
        std::function<void(int)>           selectMap;          // 地图选择光标移动
        std::function<void(float,float,float,float)> setMapPreviewRect; // 当前选中地图预览图矩形
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
        std::function<void(bool)>   setCrosshairDynamic;
        // 雷达
        std::function<void(bool)>   setRadarPlayerCentered;
        std::function<void(bool)>   setRadarRotateWithPlayer;
        std::function<void(bool)>   setRadarBlendBg;
        std::function<void(int)>    setRadarBgAlpha;
        std::function<void(int)>    setRadarHudSize;
        std::function<void(float)>  setRadarMapZoom;
        // 显示
        std::function<void(int,int,int)> setResolution;
        std::function<void(int)>           setRenderScale;    // 内部渲染缩放(%)
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
        bool  crosshairDynamic   = true;

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
        int   renderScalePct = 100;
        bool  isFullscreen = false;

        // 主菜单背景
        void* backdropTex = nullptr;   // 主菜单背景图（SDL_Texture*）
        int   backdropW = 0, backdropH = 0;
        bool  backdrop3D = false;      // 实时 3D 背景：使用更轻的压暗

        // 3D 聚焦 + 右侧轮盘
        bool        focus3D = false;
        int         focusObject = 0;
        bool        focusChicken = false;
        std::string focusName;
        int         focusWeapon = 0;   // 0=步枪 1=手枪 2=匕首 3=空手
        int         focusColor = 0;    // 0=原色 1=金色 2=赤红 3=冰蓝
        int         focusPose = 0;     // 0=待机 1=警戒 2=挥手
        float       focusProgress = 1.0f;  // 聚焦放大 / 轮盘开屏进度
        bool        cameraTransition = false; // 主菜单到模式页的相机弧线转场
        bool        mapOverlayOpen = false;
        bool        mapOverlayClosing = false;
        float       mapOverlayFade = 0.0f;
        float       mapOverlayLightProgress = 1.0f;
        float       mapOverlayBeamFade = 1.0f;
        float       cameraBeamFade = 0.0f;
        std::string mapOverlayTitle;
        std::string mapOverlaySubtitle;
        int         mapOverlaySelected = 0;
        int         mapOverlayCount = 0;
        std::array<std::string, 8> mapOverlayNames;
        std::array<std::string, 8> mapOverlayTaglines;
        std::array<void*, 8> mapOverlayPreviews{};
    };

    struct InGameCtx {
        GameState   state = PLAYING;
        std::string weaponName;
        int         ammo = 0;
        int         magazineSize = 0;
        int         reserveAmmo = 0;      // 备弹（弹匣之外的存量）
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
        int   armor = 0;
        int   grenadeCount = 0;
        int   health = 100;
        int   maxHealth = 100;
        bool  helmet = false;
        bool  matchOver = false;
        bool  matchWinnerIsCT = false;
        float matchOverReturn = -1.0f;   // 距返回主菜单的秒数（<0 表示未知）
        std::string coachReason;          // 教练本回合计划（HUD 展示）
        bool  coachFlash = false;         // 计划刚变更 → 高亮

        // 目标物
        int   objective = 0;          // 0=无 1=爆破
        bool  bombPlanted = false;
        float bombTimer = 0.0f;
        float defuseProgress = 0.0f;

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

        // 屏幕中央提示（回合开始 / 胜负）
        std::string announcement;
        float announcementAlpha = 0.0f;
    
        // 观察者 / C4 提示
        bool   spectating     = false;
        int    spectateMode   = 0;      // 0 跟随, 1 自由
        std::string spectateTarget;     // 被观战 BOT 的昵称
        bool   hasC4          = false;
        bool   holdingC4      = false;   // C4 拿在手上（按 5）
        bool   bombDropped    = false;   // C4 掉在地上
        bool   bombIntel      = false;   // CT 已知掉落 C4 的位置
        float  plantProgress  = 0.0f;   // 0~1
};

    void DrawMenu(const MenuCtx& ctx, const Actions& actions);
    void DrawFocusWheel(const MenuCtx& ctx, const Actions& actions);
    void DrawMapOverlay(const MenuCtx& ctx, const Actions& actions);

    // 地图选择界面（爆破模式）
    struct MapSelectCtx {
        int  selected = 0;                    // 当前选中索引
        void* previewTex[8] = {nullptr};      // 预览图（SDL_Texture*）
    };
    void DrawMapSelect(const MapSelectCtx& ctx, const Actions& actions);

    // 历史记录界面
    struct HistoryCtx {
        const std::vector<MatchRecord>* records = nullptr;
    };
    void DrawHistory(const HistoryCtx& ctx, const Actions& actions);

    // 阵营选择弹窗（爆破模式，进入游戏后弹出）
    struct SideSelectCtx { std::string mapName; };
    // 旧版独立选阵营页；当前流程不再进入，仅保留兼容实现。
    void DrawSideSelect(const SideSelectCtx& ctx, const Actions& actions);
    // 进入游戏后的阵营选择弹窗（半透明遮罩 + 居中面板）
    void DrawSidePopup(const SideSelectCtx& ctx, const Actions& actions,
                       float alpha = 1.0f);
    void DrawInGame(const InGameCtx& ctx, ImGuiUI* imGuiUI);
    // 进入对局前的黑屏加载提示（地图 / LLM server）
    void DrawLoadingOverlay(const char* title, const char* subtitle);

    // ---- 买枪菜单 ----
    struct BuyItem {
        int         weaponId = 0;   // kind==0 时有效
        int         kind = 0;       // 0=武器 1=护甲
        int         armorTier = 0;  // kind==1 时有效: 1=护甲 2=护甲+头盔
        std::string name;
        int         price = 0;
        bool        affordable = false;
        bool        owned = false;
        int         group = 0;      // 0=主武器 1=副武器 2=近战 3=投掷物 4=护甲
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
