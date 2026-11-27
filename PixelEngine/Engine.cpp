#include "CS2D/ShowcaseCamera.hpp"
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

// 统一菜单黑场转场的归一化时间点：
// [0, outEnd) 旧页淡出；[outEnd, inStart) 纯黑且不刷新新页；[inStart, 1] 新页淡入。
constexpr float kMenuFadeOutEnd  = 0.45f;
constexpr float kMenuFadeInStart = 0.56f;

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
    weaponIconCache_ = std::make_unique<WeaponIconCache>();

    gameUI->SetImGuiUI(imGuiUI.get());
}

Engine::~Engine() {
    SaveAudioConfig();
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
        const std::string path = PathUtils::ResolveAppPath(kMapCatalog[i].preview);
        SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
        if (!tex) {
            std::cerr << "[MapSelect] 预览图加载失败: " << path << std::endl;
            continue;
        }
        // 预览图要在选图浮层里跟随卡片淡入淡出。IMG_LoadTexture 得到的纹理
        // 默认是 SDL_BLENDMODE_NONE，直接用 ImGui 的 AddImage(alpha) 时不会
        // 做 alpha 混合，只会把颜色调暗，淡出后残留黑色矩形；这里显式开启混合。
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
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
        if (s == MAP_SELECT && currentState == MAIN_MENU &&
            menu3D && menu3D->IsReady()) {
            BeginMenuCameraTransition([this] {
                OpenMapOverlay(MatchMode::BombDefusal);
            });
            return;
        }
        if (s == MAP_SELECT) {
            LoadMapPreviews(renderer, mapPreviewTex_, kMapCatalogCount);
        }
        // 从结算页返回主菜单：恢复菜单音乐并交还鼠标
        if (currentState == MATCH_END && s == MAIN_MENU) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            audioManager->PlayMusic(AudioManager::MusicTrackPath(audioManager->GetMusicTrack()));
        }
        if (currentState == SETTINGS) saveManager->Save();
        if (s == MAIN_MENU) {
            cs2dGame->StopObjectiveAudio(audioManager.get());
        }
        currentState = s;
    };
    a.openSettings = [this](GameState ret) { settingsReturnState = ret; currentState = SETTINGS; };
    a.startMatch     = [this] { RestartCurrentMatch(); };
    a.startMatchMode = [this](int m) {
        const MatchMode mode = static_cast<MatchMode>(m);
        if (currentState == MAIN_MENU && menu3D && menu3D->IsReady()) {
            BeginMenuCameraTransition([this, mode] {
                OpenMapOverlay(mode);
            });
            return;
        }
        if (mode == MatchMode::DeathMatch) {
            BeginMatchLoad(mode, "assets/cs2d/maps/office.tmj", pendingPlayerTeam_);
        } else if (mode == MatchMode::TeamElimination) {
            BeginMatchLoad(mode, "assets/cs2d/maps/ancient_ruins.tmj", pendingPlayerTeam_);
        } else {
            StartMatch(mode);
        }
    };
    // 选择阵营（爆破模式）：0 = T, 1 = CT, <0 = 随机
    a.chooseSide = [this](int side) {
        if (!sidePopupOpen_ || sidePopupClosing_) return;
        if (side < 0) side = std::rand() % 2;
        pendingPlayerTeam_ = (side == 0) ? Team::T : Team::CT;
        // 先淡出弹窗，淡出结束后再重建首回合。
        sidePopupClosing_ = true;
        sidePopupCloseElapsed_ = 0.0f;
    };

    a.close3DFocus = [this] {
        if (focused3D_ < 0 || focus3DClosing_) return;
        focus3DClosing_ = true;
        focusExitStartMs_ = SDL_GetTicks64();
        focusCloseStartProgress_ = focus3DProgress_;
    };
    a.focus3DSelect = [this](int category, int option) {
        if (focused3D_ < 0 || focus3DClosing_ || !menu3D || !saveManager) return;
        menu3D->FocusSelect(focused3D_, category, option);

        // 自定义结果写回存档（重启后保留）
        const Menu3D::Customization& c = menu3D->GetCustomization();
        auto& s = saveManager->MutableSettings();
        for (int i = 0; i < 5; ++i) {
            s.menuColor[i]     = c.color[i];
            s.menuPose[i]      = c.pose[i];
            s.menuAccessory[i] = c.accessory[i];
        }
        s.menuWeapon[0] = c.weapon[0];
        s.menuWeapon[1] = c.weapon[1];
        saveManager->Save();
    };

    a.selectMenuScene = [this](int index) {
        if (!menu3D || !saveManager || index < 0 || index >= kMapCatalogCount) return;
        menu3D->SetScene(index);
        saveManager->MutableSettings().menuScene = index;
        saveManager->Save();
    };

    a.selectMapChoice = [this](int index) {
        if (index < 0 || index >= mapOverlayCount_) return;
        mapOverlaySelected_ = index;
        mapPreviewRectValid_ = false;
        const int catalogIndex = mapOverlayCatalogIndex_[index];
        if (catalogIndex >= 0 && catalogIndex < kMapCatalogCount) {
            mapSelectIndex_ = catalogIndex;
        } else {
            mapSelectIndex_ = -1;
        }
    };
    a.startMatchOnChoice = [this](int index) {
        if (mapOverlayClosing_ || mapOverlayLaunching_ ||
            index < 0 || index >= mapOverlayCount_) return;
        const MatchMode mode = mapOverlayMode_;
        const std::string path = mapOverlayPaths_[index];
        pendingOverlayMode_ = mode;
        pendingOverlayMapPath_ = path;
        pendingZoomPreviewTex_ = static_cast<SDL_Texture*>(mapOverlayPreviews_[index]);
        const int catalogIndex = mapOverlayCatalogIndex_[index];
        mapSelectIndex_ = (catalogIndex >= 0 && catalogIndex < kMapCatalogCount)
            ? catalogIndex : -1;
        mapOverlaySelected_ = index;
        // 先让地图外的所有 UI 淡出，短暂停顿后再开始地图放大。
        mapOverlayLaunching_ = true;
        mapOverlayLaunchStartMs_ = SDL_GetTicks64();
        mapOverlayLaunchProgress_ = 0.0f;
    };
    a.closeMapOverlay = [this] {
        if (!mapOverlayOpen_ || mapOverlayClosing_ || mapOverlayLaunching_) return;
        mapOverlayClosing_ = true;
        mapOverlayCloseStartMs_ = SDL_GetTicks64();
    };

    a.startMatchOnMap = [this](int idx) {
        if (idx < 0 || idx >= kMapCatalogCount) return;
        // 地图选择后直接进入加载/对局；阵营在进入游戏后由弹窗选择。
        const std::string mapRel = kMapCatalog[idx].tmj;
        pendingPlayerTeam_ = Team::CT;   // 占位，选阵营后会重建对局
        currentMatchMode_ = MatchMode::BombDefusal;
        BeginMatchLoad(MatchMode::BombDefusal, mapRel, pendingPlayerTeam_);
    };
    a.selectMap = [this](int idx) {
        if (idx >= 0 && idx < kMapCatalogCount) mapSelectIndex_ = idx;
    };
    a.setMapPreviewRect = [this](float x0, float y0, float x1, float y1) {
        mapPreviewRect_[0] = x0; mapPreviewRect_[1] = y0;
        mapPreviewRect_[2] = x1; mapPreviewRect_[3] = y1;
        mapPreviewRectValid_ = true;
    };
    a.resumeGame   = [this] {
        if (sidePauseStartMs_) {
            sidePhaseStartMs_ += SDL_GetTicks64() - sidePauseStartMs_;
            sidePauseStartMs_ = 0;
        }
        // 恢复对局走与其它页面切换同一套黑场软切：先压黑、再淡入战场。
        // 之前的实现直接切 currentState，画面上就是「一黑到底然后突然出现」，
        // 既没有过渡也看不到淡入，观感像卡了一下。
        if (currentState != PLAYING) BeginTransition(PAUSED, PLAYING);
        currentState = PLAYING;
        SDL_SetRelativeMouseMode(sideChoicePending_ ? SDL_FALSE : SDL_TRUE);
        SDL_ShowCursor(sideChoicePending_ ? SDL_ENABLE : SDL_DISABLE);
        audioManager->ResumeMusic();
        cs2dGame->SetObjectiveAudioPaused(false, audioManager.get());
    };
    a.confirmExit = [this](bool toDesktop) { exitReturnState_ = currentState; exitToDesktop = toDesktop; currentState = EXIT_CONFIRM; };
    a.quitApp = [this] {
        cs2dGame->StopObjectiveAudio(audioManager.get());
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        if (exitToDesktop) {
            isRunning = false;
        } else {
            sideChoicePending_ = sidePopupOpen_ = false;
            sidePauseStartMs_ = 0;
            currentState = MAIN_MENU;
            audioManager->ResumeMusic();
        }
    };
    a.quitToDesktop = [this] {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        exitReturnState_ = MAIN_MENU;
        exitToDesktop = true;
        currentState = EXIT_CONFIRM;
    };

    a.toggleMute       = [this] { audioManager->ToggleMute(); };
    a.setMasterVolume  = [this](int v) { audioManager->SetMasterVolume(v); };
    a.setMusicVolume   = [this](int v) { audioManager->SetMusicVolume(v); };
    a.setEffectsVolume = [this](int v) { audioManager->SetEffectsVolume(v); };

    // 菜单 BGM：切换后立即生效（只在大厅/设置页预览，不打断对局）
    a.setMusicTrack = [this](int idx) {
        audioManager->SetMusicTrack(idx);
        if (currentState == MAIN_MENU || (currentState == SETTINGS && settingsReturnState == MAIN_MENU)) {
            audioManager->PlayMusic(AudioManager::MusicTrackPath(idx));
        }
        SaveAudioConfig();
    };
    // 按钮点击音效：切换后立刻用新音效试听一次
    a.setClickSound = [this](int idx) {
        audioManager->SetClickSound(idx);
        audioManager->PlaySound(SoundEffect::CLICK);
        SaveAudioConfig();
    };

    // 设置类：同步写入运行时对象 + SaveManager，退出时统一落盘
    a.setMouseSensitivity = [this](float v) {
        cs2dGame->SetMouseSensitivity(v);
        saveManager->MutableSettings().mouseSensitivity = v;
    };
    a.setTargetFPS = [this](int fps) {
        targetFPS_ = std::max(0, fps);
        saveManager->MutableSettings().targetFPS = targetFPS_;
    };
    a.setVSync = [this](bool enabled) { SetVSync(enabled); };
    a.resetSettingsSection = [this](GameUI::SettingsSection section) { ResetSettingsSection(section); };

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
    a.setAimLine = [this](bool v) {
        cs2dOverlay.crosshair.aimLine = v;
        saveManager->MutableSettings().aimLine = v;
    };
    a.setAimLineStyle = [this](const AimLineStyle& style) {
        cs2dOverlay.aimLineStyle = style;
        cs2dOverlay.aimLineStyle.Normalize();
        saveManager->MutableSettings().aimLineStyle = cs2dOverlay.aimLineStyle;
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

    // 视野 / 开镜（契约 v1.2）
    a.setFogOpacity = [this](int v) {
        v = std::clamp(v, 0, 100);
        saveManager->MutableSettings().fogOpacityPct = v;
        if (cs2dRenderer) cs2dRenderer->SetFogOpacityPct(v);
    };
    a.setSniperDelayedScopeClose = [this](bool v) {
        saveManager->MutableSettings().sniperDelayedScopeClose = v;
        if (cs2dGame) cs2dGame->SetDelayedScopeClose(v);
    };
    // 开镜准星（契约 v1.3）
    a.setScopeCrossThickness = [this](float v) {
        v = std::clamp(v, 1.0f, 6.0f);
        auto& st = saveManager->MutableSettings();
        st.scopeCrossThickness = v;
        if (cs2dGame) cs2dGame->SetScopeCrosshair(v, st.scopeCrossDynamic);
    };
    a.setScopeCrossDynamic = [this](bool v) {
        auto& st = saveManager->MutableSettings();
        st.scopeCrossDynamic = v;
        if (cs2dGame) cs2dGame->SetScopeCrosshair(st.scopeCrossThickness, v);
    };
    a.setScopeSensitivity = [this](float v) {
        saveManager->MutableSettings().scopeSensitivity = std::clamp(v, 0.5f, 2.0f);
    };

    // 显示
    a.setResolution = [this](int w, int h, int idx) {
        currentResW_ = w;
        currentResH_ = h;
        currentResIdx_ = idx;
        saveManager->MutableSettings().resIdx = idx;

        // 分辨率选项记录窗口大小，桌面全屏使用渲染缩放控制精度。
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
            if (window) SDL_GetWindowSize(window, &ow, &oh);
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
        if (isFullscreen && window) SDL_GetWindowSize(window, &ww, &wh);
        else if (window) SDL_GetWindowSize(window, &ww, &wh);
        ResizeLogicalViewport(ww, wh);
    };
    a.setFullscreen = [this](bool v) {
        if (v != isFullscreen) ToggleFullscreen();
    };
    a.setMainMenuButtonRect = [this](float x0, float y0, float x1, float y1) {
        mainMenuBtnRect_[0] = x0; mainMenuBtnRect_[1] = y0;
        mainMenuBtnRect_[2] = x1; mainMenuBtnRect_[3] = y1;
        mainMenuBtnRectValid_ = true;
    };
    a.saveLoadouts = [this](const WeaponDB::MatchLoadouts& loadouts) {
        if (!saveManager) return;
        saveManager->MutableSettings().loadouts = WeaponDB::SanitizeMatchLoadouts(loadouts);
        saveManager->Save();
    };

    return a;
}

// ============================================================================
// 初始化
// ============================================================================

bool Engine::Initialize(const char* title, int width, int height, bool autostart, bool fastRound,
                        int startMode, bool coachLlm, const std::string& startMap, bool openMapSelect, bool openHistory, const std::string& side, bool openSettings) {
    pendingStartMap_ = startMap;
    openMapSelectOnStart_ = openMapSelect;
    openSettingsOnStart_  = openSettings;

    coachLlmEnabled_ = coachLlm;
    llmInitialized_ = !coachLlmEnabled_;   // --coach=rule 不初始化/拉起 LLM
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
    inputManager->SetBindings(saveManager->GetSettings().bindings);
    ApplySavedSettings();

    // 窗口
    currentResIdx_ = std::clamp(currentResIdx_, 0, kResolutionCount - 1);
    currentResW_ = kResolutions[currentResIdx_].w;
    currentResH_ = kResolutions[currentResIdx_].h;

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              currentResW_, currentResH_, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "窗口创建失败: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#if defined(_WIN32)
    // Menu3D 使用独立的隐藏 OpenGL 上下文；主渲染器固定走 D3D11，
    // 避免两者争用同一个 OpenGL current context。
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
#if defined(_WIN32)
    if (!renderer) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    }
