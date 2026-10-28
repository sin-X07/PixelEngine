#include "Engine.hpp"
#include <cstdlib>
#include <ctime>
#include <chrono>
#include "Core/Resolutions.hpp"
#include "Graphics/UiScale.hpp"
#include "CS2D/MapCatalog.hpp"
#include "Core/PathUtils.hpp"
#include "Core/IniFile.hpp"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <cmath>

namespace {

// 分辨率档位表（与 GameUI 设置页下拉一致）

// CS2D 启动地图
constexpr const char* kStartMap = "assets/cs2d/maps/ancient_ruins.tmj";

} // anonymous namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

Engine::Engine() {
    audioManager = std::make_unique<AudioManager>();
    inputManager = std::make_unique<InputManager>();
    imGuiUI      = std::make_unique<ImGuiUI>();
    gameUI       = std::make_unique<GameUI>();
    saveManager  = std::make_unique<SaveManager>();

    cs2dRenderer = std::make_unique<CS2DRenderer>();
    cs2dGame     = std::make_unique<CS2DGame>();

    gameUI->SetImGuiUI(imGuiUI.get());
}

Engine::~Engine() {
    Clean();
}

// ============================================================================
// 菜单动作绑定
// ============================================================================

// 加载地图预览图（只做一次）
static void LoadMapPreviews(SDL_Renderer* renderer, void** outTex, int count) {
    int loaded = 0;
    for (int i = 0; i < count; ++i) {
        if (outTex[i]) { ++loaded; continue; }
        if (outTex[i]) continue;
        const std::string path = PathUtils::ResolveAppPath(kMapCatalog[i].preview);
        SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
        if (!tex) {
            std::cerr << "[MapSelect] 预览图加载失败: " << path << std::endl;
            continue;
        }
        outTex[i] = tex;
        ++loaded;
    }
    std::cout << "[MapSelect] 地图 " << count << " 张，预览图 " << loaded << " 张就绪"
              << std::endl;
}

