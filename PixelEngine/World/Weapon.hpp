#pragma once
#include <string>

// 武器类型枚举
enum class WeaponType {
    DAGGER,   // 匕首
    KATANA,   // 武士刀
    PISTOL,   // 沙漠之鹰
    RIFLE     // AK47
};

// 武器类：持有射程、冷却、弹夹、伤害、移速惩罚等属性
class Weapon {
public:
    WeaponType type = WeaponType::DAGGER;

    float range = 0.0f;         // 攻击范围（世界像素；近战为刀光距离，远程为子弹最大飞行距离）
    float cooldown = 0.0f;      // 攻击冷却（秒）
    bool ranged = false;        // 是否远程
    bool automatic = false;     // 是否连发（步枪）
    int magazineSize = 0;       // 弹夹容量
    int ammo = 0;               // 当前弹夹内子弹数
    int damage = 1;             // 单次攻击伤害
    bool instaKill = false;     // 是否无视血量一击必杀（沙漠之鹰）
    float walkPenalty = 0.0f;   // 拾起后降低步行最大移速
    float sprintPenalty = 0.0f; // 拾起后降低奔跑最大移速

    bool IsMelee() const { return !ranged; }
    bool HasAmmo() const { return !ranged || ammo > 0; }
    const char* Name() const;

    static Weapon MakeDagger();
    static Weapon MakeKatana();
    static Weapon MakePistol();
    static Weapon MakeRifle();
};

// 掉落在地图上的武器拾取物
struct WeaponDrop {
    WeaponType type = WeaponType::KATANA;
    float x = 0.0f;
    float y = 0.0f;
    int ammo = -1;              // 枪械为丢弃时剩余子弹数；近战为 -1（无弹夹）
    float pickupCooldown = 0.0f; // 刚被玩家丢弃后的一段冷却，避免被自动拾取立刻收回
};
