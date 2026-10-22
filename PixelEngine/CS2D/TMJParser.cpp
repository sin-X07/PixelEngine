#include <functional>
#include "TMJParser.hpp"
#include "MiniJSON.hpp"
#include "../Core/PathUtils.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>

using json = MiniJSON::Value;

namespace {

// 解析 Tiled 自定义属性（兼容两种格式）
static void ParseProperties(const json& props, std::unordered_map<std::string, std::string>& out) {
    if (props.is_array()) {
        for (size_t i = 0; i < props.size(); ++i) {
            const auto& prop = props[i];
            if (!prop.contains("name") || !prop.contains("value")) continue;
            std::string name = prop["name"].get_str();
            if (prop["value"].is_string()) {
                out[name] = prop["value"].get_str();
            } else if (prop["value"].is_bool()) {
                out[name] = prop["value"].safe_get<bool>(false) ? "true" : "false";
            } else if (prop["value"].is_number()) {
                out[name] = std::to_string(prop["value"].safe_get<double>(0.0));
            }
        }
    } else if (props.is_object()) {
        // 直接遍历对象的键值对（旧式格式）
        // MiniJSON 不支持直接遍历；手动处理
    }
}

// 将属性字典转为 TileProperties
static TileProperties PropsToTile(const std::unordered_map<std::string, std::string>& props) {
    TileProperties tp;
    auto itSolid = props.find("solid");
    if (itSolid != props.end()) {
        tp.solid = (itSolid->second == "true" || itSolid->second == "1");
    }
    auto itPen = props.find("penetrable");
    if (itPen != props.end()) {
        tp.penetrable = (itPen->second == "true" || itPen->second == "1");
    }
    auto itDmg = props.find("damage");
    if (itDmg != props.end()) {
        try { tp.damage = std::stoi(itDmg->second); } catch (...) { tp.damage = 0; }
    }
    return tp;
}

// 解析 tilesets[] 数组
static bool ParseTilesets(const json& tilesetsArr, CS2DMapData& out) {
    for (size_t ti = 0; ti < tilesetsArr.size(); ++ti) {
        const auto& tsj = tilesetsArr[ti];
        if (!tsj.contains("firstgid")) continue;
        TilesetDef ts;
        ts.firstGid   = tsj["firstgid"].safe_get<int>(1);
        ts.name       = tsj["name"].get_str();
        ts.imagePath  = tsj["image"].get_str();
        ts.tileWidth  = tsj["tilewidth"].safe_get<int>(32);
        ts.tileHeight = tsj["tileheight"].safe_get<int>(32);
        ts.tileCount  = tsj["tilecount"].safe_get<int>(0);
        ts.columns    = tsj["columns"].safe_get<int>(0);

        // 解析图块自定义属性 tiles[j] → properties
        if (tsj.contains("tiles") && tsj["tiles"].is_array()) {
            const auto& tilesArr = tsj["tiles"];
            for (size_t j = 0; j < tilesArr.size(); ++j) {
                const auto& tilej = tilesArr[j];
                if (!tilej.contains("id")) continue;
                int localId = tilej["id"].safe_get<int>(0);
                int gid = ts.firstGid + localId;
                std::unordered_map<std::string, std::string> props;
                if (tilej.contains("properties")) {
                    ParseProperties(tilej["properties"], props);
                }
                ts.tileProps[gid] = PropsToTile(props);
            }
        }
        out.tilesets.push_back(std::move(ts));
    }
    return true;
}

// 解析 tile layer
static bool ParseTileLayer(const json& layer, TileLayerDef& out) {
    out.name    = layer["name"].get_str();
    out.opacity = static_cast<float>(layer["opacity"].safe_get<double>(1.0));
    out.visible = layer["visible"].safe_get<bool>(true);
    out.width   = layer["width"].safe_get<int>(0);
    out.height  = layer["height"].safe_get<int>(0);

    if (!layer.contains("data") || !layer["data"].is_array()) {
        std::cerr << "Tile layer '" << out.name << "' has no data array" << std::endl;
        return false;
    }
    const auto& dataArr = layer["data"];
    for (size_t i = 0; i < dataArr.size(); ++i) {
        out.data.push_back(dataArr[i].safe_get<int>(0));
    }
    return true;
}

// 解析 object layer
static bool ParseObjectLayer(const json& layer, ObjectLayerDef& out) {
    out.name    = layer["name"].get_str();
    out.opacity = static_cast<float>(layer["opacity"].safe_get<double>(1.0));
    out.visible = layer["visible"].safe_get<bool>(true);

    if (!layer.contains("objects") || !layer["objects"].is_array()) return true;
    const auto& objs = layer["objects"];
    for (size_t i = 0; i < objs.size(); ++i) {
        const auto& obj = objs[i];
        ObjectDef od;
        od.type   = obj["type"].get_str();
        od.name   = obj["name"].get_str();
        od.x      = static_cast<float>(obj["x"].safe_get<double>(0.0));
        od.y      = static_cast<float>(obj["y"].safe_get<double>(0.0));
        od.width  = static_cast<float>(obj["width"].safe_get<double>(0.0));
        od.height = static_cast<float>(obj["height"].safe_get<double>(0.0));
        if (obj.contains("properties")) {
            ParseProperties(obj["properties"], od.properties);
        }
        out.objects.push_back(std::move(od));
    }
    return true;
}

// 解析 image layer
static bool ParseImageLayer(const json& layer, ImageLayerDef& out) {
    out.name       = layer["name"].get_str();
    out.opacity    = static_cast<float>(layer["opacity"].safe_get<double>(1.0));
    out.visible    = layer["visible"].safe_get<bool>(true);
    out.imagePath  = layer["image"].get_str();
    out.imageWidth  = layer["imagewidth"].safe_get<int>(0);
    out.imageHeight = layer["imageheight"].safe_get<int>(0);
    return true;
}

} // anonymous namespace

