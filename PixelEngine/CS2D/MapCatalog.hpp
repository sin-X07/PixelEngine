#pragma once
// ============================================================================
// MapCatalog — 可选地图清单（地图选择界面用）
//
// 新增地图只需要在这里加一条，界面会自动多一张卡片。
// ============================================================================

#include <cstddef>

struct MapEntry {
    const char* id;         // 内部标识
    const char* name;       // 显示名
    const char* tagline;    // 一句话描述
    const char* tmj;        // 地图文件（相对 exe）
    const char* preview;    // 预览图（相对 exe）
    int         bombSites;  // 包点数量（展示用）
    int         areas;      // 战术区域数量（展示用）
};

inline const MapEntry kMapCatalog[] = {
    {
        "ancient_ruins",
        "古代遗迹",
        "双包点 · 中路争夺 · 攻防均衡",
        "assets/cs2d/maps/ancient_ruins.tmj",
        "assets/cs2d/tilesets/ancient_ruins.jpg",
        2, 29
    },
    {
        "dust_2",
        "沙漠Ⅱ",
        "经典沙漠 · 长枪对枪 · 转点路线多",
        "assets/cs2d/maps/dust_2.tmj",
        "assets/cs2d/tilesets/dust_2.png",
        2, 35
    },
    {
        "mirage",
        "荒漠迷城",
        "中路控制 · 双包点 · 转点节奏快",
        "assets/cs2d/maps/mirage.tmj",
        "assets/cs2d/tilesets/mirage.jpg",
        2, 32
    },
};

inline constexpr int kMapCatalogCount =
    static_cast<int>(sizeof(kMapCatalog) / sizeof(kMapCatalog[0]));
