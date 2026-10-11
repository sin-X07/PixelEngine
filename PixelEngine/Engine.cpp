#include "Engine.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

Engine::Engine() : isRunning(false), window(nullptr), renderer(nullptr) {
    audioManager = std::make_unique<AudioManager>();
    inputManager = std::make_unique<InputManager>();
    worldMap = std::make_unique<WorldMap>();
    physicsSystem = std::make_unique<PhysicsSystem>();
    particleSystem = std::make_unique<ParticleSystem>(gen);
    renderSystem = std::make_unique<RenderSystem>();
    uiRenderer = std::make_unique<UIRenderer>();
    saveManager = std::make_unique<SaveManager>();
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
    audioManager->PlayMusic("../assets/audio/menu.wav");

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

    // 初始化渲染与UI子系统
    if (!renderSystem->Initialize(renderer, width, height)) return false;
    if (!uiRenderer->Initialize(renderer)) return false;

    // 默认加载随机迷宫地图与模式一地图
    MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
    if (!worldMap->LoadLevelFromFile("../assets/maps/map1.txt", playerX, playerY, playerSize)) {
        std::cerr << "警告：未找到地图 map1.txt" << std::endl;
        return false;
    }

    // 读取历史战绩
    saveManager->LoadAll();

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

    if (audioManager) audioManager->Clean();
    if (renderSystem) renderSystem->Clean();
    if (uiRenderer) uiRenderer->Clean();

    if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }
    SDL_Quit();
    std::cout << "游戏引擎已安全关闭！" << std::endl;
}

void Engine::ToggleFullscreen() {
    if (window) {
        Uint32 flags = SDL_GetWindowFlags(window);
        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
            SDL_SetWindowFullscreen(window, 0);
            isFullscreen = false;
        } else {
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            isFullscreen = true;
        }
    }
}

void Engine::PerformAttack() {
    if (isAttacking || attackCooldownTimer > 0.0f) return;

    isAttacking = true;
    attackTimer = ATTACK_DURATION;
    attackCooldownTimer = ATTACK_COOLDOWN;
    attackDir = inputManager->GetPlayerDir();
    attackHitRegistered = false;

    audioManager->PlaySound(SoundEffect::ATTACK_SWING);
}

void Engine::StartGame(GameMode mode) {
    currentMode = mode;
    score = 0;
    vx = 0.0f;
    vy = 0.0f;
    inputManager->ResetMovementKeys();
    isAttacking = false;
    attackTimer = 0.0f;
    attackCooldownTimer = 0.0f;
    attackHitRegistered = false;
    particleSystem->Reset();
    renderSystem->ResetMovementBlends();
    diedByTrap = false;
    audioManager->ResumeMusic();

    if (mode == TIME_ATTACK) {
        gameTimer = 30.0f;
        if (!worldMap->LoadLevelFromFile("../assets/maps/map1.txt", playerX, playerY, playerSize)) return;

        // 初始宝箱随机刷新：清空地图预设固定宝箱，直接调用随机算法生成3个初始宝箱！
        worldMap->GetCoins().clear();
        worldMap->EnsureCoinCount(3, playerX, playerY, playerSize, gen);

        // 生成不可破坏的地刺与老鼠夹陷阱（保证空间连通性）
        worldMap->GenerateTrapsForTimeAttack(playerX, playerY, playerSize, gen);
        std::cout << "模式 1: 限时挑战开始! 30秒倒计时启动! 小心地刺与老鼠夹，去获得更多金币吧!" << std::endl;
    } else {
        gameTimer = 0.0f;
        gameOverStartTime = 0;
        MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
        if (!worldMap->LoadLevelFromFile("map2.txt", playerX, playerY, playerSize)) return;
        std::cout << "模式 2: 纯享迷宫模式开始! 探索吧!" << std::endl;
    }

    inputManager->SwitchToEnglishKeyboardLayout();
    currentState = PLAYING;
}

void Engine::UpdateVolumeFromMouse(int mouseX) {
    const int value = std::clamp((mouseX - 350) * 100 / 300, 0, 100);
    if (draggingVolumeSlider == 0) audioManager->SetMasterVolume(value);
    else if (draggingVolumeSlider == 1) audioManager->SetMusicVolume(value);
    else if (draggingVolumeSlider == 2) audioManager->SetEffectsVolume(value);
}

