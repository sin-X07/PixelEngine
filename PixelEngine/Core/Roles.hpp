#pragma once
#include <SDL2/SDL.h>
#include <array>

// 角色定义与选择页布局（纯数据，header-only）
// 贴图路径与 RenderSystem::Initialize 中 loadAsset 约定一致（相对运行目录的项目根相对路径）。
struct RoleDef {
    const char* name;       // 角色名（选择页/按钮文案）
    const char* texturePath;// 贴图相对路径
    const char* tagline;    // 一句话简介
};

inline const std::array<RoleDef, 4>& GetRoles() {
    static const std::array<RoleDef, 4> roles = {{
        {"豆丁",   "assets/images/role/player.png",                            "老牌小小勇者，哪里都能钻"},
        {"莉娅",   "assets/images/role/character_femaleAdventurer_side.png",   "敏捷的探险家，走位风骚"},
        {"阿凯",   "assets/images/role/character_maleAdventurer_duck.png",     "绿衣少年，元气满满"},
        {"老约翰", "assets/images/role/character_malePerson_side.png",         "沉稳大叔，见多识广"},
    }};
    return roles;
}

// 角色数量便捷常量
inline constexpr int ROLE_COUNT = 4;

// 选择页卡片矩形（接受动态视口宽度；默认 800 用于兼容）
inline SDL_FRect GetRoleCardRect(int index, float viewW = 800.0f) {
    constexpr float cardW = 170.0f;
    constexpr float cardH = 300.0f;
    constexpr float gap = 14.0f;
    constexpr float totalW = cardW * ROLE_COUNT + gap * (ROLE_COUNT - 1);
    const float startX = (viewW - totalW) * 0.5f;
    return SDL_FRect{startX + index * (cardW + gap), 140.0f, cardW, cardH};
}
