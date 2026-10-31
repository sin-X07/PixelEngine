#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>

// ============================================================================
// WeaponIconCache — 武器图标纹理缓存（契约 v1 第 3 节）
//
// 归属约定：
//   - 不 owning SDL_Renderer：Initialize 只借用，换 renderer 前必须先 Shutdown；
//   - 缓存 owning 纹理：Get 返回的是借用指针，调用者不得 Destroy；
//   - 加载失败也缓存空值，避免每帧重复 IMG_LoadTexture + 刷日志；
//   - Shutdown 必须早于 SDL_DestroyRenderer（由 Engine::Clean 保证）。
//
// 类名与三个函数签名由契约固定；表现细节（缩小/比例/占位图形）归 UI 方。
// ============================================================================
class WeaponIconCache {
public:
    WeaponIconCache() = default;
    ~WeaponIconCache() { Shutdown(); }

    // 非 owning：只记录 renderer 指针。失败（renderer 为空）返回 false。
    bool Initialize(SDL_Renderer* renderer);

    // 项目相对路径（WeaponDB.iconPath）。命中缓存直接返回（可能为 nullptr）。
    SDL_Texture* Get(const std::string& relativePath);

    // Destroy 全部缓存纹理并把状态复位为未初始化。
    void Shutdown();

    bool IsInitialized() const { return renderer_ != nullptr; }

private:
    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<std::string, SDL_Texture*> textures_;
};
