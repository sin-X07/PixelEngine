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
// 全地图共享的固定世界属性，按高清地图的全局空间基准校准。
constexpr float PlayerSize = 14.0f;
constexpr float SmokeRadius = 140.0f;
constexpr float PlayerAcceleration = 1200.0f;
constexpr float PlayerStopSpeed = 2.0f;
constexpr float BulletHitPadding = 4.0f;
constexpr float BombPickupRadius = 48.0f;
constexpr float DropPickupRadius = 32.0f;
constexpr float ManualPickupRadius = 60.0f;
constexpr float Pi = 3.14159265f;
constexpr float CameraSmoothRate = 18.0f;
constexpr float CameraSettlePixels = 0.25f;
constexpr float AimHandoffMaxSeconds = 1.25f;
constexpr float ScopeCameraShiftLow = 0.35f;
constexpr float InitialAimDistance = 90.0f;
constexpr float RecoilReferenceDistance = 200.0f;
constexpr float RecoilRecoveryDelay = 0.18f;
constexpr float RecoilShakePerDegree = 0.25f, RecoilShakeMax = 1.0f;
constexpr float RecoilShakeDecay = 14.0f;
constexpr float AimScreenMargin = 12.0f;
constexpr int AimLineSolidR = 50, AimLineSolidG = 225, AimLineSolidB = 235, AimLineSolidAlpha = 225;
constexpr int AimLineHiddenR = 185, AimLineHiddenG = 230, AimLineHiddenB = 235, AimLineHiddenAlpha = 125;
constexpr float AimLineDashPixels = 8.0f, AimLineGapPixels = 6.0f;
constexpr float WeaponOffsetMul = 0.35f, MuzzleLengthMul = 0.55f;
constexpr float PistolDrawSize = 6.0f, PrimaryDrawSize = 8.0f;
constexpr float CameraLookShift = 0.5f;
constexpr float CameraAngledLength = 200.0f;
constexpr float EdgeZoomOutZone = 0.005f;
constexpr float EdgeZoomHysteresis = 10.0f;
constexpr float EdgeZoomOutMin = 0.7f;
constexpr float EdgeZoomFovAngleMul = 0.25f;
constexpr float EdgeZoomSeconds = 0.4f;
constexpr float BotFovRadiusMul[] = {0.8f, 0.85f, 0.9f, 0.95f};
constexpr float BotFovAngleMul[] = {0.8f, 0.85f, 0.9f, 0.95f};
constexpr float BotReactionSecs[] = {2.5f, 1.0f, 0.5f, 0.35f};
constexpr float AimSettleSecs[] = {1.2f, 0.8f, 0.5f, 0.35f};
constexpr float AimSettleMul = 1.0f;
constexpr float AlertMemory = 6.0f, BulletNearRadius = 28.0f;
constexpr float BulletDodgeDistance = 60.0f;
constexpr float BulletReactionMul = 1.7f;
constexpr float RecoilRifle = 0.6f, RecoilSniper = 2.5f, RecoilSmg = 0.35f;
constexpr float RecoverRifle = 6.0f, RecoverSniper = 4.0f, RecoverSmg = 8.0f;
constexpr float BloomSniper = 10.0f, BloomSmg = 24.0f;
constexpr float DegreesToRadians = 3.14159265f / 180.0f;
constexpr float RadarFadeStart = 4.0f, RadarFadeEnd = 6.0f;
constexpr float NadeThrowSpeedMin = 360.0f, NadeThrowSpeedMax = 920.0f;
constexpr float NadeFriction = 2.0f;


// ---------------- 视野（玩家与 BOT 共用同一套几何）----------------
// 半径 200：镜头按「视野圆刚好装进较窄一边」自动拉近；
// 迷雾、敌人剔除、BOT 视野/探点都从这里取数，改一处即同步生效。
constexpr float VisionRadius      = 200.0f;  // 扇形半径（世界单位）
constexpr float VisionSoftEdge    = 40.0f;   // 距离软边（按半径等比收紧）
constexpr float VisionConeDeg     = 120.0f;  // 扇形总角度
constexpr float VisionAngleSoftDeg= 14.0f;   // 角度软边（度）
// 含软边后的有效半角（玩家实际能看到的边缘）：(120 + 14) / 2 = 67°
constexpr float VisionHalfAngleRad =
    ((VisionConeDeg + VisionAngleSoftDeg) * 0.5f) * 3.14159265f / 180.0f;

// ---------------- 回合 ----------------
constexpr float BuyWindowSeconds = 15.0f;   // 开局可购买时间（秒，自回合开始计）

