#include "IniFile.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {

std::string Trim(const std::string& s) {
    size_t beg = 0;
    size_t end = s.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (beg < end && isSpace(s[beg])) ++beg;
    while (end > beg && isSpace(s[end - 1])) --end;
    return s.substr(beg, end - beg);
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 跳过 UTF-8 BOM (EF BB BF)
void SkipBOM(std::ifstream& f) {
    char bom[3];
    if (f.read(bom, 3) && bom[0] == '\xEF' && bom[1] == '\xBB' && bom[2] == '\xBF') return;
    f.clear();
    f.seekg(0);
}

} // anonymous namespace

IniFile IniFile::Load(const std::string& path) {
    IniFile ini;
    std::ifstream f(path);
    if (!f.is_open()) return ini;   // 文件缺失 → 空对象，静默

    SkipBOM(f);

    std::string line;
    std::string section;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // [section]
        if (line.front() == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        // key=value
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = Trim(line.substr(0, eq));
        if (key.empty()) continue;
        const std::string val = Trim(line.substr(eq + 1));

        ini.data_[section][key] = val;
    }

    ini.loaded_ = true;
    return ini;
}

const IniFile::Section& IniFile::GetSection(const std::string& section) const {
    static const Section kEmpty;
    auto it = data_.find(section);
    return (it == data_.end()) ? kEmpty : it->second;
}

std::string IniFile::GetString(const std::string& section, const std::string& key,
                               const std::string& def) const {
    auto sit = data_.find(section);
    if (sit == data_.end()) return def;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return def;
    return kit->second;
}

int IniFile::GetInt(const std::string& section, const std::string& key, int def) const {
    const std::string raw = GetString(section, key);
    if (raw.empty()) return def;
    try {
        size_t used = 0;
        const int v = std::stoi(raw, &used);
        // 整串都必须被消费掉，避免 "12abc" 被当成 12
        return (used == raw.size()) ? v : def;
    } catch (...) {
        return def;
    }
}

float IniFile::GetFloat(const std::string& section, const std::string& key, float def) const {
    const std::string raw = GetString(section, key);
    if (raw.empty()) return def;
    try {
        size_t used = 0;
        const float v = std::stof(raw, &used);
        return (used == raw.size()) ? v : def;
    } catch (...) {
        return def;
    }
}

bool IniFile::GetBool(const std::string& section, const std::string& key, bool def) const {
    const std::string raw = GetString(section, key);
    if (raw.empty()) return def;
    const std::string low = ToLower(raw);
    if (low == "1" || low == "true"  || low == "yes" || low == "on")  return true;
    if (low == "0" || low == "false" || low == "no"  || low == "off") return false;
    return def;
}

// ---- 写入 ----

void IniFile::Set(const std::string& section, const std::string& key,
                  const std::string& value) {
    data_[section][key] = value;
}

void IniFile::SetInt(const std::string& section, const std::string& key, int value) {
    data_[section][key] = std::to_string(value);
}

void IniFile::SetFloat(const std::string& section, const std::string& key, float value) {
    // 用 "%.6g" 避免 0.100000 之类的冗长尾巴
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    data_[section][key] = buf;
}

void IniFile::SetBool(const std::string& section, const std::string& key, bool value) {
    data_[section][key] = value ? "1" : "0";
}

bool IniFile::Save(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "; 由 PixelEngine 自动生成\n";
    for (const auto& [section, kvs] : data_) {
        f << "\n[" << section << "]\n";
        for (const auto& [key, value] : kvs) {
            f << key << "=" << value << "\n";
        }
    }
    return true;
}
