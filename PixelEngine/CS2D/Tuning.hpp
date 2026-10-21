#pragma once

// ============================================================================
// Tuning — 玩法调参常量集中地
//
// 以前这些数字散落在 CS2DGameBots / CS2DGameSquad / CS2DGameCombat 里，
// 调手感要满仓库搜索。改这里即可，不要在各实现文件里再写裸数字。
//
// 约定：单位统一为「世界像素 / 秒」。
// ============================================================================

namespace Tuning {

// ---------------- 视野（玩家与 BOT 共用同一套几何）----------------
constexpr float VisionRadius      = 120.0f;  // 扇形半径（世界单位）
constexpr float VisionSoftEdge    = 24.0f;   // 距离软边
constexpr float VisionConeDeg     = 120.0f;  // 扇形总角度
constexpr float VisionAngleSoftDeg= 14.0f;   // 角度软边（度）
// 含软边后的有效半角（玩家实际能看到的边缘）：(120 + 14) / 2 = 67°
constexpr float VisionHalfAngleRad =
    ((VisionConeDeg + VisionAngleSoftDeg) * 0.5f) * 3.14159265f / 180.0f;

// ---------------- 移动 ----------------
constexpr float BotMoveSpeed      = 60.0f;   // BOT 基础移动速度
constexpr float SpeedMulEntry     = 1.00f;   // 突破手
constexpr float SpeedMulSupport   = 0.80f;   // 支援
constexpr float SpeedMulAnchor    = 0.55f;   // 断后
constexpr float SpeedMulMin       = 0.30f;   // 速度倍率下限（防止不移动）

// ---------------- 局部避障 ----------------
constexpr float SepRadius        = 26.0f;    // 排斥半径
constexpr float SepStrength      = 48.0f;    // 分离力强度
constexpr float SepMaxSpeed      = 60.0f;    // 合成后限速

// ---------------- 寻路 ----------------
constexpr float RepathInterval   = 0.8f;     // 重算间隔
constexpr float RepathGoalMoved  = 70.0f;    // 目标移动多少像素触发重算
constexpr float WaypointReach    = 8.0f;     // 到达路点判定
constexpr float PathAgentRadius  = 3.5f;     // 烘焙可行走网格的角色半径

// ---------------- 受击撤退 ----------------
constexpr float RetreatHealth    = 30.0f;    // 中弹后血量低于此值触发撤退
constexpr float RetreatKeepHealth= 35.0f;    // 撤退中允许继续的最高血量
constexpr float RetreatDuration  = 2.0f;     // 撤退持续秒数
constexpr float CoverRadiusNear  = 55.0f;    // 掩体采样半径（近）
constexpr float CoverRadiusMid   = 95.0f;
constexpr float CoverRadiusFar   = 135.0f;

// ---------------- 补枪 (trade) ----------------
constexpr float TradeWindow      = 3.5f;     // 队友阵亡后多久内可补
constexpr float TradeMaxDist     = 350.0f;   // 最远补枪距离
constexpr float TradeMinDist     = 40.0f;    // 太近不必补
constexpr int   TradeWindowSec   = 3;        // 提示词里显示的窗口秒数

// ---------------- 手雷 ----------------
constexpr float NadeThrowMin     = 90.0f;    // 投掷最小距离
constexpr float NadeThrowMax     = 230.0f;   // 投掷最大距离
constexpr float NadeCooldown     = 7.0f;     // 两次投掷间隔

// ---------------- 决策 ----------------
constexpr float CloseCombatRange = 180.0f;   // 低于此距离交给战术执行器（不寻路）
constexpr float AnchorHoldRange  = 260.0f;   // 断后位超出此距离不主动追击
constexpr float FocusMaxRange    = 340.0f;   // 集火目标优先半径
constexpr float LowAmmoFraction  = 0.25f;    // 弹匣低于此比例时脱战也要换弹
constexpr float PatrolHoldSpread = 90.0f;    // 守点时岗位附近的游走半径
constexpr float PatrolCentroidSpread = 260.0f; // 推进时敌方出生点质心附近的取点半径

// ---------------- BOT 朝向 ----------------
constexpr float BotScanSpeed     = 1.0f;   // 无目标时原地扫描的角速度（弧度/秒 ≈ 57°/s）
constexpr float BotScanAmplitudeRad = 0.45f; // 行进时左右摆头幅度（≈26°）
constexpr float BotTurnOnHit     = 1.0f;   // 是否在受击时立刻转向威胁

// ---------------- 小队 ----------------
constexpr float FocusUpdateInterval = 0.5f;  // 集火目标刷新间隔
constexpr float FocusMateRadius     = 160.0f; // 统计「周围有多少队友」
constexpr float FocusCoverRadius    = 120.0f; // 掩护弱势队友的判定半径
constexpr float FocusMateWeight     = 45.0f; // 集火偏好权重（每人）
constexpr float FocusCoverWeight    = 60.0f; // 掩护偏好权重

} // namespace Tuning
