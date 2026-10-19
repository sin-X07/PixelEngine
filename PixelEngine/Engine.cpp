#include "Engine.hpp"
#include "Core/PathUtils.hpp"
#include "Core/Roles.hpp"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>

Engine::Engine() : isRunning(false), window(nullptr), renderer(nullptr) {
    audioManager = std::make_unique<AudioManager>();
    inputManager = std::make_unique<InputManager>();
    worldMap = std::make_unique<WorldMap>();
    physicsSystem = std::make_unique<PhysicsSystem>();
    particleSystem = std::make_unique<ParticleSystem>(gen);
    renderSystem = std::make_unique<RenderSystem>();
    uiRenderer = std::make_unique<UIRenderer>();
    imGuiUI = std::make_unique<ImGuiUI>();
    gameUI = std::make_unique<GameUI>();
    textConfig = std::make_unique<TextConfig>();
    textTweaker = std::make_unique<TextTweakerUI>();
    saveManager = std::make_unique<SaveManager>();

    cs2dRenderer = std::make_unique<CS2DRenderer>();
    cs2dGame     = std::make_unique<CS2DGame>();

    gameUI->SetTextConfig(textConfig.get());
    gameUI->SetImGuiUI(imGuiUI.get());

    enemyManager = std::make_unique<EnemyManager>(
        player, *worldMap, *physicsSystem, *particleSystem,
        audioManager.get(), renderSystem.get(), gen,
        [this](int amount) { DamagePlayer(amount); });

    combatSystem = std::make_unique<CombatSystem>(
        player, enemyManager->monsters, enemyManager->bullets, enemyManager->weaponDrops,
        *worldMap, *physicsSystem, *particleSystem,
        audioManager.get(), gen, inputManager.get(), renderSystem.get());

    enemyManager->SetCombatSystem(combatSystem.get());
}

GameUI::Actions Engine::BuildActions() {
    GameUI::Actions a;
    a.playClick = [this]{ audioManager->PlaySound(SoundEffect::CLICK); };
    a.goState = [this](GameState s){ currentState = s; };
    a.openSettings = [this](GameState ret){ settingsReturnState = ret; currentState = SETTINGS; };
    a.startGame = [this](GameMode m){ StartGame(m); };
    a.resumeGame = [this]{ currentState = PLAYING; audioManager->ResumeMusic(); };
    a.confirmExit = [this](bool toDesktop){ exitToDesktop = toDesktop; currentState = EXIT_CONFIRM; };
    a.quitApp = [this]{
        inputManager->ResetMovementKeys();
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        if (exitToDesktop) { isRunning = false; }
        else { currentState = MAIN_MENU; audioManager->ResumeMusic(); }
    };
    a.quitToDesktop = [this]{
        inputManager->ResetMovementKeys();
        SDL_SetRelativeMouseMode(SDL_FALSE);
        isRunning = false;
    };
    a.toggleMute = [this]{ audioManager->ToggleMute(); };
    a.setMasterVolume = [this](int v){ audioManager->SetMasterVolume(v); };
    a.setMusicVolume = [this](int v){ audioManager->SetMusicVolume(v); };
    a.setEffectsVolume = [this](int v){ audioManager->SetEffectsVolume(v); };
    a.setMouseSensitivity = [this](float s){ cs2dGame->SetMouseSensitivity(s); };
    a.setTargetFPS = [this](int fps){ targetFPS_ = fps; };
    a.selectRole = [this](int idx){ SelectRole(idx); };
    // 准星
    a.setCrosshairColor      = cs2dOverlay.MakeSetCrosshairColor();
    a.setCrosshairThickness   = cs2dOverlay.MakeSetCrosshairThickness();
    a.setCrosshairCenterDot   = cs2dOverlay.MakeSetCrosshairCenterDot();
    a.setCrosshairLength      = cs2dOverlay.MakeSetCrosshairLength();
    a.setCrosshairGap         = cs2dOverlay.MakeSetCrosshairGap();
    // 雷达
    a.setRadarPlayerCentered  = cs2dOverlay.MakeSetRadarPlayerCentered();
    a.setRadarRotateWithPlayer = cs2dOverlay.MakeSetRadarRotateWithPlayer();
    a.setRadarBlendBg         = cs2dOverlay.MakeSetRadarBlendBg();
    a.setRadarBgAlpha         = cs2dOverlay.MakeSetRadarBgAlpha();
    a.setRadarHudSize         = cs2dOverlay.MakeSetRadarHudSize();
    a.setRadarMapZoom         = cs2dOverlay.MakeSetRadarMapZoom();
    // 分辨率
    a.setResolution = [this](int w, int h, int idx){
        currentResW_ = w; currentResH_ = h; currentResIdx_ = idx;
        if (!isFullscreen) {
            // 防止窗口超出桌面可用区域（2560×1600 屏幕设 2560×1600 会因标题栏撑大）
            SDL_Rect usable;
            SDL_GetDisplayUsableBounds(0, &usable);
            if (SDL_RectEmpty(&usable)) { usable.x = 0; usable.y = 0; usable.w = w; usable.h = h; }
            const int clampedW = std::min(w, usable.w);
            const int clampedH = std::min(h, usable.h);
            SDL_SetWindowSize(window, clampedW, clampedH);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            logicalW_ = clampedW; logicalH_ = clampedH;
            SDL_RenderSetLogicalSize(renderer, logicalW_, logicalH_);
            if (cs2dRenderer) cs2dRenderer->SetViewportSize(logicalW_, logicalH_);
            if (renderSystem) renderSystem->SetViewportSize(logicalW_, logicalH_);
            if (uiRenderer) uiRenderer->SetViewportSize(logicalW_, logicalH_);
            cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
            cs2dOverlay.virtCursorY = logicalH_ * 0.5f;
        }
    };
    a.setFullscreen = [this](bool v){
    if (v != isFullscreen) {
        if (v) {
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            // 动态 FOV：按屏幕宽高比扩展逻辑视口，而不是拉伸 800×600
            int screenW = 0, screenH = 0;
            SDL_GetRendererOutputSize(renderer, &screenW, &screenH);
            // 以高度为基准，宽度按屏幕比例扩展
            const float aspect = (screenH > 0) ? static_cast<float>(screenW) / static_cast<float>(screenH) : (4.0f / 3.0f);
            logicalH_ = 600;
            logicalW_ = static_cast<int>(600.0f * aspect);
            SDL_RenderSetLogicalSize(renderer, logicalW_, logicalH_);
            if (cs2dRenderer) cs2dRenderer->SetViewportSize(logicalW_, logicalH_);
            if (renderSystem) renderSystem->SetViewportSize(logicalW_, logicalH_);
            if (uiRenderer) uiRenderer->SetViewportSize(logicalW_, logicalH_);
        } else {
            SDL_SetWindowFullscreen(window, 0);
            // 回到用户选定的分辨率，同样 clamp 到可用区域
            SDL_Rect usable;
            SDL_GetDisplayUsableBounds(0, &usable);
            if (SDL_RectEmpty(&usable)) { usable.x = 0; usable.y = 0; usable.w = currentResW_; usable.h = currentResH_; }
            const int clampedW = std::min(currentResW_, usable.w);
            const int clampedH = std::min(currentResH_, usable.h);
            SDL_SetWindowSize(window, clampedW, clampedH);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            logicalW_ = clampedW; logicalH_ = clampedH;
            SDL_RenderSetLogicalSize(renderer, logicalW_, logicalH_);
            if (cs2dRenderer) cs2dRenderer->SetViewportSize(logicalW_, logicalH_);
            if (renderSystem) renderSystem->SetViewportSize(logicalW_, logicalH_);
            if (uiRenderer) uiRenderer->SetViewportSize(logicalW_, logicalH_);
            cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
            cs2dOverlay.virtCursorY = logicalH_ * 0.5f;
        }
        isFullscreen = v;
    }
};
    return a;
}

