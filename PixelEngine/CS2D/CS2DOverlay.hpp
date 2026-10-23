#pragma once
#include <SDL2/SDL.h>
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
    RadarCfg     radar;
    bool         centerRadarOn = false;

    // 虚拟准星（屏幕坐标，相对鼠标模式）
    float virtCursorX = 400.0f;
    float virtCursorY = 300.0f;

    // Tab 边沿检测（由外部设置原始状态）
    bool keyTab  = false;
    bool prevTab = false;

    // ---- 输入处理 ----
    void UpdateCursor(int relX, int relY, float sensitivity, float zoom,
                      float camX, float camY, int viewW, int viewH,
                      float& outWorldX, float& outWorldY);
    void UpdateTabToggle();

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
};