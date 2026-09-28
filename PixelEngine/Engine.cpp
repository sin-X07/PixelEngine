#include "Engine.hpp"
#include "MazeGenerator.hpp"
#include <cmath>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {
std::string ResolveProjectFile(const std::string& filename) {
    const std::filesystem::path current = std::filesystem::current_path() / filename;
    if (std::filesystem::exists(current)) {
        return current.string();
    }

    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path();
    const std::filesystem::path projectPath = sourceRoot / filename;
    if (std::filesystem::exists(projectPath)) {
        return projectPath.string();
    }

    return filename;
}
}

    // 解析外部文本地图文件
bool Engine::LoadLevelFromFile(const std::string& filename) {
    const std::string resolvedPath = ResolveProjectFile(filename);
    std::ifstream mapFile(resolvedPath);
    if (!mapFile) {
        std::cerr << "无法打开关卡地图文件：" << filename << std::endl;
        return false;
    }

    walls.clear();
    std::string line;
    int row = 0;
    while (std::getline(mapFile, line)) {
        if (line.empty()) continue;
        for (size_t col = 0; col < line.length(); col++) {
            char tileType = line[col];

            int pixelX = static_cast<int>(col) * TILE_SIZE;
            int pixelY = static_cast<int>(row) * TILE_SIZE;

            if (tileType == '#') {
                walls.push_back({pixelX, pixelY, TILE_SIZE, TILE_SIZE});
            } else if (tileType == 'P') {
                playerX = static_cast<float>(pixelX);
                playerY = static_cast<float>(pixelY);
            } else if (tileType == 'C') {
                coinX = static_cast<float>(pixelX + 12);
                coinY = static_cast<float>(pixelY + 12);
                isCoinActive = true;
            }
        }
        row++;
    }

    mapFile.close();
    std::cout << "成功解析外部关卡 [" << filename << "]! 共构建了" << walls.size() << " 面墙壁" << std::endl;
    return true;
}

// 保存战绩
void Engine::SaveScoreHistoryToFile() {
    const std::string savePath = ResolveProjectFile("score_saves.txt");
    std::ofstream outFile(savePath);

    if (!outFile) {
        std::cerr << "分数存档文件打开失败!" << std::endl;
        return;
    }

    for (int hScore : scoreHistory) {
        outFile << hScore << "\n";
    }

    outFile.close();
    std::cout << "分数存档文件保存成功!" << std::endl;
}

void Engine::SaveTimeHistoryToFile() {
    const std::string savePath = ResolveProjectFile("time_saves.txt");
    std::ofstream outFile(savePath);

    if (!outFile) {
        std::cerr << "时间存档文件打开失败!" << std::endl;
        return;
    }

    for (float hTime : timeHistory) {
        outFile << hTime << "\n";
    }

    outFile.close();
    std::cout << "时间存档文件保存成功!" << std::endl;
}

// 读取战绩
void Engine::LoadScoreHistoryFromFile() {
    const std::string savePath = ResolveProjectFile("score_saves.txt");
    std::ifstream inFile(savePath);

    if (!inFile) {
        std::cout << "创建新分数档案。" << std::endl;
    }

    scoreHistory.clear();
    int hScore = 0;

    while (inFile >> hScore) {
        scoreHistory.push_back(hScore);
    }

    inFile.close();

    if (scoreHistory.size() > 10) {
        scoreHistory.resize(10);
    }

    std::cout << "成功读取 " << scoreHistory.size() << " 局历史分数!" << std::endl;
}

void Engine::LoadTimeHistoryFromFile() {
    const std::string savePath = ResolveProjectFile("time_saves.txt");
    std::ifstream inFile(savePath);
    
    if (!inFile) {
        std::cout << "创建新时间档案。" << std::endl;
    }

    timeHistory.clear();
    float hTime = 0.0;

    while (inFile >> hTime) {
        timeHistory.push_back(hTime);
    }

    inFile.close();

    if (timeHistory.size() > 10) {
        timeHistory.resize(10);
    }

    std::cout << "成功读取 " << timeHistory.size() << " 局历史时间!" << std::endl;
}

