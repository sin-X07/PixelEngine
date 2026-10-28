#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Menu3DModel.hpp"

// ============================================================================
// Menu3D — 主菜单实时 3D 背景
//
// 当前阶段（P0）：
//   - 独立的隐藏 SDL/OpenGL 兼容上下文，不干扰主窗口的 SDL_Renderer；
//   - 固定管线绘制一个低模占位场景（左右战术人形 + 三只小鸡）；
//   - glReadPixels 回读 RGBA，更新成 SDL_Texture，交给 GameUI 当作主菜单背景。
//
// 后续阶段会把占位人形/小鸡替换为 glTF + CPU 蒙皮动画。
// ============================================================================
class Menu3D {
public:
    Menu3D() = default;
    ~Menu3D() { Shutdown(); }

    bool Initialize(SDL_Renderer* mainRenderer, int width, int height);
    void Shutdown();

    // 每帧调用；内部限制到约 30fps 更新 3D 纹理。
    void Render(float dt);

    bool IsReady() const { return ready_; }
    SDL_Texture* GetTexture() const { return texture_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::string& LastError() const { return lastError_; }

private:
    bool InitGL();
    void DrawScene(float timeSec);
    void ReadbackToTexture();

    SDL_Window*   glWindow_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    SDL_Renderer* mainRenderer_ = nullptr;
    SDL_Texture*  texture_ = nullptr;

    int width_ = 1024;
    int height_ = 576;
    bool ready_ = false;

    std::vector<uint8_t> pixels_;   // 原始 glReadPixels 数据（左下原点）
    std::vector<uint8_t> flipped_;  // 上下翻转后的 SDL 上传数据
    uint64_t lastRenderTicks_ = 0;
    float animationTime_ = 0.0f;
    std::string lastError_;

    std::unique_ptr<Menu3DModel> tModel_;
    std::unique_ptr<Menu3DModel> ctModel_;
    std::vector<std::unique_ptr<Menu3DModel>> chickens_;
    struct ChickenState {
        float x = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float targetYaw = 0.0f;
        float targetX = 0.0f;
        float targetZ = 0.0f;
        float repathTimer = 0.0f;
        float speed = 0.8f;
    };
    std::array<ChickenState, 3> chickenState_{};
    std::mt19937 chickenRng_{ std::random_device{}() };
    void InitChickenState();
    void UpdateChickens(float dt);
    void PickChickenTarget(int index);
    bool LoadModels();
    void DrawModels();
};
