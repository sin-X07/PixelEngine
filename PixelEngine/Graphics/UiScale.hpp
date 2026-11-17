#pragma once
#include <algorithm>
// ============================================================================
// UiScale — 全局 UI 缩放
//
// 设计基准 1920x1080：所有 UI 尺寸都按这个基准写死，
// 运行时统一乘 g_uiScale 得到实际像素。
// ============================================================================

inline float g_uiScale = 1.0f;

inline constexpr float kDesignW = 1920.0f;
inline constexpr float kDesignH = 1080.0f;

// 把设计尺寸换算成当前屏幕像素
#define UI(x) (static_cast<float>(x) * g_uiScale)

// Keep the Tab map inside the space left of the centered 760-unit scoreboard.
inline float ScoreboardMapSize(float width, float height) {
    const float scale = height / kDesignH;
    return std::min(340.0f * scale, std::max(96.0f * scale, (width - 760.0f * scale) * .5f - 24.0f * scale));
}
