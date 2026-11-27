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
    cameraDistanceTier_ = 0;
    screenAnchor_ = scopeClosing_ = edgeZoomRequested_ = finishHandoff_ = false;
    handoffElapsed_ = 0.0f;
    visibleWorldValid_ = anchorWorldValid_ = false;
    referenceModeZoom_ = 1.0f;
    referenceCameraShift_ = Tuning::CameraLookShift;

}

void CS2DOverlay::SetReferenceMode(float zoom, float cameraShift) {
    referenceModeZoom_ = std::max(zoom, 1.0f);
    referenceCameraShift_ = std::clamp(cameraShift, 0.0f, Tuning::CameraLookShift);

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
                              float recoilAngle, Vec2 renderOffset, Vec2 ballisticKick) {
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
        const auto base=Ballistics::RemoveKick({outWorldX-playerX,outWorldY-playerY},ballisticKick);
        outWorldX=playerX+base.x;outWorldY=playerY+base.y;
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
        const float limitZoom = VisibilityQuery::BaseZoomForViewport(viewW, viewH) * .50f;
        const float maxHalfW = viewW * 0.5f / limitZoom;
        const float maxHalfH = viewH * 0.5f / limitZoom;
        const auto maxRangeX = cameraRange(maxHalfW, playerX, referenceWorldW_);
        const auto maxRangeY = cameraRange(maxHalfH, playerY, referenceWorldH_);

        const float cameraLimitX = Tuning::CameraLookShift * maxHalfW / (1.0f - Tuning::CameraLookShift);
        const float cameraLimitY = Tuning::CameraLookShift * maxHalfH / (1.0f - Tuning::CameraLookShift);
        const float minX = std::clamp(-cameraLimitX, maxRangeX.first, maxRangeX.second) - maxHalfW;
        const float maxX = std::clamp(cameraLimitX, maxRangeX.first, maxRangeX.second) + maxHalfW;
        const float minY = std::clamp(-cameraLimitY, maxRangeY.first, maxRangeY.second) - maxHalfH;
        const float maxY = std::clamp(cameraLimitY, maxRangeY.first, maxRangeY.second) + maxHalfH;
        aimOffsetX_ = std::clamp(aimOffsetX_, minX, maxX);
        aimOffsetY_ = std::clamp(aimOffsetY_, minY, maxY);
    }
    const float cameraShift = referenceCameraShift_;
    const float cameraX = std::clamp(aimOffsetX_ * cameraShift, rangeX.first, rangeX.second);
    const float cameraY = std::clamp(aimOffsetY_ * cameraShift, rangeY.first, rangeY.second);
    const float referenceX = aimOffsetX_ - cameraX;
    const float referenceY = aimOffsetY_ - cameraY;
    const float ratios[] = {1.12f,.70f,.50f};
    auto edgeRatio = [&](int tier) {
        const float hw=halfW/ratios[tier],hh=halfH/ratios[tier];
        const auto rx=cameraRange(hw,playerX,referenceWorldW_);
        const auto ry=cameraRange(hh,playerY,referenceWorldH_);
        const float x=aimOffsetX_-std::clamp(aimOffsetX_*cameraShift,rx.first,rx.second);
        const float y=aimOffsetY_-std::clamp(aimOffsetY_*cameraShift,ry.first,ry.second);
        return std::max(std::abs(x)/hw,std::abs(y)/hh);
    };
    if (cameraDistanceTier_ < 2 && edgeRatio(cameraDistanceTier_) >= .985f) ++cameraDistanceTier_;
    else if (cameraDistanceTier_ > 0 && edgeRatio(cameraDistanceTier_-1) <= .90f) --cameraDistanceTier_;
    edgeZoomRequested_=cameraDistanceTier_>0;
    outWorldX = playerX + aimOffsetX_;
    outWorldY = playerY + aimOffsetY_;
    virtCursorX = (outWorldX - camX) * zoom;
    virtCursorY = (outWorldY - camY) * zoom;
}

std::vector<CS2DRenderer::RadarDot> CS2DOverlay::CollectRadarDots(const CS2DGame& game) {
    using Shape = CS2DRenderer::RadarDot::Shape;
    std::vector<CS2DRenderer::RadarDot> dots;
    if (game.IsDeathMatch()) return dots;
    const Team myTeam = game.GetPlayer().team;
    const ActorId localId = game.GetLocalActorId();
    const ActorId carrier = game.GetBombCarrierId();
    for (const auto& actor : game.GetActors()) {
        if (!actor.IsActive() || !actor.alive || actor.team != myTeam || actor.id == localId) continue;
        if (actor.id != carrier)
            dots.push_back({actor.x, actor.y, 90, 220, 130});
    }
    for (const auto& contact : game.GetContactsOfTeam(myTeam)) {
        if (contact.age > Tuning::RadarFadeEnd) continue;
        const auto alpha = static_cast<Uint8>(255.0f * std::clamp(
            (Tuning::RadarFadeEnd - contact.age) / (Tuning::RadarFadeEnd - Tuning::RadarFadeStart), 0.0f, 1.0f));
        if (carrier != kInvalidActorId && contact.id == carrier) {
            dots.push_back({contact.x, contact.y, 255, 170, 60, alpha, Shape::Carrier});
        } else {
            dots.push_back({contact.x, contact.y, 235, 75, 70, alpha});
        }
    }
    if (myTeam == Team::T) {
        if (const auto pos = game.GetBombCarrierPos())
            dots.push_back({pos->x, pos->y, 255, 170, 60, 255, Shape::Carrier});
    }
    if (game.IsBombDropped() && (myTeam == Team::T || (myTeam == Team::CT && game.HasBombIntel())))
        dots.push_back({game.BombDropX(), game.BombDropY(), 255, 170, 60, 255, Shape::DroppedBomb});
    return dots;
}

