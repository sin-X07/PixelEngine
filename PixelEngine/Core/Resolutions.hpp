#pragma once
// ============================================================================
// Resolutions — 常见分辨率清单（窗口/全屏共用的内部渲染分辨率）
//
// 方案 B：分辨率 = 内部渲染分辨率（逻辑尺寸），与窗口/桌面尺寸解耦。
//   - 窗口模式：逻辑尺寸 = 所选分辨率；窗口尽量按该尺寸开，超出桌面则裁到可用区
//   - 全屏模式：桌面全屏铺满，逻辑尺寸仍按所选分辨率（必要时 letterbox）
// ============================================================================

struct ResolutionEntry {
    int         w;
    int         h;
    const char* label;   // 下拉框显示文本（含比例）
};

inline const ResolutionEntry kResolutions[] = {
    // 16:9
    {1280,  720, "1280 × 720   (16:9)"},
    {1366,  768, "1366 × 768   (16:9)"},
    {1600,  900, "1600 × 900   (16:9)"},
    {1920, 1080, "1920 × 1080  (16:9)"},
    {2560, 1440, "2560 × 1440  (16:9)"},
    {3840, 2160, "3840 × 2160  (16:9  4K)"},
    // 16:10
    {1280,  800, "1280 × 800   (16:10)"},
    {1440,  900, "1440 × 900   (16:10)"},
    {1680, 1050, "1680 × 1050  (16:10)"},
    {1920, 1200, "1920 × 1200  (16:10)"},
    {2560, 1600, "2560 × 1600  (16:10)"},
    // 4:3
    {800,   600, "800 × 600    (4:3)"},
    {1024,  768, "1024 × 768   (4:3)"},
    {1280,  960, "1280 × 960   (4:3)"},
    {1600, 1200, "1600 × 1200  (4:3)"},
    // 5:4
    {1280, 1024, "1280 × 1024  (5:4)"},
    // 21:9 带鱼屏
    {2560, 1080, "2560 × 1080  (21:9)"},
    {3440, 1440, "3440 × 1440  (21:9)"},
};

inline constexpr int kResolutionCount =
    static_cast<int>(sizeof(kResolutions) / sizeof(kResolutions[0]));

// 兼容旧代码里的 kResCount
inline constexpr int kResCount = kResolutionCount;

// 找最接近给定宽高的档位（用于旧存档索引迁移 / 恢复默认）
inline int FindClosestResolution(int w, int h) {
    int best = 0;
    int bestScore = 0x7fffffff;
    for (int i = 0; i < kResolutionCount; ++i) {
        const int dw = kResolutions[i].w - w;
        const int dh = kResolutions[i].h - h;
        const int score = dw * dw + dh * dh;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}
