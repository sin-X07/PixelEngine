#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "UIRenderer.hpp"
#include <algorithm>
#include <cmath>

bool UIRenderer::Initialize(SDL_Renderer* ren) {
    renderer = ren;
    return true;
}

void UIRenderer::Clean() {
    renderer = nullptr;
}

void UIRenderer::DrawGameOver(GameMode currentMode, bool diedByTrap, bool diedByMonster, int score, float gameTimer) {
    (void)currentMode; (void)diedByTrap; (void)diedByMonster; (void)score; (void)gameTimer;
    // 只画面板背景（文字由 GameUI::DrawInGame 用 ImGui 绘制）
    SDL_Rect dimRect = {0, 0, 800, 600};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
    SDL_RenderFillRect(renderer, &dimRect);
    SDL_Rect panel = {180, 180, 440, 240};
    SDL_SetRenderDrawColor(renderer, 45, 62, 65, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_Rect inset = {184, 184, 432, 232};
    SDL_SetRenderDrawColor(renderer, 20, 39, 45, 255);
    SDL_RenderFillRect(renderer, &inset);
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

void UIRenderer::DrawWeaponStatus(SDL_Texture* icon) {
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
}
