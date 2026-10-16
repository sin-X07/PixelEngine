#pragma once
#include "CS2DWorld.hpp"
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <vector>
#include <string>
#include <unordered_map>

// ============================================================================
// CS2DRenderer —— CS2D 独立渲染器
//   背景图 + tileset tile 层 + 实体绘制
// ============================================================================
class CS2DRenderer {
public:
    CS2DRenderer() = default;
    ~CS2DRenderer() { Clean(); }

    bool Initialize(SDL_Renderer* ren, const std::string& mapDir);
    void Clean();

    // 关联世界数据（用于获取图层/tileset/背景图信息）
    void SetWorld(const CS2DWorld* world);

    // 更新相机位置（以玩家为中心）
    void UpdateCamera(float playerCenterX, float playerCenterY,
                      int viewportW, int viewportH);

    // 主体绘制：背景图 → tile layers → overlay
    void DrawWorld();
    void DrawOverlay();

    // 绘制玩家（简单矩形 + 朝向线）
    void DrawPlayer(float x, float y, float size, float aimAngle,
                    bool isCT, Uint8 alpha = 255);
    void DrawBOT(float x, float y, float size, float aimAngle,
                 bool isCT, Uint8 alpha = 255);
    void DrawBullet(float x, float y, float vx, float vy);
    void DrawCrosshair(float cx, float cy, float size = 12.0f);

    // 相机访问
    float GetCameraX() const { return cameraX_; }
    float GetCameraY() const { return cameraY_; }
    float GetZoom()   const { return zoom_; }
    void  SetZoom(float z) { zoom_ = z; }

private:
    SDL_Renderer* renderer_ = nullptr;
    const CS2DWorld* world_ = nullptr;
    std::string mapDir_;

    float cameraX_ = 0.0f;
    float cameraY_ = 0.0f;
    float zoom_    = 1.0f;

    // 背景图纹理
    SDL_Texture* backgroundTex_ = nullptr;

    // tileset 纹理
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
    void DrawTileLayer(const std::vector<int>& data, int layerWidth, int layerHeight);
    void DrawImageBackground();
    void DrawTileFallback(int gid, const SDL_Rect& dst) const;
};