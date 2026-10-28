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
// 半径 100：镜头按「视野圆刚好装进较窄一边」自动拉近；
// 迷雾、敌人剔除、BOT 视野/探点都从这里取数，改一处即同步生效。
constexpr float VisionRadius      = 100.0f;  // 扇形半径（世界单位）
constexpr float VisionSoftEdge    = 20.0f;   // 距离软边（按半径等比收紧）
constexpr float VisionConeDeg     = 120.0f;  // 扇形总角度
constexpr float VisionAngleSoftDeg= 14.0f;   // 角度软边（度）
// 含软边后的有效半角（玩家实际能看到的边缘）：(120 + 14) / 2 = 67°
constexpr float VisionHalfAngleRad =
    ((VisionConeDeg + VisionAngleSoftDeg) * 0.5f) * 3.14159265f / 180.0f;

// ---------------- 回合 ----------------
constexpr float BuyWindowSeconds = 15.0f;   // 开局可购买时间（秒，自回合开始计）

// ---------------- 移动 ----------------
constexpr float PlayerBaseSpeed    = 60.0f;  // 玩家基础移速（再乘武器倍率）
constexpr float PlayerStealthSpeed = 25.0f;  // 玩家潜行移速
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
constexpr float PathAgentRadius  = 4.0f;     // 烘焙可行走网格的角色半径

// ---------------- 受击撤退 ----------------
constexpr float RetreatHealth    = 30.0f;    // 中弹后血量低于此值触发撤退
constexpr float RetreatKeepHealth= 35.0f;    // 撤退中允许继续的最高血量
constexpr float RetreatDuration  = 2.0f;     // 撤退持续秒数
constexpr float CoverRadiusNear  = 55.0f;    // 掩体采样半径（近）
constexpr float CoverRadiusMid   = 95.0f;
constexpr float CoverRadiusFar   = 135.0f;

// ---------------- 补枪 (trade) ----------------
constexpr float TradeMaxDist     = 350.0f;   // 最远补枪距离
constexpr float TradeMinDist     = 40.0f;    // 太近不必补

// ---------------- 手雷 ----------------
constexpr float NadeBlastRadius  = 110.0f;  // 高爆雷爆炸半径（世界单位）
constexpr float NadeMaxDamage    = 78.0f;   // 中心伤害（边缘按平方衰减到 0）
constexpr float DefuseRadius     = 26.0f;   // 拆包半径：必须贴近包点
constexpr float BombHoldRing     = 85.0f;   // 下包后 T 守包的环形半径
constexpr float DefuseCoverRing  = 90.0f;   // CT 拆包时其余人的掩护环半径
constexpr float NadeBlastFxTime  = 0.35f;   // 爆炸特效持续
// 闪光弹：致盲时长随距离衰减（贴脸最长、边缘很短）
constexpr float FlashRadius      = 260.0f;  // 有效半径（超出完全不闪）
constexpr float FlashMaxBlind    = 3.2f;    // 正中心致盲秒数
constexpr float FlashMinBlind    = 0.6f;    // 边缘致盲秒数
constexpr float NadeThrowMin     = 90.0f;    // 投掷最小距离
constexpr float NadeThrowMax     = 230.0f;   // 投掷最大距离
constexpr float NadeCooldown     = 7.0f;     // 两次投掷间隔

// ---------------- 决策 ----------------
constexpr float CloseCombatRange = 180.0f;   // 低于此距离交给战术执行器（不寻路）
constexpr float AnchorHoldRange  = 260.0f;   // 断后位超出此距离不主动追击
constexpr float LowAmmoFraction  = 0.25f;    // 弹匣低于此比例时脱战也要换弹
constexpr float PatrolHoldSpread = 90.0f;    // 守点时岗位附近的游走半径

// ---------------- 移动散布 / 动态准星 ----------------
// 静止时散布 = 武器基础值；静步约 42% 速度，跑动 100% 速度。
constexpr float MoveSpreadMaxMul    = 5.0f;  // 全速跑动时的基础散布倍率
constexpr float CrosshairBloomMaxPx = 18.0f; // 全速跑动时准星 gap 的最大增量（1080p 基准）
constexpr float CrosshairBloomExpandRate   = 12.0f;  // 松开静步/加速：散开速度
constexpr float CrosshairBloomContractRate = 18.0f;  // 按下静步/减速：聚合速度

// ---------------- 下包 / 拆包的「可听」范围 ----------------
// 声音比视野传得更远：听到下包/拆包声的敌对 BOT 会优先扑向执行者。
// 这两个半径明显大于 BOT_VIEW_RADIUS(100)，体现「听见但还看不见」。
constexpr float PlantHearRadius  = 300.0f;   // C4 安装声可被听到的半径
constexpr float DefuseHearRadius = 300.0f;   // 拆包声可被听到的半径

// ---------------- 脚步声 ----------------
// 普通跑动会暴露位置；静步（玩家 Shift / BOT 的 lurk 慢走）不出声。
// 脚步声只让「听见的那只 BOT」自己知道，不进入团队情报网。
constexpr float FootstepHearRadius = 130.0f; // 普通跑动可被听到的半径
constexpr float FootstepMemory     = 2.5f;   // 听到后记住最后位置的时间
constexpr float BotStealthSpeed    = 25.0f;  // BOT 静步速度（与玩家潜行一致）

// 听觉触发时略微提高命中率（散布乘数 <1 = 打得更准）
constexpr float PlantHearSpreadMul  = 0.72f;  // 听到下包 → 散布降到 72%
constexpr float DefuseHearSpreadMul = 0.72f;  // 听到拆包 → 散布降到 72%

// ---------------- C4 掉落：回收 / 蹲守 ----------------
// T 全队本性知道 C4 掉落位置（该信息对己方始终可见）。
// 派「离掉落点最近」的少数人去取回，其余继续原任务，避免全军挤在掉包点暴露。
// 回收半径不做限制：C4 是胜负关键，再远也必须有人回头去捡。
constexpr int   BombRecoverCount  = 2;        // 最多派几个 T 去回收（就近原则）
// CT 只有在「看到掉落 C4」（bombIntel_）后才会蹲守；环形站位形成交叉火力
constexpr float BombGuardRing     = 70.0f;    // CT 蹲守 C4 的环形站位半径

// ---------------- BOT 朝向 ----------------
constexpr float BotScanSpeed     = 1.0f;   // 无目标时原地扫描的角速度（弧度/秒 ≈ 57°/s）
constexpr float BotScanAmplitudeRad = 0.45f; // 行进时左右摆头幅度（≈26°）
constexpr float BotTurnOnHit     = 1.0f;
constexpr float BotMaxTurnRate   = 8.0f;   // 最大转向速度（弧度/秒 ≈ 458°/s）   // 是否在受击时立刻转向威胁

// ---------------- 小队 ----------------
constexpr float FocusUpdateInterval = 0.5f;  // 集火目标刷新间隔
constexpr float FocusMateRadius     = 160.0f; // 统计「周围有多少队友」
constexpr float FocusCoverRadius    = 120.0f; // 掩护弱势队友的判定半径
constexpr float FocusMateWeight     = 45.0f; // 集火偏好权重（每人）
constexpr float FocusCoverWeight    = 60.0f; // 掩护偏好权重

} // namespace Tuning
