#include "TextConfig.hpp"
#include "../Core/PathUtils.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace {
    // 裁剪首尾空白与回车
    std::string Trim(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
        return s.substr(b, e - b);
    }
}

TextConfig::TextConfig() {
    // —— 游戏内 7 key（1.3.1 基线表逐字）——
    styles["weapon_status"] = TextStyle{0.0f, 549.0f, 18, 230, 238, 235, 255, 0, true};
    styles["no_weapon_hint"] = TextStyle{0.0f, 512.0f, 22, 245, 200, 90, 255, 1, false};
    styles["gameover_fail_title"] = TextStyle{355.0f, 216.0f, 30, 245, 80, 80, 255, 0, false};
    styles["gameover_fail_subtitle"] = TextStyle{305.0f, 256.0f, 18, 240, 140, 140, 255, 0, false};
    styles["gameover_end_title"] = TextStyle{334.0f, 216.0f, 30, 247, 211, 130, 255, 0, false};
    styles["gameover_result"] = TextStyle{285.0f, 280.0f, 24, 230, 238, 235, 255, 0, false};
    styles["gameover_hint"] = TextStyle{248.0f, 350.0f, 18, 134, 222, 190, 255, 0, false};

    // —— 菜单静态文字（1.3.2 基线表逐字 + 角色选择页 + 细粒度 key）——
    styles["menu_main_title"] = TextStyle{350.0f, 96.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_role_title"] = TextStyle{0.0f, 60.0f, 26, 255, 255, 255, 255, 0, false};
    styles["menu_history_title"] = TextStyle{0.0f, 120.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_history_mode1"] = TextStyle{60.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_history_mode2"] = TextStyle{460.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_controls_title"] = TextStyle{0.0f, 120.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_controls_lines"] = TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    // 操作说明 6 行独立 key（默认 x=80，y 自然流；可单独调字号/颜色/x）
    styles["menu_controls_line1"] = TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_controls_line2"] = TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_controls_line3"] = TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_controls_line4"] = TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_controls_line5"] = TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_controls_line6"] = TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    // 音量标签（设置页 3 个）
    styles["menu_settings_label_master"] = TextStyle{0.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_settings_label_music"] = TextStyle{0.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_settings_label_effects"] = TextStyle{0.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    // 历史记录动态列表行（字号/颜色，位置由布局决定）
    styles["menu_history_score_row"] = TextStyle{36.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_history_time_row"] = TextStyle{426.0f, 0.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_settings_title"] = TextStyle{0.0f, 120.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_exit_title"] = TextStyle{0.0f, 200.0f, 20, 255, 255, 255, 255, 0, false};
    styles["menu_exit_note"] = TextStyle{0.0f, 270.0f, 20, 255, 255, 255, 255, 0, false};

    globalFontPath = "C:/Windows/Fonts/msyh.ttc";
}

const TextStyle& TextConfig::Get(const std::string& key) const {
    auto it = styles.find(key);
    if (it != styles.end()) return it->second;
    static const TextStyle fallback;
    return fallback;
}

TextStyle& TextConfig::GetMutable(const std::string& key) {
    return styles[key];
}

std::vector<std::string> TextConfig::Keys() const {
    static const char* order[] = {
        "weapon_status", "no_weapon_hint",
        "gameover_fail_title", "gameover_fail_subtitle", "gameover_end_title",
        "gameover_result", "gameover_hint",
        "menu_main_title", "menu_role_title",
        "menu_history_title", "menu_history_mode1", "menu_history_mode2",
        "menu_history_score_row", "menu_history_time_row",
        "menu_controls_title", "menu_controls_lines",
        "menu_controls_line1", "menu_controls_line2", "menu_controls_line3",
        "menu_controls_line4", "menu_controls_line5", "menu_controls_line6",
        "menu_settings_title",
        "menu_settings_label_master", "menu_settings_label_music", "menu_settings_label_effects",
        "menu_exit_title", "menu_exit_note"
    };
    std::vector<std::string> result;
    for (const char* k : order) {
        if (styles.count(k)) result.push_back(k);
    }
    return result;
}

void TextConfig::ResetAll() {
    *this = TextConfig();
}

bool TextConfig::Load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        // 文件不存在：保持默认，不算错误（铁律 7 容错）
        return true;
    }

    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;

        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }

        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = Trim(t.substr(0, eq));
        const std::string v = Trim(t.substr(eq + 1));
        if (k.empty()) continue;

        if (section == "global") {
            if (k == "font") globalFontPath = v;
            continue;
        }

        if (section.empty()) continue;
        TextStyle& s = GetMutable(section);
        if (k == "x") s.x = std::stof(v);
        else if (k == "y") s.y = std::stof(v);
        else if (k == "size") s.size = std::stoi(v);
        else if (k == "r") s.r = static_cast<Uint8>(std::clamp(std::stoi(v), 0, 255));
        else if (k == "g") s.g = static_cast<Uint8>(std::clamp(std::stoi(v), 0, 255));
        else if (k == "b") s.b = static_cast<Uint8>(std::clamp(std::stoi(v), 0, 255));
        else if (k == "a") s.a = static_cast<Uint8>(std::clamp(std::stoi(v), 0, 255));
        else if (k == "alignX") s.alignX = std::stoi(v);
        else if (k == "anchored") s.anchored = (std::stoi(v) != 0);
        // 未知字段忽略
    }
    return true;
}

bool TextConfig::Save(const std::string& path) const {
    // 确保目录存在（assets/config 首次创建）
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(path);
    if (!out) {
        std::cerr << "文字配置保存失败: " << path << std::endl;
        return false;
    }

    out << "; PixelEngine 文字样式配置（由 TextConfig 自动生成）\n";
    out << "; 字号取档位 {16,18,20,22,24,30,34} 就近值；字体文件改动需重启生效。\n\n";
    out << "[global]\n";
    out << "font=" << globalFontPath << "\n\n";

    for (const std::string& key : Keys()) {
        const TextStyle& s = Get(key);
        out << "[" << key << "]\n";
        out << "x=" << s.x << "\n";
        out << "y=" << s.y << "\n";
        out << "size=" << s.size << "\n";
        out << "r=" << static_cast<int>(s.r) << "\n";
        out << "g=" << static_cast<int>(s.g) << "\n";
        out << "b=" << static_cast<int>(s.b) << "\n";
        out << "a=" << static_cast<int>(s.a) << "\n";
        out << "alignX=" << s.alignX << "\n";
        out << "anchored=" << (s.anchored ? 1 : 0) << "\n\n";
    }
    out.close();
    return true;
}
