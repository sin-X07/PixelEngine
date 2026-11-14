#pragma once
// ============================================================================
// VisibilityQuery —— 视线 / 视野判定的唯一几何来源（不依赖 SDL 与渲染器）
//
// 背景：这些判定原本长在 CS2DRenderer 上，而玩法层（BOT 视野、投掷物遮挡、
// 炸弹情报、死斗墙体遮挡）直接调用渲染器。结果是「无渲染器就无法跑仿真」，
// 并且视野半径被视口尺寸隐式影响。
//
// 现在：几何判定只依赖 CS2DWorld；渲染器与玩法层各自持有一份实例，
// 渲染器只负责把参数（半径 / 开镜圆形可见区）同步进来，不再参与判定本身。
// ============================================================================

#include "../Tuning.hpp"
#include "../../Core/PresentationContract.hpp"

#include <algorithm>
#include <cmath>

class CS2DWorld;

class VisibilityQuery {
public:
    // 与 CS2DRenderer 原先使用的常量保持一致（改这里即全项目生效）
    static constexpr float kSoftEdge     = Tuning::VisionSoftEdge;
    static constexpr float kConeDeg      = Tuning::VisionConeDeg;
    static constexpr float kAngleSoftDeg = Tuning::VisionAngleSoftDeg;
    static constexpr float kMaxRadius    = 640.0f;   // 原 MAX_PLAYER_VISION_RADIUS
    static constexpr float kBaseZoomScale = 1.15f;   // 与 UpdateCamera 的镜头放大系数一致

    void SetWorld(const CS2DWorld* world) { world_ = world; }
    const CS2DWorld* World() const { return world_; }

    void  SetVisionRadius(float r) { radius_ = std::clamp(r, 1.0f, kMaxRadius); }
    float VisionRadius() const { return radius_; }
    void SetDistanceLimited(bool limited) { distanceLimited_ = limited; }
    void SetConeAngleDeg(float angle) { coneDeg_ = std::clamp(angle, 1.0f, 360.0f); }

    // 墙体遮挡（阻挡子弹的瓦片即阻挡视线）
    // First blocking tile entry in [0, 1], or 1 when the full segment is clear.
    float FirstBulletWallFraction(float fromX, float fromY, float toX, float toY) const;
    bool HasLineOfSight(float fromX, float fromY, float toX, float toY) const;

    // [0,1]：距离因子 × 扇形因子 × 遮挡，三者取最小；0 = 完全不可见
    float ComputeVisibility(float playerX, float playerY, float aimAngle,
                            float targetX, float targetY) const;

    bool IsVisibleToPlayer(float playerX, float playerY, float aimAngle,
                           float targetX, float targetY) const {
        return ComputeVisibility(playerX, playerY, aimAngle, targetX, targetY) > 0.0f;
    }

    // ---- 视口 → 镜头/视野换算（渲染与无头仿真共用同一公式）----
    // baseZoom：让「整圆刚好装进较窄的那一屏边」并整体放大 kBaseZoomScale
    static float BaseZoomForViewport(int viewportW, int viewportH);
    // 固定世界半径，保持旧 1728×1080 基准；参数仅保留调用兼容
    static float RadiusForViewport(int viewportW, int viewportH, float baseZoom);
    // 便捷：各视口返回同一世界半径
    static float RadiusForViewport(int viewportW, int viewportH) {
        return RadiusForViewport(viewportW, viewportH, BaseZoomForViewport(viewportW, viewportH));
    }

private:
    const CS2DWorld* world_ = nullptr;
    float radius_ = Tuning::VisionRadius;
    bool distanceLimited_ = true;
    float coneDeg_ = kConeDeg;

};
