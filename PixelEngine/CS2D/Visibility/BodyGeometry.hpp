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
    return poses[std::clamp(sprite, 0, 2)][slot == 1 || slot == 4 ? 1 : slot == 2 || slot == 3 ? 2 : 0];
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