#endif
    if (!renderer) {
        std::cerr << "渲染器创建失败: " << SDL_GetError() << std::endl;
        return false;
    }
    {
        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(renderer, &info) == 0 && info.name) {
            std::cout << "[Renderer] " << info.name << std::endl;
        }
    }
    SetVSync(saveManager->GetSettings().vsync);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    {
        int ww = currentResW_, wh = currentResH_;
        SDL_GetWindowSize(window, &ww, &wh);
        ResizeLogicalViewport(ww, wh);
    }

    if (!imGuiUI->Initialize(window, renderer)) return false;

    // CS2D 渲染器（地图目录 assets/cs2d/maps）
    cs2dRenderer->Initialize(renderer, "assets/cs2d/maps", logicalW_, logicalH_);

    // 武器图标缓存（契约 v1 第 3 节）：非 owning renderer，GameUI 借用
    if (weaponIconCache_) {
        weaponIconCache_->Initialize(renderer);
        gameUI->SetWeaponIconCache(weaponIconCache_.get());
    }

    // 主菜单实时 3D 背景（独立隐藏 OpenGL 上下文；失败时回退静态背景）
    bool canInitMenu3D = true;
    {
        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(renderer, &info) == 0 && info.name &&
            std::string(info.name).find("opengl") != std::string::npos) {
            canInitMenu3D = false;
            std::cerr << "[Menu3D] 主渲染器为 OpenGL，跳过独立 3D 上下文" << std::endl;
        }
    }
    // 若存档要求全屏
    if (saveManager->GetSettings().fullscreen && !isFullscreen) {
        ToggleFullscreen();
    }

    if (canInitMenu3D) {
        menu3D = std::make_unique<Menu3D>();
        if (!menu3D->Initialize(renderer)) {
            std::cerr << "[Menu3D] 初始化失败，回退静态菜单背景: "
                      << menu3D->LastError() << std::endl;
            menu3D.reset();
        } else if (saveManager) {
            // 应用存档里的主菜单自定义（颜色/姿势/武器/配件）
            const auto& s = saveManager->GetSettings();
            Menu3D::Customization c;
            for (int i = 0; i < 5; ++i) {
                c.color[i]     = s.menuColor[i];
                c.pose[i]      = s.menuPose[i];
                c.accessory[i] = s.menuAccessory[i];
            }
            c.weapon[0] = s.menuWeapon[0];
            c.weapon[1] = s.menuWeapon[1];
            menu3D->SetCustomization(c);
            menu3D->SetScene(s.menuScene, true);
        }
    }

    // SDL's logical-size event filter has already converted mouse events.
    // Converting motion again shifts ImGui hit targets when the window is scaled.
    inputManager->SetEventCallback([](const SDL_Event& e) {
        ImGui_ImplSDL2_ProcessEvent(&e);
    });

    // LLM BOT 延迟到首次开局加载（黑屏加载阶段），避免启动菜单前阻塞。

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

    audioManager->PlayMusic(AudioManager::MusicTrackPath(audioManager->GetMusicTrack()));

    isRunning = true;

    // 调试/回归用：跳过菜单直接进入对局
    // 调试：--side=t|ct 指定玩家阵营
    if (!side.empty()) {
        pendingPlayerTeam_ = (side == "t" || side == "T") ? Team::T : Team::CT;
        std::cout << "[CS2D] 玩家阵营: "
                  << (pendingPlayerTeam_ == Team::T ? "T" : "CT") << std::endl;
    }

    if (openSettingsOnStart_) {
        // --settings：直接进设置页（验收截图用）
        settingsReturnState = MAIN_MENU;
        currentState = SETTINGS;
    } else if (openHistory) {
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

void Engine::SetVSync(bool enabled) {
    if (!renderer) return;
    if (SDL_RenderSetVSync(renderer, enabled ? 1 : 0) != 0) {
        displayError_ = "垂直同步设置失败，当前驱动未接受此设置";
        std::cerr << "[Renderer] VSync: " << SDL_GetError() << std::endl;
        saveManager->MutableSettings().vsync = vsync_;
        return;
    }
    vsync_ = enabled;
    saveManager->MutableSettings().vsync = enabled;
    displayError_.clear();
}

void Engine::ResetSettingsSection(GameUI::SettingsSection section) {
    const GameSettings defaults;
    const auto actions = BuildActions();
    switch (section) {
    case GameUI::SettingsSection::InputDisplay:
        actions.setMouseSensitivity(defaults.mouseSensitivity);
        saveManager->MutableSettings().botDifficulty = defaults.botDifficulty;
        actions.setTargetFPS(defaults.targetFPS);
        actions.setVSync(defaults.vsync);
        actions.setFullscreen(defaults.fullscreen);
        actions.setResolution(kResolutions[defaults.resIdx].w, kResolutions[defaults.resIdx].h, defaults.resIdx);
        actions.setRenderScale(defaults.renderScalePct);
        break;
    case GameUI::SettingsSection::VisionScope:
        actions.setFogOpacity(defaults.fogOpacityPct);
        actions.setSniperDelayedScopeClose(defaults.sniperDelayedScopeClose);
        actions.setScopeSensitivity(defaults.scopeSensitivity);
        actions.setScopeCrossThickness(defaults.scopeCrossThickness);
        actions.setScopeCrossDynamic(defaults.scopeCrossDynamic);
        break;
    case GameUI::SettingsSection::Crosshair:
        actions.setCrosshairColor(defaults.crossR, defaults.crossG, defaults.crossB, defaults.crossA);
        actions.setCrosshairThickness(defaults.crossThickness);
        actions.setCrosshairLength(defaults.crossLength);
        actions.setCrosshairGap(defaults.crossGap);
        actions.setCrosshairCenterDot(defaults.crossCenterDot);
        actions.setCrosshairDynamic(defaults.crossDynamic);
        break;
    case GameUI::SettingsSection::AimLine:
        actions.setAimLine(defaults.aimLine);
        actions.setAimLineStyle(defaults.aimLineStyle);
        break;
    case GameUI::SettingsSection::Radar:
        actions.setRadarPlayerCentered(defaults.radarPlayerCentered);
        actions.setRadarRotateWithPlayer(defaults.radarRotateWithPlayer);
        actions.setRadarBlendBg(defaults.radarBlendBg);
        actions.setRadarBgAlpha(defaults.radarBgAlpha);
        actions.setRadarHudSize(defaults.radarHudSize);
        actions.setRadarMapZoom(defaults.radarMapZoom);
        break;
    case GameUI::SettingsSection::Audio:
        audioManager->SetMasterVolume(AudioManager::DefaultMasterVolume);
        audioManager->SetMusicVolume(AudioManager::DefaultMusicVolume);
        audioManager->SetEffectsVolume(AudioManager::DefaultEffectsVolume);
        audioManager->SetMuted(false);
        actions.setMusicTrack(0);
        actions.setClickSound(0);
        SaveAudioConfig();
        break;
    case GameUI::SettingsSection::Bindings:
        saveManager->MutableSettings().bindings = defaults.bindings;
        inputManager->CancelCapture();
        inputManager->SetBindings(defaults.bindings);
        break;
    }
    saveManager->Save();
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
    cs2dOverlay.crosshair.aimLine = s.aimLine;
    cs2dOverlay.aimLineStyle = s.aimLineStyle;
    cs2dOverlay.crosshair.dynamic   = s.crossDynamic;

    // 雷达
    cs2dOverlay.radar.playerCentered   = s.radarPlayerCentered;
    cs2dOverlay.radar.rotateWithPlayer = s.radarRotateWithPlayer;
    cs2dOverlay.radar.blendBg          = s.radarBlendBg;
    cs2dOverlay.radar.bgAlpha          = s.radarBgAlpha;
    cs2dOverlay.radar.hudSize          = s.radarHudSize;
    cs2dOverlay.radar.mapZoom          = s.radarMapZoom;

    // 视野 / 开镜（契约 v1.2 / v1.3）
    if (cs2dRenderer) cs2dRenderer->SetFogOpacityPct(s.fogOpacityPct);
    if (cs2dGame) {
        cs2dGame->SetDelayedScopeClose(s.sniperDelayedScopeClose);
        cs2dGame->SetScopeCrosshair(s.scopeCrossThickness, s.scopeCrossDynamic);
    }

    // 显示
    targetFPS_     = std::max(0, s.targetFPS);
    currentResIdx_ = std::clamp(s.resIdx, 0, kResCount - 1);
}

// 按「窗口像素尺寸 + 渲染缩放」推导内部画布：高度锚定 1080，宽度按比例
// 效果：任何窗口比例都居中铺满、无黑边、无内容丢失；UI 按 1920x1080 基准等比缩放
void Engine::ResizeLogicalViewport(int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;
    viewportWindowW_ = winW;
    viewportWindowH_ = winH;

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

    {
        // 一次性诊断：窗口 / 逻辑画布 / 渲染输出尺寸（DPI 缩放下三者可能不同）
        static bool logged = false;
        if (!logged) {
            logged = true;
            int ow = 0, oh = 0;
            if (renderer) SDL_GetRendererOutputSize(renderer, &ow, &oh);
            std::cout << "[Viewport] window=" << winW << "x" << winH
                      << " output=" << ow << "x" << oh
                      << " logical=" << logicalW_ << "x" << logicalH_
                      << " renderScale=" << pct << "%" << std::endl;
        }
    }
}

// ============================================================================
// 主循环
// ============================================================================

void Engine::Run() {
    Uint64 previousTime = SDL_GetPerformanceCounter();
    float lag = 0.0f;
    const char* profilePath = std::getenv("PIXEL_FRAME_STATS");
    FILE* frameStats = profilePath ? std::fopen(profilePath, "w") : nullptr;
    const char* profileLimit = std::getenv("PIXEL_PROFILE_FRAMES");
    const int profileFrames = profileLimit ? std::atoi(profileLimit) : 0;
    int measuredFrames = 0;
    if (frameStats) std::fprintf(frameStats, "frame,input_ms,update_ms,render_ms,work_ms,interval_ms,fog_ms\n");

    while (isRunning) {
        const Uint64 frameStart = SDL_GetPerformanceCounter();
        const Uint64 currentTime = frameStart;
        const float elapsedTime = static_cast<float>((currentTime - previousTime) * 1000.0 / SDL_GetPerformanceFrequency());
        previousTime = currentTime;
        lag += elapsedTime;

        HandleInput();
        const Uint64 inputEnd = SDL_GetPerformanceCounter();

        // Bound catch-up work so a delayed frame cannot cause an update spiral.
        lag = std::min(lag, MS_PER_UPDATE * 2.0f);
        while (lag >= MS_PER_UPDATE) {
            Update(MS_PER_UPDATE / 1000.0f);
            lag -= MS_PER_UPDATE;
        }

        const Uint64 updateEnd = SDL_GetPerformanceCounter();
        PrepareGameplayCamera(elapsedTime / 1000.0f, lag / MS_PER_UPDATE);
        Render();
        const Uint64 renderEnd = SDL_GetPerformanceCounter();
        const double counterToMs = 1000.0 / SDL_GetPerformanceFrequency();
        const double workMs = (renderEnd - frameStart) * counterToMs;
        if (frameStats && currentState == PLAYING && !IsTransitionActive() && !sideChoicePending_) {
            std::fprintf(frameStats, "%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", measuredFrames++,
                (inputEnd - frameStart) * counterToMs, (updateEnd - inputEnd) * counterToMs,
                (renderEnd - updateEnd) * counterToMs, workMs, static_cast<double>(elapsedTime), cs2dRenderer ? cs2dRenderer->LastFogMilliseconds() : 0.0f);
            if (profileFrames > 0 && measuredFrames >= profileFrames) isRunning = false;
        }
        const double targetMs = targetFPS_ > 0 ? 1000.0 / targetFPS_ : 0.0;
        const double remainingMs = targetMs - workMs;
        if (targetFPS_ > 0 && remainingMs > 0.0) {
            SDL_Delay(static_cast<Uint32>(remainingMs));
            while ((SDL_GetPerformanceCounter() - frameStart) * counterToMs < targetMs)
                SDL_Delay(0);
        }
    }
    if (frameStats) std::fclose(frameStats);
}

// ============================================================================
// 清理
// ============================================================================

void Engine::Clean() {
    for (auto*& texture:showcaseTextures_) {if(texture) SDL_DestroyTexture(texture);texture=nullptr;}
    for (auto*& preview : settingsPreviewTex_) { if (preview) SDL_DestroyTexture(preview); preview = nullptr; }
#ifdef PIXEL_DEV_REPLAY
    CloseRecordFile();
#endif
    if (cleaned) return;
    cleaned = true;
    isRunning = false;

    inputManager->RestoreOriginalKeyboardLayout();

    if (saveManager) saveManager->Save();

    if (llmController_) { llmController_->Shutdown(); llmController_.reset(); }

    if (cs2dGame) cs2dGame->StopObjectiveAudio(audioManager.get());
    if (audioManager) audioManager->Clean();
    if (imGuiUI)      imGuiUI->Shutdown();
    if (cs2dRenderer) cs2dRenderer->Clean();
    if (menu3D)       { menu3D->Shutdown(); menu3D.reset(); }

    // 图标缓存必须先于 SDL_DestroyRenderer 释放（契约 v1 第 3 节）
    if (weaponIconCache_) { weaponIconCache_->Shutdown(); }

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
        if (SDL_SetWindowFullscreen(window, 0) != 0) {
            std::cerr << "[Window] Exit fullscreen failed: " << SDL_GetError() << std::endl;
            return;
        }
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
        // 桌面全屏的逻辑画布按桌面比例及渲染缩放计算。
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            std::cerr << "[Window] Enter fullscreen failed: " << SDL_GetError() << std::endl;
            return;
        }
        { int ow = 0, oh = 0; SDL_GetWindowSize(window, &ow, &oh);
          ResizeLogicalViewport(ow, oh); }
        isFullscreen = true;
    }

    saveManager->MutableSettings().fullscreen = isFullscreen;
}

// ============================================================================
// 输入
// ============================================================================

