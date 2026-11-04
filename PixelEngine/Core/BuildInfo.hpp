#pragma once
// ============================================================================
// BuildInfo —— 构建标识（版本 / 渠道 / commit SHA）
//
// 数据来源：CMake 在配置阶段生成 <build>/gen/Version.hpp
//   -DPE_VERSION=0.3.0 -DPE_CHANNEL=release|beta|dev
//   commit SHA 取自 `git rev-parse --short HEAD`（无 git 时为 "unknown"）。
//
// 渠道只影响三件事：
//   1. 窗口标题 / 启动日志里显示的标识；
//   2. 存档文件名后缀（dev / beta 用 cs2d_save_dev.ini，避免开发档覆盖玩家档）；
//   3. 是否编入 dev 专用命令行参数（PE_DEV_TOOLS，仅 dev）。
// 不改变任何玩法逻辑，也不影响确定性哈希。
// ============================================================================

#include "Version.hpp"

#include <string>
#include <string_view>

namespace BuildInfo {

inline constexpr std::string_view kVersion = PE_VERSION_STRING;
inline constexpr std::string_view kChannel = PE_CHANNEL_STRING;
inline constexpr std::string_view kGitSha  = PE_GIT_SHA_STRING;

// 只有 dev 构建会带上调试命令行参数（--loadout / --killplayer / --record 等）
inline constexpr bool kDevTools = (PE_CHANNEL_STRING == std::string_view("dev"));

// "0.3.0-dev (65aff2f)"
inline std::string Display() {
    std::string s;
    s.reserve(kVersion.size() + kChannel.size() + kGitSha.size() + 4);
    s.append(kVersion);
    s.push_back('-');
    s.append(kChannel);
    s.append(" (");
    s.append(kGitSha);
    s.push_back(')');
    return s;
}

// 存档文件名后缀：dev / beta 与玩家档隔离；release 用原名
inline const char* SaveSuffix() {
    if (PE_CHANNEL_STRING == std::string_view("dev"))  return "_dev";
    if (PE_CHANNEL_STRING == std::string_view("beta")) return "_beta";
    return "";
}

}  // namespace BuildInfo