GameUI::Actions Engine::BuildActions() {
    GameUI::Actions a;

    a.playClick = [this] { audioManager->PlaySound(SoundEffect::CLICK); };
    a.goState   = [this](GameState s) {
        if (s == MAP_SELECT) {
            LoadMapPreviews(renderer, mapPreviewTex_, kMapCatalogCount);
        }
        currentState = s;
    };
    a.openSettings = [this](GameState ret) { settingsReturnState = ret; currentState = SETTINGS; };
    a.startMatch     = [this] { StartMatch(currentMatchMode_); };
    a.startMatchMode = [this](int m) { StartMatch(static_cast<MatchMode>(m)); };
    // 选择阵营（爆破模式）：0 = T, 1 = CT
    a.chooseSide = [this](int side) {
        pendingPlayerTeam_ = (side == 0) ? Team::T : Team::CT;
        std::string mapRel = pendingSideMapRel_;
        if (mapRel.empty() && currentMatchMode_ == MatchMode::BombDefusal) {
            mapRel = "assets/cs2d/maps/ancient_ruins.tmj";
        }
        if (mapRel.empty()) mapRel = "assets/cs2d/maps/ancient_ruins.tmj";
        StartMatchWithMap(currentMatchMode_, mapRel, pendingPlayerTeam_);
    };

    a.startMatchOnMap = [this](int idx) {
        if (idx < 0 || idx >= kMapCatalogCount) return;
        // 爆破模式：先进入阵营选择界面
        pendingSideMapRel_ = kMapCatalog[idx].tmj;
        currentMatchMode_ = MatchMode::BombDefusal;
        currentState = SIDE_SELECT;
    };
    a.selectMap = [this](int idx) {
        if (idx >= 0 && idx < kMapCatalogCount) mapSelectIndex_ = idx;
    };
    a.resumeGame   = [this] {
        currentState = PLAYING;
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_ShowCursor(SDL_DISABLE);
        audioManager->ResumeMusic();
    };
    a.confirmExit = [this](bool toDesktop) { exitToDesktop = toDesktop; currentState = EXIT_CONFIRM; };
    a.quitApp = [this] {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        if (exitToDesktop) {
            isRunning = false;
        } else {
            currentState = MAIN_MENU;
            audioManager->ResumeMusic();
        }
    };
    a.quitToDesktop = [this] {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        isRunning = false;
    };

    a.toggleMute       = [this] { audioManager->ToggleMute(); };
    a.setMasterVolume  = [this](int v) { audioManager->SetMasterVolume(v); };
    a.setMusicVolume   = [this](int v) { audioManager->SetMusicVolume(v); };
    a.setEffectsVolume = [this](int v) { audioManager->SetEffectsVolume(v); };

    // 设置类：同步写入运行时对象 + SaveManager，退出时统一落盘
    a.setMouseSensitivity = [this](float v) {
        cs2dGame->SetMouseSensitivity(v);
        saveManager->MutableSettings().mouseSensitivity = v;
    };
    a.setTargetFPS = [this](int fps) {
        targetFPS_ = fps;
        saveManager->MutableSettings().targetFPS = fps;
    };

    // 准星
    a.setCrosshairColor = [this](Uint8 r, Uint8 g, Uint8 b, Uint8 al) {
        cs2dOverlay.crosshair.r = r; cs2dOverlay.crosshair.g = g;
        cs2dOverlay.crosshair.b = b; cs2dOverlay.crosshair.a = al;
        auto& s = saveManager->MutableSettings();
        s.crossR = r; s.crossG = g; s.crossB = b; s.crossA = al;
    };
    a.setCrosshairThickness = [this](float v) {
        cs2dOverlay.crosshair.thickness = v;
        saveManager->MutableSettings().crossThickness = v;
    };
    a.setCrosshairCenterDot = [this](bool v) {
        cs2dOverlay.crosshair.centerDot = v;
        saveManager->MutableSettings().crossCenterDot = v;
    };
    a.setCrosshairLength = [this](float v) {
        cs2dOverlay.crosshair.length = v;
        saveManager->MutableSettings().crossLength = v;
    };
    a.setCrosshairGap = [this](float v) {
        cs2dOverlay.crosshair.gap = v;
        saveManager->MutableSettings().crossGap = v;
    };
    a.setCrosshairDynamic = [this](bool v) {
        cs2dOverlay.crosshair.dynamic = v;
        saveManager->MutableSettings().crossDynamic = v;
    };

    // 雷达
    a.setRadarPlayerCentered = [this](bool v) {
        cs2dOverlay.radar.playerCentered = v;
        saveManager->MutableSettings().radarPlayerCentered = v;
    };
    a.setRadarRotateWithPlayer = [this](bool v) {
        cs2dOverlay.radar.rotateWithPlayer = v;
        saveManager->MutableSettings().radarRotateWithPlayer = v;
    };
    a.setRadarBlendBg = [this](bool v) {
        cs2dOverlay.radar.blendBg = v;
        saveManager->MutableSettings().radarBlendBg = v;
    };
    a.setRadarBgAlpha = [this](int v) {
        cs2dOverlay.radar.bgAlpha = v;
        saveManager->MutableSettings().radarBgAlpha = v;
    };
    a.setRadarHudSize = [this](int v) {
        cs2dOverlay.radar.hudSize = v;
        saveManager->MutableSettings().radarHudSize = v;
    };
    a.setRadarMapZoom = [this](float v) {
        cs2dOverlay.radar.mapZoom = v;
        saveManager->MutableSettings().radarMapZoom = v;
    };

    // 显示
    a.setResolution = [this](int w, int h, int idx) {
        currentResW_ = w;
        currentResH_ = h;
        currentResIdx_ = idx;
        saveManager->MutableSettings().resIdx = idx;

        // 方案 B：分辨率 = 内部渲染分辨率，窗口/全屏都生效
        if (!isFullscreen && window) {
            SDL_Rect usable;
            SDL_GetDisplayUsableBounds(0, &usable);
            if (SDL_RectEmpty(&usable)) { usable = { 0, 0, w, h }; }
            const int cw = std::min(w, usable.w);
            const int ch = std::min(h, usable.h);
            SDL_SetWindowSize(window, cw, ch);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        // 内部画布由「窗口实际尺寸 + 渲染缩放」推导（分辨率只决定窗口大小）
        if (isFullscreen) {
            int ow = w, oh = h;
            if (renderer) SDL_GetRendererOutputSize(renderer, &ow, &oh);
            ResizeLogicalViewport(ow, oh);
        } else if (window) {
            int ww = w, wh = h;
            SDL_GetWindowSize(window, &ww, &wh);
            ResizeLogicalViewport(ww, wh);
        }
    };
    a.setRenderScale = [this](int pct) {
        saveManager->MutableSettings().renderScalePct = pct;
        int ww = currentResW_, wh = currentResH_;
        if (isFullscreen && renderer) SDL_GetRendererOutputSize(renderer, &ww, &wh);
        else if (window) SDL_GetWindowSize(window, &ww, &wh);
        ResizeLogicalViewport(ww, wh);
    };
    a.setFullscreen = [this](bool v) {
        if (v != isFullscreen) ToggleFullscreen();
    };

    return a;
}

// ============================================================================
// 初始化
// ============================================================================

bool Engine::Initialize(const char* title, int width, int height, bool autostart, bool fastRound,
                        int startMode, bool coachLlm, const std::string& startMap, bool openMapSelect, bool openHistory, const std::string& side) {
    pendingStartMap_ = startMap;
    openMapSelectOnStart_ = openMapSelect;

    coachLlmEnabled_ = coachLlm;
    cleaned = false;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        std::cerr << "SDL 初始化失败: " << SDL_GetError() << std::endl;
        return false;
    }

    // 音频
    audioManager->Initialize();
    LoadAudioConfig();

    // 存档（先读，以便应用分辨率 / 灵敏度 / 准星等设置）
    saveManager->Load();
    ApplySavedSettings();

    // 窗口
    currentResIdx_ = std::clamp(currentResIdx_, 0, kResolutionCount - 1);
    currentResW_ = kResolutions[currentResIdx_].w;
    currentResH_ = kResolutions[currentResIdx_].h;

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              currentResW_, currentResH_, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "窗口创建失败: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "渲染器创建失败: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    {
        int ww = currentResW_, wh = currentResH_;
        SDL_GetWindowSize(window, &ww, &wh);
        ResizeLogicalViewport(ww, wh);
    }

    if (!imGuiUI->Initialize(window, renderer)) return false;

    // CS2D 渲染器（地图目录 assets/cs2d/maps）
    cs2dRenderer->Initialize(renderer, "assets/cs2d/maps", logicalW_, logicalH_);

    // ImGui 后端消费原始 SDL 事件（窗口↔逻辑坐标换算）
    inputManager->SetEventCallback([this](const SDL_Event& e) {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Event converted = e;
            float lx = 0.0f, ly = 0.0f;
            SDL_RenderWindowToLogical(renderer, e.motion.x, e.motion.y, &lx, &ly);
            converted.motion.x = static_cast<Sint32>(lx);
            converted.motion.y = static_cast<Sint32>(ly);
            ImGui_ImplSDL2_ProcessEvent(&converted);
        } else {
            ImGui_ImplSDL2_ProcessEvent(&e);
        }
    });

    // LLM BOT
    InitLLM();

    // 若存档要求全屏
    if (saveManager->GetSettings().fullscreen && !isFullscreen) {
        ToggleFullscreen();
    }

    // 主菜单背景：用一张地图实景（失败不影响运行）
    {
        const std::string bgPath = PathUtils::ResolveAppPath("assets/cs2d/tilesets/dust_2.png");
        menuBgTex_ = IMG_LoadTexture(renderer, bgPath.c_str());
        if (menuBgTex_) {
            SDL_QueryTexture(static_cast<SDL_Texture*>(menuBgTex_), nullptr, nullptr,
                             &menuBgW_, &menuBgH_);
            std::cout << "[UI] 主菜单背景: dust_2.png (" << menuBgW_ << "x" << menuBgH_ << ")"
                      << std::endl;
        }
    }

    // 历史记录（本地文本，最多 50 场）
    historyPath_ = PathUtils::ResolveWritablePath(MatchHistory::kFileName);
    history_ = MatchHistory::Load(historyPath_);

    audioManager->PlayMusic("assets/audio/menu.wav");

    isRunning = true;

    // 调试/回归用：跳过菜单直接进入对局
    // 调试：--side=t|ct 指定玩家阵营
    if (!side.empty()) {
        pendingPlayerTeam_ = (side == "t" || side == "T") ? Team::T : Team::CT;
        std::cout << "[CS2D] 玩家阵营: "
                  << (pendingPlayerTeam_ == Team::T ? "T" : "CT") << std::endl;
    }

    if (openHistory) {
        currentState = HISTORY;
    } else if (openMapSelectOnStart_) {
        LoadMapPreviews(renderer, mapPreviewTex_, kMapCatalogCount);
        currentState = MAP_SELECT;
    } else if (autostart) {
        const int m = (startMode >= 0 && startMode <= 2) ? startMode : 1;
        if (!pendingStartMap_.empty()) {
            // --map=<id>：按 id 在目录里找地图
            const char* chosen = nullptr;
            for (int i = 0; i < kMapCatalogCount; ++i) {
                if (pendingStartMap_ == kMapCatalog[i].id) { chosen = kMapCatalog[i].tmj; break; }
            }
            if (chosen) StartMatchWithMap(static_cast<MatchMode>(m), chosen, pendingPlayerTeam_);
            else {
                std::cerr << "[CS2D] 未知地图 id: " << pendingStartMap_ << std::endl;
                StartMatch(static_cast<MatchMode>(m));
            }
        } else {
            StartMatch(static_cast<MatchMode>(m));
        }
    }

    // 快节奏回合（仅测试用，便于快速验证回合状态机）
    if (fastRound) {
        RoundConfig fast;
        fast.freezeTime = 1.0f;
        fast.buyTime    = 1.0f;
        fast.roundTime  = 8.0f;
        fast.endTime    = 1.0f;
        cs2dGame->SetRoundConfig(fast);
    }

    return true;
}