// ---------------- 移动 ----------------
constexpr float PlayerBaseSpeed    = 120.0f;  // 玩家基础移速（再乘武器倍率）
constexpr float PlayerStealthSpeed = 50.0f;  // 玩家潜行移速
constexpr float BotMoveSpeed      = 120.0f;   // BOT 基础移动速度
constexpr float SpeedMulEntry     = 1.00f;   // 突破手
constexpr float SpeedMulSupport   = 0.80f;   // 支援
constexpr float SpeedMulAnchor    = 0.55f;   // 断后
constexpr float SpeedMulMin       = 0.30f;   // 速度倍率下限（防止不移动）

// ---------------- 局部避障 ----------------
constexpr float SepRadius        = 52.0f;    // 排斥半径
constexpr float SepStrength      = 96.0f;    // 分离力强度
constexpr float SepMaxSpeed      = 120.0f;    // 合成后限速

// ---------------- 寻路 ----------------
constexpr float RepathInterval   = 0.8f;     // 重算间隔
constexpr float RepathGoalMoved  = 140.0f;    // 目标移动多少像素触发重算
constexpr float WaypointReach    = 16.0f;     // 到达路点判定
constexpr float PathAgentRadius  = 8.0f;     // 烘焙可行走网格的角色半径

// ---------------- 受击撤退 ----------------
constexpr float RetreatHealth    = 30.0f;    // 中弹后血量低于此值触发撤退
constexpr float RetreatKeepHealth= 35.0f;    // 撤退中允许继续的最高血量
constexpr float RetreatDuration  = 2.0f;     // 撤退持续秒数
constexpr float CoverRadiusNear  = 110.0f;    // 掩体采样半径（近）
constexpr float CoverRadiusMid   = 190.0f;
constexpr float CoverRadiusFar   = 270.0f;

// ---------------- 补枪 (trade) ----------------
constexpr float TradeMaxDist     = 700.0f;   // 最远补枪距离
constexpr float TradeMinDist     = 80.0f;    // 太近不必补

// ---------------- 手雷 ----------------
constexpr float NadeBlastRadius  = 220.0f;  // 高爆雷爆炸半径（世界单位）
constexpr float NadeMaxDamage    = 78.0f;   // 中心伤害（边缘按平方衰减到 0）
constexpr float DefuseRadius     = 52.0f;   // 拆包半径：必须贴近包点
constexpr float BombHoldRing     = 170.0f;   // 下包后 T 守包的环形半径
constexpr float DefuseCoverRing  = 180.0f;   // CT 拆包时其余人的掩护环半径
constexpr float NadeBlastFxTime  = 0.35f;   // 爆炸特效持续

// 燃烧弹 / 燃烧瓶：落地后形成一片持续燃烧的火区
constexpr float FireRadius       = 60.0f;   // 火区半径（世界单位）
constexpr float FireLife         = 7.0f;    // 燃烧持续时间（秒）
constexpr float FireTickInterval = 0.25f;   // 伤害结算间隔
constexpr int   FireTickDamage   = 3;       // 每次结算伤害（约 12 HP/s）
// 闪光弹：致盲时长随距离衰减（贴脸最长、边缘很短）
constexpr float FlashRadius      = 520.0f;  // 有效半径（超出完全不闪）
constexpr float FlashMaxBlind    = 3.2f;    // 正中心致盲秒数
constexpr float FlashMinBlind    = 0.6f;    // 边缘致盲秒数
constexpr float NadeThrowMin     = 180.0f;    // 投掷最小距离
constexpr float NadeThrowMax     = 460.0f;   // 投掷最大距离
constexpr float NadeCooldown     = 7.0f;     // 两次投掷间隔

// ---------------- 投掷物撞墙反弹 ----------------
// 撞墙后沿法线反弹，速度乘以恢复系数（动能按 v² 衰减）。
// 0.45 ≈ 每次撞墙丢掉 80% 动能，既有明显反弹又不会把雷弹回投掷者脚下。
constexpr float NadeBounceRestitution  = 0.45f;
// 反弹后速度低于此值就直接停下，避免在墙角里高频抖动
constexpr float NadeMinBounceSpeed     = 56.0f;
// 连续撞墙时「撞墙声」听觉事件的节流（秒）
constexpr float NadeBounceHearCooldown = 0.12f;

// ---------------- 决策 ----------------
constexpr float CloseCombatRange = 360.0f;   // 低于此距离交给战术执行器（不寻路）
constexpr float AnchorHoldRange  = 520.0f;   // 断后位超出此距离不主动追击
constexpr float LowAmmoFraction  = 0.25f;    // 弹匣低于此比例时脱战也要换弹
constexpr float PatrolHoldSpread = 180.0f;    // 守点时岗位附近的游走半径

