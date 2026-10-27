#include "CS2DOverlay.hpp"
#include "../Graphics/UiScale.hpp"
#include "CS2DRenderer.hpp"
#include "CS2DGame.hpp"
#include "CS2DWorld.hpp"
#include "Tuning.hpp"
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
    // 准星尺寸随 UI 缩放（设置里的数值是 1920x1080 基准）
    float crosshairGap = crosshair.gap * g_uiScale;
    if (crosshair.dynamic) {
        // 动态准星：静止最紧，静步小扩散，跑动明显张开。
        crosshairGap += game->GetCrosshairBloom() *
                        Tuning::CrosshairBloomMaxPx * g_uiScale;
    }
    renderer->DrawCrosshair(virtCursorX, virtCursorY,
                            crosshair.r, crosshair.g, crosshair.b, crosshair.a,
                            crosshair.thickness * g_uiScale, crosshair.centerDot,
                            crosshair.length * g_uiScale, crosshairGap);

    // 顶部计分板已由 GameUI（ImGui）统一绘制，此处不再重复绘制

    // ---- 命中反馈：准星处短暂 X ----
    {
        const float hm = game->GetHitMarkerTimer();
        if (hm > 0.0f) {
            const float k = std::min(1.0f, hm / 0.18f);
            renderer->DrawHitMarker(virtCursorX, virtCursorY, g_uiScale,
                                    static_cast<Uint8>(220.0f * k));
        }
    }

    // ---- 受伤反馈：屏幕红色边缘 ----
    {
        const float dmg = game->GetDamageFlashTimer();
        if (dmg > 0.0f) {
            const float k = std::min(1.0f, dmg / 0.30f);
            renderer->DrawDamageVignette(static_cast<Uint8>(170.0f * k));
        }
    }

    // 收集雷达点位：友军（绿） + 出现在友军视野里的敌人（红）
    std::vector<CS2DRenderer::RadarDot> dots;
    const auto& bots = game->GetBOTs();
    const Team myTeam = game->GetPlayer().team;

    for (const auto& b : bots) {
        if (!b.alive || b.team != myTeam) continue;      // 只显示同队存活者
        dots.push_back({b.x, b.y, 90, 220, 130});        // 友军：绿色
    }
    for (const auto& c : game->GetContactsOfTeam(myTeam)) {
        if (c.age > 6.0f) continue;                      // 情报超过 6 秒不再显示
        dots.push_back({c.x, c.y, 235, 75, 70});         // 可见敌人：红色
    }

    // 掉落 C4：T 方始终可见；CT 只在情报期内可见（橙色小点）
    if (game->IsBombDropped()) {
        const bool show = (myTeam == Team::T) ||
                          (myTeam == Team::CT && game->HasBombIntel());
        if (show) {
            dots.push_back({game->BombDropX(), game->BombDropY(), 255, 170, 60});
        }
    }

    const auto& w = game->GetWorld();
    const float mapPixelW = static_cast<float>(w.GetMapWidth()  * w.GetTileWidth());
    const float mapPixelH = static_cast<float>(w.GetMapHeight() * w.GetTileHeight());

    // 左上角雷达 HUD
    renderer->DrawRadarHUD(
        game->GetPlayer().x, game->GetPlayer().y,
        game->GetAimAngle(),
        dots,
        mapPixelW, mapPixelH,
        radar.playerCentered, radar.rotateWithPlayer,
        radar.blendBg, radar.bgAlpha,
        static_cast<int>(std::lround(radar.hudSize * g_uiScale)), radar.mapZoom,
        false, viewW, viewH);

    // 中心雷达（Tab 切换）
    if (centerRadarOn) {
        renderer->DrawRadarHUD(
            game->GetPlayer().x, game->GetPlayer().y,
            game->GetAimAngle(),
            dots,
            mapPixelW, mapPixelH,
            radar.playerCentered, radar.rotateWithPlayer,
            false, 200,
            static_cast<int>(std::lround(320.0f * g_uiScale)), radar.mapZoom * 1.5f,
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