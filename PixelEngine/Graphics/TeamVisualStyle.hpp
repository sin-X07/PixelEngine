#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>

// ============================================================================
// TeamVisualStyle — CT / T / 中立 的唯一阵营视觉映射（契约 v1 第 5 节 / PIX-16）
//
// 纪律：
//   - inline、无动态状态、不修改 SDL/ImGui 状态，任何线程/任何页面都能直接调用；
//   - CT 冷蓝 + 圆形徽标，T 暖橙 + 菱形徽标，None 灰 + 中性；形状与文本共同承担识别，
//     不能只靠颜色（色弱/低饱和地图下仍需可辨）；
//   - 业务阵营只从 Core/Types.hpp 的 Team 来，不用 spriteKey 猜；
//   - ImGui 与 SDL 共用这一份色值（GameUI 侧用 TeamAccentU32 转换）。
// ============================================================================

struct TeamVisualStyle {
    enum class BadgeShape { Circle, Diamond, Neutral };

    SDL_Color   accent{ 150, 160, 170, 255 };   // 主色（文字/描边/徽标）
    SDL_Color   accentSoft{ 90, 100, 112, 120 }; // 半透明填充（卡片底/徽标底）
    const char* label = "-";                    // 短标签：CT / T / -
    const char* shortName = "中立";              // 中文短名
    BadgeShape  badge = BadgeShape::Neutral;    // 徽标形状（形状即语义，不只靠色）
};

inline TeamVisualStyle GetTeamVisualStyle(Team team) {
    switch (team) {
        case Team::CT: {
            TeamVisualStyle s;
            s.accent     = SDL_Color{  88, 168, 245, 255 };   // 冷蓝
            s.accentSoft = SDL_Color{  88, 168, 245, 120 };
            s.label      = "CT";
            s.shortName  = "反恐精英";
            s.badge      = TeamVisualStyle::BadgeShape::Circle;
            return s;
        }
        case Team::T: {
            TeamVisualStyle s;
            s.accent     = SDL_Color{ 240, 160,  70, 255 };   // 暖橙
            s.accentSoft = SDL_Color{ 240, 160,  70, 120 };
            s.label      = "T";
            s.shortName  = "恐怖分子";
            s.badge      = TeamVisualStyle::BadgeShape::Diamond;
            return s;
        }
        default: break;
    }
    TeamVisualStyle s;
    s.accent     = SDL_Color{ 150, 160, 170, 255 };           // 中性灰
    s.accentSoft = SDL_Color{ 150, 160, 170, 110 };
    s.label      = "-";
    s.shortName  = "中立";
    s.badge      = TeamVisualStyle::BadgeShape::Neutral;
    return s;
}
