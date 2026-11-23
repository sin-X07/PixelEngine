#pragma once
#include "../../Core/PathUtils.hpp"
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace BodyGeometry {
constexpr int Resolution = 64;
enum class HitRegion { None, Head, Chest, Arm, Abdomen, Leg };

struct RegionLayout {
    // Normalized coordinates in the unrotated sprite frame. Rear edges represent legs.
    float headX, headY, headRadiusX, headRadiusY;
    float abdomenStart, legStart, torsoHalfWidth;
};
constexpr RegionLayout Regions[3][3] = {
    {{.50f, .64f, .14f, .18f, .79f, .91f, .20f},
     {.49f, .58f, .15f, .18f, .76f, .91f, .19f},
     {.48f, .56f, .14f, .18f, .71f, .86f, .19f}},
    {{.49f, .72f, .12f, .15f, .80f, .91f, .18f},
     {.50f, .65f, .12f, .14f, .79f, .91f, .18f},
     {.48f, .57f, .13f, .18f, .72f, .86f, .18f}},
    {{.50f, .70f, .13f, .16f, .80f, .91f, .19f},
     {.51f, .61f, .14f, .17f, .78f, .91f, .19f},
     {.50f, .56f, .14f, .18f, .72f, .86f, .19f}},
};
inline int FrameIndex(int slot) {
    return slot == 1 || slot == 4 ? 1 : slot == 2 || slot == 3 ? 2 : 0;
}
struct Pose {
    std::array<unsigned char, Resolution * Resolution> alpha{};
    std::vector<SDL_FPoint> samples;
};
inline const Pose &Get(int sprite, int slot) {
    static const auto poses = [] {
        std::array<std::array<Pose, 3>, 3> result{};
        const char *names[] = {"ct_player.png", "t_player.png", "t_player_alt.png"};
        for (int sheet = 0; sheet < 3; ++sheet) {
            const auto path =
                PathUtils::ResolveProjectFile(std::string("assets/cs2d/sprites/") + names[sheet]);
            SDL_Surface *loaded = IMG_Load(path.c_str());
            SDL_Surface *image =
                loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
            if (loaded)
                SDL_FreeSurface(loaded);
            for (int pose = 0; pose < 3; ++pose) {
                auto &out = result[sheet][pose];
                const int column = pose == 1 ? 1 : 0, row = pose == 0 ? 0 : 1;
                for (int y = 0; y < Resolution; ++y)
                    for (int x = 0; x < Resolution; ++x) {
                        unsigned char alpha = 0;
                        if (image) {
                            const int frameW = image->w / 2, frameH = image->h / 3;
                            const int px =
                                column * frameW + static_cast<int>((x + .5f) * frameW / Resolution);
                            const int py = row * frameH + static_cast<int>((y + .5f) * frameH / Resolution);
                            alpha =
                                static_cast<unsigned char *>(image->pixels)[py * image->pitch + px * 4 + 3];
                        } else {
                            const float dx = (x + .5f) / Resolution - .5f, dy = (y + .5f) / Resolution - .5f;
                            alpha = dx * dx + dy * dy < .16f ? 255 : 0;
                        }
                        out.alpha[y * Resolution + x] = alpha;
                        if (alpha >= 32)
                            out.samples.push_back(
                                {(x + .5f) / Resolution - .5f, (y + .5f) / Resolution - .5f});
                    }
            }
            if (image)
                SDL_FreeSurface(image);
        }
        return result;
    }();
    return poses[std::clamp(sprite, 0, 2)][FrameIndex(slot)];
}
inline SDL_FPoint Transform(SDL_FPoint local, float x, float y, float size, float aim, int animation,
                            float timer) {
    const float angle = aim + 1.5707963268f;
    // 行走不再有纵向 bobbing：与 DrawCharacter 保持一致，否则人物描边会跟着抖。
    // （角色本体早已去掉该偏移，这里漏改会让 1px 描边按旧的 sin 曲线单独跳动。）
    const float kick = animation == 2 ? -std::max(0.0f, 1 - timer * 6) * 1.6f : 0;
    return {x + kick + size * (local.x * std::cos(angle) - local.y * std::sin(angle)),
            y + size * (local.x * std::sin(angle) + local.y * std::cos(angle))};
}
inline SDL_FPoint ToFrame(float x, float y, float size, float aim, int animation,
                          float timer, float px, float py) {
    const auto center = Transform({0, 0}, x, y, size, aim, animation, timer);
    const float angle = aim + 1.5707963268f, dx = (px - center.x) / size, dy = (py - center.y) / size;
    return {dx * std::cos(angle) + dy * std::sin(angle) + .5f,
            -dx * std::sin(angle) + dy * std::cos(angle) + .5f};
}
inline bool ContainsFramePoint(const Pose& pose, SDL_FPoint point) {
    if (point.x < 0 || point.y < 0 || point.x >= 1 || point.y >= 1) return false;
    const int u = static_cast<int>(point.x * Resolution);
    const int v = static_cast<int>(point.y * Resolution);
    return pose.alpha[v * Resolution + u] >= 32;
}
inline bool Contains(int sprite, int slot, float x, float y, float size, float aim, int animation,
                     float timer, float px, float py) {
    return ContainsFramePoint(Get(sprite, slot), ToFrame(x, y, size, aim, animation, timer, px, py));
}
inline HitRegion RegionAt(int sprite, int slot, SDL_FPoint point) {
    if (!ContainsFramePoint(Get(sprite, slot), point)) return HitRegion::None;
    const auto& region = Regions[std::clamp(sprite, 0, 2)][FrameIndex(slot)];
    const float x = point.x - region.headX;
    const float u = x / region.headRadiusX, v = (point.y - region.headY) / region.headRadiusY;
    if (u * u + v * v <= 1.0f) return HitRegion::Head;
    if (point.y >= region.legStart) return HitRegion::Leg;
    if (point.y >= region.abdomenStart) return HitRegion::Abdomen;
    if (std::abs(x) > region.torsoHalfWidth || point.y < region.headY - region.headRadiusY)
        return HitRegion::Arm;
    return HitRegion::Chest;
}
} // namespace BodyGeometry
