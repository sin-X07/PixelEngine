#pragma once
// ============================================================================
// TacticsTable —— 每张地图的战术表（数据驱动，替代散落在代码里的地图知识）
//
// 目的：把"这张图上，A 点是哪个区域、Long 叫什么、默认怎么分兵"从代码里搬出来。
// 新图接入 = 新增一张 assets/config/tactics/<mapId>.ini，不改 C++。
//
// 文件格式（INI，复用 IniFile）：
//   [areas]
//   A_Site = A_Site          ; 教练代码里的关键词 → 地图 TMJ 的实际区域名
//   A_Long = Long
//   ...
//
//   [slots.<Team>.<Intent>]  ; Team = T | CT；Intent = Default | RushA | RushB ...
//   0 = Entry|A_Long         ; 槽位序号 = 角色|区域别名（别名走 [areas] 解析）
//   1 = Support|A_Site
//   ...
//
// 缺失时完全回退到代码里的旧逻辑，因此战术表是"可选增强"，不是硬依赖。
// ============================================================================

#include <array>
#include <map>
#include <string>
#include <vector>

#include "../../Core/IniFile.hpp"
#include "../Coach/CoachTypes.hpp"

struct TacticsTable {
    bool loaded = false;
    std::string mapId;

    // 关键词（A_Site / Long / ...）→ 地图实际区域名
    std::map<std::string, std::string> areas;

    // (Team, Intent) → 5 个槽位
    std::map<std::string, std::array<SlotOrder, 5>> slots;

    // path 由调用方用 PathUtils 解析（本头文件不引入 windows.h，避免 near/far 宏污染）
    void Load(const std::string& path, const std::string& id) {
        loaded = false;
        mapId  = id;
        areas.clear();
        slots.clear();
        if (id.empty()) return;

        IniFile ini = IniFile::Load(path);
        if (!ini.IsLoaded()) return;   // 没表 = 用代码默认，正常情况

        for (const auto& [key, value] : ini.GetSection("areas")) {
            const std::string clean = StripInlineComment(value);
            if (!clean.empty()) areas[key] = clean;
        }

        static const char* kTeams[]   = {"T", "CT"};
        static const char* kIntents[] = {
            "Default", "RushA", "RushB", "SplitA", "SplitB",
            "StackA", "StackB", "RetakeA", "RetakeB", "Save", "Fake"
        };
        for (const char* team : kTeams) {
            for (const char* intent : kIntents) {
                const std::string section = std::string("slots.") + team + "." + intent;
                const auto& sec = ini.GetSection(section);
                if (sec.empty()) continue;
                std::array<SlotOrder, 5> arr;
                bool any = false;
                for (int i = 0; i < 5; ++i) {
                    auto it = sec.find(std::to_string(i));
                    if (it == sec.end()) continue;
                    if (ParseSlot(StripInlineComment(it->second), arr[static_cast<size_t>(i)])) any = true;
                }
                if (any) slots[std::string(team) + "." + intent] = arr;
            }
        }
        loaded = true;
    }

    // 关键词解析：优先用表，其次让调用方回退到旧的关键字扫描
    bool Area(const std::string& key, std::string& out) const {
        auto it = areas.find(key);
        if (it == areas.end()) return false;
        // 表里允许写空值表示"这张图没有这个区域"
        if (it->second.empty()) return false;
        out = it->second;
        return true;
    }

    const std::array<SlotOrder, 5>* Slots(const std::string& team,
                                          const std::string& intent) const {
        auto it = slots.find(team + "." + intent);
        return it == slots.end() ? nullptr : &it->second;
    }

    // 把区域别名解析成实际区域名；不是别名就原样返回
    std::string ResolveArea(const std::string& alias) const {
        auto it = areas.find(alias);
        return it == areas.end() ? alias : it->second;
    }

private:
    // 去掉行尾注释：`Long = A_Site   # 说明` → `A_Site`
    static std::string StripInlineComment(const std::string& s) {
        size_t cut = std::string::npos;
        for (size_t i = 0; i < s.size(); ++i) {
            if ((s[i] == '#' || s[i] == ';') &&
                (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) { cut = i; break; }
        }
        std::string out = (cut == std::string::npos) ? s : s.substr(0, cut);
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
        size_t pos = 0;
        while (pos < out.size() && (out[pos] == ' ' || out[pos] == '\t')) ++pos;
        return out.substr(pos);
    }

    static bool ParseSlot(const std::string& text, SlotOrder& out) {
        // 格式：Role|AreaAlias[|noTrade][|rotate]
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos <= text.size()) {
            const size_t bar = text.find('|', pos);
            parts.push_back(text.substr(pos, bar == std::string::npos
                                                 ? std::string::npos : bar - pos));
            if (bar == std::string::npos) break;
            pos = bar + 1;
        }
        if (parts.empty() || parts[0].empty()) return false;

        const std::string& role = parts[0];
        if (role == "Entry")       out.role = CoachRole::Entry;
        else if (role == "Support")out.role = CoachRole::Support;
        else if (role == "Lurk")   out.role = CoachRole::Lurk;
        else if (role == "Anchor") out.role = CoachRole::Anchor;
        else if (role == "Awper")  out.role = CoachRole::Awper;
        else return false;

        out.area     = parts.size() > 1 ? parts[1] : std::string();
        out.canTrade = true;
        out.canRotate = false;
        for (size_t i = 2; i < parts.size(); ++i) {
            if (parts[i] == "noTrade") out.canTrade = false;
            if (parts[i] == "rotate")  out.canRotate = true;
        }
        return true;
    }
};
