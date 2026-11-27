#pragma once
#include "../../Core/Vec2.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace BodyGeometry {
constexpr int Resolution = 64;
struct Pose {
    std::array<unsigned char, Resolution * Resolution> alpha{};
    std::vector<Vec2> samples;
};

const Pose& Get(int sprite, int slot);

inline Vec2 Transform(Vec2 local, float x, float y, float size, float aim, int animation,
                      float timer) {
    const float angle = aim + 1.5707963268f;
    // 行走不再有纵向 bobbing：与 DrawCharacter 保持一致，否则人物描边会跟着抖。
    // （角色本体早已去掉该偏移，这里漏改会让 1px 描边按旧的 sin 曲线单独跳动。）
    const float kick = animation == 2 ? -std::max(0.0f, 1 - timer * 6) * 1.6f : 0;
    return {x + kick + size * (local.x * std::cos(angle) - local.y * std::sin(angle)),
            y + size * (local.x * std::sin(angle) + local.y * std::cos(angle))};
}

inline bool Contains(int sprite, int slot, float x, float y, float size, float aim, int animation,
                     float timer, float px, float py) {
    const auto center = Transform({0, 0}, x, y, size, aim, animation, timer);
    const float angle = aim + 1.5707963268f, dx = (px - center.x) / size, dy = (py - center.y) / size;
    const int u = static_cast<int>((dx * std::cos(angle) + dy * std::sin(angle) + .5f) * Resolution);
    const int v = static_cast<int>((-dx * std::sin(angle) + dy * std::cos(angle) + .5f) * Resolution);
    return u >= 0 && v >= 0 && u < Resolution && v < Resolution &&
           Get(sprite, slot).alpha[v * Resolution + u] >= 32;
}
} // namespace BodyGeometry
