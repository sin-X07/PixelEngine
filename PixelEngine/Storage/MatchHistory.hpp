#pragma once
// ============================================================================
// MatchHistory — 对局历史记录（本地纯文本，最多保留 50 场）
//
// 每行一条，字段用 '|' 分隔；只存 ASCII id，显示名由 UI 映射，
// 这样文件本身不会因编码问题损坏。
// ============================================================================

#include <string>
#include <vector>

struct MatchRecord {
    std::string time;        // 2026-10-23 19:20
    std::string modeId;      // bomb / elim / hostage / dm
    std::string mapId;       // ancient_ruins / dust_2 / cs_office
    int         scoreT = 0;
    int         scoreCT = 0;
    std::string result;      // win / lose / draw
    int         kills = 0;
    int         deaths = 0;
    int         rounds = 0;
    int         durationSec = 0;
};

namespace MatchHistory {

constexpr int  kMaxRecords = 50;
constexpr const char* kFileName = "cs2d_history.txt";

// 读取（文件不存在时返回空列表）
std::vector<MatchRecord> Load(const std::string& path);

// 追加一条并写回（保留最近 kMaxRecords 条）
void Append(const std::string& path, const MatchRecord& rec);

// 显示名映射（UI 与日志共用）
const char* ModeLabel(const std::string& modeId);
const char* MapLabel(const std::string& mapId);
const char* ResultLabel(const std::string& resultId);

} // namespace MatchHistory
