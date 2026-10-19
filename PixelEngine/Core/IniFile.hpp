#pragma once
// ============================================================================
// IniFile — 极简 INI 解析器（无第三方依赖）
//
// 支持:
//   - [section] 分节
//   - key=value 键值对
//   - ';' 与 '#' 两种注释
//   - UTF-8 BOM 自动跳过
//   - 首尾空白自动裁剪
//
// 设计: 解析结果存为嵌套 map，调用方按 section/key 取值。
//       所有取值函数都带默认值，文件缺失或键缺失时静默返回默认值。
// ============================================================================

#include <string>
#include <map>
#include <vector>

class IniFile {
public:
    using Section = std::map<std::string, std::string>;
    using Data    = std::map<std::string, Section>;

    // 解析文件。文件不存在时返回空对象（不抛异常、不打印错误）。
    static IniFile Load(const std::string& path);

    // 单节读取：返回该节的键值表（不存在时返回空 map 的引用）
    const Section& GetSection(const std::string& section) const;

    // 取值（带默认值；键不存在或无法转换时返回 def）
    std::string GetString(const std::string& section, const std::string& key,
                          const std::string& def = std::string()) const;
    int         GetInt   (const std::string& section, const std::string& key, int def = 0) const;
    float       GetFloat (const std::string& section, const std::string& key, float def = 0.0f) const;
    // 布尔：接受 1/0, true/false, yes/no, on/off（大小写不敏感）
    bool        GetBool  (const std::string& section, const std::string& key, bool def = false) const;

    // 是否成功读到了文件内容
    bool IsLoaded() const { return loaded_; }

private:
    Data data_;
    bool loaded_ = false;
};