void Engine::HandleInput() {
    inputManager->SetBindings(saveManager->GetSettings().bindings);
    if (currentState != SETTINGS) {
        inputManager->CancelCapture();
    }
    inputManager->gameplayContext = currentState == PLAYING && !buyMenuOpen_ && !sideChoicePending_ && !sidePopupOpen_ && !IsTransitionActive();
    inputManager->uiConsumesMouse = ImGui::IsAnyItemHovered();
    inputManager->ProcessEvents(renderer);
    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    if (windowWidth != viewportWindowW_ || windowHeight != viewportWindowH_)
        ResizeLogicalViewport(windowWidth, windowHeight);
    if (inputManager->helpRequested) {
        auto& settings = saveManager->MutableSettings();
        settings.combatHintsVisible = !settings.combatHintsVisible;
    }
    const bool nativeFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (nativeFullscreen != isFullscreen) {
        isFullscreen = nativeFullscreen;
        saveManager->MutableSettings().fullscreen = isFullscreen;
    }

    // 契约 v1 第 4 节：右键 down 边沿只在这里采集，Update 里消费一次
    if (inputManager->IsMouseRightClicked()) scopeRightClickPending_ = true;

    if (inputManager->IsQuitRequested()) isRunning = false;
    if (inputManager->IsFullscreenToggleRequested()) ToggleFullscreen();
    if (inputManager->IsMuteToggleRequested()) audioManager->ToggleMute();

    const bool wheelOwnsEsc =
        (currentState == LOADOUT) ||
        (currentState == MAIN_MENU && focused3D_ >= 0) ||
        (currentState == PLAYING && buyMenuOpen_);

    if (!wheelOwnsEsc && inputManager->IsPauseToggleRequested() && (!IsTransitionActive() || sideChoicePending_)) {
        if (currentState == PLAYING) {
            if (sideChoicePending_ || sidePopupOpen_) sidePauseStartMs_ = SDL_GetTicks64();
            currentState = PAUSED;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            audioManager->PauseMusic();
            cs2dGame->SetObjectiveAudioPaused(true, audioManager.get());
        } else if (currentState == PAUSED) {
            if (sidePauseStartMs_) {
                sidePhaseStartMs_ += SDL_GetTicks64() - sidePauseStartMs_;
                sidePauseStartMs_ = 0;
            }
            // Esc 继续同样走黑场软切，与按钮「继续」保持一致。
            BeginTransition(PAUSED, PLAYING);
            currentState = PLAYING;
            SDL_SetRelativeMouseMode(sideChoicePending_ ? SDL_FALSE : SDL_TRUE);
            SDL_ShowCursor(sideChoicePending_ ? SDL_ENABLE : SDL_DISABLE);
            audioManager->ResumeMusic();
            cs2dGame->SetObjectiveAudioPaused(false, audioManager.get());
        } else if (currentState == SETTINGS) {
            saveManager->Save();
            SaveAudioConfig();
            currentState = settingsReturnState;
        } else if (currentState == MATCH_END) {
            // 结算页按 Esc 回主菜单（与其它页面同一套转场）
            currentState = MAIN_MENU;
            audioManager->PlayMusic(AudioManager::MusicTrackPath(audioManager->GetMusicTrack()));
        } else if (currentState == RESTART_CONFIRM || currentState == EXIT_CONFIRM) {
            currentState = currentState == EXIT_CONFIRM ? exitReturnState_ : PAUSED;
        } else if (currentState == MAP_SELECT || currentState == HISTORY) {
            currentState = MAIN_MENU;
        }
    }

    if (currentState != PLAYING) { cursorCaptured_ = false; return; }

    // 转场期间锁住对局输入，避免进入对局时第一帧误开枪/误移动。
    if (IsTransitionActive()) {
        cs2dMouseDown = false;
        cs2dKeyW = cs2dKeyA = cs2dKeyS = cs2dKeyD = false;
        cs2dKeyShift = cs2dKeyR = false;
        return;
    }
    if (sideChoicePending_ || sidePopupOpen_) {
        cs2dMouseDown = false;
        cs2dKeyW = cs2dKeyA = cs2dKeyS = cs2dKeyD = false;
        cs2dKeyShift = cs2dKeyR = false;
        return;
    }

    if (cursorSpawnVersion_ != cs2dGame->PlayerSpawnVersion()) {
        cursorSpawnVersion_ = cs2dGame->PlayerSpawnVersion();
        cs2dOverlay.InitializeCursor(cs2dGame->GetPlayer().aimAngle);
        cursorCaptured_ = false;
    }
    // ---- 买枪菜单 ----
    if (inputManager->IsBuyMenuToggleRequested()) {
        if (buyMenuOpen_) {
            CloseBuyMenu();
        } else if (cs2dGame->IsBuyPhase() && cs2dGame->IsPlayerInBuyZone()) {
            OpenBuyMenu();
        }
    }
    if (buyMenuOpen_ && (!cs2dGame->IsBuyPhase() || !cs2dGame->IsPlayerInBuyZone() || !inputManager->HasWindowFocus())) {
        CloseBuyMenu();   // 离开购买期 / 购买区 / 窗口失焦时强制关盘并清理状态
    }

    if (buyMenuOpen_) {
        // 菜单打开：绝对鼠标，允许点击；消费导航与数字键，不触发开火、切枪或丢枪
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        cursorCaptured_ = false;
        cs2dMouseDown = false;
        cs2dKeyW = cs2dKeyA = cs2dKeyS = cs2dKeyD = false;
        cs2dKeyShift = cs2dKeyR = false;
        showScoreboard_ = false;
        cs2dOverlay.showScoreboard = false;
        return;
    } else if (cs2dGame->GetPlayer().alive) {
        // 正常对局：相对鼠标瞄准
        SDL_SetRelativeMouseMode(SDL_TRUE);
        int relX = 0, relY = 0;
        SDL_GetRelativeMouseState(&relX, &relY);
        if (!cursorCaptured_) { relX = relY = 0; cursorCaptured_ = true; }
        SyncAimCursor();
        // 契约 v1.3：开镜时鼠标灵敏度再乘设置倍率（CS2 zoom_sensitivity_ratio 语义）
        const float scopeSensMul =
            (cs2dGame->GetScopeView().active && saveManager)
                ? saveManager->GetSettings().scopeSensitivity : 1.0f;
        const auto playerView = cs2dGame->GetActorView(cs2dGame->GetPlayer());
        cs2dOverlay.UpdateCursor(relX, relY,
                                 cs2dGame->GetMouseSensitivity() * scopeSensMul,
                                 cs2dRenderer->GetZoom(),
                                 cs2dRenderer->GetCameraX(), cs2dRenderer->GetCameraY(),
                                 logicalW_, logicalH_,
                                 cs2dGame->GetPlayer().x, cs2dGame->GetPlayer().y,
                                 cs2dWorldMouseX, cs2dWorldMouseY, cs2dGame->GetPlayer().aimKick,
                                 {playerView.x - cs2dGame->GetPlayer().x, playerView.y - cs2dGame->GetPlayer().y}, cs2dGame->GetPlayer().BallisticKick());
        cs2dGame->SetAimTarget(cs2dWorldMouseX, cs2dWorldMouseY, !cs2dOverlay.ScreenAnchorActive());
        cs2dMouseDown = inputManager->IsMouseLeftDown();

        if (inputManager->WasPressed(GameAction::Drop)) cs2dGame->DropCurrentWeapon();
        SDL_ShowCursor(SDL_DISABLE);
    }

    cs2dKeyW = inputManager->IsDown(GameAction::Forward);
    cs2dKeyA = inputManager->IsDown(GameAction::Left);
    cs2dKeyS = inputManager->IsDown(GameAction::Back);
    cs2dKeyD = inputManager->IsDown(GameAction::Right);
    cs2dKeyShift = inputManager->IsDown(GameAction::Stealth);
    cs2dKeyR = inputManager->IsDown(GameAction::Reload);
    showScoreboard_ = inputManager->IsDown(GameAction::Scoreboard);
    cs2dOverlay.showScoreboard = showScoreboard_;
    if (!buyMenuOpen_ && cs2dGame->IsSpectating()) {
        if (inputManager->WasPressed(GameAction::Fire)) cs2dGame->SpectatorCycle(-1);
        if (inputManager->WasPressed(GameAction::Scope)) cs2dGame->SpectatorCycle(1);
    }
    if (!buyMenuOpen_ && inputManager->WasPressed(GameAction::Spectate)) cs2dGame->RequestSpectateToggle();
    if (!buyMenuOpen_ && inputManager->WasPressed(GameAction::QuickSwitch)) cs2dGame->QuickSwitchWeapon();

    // 武器槽：1=主武器 2=副武器 3=近战 4=投掷物
    if (inputManager->IsDigit1Requested()) cs2dGame->SwitchWeaponSlot(0);
    if (inputManager->IsDigit2Requested()) cs2dGame->SwitchWeaponSlot(1);
    if (inputManager->IsDigit3Requested()) cs2dGame->SwitchWeaponSlot(2);
    if (inputManager->IsDigit4Requested()) cs2dGame->SwitchWeaponSlot(3);
    if (inputManager->WasPressed(GameAction::C4)) cs2dGame->ToggleHoldC4();

}

// ============================================================================
// 开始对局
// ============================================================================

void Engine::BeginSideMapPreview() {
    if (!cs2dGame) return;
    const CS2DWorld& w = cs2dGame->GetWorld();
    const float mapW = std::max(1.0f, w.GetMapPixelW());
    const float mapH = std::max(1.0f, w.GetMapPixelH());
    const float vw = static_cast<float>(logicalW_);
    const float vh = static_cast<float>(logicalH_);
    sideFullZoom_ = std::clamp(std::min(vw / mapW, vh / mapH) * 0.94f, 0.05f, 10.0f);
    sideFullCenterX_ = mapW * 0.5f;
    sideFullCenterY_ = mapH * 0.5f;
    sideZoomStart_ = sideFullZoom_;
    sideCamStartX_ = sideFullCenterX_;
    sideCamStartY_ = sideFullCenterY_;
    sideCamTargetX_ = sideFullCenterX_;
    sideCamTargetY_ = sideFullCenterY_;
    sideZoomTarget_ = std::clamp(std::min(vw, vh) / 200.0f, 1.2f, 6.0f);
    sidePhase_ = SidePhase::FullMap;
    sidePauseStartMs_ = 0;
    sidePhaseStartMs_ = SDL_GetTicks64();
    sideMatchInitialized_ = false;
}

void Engine::StartSideSpawnZoom() {
    if (!cs2dGame || !sideChoicePending_ || sidePhase_ != SidePhase::FullMap) return;
    cs2dGame->GetTeamSpawnCenter(pendingPlayerTeam_, sideCamTargetX_, sideCamTargetY_);
    sideCamStartX_ = sideFullCenterX_;
    sideCamStartY_ = sideFullCenterY_;
    sideZoomStart_ = sideFullZoom_;
    sidePhase_ = SidePhase::ZoomToSpawn;
    sidePauseStartMs_ = 0;
    sidePhaseStartMs_ = SDL_GetTicks64();
}

float Engine::SideFadeAlpha() const {
    switch (sidePhase_) {
        case SidePhase::FullMap:
        case SidePhase::None:
            return 0.0f;
        case SidePhase::ZoomToSpawn: {
            const float raw = std::clamp(
                static_cast<float>((sidePauseStartMs_ ? sidePauseStartMs_ : SDL_GetTicks64()) - sidePhaseStartMs_) / 900.0f,
                0.0f, 1.0f);
            return raw * raw * (3.0f - 2.0f * raw);
        }
        case SidePhase::DarkHold:
            return 1.0f;
        case SidePhase::Reveal: {
            const float raw = std::clamp(
                static_cast<float>((sidePauseStartMs_ ? sidePauseStartMs_ : SDL_GetTicks64()) - sidePhaseStartMs_) / 650.0f,
                0.0f, 1.0f);
            return 1.0f - raw * raw * (3.0f - 2.0f * raw);
        }
    }
    return 0.0f;
}

