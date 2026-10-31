#pragma once
#include <cstdint>

// ============================================================================
// WeaponDef — 数据驱动武器表（照抄 CS2 原版）
//
// 单一数据源：玩法（伤害/射速/弹匣）、渲染（贴图/朝向偏移）、音效、经济（价格/击杀奖励）
// 全部由此表驱动。新增武器 = 在下方 kWeapons 表加一行 + 补素材。
//
// 阵营隔离：WeaponTeam 标记该武器只能在哪个阵营购买（None 表示不限）。
// 击杀奖励：killReward 为该武器击杀时发放的金额（照抄 CS2：步枪$300 / AWP$100 /
// 霰弹枪$900 / 刀$1500 / 手雷$300 / 手枪$300 / SMG$600(P90$300) / 机枪$300）。
// ============================================================================

enum class WeaponId : int {
    None = 0,
    Knife,

    // ---- 手枪 ----
    Glock18,        // T 起始
    USP_S,          // CT 起始
    P2000,          // CT 起始（与 USP-S 二选一）
    DualBerettas,
    P250,
    FiveSeven,      // CT
    Tec9,           // T
    CZ75,
    Deagle,
    R8Revolver,
    Zeus,           // 电击枪（1 发，当回合用完即空）

    // ---- 冲锋枪 ----
    MAC10,          // T
    MP9,            // CT
    MP7,
    MP5SD,
    UMP45,
    P90,

    // ---- 步枪 ----
    GalilAR,        // T
    FAMAS,          // CT
    AK47,           // T
    M4A4,           // CT
    M4A1S,          // CT
    SG553,          // T
    AUG,            // CT

    // ---- 狙击枪 ----
    SSG08,
    AWP,
    SCAR20,         // CT 自动狙
    G3SG1,          // T 自动狙

    // ---- 霰弹枪 ----
    Nova,
    XM1014,
    SawedOff,       // T
    MAG7,           // CT

    // ---- 机枪 ----
    M249,
    Negev,

    // ---- 投掷物 ----
    GrenadeHE,
    GrenadeFlash,
    GrenadeSmoke,
    GrenadeMolotov,     // T 燃烧瓶
    GrenadeIncendiary,  // CT 燃烧弹
    GrenadeDecoy,

    // ---- 任务物品 ----
    C4,

    Count
};

enum class WeaponClass { Melee, Pistol, SMG, Rifle, Sniper, Shotgun, MachineGun, Grenade, Bomb, ZeusGun };

// 武器栏位
enum class WeaponSlotKind { Primary, Secondary, Melee, Grenade };

// 阵营归属
enum class WeaponTeam { Both = 0, T, CT };

struct WeaponDef {
    WeaponId        id;
    const char*     name;          // 显示名
    WeaponClass     cls;
    WeaponSlotKind  slot;
    WeaponTeam      team;          // 阵营归属（Both = 双方都能买）
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
    const char* iconPath;

    // 经济：用本武器击杀时发放的金额（照抄 CS2）
    int   killReward;
};

