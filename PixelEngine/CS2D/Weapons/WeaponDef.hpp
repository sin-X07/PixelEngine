#pragma once
#include <cstdint>

// ============================================================================
// WeaponDef — 数据驱动武器表
//
// 单一数据源：玩法（伤害/射速/弹匣）、渲染（贴图/朝向偏移）、音效、经济（价格）
// 全部由此表驱动。新增武器 = 在下方 kWeapons 表加一行 + 补素材。
// ============================================================================

enum class WeaponId : int {
    None = 0,
    Knife,
    Glock,
    USP,
    Deagle,
    Galil,
    AK47,
    M4A1,
    SMG,
    Shotgun,
    AWP,
    GrenadeHE,
    GrenadeFlash,
    GrenadeSmoke,
    C4,             // 第 5 槽：C4（仅携带者可用）
    Count
};

enum class WeaponClass { Melee, Pistol, SMG, Rifle, Sniper, Shotgun, Grenade, Bomb };

// 武器栏位
enum class WeaponSlotKind { Primary, Secondary, Melee, Grenade };

struct WeaponDef {
    WeaponId        id;
    const char*     name;          // 显示名
    WeaponClass     cls;
    WeaponSlotKind  slot;
    int             price;         // 购买价格

    // 弹道
    float damage;          // 单发伤害（霰弹为单颗）
    float cooldown;        // 射击间隔（秒）
    float bulletSpeed;     // px/s
    float range;           // 最大射程（px）
    int   pellets;         // 弹丸数（霰弹 >1）
    float spreadBase;      // 基础散布（弧度）
    bool  automatic;       // true=按住连发

    // 弹药
    int   magazine;        // 弹匣容量（0 = 不耗弹，如刀/手雷）
    float reloadTime;      // 换弹耗时（秒）

    // 表现
    float moveSpeedMul;    // 持械移动倍率
    bool  scoped;          // 狙击开镜
    float degOffset;       // 精灵原生朝向偏移（90=朝上，0=朝右）
    const char* spritePath;
    // HUD 图标：默认 nullptr = 直接复用 spritePath 缩放（只维护一套素材）
    // 需要单独的高清图标时再填 images/weapons/xxx_d.png
    const char* iconPath;
};