void Engine::DrawMatchShowcase() {
    if (!cs2dGame)
        return;
    if (!showcaseStarted_)
        showcaseStarted_ = SDL_GetTicks64();
    if (showcaseW_ != logicalW_ || showcaseH_ != logicalH_) {
        for (auto *&texture : showcaseTextures_) {
            if (texture)
                SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                        logicalW_, logicalH_);
            if (texture)
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        showcaseW_ = logicalW_;
        showcaseH_ = logicalH_;
    }
    const auto &world = cs2dGame->GetWorld();
    std::vector<SDL_FPoint> points;
    for (const auto &spawn : world.GetSpawnPoints())
        points.push_back({spawn.x, spawn.y});
    for (const auto &area : world.GetAreas())
        for (const auto &rect : area.rects)
            points.push_back({rect.x + rect.width * .5f, rect.y + rect.height * .5f});
    const float elapsed = (SDL_GetTicks64() - showcaseStarted_) * .001f;
    const float shotSeconds = ShowcaseCamera::kShotSeconds;
    const int shot = static_cast<int>(elapsed / shotSeconds);
    const float phase = std::fmod(elapsed, shotSeconds);
    const auto current = ShowcaseCamera::Sample(phase, shot, world.GetMapPixelW(), world.GetMapPixelH(),
                                                logicalW_, logicalH_, points);
    SDL_Texture *target = SDL_GetRenderTarget(renderer);
    SDL_Rect viewport, clip;
    SDL_RenderGetViewport(renderer, &viewport);
    SDL_RenderGetClipRect(renderer, &clip);
    const bool clipped = SDL_RenderIsClipEnabled(renderer);
    SDL_BlendMode blend;
    SDL_GetRenderDrawBlendMode(renderer, &blend);
    auto renderView = [&](int index, const ShowcaseCamera::View &view) {
        SDL_SetRenderTarget(renderer, showcaseTextures_[index]);
        SDL_RenderSetViewport(renderer, nullptr);
        SDL_RenderSetClipRect(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        cs2dGame->RenderMapPreview(logicalW_, logicalH_, view.center.x, view.center.y, view.zoom, true);
    };
    if (showcaseTextures_[0] && showcaseTextures_[1]) {
        renderView(0, current);
        if (shot > 0 && phase < 1.2f)
            renderView(1, ShowcaseCamera::Sample(phase + shotSeconds, shot - 1, world.GetMapPixelW(),
                                                 world.GetMapPixelH(), logicalW_, logicalH_, points));
        SDL_SetRenderTarget(renderer, target);
        SDL_RenderSetViewport(renderer, &viewport);
        SDL_RenderSetClipRect(renderer, clipped ? &clip : nullptr);
        if (shot > 0 && phase < 1.2f) {
            SDL_SetTextureAlphaMod(showcaseTextures_[1], 255);
            SDL_RenderCopy(renderer, showcaseTextures_[1], nullptr, nullptr);
            const float t = phase / 1.2f;
            SDL_SetTextureAlphaMod(showcaseTextures_[0], static_cast<Uint8>(255 * t * t * (3 - 2 * t)));
        } else
            SDL_SetTextureAlphaMod(showcaseTextures_[0], 255);
        SDL_RenderCopy(renderer, showcaseTextures_[0], nullptr, nullptr);
    } else
        cs2dGame->RenderMapPreview(logicalW_, logicalH_, current.center.x, current.center.y, current.zoom,
                                   true);
    SDL_SetRenderDrawBlendMode(renderer, blend);
}

void Engine::DrawSideMapPreview() {
    if (!cs2dGame) return;
    float cx = sideFullCenterX_;
    float cy = sideFullCenterY_;
    float zoom = sideFullZoom_;
    if (sidePhase_ == SidePhase::ZoomToSpawn) {
        const float raw = std::clamp(
            static_cast<float>((sidePauseStartMs_ ? sidePauseStartMs_ : SDL_GetTicks64()) - sidePhaseStartMs_) / 900.0f,
            0.0f, 1.0f);
        const float t = raw * raw * (3.0f - 2.0f * raw);
        cx = sideCamStartX_ + (sideCamTargetX_ - sideCamStartX_) * t;
        cy = sideCamStartY_ + (sideCamTargetY_ - sideCamStartY_) * t;
        zoom = sideZoomStart_ + (sideZoomTarget_ - sideZoomStart_) * t;
    } else if (sidePhase_ == SidePhase::DarkHold) {
        cx = sideCamTargetX_;
        cy = sideCamTargetY_;
        zoom = sideZoomTarget_;
    }
    cs2dGame->RenderMapPreview(logicalW_, logicalH_, cx, cy, zoom, false);
}

void Engine::UpdateSideSpawnTransition() {
    if (!sideChoicePending_ || !cs2dGame) return;
    const uint64_t now = SDL_GetTicks64();
    if (sidePhase_ == SidePhase::ZoomToSpawn) {
        if (now - sidePhaseStartMs_ >= 900) {
            cs2dGame->RestartMatchWithTeam(pendingPlayerTeam_);
            matchStartMs_ = static_cast<long long>(now);
            sideMatchInitialized_ = true;
            sidePhase_ = SidePhase::DarkHold;
            sidePhaseStartMs_ = now;
        }
    } else if (sidePhase_ == SidePhase::DarkHold) {
        if (now - sidePhaseStartMs_ >= 350) {
            sidePhase_ = SidePhase::Reveal;
            sidePhaseStartMs_ = now;
        }
    } else if (sidePhase_ == SidePhase::Reveal) {
        if (now - sidePhaseStartMs_ >= 650) {
            sidePhase_ = SidePhase::None;
            sideChoicePending_ = false;
            sidePopupOpen_ = false;
            sidePopupClosing_ = false;
            sidePopupAlpha_ = 0.0f;
            currentState = PLAYING;
            CloseBuyMenu();
            SDL_SetRelativeMouseMode(SDL_TRUE);
            SDL_ShowCursor(SDL_DISABLE);
        }
    }
}

void Engine::FinalizeSideChoice() {
    if (!sideChoicePending_ || !cs2dGame) return;

    sidePopupOpen_ = false;
    sidePopupClosing_ = false;
    sidePopupAlpha_ = 0.0f;
    sidePopupCloseElapsed_ = 0.0f;
    // 不立刻生成玩家/BOT：先把全图镜头放大到所选阵营出生点，黑屏后再重置对局。
    StartSideSpawnZoom();
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
}

void Engine::UpdateSidePopupFade(float dt) {
    if (!sidePopupOpen_) return;

    if (sidePopupClosing_) {
        constexpr float kFadeOut = 0.16f;
        sidePopupCloseElapsed_ += dt;
        sidePopupAlpha_ = std::clamp(1.0f - sidePopupCloseElapsed_ / kFadeOut,
                                     0.0f, 1.0f);
        if (sidePopupCloseElapsed_ >= kFadeOut) {
            FinalizeSideChoice();
        }
    } else {
        constexpr float kFadeIn = 0.18f;
        sidePopupAlpha_ = std::min(1.0f, sidePopupAlpha_ + dt / kFadeIn);
    }
}

void Engine::BeginMenuCameraTransition(std::function<void()> action) {
    if (!menu3D || !menu3D->IsReady() || menuCameraTransition_) {
        if (action) action();
        return;
    }
    menuCameraTransition_ = true;
    menuCameraStartMs_ = SDL_GetTicks64();
    pendingMenuAction_ = std::move(action);
    focused3D_ = -1;
    hovered3D_ = -1;
    focus3DClosing_ = false;
}

void Engine::OpenMapOverlay(MatchMode mode) {
    // 旧独立选图页不再进入，这里负责首次加载地图卡片预览图。
    LoadMapPreviews(renderer, mapPreviewTex_, kMapCatalogCount);
    mapOverlayMode_ = mode;
    mapOverlayCount_ = 0;
    mapOverlaySelected_ = 0;
    mapOverlayNames_.fill({});
    mapOverlayTaglines_.fill({});
    mapOverlayPaths_.fill({});
    mapOverlayPreviews_.fill(nullptr);
    mapOverlayCatalogIndex_.fill(-1);

    auto add = [&](const std::string& name, const std::string& tagline,
                   const std::string& path, void* preview, int catalogIndex) {
        if (mapOverlayCount_ >= 8) return;
        const int i = mapOverlayCount_++;
        mapOverlayNames_[i] = name;
        mapOverlayTaglines_[i] = tagline;
        mapOverlayPaths_[i] = path;
        mapOverlayPreviews_[i] = preview;
        mapOverlayCatalogIndex_[i] = catalogIndex;
    };

    if (mode == MatchMode::BombDefusal) {
        for (int i = 0; i < kMapCatalogCount; ++i) {
            add(kMapCatalog[i].name, kMapCatalog[i].tagline, kMapCatalog[i].tmj,
                mapPreviewTex_[i], i);
        }
    } else if (mode == MatchMode::TeamElimination) {
        for (int i = 0; i < kMapCatalogCount; ++i) {
            add(kMapCatalog[i].name, kMapCatalog[i].tagline, kMapCatalog[i].tmj,
                mapPreviewTex_[i], i);
        }
    } else {
        if (!officePreviewTex_) {
            const std::string preview = PathUtils::ResolveAppPath("assets/cs2d/tilesets/office.png");
            officePreviewTex_ = IMG_LoadTexture(renderer, preview.c_str());
            if (officePreviewTex_) {
                SDL_SetTextureBlendMode(officePreviewTex_, SDL_BLENDMODE_BLEND);
            }
        }
        add("办公室", "近距交火 · 室内攻防 · 快速接敌",
            "assets/cs2d/maps/office.tmj", officePreviewTex_, -1);
    }

    mapOverlayOpen_ = true;
    mapOverlayClosing_ = false;
    mapOverlayLaunching_ = false;
    mapOverlayLaunchProgress_ = 0.0f;
    mapOverlayStartMs_ = SDL_GetTicks64();
    mapOverlayFade_ = 0.0f;
    mapOverlayBeamFade_ = 0.0f;
    cameraBeamFade_ = 0.0f;
    // 相机过场期间光源已经移动到底部，浮层打开时保持在底部。
    mapOverlayLightProgress_ = 1.0f;
    mapPreviewRectValid_ = false;
    currentState = MAIN_MENU;
}

void Engine::RestartCurrentMatch() {
    if (!cs2dGame) return;

    // 重新开始 = 回到“进入游戏后的选阵营”阶段：
    // 不立刻重置对局；选完阵营后再重建首回合，保证回合数从 1 开始。
    if (currentMapRel_.empty()) {
        StartMatch(currentMatchMode_);
        return;
    }

    sideChoicePending_ = true;
    sidePopupOpen_ = false;   // 等转场进入纯黑窗口后再显示
    sidePopupClosing_ = false;
    sidePopupAlpha_ = 0.0f;
    sidePopupCloseElapsed_ = 0.0f;
    BeginSideMapPreview();
    matchLoadPhase_ = MatchLoadPhase::None;
    matchLoadElapsed_ = 0.0f;
    transitionKind_ = TransitionKind::None;
    transitionElapsed_ = 0.0f;
    CloseBuyMenu();
    showScoreboard_ = false;

    currentState = PLAYING;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
    cs2dOverlay.virtCursorY = logicalH_ * 0.5f;
}

void Engine::StartMatch(MatchMode mode) {
    // 模式 → 默认地图（爆破模式可由地图选择界面指定）
    //   死斗         → office（老图，节奏快）
    //   团队歼灭/爆破 → ancient_ruins（5v5 主战图）
    // 已经在某张地图里 → 重新开始沿用该地图，而不是回到默认地图
    if (!currentMapRel_.empty()) {
        StartMatchWithMap(mode, currentMapRel_, pendingPlayerTeam_);
        return;
    }

    const char* mapRel = "assets/cs2d/maps/ancient_ruins.tmj";
    switch (mode) {
        case MatchMode::DeathMatch:      mapRel = "assets/cs2d/maps/office.tmj"; break;
        case MatchMode::TeamElimination:
        case MatchMode::BombDefusal:     mapRel = "assets/cs2d/maps/ancient_ruins.tmj"; break;
        default:                         mapRel = "assets/cs2d/maps/ancient_ruins.tmj"; break;
    }
    StartMatchWithMap(mode, mapRel, pendingPlayerTeam_);
}

void Engine::StartMatchWithMap(MatchMode mode, const std::string& mapFile, Team playerTeam) {
    EnsureLlmInitialized();
    audioManager->StopMusic();
    currentMatchMode_ = mode;

    const std::string mapRel = mapFile;

    // 模式必须在 LoadMap 之前设定（LoadMap → ResetToNewMatch 依赖 mode_）
    cs2dGame->SetMatchMode(mode);

    currentMapRel_ = mapRel;

    // 阵营与赛前配装快照必须在 LoadMap 之前交付：LoadMap 会立刻生成玩家与初始手枪
    if (cs2dGame) {
        cs2dGame->SetAudioSink(audioManager.get());
        if (saveManager) cs2dGame->SetMatchLoadouts(saveManager->GetSettings().loadouts);
        cs2dGame->SetPlayerTeam(playerTeam);
    }

    const std::string mapPath = PathUtils::ResolveAppPath(mapRel);
    const bool deferMatchStart = sideChoicePending_;
    if (!cs2dGame->LoadMap(mapPath, cs2dRenderer.get(), !deferMatchStart)) {
        std::cerr << "CS2D 地图加载失败: " << mapPath << std::endl;
        return;
    }
    if (weaponIconCache_) {
        for (int id=1; id<WeaponDB::kWeaponCount; ++id) {
            const auto& weapon=WeaponDB::Get(static_cast<WeaponId>(id));
            weaponIconCache_->GetHud(weapon.iconPath);
            weaponIconCache_->GetBuy(weapon.iconPath);
        }
    }
    if (deferMatchStart) {
        BeginSideMapPreview();
    }



    // 记录本场地图 id 与开始时间（历史记录用）
    // 按地图目录匹配：路径里含该地图的 tmj 即认作该地图，避免逐张硬编码
    currentMapId_ = "ancient_ruins";
    for (int i = 0; i < kMapCatalogCount; ++i) {
        if (mapRel.find(kMapCatalog[i].tmj) != std::string::npos) {
            currentMapId_ = kMapCatalog[i].id;
            break;
        }
    }
    // 死斗用的 office 不在可选目录里，单独兜底
    if (currentMapId_ == "ancient_ruins" &&
        mapRel.find("office") != std::string::npos) {
        currentMapId_ = "office";
    }
    matchStartMs_ = static_cast<long long>(SDL_GetTicks64());

    currentState = PLAYING;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    // 调试装备（--loadout=）：延后到真正可操作的那一帧再给，避免被 LoadMap 重置
    if (!debugLoadout_.empty()) debugLoadoutPending_ = true;
    if (debugBuyMenu_) debugBuyMenuPending_ = true;
#ifdef PIXEL_DEV_REPLAY
    OpenRecordFileIfNeeded();
#endif
    cs2dOverlay.InitializeCursor(cs2dGame->GetPlayer().aimAngle);
    cursorCaptured_ = false;
    cs2dOverlay.UpdateCursor(0, 0, 0.0f, cs2dRenderer->GetZoom(),
        cs2dRenderer->GetCameraX(), cs2dRenderer->GetCameraY(), logicalW_, logicalH_,
        cs2dGame->GetPlayer().x, cs2dGame->GetPlayer().y, cs2dWorldMouseX, cs2dWorldMouseY);
    if (debugKillPlayer_) debugKillDelay_ = 2.0f;   // --killplayer：进对局 2s 后杀一次
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

// 调试装备：--loadout=awp / "AK-47" / 编号。仅用于验收与联调，不参与正常玩法。
void Engine::ApplyDebugLoadout() {
    if (debugLoadout_.empty() || !cs2dGame) return;

    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string want = lower(debugLoadout_);

    WeaponId id = WeaponId::None;
    for (int i = 1; i < WeaponDB::kWeaponCount; ++i) {
        const WeaponId wid = static_cast<WeaponId>(i);
        const WeaponDef& d = WeaponDB::Get(wid);
        if (d.name && lower(d.name) == want) { id = wid; break; }
    }
    if (id == WeaponId::None) {
        try {
            const int numeric = std::stoi(debugLoadout_);
            if (numeric > 0 && numeric < WeaponDB::kWeaponCount) {
                id = static_cast<WeaponId>(numeric);
            }
        } catch (...) {
        }
    }
    if (id == WeaponId::None) {
        std::cerr << "[Debug] 未知 --loadout: " << debugLoadout_ << std::endl;
        return;
    }

    CS2DGame::Player& p = cs2dGame->GetPlayer();
    p.money = 16000;
    const WeaponDef& def = WeaponDB::Get(id);
    switch (def.slot) {
        case WeaponSlotKind::Primary:
            p.SetSlotWeapon(0, id);
            p.weaponSlot = 0;
            break;
        case WeaponSlotKind::Secondary:
            p.SetSlotWeapon(1, id);
            p.weaponSlot = 1;
            break;
        case WeaponSlotKind::Melee:
            p.weapons[2] = id;
            break;
        case WeaponSlotKind::Grenade:
            if (p.grenadeCount < CS2DGame::Player::MAX_GRENADES) {
                p.grenades[p.grenadeCount++] = id;
                p.SyncActiveGrenade();
            }
            break;
    }
    p.LoadAmmoFromSlot();
    // 死斗：调试装备也记入"上一次购买"，这样重生后仍然沿用（验收用）
    cs2dGame->RememberCurrentLoadoutAsDm();
    std::cout << "[Debug] 调试装备: " << def.name
              << " (金钱 $" << p.money << ")" << std::endl;
}

#ifdef PIXEL_DEV_REPLAY
// ---------------------------------------------------------------------------
// 开发期输入录制：打开/关闭 journal（生产构建不编译）。
// 录制时固定随机种子 —— 否则回放无法复现；种子写进文件头。
// ---------------------------------------------------------------------------
void Engine::OpenRecordFileIfNeeded() {
    if (recordPath_.empty() || recordFile_) return;
    recordFile_ = std::fopen(recordPath_.c_str(), "wb");
    if (!recordFile_) {
        std::cerr << "[Record] 无法写入: " << recordPath_ << std::endl;
        return;
    }
    constexpr uint32_t kRecordSeed = 20261103u;
    if (cs2dGame) cs2dGame->SetRngSeed(kRecordSeed);
    recordTick_ = 0;
    std::fprintf(recordFile_,
                 "# PXINPUT3 map=%s seed=%u viewport=%dx%d aimTurn=0.00 aimErr=0.0 "
                 "reaction=0 tag=human\n",
                 currentMapId_.c_str(), kRecordSeed, logicalW_, logicalH_);
    std::cout << "[Record] 开始录制: " << recordPath_ << " (seed=" << kRecordSeed << ")"
              << std::endl;
}

void Engine::CloseRecordFile() {
    if (!recordFile_) return;
    std::fclose(recordFile_);
    recordFile_ = nullptr;
    std::cout << "[Record] 录制结束: " << recordTick_ << " tick" << std::endl;
}
#endif
void Engine::PrepareGameplayCamera(float dt, float interpolation) {
    if (!cs2dRenderer) return;
    cs2dRenderer->SetFrameDelta(dt);
    cs2dRenderer->FreezeCamera(currentState != PLAYING);
    if (cs2dGame && currentState == PLAYING && !IsTransitionActive() &&
        !sideChoicePending_ && !sidePopupOpen_ && !cs2dGame->CameraPrepared()) {
        cs2dGame->PrepareCamera(logicalW_, logicalH_, interpolation);
        SyncAimCursor(0.0f, true);
    }
}

void Engine::SyncAimCursor(float dt, bool commitAllowed) {
    if (!cs2dGame->GetPlayer().alive) return;
    const auto scope = cs2dGame->GetScopeView();
    const auto& player = cs2dGame->GetPlayer();
    const auto playerView = cs2dGame->GetActorView(player);
    const auto& world = cs2dGame->GetWorld();
    cs2dOverlay.SetReferenceWorldBounds(std::max(world.GetMapPixelW(), world.GetImageW()),
        std::max(world.GetMapPixelH(), world.GetImageH()));
    cs2dOverlay.SetReferenceMode(1.0f, Tuning::CameraLookShift);
    if (!cs2dOverlay.ScreenAnchorActive()) {
        cs2dOverlay.SetVisibleAimPoint(cs2dGame->GetRenderAimX(), cs2dGame->GetRenderAimY());
        cs2dOverlay.virtCursorX = (cs2dGame->GetRenderAimX() - cs2dRenderer->GetCameraX()) * cs2dRenderer->GetZoom();
        cs2dOverlay.virtCursorY = (cs2dGame->GetRenderAimY() - cs2dRenderer->GetCameraY()) * cs2dRenderer->GetZoom();
    }
    cs2dOverlay.SyncScopeCursor(scope.active, scope.level == 0,
        cs2dGame->CameraScopeTransitionComplete(), dt, commitAllowed);
    cs2dOverlay.UpdateCursor(0, 0, 0.0f, cs2dRenderer->GetZoom(),
        cs2dRenderer->GetCameraX(), cs2dRenderer->GetCameraY(), logicalW_, logicalH_,
        player.x, player.y, cs2dWorldMouseX, cs2dWorldMouseY, player.aimKick,
        {playerView.x - player.x, playerView.y - player.y}, player.BallisticKick());
    cs2dGame->SetAimTarget(cs2dWorldMouseX, cs2dWorldMouseY, !cs2dOverlay.ScreenAnchorActive());
    cs2dGame->SetCameraDistanceTier(cs2dOverlay.CameraDistanceTier());
}

void Engine::Update(float dt) {
    cs2dGame->SetBotDifficulty(saveManager->GetSettings().botDifficulty);
    if (currentState != PLAYING) {
        fireInputGate_.Block();
        // 暂停 / 设置 / 退出 / 主菜单：不跑 game.Update，开镜也必须立即归零
        scopeRightClickPending_ = false;
        if (cs2dGame) cs2dGame->ResetScope(Presentation::ScopeResetReason::InputBlocked);
        return;
    }
    UpdateSideSpawnTransition();
    if (sideChoicePending_ || sidePopupOpen_) {
        fireInputGate_.Block();
        // 选阵营期间同理：边沿丢弃 + 状态归零，不依赖 game.Update 的 dt
        scopeRightClickPending_ = false;
        if (cs2dGame) cs2dGame->ResetScope(Presentation::ScopeResetReason::InputBlocked);
        return;   // 等待游戏内选阵营
    }

    // E 键用于拆包（按住）
    const bool interactHeld = inputManager->IsDown(GameAction::Interact);

    // 调试装备只应用一次（验收截图用；正常启动不会走到这里）
    if (debugLoadoutPending_) {
        ApplyDebugLoadout();
        debugLoadoutPending_ = false;
    }
    if (debugBuyMenuPending_ && cs2dGame && cs2dGame->IsBuyPhase()) {
        OpenBuyMenu();
        debugBuyMenuPending_ = false;
    }
    // 调试死亡镜头（--killplayer）：进对局 2s 后按真实死亡流程杀一次玩家
    if (debugKillDelay_ >= 0.0f && cs2dGame) {
        debugKillDelay_ -= dt;
        if (debugKillDelay_ <= 0.0f) {
            // 周期性杀死玩家（每 8s 一次），方便验收截图总能落在死亡镜头窗口内
            debugKillDelay_ = 8.0f;
            cs2dGame->DebugKillPlayerForTest();
        }
    }

    // 开镜：每帧最多提交一次右键边沿（买枪/选边/失焦/转场/ImGui 捕获 mouse 时不允许）
    const bool scopeRightClicked = scopeRightClickPending_;
    scopeRightClickPending_ = false;
    const bool gameplayInputAllowed =
        currentState == PLAYING && cs2dGame &&
        !buyMenuOpen_ && !sideChoicePending_ && !sidePopupOpen_ &&
        !IsTransitionActive() &&
        inputManager->HasWindowFocus() &&
        !ImGui::GetIO().WantCaptureMouse;
#ifdef PIXEL_DEV_REPLAY
    if (scopeRightClicked && gameplayInputAllowed) recScopeToggle_ = true;
#endif
    cs2dGame->UpdateScopeInput(scopeRightClicked, gameplayInputAllowed);
    SyncAimCursor(dt);

    // Use fresh button state, not the aim input cached before the buy menu opened.
    const bool fireDown = fireInputGate_.Update(
        inputManager->IsMouseLeftDown(), gameplayInputAllowed);

#ifdef PIXEL_DEV_REPLAY
    if (recordFile_) {
        std::fprintf(recordFile_,
                     "%u %d %.9g %.9g %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                     recordTick_++, fireDown ? 1 : 0, cs2dWorldMouseX, cs2dWorldMouseY,
                     cs2dKeyW ? 1 : 0, cs2dKeyA ? 1 : 0, cs2dKeyS ? 1 : 0, cs2dKeyD ? 1 : 0,
                     cs2dKeyShift ? 1 : 0, cs2dKeyR ? 1 : 0, interactHeld ? 1 : 0,
                     recScopeToggle_ ? 1 : 0, recSwitchSlot_,
                     recBuyWeapon_, recBuyArmorTier_, cs2dOverlay.EdgeZoomRequested() ? 1 : 0,
                     cs2dOverlay.ScreenAnchorActive() ? 0 : 1);
        recScopeToggle_ = false;
        recSwitchSlot_ = 0;
        recBuyWeapon_ = 0;
        recBuyArmorTier_ = 0;
    }
#endif
    cs2dGame->Update(dt, fireDown, cs2dWorldMouseX, cs2dWorldMouseY,
                     cs2dKeyW, cs2dKeyA, cs2dKeyS, cs2dKeyD,
                     cs2dKeyShift, cs2dKeyR, interactHeld,
                     audioManager.get());
    SyncAimCursor();

#ifdef PIXEL_DEV_REPLAY
    // 开发期预览：PE_DEBUG_MATCH_END=1 时，进入对局 3 秒后强制结束比赛
    if (cs2dGame && getenv("PE_DEBUG_MATCH_END") && currentState == PLAYING &&
        !cs2dGame->IsMatchOver() && cs2dGame->GetRoundElapsed() > 3.0f) {
        cs2dGame->DebugForceMatchEnd(Team::CT);
    }
#endif

    // ---- 比赛结束：短暂停顿后进入结算页（不再自动弹回主菜单）----
    // 注意：这段必须放在 Update 里（HandleInput 里会被提前 return 掉），
    // 并且要在 cs2dGame->Update 之后，才能读到本帧刚累加的 matchOverTimer。
    if (cs2dGame->IsMatchOver()) {
        // 留 1.2s 给最后的击杀/拆包播报播完，再切结算页；
        // 切页本身走 Engine 的全局 0.9s 黑场软切（和其它页面一致）。
        constexpr float kSettleDelay = 1.2f;
        if (cs2dGame->GetMatchOverTimer() >= kSettleDelay && !cs2dGame->IsDeathFreeze()) {
            RecordMatchHistory(false);
            std::cout << "[CS2D] 比赛结束，进入结算页" << std::endl;
            CloseBuyMenu();
            cs2dGame->ResetScope(Presentation::ScopeResetReason::MatchExit);
            scopeRightClickPending_ = false;
            cs2dKeyW = cs2dKeyA = cs2dKeyS = cs2dKeyD = false;
            cs2dKeyShift = cs2dKeyR = false;
            cs2dMouseDown = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            currentState = MATCH_END;
            return;
        }
    }

    // LLM 只作为主教练使用（教练层在 CS2DGame::CoachUpdate 里调用），不再逐 BOT 轮询。
}

// ============================================================================
// 地图选择 → 加载 → 进入对局
// ============================================================================

void Engine::BeginMatchLoad(MatchMode mode, const std::string& mapRel, Team team) {
    pendingMatchMapRel_ = mapRel;
    pendingPlayerTeam_ = team;
    currentMatchMode_ = mode;
    sideChoicePending_ = true;
    sidePopupOpen_ = false;

    matchLoadPhase_ = MatchLoadPhase::Zoom;
    matchLoadElapsed_ = 0.0f;
    matchLoadMapStarted_ = false;
    matchZoomTex_ = pendingZoomPreviewTex_;
    pendingZoomPreviewTex_ = nullptr;
    matchZoomRectValid_ = mapPreviewRectValid_;

    if (mode == MatchMode::BombDefusal &&
        mapSelectIndex_ >= 0 && mapSelectIndex_ < kMapCatalogCount) {
        if (!matchZoomTex_) {
            matchZoomTex_ = static_cast<SDL_Texture*>(mapPreviewTex_[mapSelectIndex_]);
        }
    }
    if (matchZoomRectValid_) {
        for (int i = 0; i < 4; ++i) matchZoomRect_[i] = mapPreviewRect_[i];
    } else {
        // 没有回调到卡片矩形时，从屏幕中部的小窗口放大，避免直接硬切。
        matchZoomRect_[0] = logicalW_ * 0.36f;
        matchZoomRect_[1] = logicalH_ * 0.30f;
        matchZoomRect_[2] = logicalW_ * 0.64f;
        matchZoomRect_[3] = logicalH_ * 0.62f;
        matchZoomRectValid_ = true;
    }

    // 这里只是开始动画，真正 LoadMap/初始化对局放到黑屏阶段。
    transitionKind_ = TransitionKind::None;
    transitionElapsed_ = 0.0f;
    CloseBuyMenu();
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
}

void Engine::DrawMatchZoomOverlay() {
    if (matchLoadPhase_ != MatchLoadPhase::Zoom || !matchZoomRectValid_) return;

    const float raw = std::clamp(matchLoadElapsed_ / matchLoadZoomDuration_, 0.0f, 1.0f);
    const float ease = raw * raw * (3.0f - 2.0f * raw);
    const float W = static_cast<float>(logicalW_);
    const float H = static_cast<float>(logicalH_);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_Rect full = { 0, 0, logicalW_, logicalH_ };
    SDL_RenderFillRect(renderer, &full);

    if (matchZoomTex_) {
        const float x0 = matchZoomRect_[0] + (0.0f - matchZoomRect_[0]) * ease;
        const float y0 = matchZoomRect_[1] + (0.0f - matchZoomRect_[1]) * ease;
        const float x1 = matchZoomRect_[2] + (W - matchZoomRect_[2]) * ease;
        const float y1 = matchZoomRect_[3] + (H - matchZoomRect_[3]) * ease;

        SDL_FRect dst = { x0, y0, x1 - x0, y1 - y0 };
        SDL_SetTextureBlendMode(matchZoomTex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(matchZoomTex_, 255);
        SDL_RenderCopyF(renderer, matchZoomTex_, nullptr, &dst);
    }

    // 放大尾声压到全黑，给加载阶段留出纯黑底。
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(ease * 255.0f));
    SDL_RenderFillRect(renderer, &full);
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
        SDL_SetTextureScaleMode(sceneTex_[i], SDL_ScaleModeLinear);
    }
    return true;
}

void Engine::BeginTransition(GameState from, GameState to) {
    (void)from;
    (void)to;
    // 菜单/结算等所有普通页面统一使用同一套黑场软切。
    // 地图选择后的加载转场由 MatchLoadPhase 单独处理。
    transitionElapsed_ = 0.0f;
    transitionSlideDir_ = 1;
    transitionKind_ = TransitionKind::Fade;
    transitionDuration_ = 0.3f;
}

void Engine::DrawTransition(SDL_Texture* oldTex, SDL_Texture* newTex) {
    const int W = sceneW_;
    const int H = sceneH_;
    if (W <= 0 || H <= 0 || !newTex) return;

    const float raw = (transitionDuration_ > 0.0f)
        ? std::clamp(transitionElapsed_ / transitionDuration_, 0.0f, 1.0f)
        : 1.0f;
    const float ease = raw * raw * (3.0f - 2.0f * raw);   // smoothstep：首尾更柔

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
            // 先完全压到纯黑并保持一小段，再切到新页面；
            // 避免“还没黑透就露出下一页”。
            SDL_Rect full = { 0, 0, W, H };

            if (raw < kMenuFadeOutEnd) {
                drawScene(oldTex, 1.0f, 1.0f, 0.0f, 0.0f);
                const float t = raw / kMenuFadeOutEnd;
                const float a = t * t * (3.0f - 2.0f * t);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0,
                    static_cast<Uint8>(std::clamp(a, 0.0f, 1.0f) * 255.0f));
                SDL_RenderFillRect(renderer, &full);
            } else if (raw < kMenuFadeInStart) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderFillRect(renderer, &full);
            } else {
                drawScene(newTex, 1.0f, 1.0f, 0.0f, 0.0f);
                const float t = (raw - kMenuFadeInStart) / (1.0f - kMenuFadeInStart);
                const float a = 1.0f - t * t * (3.0f - 2.0f * t);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0,
                    static_cast<Uint8>(std::clamp(a, 0.0f, 1.0f) * 255.0f));
                SDL_RenderFillRect(renderer, &full);
            }
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

