#pragma once
#include <string>
#include <filesystem>

namespace PathUtils {
inline std::string ResolveProjectFile(const std::string& filename) {
    const std::filesystem::path current = std::filesystem::current_path() / filename;
    if (std::filesystem::exists(current)) {
        return current.string();
    }

    // 从当前文件所在源码目录回退一级探测项目根目录
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path projectPath = sourceRoot / filename;
    if (std::filesystem::exists(projectPath)) {
        return projectPath.string();
    }

    return filename;
}
}
