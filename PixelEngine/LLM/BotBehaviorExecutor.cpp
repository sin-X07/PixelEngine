#include "BotBehaviorExecutor.hpp"
#include <cmath>
#include <algorithm>

BehaviorOutput BotBehaviorExecutor::Execute(
    const BotIntent& intent,
    float botX, float botY,
    float playerX, float playerY,
    bool canSeePlayer, float distToPlayer,
    bool hasAmmo, bool isReloading, int weaponSlot,
    std::mt19937& rng,
    float dt)
{
    // ---- 无 LLM 意图 → 纯传统 AI ----
    if (!intent.fresh || intent.action == BotIntent::Action::None) {
        return TraditionalFallback(botX, botY, playerX, playerY, canSeePlayer, rng, dt);
    }

    // ---- LLM 驱动层 ----
    BehaviorOutput out{};
    const float dx = intent.targetX - botX;
    const float dy = intent.targetY - botY;
    const float toTargetDist = std::sqrt(dx * dx + dy * dy);

    // 计算敌人方向 (用于瞄准)
    const float pdx = playerX - botX;
    const float pdy = playerY - botY;

    // ================================================================
    // 1. 移动策略 — 根据 action 类型
    // ================================================================
    switch (intent.action) {
        case BotIntent::Action::Aggressive: {
            // 朝玩家移动, 保持一定距离 (不要太近, 否则被反击)
            const float desiredDist = 80.0f; // 理想交战距离
            if (distToPlayer > desiredDist + 40.0f) {
                // 太远 → 推进
                out.vx = (pdx / distToPlayer) * 55.0f;
                out.vy = (pdy / distToPlayer) * 55.0f;
            } else if (distToPlayer < desiredDist - 30.0f) {
                // 太近 → 略微后退 (横向移动, 不直退)
                out.vx = (-pdx / distToPlayer + (canSeePlayer ? 0.5f : -0.5f)) * 30.0f;
                out.vy = (-pdy / distToPlayer + (canSeePlayer ? -0.5f : 0.5f)) * 30.0f;
            } else {
                // 理想距离 → 横向移动 (strafing), 更难被命中
                out.vx = -pdy / distToPlayer * 40.0f;
                out.vy =  pdx / distToPlayer * 40.0f;
            }
            break;
        }

        case BotIntent::Action::Defensive: {
            // 据守当前位置, 轻微调整位置
            if (toTargetDist > 10.0f) {
                const float spd = std::min(toTargetDist * 0.5f, 30.0f);
                out.vx = dx / toTargetDist * spd;
                out.vy = dy / toTargetDist * spd;
            } else {
                // 到达 → 横向小幅度移动, 避免站桩
                out.vx = (canSeePlayer ? -pdy : pdy) / std::max(distToPlayer, 1.0f) * 15.0f;
                out.vy = (canSeePlayer ? pdx : -pdx) / std::max(distToPlayer, 1.0f) * 15.0f;
            }
            break;
        }

        case BotIntent::Action::Retreat: {
            // 朝目标点 (远离玩家方向) 全速移动
            if (toTargetDist > 5.0f) {
                const float spd = 60.0f;
                out.vx = dx / toTargetDist * spd;
                out.vy = dy / toTargetDist * spd;
            } else {
                out.vx = 0.0f; out.vy = 0.0f; // 到达撤退点
            }
            break;
        }

        case BotIntent::Action::MoveToCover: {
            // 朝掩体移动, 适当减速到达
            if (toTargetDist > 10.0f) {
                const float spd = std::min(toTargetDist * 0.4f, 50.0f);
                out.vx = dx / toTargetDist * spd;
                out.vy = dy / toTargetDist * spd;
            } else {
                out.vx = 0.0f; out.vy = 0.0f;
            }
            break;
        }

        default: break;
    }

    // ================================================================
    // 2. 瞄准 — 始终对准玩家如果可见
    // ================================================================
    if (canSeePlayer) {
        out.aimAngle = std::atan2(pdy, pdx);
    } else if (intent.action == BotIntent::Action::Retreat ||
               intent.action == BotIntent::Action::MoveToCover) {
        // 移动中 → 面朝移动方向
        if (std::abs(out.vx) > 1.0f || std::abs(out.vy) > 1.0f) {
            out.aimAngle = std::atan2(out.vy, out.vx);
        }
    }

    // ================================================================
    // 3. 射击策略 — 结合 LLM shootMode 和战场态势
    // ================================================================
    out.shouldShoot = canSeePlayer && hasAmmo && !isReloading;

    switch (intent.shootMode) {
        case BotIntent::ShootDiscipline::Burst:
            // 点射: 降低射速, 减小散布 (提高命中率)
            out.shootCooldownMul = 2.5f;    // 原射速 / 2.5
            out.spreadMul = 0.4f;           // 散布减至 40%
            break;

        case BotIntent::ShootDiscipline::HoldFire:
            out.shouldShoot = false;
            break;

        case BotIntent::ShootDiscipline::Auto:
        default: {
            // "连射"或默认: 标准射速, 距离越近散布越小
            if (distToPlayer < 60.0f) {
                out.shootCooldownMul = 0.8f;   // 近距离: 更快
                out.spreadMul = 0.7f;
            } else if (distToPlayer < 150.0f) {
                out.shootCooldownMul = 1.0f;
                out.spreadMul = 1.0f;
            } else {
                out.shootCooldownMul = 1.5f;   // 远距离: 更保守
                out.spreadMul = 1.3f;
            }
            break;
        }
    }

    // 如果 LLM 指令撤退或掩体移动 → 抑制射击 (除非玩家很近 < 50px)
    if ((intent.action == BotIntent::Action::Retreat ||
         intent.action == BotIntent::Action::MoveToCover) &&
        distToPlayer > 50.0f) {
        out.shouldShoot = false;
    }

    // 近战武器 → 只在很近时射击
    if (weaponSlot >= 2 && distToPlayer > 25.0f) {
        out.shouldShoot = false;
    }

    return out;
}

// ============================================================================
// 传统 AI 回退 — 保持原随机游走 + 索敌射击行为
// ============================================================================
BehaviorOutput BotBehaviorExecutor::TraditionalFallback(
    float botX, float botY,
    float playerX, float playerY,
    bool canSeePlayer,
    std::mt19937& rng,
    float dt)
{
    BehaviorOutput out{};

    aiTimer_ -= dt;
    if (aiTimer_ <= 0.0f) {
        std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);
        currentVx_ = dirDist(rng) * 60.0f;
        currentVy_ = dirDist(rng) * 60.0f;
        // 归一化对角线速度
        const float spd = std::sqrt(currentVx_ * currentVx_ + currentVy_ * currentVy_);
        if (spd > 60.0f) {
            currentVx_ = currentVx_ / spd * 60.0f;
            currentVy_ = currentVy_ / spd * 60.0f;
        }
        aiTimer_ = 1.0f + static_cast<float>(rng() % 100) / 100.0f;
    }

    out.vx = currentVx_;
    out.vy = currentVy_;

    if (canSeePlayer) {
        // 看到玩家 → 瞄准射击 (标准参数)
        out.aimAngle = std::atan2(playerY - botY, playerX - botX);
        out.shouldShoot = true;
        out.shootCooldownMul = 1.0f;
        out.spreadMul = 1.0f;
    } else {
        out.aimAngle = std::atan2(currentVy_, currentVx_);
        out.shouldShoot = false;
    }

    return out;
}