Engine::~Engine() {
    Clean();
}

bool Engine::Initialize(const char* title, int width, int height) {
    cleaned = false;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        std::cerr << "SDL 初始化失败: " << SDL_GetError() << std::endl;
        return false;
    }

    // 初始化音频子系统
    audioManager->Initialize();
    audioManager->PlayMusic("assets/audio/menu.wav");

    // 恢复音频设置（音量 / 静音）；若文件缺失则使用 AudioManager 自带默认值
    {
        const std::string audioPath = PathUtils::ResolveAppPath("audio_config.ini");
        std::ifstream audioFile(audioPath);
        if (audioFile) {
            int master = 100, music = 25, effects = 100, muted = 0;
            std::string line;
            while (std::getline(audioFile, line)) {
                if (line.empty() || line[0] == ';') continue;
                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (k == "master") master = std::stoi(v);
                else if (k == "music") music = std::stoi(v);
                else if (k == "effects") effects = std::stoi(v);
                else if (k == "muted") muted = std::stoi(v);
            }
            audioManager->SetMasterVolume(master);
            audioManager->SetMusicVolume(music);
            audioManager->SetEffectsVolume(effects);
            if (muted) audioManager->SetMuted(true);
        }
    }

    // 创建窗口与硬件渲染器
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
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
    SDL_RenderSetLogicalSize(renderer, width, height);

    // 文字样式配置：读写分离，用户配置落盘到 exe 同级 text_config.ini（不受 CMake
    // POST_BUILD 覆盖）；assets/config/text.ini 仅作为首次运行的工厂模板。
    const std::string userCfgPath   = PathUtils::ResolveWritablePath("text_config.ini");
    const std::string templatePath  = PathUtils::ResolveAppPath("assets/config/text.ini");

    if (std::filesystem::exists(userCfgPath)) {
        textConfig->Load(userCfgPath);
    } else if (std::filesystem::exists(templatePath)) {
        // 首次运行：从模板拷贝一份到用户配置路径
        std::error_code ec;
        std::filesystem::copy_file(templatePath, userCfgPath, ec);
        textConfig->Load(userCfgPath);
    }
    // 两文件都不存在：保持 C++ 默认值，F2 面板首次保存时会写出

    // [global] font= 传给 ImGuiUI（仅启动生效）
    if (!textConfig->GlobalFontPath().empty()) {
        imGuiUI->SetFontPath(textConfig->GlobalFontPath());
    }

    // 初始化渲染与UI子系统
    if (!renderSystem->Initialize(renderer, width, height)) return false;
    if (!uiRenderer->Initialize(renderer)) return false;
    if (!imGuiUI->Initialize(window, renderer)) return false;
    renderSystem->SetViewportSize(logicalW_, logicalH_);
    uiRenderer->SetViewportSize(logicalW_, logicalH_);

    // CS2D 渲染器初始化（地图目录为 assets/cs2d/maps/）
    cs2dRenderer->Initialize(renderer, "assets/cs2d/maps");

    // ImGui 后端消费原始 SDL 事件
    inputManager->SetEventCallback([this](const SDL_Event& e) {
        // 窗口物理尺寸与渲染逻辑尺寸（800x600）不一致时（全屏/缩放），把鼠标移动
        // 事件的坐标从窗口像素换算成逻辑坐标再交给 ImGui，保证悬停/点击命中与画面一致。
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Event converted = e;
            float logicalX = 0.0f;
            float logicalY = 0.0f;
            SDL_RenderWindowToLogical(renderer, e.motion.x, e.motion.y, &logicalX, &logicalY);
            converted.motion.x = static_cast<Sint32>(logicalX);
            converted.motion.y = static_cast<Sint32>(logicalY);
            ImGui_ImplSDL2_ProcessEvent(&converted);
        } else {
            ImGui_ImplSDL2_ProcessEvent(&e);
        }
    });

    // 血液粒子使用 RenderSystem 加载的血迹贴图
    particleSystem->SetBloodTexture(renderSystem->GetBloodTexture());

    // 默认加载随机迷宫地图与模式一地图
    MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
    if (!worldMap->LoadLevelFromFile("assets/maps/map1.txt", player.x, player.y, player.size)) {
        std::cerr << "警告：未找到地图 map1.txt" << std::endl;
        return false;
    }

    // 读取历史战绩与角色选择
    saveManager->LoadAll();
    selectedRole = saveManager->GetSelectedRole();
    renderSystem->SetActiveRole(selectedRole);

    // ---- LLM BOT 初始化 ----
    // 读取配置文件: 优先 exe 同目录的 llm.ini (CMake POST_BUILD 会从 assets/config 复制),
    // 若不存在则回退到源码 assets/config/llm.ini
    {
        std::string cfgPath = PathUtils::ResolveAppPath("assets/config/llm.ini");
        LLMConfig llmCfg = LLMConfig::FromFile(cfgPath);

        // 把相对路径转成绝对路径 (CreateProcess 需要; 相对基准是 exe 目录)
        if (!llmCfg.serverExePath.empty()) {
            std::string resolved = PathUtils::ResolveAppPath(llmCfg.serverExePath);
            if (!resolved.empty()) llmCfg.serverExePath = resolved;
        }
        if (!llmCfg.modelPath.empty()) {
            std::string resolved = PathUtils::ResolveAppPath(llmCfg.modelPath);
            if (!resolved.empty()) llmCfg.modelPath = resolved;
        }

        llmBot = std::make_unique<LLMBotController>();
        if (llmBot->Initialize(llmCfg)) {
            std::cout << "[LLM] LLM BOT 已就绪 (server: " << llmCfg.serverUrl
                      << (llmCfg.autoLaunch ? ", 自动拉起" : "") << ")" << std::endl;
        } else {
            std::cout << "[LLM] LLM 不可用, BOT 使用传统行为树" << std::endl;
        }
    }

    isRunning = true;
    return true;
}

