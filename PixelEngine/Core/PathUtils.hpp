#pragma once
#include <string>
#include <filesystem>
#include <cstdio>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace PathUtils {

// 二进制读入整个文件，返回是否成功。
// Windows 下用 UTF-8 → UTF-16 + _wfopen，因此安装路径含中文/空格也能打开
// （第三方库自带的 fopen(path) 在非 ASCII 路径下会失败，如 cgltf_parse_file）。
inline bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
#ifdef _WIN32
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath(static_cast<size_t>(wlen), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen) <= 0) return false;
    std::FILE* f = _wfopen(wpath.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;

    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long size = std::ftell(f);
    if (size < 0) { std::fclose(f); return false; }
    if (std::fseek(f, 0, SEEK_SET) != 0) { std::fclose(f); return false; }

    out.resize(static_cast<size_t>(size));
    const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) { out.clear(); return false; }
    return true;
}

// 读文本文件（UTF-8 原样返回，含 BOM；调用方按需处理）
inline bool ReadFileText(const std::string& path, std::string& out) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes)) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

// 文件是否存在（Windows 下同样支持非 ASCII 路径）
inline bool FileExists(const std::string& path) {
#ifdef _WIN32
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath(static_cast<size_t>(wlen), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen) <= 0) return false;
    std::FILE* f = _wfopen(wpath.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    std::fclose(f);
    return true;
}

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
