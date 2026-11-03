#include "VisibilityQuery.hpp"

#include "../CS2DWorld.hpp"

#include <cmath>

// ============================================================================
// 视线：沿线段按半个瓦片步长采样，遇到阻挡子弹的瓦片即判定不可见
// （与 CS2DRenderer::HasLineOfSight 原实现逐行等价）
// ============================================================================
bool VisibilityQuery::HasLineOfSight(float fromX, float fromY, float toX, float toY) const {
    if (!world_) return false;

    const int tileW = world_->GetTileWidth();
    const int tileH = world_->GetTileHeight();
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) return true;

    const int steps = std::max(2, static_cast<int>(dist / (std::min(tileW, tileH) * 0.5f)));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const float sx = fromX + dx * t;
        const float sy = fromY + dy * t;
        int row = 0, col = 0;
        world_->WorldToTile(sx, sy, row, col);
        if (world_->IsBlockingBullet(row, col)) return false;
    }
    return true;
}

// ============================================================================
// 可见度：[0,1]，距离因子 × 扇形因子，再乘墙体遮挡（遮挡为 0/1）
// ============================================================================
float VisibilityQuery::ComputeVisibility(float playerX, float playerY, float aimAngle,
                                        float targetX, float targetY) const {
    if (!world_) return 0.0f;

    // 开镜：以准星为圆心的圆（几何定义与回归测试共用 Presentation::ComputeScopeVisibility）
    if (scopeActive_) {
        const float circleFactor = Presentation::ComputeScopeVisibility(
            scopeX_, scopeY_, scopeR_, targetX, targetY);
        if (circleFactor <= 0.0f) return 0.0f;
        if (!HasLineOfSight(playerX, playerY, targetX, targetY)) return 0.0f;
        return circleFactor;
    }

    const float deltaX = targetX - playerX;
    const float deltaY = targetY - playerY;
    const float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);

    // 1) 距离因子：半径内 1.0，边缘 smoothstep → 0.0
    const float fullyLitRadius = radius_ - kSoftEdge;
    float distFactor = 0.0f;
    if (dist <= fullyLitRadius) {
        distFactor = 1.0f;
    } else if (dist < radius_) {
        float blend = (dist - fullyLitRadius) / kSoftEdge;
        blend = blend * blend * (3.0f - 2.0f * blend);
        distFactor = 1.0f - blend;
    }
    if (distFactor <= 0.0f) return 0.0f;

    // 2) 角度因子：以朝向为中心的扇形，扇形外完全不可见
    const float halfConeRad  = (kConeDeg * 0.5f - kAngleSoftDeg * 0.5f) * 3.14159265f / 180.0f;
    const float angleSoftRad = kAngleSoftDeg * 3.14159265f / 180.0f;

    const float targetAngle = std::atan2(deltaY, deltaX);
    float diff = targetAngle - aimAngle;
    while (diff > 3.14159265f)  diff -= 6.28318531f;
    while (diff < -3.14159265f) diff += 6.28318531f;

    const float absDiff = std::abs(diff);
    float coneFactor = 0.0f;
    if (absDiff <= halfConeRad) {
        coneFactor = 1.0f;
    } else if (absDiff < halfConeRad + angleSoftRad) {
        float blend = (absDiff - halfConeRad) / angleSoftRad;
        blend = blend * blend * (3.0f - 2.0f * blend);
        coneFactor = 1.0f - blend;
    }
    if (coneFactor <= 0.0f) return 0.0f;

    // 3) 遮挡
    if (!HasLineOfSight(playerX, playerY, targetX, targetY)) return 0.0f;

    return std::min(distFactor, coneFactor);
}

// ============================================================================
// 视口换算（原 CS2DRenderer::UpdateCamera 的两段公式，抽出来给无头仿真复用）
// ============================================================================
float VisibilityQuery::BaseZoomForViewport(int viewportW, int viewportH) {
    const float minView = static_cast<float>(std::min(viewportW, viewportH));
    const float fitZoom = minView / (2.0f * Tuning::VisionRadius) * kBaseZoomScale;
    return std::clamp(fitZoom, 1.2f, 6.0f);
}

float VisibilityQuery::RadiusForViewport(int viewportW, int viewportH, float baseZoom) {
    if (baseZoom <= 0.0f) return Tuning::VisionRadius;
    const float baseViewW = static_cast<float>(viewportW) / baseZoom;
    const float baseViewH = static_cast<float>(viewportH) / baseZoom;
    const float halfW = baseViewW * 0.5f;
    const float halfH = baseViewH * 0.5f;
    const float screenHalfDiag = std::sqrt(halfW * halfW + halfH * halfH);
    return std::clamp(screenHalfDiag + kSoftEdge, Tuning::VisionRadius, kMaxRadius);
}
