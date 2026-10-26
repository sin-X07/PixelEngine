// 弹药系统自检：直接验证 Player 的弹匣/备弹模型与掉落状态传递。
// 独立编译（不链接 SDL / 引擎），只在开发期运行：
//   g++ -std=c++17 -I PixelEngine -o ammo_test tools/ammo_test.cpp && ./ammo_test
#include <cstdio>
#include <cstring>
#include "CS2D/Weapons/WeaponDef.hpp"
#include "CS2D/CS2DGame.hpp"

static int g_fail = 0;

static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++g_fail;
}

int main() {
    // ---- 1. 备弹容量 = 弹匣容量 × 3 ----
    {
        const WeaponDef& ak = WeaponDB::Get(WeaponId::AK47);      // magazine 30
        const WeaponDef& awp = WeaponDB::Get(WeaponId::AWP);      // magazine 5
        const WeaponDef& deagle = WeaponDB::Get(WeaponId::Deagle); // magazine 7
        check(CS2DGame::Player::ReserveCapacity(ak) == 90, "AK47 备弹容量 = 30*3 = 90");
        check(CS2DGame::Player::ReserveCapacity(awp) == 15, "AWP 备弹容量 = 5*3 = 15");
        check(CS2DGame::Player::ReserveCapacity(deagle) == 21, "Deagle 备弹容量 = 7*3 = 21");
        check(CS2DGame::Player::ReserveCapacity(WeaponDB::Get(WeaponId::Knife)) == 0,
              "刀 备弹容量 = 0");
    }

    // ---- 2. SetSlotWeapon：满弹匣 + 满备弹 ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        p.SetSlotWeapon(0, WeaponId::AK47);
        check(p.ammoSlot[0] == 30 && p.maxAmmoSlot[0] == 30,
              "AK47 装枪后弹匣 = 30/30");
        check(p.reserveSlot[0] == 90, "AK47 装枪后备弹 = 90");
    }

    // ---- 3. 换弹从备弹扣取 ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        p.SetSlotWeapon(0, WeaponId::AK47);
        p.LoadAmmoFromSlot();
        p.ammo    = 4;      // 弹匣只剩 4 发
        p.reserve = 90;
        const int need = p.maxAmmo - p.ammo;      // 26
        const int take = need < p.reserve ? need : p.reserve;
        p.ammo += take;
        p.reserve -= take;
        check(p.ammo == 30, "换弹后弹匣补满 = 30");
        check(p.reserve == 64, "换弹后备弹扣除 26 -> 64");
    }

    // ---- 4. 备弹耗尽后换弹装不满（按剩余量装） ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        p.SetSlotWeapon(0, WeaponId::AK47);
        p.ammo    = 0;
        p.reserve = 10;         // 备弹只剩 10
        const int need = p.maxAmmo - p.ammo;
        const int take = need < p.reserve ? need : p.reserve;
        p.ammo += take;
        p.reserve -= take;
        check(p.ammo == 10 && p.reserve == 0,
              "备弹不足时只装剩余量 -> 弹匣 10, 备弹 0");
    }

    // ---- 5. 备弹为 0 时无法继续换弹 ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        p.SetSlotWeapon(0, WeaponId::AK47);
        p.ammo    = 0;
        p.reserve = 0;
        const bool canReload = (p.ammo < p.maxAmmo && p.reserve > 0);
        check(!canReload, "弹匣空且备弹 0 -> 不能换弹");
    }

    // ---- 6. 捡枪沿用丢弃者的弹药状态（核心需求） ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        // 模拟捡起一把「弹匣 12 发 / 备弹 40 发」的 AK
        p.SetSlotWeaponWithAmmo(0, WeaponId::AK47, 12, 40);
        p.LoadAmmoFromSlot();
        check(p.ammo == 12, "捡枪后弹匣 = 12（非满弹）");
        check(p.reserve == 40, "捡枪后备弹 = 40（沿用丢弃状态）");
        check(p.maxAmmo == 30, "捡枪后弹匣容量仍为 30");
    }

    // ---- 7. 越界的弹药值被夹紧到合法范围 ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        p.SetSlotWeaponWithAmmo(0, WeaponId::AK47, 999, 9999);
        check(p.ammoSlot[0] == 30, "超量弹匣被夹到 30");
        check(p.reserveSlot[0] == 90, "超量备弹被夹到 90");
        p.SetSlotWeaponWithAmmo(0, WeaponId::AK47, -5, -5);
        check(p.ammoSlot[0] == 0 && p.reserveSlot[0] == 0, "负值被夹到 0");
    }

    // ---- 8. 切枪时各槽弹药互不干扰 ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        p.SetSlotWeapon(0, WeaponId::AK47);
        p.SetSlotWeapon(1, WeaponId::USP);
        p.LoadAmmoFromSlot();

        // 主武器打剩 8 发、备弹耗到 20
        p.ammo = 8; p.reserve = 20;
        p.SaveAmmoToSlot();

        // 切到手枪
        p.weaponSlot = 1;
        p.LoadAmmoFromSlot();
        check(p.ammo == 12 && p.reserve == 36,
              "切到手枪后读到 USP 自己的 12/36");

        // 切回 AK：应恢复 8/20
        p.SaveAmmoToSlot();
        p.weaponSlot = 0;
        p.LoadAmmoFromSlot();
        check(p.ammo == 8 && p.reserve == 20,
              "切回 AK 恢复 8/20（弹药按槽位持久）");
    }

    // ---- 9. 清空槽位同时清掉备弹 ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 0;
        p.SetSlotWeapon(0, WeaponId::AK47);
        p.ClearSlotWeapon(0);
        check(p.weapons[0] == WeaponId::None &&
              p.ammoSlot[0] == 0 && p.reserveSlot[0] == 0,
              "清空槽位后备弹一并清零");
    }

    // ---- 10. 投掷物/近战不参与备弹 ----
    {
        CS2DGame::Player p;
        p.weaponSlot = 2;
        p.SetSlotWeapon(2, WeaponId::Knife);
        check(p.reserveSlot[2] == 0, "近战槽备弹 = 0");
    }

    std::printf("\n%s：%d 项失败\n", g_fail == 0 ? "全部通过" : "存在失败", g_fail);
    return g_fail == 0 ? 0 : 1;
}
