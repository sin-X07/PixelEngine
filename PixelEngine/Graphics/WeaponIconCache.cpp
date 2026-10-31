#include "WeaponIconCache.hpp"
#include "../Core/PathUtils.hpp"
#include <SDL_image.h>
#include <iostream>

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

    const std::string path = PathUtils::ResolveAppPath(relativePath);
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path.c_str());
    if (!tex) {
        // 失败也写入缓存：只报一次，之后不再每帧重试
        std::cerr << "[WeaponIconCache] 图标加载失败: " << relativePath
                  << " (" << IMG_GetError() << ")" << std::endl;
        textures_.emplace(relativePath, nullptr);
        return nullptr;
    }
    textures_.emplace(relativePath, tex);
    return tex;
}

void WeaponIconCache::Shutdown() {
    for (auto& kv : textures_) {
        if (kv.second) SDL_DestroyTexture(kv.second);
    }
    textures_.clear();
    renderer_ = nullptr;
}
