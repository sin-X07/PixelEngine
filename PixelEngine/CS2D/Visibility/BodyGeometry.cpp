#include "BodyGeometry.hpp"
#include "../../Core/PathUtils.hpp"

#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#include "../../ThirdParty/stb/stb_image.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <cstdint>

namespace BodyGeometry {

const Pose& Get(int sprite, int slot) {
    static const auto poses = [] {
        std::array<std::array<Pose, 3>, 3> result{};
        const char* names[] = {"ct_player.png", "t_player.png", "t_player_alt.png"};
        for (int sheet = 0; sheet < 3; ++sheet) {
            const auto path =
                PathUtils::ResolveProjectFile(std::string("assets/cs2d/sprites/") + names[sheet]);
            std::vector<uint8_t> bytes;
            int w = 0, h = 0, channels = 0;
            unsigned char* pixels = nullptr;
            if (PathUtils::ReadFileBytes(path, bytes) && !bytes.empty()) {
                pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                               &w, &h, &channels, 4);
            }
            for (int pose = 0; pose < 3; ++pose) {
                auto& out = result[sheet][pose];
                const int column = pose == 1 ? 1 : 0, row = pose == 0 ? 0 : 1;
                for (int y = 0; y < Resolution; ++y) {
                    for (int x = 0; x < Resolution; ++x) {
                        unsigned char alpha = 0;
                        if (pixels && w >= 2 && h >= 3) {
                            const int frameW = w / 2, frameH = h / 3;
                            const int px =
                                column * frameW + static_cast<int>((x + .5f) * frameW / Resolution);
                            const int py = row * frameH + static_cast<int>((y + .5f) * frameH / Resolution);
                            alpha = pixels[(py * w + px) * 4 + 3];
                        } else {
                            const float dx = (x + .5f) / Resolution - .5f;
                            const float dy = (y + .5f) / Resolution - .5f;
                            alpha = dx * dx + dy * dy < .16f ? 255 : 0;
                        }
                        out.alpha[y * Resolution + x] = alpha;
                        if (alpha >= 32) {
                            out.samples.push_back(
                                {(x + .5f) / Resolution - .5f, (y + .5f) / Resolution - .5f});
                        }
                    }
                }
            }
            if (pixels) {
                stbi_image_free(pixels);
            }
        }
        return result;
    }();
    return poses[std::clamp(sprite, 0, 2)][slot == 1 || slot == 4 ? 1 : slot == 2 || slot == 3 ? 2 : 0];
}

} // namespace BodyGeometry
