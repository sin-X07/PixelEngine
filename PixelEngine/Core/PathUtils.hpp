#pragma once
#include <string>
#include <filesystem>

namespace PathUtils {

// 进程级基准目录：构建产物 / 打包场景下 = 可执行文件所在目录。
// 由 main() 在启动时依据 argv[0] 设置；未设置时保持为空，
// 两个解析函数会自动退回到当前工作目录与源码根回退。
inline std::string& MutableBaseDir() {
    static std::string baseDir;
    return baseDir;
}

inline void SetBaseDir(const std::string& dir) {
    MutableBaseDir() = dir;
}

inline const std::string& BaseDir() {
    return MutableBaseDir();
}

// 源码根目录：本文件位于 <repo>/PixelEngine/Core/PathUtils.hpp，
// 向上三级即仓库根。仓库内的只读资源（assets/）相对仓库根定位。
inline std::string SourceRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().string();
}

// 解析"项目相对路径"（相对于仓库根，例如 "assets/maps/map1.txt"），用于【读取】。
// 依次探测：基准目录（exe 所在目录）→ 当前工作目录 → 源码根。
// 若都不存在，优先返回基准目录下的路径。
inline std::string ResolveAppPath(const std::string& rel) {
    const std::filesystem::path relative(rel);
    const std::string& base = MutableBaseDir();

    if (!base.empty()) {
        const std::filesystem::path p = std::filesystem::path(base) / relative;
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    {
        const std::filesystem::path p = std::filesystem::current_path() / relative;
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    {
        const std::filesystem::path p = std::filesystem::path(SourceRoot()) / relative;
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    if (!base.empty()) {
        return (std::filesystem::path(base) / relative).string();
    }
    return rel;
}

// 解析"项目相对路径"，用于【写入】（存档、生成的迷宫文件、文字配置等）。
// 始终落到可执行文件所在目录（未设置基准目录时落到当前工作目录），
// 不受其它位置同名文件的影响，保证落盘位置稳定；必要时创建父目录。
inline std::string ResolveWritablePath(const std::string& rel) {
    const std::filesystem::path relative(rel);
    const std::string& base = MutableBaseDir();

    std::filesystem::path root;
    if (!base.empty()) {
        root = std::filesystem::path(base);
    } else {
        root = std::filesystem::current_path();
    }

    std::filesystem::path p = root / relative;
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    return p.string();
}

// 兼容旧调用名的别名（读取语义）。
inline std::string ResolveProjectFile(const std::string& filename) {
    return ResolveAppPath(filename);
}

} // namespace PathUtils