void Engine::ApplySavedSettings() {
    const auto& s = saveManager->GetSettings();

    // 灵敏度
    cs2dGame->SetMouseSensitivity(s.mouseSensitivity);

    // 准星
    cs2dOverlay.crosshair.r         = static_cast<Uint8>(s.crossR);
    cs2dOverlay.crosshair.g         = static_cast<Uint8>(s.crossG);
    cs2dOverlay.crosshair.b         = static_cast<Uint8>(s.crossB);
    cs2dOverlay.crosshair.a         = static_cast<Uint8>(s.crossA);
    cs2dOverlay.crosshair.thickness = s.crossThickness;
    cs2dOverlay.crosshair.length    = s.crossLength;
    cs2dOverlay.crosshair.gap       = s.crossGap;
    cs2dOverlay.crosshair.centerDot = s.crossCenterDot;
    cs2dOverlay.crosshair.dynamic   = s.crossDynamic;

    // 雷达
    cs2dOverlay.radar.playerCentered   = s.radarPlayerCentered;
    cs2dOverlay.radar.rotateWithPlayer = s.radarRotateWithPlayer;
    cs2dOverlay.radar.blendBg          = s.radarBlendBg;
    cs2dOverlay.radar.bgAlpha          = s.radarBgAlpha;
    cs2dOverlay.radar.hudSize          = s.radarHudSize;
    cs2dOverlay.radar.mapZoom          = s.radarMapZoom;

    // 显示
    targetFPS_     = (s.targetFPS > 0) ? s.targetFPS : 60;
    currentResIdx_ = std::clamp(s.resIdx, 0, kResCount - 1);
}

// 按「窗口像素尺寸 + 渲染缩放」推导内部画布：高度锚定 1080，宽度按比例
// 效果：任何窗口比例都居中铺满、无黑边、无内容丢失；UI 按 1920x1080 基准等比缩放
void Engine::ResizeLogicalViewport(int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;

    const int pct = std::clamp(saveManager ? saveManager->GetSettings().renderScalePct : 100,
                               25, 100);
    const float rs = static_cast<float>(pct) / 100.0f;
    const float aspect = static_cast<float>(winW) / static_cast<float>(winH);

    logicalH_ = std::max(270, static_cast<int>(std::lround(1080.0f * rs)));
    logicalW_ = std::max(360, static_cast<int>(std::lround(static_cast<float>(logicalH_) * aspect)));

    // UI 缩放 = 逻辑画布相对 1920x1080 的等比系数
    g_uiScale = std::clamp(std::min(static_cast<float>(logicalW_) / kDesignW,
                                    static_cast<float>(logicalH_) / kDesignH),
                           0.35f, 4.0f);

    if (renderer) SDL_RenderSetLogicalSize(renderer, logicalW_, logicalH_);
    if (cs2dRenderer) cs2dRenderer->SetViewportSize(logicalW_, logicalH_);
    if (imGuiUI) imGuiUI->SetUiScale(g_uiScale);

    cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
    cs2dOverlay.virtCursorY = logicalH_ * 0.5f;
}

// ============================================================================
// 主循环
// ============================================================================

void Engine::Run() {
    uint32_t previousTime = SDL_GetTicks64();
    float lag = 0.0f;

    while (isRunning) {
        const uint32_t currentTime = SDL_GetTicks64();
        const float elapsedTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;
        lag += elapsedTime;

        HandleInput();

        while (lag >= MS_PER_UPDATE) {
            Update(MS_PER_UPDATE / 1000.0f);
            lag -= MS_PER_UPDATE;
        }

        Render();
    }
}

// ============================================================================
// 清理
// ============================================================================

void Engine::Clean() {
    if (cleaned) return;
    cleaned = true;
    isRunning = false;

    inputManager->RestoreOriginalKeyboardLayout();

    if (saveManager) saveManager->Save();

    if (llmController_) { llmController_->Shutdown(); llmController_.reset(); }

    if (audioManager) audioManager->Clean();
    if (imGuiUI)      imGuiUI->Shutdown();
    if (cs2dRenderer) cs2dRenderer->Clean();

    for (auto& t : sceneTex_) {
        if (t) { SDL_DestroyTexture(t); t = nullptr; }
    }

    if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    if (window)   { SDL_DestroyWindow(window);     window = nullptr; }
    SDL_Quit();
    std::cout << "游戏引擎已安全关闭！" << std::endl;
}

void Engine::ToggleFullscreen() {
    if (!window) return;

    const Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        // 退出全屏：回到已选分辨率
        SDL_SetWindowFullscreen(window, 0);
        SDL_Rect usable;
        SDL_GetDisplayUsableBounds(0, &usable);
        if (SDL_RectEmpty(&usable)) usable = { 0, 0, currentResW_, currentResH_ };
        const int cw = std::min(currentResW_, usable.w);
        const int ch = std::min(currentResH_, usable.h);
        SDL_SetWindowSize(window, cw, ch);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        { int ww = 0, wh = 0; SDL_GetWindowSize(window, &ww, &wh);
          ResizeLogicalViewport(ww, wh); }
        isFullscreen = false;
    } else {
        // 进入全屏：桌面全屏（不切显示模式，避免闪屏/重排）
        // 逻辑尺寸仍按所选分辨率，多余区域由 SDL 自动 letterbox
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        { int ow = 0, oh = 0; SDL_GetRendererOutputSize(renderer, &ow, &oh);
          ResizeLogicalViewport(ow, oh); }
        isFullscreen = true;
    }

    saveManager->MutableSettings().fullscreen = isFullscreen;
}

// ============================================================================
// 输入
// ============================================================================