void Engine::RenderSettingsPreviews() {
    SDL_Texture* target=SDL_GetRenderTarget(renderer);
    SDL_Rect viewport,clip;
    SDL_RenderGetViewport(renderer,&viewport);
    SDL_RenderGetClipRect(renderer,&clip);
    const bool clipped=SDL_RenderIsClipEnabled(renderer)==SDL_TRUE;
    float sx,sy;
    SDL_RenderGetScale(renderer,&sx,&sy);
    SDL_BlendMode blend;
    SDL_GetRenderDrawBlendMode(renderer,&blend);
    Uint8 red, green, blue, alpha;
    SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha);
    const auto& settings=saveManager->GetSettings();
    for (int i=0;i<3;++i) {
        if (!settingsPreviewTex_[i]) {
            settingsPreviewTex_[i]=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,640,240);
            if (!settingsPreviewTex_[i]) continue;
            SDL_SetTextureBlendMode(settingsPreviewTex_[i],SDL_BLENDMODE_BLEND);
        }
        SDL_SetRenderTarget(renderer,settingsPreviewTex_[i]);
        SDL_RenderSetViewport(renderer,nullptr);
        SDL_RenderSetClipRect(renderer,nullptr);
        SDL_RenderSetScale(renderer,1,1);
        SDL_SetRenderDrawColor(renderer,18,22,28,255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        if (i==0) cs2dRenderer->DrawCrosshair(320,120,settings.crossR,settings.crossG,settings.crossB,settings.crossA,
            settings.crossThickness*g_uiScale,settings.crossCenterDot,settings.crossLength*g_uiScale,settings.crossGap*g_uiScale);
        if (i==1 && settings.aimLine) cs2dRenderer->DrawAimLinePreview(settings.aimLineStyle,640,240);
        if (i==2) {
            const auto& world=cs2dGame->GetWorld();
            if (world.GetMapPixelW() <= 0) LoadMapPreviews(renderer, mapPreviewTex_, kMapCatalogCount);
            const float width=std::max(800.0f,static_cast<float>(world.GetMapPixelW()));
            const float height=std::max(800.0f,static_cast<float>(world.GetMapPixelH()));
            const std::vector<CS2DRenderer::RadarDot> dots={{width*.45f,height*.4f,80,180,240,255},
                {width*.65f,height*.6f,230,80,70,255}};
            cs2dRenderer->DrawRadarHUD(width*.35f,height*.5f,-.6f,dots,width,height,
                settings.radarPlayerCentered,settings.radarRotateWithPlayer,settings.radarBlendBg,settings.radarBgAlpha,
                220,settings.radarMapZoom,true,640,240,false,
                world.GetMapPixelW() <= 0 ? static_cast<SDL_Texture*>(mapPreviewTex_[0]) : nullptr);
        }
    }
    SDL_SetRenderTarget(renderer,target);
    SDL_RenderSetScale(renderer,sx,sy);
    SDL_RenderSetViewport(renderer,&viewport);
    SDL_RenderSetClipRect(renderer,clipped ? &clip : nullptr);
    SDL_SetRenderDrawBlendMode(renderer,blend);
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
}

