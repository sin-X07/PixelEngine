#pragma once
#include "Core/Types.hpp"
#include "Audio/AudioManager.hpp"
#include "Input/InputManager.hpp"
#include "Graphics/ImGuiUI.hpp"
#include "Graphics/GameUI.hpp"
#include "Storage/SaveManager.hpp"
#include "CS2D/CS2DRenderer.hpp"
#include "CS2D/CS2DGame.hpp"
#include "CS2D/CS2DOverlay.hpp"
#include "LLM/LLMBotController.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include "Storage/MatchHistory.hpp"

// ============================================================================
// Engine — 应用外壳
//
// 职责：窗口/渲染器生命周期、固定步长主循环、输入采集、状态机、菜单与设置。
// 对局逻辑全部在 CS2DGame 内，本类只做接线。
// ============================================================================
class Engine {
private:
    bool isRunning = false;
    bool cleaned = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool isFullscreen = false;
    int  logicalW_ = 800;
    int  logicalH_ = 600;

    // 子系统
    std::unique_ptr<AudioManager> audioManager;
    std::unique_ptr<InputManager> inputManager;
    std::unique_ptr<ImGuiUI>      imGuiUI;
    std::unique_ptr<GameUI>       gameUI;
    std::unique_ptr<SaveManager>  saveManager;

    // CS2D 对局
    std::unique_ptr<CS2DRenderer> cs2dRenderer;
    std::unique_ptr<CS2DGame>     cs2dGame;
    CS2DOverlay                   cs2dOverlay;
    // LLM 客户端：只服务于教练层（战术计划），不驱动 BOT 行动
    std::unique_ptr<LLMBotController> llmController_;
    bool coachLlmEnabled_ = true;   // --coach=rule 时关闭（用于对比规则教练）

    // 输入状态（HandleInput 采集 → Update 消费）
    bool  cs2dMouseDown = false;
    float cs2dWorldMouseX = 0.0f;
    float cs2dWorldMouseY = 0.0f;
    bool  cs2dKeyW = false, cs2dKeyA = false, cs2dKeyS = false, cs2dKeyD = false;
    bool  cs2dKeyShift = false, cs2dKeyR = false;
    bool  buyMenuOpen_ = false;   // 买枪菜单开关
    bool  showScoreboard_ = false; // Tab 按住显示记分板
    MatchMode currentMatchMode_ = MatchMode::TeamElimination;   // 暂停页"重新开始"用

    // 买枪菜单渲染
    void RenderBuyMenu();

    // 状态机与时钟
    const float MS_PER_UPDATE = 16.6666f;
    GameState currentState = MAIN_MENU;
    GameState settingsReturnState = MAIN_MENU;
    bool exitToDesktop = false;
    int  targetFPS_ = 60;

    // 分辨率
    int currentResW_ = 800, currentResH_ = 600, currentResIdx_ = 0;

    // ---- 页面转场 ----
    // 每帧先渲染到离屏纹理，再把上一帧与新页面合成到窗口；
    // 这样切页时可以直接拿到旧页面快照，不需要每帧读像素。
    enum class TransitionKind { None, Fade, Slide, MapZoom, GoldWipe, PauseFreeze };
    SDL_Texture* sceneTex_[2] = { nullptr, nullptr };
    int          sceneWrite_ = 0;
    int          sceneW_ = 0, sceneH_ = 0;
    bool         sceneValid_ = false;
    GameState    lastRenderedState_ = MAIN_MENU;
    TransitionKind transitionKind_ = TransitionKind::None;
    float        transitionElapsed_ = 0.0f;
    float        transitionDuration_ = 0.0f;
    int          transitionSlideDir_ = 1;
    Uint64       lastRenderTicks_ = 0;

    bool EnsureSceneTextures(int w, int h);
    void BeginTransition(GameState from, GameState to);
    void DrawTransition(SDL_Texture* oldTex, SDL_Texture* newTex);
    bool IsTransitionActive() const {
        return (transitionKind_ != TransitionKind::None &&
                transitionElapsed_ < transitionDuration_) ||
               matchLoadPhase_ != MatchLoadPhase::None;
    }

    // ---- 地图选择 → 加载 → 进入对局 ----
    // 以地图卡预览图为起点放大到全屏，压黑；LLM 就绪/至少一帧黑屏后再淡入游戏。
    enum class MatchLoadPhase { None, Zoom, Loading, FadeIn };
    MatchLoadPhase matchLoadPhase_ = MatchLoadPhase::None;
    float        matchLoadElapsed_ = 0.0f;
    float        matchLoadZoomDuration_ = 0.55f;
    float        matchLoadFadeDuration_ = 0.35f;
    float        matchLoadMinHold_ = 0.30f;
    bool         matchLoadMapStarted_ = false;
    SDL_Texture* matchZoomTex_ = nullptr;
    float        matchZoomRect_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool         matchZoomRectValid_ = false;
    float        mapPreviewRect_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool         mapPreviewRectValid_ = false;
    std::string  pendingMatchMapRel_;
    bool         sideChoicePending_ = false;
    bool         sidePopupOpen_ = false;
    bool         sidePopupClosing_ = false;
    float        sidePopupAlpha_ = 0.0f;
    float        sidePopupCloseElapsed_ = 0.0f;
    void FinalizeSideChoice();
    void UpdateSidePopupFade(float dt);
    void BeginMatchLoad(MatchMode mode, const std::string& mapRel, Team team);
    void DrawMatchZoomOverlay();

    // 内部方法
    void HandleInput();
    GameUI::Actions BuildActions();
    void StartMatch(MatchMode mode);   // 按模式加载对应地图并开局
    void RestartCurrentMatch();        // 暂停菜单“重新开始”：沿用地图，重置回合
    void StartMatchWithMap(MatchMode mode, const std::string& mapRel,
                           Team playerTeam = Team::CT);  // 指定地图开局
    // 对局历史
    std::vector<MatchRecord> history_;
    std::string historyPath_;
    std::string currentMapId_;
    long long   matchStartMs_ = 0;
    void RecordMatchHistory(bool abandoned);
    int  mapSelectIndex_ = 0;                                            // 地图选择光标
    std::string pendingStartMap_;        // --map=<id> 指定的地图
    std::string currentMapRel_;          // 当前对局地图（重新开始时沿用）
    Team        pendingPlayerTeam_ = Team::CT;
    bool openMapSelectOnStart_ = false;  // --mapselect
    void* menuBgTex_ = nullptr;   // 主菜单背景图
    int   menuBgW_ = 0, menuBgH_ = 0;
    void* mapPreviewTex_[8] = {nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr};  // 地图预览图
    void LoadAudioConfig();
    void InitLLM();
    void EnsureLlmInitialized();
    bool llmInitialized_ = false;
    void ApplySavedSettings();   // 把保存的设置应用到运行时对象
    void Update(float dt);
    void Render();
    void ResizeLogicalViewport(int w, int h);

public:
    Engine();
    ~Engine();

    bool Initialize(const char* title, int width, int height, bool autostart = false,
                    bool fastRound = false, int startMode = 1, bool coachLlm = true,
                    const std::string& startMap = std::string(),
                    bool openMapSelect = false, bool openHistory = false, const std::string& side = "");
    void Run();
    void Clean();
    void ToggleFullscreen();
};
