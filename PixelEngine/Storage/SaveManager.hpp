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

    // 视野 / 开镜（契约 v1.2）
    int   fogOpacityPct          = 100;   // 迷雾透明度 0..100%，只影响视觉，不改可见性
    bool  sniperDelayedScopeClose = false; // 开镜开枪后：开启则按住扳机可延迟关镜

    // 准星
    int   crossR = 255, crossG = 255, crossB = 255, crossA = 200;
    float crossThickness = 1.5f;
    float crossLength    = 12.0f;
    float crossGap       = 3.0f;
    bool  crossCenterDot = true;
    bool  crossDynamic   = true;   // true=随移动扩散；false=固定准星

    // 雷达
    bool  radarPlayerCentered   = true;
    bool  radarRotateWithPlayer = false;
    bool  radarBlendBg          = false;
    int   radarBgAlpha          = 160;
    int   radarHudSize          = 120;
    float radarMapZoom          = 0.005f;

    // 主菜单 3D 自定义（索引 0=T, 1=CT, 2..4=三只小鸡）
    int   menuColor[5]     = { 0, 0, 0, 0, 0 };
    int   menuPose[5]      = { 0, 0, 0, 0, 0 };
    int   menuWeapon[2]    = { 0, 0 };
    int   menuAccessory[5] = { 0, 0, 0, 0, 0 };
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