void Engine::HandleInput() {
    inputManager->ProcessEvents(renderer);

    if (inputManager->IsQuitRequested()) isRunning = false;
    if (inputManager->IsFullscreenToggleRequested()) ToggleFullscreen();
    if (inputManager->IsMuteToggleRequested()) audioManager->ToggleMute();

    if (inputManager->IsPauseToggleRequested() && !IsTransitionActive()) {
        if (currentState == PLAYING) {
            currentState = PAUSED;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            audioManager->PauseMusic();
        } else if (currentState == PAUSED) {
            currentState = PLAYING;
            SDL_SetRelativeMouseMode(SDL_TRUE);
            SDL_ShowCursor(SDL_DISABLE);
            audioManager->ResumeMusic();
        } else if (currentState == SETTINGS) {
            currentState = settingsReturnState;
        } else if (currentState == EXIT_CONFIRM) {
            currentState = PAUSED;
        } else if (currentState == SIDE_SELECT) {
            currentState = MAP_SELECT;
        } else if (currentState == MAP_SELECT || currentState == HISTORY) {
            currentState = MAIN_MENU;
        } else {
            isRunning = false;
        }
    }

    if (currentState != PLAYING) return;

    // 转场期间锁住对局输入，避免进入对局时第一帧误开枪/误移动。
    if (IsTransitionActive()) {
        cs2dMouseDown = false;
        cs2dKeyW = cs2dKeyA = cs2dKeyS = cs2dKeyD = false;
        cs2dKeyShift = cs2dKeyR = false;
        return;
    }

    // ---- 买枪菜单 ----
    if (inputManager->IsBuyMenuToggleRequested()) {
        buyMenuOpen_ = !buyMenuOpen_;
    }
    if (buyMenuOpen_ && !cs2dGame->IsBuyPhase()) {
        buyMenuOpen_ = false;   // 离开购买期自动关闭
    }

    if (buyMenuOpen_) {
        // 菜单打开：绝对鼠标，允许点击
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        // 正常对局：相对鼠标瞄准
        SDL_SetRelativeMouseMode(SDL_TRUE);
        int relX = 0, relY = 0;
        SDL_GetRelativeMouseState(&relX, &relY);
        cs2dOverlay.UpdateCursor(relX, relY,
                                 cs2dGame->GetMouseSensitivity(),
                                 cs2dRenderer->GetZoom(),
                                 cs2dRenderer->GetCameraX(), cs2dRenderer->GetCameraY(),
                                 logicalW_, logicalH_,
                                 cs2dWorldMouseX, cs2dWorldMouseY);
        cs2dMouseDown = inputManager->IsMouseLeftDown();

    // G：丢弃当前武器（近战不可丢）。边沿触发，避免按住连丢。
    {
        static bool prevG = false;
        const Uint8* kbG = SDL_GetKeyboardState(nullptr);
        const bool nowG = kbG[SDL_SCANCODE_G] != 0;
        if (nowG && !prevG && !buyMenuOpen_) {
            cs2dGame->DropCurrentWeapon();
        }
        prevG = nowG;
    }
        SDL_ShowCursor(SDL_DISABLE);
    }

    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    cs2dKeyW     = kb[SDL_SCANCODE_W];
    cs2dKeyA     = kb[SDL_SCANCODE_A];
    cs2dKeyS     = kb[SDL_SCANCODE_S];
    cs2dKeyD     = kb[SDL_SCANCODE_D];
    cs2dKeyShift = kb[SDL_SCANCODE_LSHIFT] || kb[SDL_SCANCODE_RSHIFT];
    cs2dKeyR     = kb[SDL_SCANCODE_R];

    // Tab 按住 → 记分板
    cs2dOverlay.prevTab = cs2dOverlay.keyTab;
    cs2dOverlay.keyTab  = kb[SDL_SCANCODE_TAB];
    showScoreboard_     = cs2dOverlay.keyTab;

    // 武器槽：1=主武器 2=副武器 3=近战 4=投掷物
    if (inputManager->IsDigit1Requested()) cs2dGame->SwitchWeaponSlot(0);
    if (inputManager->IsDigit2Requested()) cs2dGame->SwitchWeaponSlot(1);
    if (inputManager->IsDigit3Requested()) cs2dGame->SwitchWeaponSlot(2);
    if (inputManager->IsDigit4Requested()) cs2dGame->SwitchWeaponSlot(3);
    // 5：拿起 / 收起 C4
    {
        static bool prev5 = false;
        const bool now5 = kb[SDL_SCANCODE_5] != 0;
        if (now5 && !prev5) cs2dGame->ToggleHoldC4();
        prev5 = now5;
    }
    // 5：拿起 / 收起 C4

}

// ============================================================================
// 开始对局
// ============================================================================

void Engine::StartMatch(MatchMode mode) {
    // 模式 → 默认地图（爆破模式可由地图选择界面指定）
    //   死斗         → cs_office（老图，节奏快）
    //   团队歼灭/爆破 → ancient_ruins（5v5 主战图）
    // 已经在某张地图里 → 重新开始沿用该地图，而不是回到默认地图
    if (!currentMapRel_.empty()) {
        StartMatchWithMap(mode, currentMapRel_, pendingPlayerTeam_);
        return;
    }

    const char* mapRel = "assets/cs2d/maps/ancient_ruins.tmj";
    switch (mode) {
        case MatchMode::DeathMatch:      mapRel = "assets/cs2d/maps/cs_office.tmj"; break;
        case MatchMode::TeamElimination:
        case MatchMode::BombDefusal:     mapRel = "assets/cs2d/maps/ancient_ruins.tmj"; break;
        default:                         mapRel = "assets/cs2d/maps/ancient_ruins.tmj"; break;
    }
    StartMatchWithMap(mode, mapRel, pendingPlayerTeam_);
}

void Engine::StartMatchWithMap(MatchMode mode, const std::string& mapFile, Team playerTeam) {
    audioManager->StopMusic();
    currentMatchMode_ = mode;

    const std::string mapRel = mapFile;

    // 模式必须在 LoadMap 之前设定（LoadMap → ResetToNewMatch 依赖 mode_）
    cs2dGame->SetMatchMode(mode);

    currentMapRel_ = mapRel;

    // 阵营必须在 LoadMap 之前设置：LoadMap 会立刻生成玩家
    if (cs2dGame) cs2dGame->SetPlayerTeam(playerTeam);

    const std::string mapPath = PathUtils::ResolveAppPath(mapRel);
    if (!cs2dGame->LoadMap(mapPath, cs2dRenderer.get())) {
        std::cerr << "CS2D 地图加载失败: " << mapPath << std::endl;
        return;
    }

    inputManager->SwitchToEnglishKeyboardLayout();

    // 记录本场地图 id 与开始时间（历史记录用）
    // 按地图目录匹配：路径里含该地图的 tmj 即认作该地图，避免逐张硬编码
    currentMapId_ = "ancient_ruins";
    for (int i = 0; i < kMapCatalogCount; ++i) {
        if (mapRel.find(kMapCatalog[i].tmj) != std::string::npos) {
            currentMapId_ = kMapCatalog[i].id;
            break;
        }
    }
    // 死斗用的 cs_office 不在可选目录里，单独兜底
    if (currentMapId_ == "ancient_ruins" &&
        mapRel.find("cs_office") != std::string::npos) {
        currentMapId_ = "cs_office";
    }
    matchStartMs_ = static_cast<long long>(SDL_GetTicks64());

    currentState = PLAYING;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
    cs2dOverlay.virtCursorY = logicalH_ * 0.5f;

    // 教练层接入 LLM（与 BOT 共用同一个客户端，串行请求）
    // 教练层接入 LLM（全场唯一的 LLM 消费方：只产出战术计划，不驱动 BOT 行动）
    if (llmController_) {
        LLMBotController* ctrl = llmController_.get();
        cs2dGame->SetCoachLlm([ctrl](const std::string& sys, const std::string& usr,
                                     int maxTok, const std::string& schema,
                                     std::function<void(const std::string&)> cb) {
            ctrl->AskRaw(sys, usr, maxTok, std::move(cb), "COACH", schema);
        });
    }
    cs2dGame->SetCoachLlmEnabled(coachLlmEnabled_);

    std::cout << "CS2D 对局开始: " << mapRel << std::endl;




}

