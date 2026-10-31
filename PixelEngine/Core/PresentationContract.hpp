#pragma once
#include "Types.hpp"
#include "../CS2D/Weapons/WeaponDef.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// ============================================================================
// PresentationContract — 玩法层 → 表现层（UI）的唯一跨层契约（契约 v1 第 2 节）
//
// 只允许 include Types.hpp / WeaponDef.hpp / <cstdint>：
// 不得包含 GameUI、CS2DGame、ImGui 或 OpenGL，避免表现层反向依赖玩法实现。
// 所有结构按值传递（快照），不暴露业务对象的可写指针。
// ============================================================================

namespace Presentation {

// ---- 购买 ----
enum class BuyKind { Weapon, Armor };

struct BuyRequest {
    BuyKind  kind  = BuyKind::Weapon;
    WeaponId weapon = WeaponId::None;   // kind==Weapon 时有效
    int      armorTier = 0;             // kind==Armor 时只允许 1 / 2
};

// 失败原因优先序（EvaluatePurchase 固定按此顺序返回）：
// InvalidItem → PlayerDead → WindowClosed → OutsideBuyZone → TeamRestricted →
// AlreadyEquipped → InventoryFull → ItemLimitReached → InsufficientFunds → Ok
enum class BuyCode {
    Ok,
    InvalidItem,
    PlayerDead,
    WindowClosed,
    OutsideBuyZone,
    TeamRestricted,
    InsufficientFunds,
    InventoryFull,
    ItemLimitReached,
    AlreadyEquipped
};

struct BuyResult {
    BuyRequest request{};
    BuyCode    code = BuyCode::InvalidItem;
    int        moneyAfter = 0;
    int        quantityAfter = 0;   // 武器持有数 / 同类投掷物数；护甲为目标档位 0/1
};

// ---- 开镜 ----
// 契约 v1.1：三档循环（关 → 1.5x → 2x → 关）。开镜时可见区改为以准星为圆心的圆形，
// 镜外用视野雾遮盖；相机向准星方向偏移，使准星下的世界点不随倍率漂移。
struct ScopeView {
    WeaponId weapon = WeaponId::None;
    bool     supported = false;
    bool     active = false;              // level > 0（目标状态，不等同于过渡是否完成）
    int      level = 0;                   // 0=关 / 1=1.5x / 2=2x
    float    transition = 0.0f;           // 0..1，镜框/遮罩淡入淡出进度（不是倍率进度）
    float    zoomMultiplier = 1.0f;       // 当前实际倍率（含过渡）
    float    targetZoomMultiplier = 1.0f; // 当前档位目标倍率（1.0 / 1.5 / 2.0）
};

enum class ScopeResetReason {
    InputBlocked,
    FocusLost,
    WeaponChanged,
    ReloadStarted,
    PlayerDied,
    RoundRestart,
    MatchExit
};

// ---- 枪弹视觉段 ----
struct BulletVisualSegment {
    std::uint64_t id = 0;               // 对局内唯一视觉段编号
    WeaponId weapon = WeaponId::None;
    float fromX = 0.0f, fromY = 0.0f;   // 世界坐标（不是屏幕像素）
    float toX   = 0.0f, toY   = 0.0f;
    float remainingSeconds = 0.0f;
    float durationSeconds  = 0.08f;
    bool  impact = false;               // 实际碰撞终止；寿命/射程到期不算命中
};

// 开镜固定参数（契约 v1.1 第 4 节）
// 档位倍率表：level 即下标，0=关；右键循环 关 → 1 → 2 → 关
inline constexpr int   kScopeLevelCount = 3;
inline constexpr float kScopeZoomLevels[kScopeLevelCount] = { 1.0f, 1.5f, 2.0f };
inline constexpr float kScopeZoomMultiplier = 1.5f;      // 兼容旧调用（== 档位 1）
inline constexpr float kScopeTransitionSeconds = 0.12f;  // 每次换档的过渡时长
// 镜筒半径 = 短边 × 该系数（屏幕上），世界半径 = 半径像素 / 当前倍率
inline constexpr float kScopeCircleRadiusFrac = 0.42f;
// 相机向准星偏移的比例上限（1.5x ≈ 0.35，2x ≈ 0.5，其余按倍率线性插值）
inline constexpr float kScopeCameraShiftMax = 0.5f;

// 契约 v1.1：开镜圆形可见区的唯一几何定义（纯数学，渲染与回归测试共用）。
// 圆外返回 0；圆内返回 1；softWidth 比例内用 smoothstep 淡出，
// HasLineOfSight 等遮挡判定由调用方在此之上叠加。
inline float ComputeScopeVisibility(float centerX, float centerY, float radius,
                                    float targetX, float targetY,
                                    float softWidth = 0.06f) {
    if (radius <= 0.0f) return 0.0f;
    const float dx = targetX - centerX;
    const float dy = targetY - centerY;
    const float d = std::sqrt(dx * dx + dy * dy);
    const float soft = std::max(0.5f, radius * softWidth);
    if (d <= radius - soft) return 1.0f;
    if (d >= radius) return 0.0f;
    float blend = (d - (radius - soft)) / soft;
    blend = blend * blend * (3.0f - 2.0f * blend);
    return 1.0f - blend;
}

// 枪弹视觉段寿命（秒）与硬上限
inline constexpr float kBulletVisualSeconds = 0.08f;
inline constexpr std::size_t kBulletVisualMaxSegments = 512;

} // namespace Presentation