// ---------------- 移动散布 / 动态准星 ----------------
// 静止时散布 = 武器基础值；静步约 42% 速度，跑动 100% 速度。
constexpr float MoveSpreadMaxMul    = 5.0f;  // 全速跑动时的基础散布倍率
constexpr float CrosshairBloomMaxPx = 18.0f; // 全速跑动时准星 gap 的最大增量（1080p 基准）
constexpr float CrosshairBloomExpandRate   = 12.0f;  // 松开静步/加速：散开速度
constexpr float CrosshairBloomContractRate = 18.0f;  // 按下静步/减速：聚合速度

// ---------------- 听觉：所有发声体的传播范围 ----------------
// 声音只按距离传播（墙体 / 烟雾 / 朝向都不阻断）；半径内的单位都会收到一条
// 听觉信息（HeardSound），由各自的决策树决定反应。
// 枪声、爆炸明显大于视野半径，体现「先听见、后看见」。
constexpr float PlantHearRadius  = 600.0f;   // C4 安装声（持续声）可被听到的半径
constexpr float DefuseHearRadius = 600.0f;   // 拆包声（持续声）可被听到的半径

constexpr float FootstepHearRadius  = 260.0f; // 普通跑动可被听到的半径
constexpr float HearGunshotPistol   = 1040.0f;
constexpr float HearGunshotSMG      = 1200.0f;
constexpr float HearGunshotRifle    = 1520.0f;
constexpr float HearGunshotShotgun  = 1600.0f;
constexpr float HearGunshotSniper   = 1900.0f;
constexpr float HearGunshotMG       = 1640.0f;
constexpr float HearSuppressedMul   = 0.42f;  // 消音武器（USP-S/M4A1-S/MP5-SD）
constexpr float HearReloadRadius    = 440.0f;
constexpr float HearSwitchRadius    = 300.0f;
constexpr float HearKnifeRadius     = 180.0f;
constexpr float HearThrowRadius     = 480.0f;
constexpr float HearBounceRadius    = 520.0f;
constexpr float HearExplosionRadius = 2200.0f;
constexpr float HearFlashRadius     = 1300.0f;
constexpr float HearSmokeRadius     = 800.0f;
constexpr float HearPlantedRadius   = 2800.0f; // 下包完成 ≈ 全图可闻
constexpr float HearDefusedRadius   = 2800.0f; // 拆除完成 ≈ 全图可闻
constexpr float HearBombBeepRadius  = 1040.0f;  // CT 靠滴答声定位炸弹
constexpr float HearDeathRadius     = 700.0f;

// 听觉信息保留时长（秒）
constexpr float SoundMemory          = 3.0f;
// 听到敌对声音后「朝向声源警戒」的时长（秒）
constexpr float SoundAlertMemory     = 2.5f;
// 听到近处枪声 / 敌人换弹时，主动摸过去的距离阈值
constexpr float GunshotPushRadius    = 460.0f;
constexpr float ReloadPushRadius     = 380.0f;
// 手雷落点危险半径 + 躲闪位移
constexpr float GrenadeDangerRadius  = 300.0f;
constexpr float GrenadeDodgeDist     = 180.0f;

// 听觉触发时略微提高命中率（散布乘数 <1 = 打得更准）
constexpr float PlantHearSpreadMul  = 0.72f;  // 听到下包 → 散布降到 72%
constexpr float DefuseHearSpreadMul = 0.72f;  // 听到拆包 → 散布降到 72%

// ---------------- 脚步声细节 ----------------
// 普通跑动会暴露位置；静步（玩家 Shift / BOT 的 lurk 慢走）不出声。
// 脚步声只让「听见的那只 BOT」自己知道，不进入团队情报网。
constexpr float FootstepMemory     = 2.5f;   // 听到后记住最后位置的时间
constexpr float BotStealthSpeed    = 50.0f;  // BOT 静步速度（与玩家潜行一致）

// ---------------- C4 掉落：回收 / 蹲守 ----------------
// T 全队本性知道 C4 掉落位置（该信息对己方始终可见）。
// 派「离掉落点最近」的少数人去取回，其余继续原任务，避免全军挤在掉包点暴露。
// 回收半径不做限制：C4 是胜负关键，再远也必须有人回头去捡。
constexpr int   BombRecoverCount  = 2;        // 最多派几个 T 去回收（就近原则）
// CT 只有在「看到掉落 C4」（bombIntel_）后才会蹲守；环形站位形成交叉火力
constexpr float BombGuardRing     = 140.0f;    // CT 蹲守 C4 的环形站位半径

