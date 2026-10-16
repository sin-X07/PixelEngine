#pragma once
#include "CS2DTypes.hpp"
#include <string>

// ============================================================================
// TMJ 解析器 —— 读取 Tiled 导出的 JSON 地图文件（嵌入 tileset）
// ============================================================================
namespace TMJParser {

// 解析 TMJ 文件，填充 CS2DMapData
// 成功返回 true，失败返回 false 且在 stderr 打印错误信息
bool Parse(const std::string& resolvedPath, CS2DMapData& out);

} // namespace TMJParser