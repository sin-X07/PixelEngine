#pragma once
#include <SDL2/SDL.h>
#include <map>
#include <string>
#include <vector>

// 一种文字的完整可调样式（ImGui 渲染）。
struct TextStyle {
    float x = 0.0f, y = 0.0f;   // 位置：DrawList 坐标（游戏内）或 SetCursorPosX/Y（菜单）
    int   size = 20;            // 字号目标点数，运行时经 GetFont(size) 就近取档 {16,18,20,22,24,30,34}
    Uint8 r = 255, g = 255, b = 255, a = 255;  // 颜色
    int   alignX = 0;           // 0=左对齐(x 生效)；1=水平居中(自动算 x)
    bool  anchored = false;     // true=位置由调用点锚点决定（面板中 X/Y 禁用）；weapon_status 用
};

// 文字样式配置：16 个 key 的样式表 + 全局字体路径；支持 ini 读写。
class TextConfig {
public:
    TextConfig();

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    const TextStyle& Get(const std::string& key) const;
    TextStyle& GetMutable(const std::string& key);

    // 按默认表声明顺序返回所有 key（先游戏内 7，后菜单 9）
    std::vector<std::string> Keys() const;

    void ResetAll();

    const std::string& GlobalFontPath() const { return globalFontPath; }
    void SetGlobalFontPath(const std::string& p) { globalFontPath = p; }

private:
    std::map<std::string, TextStyle> styles;
    std::string globalFontPath;
};
