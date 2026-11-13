#include "VisibilityQuery.hpp"

#include "../CS2DWorld.hpp"

#include <cmath>
#include <limits>

// ============================================================================
// 视线与瞄准线共用挡弹网格 DDA，直接访问穿过的格子并返回精确入格交点。
// ============================================================================
float VisibilityQuery::FirstBulletWallFraction(float fromX, float fromY, float toX, float toY) const {
    if (!world_) return 1.0f;
    int row = 0, col = 0;
    world_->WorldToTile(fromX, fromY, row, col);
    if (world_->IsBlockingBullet(row, col)) return 0.0f;
    const float dx = toX - fromX, dy = toY - fromY;
    const float infinity = std::numeric_limits<float>::infinity();
    const int stepX = dx > 0.0f ? 1 : -1, stepY = dy > 0.0f ? 1 : -1;
    const float tileW = static_cast<float>(world_->GetTileWidth());
    const float tileH = static_cast<float>(world_->GetTileHeight());
    const float deltaX = dx != 0.0f ? tileW / std::abs(dx) : infinity;
    const float deltaY = dy != 0.0f ? tileH / std::abs(dy) : infinity;
    float nextX = dx != 0.0f ? ((col + (stepX > 0 ? 1 : 0)) * tileW - fromX) / dx : infinity;
    float nextY = dy != 0.0f ? ((row + (stepY > 0 ? 1 : 0)) * tileH - fromY) / dy : infinity;
    while (std::min(nextX, nextY) <= 1.0f) {
        const float entry = std::min(nextX, nextY);
        const bool crossX = nextX <= nextY;
        const bool crossY = nextY <= nextX;
        if (crossX && world_->IsBlockingBullet(row, col + stepX)) return std::max(0.0f, entry);
        if (crossY && world_->IsBlockingBullet(row + stepY, col)) return std::max(0.0f, entry);
        if (crossX) { col += stepX; nextX += deltaX; }
        if (crossY) { row += stepY; nextY += deltaY; }
        if (crossX && crossY && world_->IsBlockingBullet(row, col)) return std::max(0.0f, entry);
    }
    return 1.0f;
}

bool VisibilityQuery::HasLineOfSight(float fromX, float fromY, float toX, float toY) const {
    if (!world_) return false;

    const float dx = toX - fromX, dy = toY - fromY;
    if (dx * dx + dy * dy < 1.0f) return true;
    int row = 0, col = 0;
    world_->WorldToTile(toX, toY, row, col);
    return !world_->IsBlockingBullet(row, col) &&
        FirstBulletWallFraction(fromX, fromY, toX, toY) >= 1.0f;
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
    if (!distanceLimited_) distFactor = 1.0f;
    if (distFactor <= 0.0f) return 0.0f;

    // 2) 角度因子：以朝向为中心的扇形，扇形外完全不可见
    const float halfConeRad  = (coneDeg_ * 0.5f - kAngleSoftDeg * 0.5f) * 3.14159265f / 180.0f;
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

float VisibilityQuery::RadiusForViewport(int, int, float) {
    // PIX-20: preserve the former 1728x1080 reference view, independently
    // of the actual viewport, aspect ratio and camera zoom.
    static const float radius = [] {
        const float zoom = BaseZoomForViewport(1728, 1080);
        const float halfW = 864.0f / zoom;
        const float halfH = 540.0f / zoom;
        return std::clamp(std::sqrt(halfW * halfW + halfH * halfH) + kSoftEdge,
                          Tuning::VisionRadius, kMaxRadius);
    }();
    return radius;
}
