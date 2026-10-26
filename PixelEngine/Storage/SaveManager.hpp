#pragma once
#include <string>

// ============================================================================
// SaveManager — CS2D 单机版存档
//
// 持久化内容:
//   [settings] 灵敏度、分辨率、帧率、全屏
//   [crosshair] 准星颜色/粗细/长度/间隔/中心点
//   [radar]    雷达设置
//
// 落盘位置: exe 同级 cs2d_save.ini（ResolveWritablePath）
// ============================================================================

struct GameSettings {
    float mouseSensitivity = 1.0f;
    int   resIdx           = 0;
    bool  fullscreen       = false;
    int   targetFPS        = 60;
    int   renderScalePct   = 100;   // 内部渲染缩放（100 / 75 / 50），与窗口大小解耦

    // 准星
    int   crossR = 255, crossG = 255, crossB = 255, crossA = 200;
    float crossThickness = 1.5f;
    float crossLength    = 12.0f;
    float crossGap       = 3.0f;
    bool  crossCenterDot = true;

    // 雷达
    bool  radarPlayerCentered   = true;
    bool  radarRotateWithPlayer = false;
    bool  radarBlendBg          = false;
    int   radarBgAlpha          = 160;
    int   radarHudSize          = 120;
    float radarMapZoom          = 0.005f;
};

class SaveManager {
public:
    SaveManager() = default;

    // 读取 / 写入 cs2d_save.ini
    void Load();
    void Save();

    GameSettings&       MutableSettings()       { return settings_; }
    const GameSettings& GetSettings() const     { return settings_; }

private:
    GameSettings settings_;
    bool loaded_ = false;
};
