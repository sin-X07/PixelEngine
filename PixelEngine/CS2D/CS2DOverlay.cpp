#include "CS2DOverlay.hpp"
#include "CS2DRenderer.hpp"
#include "CS2DGame.hpp"
#include "CS2DWorld.hpp"
#include <algorithm>
#include <cmath>

// ============================================================================
// 虚拟准星更新
// ============================================================================
void CS2DOverlay::UpdateCursor(int relX, int relY, float sensitivity, float zoom,
                                float camX, float camY,
                                int viewW, int viewH,
                                float& outWorldX, float& outWorldY) {
    virtCursorX += static_cast<float>(relX) * sensitivity;
    virtCursorY += static_cast<float>(relY) * sensitivity;
    virtCursorX = std::clamp(virtCursorX, 0.0f, static_cast<float>(viewW));
    virtCursorY = std::clamp(virtCursorY, 0.0f, static_cast<float>(viewH));

    const float centerWX = camX + viewW * 0.5f / zoom;
    const float centerWY = camY + viewH * 0.5f / zoom;
    outWorldX = centerWX + (virtCursorX - viewW * 0.5f) / zoom;
    outWorldY = centerWY + (virtCursorY - viewH * 0.5f) / zoom;
}

void CS2DOverlay::UpdateTabToggle() {
    if (keyTab && !prevTab) centerRadarOn = !centerRadarOn;
}

// ============================================================================
// 渲染
// ============================================================================
void CS2DOverlay::Render(CS2DRenderer* renderer, CS2DGame* game, int viewW, int viewH) {
    if (!renderer || !game) return;

    // 准星
    renderer->DrawCrosshair(virtCursorX, virtCursorY,
                            crosshair.r, crosshair.g, crosshair.b, crosshair.a,
                            crosshair.thickness, crosshair.centerDot,
                            crosshair.length, crosshair.gap);

    // 计分板
    const int total   = game->GetBotCount() + 1;
    const int kills   = game->GetPlayerKills();
    const int deaths  = game->GetPlayerDeaths();
    renderer->DrawScoreboard(total, kills, deaths, viewW, viewH);

    // 收集 BOT 坐标
    const auto& bots = game->GetBOTs();
    std::vector<float> bXs, bYs;
    std::vector<bool> bAlive;
    bXs.reserve(bots.size());
    bYs.reserve(bots.size());
    bAlive.reserve(bots.size());
    for (const auto& b : bots) {
        bXs.push_back(b.x);
        bYs.push_back(b.y);
        bAlive.push_back(b.alive);
    }

    const auto& w = game->GetWorld();
    const float mapPixelW = static_cast<float>(w.GetMapWidth()  * w.GetTileWidth());
    const float mapPixelH = static_cast<float>(w.GetMapHeight() * w.GetTileHeight());

    // 左上角雷达 HUD
    renderer->DrawRadarHUD(
        game->GetPlayer().x, game->GetPlayer().y,
        game->GetAimAngle(),
        bXs, bYs, bAlive,
        mapPixelW, mapPixelH,
        radar.playerCentered, radar.rotateWithPlayer,
        radar.blendBg, radar.bgAlpha,
        radar.hudSize, radar.mapZoom,
        false, viewW, viewH);

    // 中心雷达（Tab 切换）
    if (centerRadarOn) {
        renderer->DrawRadarHUD(
            game->GetPlayer().x, game->GetPlayer().y,
            game->GetAimAngle(),
            bXs, bYs, bAlive,
            mapPixelW, mapPixelH,
            radar.playerCentered, radar.rotateWithPlayer,
            false, 200,
            320, radar.mapZoom * 1.5f,
            true, viewW, viewH);
    }
}

// ============================================================================
// Setter 工厂
// ============================================================================
std::function<void(Uint8,Uint8,Uint8,Uint8)> CS2DOverlay::MakeSetCrosshairColor() {
    return [this](Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
        crosshair.r = r; crosshair.g = g; crosshair.b = b; crosshair.a = a;
    };
}
std::function<void(float)> CS2DOverlay::MakeSetCrosshairThickness() {
    return [this](float v) { crosshair.thickness = v; };
}
std::function<void(bool)> CS2DOverlay::MakeSetCrosshairCenterDot() {
    return [this](bool v) { crosshair.centerDot = v; };
}
std::function<void(float)> CS2DOverlay::MakeSetCrosshairLength() {
    return [this](float v) { crosshair.length = v; };
}
std::function<void(float)> CS2DOverlay::MakeSetCrosshairGap() {
    return [this](float v) { crosshair.gap = v; };
}
std::function<void(bool)> CS2DOverlay::MakeSetRadarPlayerCentered() {
    return [this](bool v) { radar.playerCentered = v; };
}
std::function<void(bool)> CS2DOverlay::MakeSetRadarRotateWithPlayer() {
    return [this](bool v) { radar.rotateWithPlayer = v; };
}
std::function<void(bool)> CS2DOverlay::MakeSetRadarBlendBg() {
    return [this](bool v) { radar.blendBg = v; };
}
std::function<void(int)> CS2DOverlay::MakeSetRadarBgAlpha() {
    return [this](int v) { radar.bgAlpha = v; };
}
std::function<void(int)> CS2DOverlay::MakeSetRadarHudSize() {
    return [this](int v) { radar.hudSize = v; };
}
std::function<void(float)> CS2DOverlay::MakeSetRadarMapZoom() {
    return [this](float v) { radar.mapZoom = v; };
}