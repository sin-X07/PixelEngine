#pragma once
// ============================================================================
// SoundEvents —— 声音传播（听觉信息层）
//
// 约定：
//   * 每个「发声单元」都在 Tuning 里有一个可听半径（见下面的 SoundRadius）。
//   * 半径内的所有存活单位都会收到一条 HeardSound（记录在 CS2DGame::BotRuntime，
//     由 CS2DGame::EmitSound 统一分发），再由各自的决策树决定怎么反应：
//       听到枪声   → 转向警戒；贴近时摸过去（断后位不追）
//       听到换弹   → 趁机前压
//       听到脚步   → 去声源搜索（原有行为）
//       听到手雷落地 → 躲开落点
//       听到下包/拆包 → 扑执行者（原有行为，见 GetActiveSoundMaker）
//       听到 C4 滴答 → CT 转向炸弹方向
//       听到爆炸/阵亡 → 转向警戒
//   * 声音只受距离限制：墙体、烟雾、朝向都不影响（声音绕墙）。
//   * 只有敌对声音会触发警戒；友军声音只记录，不当作威胁。
// ============================================================================

#include "../Core/Types.hpp"
#include "Tuning.hpp"
#include "Weapons/WeaponDef.hpp"

enum class SoundEventType {
    Footstep,        // 脚步（静步不发声）
    Gunshot,         // 枪声（半径按武器类别/消音器换算）
    Reload,          // 换弹
    WeaponSwitch,    // 切枪
    KnifeSwing,      // 挥刀
    GrenadeThrow,    // 投掷出手
    GrenadeBounce,   // 手雷落地/撞停（落点警告）
    Explosion,       // 高爆雷 / C4 爆炸
    Flashbang,       // 闪光弹爆响
    SmokePop,        // 烟雾弹释放
    BombPlanted,     // C4 安装完成
    BombDefused,     // C4 拆除完成
    BombBeep,        // C4 滴答（供 CT 定位）
    Death,           // 阵亡
    Count
};

// 一次发声：位置 + 阵营 + 半径。sourceIdx：-1=玩家, >=0=BOT 下标, -2=环境
struct SoundEvent {
    SoundEventType type = SoundEventType::Footstep;
    float x = 0.0f;
    float y = 0.0f;
    Team  team = Team::None;
    int   sourceIdx = -2;
    float radius = 0.0f;
};

// 单位「听到」的一条信息（由 CS2DGame::EmitSound 写入，随时间老化）
struct HeardSound {
    SoundEventType type = SoundEventType::Footstep;
    float x = 0.0f;
    float y = 0.0f;
    float age = 0.0f;        // 秒；超过 Tuning::SoundMemory 丢弃
    float strength = 0.0f;   // 1=贴脸，0=恰好在半径上
    Team  team = Team::None; // 发声方阵营
    int   sourceIdx = -2;
    bool  hostile = false;   // 发声方是否是听者的敌人
};

// 武器是否带消音器（枪声传播半径显著更小）
inline bool WeaponSuppressed(WeaponId id) {
    switch (id) {
        case WeaponId::USP_S:
        case WeaponId::M4A1S:
        case WeaponId::MP5SD:
            return true;
        default:
            return false;
    }
}

// 枪声传播半径：按武器类别给基准，再按消音器衰减
inline float GunshotRadius(const WeaponDef& def) {
    float r = Tuning::HearGunshotRifle;
    switch (def.cls) {
        case WeaponClass::Pistol:     r = Tuning::HearGunshotPistol; break;
        case WeaponClass::SMG:        r = Tuning::HearGunshotSMG; break;
        case WeaponClass::Rifle:      r = Tuning::HearGunshotRifle; break;
        case WeaponClass::Shotgun:    r = Tuning::HearGunshotShotgun; break;
        case WeaponClass::Sniper:     r = Tuning::HearGunshotSniper; break;
        case WeaponClass::MachineGun: r = Tuning::HearGunshotMG; break;
        default:                      r = Tuning::HearGunshotPistol; break;
    }
    if (WeaponSuppressed(def.id)) r *= Tuning::HearSuppressedMul;
    return r;
}

// 每种发声体的默认传播半径（世界像素）
constexpr float SoundRadius(SoundEventType type) {
    switch (type) {
        case SoundEventType::Footstep:      return Tuning::FootstepHearRadius;
        case SoundEventType::Gunshot:       return Tuning::HearGunshotRifle;  // 调用方按武器覆盖
        case SoundEventType::Reload:        return Tuning::HearReloadRadius;
        case SoundEventType::WeaponSwitch:  return Tuning::HearSwitchRadius;
        case SoundEventType::KnifeSwing:    return Tuning::HearKnifeRadius;
        case SoundEventType::GrenadeThrow:  return Tuning::HearThrowRadius;
        case SoundEventType::GrenadeBounce: return Tuning::HearBounceRadius;
        case SoundEventType::Explosion:     return Tuning::HearExplosionRadius;
        case SoundEventType::Flashbang:     return Tuning::HearFlashRadius;
        case SoundEventType::SmokePop:      return Tuning::HearSmokeRadius;
        case SoundEventType::BombPlanted:   return Tuning::HearPlantedRadius;
        case SoundEventType::BombDefused:   return Tuning::HearDefusedRadius;
        case SoundEventType::BombBeep:      return Tuning::HearBombBeepRadius;
        case SoundEventType::Death:         return Tuning::HearDeathRadius;
        default:                            return 0.0f;
    }
}

inline const char* SoundEventName(SoundEventType type) {
    switch (type) {
        case SoundEventType::Footstep:      return "脚步";
        case SoundEventType::Gunshot:       return "枪声";
        case SoundEventType::Reload:        return "换弹";
        case SoundEventType::WeaponSwitch:  return "切枪";
        case SoundEventType::KnifeSwing:    return "挥刀";
        case SoundEventType::GrenadeThrow:  return "投掷";
        case SoundEventType::GrenadeBounce: return "手雷落地";
        case SoundEventType::Explosion:     return "爆炸";
        case SoundEventType::Flashbang:     return "闪光弹";
        case SoundEventType::SmokePop:      return "烟雾弹";
        case SoundEventType::BombPlanted:   return "C4 安装";
        case SoundEventType::BombDefused:   return "C4 拆除";
        case SoundEventType::BombBeep:      return "C4 滴答";
        case SoundEventType::Death:         return "阵亡";
        default:                            return "未知";
    }
}
