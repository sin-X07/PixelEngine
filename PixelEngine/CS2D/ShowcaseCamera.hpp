#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ShowcaseCamera {
struct View {
    SDL_FPoint center{};
    float zoom = 1;
};
inline View Sample(float seconds, int shot, float mapW, float mapH, float viewW, float viewH,
                   const std::vector<SDL_FPoint> &points) {
    View result;
    result.zoom = std::max(viewW / std::max(1.f, mapW), viewH / std::max(1.f, mapH)) * 1.40f;
    const float halfW = viewW / (2 * result.zoom), halfH = viewH / (2 * result.zoom);
    const float rangeX = std::max(0.f, mapW - 2 * halfW), rangeY = std::max(0.f, mapH - 2 * halfH);
    auto legal = [&](SDL_FPoint point) {
        return SDL_FPoint{std::clamp(point.x, halfW, mapW - halfW), std::clamp(point.y, halfH, mapH - halfH)};
    };
    const float angle = shot * 2.39996323f;
    const SDL_FPoint anchor =
        points.empty() ? SDL_FPoint{mapW * .5f, mapH * .5f} : points[shot % points.size()];
    const auto middle = legal(anchor);
    const float dx = std::cos(angle) * rangeX * .22f, dy = std::sin(angle) * rangeY * .22f;
    const SDL_FPoint control[] = {legal({middle.x - dx, middle.y - dy}),
                                  legal({middle.x - dx * .3f + dy * .2f, middle.y - dy * .3f - dx * .2f}),
                                  legal({middle.x + dx * .3f + dy * .2f, middle.y + dy * .3f - dx * .2f}),
                                  legal({middle.x + dx, middle.y + dy})};
    const float t = std::clamp(seconds / 8.f, 0.f, 1.f), u = 1 - t;
    result.center = {u * u * u * control[0].x + 3 * u * u * t * control[1].x + 3 * u * t * t * control[2].x +
                         t * t * t * control[3].x,
                     u * u * u * control[0].y + 3 * u * u * t * control[1].y + 3 * u * t * t * control[2].y +
                         t * t * t * control[3].y};
    return result;
}
} // namespace ShowcaseCamera
