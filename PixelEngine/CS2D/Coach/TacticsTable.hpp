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
#include "../Tuning.hpp"
#include <cmath>
#include <map>
#include <string>
#include <cstdlib>
#include <vector>

#include "../../Core/IniFile.hpp"
#include "../Coach/CoachTypes.hpp"

struct TacticsTable {
    struct Perception {
        float radius, angle, reaction, settle;
    };
    std::array<Perception, 4> perception = {{
        {Tuning::BotFovRadiusMul[0], Tuning::BotFovAngleMul[0], Tuning::BotReactionSecs[0], Tuning::AimSettleSecs[0]},
        {Tuning::BotFovRadiusMul[1], Tuning::BotFovAngleMul[1], Tuning::BotReactionSecs[1], Tuning::AimSettleSecs[1]},
        {Tuning::BotFovRadiusMul[2], Tuning::BotFovAngleMul[2], Tuning::BotReactionSecs[2], Tuning::AimSettleSecs[2]},
        {Tuning::BotFovRadiusMul[3], Tuning::BotFovAngleMul[3], Tuning::BotReactionSecs[3], Tuning::AimSettleSecs[3]}
    }};
    int botProfile = 4;
    bool loaded = false;
    std::string mapId;

    // 关键词（A_Site / Long / ...）→ 地图实际区域名
    std::map<std::string, std::string> areas;

    // 区域 → 守点时应该看的方向（弧度，0=+X）；来自 [look.<area>] 0=角度(度)
    // 也支持 [look.T.<area>] / [look.CT.<area>]（按阵营区分）
    std::map<std::string, std::vector<float>> looks;

    // 区域 → 驻守锚点（优先于区域中心）；[anchor.<area>] x=.. y=..
    // 也支持 [anchor.T.<area>] / [anchor.CT.<area>]
    std::map<std::string, std::pair<float, float>> anchors;

    // (Team, Intent) → 5 个槽位
    std::map<std::string, std::array<SlotOrder, 5>> slots;

    // path 由调用方用 PathUtils 解析（本头文件不引入 windows.h，避免 near/far 宏污染）
    void Load(const std::string& path, const std::string& id) {
        loaded = false;
        botProfile = 4;
        mapId  = id;
        areas.clear();
        slots.clear();
        looks.clear();
        anchors.clear();
        for (int d = 0; d < 4; ++d)
            perception[d] = {Tuning::BotFovRadiusMul[d], Tuning::BotFovAngleMul[d], Tuning::BotReactionSecs[d], Tuning::AimSettleSecs[d]};
        if (id.empty()) return;

        IniFile ini = IniFile::Load(path);
        if (!ini.IsLoaded()) return;   // 没表 = 用代码默认，正常情况

        botProfile = std::clamp(ini.GetInt("perception", "Profile", botProfile), 0, 7);
        for (int d = 0; d < 4; ++d) {
            const std::string section = "perception." + std::to_string(d);
            auto positive = [&](const char* key, float fallback) {
                const float value = ini.GetFloat(section, key, fallback);
                return std::isfinite(value) && value > 0.0f ? value : fallback;
            };
            auto& p = perception[d];
            p.radius = positive("FovRadiusMul", p.radius);
            p.angle = positive("FovAngleMul", p.angle);
            p.reaction = positive("ReactionSecs", p.reaction);
            p.settle = positive("AimSettleSecs", p.settle);
        }
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
        // [look.<区域>]  0 = 角度(度)，可写多条（守点时轮流看）
        for (const auto& [section, sec] : ini.GetAll()) {
            if (section.rfind("look.", 0) == 0) {
                const std::string area = section.substr(5);
                std::vector<float> dirs;
                for (const auto& [key, val] : sec) {
                    (void)key;
                    const float deg = std::atof(StripInlineComment(val).c_str());
                    dirs.push_back(deg * 3.14159265f / 180.0f);
                }
                if (!dirs.empty()) looks[area] = dirs;
            } else if (section.rfind("anchor.", 0) == 0) {
                const std::string area = section.substr(7);
                float x = 0.0f, y = 0.0f;
                for (const auto& [key, val] : sec) {
                    const std::string clean = StripInlineComment(val);
                    if (key == "x") x = static_cast<float>(std::atof(clean.c_str()));
                    else if (key == "y") y = static_cast<float>(std::atof(clean.c_str()));
                }
                if (x > 0.0f || y > 0.0f) anchors[area] = {x, y};
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

    const std::pair<float, float>* Anchor(const std::string& area) const {
        auto it = anchors.find(area);
        return it == anchors.end() ? nullptr : &it->second;
    }

    const std::vector<float>* Look(const std::string& area) const {
        auto it = looks.find(area);
        return it == looks.end() ? nullptr : &it->second;
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
            const std::string& p = parts[i];
            if (p == "noTrade") { out.canTrade = false; continue; }
            if (p == "rotate")  { out.canRotate = true; continue; }
            // key=value 扩展：look=<Area>|lookDeg=<度>|hold=anchor|offangle|passive|util=smoke:Area
            const size_t eq = p.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = p.substr(0, eq);
            const std::string val = p.substr(eq + 1);
            if (key == "look") {
                out.lookArea = val;
            } else if (key == "lookDeg") {
                out.lookAngle = static_cast<float>(std::atof(val.c_str())) * 3.14159265f / 180.0f;
            } else if (key == "hold") {
                if (val == "anchor")        out.hold = HoldStyle::Anchor;
                else if (val == "offangle") out.hold = HoldStyle::OffAngle;
                else if (val == "passive")  out.hold = HoldStyle::Passive;
            } else if (key == "util") {
                const size_t colon = val.find(':');
                const std::string kind = val.substr(0, colon);
                if (colon != std::string::npos) out.utilArea = val.substr(colon + 1);
                if (kind == "smoke")        out.util = 1;
                else if (kind == "flash")   out.util = 2;
                else if (kind == "molotov") out.util = 3;
                else if (kind == "he")      out.util = 4;
            }
        }
        return true;
    }
};
