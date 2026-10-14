#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "UIRenderer.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#undef DrawText
#endif

bool UIRenderer::Initialize(SDL_Renderer* ren) {
    renderer = ren;

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

    return true;
}

void UIRenderer::Clean() {
    if (uiFont) {
        TTF_CloseFont(uiFont);
        uiFont = nullptr;
    }
    if (ttfInitialized) {
        TTF_Quit();
        ttfInitialized = false;
    }
}

void UIRenderer::BeginFrame() {
    hoveredButtonLastFrame = hoveredButtonThisFrame;
    hoveredButtonThisFrame.clear();
}

bool UIRenderer::DidHoverChange() const {
    return !hoveredButtonThisFrame.empty() && hoveredButtonThisFrame != hoveredButtonLastFrame;
}

void UIRenderer::DrawText(const std::string& text, int x, int y, SDL_Color color, int pointSize) {
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

void UIRenderer::DrawButton(const SDL_Rect& rect, const std::string& label, bool hovered) {
    if (hovered) {
        hoveredButtonThisFrame = label + ":" + std::to_string(rect.x) + ":" +
            std::to_string(rect.y) + ":" + std::to_string(rect.w) + ":" + std::to_string(rect.h);
    }

    // 外框颜色：普通 (55,75,85)，悬停 (245,190,90)
    SDL_SetRenderDrawColor(renderer, hovered ? 245 : 55, hovered ? 190 : 75, hovered ? 90 : 85, 255);
    SDL_RenderFillRect(renderer, &rect);

    // 内部缩进 3px 的深色填充 (24,35,43)
    SDL_Rect inset = {rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6};
    SDL_SetRenderDrawColor(renderer, 24, 35, 43, 255);
    SDL_RenderFillRect(renderer, &inset);

    if (uiFont && !label.empty()) {
        SDL_Color color = hovered ? SDL_Color{255, 222, 125, 255} : SDL_Color{230, 238, 235, 255};
        int textW = 0, textH = 0;
        TTF_SetFontSize(uiFont, 24);
        TTF_SizeUTF8(uiFont, label.c_str(), &textW, &textH);
        DrawText(label, rect.x + (rect.w - textW) / 2, rect.y + (rect.h - textH) / 2, color, 24);
    }
}

void UIRenderer::DrawMenuPage(GameState currentState, int mouseX, int mouseY) {
    // 背景由 Engine::Render 使用地牢地板 + 半透明暗层绘制，这里只负责标题、金线与按钮。
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawLine(renderer, 0, 104, 800, 104);

    int titleWidth = 0, titleHeight = 0;
    TTF_SetFontSize(uiFont, 34);
    TTF_SizeUTF8(uiFont, "PIXEL ENGINE", &titleWidth, &titleHeight);
    DrawText("PIXEL ENGINE", (800 - titleWidth) / 2, 42, {247, 211, 130, 255}, 34);

    if (currentState == MAIN_MENU) {
        const char* labels[] = {"选择模式", "历史记录", "按键介绍", "设置", "退出游戏"};
        for (int i = 0; i < 5; ++i) {
            SDL_Rect button = {250, 170 + i * 58, 300, 46};
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

void UIRenderer::DrawHistoryPage(const SaveManager& saveManager, int mouseX, int mouseY) {
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_Rect background = {0, 0, 800, 600};
    SDL_RenderFillRect(renderer, &background);

    DrawButton({24, 24, 120, 44}, "返回", mouseX < 144 && mouseY < 68);
    DrawText("历史记录", 330, 35, {247, 211, 130, 255}, 30);
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawLine(renderer, 400, 105, 400, 560);

    DrawText("模式一  限时挑战", 72, 112, {245, 208, 118, 255}, 22);
    DrawText("模式二  迷宫探索", 482, 112, {134, 222, 190, 255}, 22);

    const int barWidth = 180;
    const float coefficient = 2.0f;
    const auto& scoreHistory = saveManager.GetScoreHistory();
    const auto& timeHistory = saveManager.GetTimeHistory();

    int maxScore = 0;
    for (const auto& record : scoreHistory) maxScore = std::max(maxScore, record.score);
    float bestTime = 0.0f;
    for (float record : timeHistory) {
        if (record > 0.0f && (bestTime == 0.0f || record < bestTime)) bestTime = record;
    }

    for (size_t i = 0; i < std::min<size_t>(10, scoreHistory.size()); ++i) {
        int y = 160 + static_cast<int>(i) * 38;
        const auto& recordItem = scoreHistory[i];
        int record = std::max(0, recordItem.score);
        bool success = recordItem.success;

        std::ostringstream label;
        label << std::setw(2) << std::setfill('0') << (i + 1) << "  " << record << " 分";
        DrawText(label.str(), 36, y - 2, {230, 238, 235, 255}, 16);

        if (success) {
            DrawText("成功", 120, y - 2, {119, 225, 157, 255}, 15);
        } else {
            DrawText("失败", 120, y - 2, {235, 80, 80, 255}, 15);
        }

        SDL_Rect track = {194, y + 3, barWidth, 14};
        SDL_SetRenderDrawColor(renderer, 53, 70, 72, 255);
        SDL_RenderFillRect(renderer, &track);
        float ratio = maxScore > 0
            ? std::log1p(coefficient * record) / std::log1p(coefficient * maxScore)
            : 0.0f;
        SDL_Rect fill = {track.x, track.y, static_cast<int>(barWidth * ratio), track.h};
        if (success) {
            SDL_SetRenderDrawColor(renderer, i == 0 ? 244 : 225, i == 0 ? 187 : 157, 76, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255);
        }
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

void UIRenderer::DrawControlsPage(int mouseX, int mouseY) {
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_Rect background = {0, 0, 800, 600};
    SDL_RenderFillRect(renderer, &background);

    DrawButton({24, 24, 120, 44}, "返回", mouseX < 144 && mouseY < 68);
    DrawText("操作说明", 330, 35, {247, 211, 130, 255}, 30);
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawLine(renderer, 80, 104, 720, 104);

    const SDL_Color heading = {134, 222, 190, 255};
    const SDL_Color body = {230, 238, 235, 255};
    DrawText("游戏中", 100, 132, heading, 22);
    DrawText("W / A / S / D    或    方向键：移动（松键后惯性滑行）", 100, 174, body, 19);
    DrawText("Shift：蓄力冲刺；Ctrl：聚焦视野", 100, 210, body, 19);
    DrawText("空格：挥剑攻击 / 破坏障碍；F11：切换全屏", 100, 246, body, 19);
    DrawText("ESC：暂停游戏", 100, 282, body, 19);
    DrawText("结算页面", 100, 326, heading, 22);
    DrawText("Enter / 空格：返回模式选择", 100, 362, body, 19);
    DrawText("菜单页面", 100, 408, heading, 22);
    DrawText("鼠标左键：选择按钮", 100, 444, body, 19);
    DrawText("1 / 2：在模式选择页快速开始对应模式", 100, 476, body, 19);
    DrawText("ESC：子页面返回；主菜单中退出", 100, 508, body, 19);
    DrawText("F1：切换主音量静音", 100, 540, body, 19);
}

void UIRenderer::DrawSettingsPage(int masterVolume, int musicVolume, int effectsVolume, bool masterMuted, int mouseX, int mouseY) {
    SDL_Rect background = {0, 0, 800, 600};
    SDL_SetRenderDrawColor(renderer, 18, 36, 43, 255);
    SDL_RenderFillRect(renderer, &background);
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawLine(renderer, 0, 104, 800, 104);

    DrawButton({24, 24, 120, 44}, "返回", mouseX < 144 && mouseY < 68);
    DrawText("设置", 360, 38, {247, 211, 130, 255}, 30);

    const char* labels[] = {"主音量", "音乐", "音效"};
    const int values[] = {masterVolume, musicVolume, effectsVolume};
    const int rowY[] = {230, 320, 410};
    for (int i = 0; i < 3; ++i) {
        DrawText(labels[i], 160, rowY[i] - 14, {230, 238, 235, 255}, 22);
        SDL_Rect track = {350, rowY[i], 300, 8};
        SDL_SetRenderDrawColor(renderer, 53, 70, 72, 255);
        SDL_RenderFillRect(renderer, &track);
        SDL_Rect fill = {track.x, track.y, track.w * values[i] / 100, track.h};
        SDL_SetRenderDrawColor(renderer, i == 1 ? 244 : 119, i == 1 ? 187 : 225, i == 1 ? 76 : 157, 255);
        SDL_RenderFillRect(renderer, &fill);
        SDL_Rect knob = {track.x + track.w * values[i] / 100 - 7, rowY[i] - 8, 14, 24};
        SDL_SetRenderDrawColor(renderer, 230, 238, 235, 255);
        SDL_RenderFillRect(renderer, &knob);
        DrawText(std::to_string(values[i]) + "%", 680, rowY[i] - 12, {230, 238, 235, 255}, 20);
    }
    DrawText(masterMuted ? "主音量已静音" : "F1：切换主音量静音", 350, 485, {134, 222, 190, 255}, 18);
}

void UIRenderer::DrawPausePage(int mouseX, int mouseY) {
    SDL_Rect dim = {0, 0, 800, 600};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
    SDL_RenderFillRect(renderer, &dim);
    SDL_Rect panel = {190, 70, 420, 470};
    SDL_SetRenderDrawColor(renderer, 45, 62, 65, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_Rect inset = {194, 74, 412, 462};
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_RenderFillRect(renderer, &inset);
    DrawText("游戏暂停", 333, 94, {247, 211, 130, 255}, 30);

    const char* labels[] = {"继续游戏", "重新开始", "设置", "退出游戏", "退出桌面"};
    for (int i = 0; i < 5; ++i) {
        SDL_Rect button = {250, 155 + i * 64, 300, 48};
        DrawButton(button, labels[i], mouseX >= button.x && mouseX < button.x + button.w &&
                   mouseY >= button.y && mouseY < button.y + button.h);
    }
}

void UIRenderer::DrawExitConfirmPage(int mouseX, int mouseY) {
    DrawPausePage(mouseX, mouseY);
    SDL_Rect dim = {0, 0, 800, 600};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(renderer, &dim);
    SDL_Rect panel = {110, 190, 580, 230};
    SDL_SetRenderDrawColor(renderer, 45, 62, 65, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_Rect inset = {114, 194, 572, 222};
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_RenderFillRect(renderer, &inset);
    DrawText("确认退出", 335, 218, {247, 211, 130, 255}, 28);
    DrawText("本次游戏数据将不会被保存", 248, 270, {230, 238, 235, 255}, 20);

    SDL_Rect cancel = {145, 330, 245, 58};
    SDL_Rect abandon = {410, 330, 245, 58};
    DrawButton(cancel, "算了，那我玩完吧", mouseX >= cancel.x && mouseX < cancel.x + cancel.w &&
               mouseY >= cancel.y && mouseY < cancel.y + cancel.h);
    DrawButton(abandon, "老子不玩了", mouseX >= abandon.x && mouseX < abandon.x + abandon.w &&
               mouseY >= abandon.y && mouseY < abandon.y + abandon.h);
}

void UIRenderer::DrawGameOver(GameMode currentMode, bool diedByTrap, bool diedByMonster, int score, float gameTimer) {
    SDL_Rect dimRect = {0, 0, 800, 600};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
    SDL_RenderFillRect(renderer, &dimRect);
    SDL_Rect panel = {180, 180, 440, 240};
    SDL_SetRenderDrawColor(renderer, 45, 62, 65, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_Rect inset = {184, 184, 432, 232};
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_RenderFillRect(renderer, &inset);

    if (currentMode == TIME_ATTACK && diedByMonster) {
        DrawText("失  败", 355, 216, {245, 80, 80, 255}, 30);
        DrawText("被怪物击杀，惨遭致死！", 305, 256, {240, 140, 140, 255}, 18);
    } else if (currentMode == TIME_ATTACK && diedByTrap) {
        DrawText("失  败", 355, 216, {245, 80, 80, 255}, 30);
        DrawText("触发致命陷阱，惨遭致死！", 305, 256, {240, 140, 140, 255}, 18);
    } else {
        DrawText("本局结束", 334, 216, {247, 211, 130, 255}, 30);
    }

    std::ostringstream result;
    if (currentMode == TIME_ATTACK) {
        result << "本局得分：" << score;
    } else {
        result << "通关用时：" << std::fixed << std::setprecision(2) << gameTimer << " 秒";
    }
    DrawText(result.str(), 285, 280, {230, 238, 235, 255}, 24);
    DrawText("Enter / 空格：返回模式选择", 248, 350, {134, 222, 190, 255}, 18);
}

void UIRenderer::DrawHUD(GameMode currentMode, float gameTimer, int score, int health, float maxTime) {
    // 生命值（左上角红心方块）
    for (int i = 0; i < health; ++i) {
        SDL_Rect heart = {20 + i * 14, 34, 10, 10};
        SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255);
        SDL_RenderFillRect(renderer, &heart);
        SDL_SetRenderDrawColor(renderer, 120, 30, 30, 255);
        SDL_RenderDrawRect(renderer, &heart);
    }

    if (currentMode == TIME_ATTACK) {
        SDL_Rect timeBG = {20, 10, 200, 15};
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(renderer, &timeBG);

        const float safeMax = (maxTime > 0.0f) ? maxTime : 1.0f;
        const float ratio = std::clamp(gameTimer / safeMax, 0.0f, 1.0f);
        int barWidth = static_cast<int>(ratio * 200.0f);
        SDL_Rect timeBar = {20, 10, barWidth, 15};

        if (gameTimer > 10.0f) {
            SDL_SetRenderDrawColor(renderer, 60, 255, 60, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
        }
        SDL_RenderFillRect(renderer, &timeBar);
    }

    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
    int dotRadius = 4;
    for (int i = 0; i < score; i++) {
        int cx = 760 - (i * 12) + dotRadius;
        int cy = 13 + dotRadius;
        for (int w = 0; w < dotRadius * 2; w++) {
            for (int h = 0; h < dotRadius * 2; h++) {
                int dx = dotRadius - w;
                int dy = dotRadius - h;
                if ((dx * dx + dy * dy) <= (dotRadius * dotRadius)) {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }
    }
}

void UIRenderer::DrawWeaponStatus(SDL_Texture* icon, const std::string& name, int ammo, int magazineSize) {
    SDL_Rect panel = {640, 536, 150, 54};
    SDL_SetRenderDrawColor(renderer, 24, 35, 43, 235);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 231, 176, 70, 255);
    SDL_RenderDrawRect(renderer, &panel);

    // 武器图标（按原比例适配到固定框内）
    if (icon) {
        int tw = 0;
        int th = 0;
        SDL_QueryTexture(icon, nullptr, nullptr, &tw, &th);
        const int box = 38;
        int dw = box;
        int dh = box;
        if (tw > 0 && th > 0) {
            if (tw >= th) {
                dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * th / tw)));
            } else {
                dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * tw / th)));
            }
        }
        const int iconCX = panel.x + 6 + box / 2;
        const int iconCY = panel.y + panel.h / 2;
        SDL_Rect iconRect = {iconCX - dw / 2, iconCY - dh / 2, dw, dh};
        SDL_RenderCopy(renderer, icon, nullptr, &iconRect);
    }

    std::string line = name;
    if (magazineSize > 0) {
        line += " " + std::to_string(ammo) + "/" + std::to_string(magazineSize);
    }
    DrawText(line, panel.x + (icon ? 50 : 12), panel.y + 13, {230, 238, 235, 255}, 18);
}

void UIRenderer::DrawNoWeaponHint(const std::string& msg) {
    int textW = 0;
    int textH = 0;
    TTF_SetFontSize(uiFont, 22);
    TTF_SizeUTF8(uiFont, msg.c_str(), &textW, &textH);
    DrawText(msg, (800 - textW) / 2, 512, {245, 200, 90, 255}, 22);
}