void Engine::Run() {
    uint32_t previousTime = SDL_GetTicks64();
    float lag = 0.0f;

    while (isRunning) {
        uint32_t currentTime = SDL_GetTicks64();
        float elapsedTime = static_cast<float>(currentTime - previousTime);
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

void Engine::Clean() {
    if (cleaned) return;
    cleaned = true;
    isRunning = false;

    inputManager->RestoreOriginalKeyboardLayout();

    if (saveManager) {
        saveManager->SaveAll();
    }
    if (textConfig) {
        textConfig->Save(PathUtils::ResolveWritablePath("text_config.ini"));
    }

    // 持久化音频设置
    if (audioManager && audioManager->IsInitialized()) {
        std::ofstream audioFile(PathUtils::ResolveWritablePath("audio_config.ini"));
        if (audioFile) {
            audioFile << "; PixelEngine 音频设置（自动生成）\n";
            audioFile << "master="  << audioManager->GetMasterVolume()   << "\n";
            audioFile << "music="   << audioManager->GetMusicVolume()    << "\n";
            audioFile << "effects=" << audioManager->GetEffectsVolume()  << "\n";
            audioFile << "muted="   << (audioManager->IsMuted() ? 1 : 0) << "\n";
        }
    }

    if (audioManager) audioManager->Clean();
    if (renderSystem) renderSystem->Clean();
    if (uiRenderer) uiRenderer->Clean();
    if (imGuiUI) imGuiUI->Shutdown();
    if (llmBot) { llmBot->Shutdown(); llmBot.reset(); }

    if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }
    SDL_Quit();
    std::cout << "游戏引擎已安全关闭！" << std::endl;
}

void Engine::ToggleFullscreen() {
    if (!window) return;
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        // 退出全屏
        SDL_SetWindowFullscreen(window, 0);
        SDL_Rect usable;
        SDL_GetDisplayUsableBounds(0, &usable);
        if (SDL_RectEmpty(&usable)) { usable.x = 0; usable.y = 0; usable.w = currentResW_; usable.h = currentResH_; }
        const int clampedW = std::min(currentResW_, usable.w);
        const int clampedH = std::min(currentResH_, usable.h);
        SDL_SetWindowSize(window, clampedW, clampedH);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        logicalW_ = clampedW; logicalH_ = clampedH;
        SDL_RenderSetLogicalSize(renderer, logicalW_, logicalH_);
        if (cs2dRenderer) cs2dRenderer->SetViewportSize(logicalW_, logicalH_);
        if (renderSystem) renderSystem->SetViewportSize(logicalW_, logicalH_);
        if (uiRenderer) uiRenderer->SetViewportSize(logicalW_, logicalH_);
        cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
        cs2dOverlay.virtCursorY = logicalH_ * 0.5f;
        isFullscreen = false;
    } else {
        // 进入全屏：动态 FOV
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        int screenW = 0, screenH = 0;
        SDL_GetRendererOutputSize(renderer, &screenW, &screenH);
        const float aspect = (screenH > 0) ? static_cast<float>(screenW) / static_cast<float>(screenH) : (4.0f / 3.0f);
        logicalH_ = 600;
        logicalW_ = static_cast<int>(600.0f * aspect);
        SDL_RenderSetLogicalSize(renderer, logicalW_, logicalH_);
        if (cs2dRenderer) cs2dRenderer->SetViewportSize(logicalW_, logicalH_);
        if (renderSystem) renderSystem->SetViewportSize(logicalW_, logicalH_);
        if (uiRenderer) uiRenderer->SetViewportSize(logicalW_, logicalH_);
        // 重新居中虚拟准星以避免 shift
        cs2dOverlay.virtCursorX = logicalW_ * 0.5f;
        cs2dOverlay.virtCursorY = logicalH_ * 0.5f;
        isFullscreen = true;
    }
}

