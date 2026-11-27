#pragma once
#include <SDL2/SDL.h>
#include "Tuning.hpp"
#include "AimLineStyle.hpp"
#include "CS2DRenderer.hpp"
#include <vector>
#include <functional>

// 前向声明
class CS2DRenderer;
class CS2DGame;

// ============================================================================
// CS2DOverlay — 准星、雷达、计分板独立模块（从 Engine 中抽出）
// ============================================================================

struct CrosshairCfg {
    Uint8 r = 255, g = 255, b = 255, a = 200;
    float thickness = 1.5f;
    bool  centerDot = true;
    float length    = 12.0f;
    float gap       = 3.0f;
    bool  aimLine   = true;
    bool  dynamic   = true;   // true=随移动扩散；false=固定准星
};

struct RadarCfg {
    bool  playerCentered      = false;   // 默认整张地图，玩家点在图上真实移动
    bool  rotateWithPlayer    = false;
    bool  blendBg             = false;
    int   bgAlpha             = 160;
    int   hudSize             = 120;
    float mapZoom             = 1.0f;    // 1 = 整张地图铺满雷达，>1 放大
};

class CS2DOverlay {
public:
    CrosshairCfg crosshair;
    AimLineStyle aimLineStyle;
    RadarCfg     radar;
    bool         showScoreboard = false;

    // 虚拟准星（屏幕坐标，相对鼠标模式）
    float virtCursorX = 400.0f;
    float virtCursorY = 300.0f;

    // ---- 输入处理 ----
    void InitializeCursor(float angle);
    void SetReferenceMode(float zoom, float cameraShift);
    void SetReferenceWorldBounds(float width, float height) {
        referenceWorldW_ = width;
        referenceWorldH_ = height;
    }
    void SyncScopeCursor(bool scoped, bool closing, bool cameraReady, float dt = 0.0f, bool commitAllowed = true);
    void SetVisibleAimPoint(float x, float y) { visibleWorld_ = {x, y}; visibleWorldValid_ = true; }
    bool ScreenAnchorActive() const { return screenAnchor_; }
    bool EdgeZoomRequested() const { return edgeZoomRequested_; }
    int CameraDistanceTier() const { return cameraDistanceTier_; }
    void UpdateCursor(int relX, int relY, float sensitivity, float zoom,
                      float camX, float camY, int viewW, int viewH, float playerX, float playerY,
                      float& outWorldX, float& outWorldY, float recoilAngle = 0.0f,
                      Vec2 renderOffset = {0.0f, 0.0f}, Vec2 ballisticKick = {});
    static std::vector<CS2DRenderer::RadarDot> CollectRadarDots(const CS2DGame& game);

    // ---- 渲染 ----
    void Render(CS2DRenderer* renderer, CS2DGame* game, int viewW, int viewH);

    // ---- 供 BuildActions 的 setter ----
    std::function<void(Uint8,Uint8,Uint8,Uint8)> MakeSetCrosshairColor();
    std::function<void(float)>  MakeSetCrosshairThickness();
    std::function<void(bool)>   MakeSetCrosshairCenterDot();
    std::function<void(float)>  MakeSetCrosshairLength();
    std::function<void(float)>  MakeSetCrosshairGap();
    std::function<void(bool)>   MakeSetRadarPlayerCentered();
    std::function<void(bool)>   MakeSetRadarRotateWithPlayer();
    std::function<void(bool)>   MakeSetRadarBlendBg();
    std::function<void(int)>    MakeSetRadarBgAlpha();
    std::function<void(int)>    MakeSetRadarHudSize();
    std::function<void(float)>  MakeSetRadarMapZoom();
private:
    float aimOffsetX_ = Tuning::InitialAimDistance, aimOffsetY_ = 0.0f;
    float anchorX_ = 0.0f, anchorY_ = 0.0f;
    SDL_FPoint visibleWorld_{}, anchorWorld_{};
    bool visibleWorldValid_ = false, anchorWorldValid_ = false;
    float referenceModeZoom_ = 1.0f, referenceCameraShift_ = Tuning::CameraLookShift;
    float referenceWorldW_ = 0.0f, referenceWorldH_ = 0.0f;
    float handoffElapsed_ = 0.0f;
    bool finishHandoff_ = false;

    bool screenAnchor_ = false, scopeClosing_ = false;
    bool edgeZoomRequested_ = false;
    int cameraDistanceTier_ = 0;
    bool cursorScoped_ = false;
};
