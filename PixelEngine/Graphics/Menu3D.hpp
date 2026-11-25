#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
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
// 独立 OpenGL 上下文绘制主题环境、蒙皮角色与小鸡，回读 SDL 纹理供主菜单显示。
// 场景选择独立于角色定制，环境道具位于角色与小鸡的完整活动范围之外。
// ============================================================================
class Menu3D {
public:
    Menu3D() = default;
    ~Menu3D() { Shutdown(); }

    bool Initialize(SDL_Renderer* mainRenderer, int width = 0, int height = 0);
    void Shutdown();

    // 每帧调用；内部限制到约 30fps 更新 3D 纹理。
    void Render(float dt);

    bool IsReady() const { return ready_; }
    SDL_Texture* GetTexture() const { return texture_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::string& LastError() const { return lastError_; }
    void SetScene(int index, bool immediate = false);
    int SceneIndex() const { return sceneIndex_; }
    SDL_Texture* ScenePreview(int index) const { return scenePreviews_[std::clamp(index, 0, 2)]; }
    void SetPresentationAspect(float aspect) { presentationAspect_ = std::clamp(aspect, 1.0f, 4.0f); }

    // ---- 主菜单 3D 交互 ----
    // 索引：0/1 号槽位原为 T/CT，已随角色移除（2026-11-25）不再可选；2/3/4=三只小鸡
    static constexpr int kObjectCount = 5;
    int HitTestNormalized(float u, float v) const;   // u/v 为 3D 纹理坐标，左上原点
    void SetHoveredObject(int index) { hoveredObject_ = (index >= 0 && index < 2) ? -1 : index; }
    int  HoveredObject() const { return hoveredObject_; }
    void SetFocusedObject(int index);
    int  FocusedObject() const { return focusedObject_; }
    void SetObjectColor(int index, int option);
    void SetObjectPose(int index, int option);
    void SetFocusWeapon(int option) { focusWeapon_ = option; }

    // ---- 自定义（Phase 1/2）----
    // 每项按对象索引：0/1 号槽位原为 T/CT（已移除，保留字段以免存档结构变化），2..4=三只小鸡
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
    friend struct Menu3DTestAccess;
    bool InitGL();
    SDL_Texture* CreateBackdropTexture(int width, int height) const;
    void FollowOutputSize();
    void InitSurfaceShader();
    void InitShadowMap(unsigned int& framebuffer, unsigned int& depth, int size);
    void ResizeSpotlightTargets();
    void InitSandstormAssets();
    void InitAtmosphereShadersAndPost();
    void ResizeBloomTargets();
    void ReleaseAtmosphereResources();
    void UpdateLightning(float dt);
    void ComputeSceneWeights(float& ruinsW, float& desertW, float& mirageW, float& fade) const;
    void DrawAtmosphereSky(float ruinsW, float desertW, float mirageW, float fade);
    void DrawLightningBolt(float flash);
    void DrawAtmosphereParticles(bool foreground, float ruinsW, float desertW, float mirageW);
    void ApplyAtmospherePost(float ruinsW, float desertW, float mirageW);
    void DrawEnvironmentShadow(const Menu3DModel& model, const float sun[4]);
    void DrawEnvironment();
    void DrawScene(float timeSec);
    void DrawFocusScene();
    void DrawWeaponProp(float x, float y, float z);
    void PrepareSpotlight();
    void DrawSpotlightLens();
    void DrawSpotlightVolume();
    void DrawLegacySpotlight();
    void DrawChickens(bool depthOnly);
    struct SpotUniforms { int position = -1, direction = -1, matrix = -1; };
    void SetSpotlightUniforms(const SpotUniforms& uniforms) const;
    void DrawObjectRing(float x, float z, float radius, float alpha);
    void UpdateSpotlight(float dt);
    void UpdateObjectScreenPositions();
    void ObjectWorldPosition(int index, float& x, float& y, float& z) const;
    void FocusCurrentPosition(float& x, float& z) const;
    void ReadbackToTexture(SDL_Texture* target);
    void CaptureScenePreviews();

    SDL_Window*   glWindow_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    SDL_Renderer* mainRenderer_ = nullptr;
    SDL_Texture*  texture_ = nullptr;

    int width_ = 1024;
    int height_ = 576;
    bool ready_ = false;
    bool followOutputSize_ = false;
    unsigned int surfaceProgram_ = 0;
    unsigned int shadowFramebuffer_ = 0;
    unsigned int shadowDepth_ = 0;
    std::array<float,16> shadowMatrix_{};
    Menu3DModel::SurfaceUniforms surfaceUniforms_{};
    int surfaceShadowMatrix_ = -1;
    int surfaceHasShadow_ = -1;
    int surfaceSceneWeights_ = -1;
    int surfaceEnvParams_ = -1;
    int surfaceCameraPos_ = -1;
    unsigned int skyProgram_ = 0;
    int skyCamRight_ = -1;
    int skyCamUp_ = -1;
    int skyCamForward_ = -1;
    int skyParams_ = -1;
    int skySceneWeights_ = -1;
    int skyExtraParams_ = -1;
    unsigned int sandWispTex_ = 0;
    unsigned int sandStreakTex_ = 0;
    unsigned int bloomFramebuffer_ = 0;
    unsigned int sceneCopyTex_ = 0;
    std::array<unsigned int, 2> bloomTex_{};
    int bloomW_ = 0;
    int bloomH_ = 0;
    int sceneCopyW_ = 0;
    int sceneCopyH_ = 0;
    unsigned int bloomExtractProgram_ = 0;
    int bloomExtractTex_ = -1;
    int bloomExtractThreshold_ = -1;
    int bloomExtractTint_ = -1;
    unsigned int bloomBlurProgram_ = 0;
    int bloomBlurTex_ = -1;
    int bloomBlurOffset_ = -1;
    unsigned int postHeatProgram_ = 0;
    int postHeatSceneTex_ = -1;
    int postHeatBloomTex_ = -1;
    int postHeatParams_ = -1;
    int postHeatBloomTint_ = -1;
    float lightningTimer_ = 2.8f;
    float lightningAge_ = -1.0f;
    float lightningFlash_ = 0.0f;
    float lightningX_ = -1.5f;
    unsigned int lightningSeed_ = 1337u;
    std::array<float, 3> cameraPos_{ 0.0f, 1.05f, 8.0f };
    std::array<float, 3> cameraTarget_{ 0.0f, 0.95f, 0.0f };
    float cameraUpY_ = 1.0f;
    float cameraUpZ_ = 0.0f;
    bool previewCapture_ = false;  // 抓拍场景预览时：DrawScene 只画环境本体

    std::vector<uint8_t> pixels_;   // 原始 glReadPixels 数据（左下原点）
    std::vector<uint8_t> flipped_;  // 上下翻转后的 SDL 上传数据
    uint64_t lastRenderTicks_ = 0;
    float animationTime_ = 0.0f;
    float pendingAnimationTime_ = 0.0f;
    std::string lastError_;

    std::array<std::unique_ptr<Menu3DModel>, 3> environments_;
    std::array<SDL_Texture*, 3> scenePreviews_{};
    int sceneIndex_ = 1;
    int displayedScene_ = 1;
    float sceneTransition_ = .5f;
    float presentationAspect_ = 16.0f / 9.0f;
    std::vector<std::unique_ptr<Menu3DModel>> chickens_;
    std::vector<std::unique_ptr<Menu3DModel>> focusWeapons_;   // AK / 手枪 / 匕首
    // 自定义配件：0=鸭舌帽 1=墨镜 2=背包 3=小鸡帽
    Customization custom_{};

    void ApplyAllCustomization();
    void ApplyCustomizationColor(int obj);
    void ApplyCustomizationPose(int obj);
    void ApplyCustomizationAccessory(int obj);
    struct ChickenState {
        float x = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float targetYaw = 0.0f;
        float targetX = 0.0f;
        float targetZ = 0.0f;
        float repathTimer = 0.0f;
        float speed = 0.8f;
        float movementSpeed = 0.0f;
    };
    std::array<ChickenState, 3> chickenState_{};
    std::mt19937 chickenRng_{ std::random_device{}() };
    // Menu_Walk keeps one original stride, baked into a four-second clip.
    static constexpr float kChickenModelHeight = 1.843f;
    static constexpr float kWalkClipSeconds    = 4.0f;
    // 一次 walk 循环的期望位移（按身高折算），用于避免滑步
    // The exported ankle sweep is 0.285 m each way per cycle at a height of 1.843 m.
    static constexpr float kChickenStrideBodyHeights = 0.31f;
    // 基准世界速度：dist=6 处的移动速度（世界单位/秒），透视按 dist 缩放
    static constexpr float kChickenBaseSpeed   = 0.30f;
    // Draw 用的世界缩放（距离补偿，保证屏幕上三只鸡大小接近）
    float ChickenWorldScale(const ChickenState& s) const;
    // 动画时间倍率：让「每秒位移 / 每循环步幅」与 walk 循环时长对齐，消除滑步
    float ChickenAnimTimeScale(const ChickenState& s) const;
    struct GroundObstacle { float x, z, radius; int chickenIndex = -1; };
    float ChickenRadius(int index, float z) const;
    std::vector<GroundObstacle> ChickenObstacles(int index) const;
    bool ChickenPathClear(int index, float fromX, float fromZ, float toX, float toZ) const;
    void SeparateChickens();
    void InitChickenState();
    void UpdateChickens(float dt);
    void PickChickenTarget(int index);
    bool LoadModels();
    float ActorStageX(int actor) const;

    int hoveredObject_ = -1;
    int focusedObject_ = -1;
    int focusWeapon_ = 0;
    float focusProgress_ = 1.0f;
    float focusTargetX_ = -2.4f;
    float focusTargetZ_ = 0.0f;
    float cameraTopDown_ = 0.0f;
    struct Spotlight {
        std::array<float, 3> position{}, direction{}, right{}, up{};
        float slope = 0.1f;
        float power = 0;
    } light_;
    unsigned int chickenProgram_ = 0;
    Menu3DModel::SurfaceUniforms chickenUniforms_{};
    SpotUniforms surfaceSpot_, chickenSpot_, volumeSpot_;
    int volumeCamera_ = -1, volumeMedium_ = -1;
    int chickenTransform_ = -1, chickenCamera_ = -1;
    unsigned int spotFramebuffer_ = 0, spotDepth_ = 0;
    unsigned int volumeProgram_ = 0, volumeFramebuffer_ = 0;
    unsigned int sceneDepthCopy_ = 0, volumeTexture_ = 0;
    std::array<float, 16> spotMatrix_{};
    int volumeWidth_ = 0, volumeHeight_ = 0;
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
