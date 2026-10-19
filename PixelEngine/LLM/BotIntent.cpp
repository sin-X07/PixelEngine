#include "BotIntent.hpp"
#include <algorithm>

void BotIntent::FromLLMDecision(const std::string& action, const std::string& target,
                                 const std::string& shoot,
                                 float botX, float botY,
                                 float playerX, float playerY,
                                 float teammateX, float teammateY, bool hasTeammate,
                                 float worldW, float worldH)
{
    Reset();
    fresh = true;
    ageSec = 0.0f;

    // ---- 解析 action ----
    if (action == "进攻" || action == "aggressive")
        this->action = Action::Aggressive;
    else if (action == "防守" || action == "defensive")
        this->action = Action::Defensive;
    else if (action == "撤退" || action == "retreat")
        this->action = Action::Retreat;
    else if (action == "掩体移动" || action == "move_to_cover")
        this->action = Action::MoveToCover;
    else
        this->action = Action::None;

    // ---- 解析 shoot ----
    if (shoot == "点射" || shoot == "burst")
        shootMode = ShootDiscipline::Burst;
    else if (shoot == "停止射击" || shoot == "hold_fire")
        shootMode = ShootDiscipline::HoldFire;
    else
        shootMode = ShootDiscipline::Auto;  // "连射" 或未知 → 自动

    // ---- 解析 target → 世界坐标 ----
    // 单位向量：BOT → 玩家（敌人方向），以及其垂直方向（侧移方向）
    const float dx = playerX - botX;
    const float dy = playerY - botY;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) dist = 1.0f;
    const float ux = dx / dist;   // 指向敌人的单位向量
    const float uy = dy / dist;

    // 方向词对应的推进距离
    constexpr float ADVANCE_DIST = 150.0f;
    constexpr float FLANK_DIST   = 120.0f;

    if (target == "敌人" || target == "enemy" || target.find("敌人") != std::string::npos) {
        // 正对敌人
        targetX = playerX;
        targetY = playerY;
    } else if (target == "前方" || target == "forward") {
        // 向敌人推进
        targetX = botX + ux * ADVANCE_DIST;
        targetY = botY + uy * ADVANCE_DIST;
    } else if (target == "后退" || target == "back") {
        // 远离敌人
        targetX = botX - ux * ADVANCE_DIST;
        targetY = botY - uy * ADVANCE_DIST;
    } else if (target == "左前方" || target == "left-front") {
        // 绕左侧翼：垂直方向 + 少量前压
        targetX = botX + (-uy) * FLANK_DIST + ux * (FLANK_DIST * 0.5f);
        targetY = botY + ( ux) * FLANK_DIST + uy * (FLANK_DIST * 0.5f);
    } else if (target == "右前方" || target == "right-front") {
        // 绕右侧翼
        targetX = botX + ( uy) * FLANK_DIST + ux * (FLANK_DIST * 0.5f);
        targetY = botY + (-ux) * FLANK_DIST + uy * (FLANK_DIST * 0.5f);
    } else if (target == "掩体" || target == "cover") {
        // 掩体：朝远离敌人的方向后撤
        // 行为执行器会进一步搜索实际掩体位置
        targetX = botX - ux * 200.0f;
        targetY = botY - uy * 200.0f;
    } else if (target == "队友" || target == "teammate") {
        // 向最近存活队友靠拢（坐标由调用方提供，不再是哨兵值）
        if (hasTeammate) {
            targetX = teammateX;
            targetY = teammateY;
        } else {
            // 无队友可依：退化为向敌人方向推进
            targetX = botX + ux * ADVANCE_DIST;
            targetY = botY + uy * ADVANCE_DIST;
        }
    } else if (target == "撤退" || target == "retreat") {
        // 远离敌人
        targetX = botX - ux * 300.0f;
        targetY = botY - uy * 300.0f;
    } else {
        // 未知目标：按 action 决定方向
        switch (this->action) {
            case Action::Aggressive:
            case Action::Defensive:
                targetX = playerX; targetY = playerY; break;
            case Action::Retreat:
                targetX = botX - ux * 300.0f;
                targetY = botY - uy * 300.0f;
                break;
            case Action::MoveToCover:
                targetX = botX - ux * 200.0f;
                targetY = botY - uy * 200.0f;
                break;
            default:
                targetX = botX; targetY = botY; break;
        }
    }

    // clamp to map bounds
    targetX = std::clamp(targetX, 10.0f, worldW - 10.0f);
    targetY = std::clamp(targetY, 10.0f, worldH - 10.0f);
}