//绘制金币
void Engine::DrawFilledCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius) {
    for (int w = 0; w < radius * 2; w++) {
        for (int h = 0; h < radius * 2; h++) {
            int dx = radius - w;
            int dy = radius - h;
            if ((dx * dx + dy * dy) <= (radius * radius)) {
                SDL_RenderDrawPoint(renderer, centerX + dx, centerY + dy);
            }
        }
    }
}

// 粒子爆照效果
void Engine::EmitExplosion(float spawnX, float spawnY, int count) {
    int activated = 0;

    std::uniform_real_distribution<float> randAngle(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> randSpeed(50.0f, 150.0f);
    std::uniform_real_distribution<float> randLife(0.3f, 0.7f);

    for (auto& p : particlePool) {
        if (!p.active) {
            p.active = true;
            p.x = spawnX;
            p.y = spawnY;

            float angle = randAngle(gen);
            float speed = randSpeed(gen);

            p.vx = std::cos(angle) * speed;
            p.vy = std::sin(angle) * speed;
            p.lifetime = randLife(gen);
            p.maxLifetime = p.lifetime;

            activated++;
            if (activated >= count) break;
        }
    }
}

// 初始化 SDL2 窗口和渲染器
bool Engine::Initialize(const char* title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL 初始化失败: " << SDL_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "窗口创建失败: " << SDL_GetError() << std::endl;
        return false;
    }

    // 创建硬件加速的渲染器
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "渲染器创建失败: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(renderer, width, height);

    if (TTF_Init() < 0) {
        std::cerr << "SDL_ttf 初始化失败: " << TTF_GetError() << std::endl;
        return false;
    }
    ttfInitialized = true;

    const char* fontPaths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc"
    };
    for (const char* fontPath : fontPaths) {
        uiFont = TTF_OpenFont(fontPath, 24);
        if (uiFont) break;
    }
    if (!uiFont) {
        std::cerr << "无法加载中文字体: " << TTF_GetError() << std::endl;
        return false;
    }

    // 读取文件迷宫墙
    MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);

    // 默认初始化模式一
    bool loadSuccess = LoadLevelFromFile("map1.txt");
    if (!loadSuccess) {
        std::cerr << "警告：未找到地图 map1.txt" << std::endl;
        return false;
    }

    LoadScoreHistoryFromFile();
    LoadTimeHistoryFromFile();
    historyLoaded = true;

    particlePool.resize(MAX_PARTICLES);

    isRunning = true;
    return true;
}

// 核心主循环（实现了解耦高刷屏的 Fixed Update）
void Engine::Run() {
    uint32_t previousTime = SDL_GetTicks();
    float lag = 0.0f;

    while (isRunning) {
        uint32_t currentTime = SDL_GetTicks();
        float elapsedTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;
        lag += elapsedTime;

        // ---- A. 处理输入 ----
        HandleInput();

        // ---- B. 固定时间步长更新（逻辑/物理更新） ----
        // 无论显示器是 60Hz 还是 360Hz，这里的 Update 触发频率永远是恒定的
        while (lag >= MS_PER_UPDATE) {
            Update(MS_PER_UPDATE / 1000.0f); // 转换为秒传入
            lag -= MS_PER_UPDATE;
        }

        // ---- C. 渲染绘制（每帧能跑多快跑多快） ----
        Render();
    }
}

