#pragma once
// ============================================================================
// 教练层数据契约（L3 主教练 ←→ L2 助教 ←→ L1 队员决策树）
//
// 设计边界：
//   - LLM 只输出「战术意图 + 角色 + 区域」，绝不输出坐标 / 瞄准 / 是否开火
//   - 具体怎么走、怎么打，永远由决策树（A*、避障、掩体、开火）决定
// ============================================================================

#include <array>
#include <string>
#include <vector>
#include "../../Core/Types.hpp"

// 战术意图（白名单，LLM 只能从这里选）
enum class PlanIntent {
    Default = 0,   // 默认分兵
    RushA, RushB,  // 强打
    SplitA, SplitB,// 分路夹击
    StackA, StackB,// 重兵堆点
    RetakeA, RetakeB, // 回防（包已下）
    Save,          // 保枪
    FakeThenOther  // 假打后转点
};

// 节奏
enum class Tempo { Slow = 0, Normal, Fast };

// 队员定位（影响移速倍率、补枪资格、远追许可）
enum class CoachRole { Entry = 0, Support, Lurk, Anchor, Awper };

struct SlotOrder {
    CoachRole   role  = CoachRole::Support;
    std::string area;              // 目标区域名（来自地图 areas 层），空=不指定
    bool        canTrade = true;   // 允许补枪
    bool        canRotate = false; // 允许自行换点
};

// 一份完整的队伍计划
struct TeamPlan {
    PlanIntent  intent = PlanIntent::Default;
    std::string target;            // "A" / "B" / "MID" / 空
    Tempo       tempo  = Tempo::Normal;
    std::array<SlotOrder, 5> slots;
    std::string reason;            // 只用于 HUD / 日志
    int         gen = 0;           // 代际号：旧回包直接丢弃
    float       createdAt = 0.0f;
    bool        valid = false;
    float       expiresAt = 0.0f;   // 计划失效时刻（clockSec_ 基准）

    // 条件分支：到点后自动切换（例如「前 20 秒卡点，20 秒后提速转点」）
    bool        hasBranch = false;
    float       branchAfterSec = 0.0f;
    PlanIntent  branchIntent = PlanIntent::Default;
    std::string branchTarget;
    Tempo       branchTempo = Tempo::Normal;
};

// 节奏 → 激进系数（fast 更主动、slow 更保守）
inline float TempoAggression(Tempo t) {
    switch (t) {
        case Tempo::Fast: return 1.25f;
        case Tempo::Slow: return 0.80f;
        default:          return 1.00f;
    }
}

// 一回合的比赛记忆（保留最近 10 回合）
struct RoundMemo {
    struct Death {
        Team        team = Team::None;
        std::string area;          // 阵亡所在区域
        float       x = 0.0f, y = 0.0f;
        float       atSec = 0.0f;  // 回合内时刻
    };

    int         roundNo = 0;
    PlanIntent  plan = PlanIntent::Default;
    std::string target;
    bool        won = false;
    int         scoreT = 0, scoreCT = 0;
    int         aliveEnd = 0;
    bool        bombPlanted = false;
    std::string plantSite;
    std::string firstContactArea;
    std::vector<Death> deaths;
};

// 触发教练思考的事件
enum class CoachEventType {
    RoundStart = 0,   // 回合开始（必发，出整回合计划）
    EnemySpotted,     // 首次接火
    AllyDown,         // 己方减员
    EnemyDown,        // 对方减员
    BombPlanted,      // C4 已安装
    DefuseStarted,    // 有人开始拆包
    DefuseAborted,    // 拆包中断
    TimeLow,          // 时间不足
    EconomyLow,       // 经济不足
    HumanDied,        // 人类玩家阵亡
    MatchPoint,       // 赛点
    Heartbeat         // 低频例行检查（6 秒无事件时）
};

struct CoachEvent {
    CoachEventType type = CoachEventType::RoundStart;
    float          priority = 0.0f;  // 越大越优先
    float          atSec = 0.0f;     // 入队时刻
    std::string    area;             // 相关区域
};