void Engine::HandleMenuClick(int mouseX, int mouseY) {
    auto hit = [mouseX, mouseY](const SDL_Rect& rect) {
        return mouseX >= rect.x && mouseX < rect.x + rect.w &&
               mouseY >= rect.y && mouseY < rect.y + rect.h;
    };

    if (currentState == MAIN_MENU) {
        if (hit({250, 170, 300, 46})) { audioManager->PlaySound(SoundEffect::CLICK); currentState = MODE_SELECT; }
        else if (hit({250, 228, 300, 46})) { audioManager->PlaySound(SoundEffect::CLICK); currentState = HISTORY; }
        else if (hit({250, 286, 300, 46})) { audioManager->PlaySound(SoundEffect::CLICK); currentState = CONTROLS; }
        else if (hit({250, 344, 300, 46})) { audioManager->PlaySound(SoundEffect::CLICK); settingsReturnState = MAIN_MENU; currentState = SETTINGS; }
        else if (hit({250, 402, 300, 46})) { audioManager->PlaySound(SoundEffect::CLICK); isRunning = false; }
    } else if (currentState == MODE_SELECT) {
        if (hit({220, 240, 360, 64})) { audioManager->PlaySound(SoundEffect::CLICK); StartGame(TIME_ATTACK); }
        else if (hit({220, 324, 360, 64})) { audioManager->PlaySound(SoundEffect::CLICK); StartGame(MAZE_MODE); }
        else if (hit({24, 24, 120, 44})) { audioManager->PlaySound(SoundEffect::CLICK); currentState = MAIN_MENU; }
    } else if ((currentState == HISTORY || currentState == CONTROLS) && hit({24, 24, 120, 44})) {
        audioManager->PlaySound(SoundEffect::CLICK);
        currentState = MAIN_MENU;
    } else if (currentState == SETTINGS && hit({24, 24, 120, 44})) {
        audioManager->PlaySound(SoundEffect::CLICK);
        currentState = settingsReturnState;
    } else if (currentState == PAUSED) {
        if (hit({250, 155, 300, 48})) {
            audioManager->PlaySound(SoundEffect::CLICK);
            currentState = PLAYING;
            audioManager->ResumeMusic();
        } else if (hit({250, 219, 300, 48})) {
            audioManager->PlaySound(SoundEffect::CLICK);
            StartGame(currentMode);
        } else if (hit({250, 283, 300, 48})) {
            audioManager->PlaySound(SoundEffect::CLICK);
            settingsReturnState = PAUSED;
            currentState = SETTINGS;
        } else if (hit({250, 347, 300, 48})) {
            audioManager->PlaySound(SoundEffect::CLICK);
            exitToDesktop = false;
            currentState = EXIT_CONFIRM;
        } else if (hit({250, 411, 300, 48})) {
            audioManager->PlaySound(SoundEffect::CLICK);
            exitToDesktop = true;
            currentState = EXIT_CONFIRM;
        }
    } else if (currentState == EXIT_CONFIRM) {
        if (hit({145, 330, 245, 58})) {
            audioManager->PlaySound(SoundEffect::CLICK);
            currentState = PAUSED;
        } else if (hit({410, 330, 245, 58})) {
            audioManager->PlaySound(SoundEffect::CLICK);
            inputManager->ResetMovementKeys();
            if (exitToDesktop) {
                isRunning = false;
            } else {
                currentState = MAIN_MENU;
                audioManager->ResumeMusic();
            }
        }
    }
}