// 处理退出、键盘等事件
void Engine::HandleInput() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            isRunning = false;
        }

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (currentState == MAIN_MENU || currentState == MODE_SELECT ||
                currentState == HISTORY || currentState == CONTROLS) {
                HandleMenuClick(event.button.x, event.button.y);
            }
        }

        //键盘按下
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_F11:
                    ToggleFullscreen();
                    break;

                case SDLK_ESCAPE:
                    if (currentState == MODE_SELECT || currentState == HISTORY || currentState == CONTROLS) {
                        currentState = MAIN_MENU;
                    } else {
                        isRunning = false;
                    }
                    break;

                case SDLK_1: case SDLK_KP_1:
                    if (currentState == MODE_SELECT) StartGame(TIME_ATTACK);
                    break;

                case SDLK_2: case SDLK_KP_2:
                    if (currentState == MODE_SELECT) StartGame(MAZE_MODE);
                    break;

                case SDLK_RETURN: case SDLK_SPACE:
                    if (currentState == GAMEOVER) {
                        gameOverStartTime = 0;
                        MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
                        LoadLevelFromFile("map2.txt");
                        vx = 0.0f;
                        vy = 0.0f;
                        currentState = MODE_SELECT;
                        std::cout << "返回主菜单, 请重新选择游戏模式!" << std::endl;

                    }
                    break;

                case SDLK_w: case SDLK_UP:    if (currentState == PLAYING) upPressed = true; break;
                case SDLK_s: case SDLK_DOWN:  if (currentState == PLAYING) downPressed = true; break;
                case SDLK_a: case SDLK_LEFT:  if (currentState == PLAYING) leftPressed = true; break;
                case SDLK_d: case SDLK_RIGHT: if (currentState == PLAYING) rightPressed = true; break;
            }
        }

        //键盘松开
        if (event.type == SDL_KEYUP) {
            switch (event.key.keysym.sym) {
                case SDLK_w: case SDLK_UP:    upPressed = false; break;
                case SDLK_s: case SDLK_DOWN:  downPressed = false; break;
                case SDLK_a: case SDLK_LEFT:  leftPressed = false; break;
                case SDLK_d: case SDLK_RIGHT: rightPressed = false; break;
            }
        }
    }
}

void Engine::StartGame(GameMode mode) {
    currentMode = mode;
    isCoinActive = true;
    score = 0;
    vx = 0.0f;
    vy = 0.0f;
    upPressed = downPressed = leftPressed = rightPressed = false;

    if (mode == TIME_ATTACK) {
        gameTimer = 30.0f;
        if (!LoadLevelFromFile("map1.txt")) return;
        walls.push_back({0, -5, 800, 5});
        walls.push_back({-5, 0, 5, 600});
        walls.push_back({800, -5, 5, 600});
        walls.push_back({0, 600, 800, 5});
        std::cout << "模式 1: 限时挑战开始! 30秒倒计时启动! 去获得更多金币吧!" << std::endl;
    } else {
        gameTimer = 0.0f;
        gameOverStartTime = 0;
        MazeGenerator::GenerateRandomMazeFile("map2.txt", gen);
        if (!LoadLevelFromFile("map2.txt")) return;
        std::cout << "模式 2: 纯享迷宫模式开始! 探索吧!" << std::endl;
    }

    currentState = PLAYING;
}

void Engine::HandleMenuClick(int mouseX, int mouseY) {
    auto hit = [mouseX, mouseY](const SDL_Rect& rect) {
        return mouseX >= rect.x && mouseX < rect.x + rect.w &&
               mouseY >= rect.y && mouseY < rect.y + rect.h;
    };

    if (currentState == MAIN_MENU) {
        if (hit({250, 190, 300, 54})) currentState = MODE_SELECT;
        else if (hit({250, 258, 300, 54})) currentState = HISTORY;
        else if (hit({250, 326, 300, 54})) currentState = CONTROLS;
        else if (hit({250, 394, 300, 54})) isRunning = false;
    } else if (currentState == MODE_SELECT) {
        if (hit({220, 240, 360, 64})) StartGame(TIME_ATTACK);
        else if (hit({220, 324, 360, 64})) StartGame(MAZE_MODE);
        else if (hit({24, 24, 120, 44})) currentState = MAIN_MENU;
    } else if ((currentState == HISTORY || currentState == CONTROLS) && hit({24, 24, 120, 44})) {
        currentState = MAIN_MENU;
    }
}