void Engine::Render() {

    const Uint64 nowTicks = SDL_GetTicks64();
    float frameDt = lastRenderTicks_ > 0
        ? static_cast<float>(nowTicks - lastRenderTicks_) / 1000.0f
        : (1.0f / 60.0f);
    lastRenderTicks_ = nowTicks;
    frameDt = std::clamp(frameDt, 0.0f, 0.05f);

    // 从对局/暂停回到大厅时，游戏音乐会停在 StartMatchWithMap 的 StopMusic。
    // 统一在大厅状态切换时补播菜单音乐；已在播放则不重启。
    if (currentState == MAIN_MENU && lastRenderedState_ != MAIN_MENU &&
        audioManager && !audioManager->IsMusicPlaying()) {
        audioManager->PlayMusic(AudioManager::MusicTrackPath(audioManager->GetMusicTrack()));
    }

    if (currentState == PLAYING) UpdateSidePopupFade(frameDt);

    // 进对局前的三段式：地图卡放大压黑 → 黑屏加载 → 游戏淡入
    if (matchLoadPhase_ == MatchLoadPhase::Zoom) {
        matchLoadElapsed_ += frameDt;
        if (matchLoadElapsed_ >= matchLoadZoomDuration_) {
            matchLoadPhase_ = MatchLoadPhase::Loading;
            matchLoadElapsed_ = 0.0f;
        }
    } else if (matchLoadPhase_ == MatchLoadPhase::Loading) {
        matchLoadElapsed_ += frameDt;
    } else if (matchLoadPhase_ == MatchLoadPhase::FadeIn) {
        matchLoadElapsed_ += frameDt;
        if (matchLoadElapsed_ >= matchLoadFadeDuration_) {
            matchLoadPhase_ = MatchLoadPhase::None;
            matchLoadElapsed_ = 0.0f;
            if (sideChoicePending_) {
                sidePopupOpen_ = true;
                sidePopupClosing_ = false;
                sidePopupAlpha_ = 0.0f;
                sidePopupCloseElapsed_ = 0.0f;
                // 弹窗需要绝对鼠标和可见光标
                SDL_SetRelativeMouseMode(SDL_FALSE);
                SDL_ShowCursor(SDL_ENABLE);
            }
        }
    }

    // 纯黑窗口前不要渲染新页面；旧页面快照继续显示。
    // 这样新页面的首帧刷新发生在 [kMenuFadeOutEnd, kMenuFadeInStart) 的纯黑阶段。
    const float menuProgress = (transitionDuration_ > 0.0f)
        ? std::clamp(transitionElapsed_ / transitionDuration_, 0.0f, 1.0f)
        : 1.0f;

    // 重新开始的选阵营弹窗也属于“下一页”：等转场进入纯黑段才打开/绘制。
    if (sideChoicePending_ && !sidePopupOpen_ &&
        transitionKind_ != TransitionKind::None &&
        menuProgress >= kMenuFadeOutEnd) {
        sidePopupOpen_ = true;
        sidePopupClosing_ = false;
        sidePopupAlpha_ = 0.0f;
        sidePopupCloseElapsed_ = 0.0f;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
    }

    const bool holdOldPage =
        (matchLoadPhase_ == MatchLoadPhase::None &&
         transitionKind_ == TransitionKind::Fade &&
         menuProgress < kMenuFadeOutEnd);

    // 记录本帧实际绘制的是哪个页面；按钮在绘制过程中触发切页时，
    // 下一帧才会用旧页面快照开始转场。
    const GameState renderedState = currentState;
    const bool sceneReady = EnsureSceneTextures(logicalW_, logicalH_);

    if (matchLoadPhase_ == MatchLoadPhase::None &&
        sceneReady && sceneValid_ && renderedState != lastRenderedState_) {
        BeginTransition(lastRenderedState_, renderedState);
    }
    if (matchLoadPhase_ == MatchLoadPhase::None &&
        transitionKind_ != TransitionKind::None) {
        transitionElapsed_ += frameDt;
        if (transitionElapsed_ >= transitionDuration_) {
            transitionKind_ = TransitionKind::None;
        }
    }

    const bool transitioning = IsTransitionActive();
    const bool matchLoading = (matchLoadPhase_ == MatchLoadPhase::Loading);
    if (sidePopupOpen_ && currentState == PLAYING) {
        // 弹窗期间持续保证绝对鼠标 + 可见光标（对局开始时会切到相对鼠标）
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
    }
    ImGuiIO& io = ImGui::GetIO();
    if (transitioning) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
    } else {
        io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard);
    }

    if (sceneReady) {
        SDL_SetRenderTarget(renderer, sceneTex_[sceneWrite_]);
    }
    if (matchLoading) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 18, 26, 36, 255);
    }
    SDL_RenderClear(renderer);

    const bool gameplayVisible = (currentState == PLAYING || currentState == PAUSED ||
                                  currentState == RESTART_CONFIRM ||
                                  currentState == MATCH_END ||
                                  currentState == EXIT_CONFIRM);

    if (gameplayVisible && !holdOldPage) {
        const bool sideMapPreview = sideChoicePending_ &&
            (sidePhase_ == SidePhase::FullMap ||
             sidePhase_ == SidePhase::ZoomToSpawn ||
             sidePhase_ == SidePhase::DarkHold);
        if (currentState==MATCH_END) {
            DrawMatchShowcase();
        } else if (sideMapPreview) {
            showcaseStarted_=0;
            DrawSideMapPreview();
        } else {
            showcaseStarted_=0;
            // 阵亡定格期内画面照常直播：镜头固定（PrepareCamera 提前返回），
            // 世界每帧重新渲染，BOT 走位/炸弹计时/伤害数字实时推进。
            cs2dGame->Render(logicalW_, logicalH_);
            if (!sideChoicePending_ && !cs2dGame->IsDeathFreeze()) {
                cs2dOverlay.Render(cs2dRenderer.get(), cs2dGame.get(), logicalW_, logicalH_);
            }
        }
    }

    if (currentState == SETTINGS) RenderSettingsPreviews();
    imGuiUI->BeginFrame();

    if (!holdOldPage) {
    if (matchLoading) {
        const bool llmWaiting =
            !llmInitialized_ || (llmController_ && !llmController_->IsReady());
        gameUI->DrawLoadingOverlay(
            llmWaiting ? "正在加载战术服务器..." : "正在准备战场...",
            llmWaiting ? "首次启动需要等待模型加载，请稍候"
                       : "正在同步地图、出生点与战术计划");
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
    } else if (currentState == MAIN_MENU || currentState == LOADOUT ||
        currentState == SETTINGS || currentState == PAUSED ||
        currentState == RESTART_CONFIRM || currentState == EXIT_CONFIRM) {
        GameUI::MenuCtx ctx;
        ctx.state              = currentState;
        ctx.settingsReturnState = settingsReturnState;
        ctx.exitToDesktop      = exitToDesktop;
        ctx.exitReturnState = exitReturnState_;
        ctx.settings = &saveManager->MutableSettings();
        for (size_t i=0;i<settingsPreviewTex_.size();++i) ctx.settingsPreviews[i]=settingsPreviewTex_[i];
        ctx.input = inputManager.get();
        ctx.masterVolume       = audioManager->GetMasterVolume();
        ctx.musicVolume        = audioManager->GetMusicVolume();
        ctx.effectsVolume      = audioManager->GetEffectsVolume();
        ctx.muted              = audioManager->IsMuted();
        ctx.musicTrack         = audioManager->GetMusicTrack();
        ctx.musicTrackCount    = AudioManager::kMusicTrackCount;
        for (int i = 0; i < AudioManager::kMusicTrackCount && i < 8; ++i) {
            ctx.musicTrackLabels[i] = AudioManager::MusicTrackName(i);
        }
        ctx.clickSound         = audioManager->GetClickSound();
        ctx.clickSoundCount    = AudioManager::kClickSoundCount;
        for (int i = 0; i < AudioManager::kClickSoundCount && i < 8; ++i) {
            ctx.clickSoundLabels[i] = AudioManager::ClickSoundName(i);
        }
        ctx.mouseSensitivity   = cs2dGame->GetMouseSensitivity();
        ctx.targetFPS          = targetFPS_;
        ctx.vsync              = vsync_;
        ctx.displayError       = displayError_;

        ctx.crosshairR         = cs2dOverlay.crosshair.r;
        ctx.crosshairG         = cs2dOverlay.crosshair.g;
        ctx.crosshairB         = cs2dOverlay.crosshair.b;
        ctx.crosshairA         = cs2dOverlay.crosshair.a;
        ctx.crosshairThickness = cs2dOverlay.crosshair.thickness;
        ctx.crosshairCenterDot = cs2dOverlay.crosshair.centerDot;
        ctx.crosshairLength    = cs2dOverlay.crosshair.length;
        ctx.crosshairGap       = cs2dOverlay.crosshair.gap;
        ctx.aimLine = cs2dOverlay.crosshair.aimLine;
        ctx.aimLineStyle = cs2dOverlay.aimLineStyle;
        ctx.crosshairDynamic   = cs2dOverlay.crosshair.dynamic;

        ctx.radarPlayerCentered   = cs2dOverlay.radar.playerCentered;
        ctx.radarRotateWithPlayer = cs2dOverlay.radar.rotateWithPlayer;
        ctx.radarBlendBg          = cs2dOverlay.radar.blendBg;
        ctx.radarBgAlpha          = cs2dOverlay.radar.bgAlpha;
        ctx.radarHudSize          = cs2dOverlay.radar.hudSize;
        ctx.radarMapZoom          = cs2dOverlay.radar.mapZoom;
        ctx.fogOpacityPct         = saveManager ? saveManager->GetSettings().fogOpacityPct : 100;
        ctx.sniperDelayedScopeClose =
            saveManager ? saveManager->GetSettings().sniperDelayedScopeClose : false;
        ctx.scopeSensitivity    = saveManager ? saveManager->GetSettings().scopeSensitivity : 1.0f;
        ctx.scopeCrossThickness = saveManager ? saveManager->GetSettings().scopeCrossThickness : 2.0f;
        ctx.scopeCrossDynamic   = saveManager ? saveManager->GetSettings().scopeCrossDynamic : false;

        ctx.resW         = currentResW_;
        ctx.resH         = currentResH_;
        ctx.resIdx       = currentResIdx_;
        ctx.renderScalePct = saveManager->GetSettings().renderScalePct;
        ctx.isFullscreen = isFullscreen;

        ctx.backdropTex = menuBgTex_;
        ctx.backdropW   = menuBgW_;
        ctx.backdropH   = menuBgH_;
        ctx.backdrop3D  = false;
        ctx.loadouts    = saveManager ? saveManager->GetSettings().loadouts : WeaponDB::DefaultMatchLoadouts();
        ctx.windowFocused = inputManager ? inputManager->HasWindowFocus() : true;

        if ((currentState == MAIN_MENU || currentState == LOADOUT) && menu3D && menu3D->IsReady()) {
            const ImVec2 menuViewport = ImGui::GetMainViewport()->WorkSize;
            menu3D->SetPresentationAspect(menuViewport.x / std::max(1.0f, menuViewport.y));
            if (mapOverlayOpen_ && mapOverlayLaunching_) {
                const uint64_t launchElapsed = SDL_GetTicks64() - mapOverlayLaunchStartMs_;
                mapOverlayLaunchProgress_ = std::clamp(
                    static_cast<float>(launchElapsed) /
                    std::max(1.0f, mapOverlayLaunchFadeMs_), 0.0f, 1.0f);
                if (static_cast<float>(launchElapsed) >= mapOverlayLaunchDelayMs_) {
                    mapOverlayOpen_ = false;
                    mapOverlayLaunching_ = false;
                    mapOverlayLaunchProgress_ = 0.0f;
                    mapOverlayFade_ = 0.0f;
                    mapOverlayBeamFade_ = 0.0f;
                    cameraBeamFade_ = 0.0f;
                    menu3D->SetCameraTopDownProgress(0.0f);
                    BeginMatchLoad(pendingOverlayMode_, pendingOverlayMapPath_,
                                   pendingPlayerTeam_);
                }
            }
            const bool cameraTransition = menuCameraTransition_;
            float cameraProgress = 0.0f;
            if (cameraTransition) {
                cameraProgress = std::clamp(
                    static_cast<float>(SDL_GetTicks64() - menuCameraStartMs_) / 900.0f,
                    0.0f, 1.0f);
                menu3D->SetCameraTopDownProgress(cameraProgress);
                menu3D->SetFocusedObject(-1);
                menu3D->SetHoveredObject(-1);
                mapOverlayLightProgress_ = cameraProgress;
                cameraBeamFade_ = 0.0f;
                menu3D->SetSpotlightIntensity(1.0f);
                menu3D->SetSpotlightSourceProgress(cameraProgress);
            } else if (mapOverlayOpen_) {
                menu3D->SetFocusedObject(-1);
                menu3D->SetHoveredObject(-1);
                if (mapOverlayClosing_) {
                    const float raw = std::clamp(
                        static_cast<float>(SDL_GetTicks64() - mapOverlayCloseStartMs_) / 900.0f,
                        0.0f, 1.0f);
                    const float t = raw * raw * (3.0f - 2.0f * raw);
                    menu3D->SetCameraTopDownProgress(1.0f - t);
                    menu3D->SetSpotlightIntensity(1.0f);
                    menu3D->SetSpotlightSourceProgress(1.0f - t);
                    mapOverlayFade_ = 1.0f - t;
                    mapOverlayBeamFade_ = 0.0f;
                    mapOverlayLightProgress_ = 1.0f - t;
                    cameraBeamFade_ = 0.0f;
                    if (raw >= 1.0f) {
                        mapOverlayOpen_ = false;
                        mapOverlayClosing_ = false;
                        mapOverlayFade_ = 0.0f;
                        mapOverlayBeamFade_ = 0.0f;
                        mapOverlayLightProgress_ = 0.0f;
                        menu3D->SetCameraTopDownProgress(0.0f);
                        menu3D->SetSpotlightIntensity(1.0f);
                        menu3D->SetSpotlightSourceProgress(0.0f);
                        menu3D->SetOverlayMouse(0.0f, 0.0f, false);
                    }
                } else {
                    menu3D->SetCameraTopDownProgress(1.0f);
                    menu3D->SetSpotlightIntensity(1.0f);
                    menu3D->SetSpotlightSourceProgress(1.0f);
                    mapOverlayBeamFade_ = 0.0f;
                    cameraBeamFade_ = 0.0f;
                    mapOverlayFade_ = std::clamp(
                        static_cast<float>(SDL_GetTicks64() - mapOverlayStartMs_) / 350.0f,
                        0.0f, 1.0f);
                    mapOverlayLightProgress_ = 1.0f;
                }
            } else {
                menu3D->SetCameraTopDownProgress(0.0f);
                menu3D->SetSpotlightIntensity(1.0f);
                menu3D->SetSpotlightSourceProgress(0.0f);
                menu3D->SetOverlayMouse(0.0f, 0.0f, false);
                cameraBeamFade_ = 0.0f;
                mapOverlayBeamFade_ = 0.0f;
                if (focused3D_ >= 0) {
                if (focus3DClosing_) {
                    const float elapsed = std::clamp(
                        static_cast<float>(SDL_GetTicks64() - focusExitStartMs_) / 380.0f,
                        0.0f, 1.0f);
                    focus3DProgress_ = focusCloseStartProgress_ * (1.0f - elapsed);
                    if (focus3DProgress_ <= 0.001f) {
                        focus3DProgress_ = 0.0f;
                        focus3DClosing_ = false;
                        focused3D_ = -1;
                        hovered3D_ = -1;
                        menu3D->SetFocusedObject(-1);
                        if (gameUI) gameUI->ResetChickenWheel();
                    }
                } else {
                    focus3DProgress_ = std::clamp(
                        static_cast<float>(SDL_GetTicks64() - focusAnimStartMs_) / 380.0f,
                        0.0f, 1.0f);
                }
                if (focused3D_ >= 0) {
                    menu3D->SetFocusProgress(focus3DProgress_);
                    menu3D->SetHoveredObject(-1);
                    menu3D->SetFocusedObject(focused3D_);
                }
            }
            if (focused3D_ < 0) {
                menu3D->SetHoveredObject(hovered3D_);
                menu3D->SetFocusedObject(-1);
            }
            }
            if (mapOverlayOpen_ && !mapOverlayClosing_) {
                const ImGuiIO& mio = ImGui::GetIO();
                const ImGuiViewport* mvp = ImGui::GetMainViewport();
                const float sw = std::max(1.0f, mvp->WorkSize.x);
                const float sh = std::max(1.0f, mvp->WorkSize.y);
                float u = (mio.MousePos.x - mvp->WorkPos.x) / sw;
                float v = (mio.MousePos.y - mvp->WorkPos.y) / sh;
                const float texAspect = static_cast<float>(menu3D->Width()) /
                                        static_cast<float>(menu3D->Height());
                const float scrAspect = sw / sh;
                if (texAspect > scrAspect) {
                    const float frac = scrAspect / texAspect;
                    u = (u - (1.0f - frac) * 0.5f) / frac;
                } else {
                    const float frac = texAspect / scrAspect;
                    v = (v - (1.0f - frac) * 0.5f) / frac;
                }
                menu3D->SetOverlayMouse(u, v, true);
            }
            menu3D->Render(frameDt);
            if (menu3D->GetTexture()) {
                ctx.backdropTex = menu3D->GetTexture();
                ctx.backdropW   = menu3D->Width();
                ctx.backdropH   = menu3D->Height();
                ctx.backdrop3D  = true;
            }

            ctx.focus3D     = (focused3D_ >= 0);
            ctx.menuScene = menu3D->SceneIndex();
            for (int i = 0; i < 3; ++i) ctx.menuScenePreviews[i] = menu3D->ScenePreview(i);
            ctx.focusObject = focused3D_;
            ctx.focusChicken = (focused3D_ >= 2);
            ctx.focusName   = (focused3D_ == 0) ? "恐怖分子 T"
                            : (focused3D_ == 1) ? "反恐精英 CT"
                            : "装饰小鸡";
            // 轮盘内容：按聚焦对象动态生成（T/CT 4 类，小鸡 3 类）
            if (menu3D) {
                ctx.focusCategoryCount = menu3D->FocusCategoryCount(focused3D_);
                for (int cat = 0; cat < 4; ++cat) {
                    ctx.focusCategoryNames[cat] = menu3D->FocusCategoryName(focused3D_, cat);
                    const int n = menu3D->FocusOptionCount(focused3D_, cat);
                    ctx.focusOptionCounts[cat] = n;
                    for (int opt = 0; opt < n && opt < 8; ++opt) {
                        ctx.focusOptions[cat][opt] =
                            menu3D->FocusOptionName(focused3D_, cat, opt);
                    }
                    ctx.focusSelected[cat] = menu3D->FocusSelected(focused3D_, cat);
                }
            }
            ctx.focusProgress = focus3DProgress_;
            ctx.cameraTransition = cameraTransition;
            ctx.mapOverlayOpen = mapOverlayOpen_;
            ctx.mapOverlayClosing = mapOverlayClosing_;
            ctx.mapOverlayLaunching = mapOverlayLaunching_;
            ctx.mapOverlayLaunchProgress = mapOverlayLaunchProgress_;
            ctx.mapOverlayFade = mapOverlayFade_;
            ctx.mapOverlayLightProgress = mapOverlayLightProgress_;
            ctx.mapOverlayBeamFade = mapOverlayBeamFade_;
            ctx.cameraBeamFade = cameraBeamFade_;
            ctx.mapOverlaySelected = mapOverlaySelected_;
            ctx.mapOverlayCount = mapOverlayCount_;
            if (mapOverlayMode_ == MatchMode::BombDefusal) {
                ctx.mapOverlayTitle = "选择地图";
                ctx.mapOverlaySubtitle = "爆破模式 · 选好战场后开始";
            } else if (mapOverlayMode_ == MatchMode::TeamElimination) {
                ctx.mapOverlayTitle = "选择地图";
                ctx.mapOverlaySubtitle = "团队歼灭 · 当前可用战场";
            } else {
                ctx.mapOverlayTitle = "选择地图";
                ctx.mapOverlaySubtitle = "死斗模式 · 当前可用战场";
            }
            for (int i = 0; i < 8; ++i) {
                ctx.mapOverlayNames[i] = mapOverlayNames_[i];
                ctx.mapOverlayTaglines[i] = mapOverlayTaglines_[i];
                ctx.mapOverlayPreviews[i] = mapOverlayPreviews_[i];
            }
        }

        if (currentState == LOADOUT) {
            gameUI->DrawLoadout(ctx, BuildActions());
        } else {
            gameUI->DrawMenu(ctx, BuildActions());
        }

        if (menuCameraTransition_ &&
            SDL_GetTicks64() - menuCameraStartMs_ >= 900) {
            menuCameraTransition_ = false;
            menu3D->SetCameraTopDownProgress(0.0f);
            std::function<void()> action = std::move(pendingMenuAction_);
            pendingMenuAction_ = nullptr;
            if (action) action();
        }

        if (currentState == MAIN_MENU && menu3D && menu3D->IsReady() &&
            !menuCameraTransition_ && !mapOverlayOpen_) {
            if (focused3D_ < 0) {
                const ImGuiIO& io = ImGui::GetIO();
                const ImGuiViewport* vp = ImGui::GetMainViewport();
                const float scrW = std::max(1.0f, vp->WorkSize.x);
                const float scrH = std::max(1.0f, vp->WorkSize.y);
                float u = (io.MousePos.x - vp->WorkPos.x) / scrW;
                float v = (io.MousePos.y - vp->WorkPos.y) / scrH;
                const float screenU = u;
                const float screenV = v;

                // 主菜单中央按钮列区域由 DrawMenu 实际布局结果同步，绝不穿透到 3D 物体。
                const bool overMenuButtons = mainMenuBtnRectValid_
                    ? (io.MousePos.x >= mainMenuBtnRect_[0] && io.MousePos.x <= mainMenuBtnRect_[2] &&
                       io.MousePos.y >= mainMenuBtnRect_[1] && io.MousePos.y <= mainMenuBtnRect_[3])
                    : (screenU > 0.36f && screenU < 0.64f && screenV > 0.30f && screenV < 0.72f);

                // 与 GameUI 的背景等比裁切一致，把屏幕坐标换算成 3D 纹理坐标
                const float texAspect = static_cast<float>(menu3D->Width()) /
                                        static_cast<float>(menu3D->Height());
                const float scrAspect = scrW / scrH;
                if (texAspect > scrAspect) {
                    const float frac = scrAspect / texAspect;
                    u = (u - (1.0f - frac) * 0.5f) / frac;
                } else {
                    const float frac = texAspect / scrAspect;
                    v = (v - (1.0f - frac) * 0.5f) / frac;
                }

                const bool uiHovered = ImGui::IsAnyItemHovered() || overMenuButtons ||
                    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
                int hit = -1;
                if (!uiHovered && u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                    hit = menu3D->HitTestNormalized(u, v);
                }
                hovered3D_ = hit;

                // Require a complete click in the lobby, not the press used to
                // activate/open the window. Dragging off a character cancels.
                static int pressedObject = -1;
                static int lastLobbyFrame = -2;
                static bool clickArmed = false;
                const int lobbyFrame = ImGui::GetFrameCount();
                if (lobbyFrame != lastLobbyFrame + 1 ||
                    !inputManager->HasWindowFocus()) {
                    pressedObject = -1;
                    clickArmed = false;
                }
                lastLobbyFrame = lobbyFrame;
                const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                if (clickArmed && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    pressedObject = (!uiHovered ? hit : -1);
                const bool selectObject = clickArmed && released && hit >= 0 &&
                    hit == pressedObject && !uiHovered;
                if (released) pressedObject = -1;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !released &&
                    inputManager->HasWindowFocus()) clickArmed = true;

                if (selectObject) {
                    focused3D_ = hit;
                    focusAnimStartMs_ = SDL_GetTicks64();
                    focus3DProgress_ = 0.0f;
                    focus3DClosing_ = false;
                    // 自定义结果已经是持久化的，这里不再重置外观
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    SDL_ShowCursor(SDL_ENABLE);
                }
            } else {
                hovered3D_ = -1;
            }
        }
    } else {
        GameUI::InGameCtx ictx;
        ictx.bindings = &saveManager->GetSettings().bindings;
        ictx.state    = currentState;
        const auto& death = cs2dGame->GetDeathPresentation();
        ictx.death.active = cs2dGame->IsDeathFreeze();
        ictx.death.remaining = death.remaining;
        ictx.death.killer = death.event.killer.empty() ? "环境" : death.event.killer;
        if (death.event.killer == "你") ictx.death.killer = "你（自伤）";
        ictx.death.killerTeam = death.event.killerTeam;
        const auto& deathWeapon = WeaponDB::Get(death.event.weapon);
        ictx.death.weapon = death.event.weapon == WeaponId::None ? "环境伤害" : deathWeapon.name;
        ictx.death.icon = death.event.weapon == WeaponId::None ? "" : deathWeapon.iconPath;
        ictx.bombSpectating = cs2dGame->GetSpectateMode() == CS2DGame::SpectateMode::Bomb;

        // 观战跟随 BOT 时，右下武器栏与左下血量/护甲显示被观战者的实时状态。
        const CS2DGame::Player* view = cs2dGame->GetSpectatedPlayer();
        const CS2DGame::Player& status = view ? *view : cs2dGame->GetPlayer();

        ictx.weaponName = cs2dGame->GetWeaponNameFor(status);
        auto addEquipment = [&](WeaponId id, int slot, int count, bool selected) {
            if (id == WeaponId::None)
                return;
            const auto &def = WeaponDB::Get(id);
            const GameAction action =
                slot == 4 ? GameAction::C4
                          : static_cast<GameAction>(static_cast<int>(GameAction::Slot1) + slot);
            ictx.equipment.push_back({def.name, def.iconPath,
                                      saveManager->GetSettings().bindings[action].Label(), count, selected, slot});
        };
        for (int slot = 0; slot < 3; ++slot)
            addEquipment(status.weapons[slot], slot, 1, status.weaponSlot == slot);
        for (int i = 0; i < status.grenadeCount; ++i) {
            bool seen = false;
            int count = 0;
            bool selected = false;
            for (int j = 0; j < status.grenadeCount; ++j) {
                if (status.grenades[j] != status.grenades[i])
                    continue;
                if (j < i)
                    seen = true;
                ++count;
                selected |= status.weaponSlot == 3 && status.grenadeSlot == j;
            }
            if (!seen)
                addEquipment(status.grenades[i], 3, count, selected);
        }
        addEquipment(cs2dGame->ActorHasC4(status) ? WeaponId::C4 : WeaponId::None, 4, 1,
                     status.weaponSlot == 4);

        ictx.mapName = currentMapRel_;
        for (const auto& map : kMapCatalog)
            if (currentMapRel_ == map.tmj) ictx.mapName = map.name;
        if (cs2dGame->IsDeathMatch()) ictx.mapName = "办公室";
        ictx.ammo     = status.ammo;
        const auto heldClass=status.CurrentWeapon().cls;
        ictx.magazineSize = heldClass==WeaponClass::Melee || heldClass==WeaponClass::Grenade || heldClass==WeaponClass::Bomb ? 0 : status.maxAmmo;
        ictx.reserveAmmo = status.reserve;
        ictx.reloading = status.alive && status.reloading;
        ictx.reloadProgress = cs2dGame->GetReloadProgress();
        ictx.dryFireHint = !view && status.alive && ictx.magazineSize > 0 && status.ammo == 0 &&
            status.reserve == 0 && cs2dGame->GetDryFireHintTimer() > 0;
        ictx.hasNoWeaponHint = ictx.reloading || ictx.dryFireHint;
        ictx.hintText = ictx.dryFireHint ? "没有备弹" : "换弹中...";
        ictx.totalPlayers    = cs2dGame->GetBotCount() + 1;
        ictx.kills           = view ? status.kills : cs2dGame->GetPlayerKills();
        ictx.deaths          = view ? status.deaths : cs2dGame->GetPlayerDeaths();

        ictx.scoreT           = cs2dGame->GetScoreT();
        ictx.scoreCT          = cs2dGame->GetScoreCT();
        ictx.roundNumber      = cs2dGame->GetRoundNumber();
        ictx.roundRemainingSec = static_cast<int>(cs2dGame->GetRoundRemaining());
        ictx.roundPhase = static_cast<int>(cs2dGame->GetRoundPhase());
        ictx.aliveT           = cs2dGame->AliveCount(Team::T);
        ictx.aliveCT          = cs2dGame->AliveCount(Team::CT);
        ictx.money            = status.money;
        ictx.armor            = status.armor;
        ictx.grenadeCount     = status.grenadeCount;
        ictx.health           = status.health;
        ictx.maxHealth        = status.maxHealth;
        ictx.helmet           = status.helmet;
        ictx.hasDefuseKit = status.team == Team::CT && status.hasDefuseKit;
        ictx.matchOver        = cs2dGame->IsMatchOver();
        ictx.matchWinnerIsCT  = (cs2dGame->GetMatchWinner() == Team::CT);
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
        ictx.defuseRemaining = cs2dGame->GetDefuseRemaining();
        ictx.defuserName = cs2dGame->GetDefuserName();
        // 信息隔离：只有与拆包者同阵营时才把进度交给 HUD；对手侧整行隐藏。
        ictx.defuseVisibleToViewer = cs2dGame->IsDefuseVisibleToViewer();

        // 击杀播报
        for (const auto& e : cs2dGame->GetKillFeed()) {
            ictx.killFeed.push_back({e.id, e.killer, e.victim, WeaponDB::Get(e.weapon).iconPath, e.killerTeam, e.victimTeam, e.life, e.personal});
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
        ictx.c4CarrierName = cs2dGame->GetFriendlyC4CarrierName();
        ictx.hasC4        = cs2dGame->PlayerHasC4();
        ictx.holdingC4    = cs2dGame->PlayerHoldingC4();
        ictx.bombDropped  = cs2dGame->IsBombDropped();
        ictx.bombIntel    = cs2dGame->HasBombIntel();
        ictx.plantProgress = cs2dGame->GetPlantProgress() / 3.0f;
        // 契约 v1 第 4、5 节：阵营与开镜状态由 Engine 填充，UI 只读
        // 修复：死斗一人成军，UI 不再按 T/CT 分队展示
        ictx.playerTeam = (currentMatchMode_ == MatchMode::DeathMatch)
                              ? Team::None : cs2dGame->GetPlayer().team;
        ictx.scope      = cs2dGame->GetScopeView();
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

        if (currentState == MATCH_END) {
            // 结算页：HUD 隐藏，只留压暗的对局画面 + 记分板
            GameUI::MatchEndCtx mctx;
            mctx.mapName  = currentMapRel_;
            const auto slash = mctx.mapName.find_last_of("/\\");
            if (slash != std::string::npos) mctx.mapName = mctx.mapName.substr(slash + 1);
            const auto dot = mctx.mapName.find_last_of('.');
            if (dot != std::string::npos) mctx.mapName = mctx.mapName.substr(0, dot);
            mctx.scoreT     = cs2dGame->GetScoreT();
            mctx.scoreCT    = cs2dGame->GetScoreCT();
            mctx.winner     = static_cast<int>(cs2dGame->GetMatchWinner());
            mctx.roundCount = cs2dGame->GetRoundNumber();
            const Team pt   = cs2dGame->GetPlayer().team;
            mctx.playerTeam = static_cast<int>(pt);
            mctx.playerTeamKnown = (pt == Team::T || pt == Team::CT);
            for (const auto& r : cs2dGame->BuildScoreboard()) {
                GameUI::InGameCtx::ScoreRowView v;
                v.name     = r.name;
                v.team     = static_cast<int>(r.team);
                v.kills    = r.kills;
                v.deaths   = r.deaths;
                v.money    = r.money;
                v.alive    = r.alive;
                v.isPlayer = r.isPlayer;
                mctx.rows.push_back(std::move(v));
            }
            gameUI->DrawMatchEnd(mctx, BuildActions());
        } else if (!sideChoicePending_ && !sidePopupOpen_) {
            gameUI->DrawInGame(ictx, imGuiUI.get());
        }
    }

    // 进入游戏后的阵营选择弹窗（游戏画面在背后，HUD 暂时隐藏）
    if (sidePopupOpen_ && currentState == PLAYING) {
        GameUI::SideSelectCtx sctx;
        // 契约 v1 第 5 节：爆破模式标记由实际 MatchMode 填充
        sctx.bombMode = (currentMatchMode_ == MatchMode::BombDefusal);
        sctx.mapName = "古代遗迹";
        for (int i = 0; i < kMapCatalogCount; ++i) {
            if (currentMapRel_.find(kMapCatalog[i].tmj) != std::string::npos ||
                std::string(kMapCatalog[i].tmj) == currentMapRel_) {
                sctx.mapName = kMapCatalog[i].name;
                break;
            }
        }
        gameUI->DrawSidePopup(sctx, BuildActions(), sidePopupAlpha_);
    }

    // 买枪菜单（覆盖在 HUD 之上）
    if (buyMenuOpen_ && currentState == PLAYING && !sidePopupOpen_ && !sideChoicePending_) {
        RenderBuyMenu();
    }
    }   // !holdOldPage

    if (currentState == PLAYING && cs2dGame->GetPlayer().alive && !sideChoicePending_ && !sidePopupOpen_) {
        gameUI->DrawDamageDirections(cs2dGame->GetDamageDirections());
    }
    if (saveManager->GetSettings().combatHintsVisible && currentState == PLAYING &&
        cs2dGame->GetPlayer().alive && !buyMenuOpen_ && !sideChoicePending_ && !sidePopupOpen_) {
        gameUI->DrawCombatHints(saveManager->GetSettings().bindings);
    }
    imGuiUI->EndFrame();

    if (sceneReady) {
        // 回到默认 render target：先清黑，再把旧/新场景合成到窗口。
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_Texture* oldTex = sceneValid_ ? sceneTex_[1 - sceneWrite_] : nullptr;
        DrawTransition(oldTex, sceneTex_[sceneWrite_]);

        if (matchLoadPhase_ == MatchLoadPhase::Zoom) {
            DrawMatchZoomOverlay();
        } else if (matchLoadPhase_ == MatchLoadPhase::FadeIn) {
            const float raw = (matchLoadFadeDuration_ > 0.0f)
                ? std::clamp(matchLoadElapsed_ / matchLoadFadeDuration_, 0.0f, 1.0f)
                : 1.0f;
            SDL_SetRenderDrawColor(renderer, 0, 0, 0,
                static_cast<Uint8>((1.0f - raw) * 255.0f));
            SDL_Rect full = { 0, 0, logicalW_, logicalH_ };
            SDL_RenderFillRect(renderer, &full);
        }
    }

    if (sideChoicePending_ && currentState == PLAYING) {
        const float a = SideFadeAlpha();
        if (a > 0.001f) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0,
                static_cast<Uint8>(std::clamp(a, 0.0f, 1.0f) * 255.0f));
            SDL_Rect full = { 0, 0, logicalW_, logicalH_ };
            SDL_RenderFillRect(renderer, &full);
        }
    }

    SDL_RenderPresent(renderer);

    lastRenderedState_ = renderedState;
    const bool holdTransitionSnapshot =
        (matchLoadPhase_ == MatchLoadPhase::None &&
         transitionKind_ != TransitionKind::None);
    if (sceneReady && !holdTransitionSnapshot) {
        sceneValid_ = true;
        sceneWrite_ ^= 1;
    }

    // 黑屏阶段结束后才真正加载地图/进入对局；blocking 加载期间画面保持黑屏。
    if (matchLoadPhase_ == MatchLoadPhase::Loading && !matchLoadMapStarted_) {
        if (matchLoadElapsed_ >= matchLoadMinHold_) {
            matchLoadMapStarted_ = true;
            EnsureLlmInitialized();
            StartMatchWithMap(currentMatchMode_, pendingMatchMapRel_, pendingPlayerTeam_);
            if (currentState == PLAYING) {
                matchLoadPhase_ = MatchLoadPhase::FadeIn;
                matchLoadElapsed_ = 0.0f;
                lastRenderedState_ = PLAYING;
            } else {
                matchLoadPhase_ = MatchLoadPhase::None;
            }
        }
    }


}

