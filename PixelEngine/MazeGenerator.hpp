#pragma once
#include <random>
#include <string>

namespace MazeGenerator {
// 生成一个随机编织迷宫并写入文件
void GenerateRandomMazeFile(const std::string& filename, std::mt19937& gen);
}
