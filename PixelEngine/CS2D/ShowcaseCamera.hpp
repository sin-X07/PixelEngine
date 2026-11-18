#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ShowcaseCamera {
// 单个机位时长（秒）。Engine 的淡入淡出也用它，避免两边各写一份魔法数字。
inline constexpr float kShotSeconds = 6.8f;

struct View {
    SDL_FPoint center{};
    float zoom = 1;
};

// 结算页机位采样。
//
// 关键：相邻 shot 必须落在**地图的不同区域**。旧实现把中心锁在少数几个
// 出生点/区域中心上，只用 rangeX*0.22 的偏移在附近晃，于是每次切换几乎原地不动。
// 现在改成「黄金比低差异序列」在合法中心矩形里铺点：
//   相邻 shot 的归一化坐标相差 ~0.618 / ~0.382，落到屏幕上就是地图的两端。
// 每个 shot 再叠一点极小的往返漂移，保证镜头不是死的。
inline View Sample(float seconds, int shot, float mapW, float mapH, float viewW, float viewH,
                   const std::vector<SDL_FPoint> &points) {
    View result;
    const float safeMapW = std::max(1.0f, mapW);
    const float safeMapH = std::max(1.0f, mapH);

    // 每个 shot 换一档缩放（都 >= 铺满屏幕的 fit），远近交替，避免全是同一个比例。
    // 注意：zoom 越大，合法中心矩形越大（镜头越小、能挪的范围越宽）。
    // 所以档位不能取得太近——1.22 那档的可挪范围只有地图的 ~18%，
    // 相邻机位就必然挤在一起。这里最低取 1.55。
    const float fit = std::max(viewW / safeMapW, viewH / safeMapH);
    const float zoomSteps[] = {1.55f, 2.15f, 1.78f, 2.60f};
    result.zoom = fit * zoomSteps[static_cast<int>(((shot % 4) + 4) % 4)];

    const float halfW = viewW / (2.0f * result.zoom);
    const float halfH = viewH / (2.0f * result.zoom);
    // 合法中心矩形：镜头不许露出地图之外（太小的地图退化为居中）。
    const float loX = std::min(halfW, safeMapW * 0.5f);
    const float hiX = std::max(loX, safeMapW - halfW);
    const float loY = std::min(halfH, safeMapH * 0.5f);
    const float hiY = std::max(loY, safeMapH - halfH);
    const float spanX = hiX - loX, spanY = hiY - loY;

    auto frac = [](float v) { return v - std::floor(v); };
    const float index = static_cast<float>(shot) + 1.0f;
    // 两个不同无理数的低差异序列：相邻 shot 在 x/y 上都不重合，落点铺满整张图。
    float ux = frac(index * 0.61803398875f);
    float uy = frac(index * 0.75487766625f);

    // 轻微向「有内容的点」拉 12%：仍然保持大跨度，只是偶尔把据点框进画面。
    if (!points.empty()) {
        const SDL_FPoint anchor = points[static_cast<size_t>(shot < 0 ? 0 : shot) % points.size()];
        const float ax = std::clamp((anchor.x - loX) / std::max(1.0f, spanX), 0.0f, 1.0f);
        const float ay = std::clamp((anchor.y - loY) / std::max(1.0f, spanY), 0.0f, 1.0f);
        ux = ux * 0.88f + ax * 0.12f;
        uy = uy * 0.88f + ay * 0.12f;
    }

    // 镜头内的往复漂移（0 → 峰值 → 0），让静态截图里也看得出在动。
    const float t = std::clamp(seconds / kShotSeconds, 0.0f, 1.0f);
    const float cycle = t * 6.2831853f;
    const float sway = (shot % 2 == 0) ? 1.0f : -1.0f;
    const float driftX = std::sin(cycle) * spanX * 0.10f * sway;
    const float driftY = std::sin(cycle * 2.0f) * spanY * 0.10f;

    result.center = {
        std::clamp(loX + spanX * ux + driftX, loX, hiX),
        std::clamp(loY + spanY * uy + driftY, loY, hiY)};
    return result;
}
} // namespace ShowcaseCamera
