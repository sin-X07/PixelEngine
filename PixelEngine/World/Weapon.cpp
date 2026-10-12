#include "Weapon.hpp"

const char* Weapon::Name() const {
    switch (type) {
        case WeaponType::DAGGER: return "匕首";
        case WeaponType::KATANA: return "武士刀";
        case WeaponType::PISTOL: return "沙漠之鹰";
        case WeaponType::RIFLE:  return "AK47";
    }
    return "未知";
}

Weapon Weapon::MakeDagger() {
    Weapon w;
    w.type = WeaponType::DAGGER;
    w.range = 18.0f;
    w.cooldown = 0.28f;
    w.ranged = false;
    w.automatic = false;
    w.magazineSize = 0;
    w.ammo = 0;
    w.damage = 1;
    w.instaKill = false;
    w.walkPenalty = 0.0f;
    w.sprintPenalty = 0.0f;
    return w;
}

Weapon Weapon::MakeKatana() {
    Weapon w;
    w.type = WeaponType::KATANA;
    w.range = 40.0f;
    w.cooldown = 0.55f;
    w.ranged = false;
    w.automatic = false;
    w.magazineSize = 0;
    w.ammo = 0;
    w.damage = 3;
    w.instaKill = false;
    w.walkPenalty = 0.0f;
    w.sprintPenalty = 0.0f;
    return w;
}

Weapon Weapon::MakePistol() {
    Weapon w;
    w.type = WeaponType::PISTOL;
    w.range = 500.0f;
    w.cooldown = 0.5f;
    w.ranged = true;
    w.automatic = false;
    w.magazineSize = 7;
    w.ammo = 7;
    w.damage = 0;        // 伤害由 instaKill 体现
    w.instaKill = true;
    w.walkPenalty = 3.0f;
    w.sprintPenalty = 7.0f;
    return w;
}

Weapon Weapon::MakeRifle() {
    Weapon w;
    w.type = WeaponType::RIFLE;
    w.range = 500.0f;
    w.cooldown = 0.12f;
    w.ranged = true;
    w.automatic = true;
    w.magazineSize = 30;
    w.ammo = 30;
    w.damage = 2;
    w.instaKill = false;
    w.walkPenalty = 6.0f;
    w.sprintPenalty = 14.0f;
    return w;
}
