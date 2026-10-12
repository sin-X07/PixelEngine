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

    // 血液粒子使用 RenderSystem 加载的血迹贴图
    particleSystem->SetBloodTexture(renderSystem->GetBloodTexture());

    // 默认加载随机迷宫地图与模式一地图
    MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
    if (!worldMap->LoadLevelFromFile("../assets/maps/map1.txt", player.x, player.y, player.size)) {
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
        gameTimer = 120.0f;
        if (!worldMap->LoadLevelFromFile("../assets/maps/map1.txt", player.x, player.y, player.size)) return;

        // 初始宝箱随机刷新：清空地图预设固定宝箱，直接调用随机算法生成3个初始宝箱！
        worldMap->GetCoins().clear();
        worldMap->EnsureCoinCount(3, player.x, player.y, player.size, gen);

        // 生成不可破坏的地刺与老鼠夹陷阱（保证空间连通性）
        worldMap->GenerateTrapsForTimeAttack(player.x, player.y, player.size, gen);

        // 新增可破坏障碍，丰富地形
        worldMap->GenerateExtraObstaclesForTimeAttack(player.x, player.y, player.size, gen);
        std::cout << "模式 1: 限时挑战开始! 两分钟倒计时启动! 小心地刺、老鼠夹与怪物，去获得更多金币吧!" << std::endl;
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
            player.vx = player.vy = 0.0f;
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
        worldMap->LoadLevelFromFile("map2.txt", player.x, player.y, player.size);
        player.vx = player.vy = 0.0f;
        currentState = MODE_SELECT;
    }

    if (currentState == PLAYING) {
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
    uiRenderer->BeginFrame();

    const bool gameplayVisible = (currentState == PLAYING || currentState == PAUSED ||
                                  currentState == EXIT_CONFIRM || currentState == GAMEOVER);
    SDL_FPoint shakeOffset = particleSystem->GetShakeOffset();
    renderSystem->UpdateCamera(player.x, player.y, player.size, *worldMap, gameplayVisible, shakeOffset.x, shakeOffset.y);

    SDL_SetRenderDrawColor(renderer, 20, 30, 40, 255);
    SDL_RenderClear(renderer);

    if (gameplayVisible) {
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
        uiRenderer->DrawHUD(currentMode, gameTimer, score, player.health);

        const Weapon* currentWeapon = combatSystem->GetActiveWeapon();
        SDL_Texture* weaponIcon = renderSystem->GetWeaponIconTexture(currentWeapon->type);
        if (currentWeapon->ranged) {
            uiRenderer->DrawWeaponStatus(weaponIcon, currentWeapon->Name(), currentWeapon->ammo, currentWeapon->magazineSize);
        } else {
            uiRenderer->DrawWeaponStatus(weaponIcon, currentWeapon->Name(), -1, 0);
        }
        if (combatSystem->HasActiveNoWeaponHint()) {
            uiRenderer->DrawNoWeaponHint(combatSystem->GetNoWeaponHintText());
        }
    } else if (currentState == GAMEOVER) {
        uiRenderer->DrawGameOver(currentMode, player.diedByTrap, player.diedByMonster, score, gameTimer);
    }

    if (uiRenderer->DidHoverChange()) {
        audioManager->PlaySound(SoundEffect::BUTTON_HOVER);
    }

    SDL_RenderPresent(renderer);
    SDL_Delay(16);
}