namespace WeaponDB {

// 武器表（唯一数据源）
inline const WeaponDef kWeapons[] = {
    // id,           name,        cls,                  slot,                          price, dmg,  cd,   spd,   range, pellets, spread, auto, mag, reload, spdMul, scoped, deg, sprite,                          icon
    { WeaponId::None,        "-",     WeaponClass::Melee,  WeaponSlotKind::Melee,         0,   0.0f, 0.0f,    0.0f,    0.0f,  1, 0.00f, false,   0, 0.0f,  1.00f, false, 90.0f, nullptr,                        nullptr },
    { WeaponId::Knife,       "匕首",   WeaponClass::Melee,  WeaponSlotKind::Melee,         0,  50.0f, 0.50f,    0.0f,   45.0f,  1, 0.00f, false,   0, 0.0f,  1.00f, false, 90.0f, "cs2d/sprites/knife.png", "cs2d/sprites/knife_d.png" },
    { WeaponId::Glock,       "Glock", WeaponClass::Pistol, WeaponSlotKind::Secondary,   400,  22.0f, 0.18f,  700.0f,  700.0f,  1, 0.030f, false,  20, 1.99f,  0.92f, false,  0.0f, "cs2d/sprites/glock.png", "cs2d/sprites/glock_d.png" },
    // USP：CT 起始手枪（素材为占位，替换 assets/cs2d/sprites/usp.png 与 assets/audio/usp.wav 即可）
    { WeaponId::USP,         "USP",   WeaponClass::Pistol, WeaponSlotKind::Secondary,   200,  24.0f, 0.16f,  720.0f,  700.0f,  1, 0.026f, false,  12, 1.49f,  0.92f, false,  0.0f, "cs2d/sprites/usp.png", "cs2d/sprites/usp_d.png" },
    { WeaponId::Deagle,      "Deagle",WeaponClass::Pistol, WeaponSlotKind::Secondary,   700,  55.0f, 0.40f,  700.0f,  700.0f,  1, 0.020f, false,   7, 1.31f,  0.88f, false,  0.0f, "cs2d/sprites/deagle.png", "cs2d/sprites/deagle_d.png" },
    { WeaponId::Galil,       "Galil", WeaponClass::Rifle,  WeaponSlotKind::Primary,    2000,  28.0f, 0.11f,  640.0f,  800.0f,  1, 0.045f, true,   35, 1.83f,  0.80f, false, 90.0f, "cs2d/sprites/galil.png", "cs2d/sprites/galil_d.png" },
    { WeaponId::AK47,        "AK-47", WeaponClass::Rifle,  WeaponSlotKind::Primary,    2700,  36.0f, 0.10f,  620.0f,  800.0f,  1, 0.050f, true,   30, 2.74f,  0.80f, false, 90.0f, "cs2d/sprites/ak47.png", "cs2d/sprites/ak47_d.png" },
    { WeaponId::M4A1,        "M4A1",  WeaponClass::Rifle,  WeaponSlotKind::Primary,    3100,  32.0f, 0.09f,  640.0f,  800.0f,  1, 0.042f, true,   30, 1.75f,  0.80f, false, 90.0f, "cs2d/sprites/m4a1.png", "cs2d/sprites/m4a1_d.png" },
    { WeaponId::SMG,         "SMG",   WeaponClass::SMG,    WeaponSlotKind::Primary,    1500,  20.0f, 0.075f, 680.0f,  650.0f,  1, 0.055f, true,   30, 1.83f,  0.86f, false, 90.0f, "cs2d/sprites/smg.png", "cs2d/sprites/smg_d.png" },
    { WeaponId::Shotgun,     "霰弹枪", WeaponClass::Shotgun,WeaponSlotKind::Primary,   1700,  16.0f, 0.75f,  600.0f,  400.0f,  8, 0.140f, false,   8, 1.10f,  0.84f, false, 90.0f, "cs2d/sprites/shotgun.png", "cs2d/sprites/shotgun_d.png" },
    { WeaponId::AWP,         "AWP",   WeaponClass::Sniper, WeaponSlotKind::Primary,    4750, 115.0f, 1.45f, 1100.0f, 1200.0f,  1, 0.004f, false,   5, 3.16f,  0.72f, true,  90.0f, "cs2d/sprites/awp.png", "cs2d/sprites/awp_d.png" },
    { WeaponId::GrenadeHE,   "高爆雷", WeaponClass::Grenade,WeaponSlotKind::Grenade,    300,  98.0f, 1.00f,  420.0f,  250.0f,  1, 0.000f, false,   0, 0.0f,  0.95f, false, 90.0f, "cs2d/sprites/grenade_he.png", "cs2d/sprites/grenade_he_d.png" },
    { WeaponId::GrenadeFlash,"闪光弹", WeaponClass::Grenade,WeaponSlotKind::Grenade,    200,   0.0f, 1.00f,  420.0f,  250.0f,  1, 0.000f, false,   0, 0.0f,  0.95f, false, 90.0f, "cs2d/sprites/grenade_flash.png", "cs2d/sprites/grenade_flash_d.png" },
    { WeaponId::GrenadeSmoke,"烟雾弹", WeaponClass::Grenade,WeaponSlotKind::Grenade,    300,   0.0f, 1.00f,  420.0f,  250.0f,  1, 0.000f, false,   0, 0.0f,  0.95f, false, 90.0f, "cs2d/sprites/grenade_smoke.png", "cs2d/sprites/grenade_smoke_d.png" },
    { WeaponId::C4,          "C4",    WeaponClass::Bomb,   WeaponSlotKind::Melee,         0,   0.0f, 0.0f,    0.0f,    0.0f,  1, 0.00f, false,   0, 0.0f,  1.00f, false, 90.0f, nullptr, nullptr },
};

inline constexpr int kWeaponCount = static_cast<int>(sizeof(kWeapons) / sizeof(kWeapons[0]));

inline const WeaponDef& Get(WeaponId id) {
    const int idx = static_cast<int>(id);
    if (idx < 0 || idx >= kWeaponCount) return kWeapons[0];
    return kWeapons[idx];
}

inline bool IsGrenade(WeaponId id) {
    return Get(id).cls == WeaponClass::Grenade;
}

inline bool IsMelee(WeaponId id) {
    return Get(id).cls == WeaponClass::Melee;
}

} // namespace WeaponDB
