#include "CS2DOverlay.hpp"
#include "../Graphics/UiScale.hpp"
#include "CS2DRenderer.hpp"
#include "CS2DGame.hpp"
#include "CS2DWorld.hpp"
#include "Tuning.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

// ============================================================================
// 虚拟准星更新
// ============================================================================
void CS2DOverlay::InitializeCursor(float angle) {
    aimOffsetX_ = std::cos(angle) * Tuning::InitialAimDistance;
    aimOffsetY_ = std::sin(angle) * Tuning::InitialAimDistance;
    cursorScoped_ = false;
    screenAnchor_ = scopeClosing_ = edgeZoomRequested_ = finishHandoff_ = false;
    handoffElapsed_ = 0.0f;
    visibleWorldValid_ = anchorWorldValid_ = false;
    referenceModeZoom_ = 1.0f;
    referenceCameraShift_ = Tuning::CameraLookShift;
    referenceAngled_ = limitAngled_ = false;
}

void CS2DOverlay::SetReferenceMode(float zoom, float cameraShift, bool angled, bool normalAngled) {
    referenceModeZoom_ = std::max(zoom, 1.0f);
    referenceCameraShift_ = std::clamp(cameraShift, 0.0f, Tuning::CameraLookShift);
    referenceAngled_ = angled;
    limitAngled_ = angled || normalAngled;
}

void CS2DOverlay::SyncScopeCursor(bool scoped, bool closing, bool cameraReady, float dt, bool commitAllowed) {
    if (cursorScoped_ && !scopeClosing_ && (closing || !scoped)) {
        anchorX_ = virtCursorX;
        anchorY_ = virtCursorY;
        anchorWorld_ = visibleWorld_;
        anchorWorldValid_ = visibleWorldValid_;
        screenAnchor_ = true;
        finishHandoff_ = false;
        handoffElapsed_ = 0.0f;
    }
    if (screenAnchor_) {
        handoffElapsed_ += std::max(dt, 0.0f);
        finishHandoff_ = commitAllowed && ((scoped && !closing) || (!scoped &&
            (cameraReady || handoffElapsed_ >= Tuning::AimHandoffMaxSeconds)));
    }
    scopeClosing_ = closing;
    cursorScoped_ = scoped;
}