// ============================================================================
// 渲染
// ============================================================================
void CS2DOverlay::Render(CS2DRenderer* renderer, CS2DGame* game, int viewW, int viewH) {
    if (!renderer || !game) return;

    // 准星
    // 契约 v1.3：开镜期间不画普通准星，改用镜筒里的大十字（四象限）
    const Presentation::ScopeView scope = game->GetRenderScopeView();
    const bool scoped = scope.supported && (scope.active || scope.transition > .001f);
    const float reticleX = (game->GetRenderAimX() - renderer->GetCameraX()) * renderer->GetZoom();
    const float reticleY = (game->GetRenderAimY() - renderer->GetCameraY()) * renderer->GetZoom();
    SetVisibleAimPoint(game->GetRenderAimX(), game->GetRenderAimY());
    virtCursorX = reticleX;
    virtCursorY = reticleY;
    // 准星尺寸随 UI 缩放（设置里的数值是 1920x1080 基准）
    float crosshairGap = crosshair.gap * g_uiScale;
    if (crosshair.dynamic) {
        // 动态准星：静止最紧，静步小扩散，跑动明显张开。
        // 开火后坐力额外张开的量单独相加（与移动扩散互不覆盖）。
        const float moveBloom = game->GetCrosshairBloom();
        const float recoilBloom = game->GetRecoilBloom();
        crosshairGap += (moveBloom + recoilBloom) *
                        Tuning::CrosshairBloomMaxPx * g_uiScale;
    }
    const bool localAimVisible = game->GetPlayer().alive && !game->IsSpectating();
    if (!scoped && localAimVisible) {
        const auto& player = game->GetPlayer();
        const auto playerView = game->GetActorView(player);
        const WeaponClass cls = player.CurrentWeapon().cls;
        if (crosshair.aimLine && player.alive && !game->IsSpectating() &&
            cls != WeaponClass::Sniper && cls != WeaponClass::Melee && cls != WeaponClass::Grenade && cls != WeaponClass::Bomb) {
            // 移动模糊只应对「真正在跑动」生效。回合开始前的准备/冻结阶段
            // 玩家不能移动，但上一帧残留的 bloom 会让瞄准线看起来发虚，
            // 这里按当前是否真的可动来决定是否传模糊量。
            const float blur = game->IsMovementAllowed() ? game->GetAimMotionBlur() : 0.0f;
            renderer->DrawAimLine(playerView.x, playerView.y, game->GetRenderAimAngle(), aimLineStyle, g_uiScale, blur);
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
        if (hm > 0.0f && localAimVisible) {
            const auto kind = game->GetHitMarkerKind();
            const bool kill = kind == CS2DGame::HitMarkerKind::Kill;
            const SDL_Color color = kill ? SDL_Color{244, 83, 72, 255} :
                kind == CS2DGame::HitMarkerKind::Armor ? SDL_Color{110, 193, 255, 255} : SDL_Color{255, 255, 255, 255};
            const float k = std::min(1.0f, hm / (kill ? 0.30f : 0.18f));
            renderer->DrawHitMarker(reticleX, reticleY, g_uiScale * (kill ? 1.5f : 1.0f),
                                    static_cast<Uint8>(220.0f * k), color);
        }
    }

    renderer->DrawDamageVignette(static_cast<Uint8>(85.0f *
        std::clamp(game->GetDamageFlashTimer() / 0.30f, 0.0f, 1.0f)));

    const auto dots = CollectRadarDots(*game);

    const auto& w = game->GetWorld();
    const float mapPixelW = static_cast<float>(w.GetMapWidth()  * w.GetTileWidth());
    const float mapPixelH = static_cast<float>(w.GetMapHeight() * w.GetTileHeight());

    // Tab 期间以全图替换小雷达。整图缩入圆形内接区域，避免裁掉地图角落。
    const float fullMapZoom = 0.90f * std::max(mapPixelW, mapPixelH) /
        std::max(1.0f, std::hypot(mapPixelW, mapPixelH));
    renderer->DrawRadarHUD(
        game->GetPlayer().x, game->GetPlayer().y, game->GetAimAngle(), dots,
        mapPixelW, mapPixelH,
        showScoreboard ? false : radar.playerCentered,
        showScoreboard ? false : radar.rotateWithPlayer,
        showScoreboard ? true : radar.blendBg,
        showScoreboard ? 225 : radar.bgAlpha,
        static_cast<int>(std::lround(showScoreboard ? ScoreboardMapSize(viewW, viewH) : radar.hudSize * g_uiScale)),
        showScoreboard ? fullMapZoom : radar.mapZoom,
        false, viewW, viewH, showScoreboard);

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