// ============================================================================
// 配置加载
// ============================================================================

void Engine::LoadAudioConfig() {
    const std::string path = PathUtils::ResolveWritablePath("audio_config.ini");
    IniFile ini = IniFile::Load(path);
    if (!ini.IsLoaded()) return;

    // 兼容旧格式：早期 audio_config.ini 没有 [audio] 分节，键直接写在顶层。
    const bool hasAudioSection = !ini.GetSection("audio").empty();
    auto readInt = [&](const char* key, int def) {
        return hasAudioSection ? ini.GetInt("audio", key, def) : ini.GetInt("", key, def);
    };
    auto readBool = [&](const char* key, bool def) {
        return hasAudioSection ? ini.GetBool("audio", key, def) : ini.GetBool("", key, def);
    };

    audioManager->SetMasterVolume (readInt("master",  AudioManager::DefaultMasterVolume));
    audioManager->SetMusicVolume  (readInt("music",    AudioManager::DefaultMusicVolume));
    audioManager->SetEffectsVolume(readInt("effects", AudioManager::DefaultEffectsVolume));
    audioManager->SetMuted        (readBool("muted", false));
    audioManager->SetMusicTrack   (readInt("music_track", 0));
    audioManager->SetClickSound   (readInt("click_sound", 0));

    // 旧格式迁移：立即用带 [audio] 分节的新格式重写一次
    if (!hasAudioSection) SaveAudioConfig();
}

