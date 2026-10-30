#pragma once
#include "Tuning.hpp"
#include "CS2DWorld.hpp"
#include "Weapons/WeaponDef.hpp"
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <vector>
#include <string>

// ============================================================================
// CS2DRenderer —— CS2D 独立渲染器
//   背景图 + tileset tile 层 + 精灵图角色 + 武器 + 视野遮罩
// ============================================================================

// 精灵绘制参数（由 CS2DGame 填充，避免循环 include）
struct SpriteDrawParams {
    float x       = 0.0f;
    float y       = 0.0f;
    float size    = 6.0f;       // 仅作命中盒参考，视觉尺寸由 SPRITE_VISUAL_SIZE 决定
    float aimAngle = 0.0f;
    int   spriteKey  = 0;   // 0=CT头盔, 1=T无头盔, 2=T备选
    int   weaponId   = 0;   // WeaponId（决定手持武器贴图）
    int   weaponSlot = 0;   // 0=主武器, 1=副武器, 2=近战, 3=投掷物（决定姿态帧）
    int   animState  = 0;   // 0=Idle, 1=Walk, 2=Shoot, 3=Dead
    float animTimer  = 0.0f;
    float deathTimer = 0.0f;
    Uint8 alpha      = 255;
};

// 序列帧精灵图元数据：3 行 × 2 列，共 6 帧移动姿态
struct SpriteSheetDef {
    SDL_Texture* tex  = nullptr;
    int frameW   = 32;
    int frameH   = 32;
    int cols     = 2;
    int rows     = 3;
};

class CS2DRenderer {
public:
    CS2DRenderer() = default;
    ~CS2DRenderer() { Clean(); }

    bool Initialize(SDL_Renderer* ren, const std::string& mapDir, int viewW = 800, int viewH = 600);
    void Clean();

    void SetWorld(const CS2DWorld* world);

    void UpdateCamera(float playerCenterX, float playerCenterY,
                      int viewportW, int viewportH);

    // 背景 + tile 层
    void DrawWorld();
    void DrawOverlay();

    // 角色精灵绘制（统一玩家/BOT）
    void DrawCharacter(const SpriteDrawParams& p);

    // 子弹 / 准星
    void DrawBullet(float x, float y, float vx, float vy);
    // 飞行中的投掷物：按类型画对应的旋转图标（替代原来的 bullet 占位）
    void DrawThrownGrenade(int weaponId, float worldX, float worldY, float spinDeg);
    // 掉在地上的武器：用 _d 图标（与 HUD 同一套素材），带轻微上下浮动
    // angleDeg：平铺朝向（不浮动）
    void DrawDroppedWeapon(int weaponId, float worldX, float worldY, float angleDeg);
    // 世界坐标圆（用于手雷爆炸范围提示 / 爆炸特效）
    void DrawCircleOutline(float worldX, float worldY, float radius,
                           Uint8 r, Uint8 g, Uint8 b, Uint8 a, int segments = 48);
    void DrawCircleFilled(float worldX, float worldY, float radius,
                          Uint8 r, Uint8 g, Uint8 b, Uint8 a, int segments = 40);
    // 目标物标记：包点区域、已安装的 C4（timeSec 用于呼吸动画）
    void DrawObjectiveMarkers(float timeSec);
    void DrawCrosshair(float cx, float cy,
                       Uint8 r = 255, Uint8 g = 255, Uint8 b = 255, Uint8 a = 200,
                       float thickness = 1.5f, bool centerDot = true,
                       float length = 12.0f, float gap = 3.0f);

    // 雷达（左上角 HUD + 中心全屏覆盖 — Tab）
    // 雷达上的一个标记（世界坐标 + 颜色）
    struct RadarDot {
        float x = 0.0f, y = 0.0f;
        Uint8 r = 255, g = 255, b = 255;
    };

    void DrawRadarHUD(float playerX, float playerY, float playerAngle,
                      const std::vector<RadarDot>& dots,
                      float mapW, float mapH,
                      bool playerCentered, bool rotateWithPlayer,
                      bool blendBg, int bgAlpha,
                      int hudSize, float mapZoom,
                      bool showCenterRadar,
                      int viewW, int viewH);

    // 计分板（屏幕顶端正中）
    void DrawScoreboard(int total, int kills, int deaths, int viewW = 800, int viewH = 600);

    // 右下角武器栏
    void DrawWeaponStatus(int weaponId, int ammo, int magazine,
                          int viewW = 800, int viewH = 600);

    // 视野遮罩
    void DrawVisionFog(float playerX, float playerY, float aimAngle);

    // 烟雾云 / 闪光致盲整屏覆盖
    void DrawSmokeCloud(float worldX, float worldY, float worldRadius,
                        float age, float expandTime, float life, float maxLife, float seed);
    // 燃烧弹火区：焦黑地面 + 闪烁的橙色火团
    void DrawFireZone(float worldX, float worldY, float worldRadius,
                      float age, float life, float maxLife, float seed);
    void DrawFlashOverlay(Uint8 alpha);

    // 命中反馈（屏幕坐标，X 形准星）与受伤红色边缘
    void DrawHitMarker(float screenX, float screenY, float scale, Uint8 alpha);
    void DrawDamageVignette(Uint8 alpha);

    // 视线检查（供 CS2DGame 使用）
    bool HasLineOfSight(float fromX, float fromY, float toX, float toY) const;

