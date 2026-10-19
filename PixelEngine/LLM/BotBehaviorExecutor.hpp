#pragma once
// ============================================================================
// BotBehaviorExecutor — 分层架构的"战术层"
//
// 职责: 将 BotIntent (战略目标) 转换为每帧的 BehaviorOutput (具体动作)。
//
// 分层:
//   Layer 3: LLM (Engine::RequestLLMDecision) → BotIntent 每 N ms
//   Layer 2: BotBehaviorExecutor (本类)        → BehaviorOutput 每帧
//   Layer 1: CS2DGame::UpdateBOTs              → MoveWithCollision + FireBullet
//
// 设计:
//   - 每个 BOT 一个 executor 实例 (保有传统AI的aiTimer)
//   - 无 LLM 意图时自动降级为传统随机游走+索敌射击
//   - 有 LLM 意图时按 action 类型调整移动/瞄准/射击参数
//   - 射击精度/散布乘数由 CS2DGame 消费, 本类只输出建议乘数
// ============================================================================

#include "BotIntent.hpp"
#include <random>

class BotBehaviorExecutor {
public:
    BotBehaviorExecutor() = default;

    // 每帧调用一次。参数已由 CS2DGame 计算好, 本类只负责决策。
    //
    // intent       : 该 BOT 当前的 LLM 意图 (可能 fresh=false 表示回退)
    // botX/Y       : BOT 当前世界坐标
    // playerX/Y    : 玩家当前世界坐标
    // canSeePlayer : BOT 视线是否可见玩家
    // distToPlayer : 距离玩家 (像素)
    // hasAmmo      : BOT 弹药是否 >0
    // isReloading  : 是否在换弹中
    // weaponSlot   : 0=主武器, 1=副武器, 2=近战
    // rng          : 共享随机引擎 (传统回退用)
    // dt           : 帧时间
    BehaviorOutput Execute(
        const BotIntent& intent,
        float botX, float botY,
        float playerX, float playerY,
        bool canSeePlayer, float distToPlayer,
        bool hasAmmo, bool isReloading, int weaponSlot,
        std::mt19937& rng,
        float dt);

private:
    // 传统 AI 回退 — 随机游走 + 发现玩家后射击
    BehaviorOutput TraditionalFallback(
        float botX, float botY,
        float playerX, float playerY,
        bool canSeePlayer,
        std::mt19937& rng,
        float dt);

    float aiTimer_ = 0.0f;      // 传统 AI 方向切换倒计时
    float currentVx_ = 0.0f;
    float currentVy_ = 0.0f;
};