namespace WeaponDB {

// CS2 击杀奖励（按武器类型）
//   步枪/手枪/机枪/手雷 = $300
//   SMG = $600（P90 特殊：$300）
//   霰弹枪 = $900
//   刀 = $1500
//   狙击枪 = $100
//   宙斯 = $0
inline constexpr int kKillRewardRifle     = 300;
inline constexpr int kKillRewardPistol    = 300;
inline constexpr int kKillRewardSMG       = 600;
inline constexpr int kKillRewardSMGP90    = 300;
inline constexpr int kKillRewardShotgun   = 900;
inline constexpr int kKillRewardKnife     = 1500;
inline constexpr int kKillRewardSniper    = 100;
inline constexpr int kKillRewardGrenade   = 300;
inline constexpr int kKillRewardMG        = 300;
inline constexpr int kKillRewardZeus      = 0;

// 武器表（唯一数据源）—— 价格照抄 CS2 2024
// 列：id, name, cls, slot, team, price, dmg, cd, spd, range, pellets, spread, auto, mag, reload, spdMul, scoped, deg, sprite, icon, killReward
inline const WeaponDef kWeapons[] = {
    // ---------------- None / 刀 ----------------
    { WeaponId::None, "-", WeaponClass::Melee, WeaponSlotKind::Melee, WeaponTeam::Both,
      0, 0.0f, 0.0f, 0.0f, 0.0f, 1, 0.00f, false, 0, 0.0f, 1.00f, false, 90.0f, nullptr, nullptr, 0 },
    { WeaponId::Knife, "匕首", WeaponClass::Melee, WeaponSlotKind::Melee, WeaponTeam::Both,
      0, 50.0f, 0.50f, 0.0f, 45.0f, 1, 0.00f, false, 0, 0.0f, 1.00f, false, 90.0f,
      "cs2d/sprites/knife.png", "cs2d/sprites/knife_d.png", kKillRewardKnife },

    // ---------------- 手枪 ----------------
    { WeaponId::Glock18, "Glock-18", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::T,
      200, 28.0f, 0.18f, 700.0f, 700.0f, 1, 0.030f, false, 20, 1.99f, 0.92f, false, 0.0f,
      "cs2d/sprites/glock.png", "cs2d/sprites/glock_d.png", kKillRewardPistol },
    { WeaponId::USP_S, "USP-S", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::CT,
      200, 35.0f, 0.16f, 720.0f, 700.0f, 1, 0.026f, false, 12, 1.49f, 0.92f, false, 0.0f,
      "cs2d/sprites/usp.png", "cs2d/sprites/usp_d.png", kKillRewardPistol },
    { WeaponId::P2000, "P2000", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::CT,
      200, 35.0f, 0.17f, 720.0f, 700.0f, 1, 0.028f, false, 13, 1.55f, 0.92f, false, 0.0f,
      "cs2d/sprites/p2000.png", "cs2d/sprites/p2000_d.png", kKillRewardPistol },
    { WeaponId::DualBerettas, "Dual Berettas", WeaponClass::Pistol, WeaponSlotKind::Secondary,
      WeaponTeam::Both, 300, 38.0f, 0.12f, 700.0f, 700.0f, 1, 0.035f, true, 30, 2.10f, 0.90f, false, 0.0f,
      "cs2d/sprites/dualberettas.png", "cs2d/sprites/dualberettas_d.png", kKillRewardPistol },
    { WeaponId::P250, "P250", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::Both,
      300, 35.0f, 0.17f, 700.0f, 700.0f, 1, 0.028f, false, 13, 1.60f, 0.92f, false, 0.0f,
      "cs2d/sprites/p250.png", "cs2d/sprites/p250_d.png", kKillRewardPistol },
    { WeaponId::FiveSeven, "Five-SeveN", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::CT,
      500, 32.0f, 0.15f, 720.0f, 700.0f, 1, 0.026f, false, 20, 1.70f, 0.92f, false, 0.0f,
      "cs2d/sprites/fn57.png", "cs2d/sprites/fn57_d.png", kKillRewardPistol },
    { WeaponId::Tec9, "Tec-9", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::T,
      500, 33.0f, 0.13f, 720.0f, 700.0f, 1, 0.032f, false, 18, 1.80f, 0.92f, false, 0.0f,
      "cs2d/sprites/tec9.png", "cs2d/sprites/tec9_d.png", kKillRewardPistol },
    { WeaponId::CZ75, "CZ75-Auto", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::Both,
      500, 31.0f, 0.10f, 700.0f, 700.0f, 1, 0.038f, true, 12, 2.00f, 0.90f, false, 0.0f,
      "cs2d/sprites/cz75.png", "cs2d/sprites/cz75_d.png", kKillRewardPistol },
    { WeaponId::Deagle, "Desert Eagle", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::Both,
      700, 63.0f, 0.40f, 700.0f, 700.0f, 1, 0.020f, false, 7, 1.31f, 0.88f, false, 0.0f,
      "cs2d/sprites/deagle.png", "cs2d/sprites/deagle_d.png", kKillRewardPistol },
    { WeaponId::R8Revolver, "R8 Revolver", WeaponClass::Pistol, WeaponSlotKind::Secondary, WeaponTeam::Both,
      600, 86.0f, 0.55f, 700.0f, 700.0f, 1, 0.016f, false, 8, 2.20f, 0.88f, false, 0.0f,
      "cs2d/sprites/r8.png", "cs2d/sprites/r8_d.png", kKillRewardPistol },
    // 宙斯：1 发，无备弹；下回合自动补满（照抄 CS2）
    { WeaponId::Zeus, "Zeus x27", WeaponClass::ZeusGun, WeaponSlotKind::Secondary, WeaponTeam::Both,
      200, 255.0f, 1.00f, 2000.0f, 60.0f, 1, 0.000f, false, 1, 0.00f, 0.95f, false, 0.0f,
      "cs2d/sprites/zeus.png", "cs2d/sprites/zeus_d.png", kKillRewardZeus },

    // ---------------- 冲锋枪 ----------------
    { WeaponId::MAC10, "MAC-10", WeaponClass::SMG, WeaponSlotKind::Primary, WeaponTeam::T,
      1050, 29.0f, 0.075f, 680.0f, 650.0f, 1, 0.055f, true, 30, 1.83f, 0.86f, false, 90.0f,
      "cs2d/sprites/mac-10.png", "cs2d/sprites/mac-10_d.png", kKillRewardSMG },
    { WeaponId::MP9, "MP9", WeaponClass::SMG, WeaponSlotKind::Primary, WeaponTeam::CT,
      1250, 26.0f, 0.070f, 680.0f, 650.0f, 1, 0.052f, true, 30, 1.80f, 0.86f, false, 90.0f,
      "cs2d/sprites/mp9.png", "cs2d/sprites/mp9_d.png", kKillRewardSMG },
    { WeaponId::MP7, "MP7", WeaponClass::SMG, WeaponSlotKind::Primary, WeaponTeam::Both,
      1500, 30.0f, 0.075f, 680.0f, 650.0f, 1, 0.052f, true, 30, 1.85f, 0.86f, false, 90.0f,
      "cs2d/sprites/mp7.png", "cs2d/sprites/mp7_d.png", kKillRewardSMG },
    { WeaponId::MP5SD, "MP5-SD", WeaponClass::SMG, WeaponSlotKind::Primary, WeaponTeam::Both,
      1500, 27.0f, 0.075f, 680.0f, 650.0f, 1, 0.050f, true, 30, 1.83f, 0.86f, false, 90.0f,
      "cs2d/sprites/mp5sd.png", "cs2d/sprites/mp5sd_d.png", kKillRewardSMG },
    { WeaponId::UMP45, "UMP-45", WeaponClass::SMG, WeaponSlotKind::Primary, WeaponTeam::Both,
      1200, 35.0f, 0.090f, 660.0f, 650.0f, 1, 0.055f, true, 25, 1.90f, 0.86f, false, 90.0f,
      "cs2d/sprites/ump45.png", "cs2d/sprites/ump45_d.png", kKillRewardSMG },
    { WeaponId::P90, "P90", WeaponClass::SMG, WeaponSlotKind::Primary, WeaponTeam::Both,
      2350, 26.0f, 0.065f, 690.0f, 650.0f, 1, 0.058f, true, 50, 2.10f, 0.84f, false, 90.0f,
      "cs2d/sprites/p90.png", "cs2d/sprites/p90_d.png", kKillRewardSMGP90 },

    // ---------------- 步枪 ----------------
    { WeaponId::GalilAR, "Galil AR", WeaponClass::Rifle, WeaponSlotKind::Primary, WeaponTeam::T,
      1800, 30.0f, 0.11f, 640.0f, 800.0f, 1, 0.045f, true, 35, 1.83f, 0.80f, false, 90.0f,
      "cs2d/sprites/galil.png", "cs2d/sprites/galil_d.png", kKillRewardRifle },
    { WeaponId::FAMAS, "FAMAS", WeaponClass::Rifle, WeaponSlotKind::Primary, WeaponTeam::CT,
      2050, 30.0f, 0.105f, 640.0f, 800.0f, 1, 0.044f, true, 25, 1.80f, 0.80f, false, 90.0f,
      "cs2d/sprites/famas.png", "cs2d/sprites/famas_d.png", kKillRewardRifle },
    { WeaponId::AK47, "AK-47", WeaponClass::Rifle, WeaponSlotKind::Primary, WeaponTeam::T,
      2700, 36.0f, 0.10f, 620.0f, 800.0f, 1, 0.050f, true, 30, 2.74f, 0.80f, false, 90.0f,
      "cs2d/sprites/ak47.png", "cs2d/sprites/ak47_d.png", kKillRewardRifle },
    { WeaponId::M4A4, "M4A4", WeaponClass::Rifle, WeaponSlotKind::Primary, WeaponTeam::CT,
      3100, 33.0f, 0.09f, 640.0f, 800.0f, 1, 0.045f, true, 30, 1.90f, 0.80f, false, 90.0f,
      "cs2d/sprites/m4a4.png", "cs2d/sprites/m4a4_d.png", kKillRewardRifle },
    { WeaponId::M4A1S, "M4A1-S", WeaponClass::Rifle, WeaponSlotKind::Primary, WeaponTeam::CT,
      2900, 38.0f, 0.10f, 640.0f, 800.0f, 1, 0.038f, true, 20, 1.75f, 0.80f, false, 90.0f,
      "cs2d/sprites/m4a1.png", "cs2d/sprites/m4a1_d.png", kKillRewardRifle },
    { WeaponId::SG553, "SG 553", WeaponClass::Rifle, WeaponSlotKind::Primary, WeaponTeam::T,
      3000, 30.0f, 0.095f, 640.0f, 800.0f, 1, 0.042f, true, 30, 2.00f, 0.78f, true, 90.0f,
      "cs2d/sprites/sg553.png", "cs2d/sprites/sg553_d.png", kKillRewardRifle },
    { WeaponId::AUG, "AUG", WeaponClass::Rifle, WeaponSlotKind::Primary, WeaponTeam::CT,
      3300, 28.0f, 0.09f, 640.0f, 800.0f, 1, 0.040f, true, 30, 2.05f, 0.78f, true, 90.0f,
      "cs2d/sprites/aug.png", "cs2d/sprites/aug_d.png", kKillRewardRifle },

    // ---------------- 狙击枪 ----------------
    { WeaponId::SSG08, "SSG 08", WeaponClass::Sniper, WeaponSlotKind::Primary, WeaponTeam::Both,
      1700, 88.0f, 1.20f, 1000.0f, 1100.0f, 1, 0.006f, false, 10, 2.40f, 0.78f, true, 90.0f,
      "cs2d/sprites/ssg08.png", "cs2d/sprites/ssg08_d.png", kKillRewardSniper },
    { WeaponId::AWP, "AWP", WeaponClass::Sniper, WeaponSlotKind::Primary, WeaponTeam::Both,
      4750, 115.0f, 1.45f, 1100.0f, 1200.0f, 1, 0.004f, false, 5, 3.16f, 0.72f, true, 90.0f,
      "cs2d/sprites/awp.png", "cs2d/sprites/awp_d.png", kKillRewardSniper },
    { WeaponId::SCAR20, "SCAR-20", WeaponClass::Sniper, WeaponSlotKind::Primary, WeaponTeam::CT,
      5000, 80.0f, 0.25f, 1000.0f, 1200.0f, 1, 0.010f, true, 20, 3.00f, 0.74f, true, 90.0f,
      "cs2d/sprites/scar20.png", "cs2d/sprites/scar20_d.png", kKillRewardSniper },
    { WeaponId::G3SG1, "G3SG1", WeaponClass::Sniper, WeaponSlotKind::Primary, WeaponTeam::T,
      5000, 80.0f, 0.25f, 1000.0f, 1200.0f, 1, 0.010f, true, 20, 3.00f, 0.74f, true, 90.0f,
      "cs2d/sprites/g3sg1.png", "cs2d/sprites/g3sg1_d.png", kKillRewardSniper },

    // ---------------- 霰弹枪 ----------------
    { WeaponId::Nova, "Nova", WeaponClass::Shotgun, WeaponSlotKind::Primary, WeaponTeam::Both,
      1050, 26.0f, 0.75f, 600.0f, 400.0f, 8, 0.140f, false, 8, 1.10f, 0.84f, false, 90.0f,
      "cs2d/sprites/nova.png", "cs2d/sprites/nova_d.png", kKillRewardShotgun },
    { WeaponId::XM1014, "XM1014", WeaponClass::Shotgun, WeaponSlotKind::Primary, WeaponTeam::Both,
      2000, 20.0f, 0.30f, 600.0f, 400.0f, 8, 0.150f, true, 7, 1.50f, 0.84f, false, 90.0f,
      "cs2d/sprites/xm1014.png", "cs2d/sprites/xm1014_d.png", kKillRewardShotgun },
    { WeaponId::SawedOff, "Sawed-Off", WeaponClass::Shotgun, WeaponSlotKind::Primary, WeaponTeam::T,
      1100, 32.0f, 0.85f, 600.0f, 350.0f, 8, 0.160f, false, 7, 1.20f, 0.84f, false, 90.0f,
      "cs2d/sprites/sawedoff.png", "cs2d/sprites/sawedoff_d.png", kKillRewardShotgun },
    { WeaponId::MAG7, "MAG-7", WeaponClass::Shotgun, WeaponSlotKind::Primary, WeaponTeam::CT,
      1800, 30.0f, 0.80f, 600.0f, 380.0f, 8, 0.150f, false, 5, 1.15f, 0.84f, false, 90.0f,
      "cs2d/sprites/mag7.png", "cs2d/sprites/mag7_d.png", kKillRewardShotgun },

    // ---------------- 机枪 ----------------
    { WeaponId::M249, "M249", WeaponClass::MachineGun, WeaponSlotKind::Primary, WeaponTeam::Both,
      5200, 32.0f, 0.08f, 660.0f, 800.0f, 1, 0.060f, true, 100, 4.50f, 0.72f, false, 90.0f,
      "cs2d/sprites/m249.png", "cs2d/sprites/m249_d.png", kKillRewardMG },
    { WeaponId::Negev, "Negev", WeaponClass::MachineGun, WeaponSlotKind::Primary, WeaponTeam::Both,
      1700, 35.0f, 0.07f, 660.0f, 800.0f, 1, 0.058f, true, 150, 4.80f, 0.72f, false, 90.0f,
      "cs2d/sprites/negev.png", "cs2d/sprites/negev_d.png", kKillRewardMG },

    // ---------------- 投掷物 ----------------
    { WeaponId::GrenadeHE, "高爆雷", WeaponClass::Grenade, WeaponSlotKind::Grenade, WeaponTeam::Both,
      300, 98.0f, 1.00f, 420.0f, 250.0f, 1, 0.000f, false, 0, 0.0f, 0.95f, false, 90.0f,
      "cs2d/sprites/grenade_he.png", "cs2d/sprites/grenade_he_d.png", kKillRewardGrenade },
    { WeaponId::GrenadeFlash, "闪光弹", WeaponClass::Grenade, WeaponSlotKind::Grenade, WeaponTeam::Both,
      200, 0.0f, 1.00f, 420.0f, 250.0f, 1, 0.000f, false, 0, 0.0f, 0.95f, false, 90.0f,
      "cs2d/sprites/grenade_flash.png", "cs2d/sprites/grenade_flash_d.png", kKillRewardGrenade },
    { WeaponId::GrenadeSmoke, "烟雾弹", WeaponClass::Grenade, WeaponSlotKind::Grenade, WeaponTeam::Both,
      300, 0.0f, 1.00f, 420.0f, 250.0f, 1, 0.000f, false, 0, 0.0f, 0.95f, false, 90.0f,
      "cs2d/sprites/grenade_smoke.png", "cs2d/sprites/grenade_smoke_d.png", kKillRewardGrenade },
    { WeaponId::GrenadeMolotov, "燃烧瓶", WeaponClass::Grenade, WeaponSlotKind::Grenade, WeaponTeam::T,
      400, 0.0f, 1.00f, 420.0f, 250.0f, 1, 0.000f, false, 0, 0.0f, 0.95f, false, 90.0f,
      "cs2d/sprites/grenade_molotov.png", "cs2d/sprites/grenade_molotov_d.png", kKillRewardGrenade },
    { WeaponId::GrenadeIncendiary, "燃烧弹", WeaponClass::Grenade, WeaponSlotKind::Grenade, WeaponTeam::CT,
      600, 0.0f, 1.00f, 420.0f, 250.0f, 1, 0.000f, false, 0, 0.0f, 0.95f, false, 90.0f,
      "cs2d/sprites/grenade_incendiary.png", "cs2d/sprites/grenade_incendiary_d.png", kKillRewardGrenade },
    { WeaponId::GrenadeDecoy, "诱饵弹", WeaponClass::Grenade, WeaponSlotKind::Grenade, WeaponTeam::Both,
      50, 0.0f, 1.00f, 420.0f, 250.0f, 1, 0.000f, false, 0, 0.0f, 0.95f, false, 90.0f,
      "cs2d/sprites/grenade_decoy.png", "cs2d/sprites/grenade_decoy_d.png", kKillRewardGrenade },

    // ---------------- C4 ----------------
    { WeaponId::C4, "C4", WeaponClass::Bomb, WeaponSlotKind::Melee, WeaponTeam::Both,
      0, 0.0f, 0.0f, 0.0f, 0.0f, 1, 0.00f, false, 0, 0.0f, 1.00f, false, 90.0f,
      nullptr, nullptr, 0 },
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

// 阵营可用性：isTerrorist=true 表示 T 方
inline bool TeamAllowed(WeaponId id, bool isTerrorist) {
    const WeaponTeam t = Get(id).team;
    if (t == WeaponTeam::Both) return true;
    return isTerrorist ? (t == WeaponTeam::T) : (t == WeaponTeam::CT);
}

// 护甲穿透（0..1，来自 CS2 官方 Armor% 列，与 kWeapons 同索引）
// 来源：https://counterstrike.codexery.com/weapons （Dmg / Armor% 两列）
// 说明：表里没有的项按武器类别取值——近战 0.85（刀）、投掷物 0.5（高爆）/0（其它）、
//       C4 1.0；Zeus 无视护甲（1.0）。
inline constexpr float kArmorPen[] = {
    // None, Knife
    1.00f, 0.85f,
    // Glock-18, USP-S, P2000, Dual Berettas, P250, Five-SeveN, Tec-9, CZ75-Auto, Deagle, R8, Zeus
    0.470f, 0.585f, 0.585f, 0.742f, 0.644f, 0.915f, 0.904f, 0.794f, 0.932f, 0.932f, 1.00f,
    // MAC-10, MP9, MP7, MP5-SD, UMP-45, P90
    0.575f, 0.600f, 0.625f, 0.625f, 0.650f, 0.690f,
    // Galil AR, FAMAS, AK-47, M4A4, M4A1-S, SG 553, AUG
    0.775f, 0.700f, 0.775f, 0.700f, 0.700f, 1.000f, 0.900f,
    // SSG 08, AWP, SCAR-20, G3SG1
    0.850f, 0.975f, 0.820f, 0.820f,
    // Nova, XM1014, Sawed-Off, MAG-7
    0.500f, 0.800f, 0.500f, 0.600f,
    // M249, Negev
    0.800f, 0.750f,
    // 高爆雷, 闪光弹, 烟雾弹, 燃烧瓶, 燃烧弹, 诱饵弹
    0.500f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    // C4
    1.00f,
};
inline constexpr int kArmorPenCount =
    static_cast<int>(sizeof(kArmorPen) / sizeof(kArmorPen[0]));

// 护甲穿透（0..1）；非法/越界返回 1.0（不额外减免，避免除零或误伤）
inline float ArmorPen(WeaponId id) {
    const int idx = static_cast<int>(id);
    if (idx < 0 || idx >= kArmorPenCount) return 1.0f;
    return kArmorPen[idx];
}

// 击杀奖励（按武器查表）
inline int KillReward(WeaponId id) {
    return Get(id).killReward;
}

// 是否为「起始手枪」（掉地时不需要掉落）
inline bool IsDefaultPistol(WeaponId id, bool isTerrorist) {
    return isTerrorist ? (id == WeaponId::Glock18)
                       : (id == WeaponId::USP_S || id == WeaponId::P2000);
}

} // namespace WeaponDB
