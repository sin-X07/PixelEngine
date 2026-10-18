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
    // 全屏半透暗色蒙版（视口尺寸动态）
    SDL_Rect dimRect = {0, 0, viewW_, viewH_};
    SDL_SetRenderDrawColor(renderer, 10, 14, 22, 200);
    SDL_RenderFillRect(renderer, &dimRect);

    // 面板：深蓝灰底 + 细金边（水平居中）
    const int panelW = 470, panelH = 270;
    const int panelX = (viewW_ - panelW) / 2;
    const int panelY = (viewH_ - panelH) / 2;
    SDL_Rect panel = {panelX, panelY, panelW, panelH};
    SDL_SetRenderDrawColor(renderer, 28, 40, 56, 250);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 200, 150, 60, 140);
    SDL_RenderDrawRect(renderer, &panel);

    // 内面板：更深的背景
    SDL_Rect inset = {panelX + 6, panelY + 6, panelW - 12, panelH - 12};
    SDL_SetRenderDrawColor(renderer, 14, 22, 32, 250);
    SDL_RenderFillRect(renderer, &inset);

    // 顶部分隔线（金色细线）
    SDL_SetRenderDrawColor(renderer, 180, 135, 55, 180);
    SDL_RenderDrawLine(renderer, panelX + 15, panelY + 50, panelX + panelW - 15, panelY + 50);
}

void UIRenderer::DrawHUD(GameMode currentMode, float gameTimer, int score, int health, float maxTime) {
    // 生命值（左上角精致红心：暗底实心+亮底描边）
    for (int i = 0; i < health; ++i) {
        const int hx = 18 + i * 16;
        const int hy = 32;
        const int hw = 10;
        const int hh = 10;
        SDL_Rect heart = {hx, hy, hw, hh};
        // 暗底
        SDL_SetRenderDrawColor(renderer, 80, 18, 18, 255);
        SDL_RenderFillRect(renderer, &heart);
        // 亮芯
        SDL_Rect heartInner = {hx + 1, hy + 1, hw - 2, hh - 2};
        SDL_SetRenderDrawColor(renderer, 230, 50, 50, 255);
        SDL_RenderFillRect(renderer, &heartInner);
        // 高亮上沿
        SDL_SetRenderDrawColor(renderer, 255, 110, 110, 180);
        SDL_RenderDrawLine(renderer, hx + 1, hy + 1, hx + hw - 2, hy + 1);
    }

    if (currentMode == TIME_ATTACK) {
        // 时间条背景（深灰 + 细边框）
        const int tbx = 18;
        const int tby = 10;
        const int tbw = 210;
        const int tbh = 15;
        SDL_Rect timeBG = {tbx, tby, tbw, tbh};
        SDL_SetRenderDrawColor(renderer, 24, 30, 36, 255);
        SDL_RenderFillRect(renderer, &timeBG);
        SDL_SetRenderDrawColor(renderer, 60, 70, 82, 200);
        SDL_RenderDrawRect(renderer, &timeBG);

        const float safeMax = (maxTime > 0.0f) ? maxTime : 1.0f;
        const float ratio = std::clamp(gameTimer / safeMax, 0.0f, 1.0f);
        int barWidth = static_cast<int>(ratio * static_cast<float>(tbw - 2)) + 1;
        if (barWidth < 1) barWidth = 1;
        SDL_Rect timeBar = {tbx + 1, tby + 1, barWidth, tbh - 2};

        if (gameTimer > 10.0f) {
            // 安全区：翠绿渐变（上亮下暗）
            SDL_SetRenderDrawColor(renderer, 40, 180, 80, 255);
            SDL_RenderFillRect(renderer, &timeBar);
            SDL_SetRenderDrawColor(renderer, 90, 230, 120, 180);
            SDL_RenderDrawLine(renderer, tbx + 1, tby + 1, tbx + barWidth, tby + 1);
        } else {
            // 危险区：红橙渐变带呼吸感
            SDL_SetRenderDrawColor(renderer, 200, 45, 40, 255);
            SDL_RenderFillRect(renderer, &timeBar);
            SDL_SetRenderDrawColor(renderer, 255, 110, 80, 180);
            SDL_RenderDrawLine(renderer, tbx + 1, tby + 1, tbx + barWidth, tby + 1);
            // <30s 闪烁脉冲：底边额外红色辉光
            if (gameTimer < 30.0f && (static_cast<int>(gameTimer * 10) % 2 == 0)) {
                SDL_Rect pulse = {tbx - 1, tby - 1, tbw + 2, tbh + 2};
                SDL_SetRenderDrawColor(renderer, 255, 40, 40, 60);
                SDL_RenderDrawRect(renderer, &pulse);
            }
        }
    }

    // 金币得分（右上角金色圆点阵）
    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
    int dotRadius = 4;
    for (int i = 0; i < score; i++) {
        int cx = viewW_ - 40 - (i * 12) + dotRadius;
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
    // 武器面板：深色底 + 暖金边框 + 底部微光（右下角定位）
    const int panelW = 156, panelH = 58;
    const int panelX = viewW_ - panelW - 10;
    const int panelY = viewH_ - panelH - 10;
    SDL_Rect panel = {panelX, panelY, panelW, panelH};
    SDL_SetRenderDrawColor(renderer, 18, 28, 38, 240);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 200, 155, 60, 200);
    SDL_RenderDrawRect(renderer, &panel);
    // 底沿金色高亮线
    SDL_SetRenderDrawColor(renderer, 230, 185, 80, 140);
    SDL_RenderDrawLine(renderer, panel.x + 2, panel.y + panel.h - 1, panel.x + panel.w - 2, panel.y + panel.h - 1);

    // 武器图标（按原比例适配到固定框内）
    if (icon) {
        int tw = 0;
        int th = 0;
        SDL_QueryTexture(icon, nullptr, nullptr, &tw, &th);
        const int box = 42;
        int dw = box;
        int dh = box;
        if (tw > 0 && th > 0) {
            if (tw >= th) {
                dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * th / tw)));
            } else {
                dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * tw / th)));
            }
        }
        const int iconCX = panel.x + 8 + box / 2;
        const int iconCY = panel.y + panel.h / 2;
        SDL_Rect iconRect = {iconCX - dw / 2, iconCY - dh / 2, dw, dh};
        SDL_RenderCopy(renderer, icon, nullptr, &iconRect);
    }
}
