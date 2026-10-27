// CS2 经济规则 + 武器表自检
// 验证：起始金/胜利奖励/连败补偿/按武器类型击杀奖励/阵营隔离/起始手枪
//
// 编译（需要 SDL 头文件，借用构建目录）：
//   g++ -std=c++17 -DSDL_MAIN_HANDLED -I PixelEngine -I <SDL2inc> -I <SDL2inc>/SDL2 \
//       tools/cs2_economy_test.cpp -o economy_test.exe
#include <cstdio>
#include <cstring>
#include "CS2D/Weapons/WeaponDef.hpp"
#include "CS2D/Economy/EconomySystem.hpp"
#include "Core/Types.hpp"

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++g_fail;
}

int main() {
    std::printf("===== CS2 经济数值 =====\n");
    EconomySystem eco;

    check(eco.StartMoney() == 800, "起始金 = $800");
    check(eco.Config().maxMoney == 16000, "上限 = $16000");
    check(eco.Config().winReward == 3250, "胜利奖励 = $3250");

    // 连败补偿：1400 / 1900 / 2400 / 2900 / 3400(封顶)
    {
        EconomySystem e;
        const int expect[6] = { 1400, 1900, 2400, 2900, 3400, 3400 };
        for (int i = 0; i < 6; ++i) {
            const int got = e.GrantRoundEnd(Team::CT, Team::T);  // T 连败
            char buf[96];
            std::snprintf(buf, sizeof(buf), "T 第 %d 次连败补偿 = $%d (期望 $%d)",
                          i + 1, got, expect[i]);
            check(got == expect[i], buf);
        }
    }
    // 胜利后连败清零
    {
        EconomySystem e;
        e.GrantRoundEnd(Team::CT, Team::T);   // 败 1
        e.GrantRoundEnd(Team::CT, Team::T);   // 败 2
        check(e.LossStreak(Team::T) == 2, "T 连败计数 = 2");
        e.GrantRoundEnd(Team::T, Team::T);    // 胜
        check(e.LossStreak(Team::T) == 0, "胜利后连败计数清零");
        check(e.GrantRoundEnd(Team::CT, Team::T) == 1400, "清零后再败回到 $1400");
    }
    // T / CT 连败独立
    {
        EconomySystem e;
        e.GrantRoundEnd(Team::CT, Team::T);   // T 败 1
        check(e.LossStreak(Team::T) == 1 && e.LossStreak(Team::CT) == 0,
              "T/CT 连败计数互相独立");
    }

    std::printf("\n===== 目标奖励 =====\n");
    check(eco.GrantPlant() == 300,  "下包奖励 = $300/人");
    check(eco.GrantDefuse() == 300, "拆包奖励 = $300/个人");

    std::printf("\n===== 按武器类型的击杀奖励 =====\n");
    struct { WeaponId id; int want; const char* name; } kills[] = {
        { WeaponId::AK47,        300,  "AK-47 (步枪) $300" },
        { WeaponId::M4A4,        300,  "M4A4 (步枪) $300" },
        { WeaponId::Glock18,     300,  "Glock-18 (手枪) $300" },
        { WeaponId::Deagle,      300,  "Deagle (手枪) $300" },
        { WeaponId::MAC10,       600,  "MAC-10 (SMG) $600" },
        { WeaponId::MP9,         600,  "MP9 (SMG) $600" },
        { WeaponId::P90,         300,  "P90 (SMG 特例) $300" },
        { WeaponId::Nova,        900,  "Nova (霰弹枪) $900" },
        { WeaponId::XM1014,      900,  "XM1014 (霰弹枪) $900" },
        { WeaponId::AWP,         100,  "AWP (狙击) $100" },
        { WeaponId::SSG08,       100,  "SSG 08 (狙击) $100" },
        { WeaponId::Knife,       1500, "刀 $1500" },
        { WeaponId::M249,        300,  "M249 (机枪) $300" },
        { WeaponId::GrenadeHE,   300,  "高爆雷 $300" },
        { WeaponId::Zeus,        0,    "Zeus $0" },
    };
    for (auto& k : kills) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s → 实际 $%d", k.name,
                      WeaponDB::KillReward(k.id));
        check(WeaponDB::KillReward(k.id) == k.want, buf);
    }

    std::printf("\n===== 武器价格（照抄 CS2）=====\n");
    struct { WeaponId id; int want; const char* name; } prices[] = {
        { WeaponId::AK47,    2700, "AK-47 $2700" },
        { WeaponId::M4A4,    3100, "M4A4 $3100" },
        { WeaponId::M4A1S,   2900, "M4A1-S $2900" },
        { WeaponId::AWP,     4750, "AWP $4750" },
        { WeaponId::Deagle,   700, "Deagle $700" },
        { WeaponId::Glock18,  200, "Glock-18 $200" },
        { WeaponId::USP_S,    200, "USP-S $200" },
        { WeaponId::P250,     300, "P250 $300" },
        { WeaponId::FiveSeven,500, "Five-SeveN $500" },
        { WeaponId::Tec9,     500, "Tec-9 $500" },
        { WeaponId::MAC10,   1050, "MAC-10 $1050" },
        { WeaponId::MP9,     1250, "MP9 $1250" },
        { WeaponId::UMP45,   1200, "UMP-45 $1200" },
        { WeaponId::P90,     2350, "P90 $2350" },
        { WeaponId::GalilAR, 1800, "Galil AR $1800" },
        { WeaponId::FAMAS,   2050, "FAMAS $2050" },
        { WeaponId::SG553,   3000, "SG 553 $3000" },
        { WeaponId::AUG,     3300, "AUG $3300" },
        { WeaponId::SSG08,   1700, "SSG 08 $1700" },
        { WeaponId::Nova,    1050, "Nova $1050" },
        { WeaponId::XM1014,  2000, "XM1014 $2000" },
        { WeaponId::SawedOff,1100, "Sawed-Off $1100" },
        { WeaponId::MAG7,    1800, "MAG-7 $1800" },
        { WeaponId::M249,    5200, "M249 $5200" },
        { WeaponId::Negev,   1700, "Negev $1700" },
        { WeaponId::GrenadeHE,   300, "高爆雷 $300" },
        { WeaponId::GrenadeFlash,200, "闪光弹 $200" },
        { WeaponId::GrenadeSmoke,300, "烟雾弹 $300" },
        { WeaponId::GrenadeMolotov,  400, "燃烧瓶 $400" },
        { WeaponId::GrenadeIncendiary, 600, "燃烧弹 $600" },
        { WeaponId::GrenadeDecoy,     50,  "诱饵弹 $50" },
    };
    for (auto& p : prices) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s → 实际 $%d", p.name,
                      WeaponDB::Get(p.id).price);
        check(WeaponDB::Get(p.id).price == p.want, buf);
    }

    std::printf("\n===== 阵营隔离 =====\n");
    // 必须是 T 专属
    check(WeaponDB::TeamAllowed(WeaponId::AK47, true) &&
          !WeaponDB::TeamAllowed(WeaponId::AK47, false), "AK-47 仅 T 可买");
    check(WeaponDB::TeamAllowed(WeaponId::SG553, true) &&
          !WeaponDB::TeamAllowed(WeaponId::SG553, false), "SG 553 仅 T 可买");
    check(WeaponDB::TeamAllowed(WeaponId::GalilAR, true) &&
          !WeaponDB::TeamAllowed(WeaponId::GalilAR, false), "Galil AR 仅 T 可买");
    check(WeaponDB::TeamAllowed(WeaponId::MAC10, true) &&
          !WeaponDB::TeamAllowed(WeaponId::MAC10, false), "MAC-10 仅 T 可买");
    check(WeaponDB::TeamAllowed(WeaponId::Tec9, true) &&
          !WeaponDB::TeamAllowed(WeaponId::Tec9, false), "Tec-9 仅 T 可买");
    check(WeaponDB::TeamAllowed(WeaponId::GrenadeMolotov, true) &&
          !WeaponDB::TeamAllowed(WeaponId::GrenadeMolotov, false), "燃烧瓶 仅 T 可买");

    // 必须是 CT 专属
    check(!WeaponDB::TeamAllowed(WeaponId::M4A4, true) &&
          WeaponDB::TeamAllowed(WeaponId::M4A4, false), "M4A4 仅 CT 可买");
    check(!WeaponDB::TeamAllowed(WeaponId::M4A1S, true) &&
          WeaponDB::TeamAllowed(WeaponId::M4A1S, false), "M4A1-S 仅 CT 可买");
    check(!WeaponDB::TeamAllowed(WeaponId::AUG, true) &&
          WeaponDB::TeamAllowed(WeaponId::AUG, false), "AUG 仅 CT 可买");
    check(!WeaponDB::TeamAllowed(WeaponId::FAMAS, true) &&
          WeaponDB::TeamAllowed(WeaponId::FAMAS, false), "FAMAS 仅 CT 可买");
    check(!WeaponDB::TeamAllowed(WeaponId::MP9, true) &&
          WeaponDB::TeamAllowed(WeaponId::MP9, false), "MP9 仅 CT 可买");
    check(!WeaponDB::TeamAllowed(WeaponId::FiveSeven, true) &&
          WeaponDB::TeamAllowed(WeaponId::FiveSeven, false), "Five-SeveN 仅 CT 可买");
    check(!WeaponDB::TeamAllowed(WeaponId::GrenadeIncendiary, true) &&
          WeaponDB::TeamAllowed(WeaponId::GrenadeIncendiary, false), "燃烧弹 仅 CT 可买");

    // 双方通用
    check(WeaponDB::TeamAllowed(WeaponId::AWP, true) &&
          WeaponDB::TeamAllowed(WeaponId::AWP, false), "AWP 双方通用");
    check(WeaponDB::TeamAllowed(WeaponId::Deagle, true) &&
          WeaponDB::TeamAllowed(WeaponId::Deagle, false), "Deagle 双方通用");
    check(WeaponDB::TeamAllowed(WeaponId::P250, true) &&
          WeaponDB::TeamAllowed(WeaponId::P250, false), "P250 双方通用");
    check(WeaponDB::TeamAllowed(WeaponId::GrenadeHE, true) &&
          WeaponDB::TeamAllowed(WeaponId::GrenadeHE, false), "高爆雷 双方通用");

    std::printf("\n===== 起始手枪 =====\n");
    check(WeaponDB::IsDefaultPistol(WeaponId::Glock18, true),  "T 起始手枪 = Glock-18");
    check(WeaponDB::IsDefaultPistol(WeaponId::USP_S, false),   "CT 起始手枪 = USP-S");
    check(WeaponDB::IsDefaultPistol(WeaponId::P2000, false),   "CT 起始手枪 = P2000（可选）");
    check(!WeaponDB::IsDefaultPistol(WeaponId::AK47, true),    "AK-47 不是起始手枪");

    std::printf("\n%s：%d 项失败\n", g_fail == 0 ? "全部通过" : "存在失败", g_fail);
    return g_fail == 0 ? 0 : 1;
}