void Engine::DamagePlayer(int amount) {
    if (currentState != PLAYING) return;
    const int result = player.TakeDamage(amount);
    if (result == 0) return; // 无敌帧，未受伤

    particleSystem->TriggerScreenShake(0.2f, 4.0f);
    audioManager->PlaySound(SoundEffect::PLAYER_HURT);
    particleSystem->EmitBloodBurst(player.x + player.size * 0.5f, player.y + player.size * 0.5f, 10);

    if (result == 2) {
        currentState = GAMEOVER;
        gameOverStartTime = SDL_GetTicks();

        audioManager->PlaySound(SoundEffect::PLAYER_DEATH);
        particleSystem->TriggerScreenShake(0.35f, 6.0f);
        particleSystem->EmitDebrisBurst(player.x + player.size * 0.5f, player.y + player.size * 0.5f, 45, {220, 35, 35, 255});

        saveManager->AddScoreRecord(score, false);
        std::cout << "\n💀 被怪物击杀！挑战失败！最终得分: " << score << " 分！\n" << std::endl;
    }
}

// 切换当前选中角色：同步渲染系统与存档（越界索引由两侧钳制兜底）
void Engine::SelectRole(int index) {
    selectedRole = index;
    renderSystem->SetActiveRole(selectedRole);
    saveManager->SetSelectedRole(selectedRole);
    std::cout << "已选择角色: " << GetRoles()[selectedRole].name
              << " —— " << GetRoles()[selectedRole].tagline << std::endl;
}

void Engine::StartGame(GameMode mode) {
    currentMode = mode;
    score = 0;
    player.Reset();
    inputManager->ResetMovementKeys();
    particleSystem->Reset();
    renderSystem->ResetMovementBlends();
    audioManager->ResumeMusic();

    combatSystem->Reset();

    // 重置怪物与武器系统
    enemyManager->Reset();

    if (mode == TIME_ATTACK) {
        gameTimer = TIME_ATTACK_DURATION;
        if (!worldMap->LoadLevelFromFile("assets/maps/map1.txt", player.x, player.y, player.size)) return;

        // 初始宝箱随机刷新：清空地图预设固定宝箱，直接调用随机算法生成3个初始宝箱！
        worldMap->GetCoins().clear();
        worldMap->EnsureCoinCount(3, player.x, player.y, player.size, gen);

        // 生成不可破坏的地刺与老鼠夹陷阱（保证空间连通性）
        worldMap->GenerateTrapsForTimeAttack(player.x, player.y, player.size, gen);

        // 新增可破坏障碍，丰富地形
        worldMap->GenerateExtraObstaclesForTimeAttack(player.x, player.y, player.size, gen);
        std::cout << "模式 1: 限时挑战开始! 两分钟倒计时启动! 小心地刺、老鼠夹与怪物，去获得更多金币吧!" << std::endl;
    } else if (mode == CS2D_MODE) {
        gameTimer = 0.0f;
        gameOverStartTime = 0;

        // CS2D 模式关闭背景音乐（使用枪声音效系统）
        audioManager->StopMusic();

        // 加载 CS2D 地图（TMJ JSON 格式）
        const std::string cs2dMapPath = PathUtils::ResolveAppPath("assets/cs2d/maps/cs_office.tmj");
        if (!cs2dGame->LoadMap(cs2dMapPath, cs2dRenderer.get())) {
            std::cerr << "CS2D 地图加载失败: " << cs2dMapPath << std::endl;
            return;
        }

        inputManager->SwitchToEnglishKeyboardLayout();
        currentState = PLAYING;
        SDL_ShowCursor(SDL_DISABLE);
        SDL_SetRelativeMouseMode(SDL_TRUE);   // 相对鼠标模式，灵敏度由偏移量控制
        cs2dOverlay.virtCursorX = logicalW_ * 0.5f;  // 重置虚拟准星到屏幕中心
        cs2dOverlay.virtCursorY = logicalH_ * 0.5f;
        std::cout << "模式 3: CS2D 俯视角射击对抗开始!" << std::endl;
        return; // CS2D 有自己的主循环逻辑，跳过其余初始化
    } else {
        gameTimer = 0.0f;
        gameOverStartTime = 0;
        MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
        if (!worldMap->LoadLevelFromFile("map2.txt", player.x, player.y, player.size)) return;
        std::cout << "模式 2: 纯享迷宫模式开始! 探索吧!" << std::endl;
    }

    inputManager->SwitchToEnglishKeyboardLayout();
    currentState = PLAYING;
}

