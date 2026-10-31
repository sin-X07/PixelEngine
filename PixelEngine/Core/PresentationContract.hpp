#pragma once
#include "Types.hpp"
#include "../CS2D/Weapons/WeaponDef.hpp"
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
struct ScopeView {
    WeaponId weapon = WeaponId::None;
    bool     supported = false;
    bool     active = false;            // 目标状态，不等同于过渡动画是否完成
    float    transition = 0.0f;         // 0..1，当前视觉进度
    float    zoomMultiplier = 1.0f;     // 当前倍率，1 + 0.5 * transition
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

// 开镜固定参数（契约第 4 节：单级 1.5 倍、0.12 秒过渡）
inline constexpr float kScopeZoomMultiplier = 1.5f;
inline constexpr float kScopeTransitionSeconds = 0.12f;
// 枪弹视觉段寿命（秒）与硬上限
inline constexpr float kBulletVisualSeconds = 0.08f;
inline constexpr std::size_t kBulletVisualMaxSegments = 512;

} // namespace Presentation