void Engine::SaveAudioConfig() {
    if (!audioManager) return;
    IniFile ini;
    ini.SetInt ("audio", "master",      audioManager->GetMasterVolume());
    ini.SetInt ("audio", "music",       audioManager->GetMusicVolume());
    ini.SetInt ("audio", "effects",     audioManager->GetEffectsVolume());
    ini.SetBool("audio", "muted",       audioManager->IsMuted());
    ini.SetInt ("audio", "music_track", audioManager->GetMusicTrack());
    ini.SetInt ("audio", "click_sound", audioManager->GetClickSound());
    const std::string path = PathUtils::ResolveWritablePath("audio_config.ini");
    ini.Save(path);
}

// 买枪菜单：由 WeaponDB 生成快照，点击回调走 ExecutePurchase（契约 v1 第 3 节）
static const char* BuyCategoryLabel(WeaponClass cls) {
    switch (cls) {
        case WeaponClass::Pistol:     return "手枪";
        case WeaponClass::SMG:        return "冲锋枪";
        case WeaponClass::Rifle:      return "步枪";
        case WeaponClass::Sniper:     return "狙击枪";
        case WeaponClass::Shotgun:    return "霰弹枪";
        case WeaponClass::MachineGun: return "机枪";
        case WeaponClass::Grenade:    return "投掷物";
        case WeaponClass::Melee:      return "近战";
        case WeaponClass::ZeusGun:    return "手枪";
        case WeaponClass::Bomb:       return "任务物品";
    }
    return "";
}

void Engine::OpenBuyMenu() {
    buyMenuOpen_ = true;
    buyMenuOpenedThisFrame_ = true;
    if (gameUI) gameUI->ResetBuyWheel();
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    cursorCaptured_ = false;
    fireInputGate_.Block();
    if (window && renderer) {
        const ImVec2 center = GameUI::ComputeBuyWheelCenter(
            ImVec2(0.0f, 0.0f),
            ImVec2(static_cast<float>(logicalW_), static_cast<float>(logicalH_)));
        int wx = 0, wy = 0;
        SDL_RenderLogicalToWindow(renderer, center.x, center.y, &wx, &wy);
        SDL_WarpMouseInWindow(window, wx, wy);
        ImGui::GetIO().MousePos = center;
    }
    SDL_GetRelativeMouseState(nullptr, nullptr);
}

void Engine::CloseBuyMenu() {
    if (!buyMenuOpen_) return;
    buyMenuOpen_ = false;
    buyMenuOpenedThisFrame_ = false;
    if (gameUI) gameUI->ResetBuyWheel();
    if (currentState == PLAYING && !sideChoicePending_ && !sidePopupOpen_ &&
        cs2dGame && cs2dGame->GetPlayer().alive) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_ShowCursor(SDL_DISABLE);
    }
    SDL_GetRelativeMouseState(nullptr, nullptr);
    cursorCaptured_ = false;
    fireInputGate_.Block();
}

void Engine::RenderBuyMenu() {
    GameUI::BuyMenuCtx bctx;
    bctx.closeBinding = saveManager->GetSettings().bindings[GameAction::Buy].Label();
    bctx.money  = cs2dGame->GetPlayerMoney();
    bctx.roundNumber = cs2dGame->GetRoundNumber();
    bctx.buyRemaining = std::max(0.0f, Tuning::BuyWindowSeconds - cs2dGame->GetRoundElapsed());
    if (cs2dGame->IsDeathMatch()) bctx.buyRemaining = -1;
    bctx.canBuy = true;

    const CS2DGame::Player& p = cs2dGame->GetPlayer();
    bctx.playerTeam = p.team;
    bctx.loadout = cs2dGame->LoadoutForTeam(p.team);
    bctx.windowFocused = inputManager ? inputManager->HasWindowFocus() : true;
    bctx.suppressNav = buyMenuOpenedThisFrame_;
    buyMenuOpenedThisFrame_ = false;
    bctx.onClose = [this] { CloseBuyMenu(); };
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

        // 新字段：唯一判定来源是 EvaluatePurchase（无副作用）
        it.request.kind   = Presentation::BuyKind::Weapon;
        it.request.weapon = wid;
        it.availability   = cs2dGame->EvaluatePurchase(it.request);
        it.owned          = (it.availability == Presentation::BuyCode::AlreadyEquipped);
        it.iconPath       = (def.iconPath ? def.iconPath : "");
        it.categoryLabel  = BuyCategoryLabel(def.cls);
        it.magazine       = def.magazine;
        it.automatic      = def.automatic;
        it.scopeSupported = def.scoped;

        switch (def.slot) {
            case WeaponSlotKind::Primary:   it.group = 0; break;
            case WeaponSlotKind::Secondary: it.group = 1; break;
            case WeaponSlotKind::Melee:     it.group = 2; break;
            case WeaponSlotKind::Grenade:   it.group = 3; break;
        }
        bctx.items.push_back(it);
    }

    // 护甲条目（武器侧用 kind=Armor；旧 weaponId 负数编码只保留作兼容）
    for (int tier = 1; tier <= 2; ++tier) {
        GameUI::BuyItem k;
        k.kind = 1;
        k.armorTier = tier;
        k.weaponId = -tier;
        k.name = (tier == 2) ? "护甲+头盔" : "护甲";
        k.price = (tier == 2) ? 1000 : 650;
        k.group = 4;
        k.affordable = (bctx.money >= k.price);
        k.request.kind = Presentation::BuyKind::Armor;
        k.request.weapon = WeaponId::None;
        k.request.armorTier = tier;
        k.availability = cs2dGame->EvaluatePurchase(k.request);
        k.owned = (k.availability == Presentation::BuyCode::AlreadyEquipped);
        k.categoryLabel = "护甲";
        k.iconPath = (tier == 2) ? "cs2d/sprites/armor_helmet_d.png" : "cs2d/sprites/armor_d.png";
        bctx.items.push_back(k);
    }

    if (p.team == Team::CT) {
        GameUI::BuyItem kit;
        kit.kind = 2;
        kit.weaponId = -3;
        kit.name = "拆包钳";
        kit.price = 400;
        kit.group = 4;
        kit.categoryLabel = "拆包 4 秒";
        kit.iconPath = "cs2d/sprites/defuse_kit_d.png";
        kit.request.kind = Presentation::BuyKind::DefuseKit;
        kit.affordable = bctx.money >= kit.price;
        kit.availability = cs2dGame->EvaluatePurchase(kit.request);
        kit.owned = kit.availability == Presentation::BuyCode::AlreadyEquipped;
        bctx.items.push_back(kit);
    }
    for (auto& item:bctx.items) {
        const auto refund=cs2dGame->EvaluateRefund(item.request);
        item.refundAmount=refund.amount;
        item.refundQuantity=refund.quantity;
        item.refundReason=refund.reason;
    }
    bctx.refund=[this](const Presentation::BuyRequest& request) { return cs2dGame->ExecuteRefund(request); };
    gameUI->DrawBuyMenu(bctx, [this](const Presentation::BuyRequest& r) {
        // 一帧最多一次：DrawBuyMenu 内部保证，这里只负责提交与返回真实结果
        const Presentation::BuyResult res = cs2dGame->ExecutePurchase(r, audioManager.get());
#ifdef PIXEL_DEV_REPLAY
        if (res.code == Presentation::BuyCode::Ok) {
            if (r.kind == Presentation::BuyKind::Armor) recBuyArmorTier_ = r.armorTier;
            else recBuyWeapon_ = static_cast<int>(r.weapon);
        }
#endif
        return res;
    });
}

void Engine::EnsureLlmInitialized() {
    if (llmInitialized_) return;
    llmInitialized_ = true;

    // --coach=rule 时不需要拉起 LLM，避免白白占用显存/启动时间。
    if (!coachLlmEnabled_) return;

    InitLLM();
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
        std::cout << "[LLM] 客户端就绪 (server: " << cfg.serverUrl
                  << (cfg.autoLaunch ? ", 自动拉起" : "")
                  << (llmController_->IsReady() ? "" : ", 服务加载中") << ")" << std::endl;

        // 预热：冷启动后的第一个请求要初始化 CUDA 图/预填充，经常超过教练等待时间。
        // 先发一个极小请求把首帧成本付掉，避免第一回合战术请求超时。
        llmController_->AskRaw("You are a warmup helper.", "ok", 1,
                               [](const std::string&) {});
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