void Engine::HandleInput() {
    inputManager->ProcessEvents(renderer, currentState);

    if (inputManager->IsQuitRequested()) isRunning = false;
    if (inputManager->IsFullscreenToggleRequested()) ToggleFullscreen();
    if (inputManager->IsMuteToggleRequested()) audioManager->ToggleMute();

    if (inputManager->IsPauseToggleRequested()) {
        if (currentState == PLAYING) {
            currentState = PAUSED;
            inputManager->ResetMovementKeys();
            vx = vy = 0.0f;
            audioManager->PauseMusic();
        } else if (currentState == PAUSED) {
            currentState = PLAYING;
            audioManager->ResumeMusic();
        } else if (currentState == SETTINGS) {
            currentState = settingsReturnState;
        } else if (currentState == EXIT_CONFIRM) {
            currentState = PAUSED;
        } else if (currentState == MODE_SELECT || currentState == HISTORY || currentState == CONTROLS) {
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
    }

    if (currentState == GAMEOVER && inputManager->IsEnterOrSpaceRequested()) {
        gameOverStartTime = 0;
        MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
        worldMap->LoadLevelFromFile("map2.txt", playerX, playerY, playerSize);
        vx = vy = 0.0f;
        currentState = MODE_SELECT;
    }

    if (currentState == PLAYING && inputManager->IsAttackRequested()) {
        PerformAttack();
    }

    // 鼠标点击事件
    if (inputManager->IsMouseLeftClicked()) {
        const int mouseX = inputManager->GetMouseX();
        const int mouseY = inputManager->GetMouseY();

        if (currentState == SETTINGS) {
            const int rowY[] = {230, 320, 410};
            for (int i = 0; i < 3; ++i) {
                if (mouseX >= 335 && mouseX < 665 && mouseY >= rowY[i] - 18 && mouseY < rowY[i] + 32) {
                    draggingVolumeSlider = i;
                    UpdateVolumeFromMouse(mouseX);
                    audioManager->PlaySound(SoundEffect::CLICK);
                    break;
                }
            }
        }
        HandleMenuClick(mouseX, mouseY);
    }

    if (inputManager->IsMouseLeftDown() && draggingVolumeSlider != -1 && currentState == SETTINGS) {
        UpdateVolumeFromMouse(inputManager->GetMouseX());
    }

    if (inputManager->IsMouseLeftReleased()) {
        draggingVolumeSlider = -1;
    }
}

void Engine::Update(float dt) {
    // 特效需要在结算页继续推进，否则死亡时的血沫与震屏会永久停在首帧。
    particleSystem->Update(dt);

    if (currentState != PLAYING) return;

    // 攻击冷却推进
    if (attackCooldownTimer > 0.0f) {
        attackCooldownTimer = std::max(0.0f, attackCooldownTimer - dt);
    }

    // 挥剑攻击状态与破坏障碍判定
    if (isAttacking) {
        attackTimer -= dt;
        if (attackTimer <= 0.0f) {
            attackTimer = 0.0f;
            isAttacking = false;
        }

        if (!attackHitRegistered) {
            if (physicsSystem->PerformAttackHitCheck(playerX, playerY, playerSize,
                                                     attackDir, *worldMap, *particleSystem)) {
                attackHitRegistered = true;
                audioManager->PlaySound(SoundEffect::OBSTACLE_BREAK);
                particleSystem->TriggerScreenShake(0.12f, 3.0f);
            }
        }
    }

    // 移动速度更新（Shift 冲刺 / Ctrl 聚焦）
    const Uint8* keyboardState = SDL_GetKeyboardState(nullptr);
    const bool sprintHeld = keyboardState[SDL_SCANCODE_LSHIFT] || keyboardState[SDL_SCANCODE_RSHIFT];
    const bool focusHeld = keyboardState[SDL_SCANCODE_LCTRL] || keyboardState[SDL_SCANCODE_RCTRL];
    renderSystem->UpdateMovementBlends(dt, sprintHeld, focusHeld);

    const float sprintMaxSpeed = MAX_SPEED * (1.0f + renderSystem->GetSprintBlend());
    const float movementMaxSpeed = sprintMaxSpeed +
        (MAX_SPEED / 3.0f - sprintMaxSpeed) * renderSystem->GetFocusBlend();

    // 游戏倒计时更新
    if (currentMode == TIME_ATTACK) {
        gameTimer -= dt;
        if (gameTimer <= 0.0f) {
            gameTimer = 0.0f;
            diedByTrap = false;
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
    float oldX = playerX;
    float oldY = playerY;

    const int verticalInput = static_cast<int>(inputManager->IsDownPressed()) - static_cast<int>(inputManager->IsUpPressed());
    if (verticalInput != 0) {
        vy += verticalInput * ACCEL * dt;
    } else {
        vy -= vy * FRICTION * dt;
        if (std::abs(vy) < 1.0f) vy = 0.0f;
    }
    vy = std::clamp(vy, -movementMaxSpeed, movementMaxSpeed);
    playerY += vy * dt;

    for (const auto& wall : worldMap->GetWalls()) {
        if (PhysicsSystem::CheckCollision(playerX, playerY, playerSize, wall.x, wall.y, wall.w, wall.h)) {
            playerY = oldY;
            vy = 0.0f;
            break;
        }
    }
    if (physicsSystem->CheckObstacleCollision(playerX, playerY, playerSize, *worldMap)) {
        playerY = oldY;
        vy = 0.0f;
    }

    const int horizontalInput = static_cast<int>(inputManager->IsRightPressed()) - static_cast<int>(inputManager->IsLeftPressed());
    if (horizontalInput != 0) {
        vx += horizontalInput * ACCEL * dt;
    } else {
        vx -= vx * FRICTION * dt;
        if (std::abs(vx) < 1.0f) vx = 0.0f;
    }
    vx = std::clamp(vx, -movementMaxSpeed, movementMaxSpeed);
    playerX += vx * dt;

    for (const auto& wall : worldMap->GetWalls()) {
        if (PhysicsSystem::CheckCollision(playerX, playerY, playerSize, wall.x, wall.y, wall.w, wall.h)) {
            playerX = oldX;
            vx = 0.0f;
            break;
        }
    }
    if (physicsSystem->CheckObstacleCollision(playerX, playerY, playerSize, *worldMap)) {
        playerX = oldX;
        vx = 0.0f;
    }

    // 陷阱触碰致死判定（模式一：得分以当前得分为准，标记失败）
    if (currentMode == TIME_ATTACK) {
        int trapHitIdx = physicsSystem->CheckTrapCollision(playerX, playerY, playerSize, *worldMap);
        if (trapHitIdx != -1) {
            const auto& trap = worldMap->GetTraps()[trapHitIdx];
            diedByTrap = true;
            currentState = GAMEOVER;
            gameOverStartTime = SDL_GetTicks();

            audioManager->PlaySound(SoundEffect::PLAYER_DEATH);
            particleSystem->TriggerScreenShake(0.35f, 6.0f);
            particleSystem->EmitDebrisBurst(playerX + playerSize * 0.5f, playerY + playerSize * 0.5f, 45, {220, 35, 35, 255});
            particleSystem->EmitExplosion(trap.x + trap.size * 0.5f, trap.y + trap.size * 0.5f, 25);

            saveManager->AddScoreRecord(score, false);
            std::cout << "\n💀 惨死！触发致命陷阱！挑战失败！最终得分: " << score << " 分！\n" << std::endl;
            return;
        }
    }

    // 收集宝藏金币
    int coinIdx = physicsSystem->CheckCoinPickup(playerX, playerY, playerSize, *worldMap);
    if (coinIdx != -1) {
        score++;
        audioManager->PlaySound(SoundEffect::PICKUP);

        const auto coin = worldMap->GetCoins()[coinIdx];
        const float coinSize = worldMap->GetCoinSize();
        particleSystem->EmitExplosion(coin.x + coinSize * 0.5f, coin.y + coinSize * 0.5f, 30);

        if (currentMode == TIME_ATTACK) {
            std::cout << "💰 叮！吃到金币！当前得分: " << score << " 分！" << std::endl;
            worldMap->GetCoins().erase(worldMap->GetCoins().begin() + coinIdx);
            worldMap->ResetCoinPosition(playerX, playerY, playerSize, gen);
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
    uiRenderer->BeginFrame();

    const bool gameplayVisible = (currentState == PLAYING || currentState == PAUSED ||
                                  currentState == EXIT_CONFIRM || currentState == GAMEOVER);
    SDL_FPoint shakeOffset = particleSystem->GetShakeOffset();
    renderSystem->UpdateCamera(playerX, playerY, playerSize, *worldMap, gameplayVisible, shakeOffset.x, shakeOffset.y);

    SDL_SetRenderDrawColor(renderer, 20, 30, 40, 255);
    SDL_RenderClear(renderer);

    if (gameplayVisible) {
        renderSystem->DrawDungeonBackground(*worldMap, 0);
        renderSystem->DrawWorld(*worldMap, playerX, playerY, playerSize,
                                isAttacking, attackDir, attackTimer, ATTACK_DURATION,
                                currentState == PLAYING);
        particleSystem->Render(renderer, renderSystem->GetCameraX(), renderSystem->GetCameraY(), renderSystem->CurrentCameraZoom());

        if (currentMode == TIME_ATTACK) {
            renderSystem->DrawVisionFog(playerX, playerY, playerSize, *worldMap);
        }
    }

    const int mouseX = inputManager->GetMouseX();
    const int mouseY = inputManager->GetMouseY();

    if (currentState == MAIN_MENU || currentState == MODE_SELECT) {
        renderSystem->DrawDungeonBackground(*worldMap, 145);
        uiRenderer->DrawMenuPage(currentState, mouseX, mouseY);
    } else if (currentState == HISTORY) {
        uiRenderer->DrawHistoryPage(*saveManager, mouseX, mouseY);
    } else if (currentState == CONTROLS) {
        uiRenderer->DrawControlsPage(mouseX, mouseY);
    } else if (currentState == SETTINGS) {
        uiRenderer->DrawSettingsPage(audioManager->GetMasterVolume(),
                                     audioManager->GetMusicVolume(),
                                     audioManager->GetEffectsVolume(),
                                     audioManager->IsMuted(),
                                     mouseX, mouseY);
    } else if (currentState == PAUSED) {
        uiRenderer->DrawPausePage(mouseX, mouseY);
    } else if (currentState == EXIT_CONFIRM) {
        uiRenderer->DrawExitConfirmPage(mouseX, mouseY);
    } else if (currentState == PLAYING) {
        uiRenderer->DrawHUD(currentMode, gameTimer, score);
    } else if (currentState == GAMEOVER) {
        uiRenderer->DrawGameOver(currentMode, diedByTrap, score, gameTimer);
    }

    if (uiRenderer->DidHoverChange()) {
        audioManager->PlaySound(SoundEffect::BUTTON_HOVER);
    }

    SDL_RenderPresent(renderer);
    SDL_Delay(16);
}