void Engine::HandleInput() {
    inputManager->ProcessEvents(renderer, currentState);

    if (inputManager->IsQuitRequested()) isRunning = false;
    if (inputManager->IsFullscreenToggleRequested()) ToggleFullscreen();
    if (inputManager->IsMuteToggleRequested()) audioManager->ToggleMute();
    if (inputManager->IsTweakerToggleRequested()) tweakerOpen = !tweakerOpen;

    if (inputManager->IsPauseToggleRequested()) {
        if (currentState == PLAYING) {
            currentState = PAUSED;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            inputManager->ResetMovementKeys();
            player.vx = player.vy = 0.0f;
            audioManager->PauseMusic();
        } else if (currentState == PAUSED) {
            currentState = PLAYING;
            audioManager->ResumeMusic();
        } else if (currentState == SETTINGS) {
            currentState = settingsReturnState;
        } else if (currentState == EXIT_CONFIRM) {
            currentState = PAUSED;
        } else if (currentState == MODE_SELECT || currentState == ROLE_SELECT ||
                   currentState == HISTORY || currentState == CONTROLS) {
            currentState = MAIN_MENU;
        } else if (currentState == GAMEOVER) {
            gameOverStartTime = 0;
            currentState = MODE_SELECT;
        } else {
            isRunning = false;
        }
    }

    if (currentState == MODE_SELECT) {
        if (inputManager->IsDigit1Requested()) StartGame(TIME_ATTACK);
        else if (inputManager->IsDigit2Requested()) StartGame(MAZE_MODE);
        else if (inputManager->IsDigit3Requested()) StartGame(CS2D_MODE);
    }

    // 角色选择页：数字键 1-4 快捷选中对应角色
    if (currentState == ROLE_SELECT) {
        if (inputManager->IsDigit1Requested()) SelectRole(0);
        else if (inputManager->IsDigit2Requested()) SelectRole(1);
        else if (inputManager->IsDigit3Requested()) SelectRole(2);
        else if (inputManager->IsDigit4Requested()) SelectRole(3);
    }

    if (currentState == GAMEOVER && inputManager->IsEnterOrSpaceRequested()) {
        gameOverStartTime = 0;
        MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
        worldMap->LoadLevelFromFile("map2.txt", player.x, player.y, player.size);
        player.vx = player.vy = 0.0f;
        currentState = MODE_SELECT;
    }

    if (currentState == PLAYING) {
        // CS2D 模式：采集输入状态，由 Update() 消费
        if (currentMode == CS2D_MODE) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            // 相对鼠标模式：用鼠标位移量 × 灵敏度控制虚拟准星
            int relX = 0, relY = 0;
            SDL_GetRelativeMouseState(&relX, &relY);
            cs2dOverlay.UpdateCursor(relX, relY,
                                     cs2dGame->GetMouseSensitivity(),
                                     cs2dRenderer->GetZoom(),
                                     cs2dRenderer->GetCameraX(), cs2dRenderer->GetCameraY(),
                                     logicalW_, logicalH_,
                                     cs2dWorldMouseX, cs2dWorldMouseY);
            cs2dMouseDown = inputManager->IsMouseLeftDown();

            const Uint8* kb = SDL_GetKeyboardState(nullptr);
            cs2dKeyW = kb[SDL_SCANCODE_W];
            cs2dKeyA = kb[SDL_SCANCODE_A];
            cs2dKeyS = kb[SDL_SCANCODE_S];
            cs2dKeyD = kb[SDL_SCANCODE_D];
            cs2dKeyShift = kb[SDL_SCANCODE_LSHIFT] || kb[SDL_SCANCODE_RSHIFT];
            cs2dKeyR = kb[SDL_SCANCODE_R];
            // Tab 边沿检测 → 中心雷达切换
            cs2dOverlay.prevTab = cs2dOverlay.keyTab;
            cs2dOverlay.keyTab = kb[SDL_SCANCODE_TAB];
            cs2dOverlay.UpdateTabToggle();

            // 武器槽切换：1=主武器, 2=副武器, 3=近战
            if (inputManager->IsDigit1Requested()) cs2dGame->SwitchWeaponSlot(0);
            if (inputManager->IsDigit2Requested()) cs2dGame->SwitchWeaponSlot(1);
            if (inputManager->IsDigit3Requested()) cs2dGame->SwitchWeaponSlot(2);

            // 准星模式：隐藏系统光标
            SDL_ShowCursor(SDL_DISABLE);
            return; // CS2D 使用自己的战斗系统，跳过下方 CombatSystem 逻辑
        }
        // 非 CS2D 模式：恢复绝对鼠标
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);

        combatSystem->UpdateAimAngle();

        // 武器切换：1 步枪 / 2 手枪 / 3 近战
        if (inputManager->IsDigit1Requested()) combatSystem->TrySwitchWeapon(CombatSystem::SLOT_RIFLE);
        if (inputManager->IsDigit2Requested()) combatSystem->TrySwitchWeapon(CombatSystem::SLOT_PISTOL);
        if (inputManager->IsDigit3Requested()) combatSystem->TrySwitchWeapon(CombatSystem::SLOT_MELEE);
        if (inputManager->IsFRequested()) combatSystem->HandlePickupOrDrop();

        // 攻击：步枪连发按住不放，其余单击 / 空格
        const Weapon* activeWeapon = combatSystem->GetActiveWeapon();
        bool wantAttack = false;
        if (activeWeapon->ranged && activeWeapon->automatic) {
            wantAttack = inputManager->IsMouseLeftDown();
        } else {
            wantAttack = inputManager->IsMouseLeftClicked() || inputManager->IsAttackRequested();
        }
        if (wantAttack) combatSystem->TryAttack();
    }
}