// ============================================================================
// 更新 / 渲染
// ============================================================================

void Engine::Update(float dt) {
    if (currentState != PLAYING) return;

    // E 键用于拆包（按住）
    const Uint8* kbNow = SDL_GetKeyboardState(nullptr);
    const bool interactHeld = kbNow[SDL_SCANCODE_E];

    cs2dGame->Update(dt, cs2dMouseDown, cs2dWorldMouseX, cs2dWorldMouseY,
                     cs2dKeyW, cs2dKeyA, cs2dKeyS, cs2dKeyD,
                     cs2dKeyShift, cs2dKeyR, interactHeld,
                     audioManager.get());

    // ---- 比赛结束：展示 8 秒后自动返回主菜单 ----
    // 注意：这段必须放在 Update 里（HandleInput 里会被提前 return 掉），
    // 并且要在 cs2dGame->Update 之后，才能读到本帧刚累加的 matchOverTimer。
    if (cs2dGame->IsMatchOver()) {
        constexpr float kReturnDelay = 8.0f;
        if (cs2dGame->GetMatchOverTimer() >= kReturnDelay) {
            RecordMatchHistory(false);
            std::cout << "[CS2D] 比赛结束，返回主菜单" << std::endl;
            buyMenuOpen_ = false;
            currentState = MAIN_MENU;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            audioManager->PlayMusic("assets/audio/menu.wav");
            return;
        }
    }

    // LLM 只作为主教练使用（教练层在 CS2DGame::CoachUpdate 里调用），不再逐 BOT 轮询。
}

// ============================================================================
// 页面转场：双离屏纹理 + 上一帧快照合成
// ============================================================================

bool Engine::EnsureSceneTextures(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (sceneTex_[0] && sceneTex_[1] && sceneW_ == w && sceneH_ == h) return true;

    for (auto& t : sceneTex_) {
        if (t) { SDL_DestroyTexture(t); t = nullptr; }
    }
    sceneW_ = w;
    sceneH_ = h;
    sceneValid_ = false;
    sceneWrite_ = 0;
    transitionKind_ = TransitionKind::None;

    for (int i = 0; i < 2; ++i) {
        sceneTex_[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, w, h);
        if (!sceneTex_[i]) {
            for (auto& t : sceneTex_) {
                if (t) { SDL_DestroyTexture(t); t = nullptr; }
            }
            std::cerr << "[Transition] 无法创建离屏场景纹理，转场禁用" << std::endl;
            return false;
        }
        SDL_SetTextureBlendMode(sceneTex_[i], SDL_BLENDMODE_BLEND);
    }
    return true;
}

void Engine::BeginTransition(GameState from, GameState to) {
    transitionElapsed_ = 0.0f;
    transitionSlideDir_ = 1;

    auto menuDepth = [](GameState s) {
        switch (s) {
            case MAIN_MENU:    return 0;
            case MAP_SELECT:   return 1;
            case SIDE_SELECT:  return 2;
            default:           return 1;
        }
    };

    if (to == PLAYING && (from == SIDE_SELECT || from == MAP_SELECT)) {
        transitionKind_ = TransitionKind::GoldWipe;
        transitionDuration_ = 0.30f;
    } else if ((from == PLAYING && to == PAUSED) ||
               (from == PAUSED && to == PLAYING)) {
        transitionKind_ = TransitionKind::PauseFreeze;
        transitionDuration_ = 0.16f;
    } else if (from == MAP_SELECT && to == SIDE_SELECT) {
        transitionKind_ = TransitionKind::MapZoom;
        transitionDuration_ = 0.28f;
    } else if (to == SETTINGS || to == HISTORY) {
        // 设置 / 历史记录从右侧抽屉式进入
        transitionKind_ = TransitionKind::Slide;
        transitionDuration_ = 0.20f;
        transitionSlideDir_ = 1;
    } else if (from == SETTINGS || from == HISTORY) {
        transitionKind_ = TransitionKind::Slide;
        transitionDuration_ = 0.20f;
        transitionSlideDir_ = -1;
    } else if ((from == MAIN_MENU || from == MAP_SELECT || from == SIDE_SELECT) &&
               (to == MAIN_MENU || to == MAP_SELECT || to == SIDE_SELECT) &&
               from != to) {
        transitionKind_ = TransitionKind::Slide;
        transitionDuration_ = 0.22f;
        transitionSlideDir_ = (menuDepth(to) >= menuDepth(from)) ? 1 : -1;
    } else {
        transitionKind_ = TransitionKind::Fade;
        transitionDuration_ = 0.20f;
    }
}