void Engine::DrawText(const std::string& text, int x, int y, SDL_Color color, int pointSize) {
    if (!uiFont || text.empty()) return;
    TTF_SetFontSize(uiFont, pointSize);
    SDL_Surface* surface = TTF_RenderUTF8_Blended(uiFont, text.c_str(), color);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect destination = {x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void Engine::DrawButton(const SDL_Rect& rect, const std::string& label, bool hovered) {
    SDL_SetRenderDrawColor(renderer, hovered ? 245 : 55, hovered ? 190 : 75, hovered ? 90 : 85, 255);
    SDL_RenderFillRect(renderer, &rect);
    SDL_Rect inset = {rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6};
    SDL_SetRenderDrawColor(renderer, 24, 35, 43, 255);
    SDL_RenderFillRect(renderer, &inset);
    SDL_Color color = hovered ? SDL_Color{255, 222, 125, 255} : SDL_Color{230, 238, 235, 255};
    TTF_SetFontSize(uiFont, 24);
    int textWidth = 0;
    int textHeight = 0;
    TTF_SizeUTF8(uiFont, label.c_str(), &textWidth, &textHeight);
    DrawText(label, rect.x + (rect.w - textWidth) / 2, rect.y + (rect.h - textHeight) / 2, color);
}

void Engine::DrawMenuPage() {
    SDL_Rect background = {0, 0, 800, 600};
    SDL_SetRenderDrawColor(renderer, 18, 36, 43, 255);
    SDL_RenderFillRect(renderer, &background);
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawLine(renderer, 0, 104, 800, 104);
    DrawText("PIXEL ENGINE", 250, 42, {247, 211, 130, 255}, 34);

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    float logicalX = 0.0f;
    float logicalY = 0.0f;
    SDL_RenderWindowToLogical(renderer, mouseX, mouseY, &logicalX, &logicalY);
    mouseX = static_cast<int>(logicalX);
    mouseY = static_cast<int>(logicalY);
    if (currentState == MAIN_MENU) {
        const char* labels[] = {"选择模式", "历史记录", "按键介绍", "退出游戏"};
        for (int i = 0; i < 4; ++i) {
            SDL_Rect button = {250, 190 + i * 68, 300, 54};
            DrawButton(button, labels[i], mouseX >= button.x && mouseX < button.x + button.w &&
                       mouseY >= button.y && mouseY < button.y + button.h);
        }
    } else {
        DrawButton({24, 24, 120, 44}, "返回", mouseX < 144 && mouseY < 68);
        DrawText("选择游戏模式", 290, 160, {235, 241, 232, 255}, 30);
        SDL_Rect modeOne = {220, 240, 360, 64};
        SDL_Rect modeTwo = {220, 324, 360, 64};
        DrawButton(modeOne, "模式一  限时挑战", mouseX >= modeOne.x && mouseX < modeOne.x + modeOne.w &&
                   mouseY >= modeOne.y && mouseY < modeOne.y + modeOne.h);
        DrawButton(modeTwo, "模式二  迷宫探索", mouseX >= modeTwo.x && mouseX < modeTwo.x + modeTwo.w &&
                   mouseY >= modeTwo.y && mouseY < modeTwo.y + modeTwo.h);
        DrawText("点击按钮开始", 330, 430, {164, 190, 182, 255}, 18);
    }
}

void Engine::DrawHistoryPage() {
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_Rect background = {0, 0, 800, 600};
    SDL_RenderFillRect(renderer, &background);
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    float logicalX = 0.0f;
    float logicalY = 0.0f;
    SDL_RenderWindowToLogical(renderer, mouseX, mouseY, &logicalX, &logicalY);
    mouseX = static_cast<int>(logicalX);
    mouseY = static_cast<int>(logicalY);
    DrawButton({24, 24, 120, 44}, "返回", mouseX < 144 && mouseY < 68);
    DrawText("历史记录", 330, 35, {247, 211, 130, 255}, 30);
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawLine(renderer, 400, 105, 400, 560);

    DrawText("模式一  限时挑战", 72, 112, {245, 208, 118, 255}, 22);
    DrawText("模式二  迷宫探索", 482, 112, {134, 222, 190, 255}, 22);

    const int barWidth = 180;
    const float coefficient = 2.0f;
    int maxScore = 0;
    for (int record : scoreHistory) maxScore = std::max(maxScore, record);
    float bestTime = 0.0f;
    for (float record : timeHistory) {
        if (record > 0.0f && (bestTime == 0.0f || record < bestTime)) bestTime = record;
    }

    for (size_t i = 0; i < std::min<size_t>(10, scoreHistory.size()); ++i) {
        int y = 160 + static_cast<int>(i) * 38;
        int record = std::max(0, scoreHistory[i]);
        std::ostringstream label;
        label << std::setw(2) << std::setfill('0') << (i + 1) << "  " << record << " 分";
        DrawText(label.str(), 36, y - 2, {230, 238, 235, 255}, 16);
        SDL_Rect track = {194, y + 3, barWidth, 14};
        SDL_SetRenderDrawColor(renderer, 53, 70, 72, 255);
        SDL_RenderFillRect(renderer, &track);
        float ratio = maxScore > 0
            ? std::log1p(coefficient * record) / std::log1p(coefficient * maxScore)
            : 0.0f;
        SDL_Rect fill = {track.x, track.y, static_cast<int>(barWidth * ratio), track.h};
        SDL_SetRenderDrawColor(renderer, i == 0 ? 244 : 225, i == 0 ? 187 : 157, 76, 255);
        SDL_RenderFillRect(renderer, &fill);
    }

    for (size_t i = 0; i < std::min<size_t>(10, timeHistory.size()); ++i) {
        int y = 160 + static_cast<int>(i) * 38;
        float record = timeHistory[i];
        std::ostringstream label;
        label << std::setw(2) << std::setfill('0') << (i + 1) << "  "
              << std::fixed << std::setprecision(2) << record << " 秒";
        DrawText(label.str(), 426, y - 2, {230, 238, 235, 255}, 16);
        SDL_Rect track = {584, y + 3, barWidth, 14};
        SDL_SetRenderDrawColor(renderer, 53, 70, 72, 255);
        SDL_RenderFillRect(renderer, &track);
        float ratio = bestTime > 0.0f && record > 0.0f
            ? std::log1p(coefficient * bestTime / record) / std::log1p(coefficient)
            : 0.0f;
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        SDL_Rect fill = {track.x, track.y, static_cast<int>(barWidth * ratio), track.h};
        SDL_SetRenderDrawColor(renderer, i == 0 ? 119 : 78, i == 0 ? 225 : 183, 157, 255);
        SDL_RenderFillRect(renderer, &fill);
    }

    if (scoreHistory.empty()) DrawText("暂无记录", 110, 180, {155, 177, 171, 255}, 18);
    if (timeHistory.empty()) DrawText("暂无记录", 510, 180, {155, 177, 171, 255}, 18);
}

void Engine::DrawControlsPage() {
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_Rect background = {0, 0, 800, 600};
    SDL_RenderFillRect(renderer, &background);
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    float logicalX = 0.0f;
    float logicalY = 0.0f;
    SDL_RenderWindowToLogical(renderer, mouseX, mouseY, &logicalX, &logicalY);
    mouseX = static_cast<int>(logicalX);
    mouseY = static_cast<int>(logicalY);
    DrawButton({24, 24, 120, 44}, "返回", mouseX < 144 && mouseY < 68);
    DrawText("操作说明", 330, 35, {247, 211, 130, 255}, 30);
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawLine(renderer, 80, 104, 720, 104);

    const SDL_Color heading = {134, 222, 190, 255};
    const SDL_Color body = {230, 238, 235, 255};
    DrawText("游戏中", 100, 132, heading, 22);
    DrawText("W / A / S / D    或    方向键：移动", 100, 174, body, 19);
    DrawText("F11：切换全屏", 100, 210, body, 19);
    DrawText("ESC：退出游戏", 100, 246, body, 19);
    DrawText("结算页面", 100, 308, heading, 22);
    DrawText("Enter / 空格：返回模式选择", 100, 350, body, 19);
    DrawText("菜单页面", 100, 412, heading, 22);
    DrawText("鼠标左键：选择按钮", 100, 454, body, 19);
    DrawText("1 / 2：在模式选择页快速开始对应模式", 100, 490, body, 19);
    DrawText("ESC：返回主菜单（主菜单中退出）", 100, 526, body, 19);
}

// 碰撞检测
bool Engine::CheckCollision(float px, float py, float pSize, float wx, float wy, float wW, float wH) {
    if (px + pSize <= wx) return false; //墙左
    if (px >= wx + wW)    return false; //墙右
    if (py + pSize <= wy) return false; //墙上
    if (py >= wy + wH)    return false; //墙下

    return true;
}

// 物理/逻辑更新（帧率无关，速度恒定）
void Engine::Update(float dt) {
    if (currentState != PLAYING) return;

    for (auto& p : particlePool) {
        if (p.active) {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += 200.0f * dt;
            p.lifetime -= dt;
            if (p.lifetime <= 0.0f) {
                p.active = false;
            }
        }
    }

    if (currentMode == TIME_ATTACK) {
        gameTimer -= dt;

        if (gameTimer <= 0.0f) {
            gameTimer = 0.0f;
            currentState = GAMEOVER;

            scoreHistory.insert(scoreHistory.begin(), score);
            if (scoreHistory.size() > 10) {
                scoreHistory.pop_back();
            }

            std::cout << "\n======================================" << std::endl;
            std::cout << "🏁 TIME UP! 时间到！挑战结束！" << std::endl;
            std::cout << "🏆 您的最终得分是: " << score << " 分！" << std::endl;
            std::cout << "========================================\n" << std::endl;
            return;
        }
    } else if (currentMode == MAZE_MODE) {
        gameTimer += dt;
    }

    //存入安全坐标
    float oldX = playerX;
    float oldY = playerY;

    if (upPressed) vy -= ACCEL * dt;
    else if (downPressed) vy += ACCEL * dt;
    else {
        vy -= vy * FRICTION * dt;
        if (std::abs(vy) < 1.0f) vy = 0.0f;
    }

    if (vy > MAX_SPEED)  vy = MAX_SPEED;
    if (vy < -MAX_SPEED) vy = -MAX_SPEED;

    playerY += vy * dt;

    for (const auto& wall : walls) {
        if (CheckCollision(playerX, playerY, playerSize, wall.x, wall.y, wall.w, wall.h)) {
            playerY = oldY;
            vy = 0.0f;
            break;
        }
    }

    if (leftPressed)  vx -= ACCEL * dt;
    else if (rightPressed) vx += ACCEL * dt;
    else {
        vx -= vx * FRICTION * dt;
        if (std::abs(vx) < 1.0f) vx = 0.0f;
    }

    if (vx > MAX_SPEED) vx = MAX_SPEED;
    if (vx < -MAX_SPEED) vx = -MAX_SPEED;

    playerX += vx * dt;

    for (const auto& wall : walls) {
        if (CheckCollision(playerX, playerY, playerSize, wall.x, wall.y, wall.w, wall.h)) {
            playerX = oldX;
            vx = 0.0f;
            break;
        }
    }

    //吃金币
    if (isCoinActive) {
        if (CheckCollision(playerX, playerY, playerSize, coinX, coinY, coinSize, coinSize)) {
            score++;

            EmitExplosion(coinX + coinSize / 2.0f, coinY + coinSize / 2.0f, 30);

            if (currentMode == TIME_ATTACK) {
                std::cout << "💰 叮！吃到金币！当前得分: " << score << " 分！" << std::endl;
                resetCoinPosition();
            } else if (currentMode == MAZE_MODE) {
                isCoinActive = false;
                currentState = GAMEOVER;

                gameOverStartTime = SDL_GetTicks();

                timeHistory.insert(timeHistory.begin(), gameTimer);
                if (timeHistory.size() > 10) timeHistory.pop_back();

                std::cout << "成功通关! 耗时: " << gameTimer << " 秒!" << std::endl;
                return;
            }
        }
    }
}

// 随机生成金币
void Engine::resetCoinPosition() {
    bool isCollidingWithAnyWall = true;
    while (isCollidingWithAnyWall) {
        coinX = static_cast<float>(50 + rand() % 700);
        coinY = static_cast<float>(50 + rand() % 500);
        isCollidingWithAnyWall = false;

        for (const auto& wall : walls) {
            if (CheckCollision(coinX, coinY, coinSize, wall.x, wall.y, wall.w, wall.h)) {
                isCollidingWithAnyWall = true;
                break;
            }
        }
    }
}

// 像素级画面渲染
void Engine::Render() {
    // 用一种复古的像素风暗青色清空屏幕
    SDL_SetRenderDrawColor(renderer, 20, 30, 40, 255);
    SDL_RenderClear(renderer);

    // 红色小方块
    SDL_Rect pRect;
    pRect.x = static_cast<int>(playerX);
    pRect.y = static_cast<int>(playerY);
    pRect.w = static_cast<int>(playerSize);
    pRect.h = static_cast<int>(playerSize);
    SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
    SDL_RenderFillRect(renderer, &pRect);

    // 所有绿色墙壁
    SDL_SetRenderDrawColor(renderer, 60, 255, 60, 255);
    for (const auto& wall : walls) {
        SDL_RenderFillRect(renderer, &wall);
    }

    // 金币
    if (isCoinActive && currentState == PLAYING) {
        SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
        int radius = static_cast<int>(coinSize / 2);
        int cx = static_cast<int>(coinX) + radius;
        int cy = static_cast<int>(coinY) + radius;

        DrawFilledCircle(renderer, cx, cy, radius);
    }

    // 粒子特效
    for (const auto& p : particlePool) {
        if (p.active) {
            float lifeRatio = p.lifetime / p.maxLifetime;
            int alpha = static_cast<int>(lifeRatio * 255.0f);
            if (alpha < 0) alpha = 0;

            int red = 255;
            int green = static_cast<int>(140 + 115 * lifeRatio);
            int blue = static_cast<int>(30 * lifeRatio);

            SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
            SDL_Rect pRect = {static_cast<int>(p.x), static_cast<int>(p.y), 2, 2};
            SDL_RenderFillRect(renderer, &pRect);
        }
    }

    if (currentState == MAIN_MENU || currentState == MODE_SELECT) DrawMenuPage();
    else if (currentState == HISTORY) DrawHistoryPage();
    else if (currentState == CONTROLS) DrawControlsPage();

    // 游戏中顶部UI
    if (currentState == PLAYING) {
        //进度条
        if (currentMode == TIME_ATTACK) {
            SDL_Rect timeBG = {20, 10, 200, 15};
            SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
            SDL_RenderFillRect(renderer, &timeBG);

            int barWidth = static_cast<int>((gameTimer / 30.0f) * 200.0f);
            SDL_Rect timeBar = {20, 10, barWidth, 15};

            if (gameTimer > 10.0f) {
                SDL_SetRenderDrawColor(renderer, 60, 255, 60, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
            }
            SDL_RenderFillRect(renderer, &timeBar);
        }

        //计分
        SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
        int dotRadius = 4;
        for (int i = 0; i < score; i++) {
            int cx = 760 - (i * 12) + dotRadius;
            int cy = 13 + dotRadius;
            DrawFilledCircle(renderer, cx, cy, dotRadius);
        }
    }
    

    if (currentState == GAMEOVER) {
        SDL_Rect dimRect = {0, 0, 800, 600};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
        SDL_RenderFillRect(renderer, &dimRect);
        SDL_Rect panel = {180, 180, 440, 240};
        SDL_SetRenderDrawColor(renderer, 45, 62, 65, 255);
        SDL_RenderFillRect(renderer, &panel);
        SDL_Rect inset = {184, 184, 432, 232};
        SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
        SDL_RenderFillRect(renderer, &inset);

        DrawText("本局结束", 334, 216, {247, 211, 130, 255}, 30);
        std::ostringstream result;
        if (currentMode == TIME_ATTACK) {
            result << "本局得分：" << score;
        } else {
            result << "通关用时：" << std::fixed << std::setprecision(2) << gameTimer << " 秒";
        }
        DrawText(result.str(), 285, 280, {230, 238, 235, 255}, 24);
        DrawText("Enter / 空格：返回模式选择", 248, 350, {134, 222, 190, 255}, 18);
    }
    SDL_RenderPresent(renderer);
    SDL_Delay(16);    //控帧，放cpu满载
}

// 释放内存，安全退出
void Engine::Clean() {
    if (historyLoaded) {
        SaveScoreHistoryToFile();
        SaveTimeHistoryToFile();
    }

    if (uiFont) {
        TTF_CloseFont(uiFont);
        uiFont = nullptr;
    }
    if (ttfInitialized) {
        TTF_Quit();
        ttfInitialized = false;
    }
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "游戏引擎已安全关闭！" << std::endl;
}

void Engine::ToggleFullscreen() {
    if (window) {
        Uint32 flags = SDL_GetWindowFlags(window);
        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
            SDL_SetWindowFullscreen(window, 0); // 退出全屏
            isFullscreen = false;
        } else {
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP); // 进入全屏
            isFullscreen = true;
        }
    }
}