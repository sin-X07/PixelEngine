#pragma once
// ============================================================================
// NadePhysics —— 投掷物撞墙反弹的纯数学部分（可单测）
//
// 与地图无关，只负责「按碰撞轴反弹 + 动能损失」：
//   * 只反转被墙挡住的速度分量，切向速度保留（撞侧面时沿墙滑行）
//   * 每次反弹速度乘恢复系数，动能按 v² 衰减
//   * 反弹后速度低于阈值就直接停止，避免在墙角高频抖动
//
// 用法：先用地图分轴判定得到 hitX / hitY，再调用 ApplyNadeBounce()。
// ============================================================================

inline bool ApplyNadeBounce(float& vx, float& vy, bool hitX, bool hitY,
                            float restitution, float minSpeed) {
    if (!hitX && !hitY) return false;

    if (hitX) vx = -vx * restitution;
    if (hitY) vy = -vy * restitution;

    if (vx * vx + vy * vy < minSpeed * minSpeed) {
        vx = 0.0f;
        vy = 0.0f;
    }
    return true;
}
