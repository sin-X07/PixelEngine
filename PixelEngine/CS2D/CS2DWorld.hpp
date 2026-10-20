#pragma once
#include "CS2DTypes.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <unordered_map>

// ============================================================================
// CS2DWorld —— CS2D 运行时世界：
//   - 碰撞网格（基于 object layer 矩形 或 tile 属性）
//   - 图片层背景
//   - 实体出生点列表
// ============================================================================
class CS2DWorld {
public:
    // 碰撞类型枚举（逐 tile 记录）
    enum class CellType : uint8_t {
        Empty      = 0,   // 可通行
        Solid      = 1,   // 完全阻挡
        Penetrable = 2,   // 子弹可穿透但实体不可通过
        Damage     = 3,   // 伤害地形
    };

    struct SpawnPoint {
        float x = 0.0f;
        float y = 0.0f;
        bool  isCT = false;
        bool  teamSpecific = false;   // true: spawn_t/spawn_ct 指定；false: 通用 SpawnPoint
    };

    CS2DWorld() = default;

    // 从解析后的 CS2DMapData 构建运行时数据
    bool Build(const CS2DMapData& data);

    // 碰撞查询
    bool IsSolid(int tileRow, int tileCol) const;
    bool IsBlockingPlayer(int tileRow, int tileCol) const;
    bool IsBlockingBullet(int tileRow, int tileCol) const;
    int  GetDamageAt(int tileRow, int tileCol) const;

    // 将世界坐标映射到瓦片坐标
    void WorldToTile(float worldX, float worldY, int& outRow, int& outCol) const;

    // 访问器
    int  GetMapWidth()      const { return mapWidth_; }
    int  GetMapHeight()     const { return mapHeight_; }
    int  GetTileWidth()     const { return tileWidth_; }
    int  GetTileHeight()    const { return tileHeight_; }
    float GetMapPixelW()    const { return static_cast<float>(mapWidth_  * tileWidth_); }
    float GetMapPixelH()    const { return static_cast<float>(mapHeight_ * tileHeight_); }
    float GetImageW()       const { return imageWidth_; }
    float GetImageH()       const { return imageHeight_; }

    const std::string& GetBackgroundImagePath() const { return backgroundImagePath_; }
    bool               HasBackgroundImage()     const { return !backgroundImagePath_.empty(); }

    const std::vector<CellType>& GetCollisionGrid() const { return collisionGrid_; }

    const std::vector<std::string>&      GetVisibleLayerNames() const { return visibleLayerNames_; }
    const std::vector<std::vector<int>>& GetVisibleLayerData()  const { return visibleLayerData_; }

    const std::vector<SpawnPoint>&  GetSpawnPoints()  const { return spawnPoints_; }

    // ---- 目标物 / 购买区（来自 Entities 层对象，Phase 4/5 使用）----
    const std::vector<ObjectDef>& GetBombSites()  const { return bombSites_; }
    const std::vector<ObjectDef>& GetHostages()   const { return hostages_; }
    const std::vector<ObjectDef>& GetBuyZones()   const { return buyZones_; }

    // 指定阵营的出生点；无专属点时回退到全部出生点
    std::vector<const SpawnPoint*> SpawnsForTeam(bool isCT) const;

    const CS2DMapData* GetMapData() const { return mapData_; }

    // 检查世界坐标矩形是否与任何固体瓦片碰撞
    bool CheckRectCollision(float x, float y, float w, float h) const;

private:
    int mapWidth_     = 0;
    int mapHeight_    = 0;
    int tileWidth_    = 8;
    int tileHeight_   = 8;

    const CS2DMapData* mapData_ = nullptr;

    // 图片层背景
    std::string backgroundImagePath_;
    int         imageWidth_  = 0;
    int         imageHeight_ = 0;

    // 碰撞网格 (row * mapWidth_ + col)
    std::vector<CellType> collisionGrid_;
    std::vector<int>      damageGrid_;

    // 可见渲染层数据（tile layers）
    std::vector<std::string>       visibleLayerNames_;
    std::vector<std::vector<int>>  visibleLayerData_;

    // 实体出生点
    std::vector<SpawnPoint>  spawnPoints_;

    // 目标物 / 购买区
    std::vector<ObjectDef>   bombSites_;
    std::vector<ObjectDef>   hostages_;
    std::vector<ObjectDef>   buyZones_;
};