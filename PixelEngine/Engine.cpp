#include "Engine.hpp"
#include "CS2D/MapCatalog.hpp"
#include "Core/PathUtils.hpp"
#include "Core/IniFile.hpp"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace {

// 分辨率档位表（与 GameUI 设置页下拉一致）
const int kResW[] = { 800, 1024, 1280, 1920, 2560 };
const int kResH[] = { 600, 768,  960,  1080, 1440 };
constexpr int kResCount = static_cast<int>(sizeof(kResW) / sizeof(kResW[0]));

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
    a.startMatchOnMap = [this](int idx) {
        if (idx < 0 || idx >= kMapCatalogCount) return;
        StartMatchWithMap(MatchMode::BombDefusal, kMapCatalog[idx].tmj);
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
        if (!isFullscreen && window) {
            SDL_Rect usable;
            SDL_GetDisplayUsableBounds(0, &usable);
            if (SDL_RectEmpty(&usable)) { usable = { 0, 0, w, h }; }
            const int cw = std::min(w, usable.w);
            const int ch = std::min(h, usable.h);
            SDL_SetWindowSize(window, cw, ch);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            ResizeLogicalViewport(cw, ch);
        }
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
                        int startMode, bool coachLlm, const std::string& startMap, bool openMapSelect) {
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
    currentResW_ = (currentResIdx_ >= 0 && currentResIdx_ < kResCount) ? kResW[currentResIdx_] : width;
    currentResH_ = (currentResIdx_ >= 0 && currentResIdx_ < kResCount) ? kResH[currentResIdx_] : height;

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
    ResizeLogicalViewport(currentResW_, currentResH_);

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
    InitLLMBot();

    // 若存档要求全屏
    if (saveManager->GetSettings().fullscreen && !isFullscreen) {
        ToggleFullscreen();
    }

    audioManager->PlayMusic("assets/audio/menu.wav");

    isRunning = true;

    // 调试/回归用：跳过菜单直接进入对局
    if (openMapSelectOnStart_) {
        LoadMapPreviews(renderer, mapPreviewTex_, kMapCatalogCount);
        currentState = MAP_SELECT;
    } else if (autostart) {
        const int m = (startMode >= 0 && startMode <= 3) ? startMode : 1;
        if (!pendingStartMap_.empty()) {
            // --map=<id>：按 id 在目录里找地图
            const char* chosen = nullptr;
            for (int i = 0; i < kMapCatalogCount; ++i) {
                if (pendingStartMap_ == kMapCatalog[i].id) { chosen = kMapCatalog[i].tmj; break; }
            }
            if (chosen) StartMatchWithMap(static_cast<MatchMode>(m), chosen);
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

void Engine::ResizeLogicalViewport(int w, int h) {
    logicalW_ = w;
    logicalH_ = h;
    if (renderer) SDL_RenderSetLogicalSize(renderer, logicalW_, logicalH_);
    if (cs2dRenderer) cs2dRenderer->SetViewportSize(logicalW_, logicalH_);
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

    if (botDriver) { botDriver->Shutdown(); botDriver.reset(); }

    if (audioManager) audioManager->Clean();
    if (imGuiUI)      imGuiUI->Shutdown();
    if (cs2dRenderer) cs2dRenderer->Clean();

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
        ResizeLogicalViewport(cw, ch);
        isFullscreen = false;
    } else {
        // 进入全屏：以高度为基准按屏幕比例扩展逻辑视口（动态 FOV）
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        int sw = 0, sh = 0;
        SDL_GetRendererOutputSize(renderer, &sw, &sh);
        const float aspect = (sh > 0) ? static_cast<float>(sw) / static_cast<float>(sh) : (4.0f / 3.0f);
        const int h = 600;
        const int w = static_cast<int>(600.0f * aspect);
        ResizeLogicalViewport(w, h);
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

    if (inputManager->IsPauseToggleRequested()) {
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
        } else if (currentState == MAP_SELECT) {
            currentState = MAIN_MENU;
        } else {
            isRunning = false;
        }
    }

    if (currentState != PLAYING) return;

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
}

// ============================================================================
// 开始对局
// ============================================================================

void Engine::StartMatch(MatchMode mode) {
    // 模式 → 默认地图（爆破模式可由地图选择界面指定）
    //   死斗               → cs_office（老图，节奏快）
    //   团队歼灭/爆破/人质  → ancient_ruins（5v5 主战图）
    const char* mapRel = "assets/cs2d/maps/ancient_ruins.tmj";
    switch (mode) {
        case MatchMode::DeathMatch:      mapRel = "assets/cs2d/maps/cs_office.tmj"; break;
        case MatchMode::TeamElimination:
        case MatchMode::BombDefusal:
        case MatchMode::HostageRescue:   mapRel = "assets/cs2d/maps/ancient_ruins.tmj"; break;
        default:                         mapRel = "assets/cs2d/maps/ancient_ruins.tmj"; break;
    }
    StartMatchWithMap(mode, mapRel);
}

void Engine::StartMatchWithMap(MatchMode mode, const std::string& mapFile) {
    audioManager->StopMusic();
    currentMatchMode_ = mode;

    const std::string mapRel = mapFile;

    // 模式必须在 LoadMap 之前设定（LoadMap → ResetToNewMatch 依赖 mode_）
    cs2dGame->SetMatchMode(mode);

    const std::string mapPath = PathUtils::ResolveAppPath(mapRel);
    if (!cs2dGame->LoadMap(mapPath, cs2dRenderer.get())) {
        std::cerr << "CS2D 地图加载失败: " << mapPath << std::endl;
        return;
    }

    inputManager->SwitchToEnglishKeyboardLayout();

    currentState = PLAYING;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
    cs2dOverlay.virtCursorY = logicalH_ * 0.5f;

    // 教练层接入 LLM（与 BOT 共用同一个客户端，串行请求）
    if (botDriver) {
        if (auto* ctrl = botDriver->Controller()) {
            cs2dGame->SetCoachLlm([ctrl](const std::string& sys, const std::string& usr,
                                         int maxTok,
                                         std::function<void(const std::string&)> cb) {
                ctrl->AskRaw(sys, usr, maxTok, std::move(cb), "COACH");
            });
        }
    }
    cs2dGame->SetCoachLlmEnabled(coachLlmEnabled_);

    std::cout << "CS2D 对局开始: " << mapRel << std::endl;




}

// ============================================================================
// 更新 / 渲染
// ============================================================================

void Engine::Update(float dt) {
    if (currentState != PLAYING) return;

    // E 键用于拆包 / 营救人质（按住）
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

void Engine::Render() {
    SDL_SetRenderDrawColor(renderer, 18, 26, 36, 255);
    SDL_RenderClear(renderer);

    const bool gameplayVisible = (currentState == PLAYING || currentState == PAUSED ||
                                  currentState == EXIT_CONFIRM);

    if (gameplayVisible) {
        cs2dGame->Render(logicalW_, logicalH_);
        cs2dOverlay.Render(cs2dRenderer.get(), cs2dGame.get(), logicalW_, logicalH_);
    }

    imGuiUI->BeginFrame();

    if (currentState == MAP_SELECT) {
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

        ctx.radarPlayerCentered   = cs2dOverlay.radar.playerCentered;
        ctx.radarRotateWithPlayer = cs2dOverlay.radar.rotateWithPlayer;
        ctx.radarBlendBg          = cs2dOverlay.radar.blendBg;
        ctx.radarBgAlpha          = cs2dOverlay.radar.bgAlpha;
        ctx.radarHudSize          = cs2dOverlay.radar.hudSize;
        ctx.radarMapZoom          = cs2dOverlay.radar.mapZoom;

        ctx.resW         = currentResW_;
        ctx.resH         = currentResH_;
        ctx.resIdx       = currentResIdx_;
        ctx.isFullscreen = isFullscreen;

        ctx.statKills  = saveManager->GetStats().kills;
        ctx.statDeaths = saveManager->GetStats().deaths;

        gameUI->DrawMenu(ctx, BuildActions());
    } else {
        GameUI::InGameCtx ictx;
        ictx.state    = currentState;
        ictx.weaponName = cs2dGame->GetWeaponName();
        ictx.ammo     = cs2dGame->GetPlayer().ammo;
        ictx.magazineSize = (cs2dGame->GetPlayer().weaponSlot == 2)
                              ? 0 : cs2dGame->GetPlayer().maxAmmo;
        ictx.hasNoWeaponHint = cs2dGame->GetPlayer().reloading;
        ictx.hintText        = "换弹中...";
        ictx.totalPlayers    = cs2dGame->GetBotCount() + 1;
        ictx.kills           = cs2dGame->GetPlayerKills();
        ictx.deaths          = cs2dGame->GetPlayerDeaths();

        ictx.scoreT           = cs2dGame->GetScoreT();
        ictx.scoreCT          = cs2dGame->GetScoreCT();
        ictx.roundNumber      = cs2dGame->GetRoundNumber();
        ictx.roundRemainingSec = static_cast<int>(cs2dGame->GetRoundRemaining());
        ictx.aliveT           = cs2dGame->AliveCount(Team::T);
        ictx.aliveCT          = cs2dGame->AliveCount(Team::CT);
        ictx.money            = cs2dGame->GetPlayerMoney();
        ictx.armor            = cs2dGame->GetPlayerArmor();
        ictx.grenadeCount     = cs2dGame->GetPlayer().grenadeCount;
        ictx.health           = cs2dGame->GetPlayerHealth();
        ictx.maxHealth        = cs2dGame->GetPlayerMaxHealth();
        ictx.helmet           = cs2dGame->GetPlayerHelmet();
        ictx.matchOver        = cs2dGame->IsMatchOver();
        ictx.matchWinnerIsCT  = (cs2dGame->GetMatchWinner() == Team::CT);
        ictx.matchOverReturn  = 8.0f - cs2dGame->GetMatchOverTimer();
        ictx.coachReason      = cs2dGame->GetCoachReason(cs2dGame->GetPlayer().team);
        ictx.coachFlash       = cs2dGame->GetCoachPlanAge() < 1.5f;

        // 目标物
        switch (cs2dGame->GetMatchMode()) {
            case MatchMode::BombDefusal:   ictx.objective = 1; break;
            case MatchMode::HostageRescue: ictx.objective = 2; break;
            default:                       ictx.objective = 0; break;
        }
        ictx.bombPlanted      = cs2dGame->IsBombPlanted();
        ictx.bombTimer        = cs2dGame->GetBombTimer();
        ictx.defuseProgress   = cs2dGame->GetDefuseProgress();
        ictx.hostagesRescued  = cs2dGame->GetHostagesRescued();
        ictx.hostagesTotal    = cs2dGame->GetHostagesTotal();
        ictx.rescueProgress   = cs2dGame->GetRescueProgress();

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
    SDL_RenderPresent(renderer);
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
    for (int i = 1; i < static_cast<int>(WeaponId::Count); ++i) {
        const WeaponDef& def = WeaponDB::Get(static_cast<WeaponId>(i));
        if (def.price <= 0) continue;

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
            cs2dGame->TryBuyArmor(-id);      // -1 → 护甲, -2 → 护甲+头盔
        } else {
            cs2dGame->TryBuy(static_cast<WeaponId>(id));
        }
    });
}

void Engine::InitLLMBot() {
    const std::string cfgPath = PathUtils::ResolveAppPath("assets/config/llm.ini");

    botDriver = std::make_unique<BotDecisionScheduler>();
    botDriver->Initialize(cfgPath);   // 失败时内部打印提示, BOT 回退传统行为树
}