void Engine::Update(float dt) {
    // 特效需要在结算页继续推进，否则死亡时的血沫与震屏会永久停在首帧。
    particleSystem->Update(dt);

    if (currentState != PLAYING) return;

    // CS2D 模式：独立游戏逻辑
    if (currentMode == CS2D_MODE) {
        cs2dGame->Update(dt, cs2dMouseDown, cs2dWorldMouseX, cs2dWorldMouseY,
                         cs2dKeyW, cs2dKeyA, cs2dKeyS, cs2dKeyD,
                         cs2dKeyShift, cs2dKeyR,
                         audioManager.get());

        // ---- LLM BOT 思考间隔 ----
        if (llmBot && llmBot->IsReady()) {
            llmCooldownTimer_ -= dt;
            if (llmCooldownTimer_ <= 0.0f) {
                llmCooldownTimer_ = LLM_THINK_INTERVAL;
                RequestLLMDecision();
            }
        }

        return;
    }

    // 战斗冷却 / 攻击动画 / 未装配提示计时
    combatSystem->Update(dt);

    // 玩家无敌 / 回血计时
    player.UpdateTimers(dt);

    // 怪物/子弹/刷怪/压力/掉落/自动拾取
    enemyManager->Update(dt, currentMode == TIME_ATTACK);

    // 若本帧玩家被怪物击杀，提前结束本帧
    if (currentState != PLAYING) return;

    // 移动速度更新（Shift 冲刺 / Ctrl 聚焦），并应用武器移速惩罚
    const Uint8* keyboardState = SDL_GetKeyboardState(nullptr);
    const bool sprintHeld = keyboardState[SDL_SCANCODE_LSHIFT] || keyboardState[SDL_SCANCODE_RSHIFT];
    const bool focusHeld = keyboardState[SDL_SCANCODE_LCTRL] || keyboardState[SDL_SCANCODE_RCTRL];
    renderSystem->UpdateMovementBlends(dt, sprintHeld, focusHeld);

    const float walkMax = Player::MAX_SPEED - combatSystem->ComputeTotalWalkPenalty();
    const float sprintMaxSpeed = walkMax + (Player::MAX_SPEED - combatSystem->ComputeTotalSprintPenalty()) * renderSystem->GetSprintBlend();
    const float movementMaxSpeed = sprintMaxSpeed +
        (walkMax / 3.0f - sprintMaxSpeed) * renderSystem->GetFocusBlend();

    // 游戏倒计时更新
    if (currentMode == TIME_ATTACK) {
        gameTimer -= dt;
        if (gameTimer <= 0.0f) {
            gameTimer = 0.0f;
            player.diedByTrap = false;
            currentState = GAMEOVER;
            gameOverStartTime = SDL_GetTicks();

            saveManager->AddScoreRecord(score, true);
            std::cout << "\n🏁 时间到！挑战完成！最终得分: " << score << " 分！\n" << std::endl;
            return;
        }
    } else if (currentMode == MAZE_MODE) {
        gameTimer += dt;
    }

    // 玩家位移与碰撞推挤
    float oldX = player.x;
    float oldY = player.y;

    const int verticalInput = static_cast<int>(inputManager->IsDownPressed()) - static_cast<int>(inputManager->IsUpPressed());
    if (verticalInput != 0) {
        player.vy += verticalInput * Player::ACCEL * dt;
    } else {
        player.vy -= player.vy * Player::FRICTION * dt;
        if (std::abs(player.vy) < 1.0f) player.vy = 0.0f;
    }
    player.vy = std::clamp(player.vy, -movementMaxSpeed, movementMaxSpeed);
    player.y += player.vy * dt;

    for (const auto& wall : worldMap->GetWalls()) {
        if (PhysicsSystem::CheckCollision(player.x, player.y, player.size, wall.x, wall.y, wall.w, wall.h)) {
            player.y = oldY;
            player.vy = 0.0f;
            break;
        }
    }
    if (physicsSystem->CheckObstacleCollision(player.x, player.y, player.size, *worldMap)) {
        player.y = oldY;
        player.vy = 0.0f;
    }

    const int horizontalInput = static_cast<int>(inputManager->IsRightPressed()) - static_cast<int>(inputManager->IsLeftPressed());
    if (horizontalInput != 0) {
        player.vx += horizontalInput * Player::ACCEL * dt;
    } else {
        player.vx -= player.vx * Player::FRICTION * dt;
        if (std::abs(player.vx) < 1.0f) player.vx = 0.0f;
    }
    player.vx = std::clamp(player.vx, -movementMaxSpeed, movementMaxSpeed);
    player.x += player.vx * dt;

    for (const auto& wall : worldMap->GetWalls()) {
        if (PhysicsSystem::CheckCollision(player.x, player.y, player.size, wall.x, wall.y, wall.w, wall.h)) {
            player.x = oldX;
            player.vx = 0.0f;
            break;
        }
    }
    if (physicsSystem->CheckObstacleCollision(player.x, player.y, player.size, *worldMap)) {
        player.x = oldX;
        player.vx = 0.0f;
    }

    // 陷阱触碰致死判定（模式一：得分以当前得分为准，标记失败）
    if (currentMode == TIME_ATTACK) {
        int trapHitIdx = physicsSystem->CheckTrapCollision(player.x, player.y, player.size, *worldMap);
        if (trapHitIdx != -1) {
            const auto& trap = worldMap->GetTraps()[trapHitIdx];
            player.diedByTrap = true;
            currentState = GAMEOVER;
            gameOverStartTime = SDL_GetTicks();

            audioManager->PlaySound(SoundEffect::PLAYER_DEATH);
            particleSystem->TriggerScreenShake(0.35f, 6.0f);
            particleSystem->EmitDebrisBurst(player.x + player.size * 0.5f, player.y + player.size * 0.5f, 45, {220, 35, 35, 255});
            particleSystem->EmitExplosion(trap.x + trap.size * 0.5f, trap.y + trap.size * 0.5f, 25);

            saveManager->AddScoreRecord(score, false);
            std::cout << "\n💀 惨死！触发致命陷阱！挑战失败！最终得分: " << score << " 分！\n" << std::endl;
            return;
        }
    }

    // 收集宝藏金币
    int coinIdx = physicsSystem->CheckCoinPickup(player.x, player.y, player.size, *worldMap);
    if (coinIdx != -1) {
        score++;
        audioManager->PlaySound(SoundEffect::PICKUP);

        const auto coin = worldMap->GetCoins()[coinIdx];
        const float coinSize = worldMap->GetCoinSize();
        particleSystem->EmitExplosion(coin.x + coinSize * 0.5f, coin.y + coinSize * 0.5f, 30);

        if (currentMode == TIME_ATTACK) {
            std::cout << "💰 叮！吃到金币！当前得分: " << score << " 分！" << std::endl;
            worldMap->GetCoins().erase(worldMap->GetCoins().begin() + coinIdx);
            worldMap->ResetCoinPosition(player.x, player.y, player.size, gen);
        } else if (currentMode == MAZE_MODE) {
            worldMap->GetCoins().clear();
            currentState = GAMEOVER;
            gameOverStartTime = SDL_GetTicks();

            saveManager->AddTimeRecord(gameTimer);
            std::cout << "成功通关! 耗时: " << gameTimer << " 秒!" << std::endl;
            return;
        }
    }
}