void CS2DOverlay::UpdateCursor(int relX, int relY, float sensitivity, float zoom,
                              float camX, float camY, int viewW, int viewH,
                              float playerX, float playerY, float& outWorldX, float& outWorldY,
                              float recoilAngle, SDL_FPoint renderOffset) {
    zoom = std::max(zoom, 0.001f);
    const float motionX = static_cast<float>(relX) * sensitivity;
    const float motionY = static_cast<float>(relY) * sensitivity;
    const bool moved = relX != 0 || relY != 0;
    const float referenceZoom = VisibilityQuery::BaseZoomForViewport(viewW, viewH) * referenceModeZoom_;
    const float halfW = viewW * 0.5f / referenceZoom;
    const float halfH = viewH * 0.5f / referenceZoom;

    if (screenAnchor_) {
        if (!anchorWorldValid_) {
            anchorWorld_ = {camX + anchorX_ / zoom, camY + anchorY_ / zoom};
            anchorWorldValid_ = true;
        }
        anchorWorld_.x += motionX / referenceZoom;
        anchorWorld_.y += motionY / referenceZoom;
        outWorldX = anchorWorld_.x;
        outWorldY = anchorWorld_.y;
        anchorX_ = (anchorWorld_.x - camX) * zoom;
        anchorY_ = (anchorWorld_.y - camY) * zoom;
        // The anchor stores the visible, recoil-adjusted point. Invert recoil once
        // so gameplay and the eventual base offset reproduce that same point.
        const float originX = playerX + (finishHandoff_ ? renderOffset.x : 0.0f);
        const float originY = playerY + (finishHandoff_ ? renderOffset.y : 0.0f);
        const float dx = outWorldX - originX, dy = outWorldY - originY;
        outWorldX = playerX + dx * std::cos(recoilAngle) + dy * std::sin(recoilAngle);
        outWorldY = playerY - dx * std::sin(recoilAngle) + dy * std::cos(recoilAngle);
        virtCursorX = anchorX_;
        virtCursorY = anchorY_;
        if (!finishHandoff_) return; // Keep the base offset and edge target frozen during handoff.
        aimOffsetX_ = outWorldX - playerX;
        aimOffsetY_ = outWorldY - playerY;
        screenAnchor_ = false;
        finishHandoff_ = false;
    } else {
        // Input uses the fixed mode scale, independent of animated edge zoom and camera smoothing.
        aimOffsetX_ += motionX / referenceZoom;
        aimOffsetY_ += motionY / referenceZoom;
    }

    // Logical camera ranges include map clamping, using only fixed reference zooms.
    auto cameraRange = [](float halfExtent, float player, float worldExtent) {
        const float infinity = std::numeric_limits<float>::infinity();
        if (worldExtent <= 0.0f) return std::pair<float, float>{-infinity, infinity};
        const float lo = -Tuning::VisionRadius;
        const float hi = std::max(lo, worldExtent - halfExtent * 2.0f + Tuning::VisionRadius);
        return std::pair<float, float>{lo + halfExtent - player, hi + halfExtent - player};
    };
    const auto rangeX = cameraRange(halfW, playerX, referenceWorldW_);
    const auto rangeY = cameraRange(halfH, playerY, referenceWorldH_);
    if (moved) {
        const float limitZoom = VisibilityQuery::BaseZoomForViewport(viewW, viewH) * Tuning::EdgeZoomOutMin;
        const float maxHalfW = viewW * 0.5f / limitZoom;
        const float maxHalfH = viewH * 0.5f / limitZoom;
        const auto maxRangeX = cameraRange(maxHalfW, playerX, referenceWorldW_);
        const auto maxRangeY = cameraRange(maxHalfH, playerY, referenceWorldH_);
        if (limitAngled_) {
            const float distance = std::max(std::hypot(aimOffsetX_, aimOffsetY_), 0.001f);
            const float ux = aimOffsetX_ / distance, uy = aimOffsetY_ / distance;
            const float cx = std::clamp(ux * Tuning::CameraAngledLength, maxRangeX.first, maxRangeX.second);
            const float cy = std::clamp(uy * Tuning::CameraAngledLength, maxRangeY.first, maxRangeY.second);
            const float infinity = std::numeric_limits<float>::infinity();
            const float limitX = ux != 0.0f ? (cx + std::copysign(maxHalfW, ux)) / ux : infinity;
            const float limitY = uy != 0.0f ? (cy + std::copysign(maxHalfH, uy)) / uy : infinity;
            const float scale = std::clamp(std::min(limitX, limitY) / distance, 0.0f, 1.0f);
            aimOffsetX_ *= scale;
            aimOffsetY_ *= scale;
        } else {
            const float cameraLimitX = Tuning::CameraLookShift * maxHalfW / (1.0f - Tuning::CameraLookShift);
            const float cameraLimitY = Tuning::CameraLookShift * maxHalfH / (1.0f - Tuning::CameraLookShift);
            const float minX = std::clamp(-cameraLimitX, maxRangeX.first, maxRangeX.second) - maxHalfW;
            const float maxX = std::clamp(cameraLimitX, maxRangeX.first, maxRangeX.second) + maxHalfW;
            const float minY = std::clamp(-cameraLimitY, maxRangeY.first, maxRangeY.second) - maxHalfH;
            const float maxY = std::clamp(cameraLimitY, maxRangeY.first, maxRangeY.second) + maxHalfH;
            aimOffsetX_ = std::clamp(aimOffsetX_, minX, maxX);
            aimOffsetY_ = std::clamp(aimOffsetY_, minY, maxY);
        }
    }
    const float distance = std::max(std::hypot(aimOffsetX_, aimOffsetY_), 0.001f);
    const float cameraShift = referenceAngled_
        ? std::min(1.0f, Tuning::CameraAngledLength / distance) : referenceCameraShift_;
    const float cameraX = std::clamp(aimOffsetX_ * cameraShift, rangeX.first, rangeX.second);
    const float cameraY = std::clamp(aimOffsetY_ * cameraShift, rangeY.first, rangeY.second);
    const float referenceX = aimOffsetX_ - cameraX;
    const float referenceY = aimOffsetY_ - cameraY;
    const float edgeDistance = std::min(halfW - std::abs(referenceX), halfH - std::abs(referenceY));
    const float band = std::max(halfW, halfH) * 2.0f * Tuning::EdgeZoomOutZone;
    if (edgeZoomRequested_) {
        if (edgeDistance >= band * Tuning::EdgeZoomHysteresis) edgeZoomRequested_ = false;
    } else if (edgeDistance < band) {
        edgeZoomRequested_ = true;
    }
    outWorldX = playerX + aimOffsetX_;
    outWorldY = playerY + aimOffsetY_;
    virtCursorX = (outWorldX - camX) * zoom;
    virtCursorY = (outWorldY - camY) * zoom;
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
    // 契约 v1.3：开镜期间不画普通准星，改用镜筒里的大十字（四象限）
    const Presentation::ScopeView scope = game->GetScopeView();
    const bool scoped = scope.active && scope.supported;
    const float reticleX = (game->GetRenderAimX() - renderer->GetCameraX()) * renderer->GetZoom();
    const float reticleY = (game->GetRenderAimY() - renderer->GetCameraY()) * renderer->GetZoom();
    SetVisibleAimPoint(game->GetRenderAimX(), game->GetRenderAimY());
    virtCursorX = reticleX;
    virtCursorY = reticleY;
    // 准星尺寸随 UI 缩放（设置里的数值是 1920x1080 基准）
    float crosshairGap = crosshair.gap * g_uiScale;
    if (crosshair.dynamic) {
        // 动态准星：静止最紧，静步小扩散，跑动明显张开。
        crosshairGap += game->GetCrosshairBloom() *
                        Tuning::CrosshairBloomMaxPx * g_uiScale;
    }
    if (!scoped) {
        const auto& player = game->GetPlayer();
        const auto playerView = game->GetActorView(player);
        const WeaponClass cls = player.CurrentWeapon().cls;
        if (crosshair.aimLine && player.alive && !game->IsSpectating() &&
            cls != WeaponClass::Melee && cls != WeaponClass::Grenade && cls != WeaponClass::Bomb) {
            renderer->DrawAimLine(playerView.x, playerView.y, player.size, game->GetRenderAimAngle(),
                player.weaponSlot, game->GetRenderAimX(), game->GetRenderAimY(), aimLineStyle, g_uiScale);
        }
        renderer->DrawCrosshair(reticleX, reticleY,
                                crosshair.r, crosshair.g, crosshair.b, crosshair.a,
                                crosshair.thickness * g_uiScale, crosshair.centerDot,
                                crosshair.length * g_uiScale, crosshairGap);
    }

    // 顶部计分板已由 GameUI（ImGui）统一绘制，此处不再重复绘制

    // ---- 命中反馈：准星处短暂 X ----
    {
        const float hm = game->GetHitMarkerTimer();
        if (hm > 0.0f) {
            const float k = std::min(1.0f, hm / 0.18f);
            renderer->DrawHitMarker(reticleX, reticleY, g_uiScale,
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

    // 死斗 = 一人成军：雷达不显示任何 BOT 位置——
    // 既没有"队友常驻绿点"，也不显示情报红点（FFA 里 9 个 BOT 一直在互相开火，
    // 情报点会持续铺满全图，等于全图透视）。
    const bool dmMode = game->IsDeathMatch();
    if (!dmMode) {
        for (const auto& b : bots) {
            if (!b.alive || b.team != myTeam) continue;  // 只显示同队存活者
            dots.push_back({b.x, b.y, 90, 220, 130});    // 友军：绿色
        }
        for (const auto& c : game->GetContactsOfTeam(myTeam)) {
            if (c.age > 6.0f) continue;                  // 情报超过 6 秒不再显示
            dots.push_back({c.x, c.y, 235, 75, 70, static_cast<Uint8>(255.0f * std::clamp((Tuning::RadarFadeEnd - c.age) / (Tuning::RadarFadeEnd - Tuning::RadarFadeStart), 0.0f, 1.0f))});     // 可见敌人：红色
        }
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
