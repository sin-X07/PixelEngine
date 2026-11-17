#include "WeaponIconCache.hpp"
#include "../Core/PathUtils.hpp"
#include <SDL_image.h>
#include <iostream>
#include <algorithm>

bool WeaponIconCache::Initialize(SDL_Renderer* renderer) {
    if (renderer_ == renderer && renderer_ != nullptr) return true;
    // 换 renderer 前先把旧纹理释放掉，避免纹理属于已销毁的 renderer
    if (renderer_) Shutdown();
    renderer_ = renderer;
    return renderer_ != nullptr;
}

SDL_Texture* WeaponIconCache::Get(const std::string& relativePath) {
    if (!renderer_ || relativePath.empty()) return nullptr;

    auto it = textures_.find(relativePath);
    if (it != textures_.end()) return it->second;   // 可能缓存的是 nullptr（失败占位）

    const std::string withRoot = (relativePath.rfind("assets/", 0) == 0)
                                     ? relativePath : ("assets/" + relativePath);
    const std::string path = PathUtils::ResolveAppPath(withRoot);
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path.c_str());
    if (!tex) {
        // 失败也写入缓存：只报一次，之后不再每帧重试
        std::cerr << "[WeaponIconCache] 图标加载失败: " << withRoot
                  << " (" << IMG_GetError() << ")" << std::endl;
        textures_.emplace(relativePath, nullptr);
        return nullptr;
    }
    textures_.emplace(relativePath, tex);
    return tex;
}

SDL_Texture *WeaponIconCache::GetHud(const std::string &relativePath) {
    if (!renderer_ || relativePath.empty())
        return nullptr;
    const std::string key = "hud/" + relativePath;
    const auto cached = textures_.find(key);
    if (cached != textures_.end())
        return cached->second;
    const std::string rooted =
        relativePath.rfind("assets/", 0) == 0 ? relativePath : "assets/" + relativePath;
    SDL_Surface *loaded = IMG_Load(PathUtils::ResolveAppPath(rooted).c_str());
    SDL_Surface *image = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
    if (loaded)
        SDL_FreeSurface(loaded);
    SDL_Texture *texture = nullptr;
    if (image) {
        int left = image->w, top = image->h, right = -1, bottom = -1;
        for (int y = 0; y < image->h; ++y)
            for (int x = 0; x < image->w; ++x) {
                auto *pixel = static_cast<Uint8 *>(image->pixels) + y * image->pitch + x * 4;
                if (!pixel[3])
                    continue;
                const float luminance = pixel[0] * .2126f + pixel[1] * .7152f + pixel[2] * .0722f;
                pixel[0] = static_cast<Uint8>(145 + luminance * .38f);
                pixel[1] = static_cast<Uint8>(157 + luminance * .36f);
                pixel[2] = static_cast<Uint8>(168 + luminance * .34f);
                if (pixel[3] >= 8) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
            }
        if (right >= left && bottom >= top) {
            SDL_Rect source{left, top, right - left + 1, bottom - top + 1};
            SDL_Surface *cropped =
                SDL_CreateRGBSurfaceWithFormat(0, source.w, source.h, 32, SDL_PIXELFORMAT_RGBA32);
            if (cropped) {
                SDL_SetSurfaceBlendMode(image, SDL_BLENDMODE_NONE);
                SDL_BlitSurface(image, &source, cropped, nullptr);
                texture = SDL_CreateTextureFromSurface(renderer_, cropped);
                SDL_FreeSurface(cropped);
            }
        }
        SDL_FreeSurface(image);
    }
    if (texture)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    textures_.emplace(key, texture);
    return texture;
}

void WeaponIconCache::Shutdown() {
    for (auto& kv : textures_) {
        if (kv.second) SDL_DestroyTexture(kv.second);
    }
    textures_.clear();
    renderer_ = nullptr;
}
