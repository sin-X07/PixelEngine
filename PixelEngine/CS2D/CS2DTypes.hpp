#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <unordered_map>

// ============================================================================
// CS2D 专属数据结构 —— 完全独立于现有模式一/二的类型体系
// ============================================================================

// ---- 图块自定义属性（从 Tiled tileset tileproperties 解析） ----
struct TileProperties {
    bool  solid      = false;   // 碰撞阻挡（玩家/BOT/子弹均不可通过）
    bool  penetrable = false;   // 子弹可穿透但实体不可通过（如栅栏/铁丝网）
    int   damage     = 0;       // 伤害地形，每帧伤害量（>0 时生效）
};

// ---- Tiled tileset 描述 ----
struct TilesetDef {
    int         firstGid  = 0;        // tileset 全局起始 GID
    std::string name;
    std::string imagePath;            // 图块集贴图（相对于 .tmj 文件）
    int         tileWidth  = 32;
    int         tileHeight = 32;
    int         tileCount  = 0;
    int         columns    = 0;
    // GID → 图块属性映射
    std::unordered_map<int, TileProperties> tileProps;
};

// ---- 瓦片图层数据 ----
struct TileLayerDef {
    std::string name;
    float       opacity    = 1.0f;
    bool        visible    = true;
    int         width      = 0;
    int         height     = 0;
    std::vector<int> data;          // 1D 数组, row-major, 全局 GID
};

// ---- 对象层中的单个对象 ----
struct ObjectDef {
    std::string type;               // 对象类型（spawn_t / spawn_ct / bomb_site ...）
    std::string name;               // 对象名称
    float       x       = 0.0f;
    float       y       = 0.0f;
    float       width   = 0.0f;
    float       height  = 0.0f;
    // 扩展属性（按需）
    std::unordered_map<std::string, std::string> properties;
};

// ---- 图片层数据（背景图等） ----
struct ImageLayerDef {
    std::string name;
    std::string imagePath;      // 图片路径（相对 TMJ 所在目录）
    int         imageWidth  = 0;
    int         imageHeight = 0;
    float       opacity    = 1.0f;
    bool        visible    = true;
};

// ---- 对象层数据 ----
struct ObjectLayerDef {
    std::string name;
    std::string group;              // 所属组图层名（空=顶层）；区域层放在 "site" 组里
    float       opacity = 1.0f;
    bool        visible = true;
    std::vector<ObjectDef> objects;
};

// ---- TMJ 解析完成后的完整中间数据 ----
struct CS2DMapData {
    int    mapWidth     = 0;
    int    mapHeight    = 0;
    int    tileWidth    = 32;
    int    tileHeight   = 32;
    bool   infinite     = false;

    std::vector<TilesetDef>     tilesets;
    std::vector<TileLayerDef>   tileLayers;
    std::vector<ImageLayerDef>  imageLayers;
    std::vector<ObjectLayerDef> objectLayers;

    // 辅助：根据全局 GID 查找对应 tileset 和局部 id
    const TilesetDef* FindTileset(int gid) const;
    const TileProperties* FindTileProperties(int gid) const;
};

// ---- 内联辅助实现 ----
inline const TilesetDef* CS2DMapData::FindTileset(int gid) const {
    if (gid <= 0 || tilesets.empty()) return nullptr;
    for (size_t i = tilesets.size(); i > 0; --i) {
        if (static_cast<int>(tilesets[i - 1].firstGid) <= gid) {
            return &tilesets[i - 1];
        }
    }
    return nullptr;
}

inline const TileProperties* CS2DMapData::FindTileProperties(int gid) const {
    const TilesetDef* ts = FindTileset(gid);
    if (!ts) return nullptr;
    auto it = ts->tileProps.find(gid);
    if (it != ts->tileProps.end()) return &it->second;
    return nullptr;
}