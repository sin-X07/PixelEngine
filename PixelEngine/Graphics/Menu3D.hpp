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

    // ---- 主菜单 3D 交互 ----
    // 索引：0=T, 1=CT, 2/3/4=三只小鸡
    static constexpr int kObjectCount = 5;
    int HitTestNormalized(float u, float v) const;   // u/v 为 3D 纹理坐标，左上原点
    void SetHoveredObject(int index) { hoveredObject_ = index; }
    int  HoveredObject() const { return hoveredObject_; }
    void SetFocusedObject(int index);
    int  FocusedObject() const { return focusedObject_; }
    void SetObjectColor(int index, int option);
    void SetObjectPose(int index, int option);
    void SetFocusWeapon(int option) { focusWeapon_ = option; }

    // ---- 自定义（Phase 1/2）----
    // 每项按对象索引：0=T, 1=CT, 2..4=三只小鸡
    struct Customization {
        int color[5]     = { 0, 0, 0, 0, 0 };   // 角色调色板 / 小鸡羽色
        int pose[5]      = { 0, 0, 0, 0, 0 };   // 动画选项
        int weapon[2]    = { 0, 0 };            // T/CT 手持武器（节点）
        int accessory[5] = { 0, 0, 0, 0, 0 };   // 0=无
    };
    void SetCustomization(const Customization& c);
    const Customization& GetCustomization() const { return custom_; }

    // 轮盘数据：按对象动态生成类别与选项
    int         FocusCategoryCount(int objectIndex) const;
    const char* FocusCategoryName(int objectIndex, int cat) const;
    int         FocusOptionCount(int objectIndex, int cat) const;
    const char* FocusOptionName(int objectIndex, int cat, int opt) const;
    int         FocusSelected(int objectIndex, int cat) const;
    void        FocusSelect(int objectIndex, int cat, int opt);
    void SetFocusProgress(float progress) { focusProgress_ = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress); }
    float FocusProgress() const { return focusProgress_; }
    void SetCameraTopDownProgress(float progress) {
        cameraTopDown_ = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
    }
    void SetSpotlightSuppressed(bool suppressed) { spotlightSuppressed_ = suppressed; }
    void SetSpotlightIntensity(float intensity) {
        spotlightIntensity_ = intensity < 0.0f ? 0.0f : (intensity > 1.0f ? 1.0f : intensity);
    }
    void SetSpotlightSourceProgress(float progress) {
        spotSourceZ_ = (progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress)) * 1.35f;
    }
    // 选图浮层：用真实 3D 聚光灯打在俯视地面上，u/v 为 3D 纹理坐标。
    void SetOverlayMouse(float u, float v, bool active);

private:
    bool InitGL();
    void DrawScene(float timeSec);
    void DrawFocusScene();
    void DrawWeaponProp(float x, float y, float z);
    void DrawSpotlight();
    void DrawObjectRing(float x, float z, float radius, float alpha);
    void UpdateSpotlight(float dt);
    void UpdateObjectScreenPositions();
    void ObjectWorldPosition(int index, float& x, float& y, float& z) const;
    void FocusCurrentPosition(float& x, float& z) const;
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
    std::vector<std::unique_ptr<Menu3DModel>> focusWeapons_;   // AK / 手枪 / 匕首
    // 自定义配件：0=鸭舌帽 1=墨镜 2=背包 3=小鸡帽
    std::unique_ptr<Menu3DModel> accessories_[4];
    Customization custom_{};

    void ApplyAllCustomization();
    void ApplyCustomizationColor(int obj);
    void ApplyCustomizationPose(int obj);
    void ApplyCustomizationWeapon(int obj);
    void DrawAccessory(int obj, float scale, float yOffset, float yaw) const;
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
    // ---- 小鸡步态校准（数值来源：assets/models/chickensoft/manifest.md）----
    // 模型 y 向上，1 单位 = 1 米，实测 AABB 高度 1.843；基础动画 walk 循环 0.8s。
    static constexpr float kChickenModelHeight = 1.843f;
    static constexpr float kWalkClipSeconds    = 0.8f;
    static constexpr float kRunClipSeconds     = 0.5333f;
    // 一次 walk 循环的期望位移（按身高折算），用于避免滑步
    static constexpr float kChickenStrideBodyHeights = 1.0f;
    // 基准世界速度：dist=6 处的移动速度（世界单位/秒），透视按 dist 缩放
    static constexpr float kChickenBaseSpeed   = 1.05f;
    // Draw 用的世界缩放（距离补偿，保证屏幕上三只鸡大小接近）
    float ChickenWorldScale(const ChickenState& s) const;
    // 动画时间倍率：让「每秒位移 / 每循环步幅」与 walk 循环时长对齐，消除滑步
    float ChickenAnimTimeScale(const ChickenState& s) const;
    void InitChickenState();
    void UpdateChickens(float dt);
    void PickChickenTarget(int index);
    bool LoadModels();
    void DrawModels();

    int hoveredObject_ = -1;
    int focusedObject_ = -1;
    int focusWeapon_ = 0;
    float focusProgress_ = 1.0f;
    float focusTargetX_ = -2.4f;
    float focusTargetZ_ = 0.0f;
    float cameraTopDown_ = 0.0f;
    bool spotlightSuppressed_ = false;
    float spotlightIntensity_ = 1.0f;
    float spotSourceZ_ = 0.0f;
    bool overlaySpotlightActive_ = false;
    float overlayTargetX_ = 0.0f;
    float overlayTargetZ_ = 0.0f;
    float spotX_ = 0.0f;
    float spotZ_ = 0.0f;
    float spotRadius_ = 0.72f;
    float spotY_ = 0.05f;
    float spotTargetX_ = 0.0f;
    float spotTargetZ_ = 0.0f;
    float spotScanTimer_ = 0.0f;
    std::array<float, kObjectCount> screenU_{};
    std::array<float, kObjectCount> screenV_{};
    std::array<float, kObjectCount> screenRU_{};
    std::array<float, kObjectCount> screenRV_{};
};