    // ---- 玩家视野可见度（唯一来源，遮罩与敌人剔除共用） ----
    // 返回 [0,1]：距离因子(VISION_RADIUS, VISION_SOFT_EDGE 软边)
    //          × 扇形因子(VISION_CONE_DEG 总角, VISION_ANGLE_SOFT 软边)
    //          × 视线遮挡，三者取最小。0 表示完全不可见。
    float ComputeVisibility(float playerX, float playerY, float aimAngle,
                            float targetX, float targetY) const;

    // 目标是否落在玩家视野内（ComputeVisibility > 0）
    bool IsVisibleToPlayer(float playerX, float playerY, float aimAngle,
                           float targetX, float targetY) const {
        return ComputeVisibility(playerX, playerY, aimAngle, targetX, targetY) > 0.0f;
    }

    float GetCameraX() const { return cameraX_; }
    float GetCameraY() const { return cameraY_; }
    float GetZoom()   const { return zoom_; }
    float GetVisionRadius() const { return visionRadius_; }
    void  SetZoom(float z) { zoom_ = z; }
    // 选阵营/地图总览：直接指定世界中心与缩放，不经过玩家视野拟合。
    void  SetPreviewCamera(float centerX, float centerY, float zoom,
                           bool clampToMap = true);
    void  SetViewportSize(int w, int h);   // 全屏 FOV 自适应后重建视野纹理

private:
    SDL_Renderer* renderer_ = nullptr;
    const CS2DWorld* world_ = nullptr;
    std::string mapDir_;

    float cameraX_ = 0.0f;
    float cameraY_ = 0.0f;
    float zoom_    = 6.0f;   // 镜头缩放（6px 命中盒 → 屏上约 36px）
    // 玩家视野半径：随视口/缩放动态更新，覆盖屏幕边缘但不超过上限。
    float visionRadius_ = Tuning::VisionRadius;
    static constexpr float MAX_PLAYER_VISION_RADIUS = 320.0f;

    int viewW_ = 800;        // 逻辑视口尺寸（供 tile 剔除与视野纹理重建）
    int viewH_ = 600;

    // ========= 背景 & tileset =========
    SDL_Texture* backgroundTex_ = nullptr;

    struct TilesetTexture {
        SDL_Texture* texture  = nullptr;
        int          tileW     = 32;
        int          tileH     = 32;
        int          columns   = 0;
        int          firstGid  = 0;
    };
    std::vector<TilesetTexture> tilesetTextures_;

    bool FindTileSrc(int gid, SDL_Texture*& outTex, SDL_Rect& outSrc) const;
    SDL_Texture* LoadTexture(const std::string& filename);
    int  failedLoads_ = 0;   // 本次 SetWorld 期间加载失败的贴图数（启动期素材审计用）
    void DrawTileLayer(const std::vector<int>& data, int layerWidth, int layerHeight);
    void DrawImageBackground();
    void DrawTileFallback(int gid, const SDL_Rect& dst) const;

    // ========= 精灵系统 =========
    // 三张角色序列帧贴图（2列×3行；切片帧按武器槽固定，不再循环播放）
    SpriteSheetDef ctSheet_   {};   // soldier_with_helmet    64×96 → 32×32/frame
    SpriteSheetDef tSheet_    {};   // soldier_no_helmet      64×96 → 32×32/frame
    SpriteSheetDef tAltSheet_ {};   // soldier_by_risingXD   64×94 → 32×31/frame

    // 武器贴图 / 图标（索引 = WeaponId）
    std::vector<SDL_Texture*> weaponSprites_;
    std::vector<SDL_Texture*> weaponIcons_;

    SDL_Texture* bulletTex_ = nullptr;
    SDL_Texture* smokeTex_  = nullptr;   // 程序生成的柔和烟雾贴图（fBm 噪声）
    void CreateSmokeTexture();

    // 雷达底图：把地图背景（或碰撞网格）一次性烘焙成纹理，避免每帧逐像素采样
    SDL_Texture* radarMapTex_ = nullptr;
    int   radarMapW_ = 0, radarMapH_ = 0;       // 底图分辨率
    float radarImgX_ = 0.0f, radarImgY_ = 0.0f; // 图像在底图内的偏移（letterbox）
    float radarImgW_ = 0.0f, radarImgH_ = 0.0f;
    void BuildRadarTexture();

    void DrawWeaponForCharacter(float cx, float cy, float size, float aimAngle,
                                int weaponSlot, int weaponId, bool muzzleFlash);
    void DrawMuzzleFlash(float cx, float cy, float size, float aimAngle, int weaponSlot);

    // ========= 视野遮罩 =========
    static constexpr int VISION_MASK_SCALE = 4;
    // 几何参数统一在 Tuning.hpp（玩家与 BOT 共用），此处只留别名以便阅读
    static constexpr float VISION_RADIUS     = Tuning::VisionRadius;
    static constexpr float VISION_SOFT_EDGE  = Tuning::VisionSoftEdge;
    static constexpr float VISION_CONE_DEG   = Tuning::VisionConeDeg;
    static constexpr float VISION_ANGLE_SOFT = Tuning::VisionAngleSoftDeg;
    static constexpr Uint8 VISION_FOG_ALPHA  = 160;     // 不可见区半透明上限
    static constexpr Uint8 VISION_FOG_R      = 200;     // 浅灰遮罩颜色
    static constexpr Uint8 VISION_FOG_G      = 200;
    static constexpr Uint8 VISION_FOG_B      = 200;

    SDL_Texture*      visionTex_ = nullptr;
    std::vector<Uint8> visionPixels_;
    int maskW_ = 0;
    int maskH_ = 0;

    void CreateVisionTexture();   // 创建/重建视野 streaming 纹理

};