void Engine::DrawTransition(SDL_Texture* oldTex, SDL_Texture* newTex) {
    const int W = sceneW_;
    const int H = sceneH_;
    if (W <= 0 || H <= 0 || !newTex) return;

    const float raw = (transitionDuration_ > 0.0f)
        ? std::clamp(transitionElapsed_ / transitionDuration_, 0.0f, 1.0f)
        : 1.0f;
    const float ease = 1.0f - std::pow(1.0f - raw, 3.0f);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    auto drawScene = [&](SDL_Texture* tex, float alpha, float scale, float dx, float dy) {
        if (!tex) return;
        const float w = static_cast<float>(W) * scale;
        const float h = static_cast<float>(H) * scale;
        SDL_FRect dst = { (static_cast<float>(W) - w) * 0.5f + dx,
                          (static_cast<float>(H) - h) * 0.5f + dy, w, h };
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(
            std::clamp(alpha * 255.0f, 0.0f, 255.0f)));
        SDL_RenderCopyF(renderer, tex, nullptr, &dst);
    };

    switch (transitionKind_) {
        case TransitionKind::Fade: {
            // 新页面轻微放大浮入，旧页面淡出
            drawScene(newTex, 1.0f, 0.985f + 0.015f * ease, 0.0f, 0.0f);
            drawScene(oldTex, 1.0f - ease, 1.0f, 0.0f, 0.0f);
            break;
        }
        case TransitionKind::Slide: {
            const float off = 24.0f * (1.0f - ease);
            const float oldOff = -static_cast<float>(transitionSlideDir_) * 24.0f * ease;
            drawScene(newTex, 1.0f, 1.0f,
                      static_cast<float>(transitionSlideDir_) * off, 0.0f);
            drawScene(oldTex, 1.0f - ease, 1.0f, oldOff, 0.0f);
            break;
        }
        case TransitionKind::MapZoom: {
            // 地图预览轻微放大，阵营选择面板从下方浮入
            drawScene(oldTex, 1.0f - ease, 1.0f + 0.08f * ease, 0.0f, 0.0f);
            drawScene(newTex, ease, 1.0f, 0.0f, 10.0f * (1.0f - ease));
            break;
        }
        case TransitionKind::PauseFreeze: {
            // 旧对局画面冻结并轻微放大，暂停面板浮入
            drawScene(oldTex, 1.0f, 1.0f + 0.015f * ease, 0.0f, 0.0f);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0,
                static_cast<Uint8>(70.0f * (1.0f - 0.5f * ease)));
            SDL_Rect full = { 0, 0, W, H };
            SDL_RenderFillRect(renderer, &full);
            drawScene(newTex, ease, 1.0f, 0.0f, 6.0f * (1.0f - ease));
            break;
        }
        case TransitionKind::GoldWipe: {
            // 进入对局：旧页面淡出，金色斜向扫描线掠过
            drawScene(newTex, 1.0f, 1.0f, 0.0f, 0.0f);
            drawScene(oldTex, 1.0f - ease, 1.0f, 0.0f, 0.0f);

            const float pulse = std::sin(ease * 3.14159265f);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0,
                static_cast<Uint8>(pulse * 45.0f));
            SDL_Rect full = { 0, 0, W, H };
            SDL_RenderFillRect(renderer, &full);

            const float bandX = -80.0f + ease * (static_cast<float>(W) + 160.0f);
            for (int y = 0; y < H; y += 2) {
                const int x = static_cast<int>(bandX + static_cast<float>(y) * 0.20f);
                SDL_SetRenderDrawColor(renderer, 230, 185, 80, 24);
                SDL_RenderDrawLine(renderer, x - 50, y, x + 50, y);
                SDL_SetRenderDrawColor(renderer, 255, 225, 150, 85);
                SDL_RenderDrawLine(renderer, x - 2, y, x + 2, y);
            }
            break;
        }
        case TransitionKind::None:
        default:
            drawScene(newTex, 1.0f, 1.0f, 0.0f, 0.0f);
            break;
    }

    if (oldTex) SDL_SetTextureAlphaMod(oldTex, 255);
    SDL_SetTextureAlphaMod(newTex, 255);
}

