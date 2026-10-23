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
#include "LLM/BotDecisionScheduler.hpp"
#include <SDL2/SDL.h>
#include <memory>

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
    std::unique_ptr<BotDecisionScheduler> botDriver;
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

    // 内部方法
    void HandleInput();
    GameUI::Actions BuildActions();
    void StartMatch(MatchMode mode);   // 按模式加载对应地图并开局
    void StartMatchWithMap(MatchMode mode, const std::string& mapRel);  // 指定地图开局
    int  mapSelectIndex_ = 0;                                            // 地图选择光标
    std::string pendingStartMap_;        // --map=<id> 指定的地图
    bool openMapSelectOnStart_ = false;  // --mapselect
    void* menuBgTex_ = nullptr;   // 主菜单背景图
    int   menuBgW_ = 0, menuBgH_ = 0;
    void* mapPreviewTex_[8] = {nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr};  // 地图预览图
    void LoadAudioConfig();
    void InitLLMBot();
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
                    bool openMapSelect = false);
    void Run();
    void Clean();
    void ToggleFullscreen();
};