bool TMJParser::Parse(const std::string& resolvedPath, CS2DMapData& out) {
    // 将整个文件读入字符串
    std::ifstream ifs(resolvedPath);
    if (!ifs) {
        std::cerr << "[TMJParser] 无法打开 TMJ 文件: " << resolvedPath << std::endl;
        return false;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string jsonStr = buffer.str();

    MiniJSON::Value root;
    {
        MiniJSON::Parser parser(jsonStr);
        root = parser.parse();
        std::string err = parser.getError();
        if (!err.empty()) {
            std::cerr << "[TMJParser] JSON 解析失败: " << err << std::endl;
            return false;
        }
    }

    // 验证基本字段
    if (!root.contains("type") || root["type"].get_str() != "map") {
        std::cerr << "[TMJParser] 非 Tiled 地图 JSON: type != 'map'" << std::endl;
        return false;
    }

    out.mapWidth   = root["width"].safe_get<int>(0);
    out.mapHeight  = root["height"].safe_get<int>(0);
    out.tileWidth  = root["tilewidth"].safe_get<int>(32);
    out.tileHeight = root["tileheight"].safe_get<int>(32);
    out.infinite   = root["infinite"].safe_get<bool>(false);

    std::cout << "[TMJParser] 解析地图: " << out.mapWidth << "x" << out.mapHeight
              << ", tile " << out.tileWidth << "x" << out.tileHeight
              << (out.infinite ? ", infinite" : "") << std::endl;

    // tilesets
    if (root.contains("tilesets") && root["tilesets"].is_array()) {
        ParseTilesets(root["tilesets"], out);
    }
    std::cout << "[TMJParser] 解析了 " << out.tilesets.size() << " 个 tileset" << std::endl;

    // layers（递归进入 Tiled 的「组图层」——区域层就放在 site 组里）
    std::function<void(const MiniJSON::Value&, const std::string&)> parseLayers;
    parseLayers = [&](const MiniJSON::Value& arr, const std::string& groupName) {
        if (!arr.is_array()) return;
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto& layer = arr[i];
            const std::string layerType = layer["type"].get_str();

            if (layerType == "tilelayer") {
                TileLayerDef tl;
                if (ParseTileLayer(layer, tl)) {
                    std::cout << "[TMJParser]   瓦片层: '" << tl.name << "' "
                              << tl.width << "x" << tl.height << ", "
                              << tl.data.size() << " 个图块" << std::endl;
                    out.tileLayers.push_back(std::move(tl));
                }
            } else if (layerType == "imagelayer") {
                ImageLayerDef il;
                if (ParseImageLayer(layer, il)) {
                    std::cout << "[TMJParser]   图片层: '" << il.name << "' "
                              << il.imageWidth << "x" << il.imageHeight
                              << " " << il.imagePath << std::endl;
                    out.imageLayers.push_back(std::move(il));
                }
            } else if (layerType == "objectgroup") {
                ObjectLayerDef ol;
                if (ParseObjectLayer(layer, ol)) {
                    ol.group = groupName;
                    std::cout << "[TMJParser]   对象层: '"
                              << (groupName.empty() ? "" : groupName + "/") << ol.name
                              << "' " << ol.objects.size() << " 个对象" << std::endl;
                    out.objectLayers.push_back(std::move(ol));
                }
            } else if (layerType == "group") {
                // 组图层：递归处理子图层，把组名带下去
                if (layer.contains("layers")) {
                    const std::string childGroup =
                        groupName.empty() ? layer["name"].get_str()
                                          : groupName + "/" + layer["name"].get_str();
                    parseLayers(layer["layers"], childGroup);
                }
            }
        }
    };
    if (root.contains("layers")) {
        parseLayers(root["layers"], "");
    }

    std::cout << "[TMJParser] 共解析 " << out.tileLayers.size() << " 个瓦片层 + "
              << out.imageLayers.size() << " 个图片层 + "
              << out.objectLayers.size() << " 个对象层" << std::endl;
    return true;
}