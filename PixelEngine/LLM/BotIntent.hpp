#pragma once
// ============================================================================
// BotIntent — 每个 BOT 的 LLM 战略意图 (行为树的目标输入)
//
// 分层架构:
//   Layer 3: LLM 每 N 秒输出 BotIntent (战略)
//   Layer 2: BotBehaviorExecutor 每帧将 Intent 转为具体动作 (战术)
//   Layer 1: CS2DGame 物理/碰撞/射击 (执行)
//
// BotIntent 只是一个 "目标描述", 不包含具体移动/射击指令.
// 具体执行由 BotBehaviorExecutor 根据当前战场态势完成.
// ============================================================================

#include <string>
#include <cmath>

// ---- BOT 意图 (LLM → 行为层的接口) ----
struct BotIntent {
    enum class Action {
        None,           // 无 LLM 指令, 使用纯传统 AI
        Aggressive,     // "进攻" → 朝敌人移动 + 积极开火
        Defensive,      // "防守" → 据守当前位置 + 谨慎射击
        Retreat,        // "撤退" → 远离敌人 + 不/少开火
        MoveToCover     // "掩体移动" → 向掩体移动
    };

    enum class ShootDiscipline {
        Auto,           // 使用传统 AI 的射速/散布 (默认)
        Burst,          // "点射" → 降低射速, 减小散布
        HoldFire         // "停止射击" → 完全不攻击
    };

    Action          action       = Action::None;
    ShootDiscipline shootMode    = ShootDiscipline::Auto;
    float           targetX      = 0.0f;   // 目标世界坐标 (移动/防守朝向)
    float           targetY      = 0.0f;
    bool            fresh        = false;  // 是否有有效 LLM 决策
    float           ageSec       = 0.0f;   // 距离上次 LLM 更新已过秒数

    // teammateX/Y: 最近存活队友坐标 (由调用方提供, hasTeammate=false 时忽略)

    // 每帧老化
    void Tick(float dt) {
        ageSec += dt;
        if (ageSec > 10.0f) fresh = false;  // 超过 10 秒未更新 → 标记过期
    }

    // 重置为纯传统 AI
    void Reset() {
        action = Action::None;
        shootMode = ShootDiscipline::Auto;
        fresh = false;
        ageSec = 0.0f;
    }
};

// ---- 行为执行器输出 (每帧) ----
struct BehaviorOutput {
    float vx = 0.0f, vy = 0.0f;     // 期望速度
    float aimAngle = 0.0f;           // 期望瞄准角度
    bool  shouldShoot = false;       // 是否扣扳机
    float shootCooldownMul = 1.0f;   // 射击冷却乘数 (>1 = 更慢, <1 = 更快)
    float spreadMul = 1.0f;          // 散布乘数
};