void Engine::Render() {
    const Uint64 nowTicks = SDL_GetTicks64();
    float frameDt = lastRenderTicks_ > 0
        ? static_cast<float>(nowTicks - lastRenderTicks_) / 1000.0f
        : (1.0f / 60.0f);
    lastRenderTicks_ = nowTicks;
    frameDt = std::clamp(frameDt, 0.0f, 0.05f);

    // 记录本帧实际绘制的是哪个页面；按钮在绘制过程中触发切页时，
    // 下一帧才会用旧页面快照开始转场。
    const GameState renderedState = currentState;
    const bool sceneReady = EnsureSceneTextures(logicalW_, logicalH_);

    if (sceneReady && sceneValid_ && renderedState != lastRenderedState_) {
        BeginTransition(lastRenderedState_, renderedState);
    }
    if (transitionKind_ != TransitionKind::None) {
        transitionElapsed_ += frameDt;
        if (transitionElapsed_ >= transitionDuration_) {
            transitionKind_ = TransitionKind::None;
        }
    }

    const bool transitioning = IsTransitionActive();
    ImGuiIO& io = ImGui::GetIO();
    if (transitioning) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
    } else {
        io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard);
    }

    if (sceneReady) {
        SDL_SetRenderTarget(renderer, sceneTex_[sceneWrite_]);
    }
    SDL_SetRenderDrawColor(renderer, 18, 26, 36, 255);
    SDL_RenderClear(renderer);

    const bool gameplayVisible = (currentState == PLAYING || currentState == PAUSED ||
                                  currentState == EXIT_CONFIRM);

    if (gameplayVisible) {
        cs2dGame->Render(logicalW_, logicalH_);
        cs2dOverlay.Render(cs2dRenderer.get(), cs2dGame.get(), logicalW_, logicalH_);
    }

    imGuiUI->BeginFrame();

    if (currentState == SIDE_SELECT) {
        GameUI::SideSelectCtx sctx;
        // 用地图目录里的显示名
        sctx.mapName = "古代遗迹";
        for (int i = 0; i < kMapCatalogCount; ++i) {
            if (pendingSideMapRel_.find(kMapCatalog[i].tmj) != std::string::npos ||
                std::string(kMapCatalog[i].tmj) == pendingSideMapRel_) {
                sctx.mapName = kMapCatalog[i].name;
                break;
            }
        }
        gameUI->DrawSideSelect(sctx, BuildActions());
    } else if (currentState == HISTORY) {
        GameUI::HistoryCtx hctx;
        hctx.records = &history_;
        gameUI->DrawHistory(hctx, BuildActions());
    } else if (currentState == MAP_SELECT) {
        GameUI::MapSelectCtx mctx;
        mctx.selected = mapSelectIndex_;
        for (int i = 0; i < kMapCatalogCount && i < 8; ++i) {
            mctx.previewTex[i] = mapPreviewTex_[i];
        }
        gameUI->DrawMapSelect(mctx, BuildActions());
    } else if (currentState == MAIN_MENU || currentState == SETTINGS ||
        currentState == PAUSED    || currentState == EXIT_CONFIRM) {
        GameUI::MenuCtx ctx;
        ctx.state              = currentState;
        ctx.settingsReturnState = settingsReturnState;
        ctx.exitToDesktop      = exitToDesktop;
        ctx.masterVolume       = audioManager->GetMasterVolume();
        ctx.musicVolume        = audioManager->GetMusicVolume();
        ctx.effectsVolume      = audioManager->GetEffectsVolume();
        ctx.muted              = audioManager->IsMuted();
        ctx.mouseSensitivity   = cs2dGame->GetMouseSensitivity();
        ctx.targetFPS          = targetFPS_;

        ctx.crosshairR         = cs2dOverlay.crosshair.r;
        ctx.crosshairG         = cs2dOverlay.crosshair.g;
        ctx.crosshairB         = cs2dOverlay.crosshair.b;
        ctx.crosshairA         = cs2dOverlay.crosshair.a;
        ctx.crosshairThickness = cs2dOverlay.crosshair.thickness;
        ctx.crosshairCenterDot = cs2dOverlay.crosshair.centerDot;
        ctx.crosshairLength    = cs2dOverlay.crosshair.length;
        ctx.crosshairGap       = cs2dOverlay.crosshair.gap;
        ctx.crosshairDynamic   = cs2dOverlay.crosshair.dynamic;

        ctx.radarPlayerCentered   = cs2dOverlay.radar.playerCentered;
        ctx.radarRotateWithPlayer = cs2dOverlay.radar.rotateWithPlayer;
        ctx.radarBlendBg          = cs2dOverlay.radar.blendBg;
        ctx.radarBgAlpha          = cs2dOverlay.radar.bgAlpha;
        ctx.radarHudSize          = cs2dOverlay.radar.hudSize;
        ctx.radarMapZoom          = cs2dOverlay.radar.mapZoom;

        ctx.resW         = currentResW_;
        ctx.resH         = currentResH_;
        ctx.resIdx       = currentResIdx_;
        ctx.renderScalePct = saveManager->GetSettings().renderScalePct;
        ctx.isFullscreen = isFullscreen;

        ctx.backdropTex = menuBgTex_;
        ctx.backdropW   = menuBgW_;
        ctx.backdropH   = menuBgH_;

        gameUI->DrawMenu(ctx, BuildActions());
    } else {
        GameUI::InGameCtx ictx;
        ictx.state    = currentState;

        // 观战跟随 BOT 时，右下武器栏与左下血量/护甲显示被观战者的实时状态。
        const CS2DGame::Player* view = cs2dGame->GetSpectatedPlayer();
        const CS2DGame::Player& status = view ? *view : cs2dGame->GetPlayer();

        ictx.weaponName = cs2dGame->GetWeaponNameFor(status);
        ictx.ammo     = status.ammo;
        ictx.magazineSize = (status.weaponSlot == 2) ? 0 : status.maxAmmo;
        ictx.reserveAmmo = status.reserve;
        ictx.hasNoWeaponHint = status.reloading;
        ictx.hintText        = "换弹中...";
        ictx.totalPlayers    = cs2dGame->GetBotCount() + 1;
        ictx.kills           = view ? status.kills : cs2dGame->GetPlayerKills();
        ictx.deaths          = view ? status.deaths : cs2dGame->GetPlayerDeaths();

        ictx.scoreT           = cs2dGame->GetScoreT();
        ictx.scoreCT          = cs2dGame->GetScoreCT();
        ictx.roundNumber      = cs2dGame->GetRoundNumber();
        ictx.roundRemainingSec = static_cast<int>(cs2dGame->GetRoundRemaining());
        ictx.aliveT           = cs2dGame->AliveCount(Team::T);
        ictx.aliveCT          = cs2dGame->AliveCount(Team::CT);
        ictx.money            = status.money;
        ictx.armor            = status.armor;
        ictx.grenadeCount     = status.grenadeCount;
        ictx.health           = status.health;
        ictx.maxHealth        = status.maxHealth;
        ictx.helmet           = status.helmet;
        ictx.matchOver        = cs2dGame->IsMatchOver();
        ictx.matchWinnerIsCT  = (cs2dGame->GetMatchWinner() == Team::CT);
        ictx.matchOverReturn  = 8.0f - cs2dGame->GetMatchOverTimer();
        ictx.coachReason      = cs2dGame->GetCoachReason(status.team);
        ictx.coachFlash       = cs2dGame->GetCoachPlanAge() < 1.5f;

        // 目标物
        switch (cs2dGame->GetMatchMode()) {
            case MatchMode::BombDefusal:   ictx.objective = 1; break;
            default:                       ictx.objective = 0; break;
        }
        ictx.bombPlanted      = cs2dGame->IsBombPlanted();
        ictx.bombTimer        = cs2dGame->GetBombTimer();
        ictx.defuseProgress   = cs2dGame->GetDefuseProgress();

        // 击杀播报
        for (const auto& e : cs2dGame->GetKillFeed()) {
            ictx.killFeed.push_back(e.text);
        }

        // 屏幕中央提示（回合开始 / 胜负）
        ictx.announcement = cs2dGame->GetAnnouncement();
        {
            const float t = cs2dGame->GetAnnouncementTimer();
            ictx.announcementAlpha = std::clamp(t / 0.6f, 0.0f, 1.0f);
        }

        // 记分板
        ictx.showScoreboard = showScoreboard_;
        // 观察者 / C4
        ictx.spectating   = cs2dGame->IsSpectating();
        ictx.spectateMode = (cs2dGame->GetSpectateMode() == CS2DGame::SpectateMode::Free) ? 1 : 0;
        ictx.hasC4        = cs2dGame->PlayerHasC4();
        ictx.holdingC4    = cs2dGame->PlayerHoldingC4();
        ictx.bombDropped  = cs2dGame->IsBombDropped();
        ictx.bombIntel    = cs2dGame->HasBombIntel();
        ictx.plantProgress = cs2dGame->GetPlantProgress() / 3.0f;
        if (ictx.spectating) {
            ictx.spectateTarget = cs2dGame->GetSpectatedName();
        }

        if (showScoreboard_) {
            for (const auto& r : cs2dGame->BuildScoreboard()) {
                GameUI::InGameCtx::ScoreRowView v;
                v.name     = r.name;
                v.team     = static_cast<int>(r.team);
                v.kills    = r.kills;
                v.deaths   = r.deaths;
                v.money    = r.money;
                v.alive    = r.alive;
                v.isPlayer = r.isPlayer;
                ictx.scoreRows.push_back(v);
            }
        }

        gameUI->DrawInGame(ictx, imGuiUI.get());
    }

    // 买枪菜单（覆盖在 HUD 之上）
    if (buyMenuOpen_ && currentState == PLAYING) {
        RenderBuyMenu();
    }

    imGuiUI->EndFrame();

    if (sceneReady) {
        // 回到默认 render target：先清黑，再把旧/新场景合成到窗口。
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_Texture* oldTex = sceneValid_ ? sceneTex_[1 - sceneWrite_] : nullptr;
        DrawTransition(oldTex, sceneTex_[sceneWrite_]);
    }

    SDL_RenderPresent(renderer);

    lastRenderedState_ = renderedState;
    if (sceneReady) {
        sceneValid_ = true;
        sceneWrite_ ^= 1;
    }

    SDL_Delay(1000 / std::max(1, targetFPS_));
}

// ============================================================================
// 配置加载
// ============================================================================

