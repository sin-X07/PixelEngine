#include "CS2DWorld.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

bool CS2DWorld::Build(const CS2DMapData& data) {
    mapData_    = &data;
    mapWidth_   = data.mapWidth;
    mapHeight_  = data.mapHeight;
    tileWidth_  = data.tileWidth;
    tileHeight_ = data.tileHeight;

    // ---- 碰撞网格 ----
    const size_t gridSize = static_cast<size_t>(mapWidth_) * mapHeight_;
    collisionGrid_.assign(gridSize, CellType::Empty);
    damageGrid_.assign(gridSize, 0);

    // ---- 图片层（背景图）----
    for (const auto& il : data.imageLayers) {
        if (!il.visible) continue;
        // 取第一个可见图片层作为背景
        backgroundImagePath_ = il.imagePath;
        imageWidth_  = il.imageWidth;
        imageHeight_ = il.imageHeight;
        std::cout << "[CS2DWorld] 背景图: " << il.imagePath
                  << " (" << il.imageWidth << "x" << il.imageHeight << ")" << std::endl;
        break;  // 只取第一张
    }

    // ---- 从 object layer 构建碰撞矩形 ----
    // 遍历所有 object layer，将 Collisions 层的矩形写入碰撞网格
    for (const auto& ol : data.objectLayers) {
        std::string lower;
        lower.reserve(ol.name.size());
        for (char c : ol.name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower == "collisions") {
            for (const auto& obj : ol.objects) {
                // 计算该矩形覆盖的 tile 范围
                float rx = obj.x;
                float ry = obj.y;
                float rw = obj.width;
                float rh = obj.height;

                // 跳过零尺寸对象（可能有空 rect）
                if (rw <= 0.0f || rh <= 0.0f) continue;

                int colMin = static_cast<int>(std::floor(rx / tileWidth_));
                int colMax = static_cast<int>(std::floor((rx + rw - 0.001f) / tileWidth_));
                int rowMin = static_cast<int>(std::floor(ry / tileHeight_));
                int rowMax = static_cast<int>(std::floor((ry + rh - 0.001f) / tileHeight_));

                // 裁剪到地图边界
                colMin = std::max(0, colMin);
                colMax = std::min(mapWidth_  - 1, colMax);
                rowMin = std::max(0, rowMin);
                rowMax = std::min(mapHeight_ - 1, rowMax);

                for (int row = rowMin; row <= rowMax; ++row) {
                    for (int col = colMin; col <= colMax; ++col) {
                        collisionGrid_[row * mapWidth_ + col] = CellType::Solid;
                    }
                }
            }
            std::cout << "[CS2DWorld] 碰撞层: '" << ol.name << "' "
                      << ol.objects.size() << " 个矩形" << std::endl;
        }
    }

    // ---- 对象层解析 ----
    // 语义来源优先级：对象自身的 type > 所在图层名
    // 这样既支持「一个 Entities 层 + 各类 type」，也支持 Tiled 常见的
    // 「每类一个对象层」写法（spawn_t / spawn_ct / bomb_site / buy_zone ...）
    auto toLower = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };

    auto kindFromName = [](const std::string& n) -> std::string {
        if (n == "spawn_t" || n == "spawnt" || n == "spawn_terrorist") return "spawn_t";
        if (n == "spawn_ct"  || n == "spawnct")                        return "spawn_ct";
        if (n == "spawn" || n == "spawns" || n == "spawnpoint" ||
            n == "spawnpoints" || n == "entities_spawn")               return "spawn";
        if (n == "bomb_site" || n == "bombsites" || n == "bombsite" ||
            n == "bomb_sites" || n == "site")                          return "bomb_site";
        if (n == "hostage" || n == "hostages")                         return "hostage";
        if (n == "buy_zone" || n == "buyzone" || n == "buy_zones" ||
            n == "buyzones")                                           return "buy_zone";
        return "";
    };

    for (const auto& ol : data.objectLayers) {
        const std::string lowerLayer = toLower(ol.name);
        if (lowerLayer == "collisions") continue;      // 已在上方处理

        const std::string layerKind = kindFromName(lowerLayer);

        for (const auto& obj : ol.objects) {
            std::string kind = toLower(obj.type);
            if (kind.empty()) kind = layerKind;
            if (kind.empty()) continue;

            if (kind == "spawnpoint" || kind == "spawn" ||
                kind == "spawn_t" || kind == "spawn_ct") {
                SpawnPoint sp;
                sp.x = obj.x;
                sp.y = obj.y;
                sp.isCT = (kind == "spawn_ct");
                sp.teamSpecific = (kind == "spawn_t" || kind == "spawn_ct");
                spawnPoints_.push_back(sp);
            } else if (kind == "bomb_site" || kind == "bombsites" || kind == "bombsite") {
                bombSites_.push_back(obj);
            } else if (kind == "hostage" || kind == "hostages") {
                hostages_.push_back(obj);
            } else if (kind == "buy_zone" || kind == "buyzone") {
                buyZones_.push_back(obj);
            }
        }
    }

    // ---- 可见 tile 层（没有 tile layer 则跳过）----
    for (const auto& layer : data.tileLayers) {
        if (!layer.visible) continue;
        std::string lower;
        lower.reserve(layer.name.size());
        for (char c : layer.name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "ground" || lower == "walls" || lower == "decorations" || lower == "overlay") {
            visibleLayerNames_.push_back(layer.name);
            visibleLayerData_.push_back(layer.data);
        }
    }

    int spawnT = 0, spawnCT = 0, spawnGeneric = 0;
    for (const auto& sp : spawnPoints_) {
        if (!sp.teamSpecific) ++spawnGeneric;
        else if (sp.isCT)     ++spawnCT;
        else                  ++spawnT;
    }

    std::cout << "[CS2DWorld] 构建完成: " << mapWidth_ << "x" << mapHeight_
              << " (" << (mapWidth_ * tileWidth_) << "x" << (mapHeight_ * tileHeight_) << " px)"
              << ", 背景图 " << (HasBackgroundImage() ? backgroundImagePath_ : "无")
              << ", 出生点 " << spawnPoints_.size() << " (T" << spawnT
              << "/CT" << spawnCT
              << (spawnGeneric > 0 ? "/通用" + std::to_string(spawnGeneric) : "")
              << ")"
              << ", " << bombSites_.size() << " 个包点"
              << ", " << hostages_.size() << " 名人质"
              << ", " << buyZones_.size() << " 个购买区" << std::endl;
    return true;
}