void Engine::Render() {
    const bool gameplayVisible = (currentState == PLAYING || currentState == PAUSED ||
                                  currentState == EXIT_CONFIRM || currentState == GAMEOVER);
    SDL_FPoint shakeOffset = particleSystem->GetShakeOffset();
    renderSystem->UpdateCamera(player.x, player.y, player.size, *worldMap, gameplayVisible, shakeOffset.x, shakeOffset.y);

    SDL_SetRenderDrawColor(renderer, 20, 30, 40, 255);
    SDL_RenderClear(renderer);

    if (gameplayVisible && currentMode == CS2D_MODE) {
        // CS2D 独立渲染流程
        cs2dGame->Render(logicalW_, logicalH_);
        // 准星 / 雷达 / 计分板（独立模块）
        cs2dOverlay.Render(cs2dRenderer.get(), cs2dGame.get(), logicalW_, logicalH_);
    } else if (gameplayVisible) {
        renderSystem->DrawDungeonBackground(*worldMap, 0);
        renderSystem->DrawWorld(*worldMap, player.x, player.y, player.size,
                                combatSystem->attackAnimActive, combatSystem->aimAngle,
                                combatSystem->attackAnimTimer, CombatSystem::ATTACK_DURATION,
                                currentState == PLAYING);
        renderSystem->DrawWeaponDrops(enemyManager->weaponDrops);
        renderSystem->DrawMonsters(enemyManager->monsters);
        renderSystem->DrawBullets(enemyManager->bullets);
        particleSystem->Render(renderer, renderSystem->GetCameraX(), renderSystem->GetCameraY(), renderSystem->CurrentCameraZoom());

        if (currentMode == TIME_ATTACK) {
            renderSystem->DrawVisionFog(player.x, player.y, player.size, *worldMap);
        }
    }

    const float menuTime = static_cast<float>(SDL_GetTicks64()) / 1000.0f;

    // 菜单类状态：主菜单/模式选择/角色选择用视差动画背景，其余用纯色底
    if (currentState == MAIN_MENU || currentState == MODE_SELECT || currentState == ROLE_SELECT) {
        renderSystem->DrawAnimatedMenuBackground(*worldMap, menuTime);
    } else if (currentState == HISTORY || currentState == CONTROLS || currentState == SETTINGS) {
        SDL_Rect bg = {0, 0, logicalW_, logicalH_};
        SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
        SDL_RenderFillRect(renderer, &bg);
    }

    // 角色选择页：先在 ImGui 之下画 4 张角色立绘预览（与 GameUI 的卡片矩形严格对齐）
    if (currentState == ROLE_SELECT) {
        for (int i = 0; i < ROLE_COUNT; ++i) {
            const SDL_FRect card = GetRoleCardRect(i, static_cast<float>(logicalW_));
            // 预览区取卡片上部（下部留给按钮，见 GameUI 的 ROLE_SELECT 分支）
            const SDL_FRect previewArea = {card.x + 6.0f, card.y + 6.0f, card.w - 12.0f, card.h - 96.0f};
            renderSystem->DrawRolePreview(i, previewArea);
        }
    }

    if (currentState == PLAYING) {
        if (currentMode == CS2D_MODE) {
            // CS2D 模式 HUD：通过 ImGui InGameCtx 绘制血量/击杀等信息
        } else {
            uiRenderer->DrawHUD(currentMode, gameTimer, score, player.health, TIME_ATTACK_DURATION);

            const Weapon* currentWeapon = combatSystem->GetActiveWeapon();
            SDL_Texture* weaponIcon = renderSystem->GetWeaponIconTexture(currentWeapon->type);
            uiRenderer->DrawWeaponStatus(weaponIcon);
        }
    } else if (currentState == GAMEOVER) {
        uiRenderer->DrawGameOver(currentMode, player.diedByTrap, player.diedByMonster, score, gameTimer);
    }
    
    // ImGui 菜单（按钮/滑条由 ImGui 绘制并响应）
    const bool isImGuiState = (currentState == MAIN_MENU || currentState == MODE_SELECT ||
                               currentState == ROLE_SELECT ||
                               currentState == HISTORY || currentState == CONTROLS ||
                               currentState == SETTINGS || currentState == PAUSED ||
                               currentState == EXIT_CONFIRM);

    // ImGui 帧：每帧恰好开启一次（菜单态画菜单，游戏/结算态 DrawInGame 负责文字）
    imGuiUI->BeginFrame();
    if (isImGuiState) {
        GameUI::MenuCtx ctx;
        ctx.state = currentState;
        ctx.settingsReturnState = settingsReturnState;
        ctx.currentMode = currentMode;
        ctx.exitToDesktop = exitToDesktop;
        ctx.masterVolume = audioManager->GetMasterVolume();
        ctx.musicVolume = audioManager->GetMusicVolume();
        ctx.effectsVolume = audioManager->GetEffectsVolume();
        ctx.muted = audioManager->IsMuted();
        ctx.mouseSensitivity = cs2dGame->GetMouseSensitivity();
        ctx.targetFPS = targetFPS_;
        ctx.saves = saveManager.get();
        ctx.selectedRole = selectedRole;
        // 准星
        ctx.crosshairR = cs2dOverlay.crosshair.r; ctx.crosshairG = cs2dOverlay.crosshair.g;
        ctx.crosshairB = cs2dOverlay.crosshair.b; ctx.crosshairA = cs2dOverlay.crosshair.a;
        ctx.crosshairThickness = cs2dOverlay.crosshair.thickness;
        ctx.crosshairCenterDot = cs2dOverlay.crosshair.centerDot;
        ctx.crosshairLength     = cs2dOverlay.crosshair.length;
        ctx.crosshairGap        = cs2dOverlay.crosshair.gap;
        // 雷达
        ctx.radarPlayerCentered   = cs2dOverlay.radar.playerCentered;
        ctx.radarRotateWithPlayer = cs2dOverlay.radar.rotateWithPlayer;
        ctx.radarBlendBg          = cs2dOverlay.radar.blendBg;
        ctx.radarBgAlpha          = cs2dOverlay.radar.bgAlpha;
        ctx.radarHudSize          = cs2dOverlay.radar.hudSize;
        ctx.radarMapZoom          = cs2dOverlay.radar.mapZoom;
        // 分辨率
        ctx.resW = currentResW_; ctx.resH = currentResH_; ctx.resIdx = currentResIdx_;
        ctx.isFullscreen = isFullscreen;
        gameUI->DrawMenu(ctx, BuildActions());
    } else {
        GameUI::InGameCtx ictx;
        ictx.state = currentState;
        ictx.mode = currentMode;

        if (currentMode == CS2D_MODE) {
            ictx.gameTimer = 0.0f;
            ictx.score = cs2dGame->GetPlayerKills();
            ictx.diedByTrap = false;
            ictx.diedByMonster = false;
            ictx.weaponName = cs2dGame->GetWeaponName();
            ictx.ammo = cs2dGame->GetPlayer().ammo;
            ictx.magazineSize = (cs2dGame->GetPlayer().weaponSlot == 2) ? 0 : cs2dGame->GetPlayer().maxAmmo;
            ictx.hasWeaponIcon = true;
            ictx.hasNoWeaponHint = cs2dGame->GetPlayer().reloading;
            ictx.hintText = "换弹中...";
            ictx.cs2dTotal  = cs2dGame->GetBotCount() + 1;
            ictx.cs2dKills  = cs2dGame->GetPlayerKills();
            ictx.cs2dDeaths = cs2dGame->GetPlayerDeaths();
        } else {
            ictx.gameTimer = gameTimer;
            ictx.score = score;
            ictx.diedByTrap = player.diedByTrap;
            ictx.diedByMonster = player.diedByMonster;
            const Weapon* currentWeapon = combatSystem->GetActiveWeapon();
            ictx.weaponName = currentWeapon->Name();
            ictx.ammo = currentWeapon->ammo;
            ictx.magazineSize = currentWeapon->magazineSize;
            ictx.hasWeaponIcon = renderSystem->GetWeaponIconTexture(currentWeapon->type) != nullptr;
            ictx.hasNoWeaponHint = combatSystem->HasActiveNoWeaponHint();
            ictx.hintText = combatSystem->GetNoWeaponHintText();
        }
        gameUI->DrawInGame(ictx, imGuiUI.get());
    }

    // 文字调参面板（F2 开关，须在 EndFrame 前绘制）
    if (tweakerOpen && textTweaker) {
        textTweaker->Draw(*textConfig);
    }
    imGuiUI->EndFrame();

    SDL_RenderPresent(renderer);
    SDL_Delay(1000 / targetFPS_);
}

