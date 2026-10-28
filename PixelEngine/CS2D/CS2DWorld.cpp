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

    // 同一个 CS2DWorld 可能在换图时复用，先清空上一张图的所有运行时数据。
    spawnPoints_.clear();
    bombSites_.clear();
    areas_.clear();
    buyZones_.clear();
    visibleLayerNames_.clear();
    visibleLayerData_.clear();
    backgroundImagePath_.clear();
    imageWidth_ = imageHeight_ = 0;

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
        if (n == "buy_zone" || n == "buyzone" || n == "buy_zones" ||
            n == "buyzones")                                           return "buy_zone";
        return "";
    };

    // bomb_site 的聚合必须先于实体层解析收集，等区域层建好后再按 A/B 命名。
    std::vector<std::vector<ObjectDef>> bombSiteLayers;

    for (const auto& ol : data.objectLayers) {
        const std::string lowerLayer = toLower(ol.name);
        if (lowerLayer == "collisions") continue;      // 已在上方处理

        const std::string layerKind = kindFromName(lowerLayer);
        std::vector<ObjectDef> layerBombRects;

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
                // 同一对象层里的所有矩形属于同一个包点；不能把每个矩形都当成包点。
                layerBombRects.push_back(obj);
            } else if (kind == "buy_zone" || kind == "buyzone") {
                buyZones_.push_back(obj);
            }
        }

        if (!layerBombRects.empty()) {
            bombSiteLayers.push_back(std::move(layerBombRects));
        }
    }

    // ---- 区域层：组图层里的每个对象层就是一个命名区域 ----
    // （Tiled 里 site 组下 A_Site / Mid / B_Long ... 各自是一层，里面是可行走矩形）
    for (const auto& ol : data.objectLayers) {
        if (ol.group.empty()) continue;      // 只认组里的图层，避免和顶层的实体层混淆
        AreaDef area;
        area.name = ol.name;
        area.rects = ol.objects;
        if (!area.rects.empty()) areas_.push_back(std::move(area));
    }
    if (!areas_.empty()) {
        std::cout << "[CS2DWorld] 战术区域 " << areas_.size() << " 个: ";
        for (size_t i = 0; i < areas_.size(); ++i) {
            std::cout << areas_[i].name << (i + 1 < areas_.size() ? " " : "");
        }
        std::cout << std::endl;
    }

    // ---- 包点聚合：每个 bomb_site 对象层 = 一个包点 ----
    // 用 A_Site / B_Site 区域中心判断该层属于哪一侧；找不到区域时按层顺序命名。
    auto areaCenter = [&](const std::string& want, float& ox, float& oy) -> bool {
        for (const auto& a : areas_) {
            if (a.name != want || a.rects.empty()) continue;
            float sumW = 0.0f, cx = 0.0f, cy = 0.0f;
            for (const auto& r : a.rects) {
                const float w = std::max(1.0f, r.width * r.height);
                cx += (r.x + r.width * 0.5f) * w;
                cy += (r.y + r.height * 0.5f) * w;
                sumW += w;
            }
            if (sumW <= 0.0f) return false;
            ox = cx / sumW;
            oy = cy / sumW;
            return true;
        }
        return false;
    };

    float aX = 0.0f, aY = 0.0f, bX = 0.0f, bY = 0.0f;
    const bool hasA = areaCenter("A_Site", aX, aY);
    const bool hasB = areaCenter("B_Site", bX, bY);

    for (size_t li = 0; li < bombSiteLayers.size(); ++li) {
        BombSiteDef site;
        site.rects = std::move(bombSiteLayers[li]);
        const ObjectDef& first = site.rects.front();
        site.minX = site.maxX = first.x;
        site.minY = site.maxY = first.y;
        for (const auto& r : site.rects) {
            site.minX = std::min(site.minX, r.x);
            site.minY = std::min(site.minY, r.y);
            site.maxX = std::max(site.maxX, r.x + r.width);
            site.maxY = std::max(site.maxY, r.y + r.height);
        }
        // 代表点取面积最大的矩形中心：包围盒中心可能落在多个包点矩形之间的空隙，
        // 会让 C4 携带者站在"包点中心"却不在任何包点矩形内，永远无法下包。
        const ObjectDef* largest = &site.rects.front();
        float largestArea = largest->width * largest->height;
        for (const auto& r : site.rects) {
            const float area = r.width * r.height;
            if (area > largestArea) { largestArea = area; largest = &r; }
        }
        site.cx = largest->x + largest->width * 0.5f;
        site.cy = largest->y + largest->height * 0.5f;

        if (hasA && hasB) {
            const float da = (site.cx - aX) * (site.cx - aX) +
                             (site.cy - aY) * (site.cy - aY);
            const float db = (site.cx - bX) * (site.cx - bX) +
                             (site.cy - bY) * (site.cy - bY);
            const bool onA = (da <= db);
            site.name = onA ? "A" : "B";
            site.area = onA ? "A_Site" : "B_Site";
        } else {
            site.name = (li == 0) ? "A" : (li == 1) ? "B"
                                                   : ("Site" + std::to_string(li + 1));
            site.area = FindAreaAt(site.cx, site.cy);
        }
        bombSites_.push_back(std::move(site));
    }

    if (!bombSites_.empty()) {
        std::cout << "[CS2DWorld] 包点 " << bombSites_.size() << " 个: ";
        for (const auto& s : bombSites_) {
            std::cout << s.name << "(" << s.rects.size() << "矩形) ";
        }
        std::cout << std::endl;
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

// ============================================================================
// 战术区域查询
// ============================================================================

std::string CS2DWorld::FindAreaAt(float x, float y) const {
    for (const auto& a : areas_) {
        for (const auto& r : a.rects) {
            if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height) {
                return a.name;
            }
        }
    }
    return std::string();
}

const CS2DWorld::BombSiteDef* CS2DWorld::FindBombSiteAt(float x, float y) const {
    for (const auto& s : bombSites_) {
        if (s.Contains(x, y)) return &s;
    }
    return nullptr;
}

std::vector<std::string> CS2DWorld::AreaNames() const {
    std::vector<std::string> names;
    names.reserve(areas_.size());
    for (const auto& a : areas_) names.push_back(a.name);
    return names;
}