void Engine::LoadAudioConfig() {
    const std::string path = PathUtils::ResolveWritablePath("audio_config.ini");
    IniFile ini = IniFile::Load(path);
    if (!ini.IsLoaded()) return;

    audioManager->SetMasterVolume (ini.GetInt("audio", "master",  100));
    audioManager->SetMusicVolume  (ini.GetInt("audio", "music",    25));
    audioManager->SetEffectsVolume(ini.GetInt("audio", "effects", 100));
    audioManager->SetMuted        (ini.GetBool("audio", "muted", false));
}

// 买枪菜单：由 WeaponDB 生成条目，点击回调直接调用 CS2DGame::TryBuy
void Engine::RenderBuyMenu() {
    GameUI::BuyMenuCtx bctx;
    bctx.money  = cs2dGame->GetPlayerMoney();
    bctx.canBuy = cs2dGame->IsBuyPhase() && cs2dGame->IsPlayerInBuyZone();
    if (!bctx.canBuy) {
        bctx.hint = cs2dGame->IsBuyPhase() ? "不在购买区内" : "当前不是购买阶段";
    }

    const CS2DGame::Player& p = cs2dGame->GetPlayer();
    const bool isTerrorist = (p.team == Team::T);
    for (int i = 1; i < static_cast<int>(WeaponId::Count); ++i) {
        const WeaponId wid = static_cast<WeaponId>(i);
        const WeaponDef& def = WeaponDB::Get(wid);
        if (def.price <= 0) continue;
        // 阵营隔离：只显示本阵营可购买的武器（照抄 CS2）
        if (!WeaponDB::TeamAllowed(wid, isTerrorist)) continue;

        GameUI::BuyItem it;
        it.weaponId   = i;
        it.name       = def.name;
        it.price      = def.price;
        it.affordable = (bctx.money >= def.price);

        switch (def.slot) {
            case WeaponSlotKind::Primary:   it.group = 0; break;
            case WeaponSlotKind::Secondary: it.group = 1; break;
            case WeaponSlotKind::Melee:     it.group = 2; break;
            case WeaponSlotKind::Grenade:   it.group = 3; break;
        }
        if (def.slot == WeaponSlotKind::Grenade) {
            // 投掷物有数量概念：闪光 2 个，其它 1 个，满了才标记为已拥有
            const int limit = (def.id == WeaponId::GrenadeFlash) ? 2 : 1;
            it.owned = (p.CountGrenade(def.id) >= limit ||
                        p.grenadeCount >= CS2DGame::Player::MAX_GRENADES);
        } else {
            for (int s = 0; s < 4; ++s) {
                if (static_cast<int>(p.weapons[s]) == i && s == it.group) {
                    it.owned = true;
                    break;
                }
            }
        }
        bctx.items.push_back(it);
    }

    // 护甲条目（用负数 weaponId 编码：-1=护甲 -2=护甲+头盔）
    {
        const int money = bctx.money;
        GameUI::BuyItem k1;
        k1.kind = 1; k1.armorTier = 1; k1.weaponId = -1;
        k1.name = "护甲"; k1.price = 650; k1.group = 4;
        k1.affordable = (money >= 650);
        k1.owned = (p.armor >= 100);
        bctx.items.push_back(k1);

        GameUI::BuyItem k2;
        k2.kind = 1; k2.armorTier = 2; k2.weaponId = -2;
        k2.name = "护甲+头盔"; k2.price = 1000; k2.group = 4;
        k2.affordable = (money >= 1000);
        k2.owned = (p.armor >= 100 && p.helmet);
        bctx.items.push_back(k2);
    }

    gameUI->DrawBuyMenu(bctx, [this](int id) {
        if (id < 0) {
            cs2dGame->TryBuyArmor(-id, audioManager.get());      // -1 → 护甲, -2 → 护甲+头盔
        } else {
            cs2dGame->TryBuy(static_cast<WeaponId>(id), audioManager.get());
        }
    });
}

void Engine::InitLLM() {
    const std::string cfgPath = PathUtils::ResolveAppPath("assets/config/llm.ini");

    LLMConfig cfg = LLMConfig::FromFile(cfgPath);

    // 相对路径 → 绝对路径（CreateProcess 需要；基准是 exe 所在目录）
    if (!cfg.serverExePath.empty()) {
        const std::string resolved = PathUtils::ResolveAppPath(cfg.serverExePath);
        if (!resolved.empty()) cfg.serverExePath = resolved;
    }
    if (!cfg.modelPath.empty()) {
        const std::string resolved = PathUtils::ResolveAppPath(cfg.modelPath);
        if (!resolved.empty()) cfg.modelPath = resolved;
    }

    llmController_ = std::make_unique<LLMBotController>();
    const bool ok = llmController_->Initialize(cfg);
    if (ok) {
        std::cout << "[LLM] 就绪 (server: " << cfg.serverUrl
                  << (cfg.autoLaunch ? ", 自动拉起" : "") << ")" << std::endl;
    } else {
        std::cout << "[LLM] 不可用, 教练回退规则引擎" << std::endl;
    }
}


// ============================================================================
// 对局历史
// ============================================================================

void Engine::RecordMatchHistory(bool abandoned) {
    if (!cs2dGame) return;

    MatchRecord r;
    // 时间戳
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    tmv = *std::localtime(&now);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min);
    r.time = buf;

    switch (cs2dGame->GetMatchMode()) {
        case MatchMode::BombDefusal:    r.modeId = "bomb";    break;
        case MatchMode::TeamElimination: r.modeId = "elim";    break;
        case MatchMode::DeathMatch:     r.modeId = "dm";      break;
        default:                        r.modeId = "bomb";    break;
    }

    r.mapId   = currentMapId_;
    r.scoreT  = cs2dGame->GetScoreT();
    r.scoreCT  = cs2dGame->GetScoreCT();
    r.kills   = cs2dGame->GetPlayerKills();
    r.deaths  = cs2dGame->GetPlayerDeaths();
    r.rounds  = cs2dGame->GetRoundNumber();

    const long long nowMs = static_cast<long long>(SDL_GetTicks64());
    r.durationSec = (matchStartMs_ > 0 && nowMs > matchStartMs_)
                    ? static_cast<int>((nowMs - matchStartMs_) / 1000) : 0;

    if (abandoned) {
        r.result = "draw";
    } else {
        const Team winner = cs2dGame->GetMatchWinner();
        const Team mine   = cs2dGame->GetPlayer().team;
        r.result = (winner == Team::None) ? "draw" : (winner == mine ? "win" : "lose");
    }

    MatchHistory::Append(historyPath_, r);
    history_ = MatchHistory::Load(historyPath_);

    std::cout << "[History] 已记录: " << r.time << " " << r.modeId << " "
              << r.mapId << " " << r.scoreT << ":" << r.scoreCT
              << " " << r.result << " 共 " << history_.size() << " 场" << std::endl;
}