// ============================================================================
// LLM 决策请求 —— 采集第一个存活 BOT 的状态, 发送到 llama-server
// ============================================================================
void Engine::RequestLLMDecision() {
    if (!cs2dGame || !llmBot) return;
    const auto& bots = cs2dGame->GetBOTs();
    const auto& playerData = cs2dGame->GetPlayer();

    // 找一个存活的 BOT
    const CS2DGame::Player* targetBot = nullptr;
    for (const auto& b : bots) {
        if (b.alive) { targetBot = &b; break; }
    }
    if (!targetBot) return;  // 没有存活 BOT, 跳过

    // ---- 构建状态信息 ----
    float hp = static_cast<float>(targetBot->health);
    float maxHp = static_cast<float>(targetBot->maxHealth);
    float armor = 0.0f;  // 当前未实现护甲系统

    // 武器名
    std::string weaponName;
    if (targetBot->weaponSlot == 0)      weaponName = targetBot->weaponType == 0 ? "AK47" : "M4A1";
    else if (targetBot->weaponSlot == 1) weaponName = "手枪";
    else                                 weaponName = "近战";

    int ammo = targetBot->ammo;
    int maxAmmo = targetBot->maxAmmo;

    std::string area = "CS2D地图";

    // 敌人信息 = 玩家
    bool enemyVisible = false;
    float enemyDist = 0.0f;
    std::string enemyDir = "前方";
    bool enemyFiring = false;

    if (playerData.alive) {
        float dx = playerData.x - targetBot->x;
        float dy = playerData.y - targetBot->y;
        enemyDist = std::sqrt(dx * dx + dy * dy);

        // 视野半径检查
        if (enemyDist < CS2DGame::BOT_VIEW_RADIUS) {
            enemyVisible = true;

            // 方向
            if (std::abs(dx) > std::abs(dy)) {
                enemyDir = (dx > 0) ? "右前方" : "左前方";
            } else {
                enemyDir = (dy > 0) ? "正前方" : "";  // 下方通常不在视野
                if (enemyDir.empty()) enemyDir = (dx > 0) ? "右" : "左";
            }
        }
    }

    // 队友信息 = 其他存活 BOT
    bool teammateAlive = false;
    float teammateDist = 0.0f;
    std::string teammateDir = "后方";
    int aliveCount = 0;
    for (const auto& b : bots) {
        if (&b == targetBot) continue;
        if (b.alive) {
            aliveCount++;
            float dx = b.x - targetBot->x;
            float dy = b.y - targetBot->y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < CS2DGame::BOT_VIEW_RADIUS * 1.5f) {
                teammateAlive = true;
                teammateDist = d;
                teammateDir = (dx > 0) ? "右" : "左";
                teammateDir += (dy > 0) ? "前方" : "后方";
            }
        }
    }

    // 构建请求
    std::string userMsg = PromptBuilder::BuildUserMessage(
        hp, maxHp, armor, weaponName, ammo, maxAmmo,
        targetBot->x, targetBot->y, area,
        enemyVisible, enemyDist, enemyDir, enemyFiring,
        teammateAlive, teammateDist, teammateDir,
        targetBot->reloading);

    std::string requestBody = PromptBuilder::BuildRequestBody(userMsg);

    // 发送异步请求
    llmBot->Decide(requestBody, [this](const LLMDecision& d) {
        if (d.valid) {
            llmDecisionCache_ = d;
            llmDecisionFresh_ = true;
        }
    });

    // 调试输出 (每 10 秒一次)
    llmDebugTimer_ += LLM_THINK_INTERVAL;
    if (llmDebugTimer_ >= 10.0f) {
        llmDebugTimer_ = 0.0f;
        std::cout << "[LLM] 决策请求已发送, BOT(" << static_cast<int>(targetBot->health)
                  << "HP, " << weaponName << ")" << std::endl;
    }
}
