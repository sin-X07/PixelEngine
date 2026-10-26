#include "MatchHistory.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>

namespace MatchHistory {

namespace {

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

int ToInt(const std::string& s) {
    try { return std::stoi(s); } catch (...) { return 0; }
}

} // namespace

std::vector<MatchRecord> Load(const std::string& path) {
    std::vector<MatchRecord> out;
    std::ifstream in(path);
    if (!in.is_open()) return out;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto f = Split(line, '|');
        if (f.size() < 10) continue;

        MatchRecord r;
        r.time        = f[0];
        r.modeId      = f[1];
        r.mapId       = f[2];
        r.scoreT      = ToInt(f[3]);
        r.scoreCT     = ToInt(f[4]);
        r.result      = f[5];
        r.kills       = ToInt(f[6]);
        r.deaths      = ToInt(f[7]);
        r.rounds      = ToInt(f[8]);
        r.durationSec = ToInt(f[9]);
        out.push_back(std::move(r));
    }
    return out;
}

void Append(const std::string& path, const MatchRecord& rec) {
    // 读旧 + 追加 + 截断到上限后整体重写，保证文件不会无限增长
    auto all = Load(path);
    all.push_back(rec);
    if (static_cast<int>(all.size()) > kMaxRecords) {
        all.erase(all.begin(), all.begin() + (all.size() - kMaxRecords));
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;
    out << "# CS2D 对局历史（time|mode|map|scoreT|scoreCT|result|kills|deaths|rounds|seconds）\n";
    for (const auto& r : all) {
        out << r.time << '|' << r.modeId << '|' << r.mapId << '|'
            << r.scoreT << '|' << r.scoreCT << '|' << r.result << '|'
            << r.kills << '|' << r.deaths << '|' << r.rounds << '|'
            << r.durationSec << '\n';
    }
}

const char* ModeLabel(const std::string& modeId) {
    if (modeId == "bomb")    return "爆破";
    if (modeId == "elim")    return "团队歼灭";
    if (modeId == "hostage") return "人质营救";
    if (modeId == "dm")      return "死斗";
    return "未知";
}

const char* MapLabel(const std::string& mapId) {
    if (mapId == "ancient_ruins") return "古代遗迹";
    if (mapId == "dust_2")        return "沙漠Ⅱ";
    if (mapId == "mirage")        return "荒漠迷城";
    if (mapId == "cs_office")     return "办公室";
    return mapId.empty() ? "未知" : mapId.c_str();
}

const char* ResultLabel(const std::string& resultId) {
    if (resultId == "win")  return "胜利";
    if (resultId == "lose") return "失败";
    if (resultId == "draw") return "平局";
    return "未完成";
}

} // namespace MatchHistory