// 指定阵营的出生点；无专属点时回退到全部出生点
std::vector<const CS2DWorld::SpawnPoint*> CS2DWorld::SpawnsForTeam(bool isCT) const {
    std::vector<const SpawnPoint*> specific;
    for (const auto& sp : spawnPoints_) {
        if (sp.teamSpecific && sp.isCT == isCT) specific.push_back(&sp);
    }
    if (!specific.empty()) return specific;

    // 回退：使用全部出生点
    std::vector<const SpawnPoint*> all;
    all.reserve(spawnPoints_.size());
    for (const auto& sp : spawnPoints_) all.push_back(&sp);
    return all;
}

// ========= 碰撞查询 =========

void CS2DWorld::WorldToTile(float worldX, float worldY, int& outRow, int& outCol) const {
    outCol = static_cast<int>(std::floor(worldX / tileWidth_));
    outRow = static_cast<int>(std::floor(worldY / tileHeight_));
}

bool CS2DWorld::IsSolid(int tileRow, int tileCol) const {
    if (tileRow < 0 || tileRow >= mapHeight_ || tileCol < 0 || tileCol >= mapWidth_) return true;
    const CellType t = collisionGrid_[tileRow * mapWidth_ + tileCol];
    return t == CellType::Solid || t == CellType::Damage;
}

bool CS2DWorld::IsBlockingPlayer(int tileRow, int tileCol) const {
    if (tileRow < 0 || tileRow >= mapHeight_ || tileCol < 0 || tileCol >= mapWidth_) return true;
    const CellType t = collisionGrid_[tileRow * mapWidth_ + tileCol];
    return t == CellType::Solid || t == CellType::Damage || t == CellType::Penetrable;
}

bool CS2DWorld::IsBlockingBullet(int tileRow, int tileCol) const {
    if (tileRow < 0 || tileRow >= mapHeight_ || tileCol < 0 || tileCol >= mapWidth_) return true;
    const CellType t = collisionGrid_[tileRow * mapWidth_ + tileCol];
    return t == CellType::Solid || t == CellType::Damage;
}

int CS2DWorld::GetDamageAt(int tileRow, int tileCol) const {
    if (tileRow < 0 || tileRow >= mapHeight_ || tileCol < 0 || tileCol >= mapWidth_) return 0;
    return damageGrid_[tileRow * mapWidth_ + tileCol];
}

bool CS2DWorld::CheckRectCollision(float x, float y, float w, float h) const {
    int colMin = static_cast<int>(std::floor(x / tileWidth_));
    int colMax = static_cast<int>(std::floor((x + w - 0.001f) / tileWidth_));
    int rowMin = static_cast<int>(std::floor(y / tileHeight_));
    int rowMax = static_cast<int>(std::floor((y + h - 0.001f) / tileHeight_));

    for (int row = rowMin; row <= rowMax; ++row) {
        for (int col = colMin; col <= colMax; ++col) {
            if (IsBlockingPlayer(row, col)) return true;
        }
    }
    return false;
}