// ---------------- BOT 朝向 ----------------
constexpr float BotScanSpeed     = 1.0f;   // 无目标时原地扫描的角速度（弧度/秒 ≈ 57°/s）
constexpr float BotScanAmplitudeRad = 0.45f; // 行进时左右摆头幅度（≈26°）
constexpr float BotTurnOnHit     = 1.0f;
constexpr float BotMaxTurnRate   = 8.0f;   // 最大转向速度（弧度/秒 ≈ 458°/s）   // 是否在受击时立刻转向威胁

// ---------------- 小队 ----------------
constexpr float FocusUpdateInterval = 0.5f;  // 集火目标刷新间隔
constexpr float FocusMateRadius     = 320.0f; // 统计「周围有多少队友」
constexpr float FocusCoverRadius    = 240.0f; // 掩护弱势队友的判定半径
constexpr float FocusMateWeight     = 90.0f; // 集火偏好权重（每人）
constexpr float FocusCoverWeight    = 120.0f; // 掩护偏好权重




// ---------------------------------------------------------------------------
// AI 行为增强（P0–P3）：近战 / 交火机动 / 投掷 / 支援 / 搜点 / 角色 / 指令
// ---------------------------------------------------------------------------
namespace ai {
// 近战
constexpr float MeleeReachBase   = 36.0f;   // 基础挥击距离
constexpr float MeleeReachPerSize = 0.8f;   // 每个 body size 增加的触及距离
constexpr float MeleeChaseRange  = 180.0f;   // 持刀时低于此距离主动贴近

// 交火机动（滞回 + 沿墙滑动 + 短程寻路）
constexpr float StrafeForwardGap = 130.0f;   // 超过 ideal+gap 才前进
constexpr float StrafeBackGap    = 110.0f;   // 低于 ideal-gap 才后退
constexpr float StrafeDirHold    = 0.75f;   // 侧拉方向保持秒数
constexpr float StrafeDirLatch   = 0.50f;   // 前进/后退方向保持秒数
constexpr float StrafeCombatPath = 520.0f;  // 交火中允许短程寻路的最大距离
constexpr float StrafeSlideProbe = 24.0f;   // 沿墙滑动探测步长
constexpr int   StrafeSlideAngles = 4;      // 探测角度数量（±45/±90）

// 投掷物
constexpr float NadeValidAngleStep = 15.0f; // 失败后尝试的角度步长（度）
constexpr int   NadeValidTries      = 5;    // 最多尝试次数（含正向）
constexpr float NadeLandingSlack    = 48.0f;// 落点可行走判定的容差
constexpr float NadeMinLandingDist  = 120.0f;

// 支援（队友交战中前压补枪）
constexpr float AssistPushRadius    = 380.0f;  // 队友交火点距我方的最大距离
constexpr float AssistPushMaxHealth = 70.0f;   // 队友血量低于此比例才支援（%）
constexpr float AssistPushCooldown  = 10.0f;    // 同一 BOT 两次支援冷却
constexpr float AssistPushHold      = 1.2f;    // 到达后停留时间

// 搜点
constexpr float SearchConfirmSec    = 1.0f;    // 进入区域后确认"已搜"的停留时间
constexpr float SearchAreaRadius    = 320.0f;  // 判定"进入区域"的距离
constexpr float SearchMaxDetour     = 840.0f;  // 为搜点最多绕行距离
constexpr float SearchWindowSec     = 40.0f;   // 回合开始后可搜点的时间窗

// 角色动态重算
constexpr float RoleRefreshInterval = 2.0f;    // 重算间隔
constexpr float RoleSwitchCooldown  = 8.0f;    // 同一 BOT 两次换角色的最短间隔
constexpr float RoleTransitionSec   = 3.0f;    // 速度倍率过渡时间

// 教练指令变更预算
constexpr float IntentMinHold       = 10.0f;   // 意图最短保持
constexpr int   IntentMaxChanges    = 2;       // 每回合最多变更次数
constexpr float IntentChangeCooldown = 6.0f;   // 两次变更最短间隔
constexpr float IntentRenewSec      = 8.0f;    // 到期续期时长
constexpr float SilentSeconds       = 30.0f;   // 无接触多久算"沉默期"
constexpr int   SameSideStreakLimit = 3;      // 连续同一侧打几次才强制换边（1=每两次必换）

}  // namespace ai

} // namespace Tuning
