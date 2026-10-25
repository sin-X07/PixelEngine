#pragma once
#include <SDL2/SDL.h>
#include <string>

// ============================================================================
// 全局枚举与轻量类型（CS2D 单机版）
// ============================================================================

// 游戏状态机
enum GameState {
    MAIN_MENU,      // 主菜单
    MAP_SELECT,     // 选择地图（爆破模式）
    HISTORY,        // 历史记录
    SIDE_SELECT,    // 选择阵营（爆破模式）
    SETTINGS,       // 设置
    PLAYING,        // 对局中
    PAUSED,         // 暂停
    EXIT_CONFIRM    // 退出确认
};

// 阵营
enum class Team {
    None,   // 未分配（死斗模式 / 观战）
    T,      // 恐怖分子
    CT      // 反恐精英
};

// 阵营名（日志 / UI 用）
inline const char* TeamName(Team t) {
    switch (t) {
        case Team::T:  return "T";
        case Team::CT: return "CT";
        default:       return "-";
    }
}

// 回合阶段（Phase 2 起由 RoundManager 驱动，这里先定义供全局引用）
enum class RoundPhase {
    Warmup,     // 热身 / 等待
    Freeze,     // 冻结（不可移动）
    Buy,        // 购买期
    Live,       // 交战期
    RoundEnd    // 回合结算
};
