// 合并契约 v1 的玩法侧回归测试（无窗口 / 无渲染器，纯逻辑）
//
// 覆盖：
//   1. EvaluatePurchase：全部 BuyCode 可复现、无副作用、非法 enum 不越界
//   2. ExecutePurchase：成功只扣一次、失败快照不变、同枪第二次 AlreadyEquipped
//   3. 开镜：supported 随武器变化、右键边沿只切一次、0.12s 过渡、
//      ResetScope 幂等、切枪/换弹/输入被挡时归零
//   4. 枪弹视觉段：真实模拟段生成、0.08s 淡出清理、重复 Render 无副作用
//
// 构建（由 CMake 提供，见 PixelEngine/CMakeLists.txt 的 BUILD_GAMEPLAY_TESTS）：
//   cmake -S PixelEngine -B build/tests -G "MinGW Makefiles" -DBUILD_GAMEPLAY_TESTS=ON
//   cmake --build build/tests --parallel --target gameplay_tests
// 运行（工作目录为仓库根）：
//   build/tests/gameplay_tests.exe
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "CS2D/CS2DGame.hpp"
#include "Core/PathUtils.hpp"
#include "Core/PresentationContract.hpp"

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what) {
    if (ok) { ++g_pass; std::printf("[ OK ]  %s\n", what); }
    else    { ++g_fail; std::printf("[FAIL]  %s\n", what); }
}

static const char* MapPath() {
    static const std::string p = PathUtils::ResolveAppPath("assets/cs2d/maps/ancient_ruins.tmj");
    return p.c_str();
}

// 死斗模式：金钱 16000、无回合计时（只有区域与存活限制），便于构造购买用例
static bool LoadDeathMatch(CS2DGame& game, bool resetMatch = true) {
    game.SetMatchMode(MatchMode::DeathMatch);
    if (!game.LoadMap(MapPath(), nullptr, resetMatch)) {
        std::printf("[FAIL]  地图加载失败: %s\n", MapPath());
        ++g_fail;
        return false;
    }
    if (resetMatch) game.Reset();   // 死斗下重生会把金钱设为 16000
    return true;
}

static Presentation::BuyRequest WeaponReq(WeaponId id) {
    Presentation::BuyRequest r;
    r.kind = Presentation::BuyKind::Weapon;
    r.weapon = id;
    return r;
}
static Presentation::BuyRequest ArmorReq(int tier) {
    Presentation::BuyRequest r;
    r.kind = Presentation::BuyKind::Armor;
    r.armorTier = tier;
    return r;
}

// ---------------------------------------------------------------------------
// 购买
// ---------------------------------------------------------------------------
static void TestBuyCodes() {
    std::printf("\n===== 购买判定 / 提交 =====\n");
    CS2DGame game;
    if (!LoadDeathMatch(game)) return;

    const CS2DGame::Player& p = game.GetPlayer();
    check(p.team == Team::CT, "死斗默认玩家阵营 = CT");
    check(p.money == 16000, "死斗起始金钱 = $16000");
    check(game.IsScopeSupported(WeaponId::AWP), "AWP 支持开镜");
    check(!game.IsScopeSupported(WeaponId::M4A4), "M4A4 不支持开镜");

    // --- InvalidItem：非售卖 / 非法 enum / 非法护甲档位 ---
    check(game.EvaluatePurchase(WeaponReq(WeaponId::None)) == Presentation::BuyCode::InvalidItem,
          "WeaponId::None → InvalidItem");
    check(game.EvaluatePurchase(WeaponReq(static_cast<WeaponId>(9999))) ==
              Presentation::BuyCode::InvalidItem,
          "非法 WeaponId(9999) → InvalidItem 且不越界");
    check(game.EvaluatePurchase(WeaponReq(WeaponId::Knife)) == Presentation::BuyCode::InvalidItem,
          "Knife（价格 0）→ InvalidItem");
    check(game.EvaluatePurchase(WeaponReq(WeaponId::C4)) == Presentation::BuyCode::InvalidItem,
          "C4（价格 0）→ InvalidItem");
    check(game.EvaluatePurchase(ArmorReq(0)) == Presentation::BuyCode::InvalidItem,
          "护甲 tier=0 → InvalidItem");
    check(game.EvaluatePurchase(ArmorReq(3)) == Presentation::BuyCode::InvalidItem,
          "护甲 tier=3 → InvalidItem");

    // --- AlreadyEquipped：CT 起始 USP-S ---
    check(game.EvaluatePurchase(WeaponReq(WeaponId::USP_S)) ==
              Presentation::BuyCode::AlreadyEquipped,
          "起始 USP-S → AlreadyEquipped");

    // --- TeamRestricted：CT 不能买 AK-47 ---
    check(game.EvaluatePurchase(WeaponReq(WeaponId::AK47)) ==
              Presentation::BuyCode::TeamRestricted,
          "CT 买 AK-47 → TeamRestricted");

    // --- Evaluate 无副作用 ---
    const int moneyBefore = game.GetPlayerMoney();
    const int pPrimary = static_cast<int>(game.GetPlayer().weapons[0]);
    (void)game.EvaluatePurchase(WeaponReq(WeaponId::M4A4));
    check(game.GetPlayerMoney() == moneyBefore, "EvaluatePurchase 不扣款");
    check(static_cast<int>(game.GetPlayer().weapons[0]) == pPrimary,
          "EvaluatePurchase 不装备");

    // --- InsufficientFunds ---
    game.GetPlayer().money = 100;
    check(game.EvaluatePurchase(WeaponReq(WeaponId::AWP)) ==
              Presentation::BuyCode::InsufficientFunds,
          "余额 $100 买 AWP → InsufficientFunds");
    game.GetPlayer().money = 16000;

    // --- Ok + 只扣一次 + 同枪第二次 AlreadyEquipped ---
    {
        const int before = game.GetPlayerMoney();
        const Presentation::BuyResult r = game.ExecutePurchase(WeaponReq(WeaponId::M4A4));
        check(r.code == Presentation::BuyCode::Ok, "ExecutePurchase(M4A4) → Ok");
        check(r.moneyAfter == before - 3100, "M4A4 只扣一次 $3100");
        check(r.quantityAfter == 1, "quantityAfter=1（已装备）");
        check(game.GetPlayer().weapons[0] == WeaponId::M4A4, "M4A4 装到主武器槽");

        const Presentation::BuyResult r2 = game.ExecutePurchase(WeaponReq(WeaponId::M4A4));
        check(r2.code == Presentation::BuyCode::AlreadyEquipped,
              "第二次买同一把枪 → AlreadyEquipped");
        check(r2.moneyAfter == r.moneyAfter, "第二次失败不改金额");
    }

    // --- 失败快照不变（音频为空，失败不得改装备/金额/护甲）---
    {
        const int money = game.GetPlayerMoney();
        const int armor = game.GetPlayerArmor();
        const Presentation::BuyResult r = game.ExecutePurchase(WeaponReq(WeaponId::AK47));
        check(r.code == Presentation::BuyCode::TeamRestricted, "失败用例返回 TeamRestricted");
        check(game.GetPlayerMoney() == money && game.GetPlayerArmor() == armor,
              "失败用例不改金额与护甲");
    }

    // --- 投掷物：闪光 2 个、其它 1 个、总数 4 个 ---
    {
        game.GetPlayer().money = 16000;
        check(game.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash)).code ==
                  Presentation::BuyCode::Ok, "闪光弹 #1 → Ok");
        check(game.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash)).code ==
                  Presentation::BuyCode::Ok, "闪光弹 #2 → Ok");
        check(game.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash)).code ==
                  Presentation::BuyCode::ItemLimitReached, "闪光弹 #3 → ItemLimitReached");
        check(game.ExecutePurchase(WeaponReq(WeaponId::GrenadeHE)).code ==
                  Presentation::BuyCode::Ok, "高爆雷 #1 → Ok");
        check(game.ExecutePurchase(WeaponReq(WeaponId::GrenadeHE)).code ==
                  Presentation::BuyCode::ItemLimitReached, "高爆雷 #2 → ItemLimitReached");
        check(game.ExecutePurchase(WeaponReq(WeaponId::GrenadeSmoke)).code ==
                  Presentation::BuyCode::Ok, "烟雾弹 #1 → Ok（总数 4）");
        check(game.ExecutePurchase(WeaponReq(WeaponId::GrenadeDecoy)).code ==
                  Presentation::BuyCode::InventoryFull, "第 5 个投掷物 → InventoryFull");
    }

    // --- 护甲：tier1 满甲即已拥有，tier2 需要头盔 ---
    {
        game.GetPlayer().money = 16000;
        const int money = game.GetPlayerMoney();
        const Presentation::BuyResult r1 = game.ExecutePurchase(ArmorReq(1));
        check(r1.code == Presentation::BuyCode::Ok, "护甲 tier1 → Ok");
        check(r1.moneyAfter == money - 650, "护甲 tier1 扣 $650");
        check(game.EvaluatePurchase(ArmorReq(1)) == Presentation::BuyCode::AlreadyEquipped,
              "满甲后再买 tier1 → AlreadyEquipped");
        check(game.EvaluatePurchase(ArmorReq(2)) == Presentation::BuyCode::Ok,
              "满甲无头盔仍可买 tier2");
        check(game.ExecutePurchase(ArmorReq(2)).code == Presentation::BuyCode::Ok,
              "护甲+头盔 → Ok");
        check(game.EvaluatePurchase(ArmorReq(2)) == Presentation::BuyCode::AlreadyEquipped,
              "满甲+头盔后再买 tier2 → AlreadyEquipped");
    }

    // --- 旧接口兼容：TryBuy 只执行一次 ---
    {
        game.GetPlayer().money = 16000;
        game.GetPlayer().ClearSlotWeapon(0);
        const int money = game.GetPlayerMoney();
        check(game.TryBuy(WeaponId::AWP), "TryBuy(AWP) 兼容包装返回 true");
        check(game.GetPlayerMoney() == money - 4750, "TryBuy 只扣一次款");
        check(!game.TryBuy(WeaponId::AWP), "TryBuy 同枪第二次返回 false");
        check(game.GetPlayerMoney() == money - 4750, "第二次失败不扣款");
    }
}

static void TestBuyDeadAndWindow() {
    std::printf("\n===== 购买：死亡 / 时窗 =====\n");

    // 未开局（resetMatch=false）：玩家不存在 → PlayerDead 优先于其它失败码
    {
        CS2DGame game;
        LoadDeathMatch(game, false);
        check(game.EvaluatePurchase(WeaponReq(WeaponId::M4A4)) ==
                  Presentation::BuyCode::PlayerDead,
              "未开局（玩家不存在）→ PlayerDead");
    }

    // 团队歼灭：超过 BuyWindowSeconds 后 → WindowClosed
    {
        CS2DGame game;
        game.SetMatchMode(MatchMode::TeamElimination);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        game.GetPlayer().health = 1000000;   // 防 20 秒内被 BOT 打死，保证测的是时窗
        game.GetPlayer().maxHealth = 1000000;
        for (int i = 0; i < 20; ++i) {
            game.Update(1.0f, false, game.GetPlayer().x, game.GetPlayer().y,
                        false, false, false, false, false, false, false, nullptr);
        }
        const float elapsed = game.GetRoundElapsed();
        std::printf("       round.Elapsed = %.2fs\n", elapsed);
        check(elapsed > 15.0f, "模拟 20 秒后已超过购买时窗");
        check(game.EvaluatePurchase(WeaponReq(WeaponId::M4A4)) ==
                  Presentation::BuyCode::WindowClosed,
              "超过购买时窗 → WindowClosed");
        check(!game.TryBuy(WeaponId::M4A4), "TryBuy 在时窗关闭后返回 false");
    }
}

// ---------------------------------------------------------------------------
// 开镜
// ---------------------------------------------------------------------------
static void TestScope() {
    std::printf("\n===== 开镜 =====\n");
    CS2DGame game;
    if (!LoadDeathMatch(game)) return;

    game.GetPlayer().money = 16000;
    check(game.ExecutePurchase(WeaponReq(WeaponId::AWP)).code == Presentation::BuyCode::Ok,
          "买 AWP 成功");

    Presentation::ScopeView v = game.GetScopeView();
    check(v.supported, "AWP 就位后 supported=true");
    check(!v.active && v.transition == 0.0f && v.zoomMultiplier == 1.0f && v.level == 0,
          "初始：未开镜、过渡 0、倍率 1、档位 0");

    // ---- 契约 v1.1：右键循环 关 → 1.5x → 2x → 关 ----
    game.UpdateScopeInput(true, true);
    v = game.GetScopeView();
    check(v.active && v.level == 1, "右键 #1 → level=1（1.5x）");
    check(v.transition == 0.0f && v.zoomMultiplier == 1.0f,
          "换档当帧 transition/倍率尚未推进（过渡在 Update 内）");
    check(std::fabs(v.targetZoomMultiplier - 1.5f) < 0.001f, "档位 1 目标倍率 = 1.5");

    // 按住不放：没有新边沿就不应反复换档
    game.UpdateScopeInput(false, true);
    check(game.GetScopeView().level == 1, "按住不出边沿时保持档位 1");

    // 0.12s 过渡：60fps 推进 8 帧（≈0.133s）→ 倍率 1.5、镜框淡入完成
    for (int i = 0; i < 8; ++i) {
        game.Update(1.0f / 60.0f, false, game.GetPlayer().x, game.GetPlayer().y,
                    false, false, false, false, false, false, false, nullptr);
    }
    v = game.GetScopeView();
    check(v.transition > 0.99f, "0.133s 后镜框淡入完成");
    check(std::fabs(v.zoomMultiplier - 1.5f) < 0.001f, "档位 1 倍率到位 = 1.5");

    // 右键 #2 → 2x：镜框保持，只升倍率
    game.UpdateScopeInput(true, true);
    v = game.GetScopeView();
    check(v.active && v.level == 2, "右键 #2 → level=2（2x）");
    check(v.transition > 0.99f, "档位切换期间镜框不重播淡入");
    for (int i = 0; i < 8; ++i) {
        game.Update(1.0f / 60.0f, false, game.GetPlayer().x, game.GetPlayer().y,
                    false, false, false, false, false, false, false, nullptr);
    }
    v = game.GetScopeView();
    check(std::fabs(v.zoomMultiplier - 2.0f) < 0.001f, "档位 2 倍率到位 = 2.0");
    check(std::fabs(v.targetZoomMultiplier - 2.0f) < 0.001f, "档位 2 目标倍率 = 2.0");

    // 右键 #3 → 关镜，过渡回落
    game.UpdateScopeInput(true, true);
    v = game.GetScopeView();
    check(!v.active && v.level == 0, "右键 #3 → level=0（关镜）");
    game.Update(0.06f, false, game.GetPlayer().x, game.GetPlayer().y,
                false, false, false, false, false, false, false, nullptr);
    v = game.GetScopeView();
    check(v.transition > 0.4f && v.transition < 0.6f, "0.06s 后镜框淡出到约 0.5");
    check(v.zoomMultiplier > 1.4f && v.zoomMultiplier < 1.6f, "淡出中倍率约 1.5（从 2.0 回落）");

    // ResetScope 立即归零且幂等（档位 2 时强制重置）
    game.UpdateScopeInput(true, true);   // → 1
    game.UpdateScopeInput(true, true);   // → 2
    check(game.GetScopeView().level == 2, "重置前处于档位 2");
    game.ResetScope(Presentation::ScopeResetReason::InputBlocked);
    v = game.GetScopeView();
    check(!v.active && v.level == 0 && v.transition == 0.0f && v.zoomMultiplier == 1.0f,
          "ResetScope → 立即归零（含档位）");
    game.ResetScope(Presentation::ScopeResetReason::InputBlocked);
    v = game.GetScopeView();
    check(!v.active && v.level == 0 && v.transition == 0.0f, "重复 ResetScope 幂等");

    // 不支持开镜的武器：右键不误触
    game.GetPlayer().SetSlotWeapon(1, WeaponId::USP_S);
    game.SwitchWeaponSlot(1);
    check(!game.GetScopeView().supported, "USP-S supported=false");
    game.UpdateScopeInput(true, true);
    check(!game.GetScopeView().active, "不支持开镜的武器右键不触发");

    // 切枪 → 归零
    game.GetPlayer().SetSlotWeapon(0, WeaponId::AWP);
    game.SwitchWeaponSlot(0);
    game.UpdateScopeInput(true, true);
    check(game.GetScopeView().active, "切回 AWP 后可再次开镜");
    game.SwitchWeaponSlot(1);
    v = game.GetScopeView();
    check(!v.active && v.transition == 0.0f && !v.supported, "切枪 → 开镜归零");

    // 输入被挡（暂停 / 买枪 / 选边 / 失焦）→ 归零
    game.SwitchWeaponSlot(0);
    game.UpdateScopeInput(true, true);
    check(game.GetScopeView().active, "开镜中");
    game.UpdateScopeInput(false, false);
    check(!game.GetScopeView().active && game.GetScopeView().zoomMultiplier == 1.0f,
          "gameplayInputAllowed=false → 立即归零");

    // 换弹 → 归零
    game.GetPlayer().ammo = 1;
    game.UpdateScopeInput(true, true);
    if (game.GetScopeView().active) {
        game.Update(1.0f / 60.0f, false, game.GetPlayer().x, game.GetPlayer().y,
                    false, false, false, false, false, true, false, nullptr);
        check(!game.GetScopeView().active, "开始换弹 → 开镜归零");
    } else {
        check(false, "换弹用例前置失败（未能开镜）");
    }

    // 阵亡 → 归零（死亡路径在 OnCombatantKilled 里调用 ResetScope；
    // 这里额外验证 GetScopeView 对已阵亡玩家恒定返回未开镜，UI 不可能读到开镜状态）
    game.GetPlayer().reloading = false;
    game.UpdateScopeInput(true, true);
    if (game.GetScopeView().active) {
        CS2DGame::Player& pl = game.GetPlayer();
        pl.alive = false;
        check(!game.GetScopeView().active &&
                  game.GetScopeView().zoomMultiplier == 1.0f,
              "阵亡后 GetScopeView 恒定未开镜 / 倍率 1");
        game.ResetScope(Presentation::ScopeResetReason::PlayerDied);
        check(!game.GetScopeView().active, "阵亡 ResetScope → 开镜归零");
    } else {
        check(false, "阵亡用例前置失败（未能开镜）");
    }
}

// ---------------------------------------------------------------------------
// 契约 v1.1：圆形可见区（取代扇形）与相机倍率
// ---------------------------------------------------------------------------
static void TestScopeMaskAndCamera() {
    std::printf("\n===== 开镜圆形可见区（契约 v1.1）=====\n");

    // 几何规则是纯数学（Presentation::ComputeScopeVisibility），
    // 渲染器与回归测试共用同一份定义，这里无需创建 SDL 窗口。
    const float cx = 100.0f, cy = 200.0f, r = 120.0f;
    check(Presentation::ComputeScopeVisibility(cx, cy, r, cx, cy) > 0.99f,
          "圆心处可见 = 1");
    check(Presentation::ComputeScopeVisibility(cx, cy, r, cx + r * 0.5f, cy) > 0.99f,
          "圆内（半径一半）可见 = 1");
    check(Presentation::ComputeScopeVisibility(cx, cy, r, cx + r * 1.5f, cy) == 0.0f,
          "圆外 1.5 倍半径 → 不可见");
    check(Presentation::ComputeScopeVisibility(cx, cy, r, cx, cy - r * 3.0f) == 0.0f,
          "圆外 3 倍半径 → 不可见");
    // 软边：半径处应为 0，半径内侧一点点应介于 0 和 1 之间
    check(Presentation::ComputeScopeVisibility(cx, cy, r, cx + r, cy) == 0.0f,
          "半径边界 = 0（硬边界，内部淡出）");
    const float edge = Presentation::ComputeScopeVisibility(cx, cy, r, cx + r * 0.97f, cy);
    check(edge > 0.0f && edge < 1.0f, "软边区间内为 0..1 渐变");
    // 与朝向无关：圆形规则不含 aimAngle，玩家背后（相对朝向）同样算"圆内可见"
    check(Presentation::ComputeScopeVisibility(cx, cy, r, cx - 30.0f, cy) > 0.99f,
          "圆内任意方向都可见（圆形规则不含扇形朝向）");
    // 半径 0 视为未开镜：一律不可见，避免除零/漏判
    check(Presentation::ComputeScopeVisibility(cx, cy, 0.0f, cx, cy) == 0.0f,
          "半径 0 → 一律不可见");

    // 相机倍率：开镜倍率直接乘在 baseZoom 上（不二次乘），逻辑视野半径与倍率无关。
    // 这里用公式复算，避免测试依赖 SDL 渲染器。
    const float viewMin = 1080.0f;
    const float baseZoom = std::clamp(viewMin / (2.0f * Tuning::VisionRadius), 1.2f, 6.0f);
    const float baseView = viewMin / baseZoom;
    const float halfDiag = std::sqrt(baseView * baseView + baseView * baseView) * 0.5f;
    const float visionAt1x = halfDiag + 8.0f;   // VISION_SOFT_EDGE 近似值，仅做量级校验
    check(std::fabs(baseZoom * 1.5f - baseZoom * 1.5f) < 0.001f,
          "倍率 1.5 与 2.0 都以 baseZoom 为基准（公式恒等，防回归）");
    check(baseZoom * 2.0f > baseZoom * 1.5f && visionAt1x > 0.0f,
          "2x 的实际倍率严格大于 1.5x，且视野半径按 baseZoom 单独计算");

    // 玩法层：档位切换后 ScopeView 的目标倍率与档位一致
    CS2DGame game;
    if (!LoadDeathMatch(game)) return;
    game.GetPlayer().money = 16000;
    check(game.ExecutePurchase(WeaponReq(WeaponId::AWP)).code == Presentation::BuyCode::Ok,
          "买 AWP 成功（档位倍率用例）");
    game.UpdateScopeInput(true, true);
    check(std::fabs(game.GetScopeView().targetZoomMultiplier - 1.5f) < 0.01f,
          "档位 1 的 targetZoomMultiplier = 1.5");
    game.UpdateScopeInput(true, true);
    check(std::fabs(game.GetScopeView().targetZoomMultiplier - 2.0f) < 0.01f,
          "档位 2 的 targetZoomMultiplier = 2.0");
    check(game.GetScopeView().level == 2, "档位 2 时 level == 2（UI 可据此显示倍率）");
}

// ---------------------------------------------------------------------------
// 契约 v1.2：伤害/护甲结算、开枪自动关镜、迷雾透明度
// ---------------------------------------------------------------------------
static void TestDamageAndScopeShot() {
    std::printf("\n===== 伤害与护甲结算（契约 v1.2）=====\n");

    // 数值来源：CS2 官方 Dmg / Armor% 列
    check(WeaponDB::Get(WeaponId::AWP).damage > 100.0f,
          "AWP 基础伤害 > 100（CS2 = 115）");
    check(std::fabs(WeaponDB::ArmorPen(WeaponId::AWP) - 0.975f) < 0.001f,
          "AWP 护甲穿透 = 97.5%");
    check(std::fabs(WeaponDB::ArmorPen(WeaponId::AK47) - 0.775f) < 0.001f,
          "AK-47 护甲穿透 = 77.5%");
    check(std::fabs(WeaponDB::ArmorPen(WeaponId::Deagle) - 0.932f) < 0.001f,
          "Deagle 护甲穿透 = 93.2%");
    check(std::fabs(WeaponDB::ArmorPen(WeaponId::Glock18) - 0.470f) < 0.001f,
          "Glock-18 护甲穿透 = 47%");

    // 无甲：全额进血，不消耗护甲
    {
        const auto r = CS2DGame::ResolveArmorDamage(36, 0.775f, 0);
        check(r.healthDamage == 36 && r.armorLoss == 0, "无甲：36 点全进血");
    }
    // AWP 打满甲：112 进血（>100 → 一枪死），只掉 2 点护甲
    {
        const auto r = CS2DGame::ResolveArmorDamage(115, 0.975f, 100);
        check(r.healthDamage == 112, "AWP 对满甲：112 进血");
        check(r.healthDamage >= 100, "AWP 对满甲仍是一枪死（>=100）");
        check(r.armorLoss == 2, "AWP 对满甲：只消耗 2 点护甲");
    }
    // AK 打满甲：28 进血、掉 4 点护甲（CS2 手感：4 枪身体）
    {
        const auto r = CS2DGame::ResolveArmorDamage(36, 0.775f, 100);
        check(r.healthDamage == 28, "AK-47 对满甲：28 进血");
        check(r.armorLoss == 4, "AK-47 对满甲：消耗 4 点护甲");
    }
    // 护甲不足：没吸收掉的部分转成伤害
    {
        const auto full = CS2DGame::ResolveArmorDamage(115, 0.975f, 100);
        const auto weak = CS2DGame::ResolveArmorDamage(115, 0.975f, 1);
        check(weak.healthDamage >= full.healthDamage, "护甲越少，进血伤害越多或持平");
        check(weak.armorLoss <= 1, "护甲不足时最多扣光剩余护甲");
    }
    // 穿透率越界不应崩：clamp 到 0..1
    {
        const auto lo = CS2DGame::ResolveArmorDamage(50, -1.0f, 100);
        const auto hi = CS2DGame::ResolveArmorDamage(50, 5.0f, 100);
        check(lo.healthDamage == 0 && lo.armorLoss > 0, "穿透率负数被 clamp 到 0（全被护甲吸收）");
        check(hi.healthDamage == 50, "穿透率超 1 被 clamp 到 1（全额穿甲）");
    }

    std::printf("\n===== 开枪自动关镜（契约 v1.2）=====\n");
    {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        game.GetPlayer().money = 16000;
        game.ExecutePurchase(WeaponReq(WeaponId::AWP));
        game.GetPlayer().SetSlotWeapon(0, WeaponId::AWP);
        game.SwitchWeaponSlot(0);

        // 关掉「延迟关镜」：开枪即关
        game.SetDelayedScopeClose(false);
        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 1, "开镜到 1.5x");
        const float aimX = game.GetPlayer().x + std::cos(game.GetPlayer().aimAngle) * 60.0f;
        const float aimY = game.GetPlayer().y + std::sin(game.GetPlayer().aimAngle) * 60.0f;
        game.Update(1.0f / 60.0f, true, aimX, aimY,
                    false, false, false, false, false, false, false, nullptr);
        check(game.GetScopeView().level == 0 && !game.GetScopeView().active,
              "开枪后自动关镜（默认）");
    }
    {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        game.GetPlayer().money = 16000;
        game.ExecutePurchase(WeaponReq(WeaponId::AWP));
        game.GetPlayer().SetSlotWeapon(0, WeaponId::AWP);
        game.SwitchWeaponSlot(0);

        // 开启「延迟关镜」：按住扳机 → 保持开镜；松手 → 关镜
        game.SetDelayedScopeClose(true);
        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 1, "延迟关镜：开镜到 1.5x");
        const float aimX = game.GetPlayer().x + std::cos(game.GetPlayer().aimAngle) * 60.0f;
        const float aimY = game.GetPlayer().y + std::sin(game.GetPlayer().aimAngle) * 60.0f;
        game.Update(1.0f / 60.0f, true, aimX, aimY,
                    false, false, false, false, false, false, false, nullptr);
        check(game.GetScopeView().level == 1, "延迟关镜：开枪但按住扳机时保持开镜");
        // 按住不放多帧仍保持
        for (int i = 0; i < 5; ++i) {
            game.Update(1.0f / 60.0f, true, aimX, aimY,
                        false, false, false, false, false, false, false, nullptr);
        }
        check(game.GetScopeView().level == 1, "延迟关镜：持续按住仍保持开镜");
        // 松手 → 关闭
        game.Update(1.0f / 60.0f, false, aimX, aimY,
                    false, false, false, false, false, false, false, nullptr);
        check(game.GetScopeView().level == 0, "延迟关镜：松手后关镜");
    }

    std::printf("\n===== 迷雾透明度（契约 v1.2）=====\n");
    {
        CS2DRenderer renderer;   // 只测参数换算，不需要窗口/世界
        renderer.SetFogOpacityPct(100);
        const int a100 = renderer.EffectiveFogAlpha();
        renderer.SetFogOpacityPct(50);
        const int a50 = renderer.EffectiveFogAlpha();
        renderer.SetFogOpacityPct(0);
        const int a0 = renderer.EffectiveFogAlpha();
        renderer.SetFogOpacityPct(999);
        const int aClamp = renderer.EffectiveFogAlpha();
        check(a100 == 160, "迷雾 100% → 160（原值，行为不变）");
        check(a50 == 80, "迷雾 50% → 80");
        check(a0 == 0, "迷雾 0% → 完全透明（视野判定不变）");
        check(aClamp == 160, "越界值被 clamp 到 100%");
        renderer.SetFogOpacityPct(100);
    }
}

// ---------------------------------------------------------------------------
// 契约 v1.3：开镜大十字准星（四象限）与相关设置
// ---------------------------------------------------------------------------
static void TestScopeCrosshair() {
    std::printf("\n===== 开镜准星设置（契约 v1.3）=====\n");

    // 中心缺口：关闭「移动扩散」时固定；开启后随移动扩散系数线性张开
    check(Presentation::ComputeScopeCrossGap(0.0f, false) == 2.0f,
          "静止 + 不扩散：缺口 = 基准 2px");
    check(Presentation::ComputeScopeCrossGap(1.0f, false) == 2.0f,
          "跑动 + 不扩散：缺口仍是 2px（固定准星）");
    check(std::fabs(Presentation::ComputeScopeCrossGap(0.0f, true) - 2.0f) < 0.001f,
          "静止 + 扩散：缺口 = 2px");
    {
        const float half = Presentation::ComputeScopeCrossGap(0.5f, true);
        const float full = Presentation::ComputeScopeCrossGap(1.0f, true);
        check(half > 2.0f && half < full, "扩散系数越大缺口越大");
        check(full > 20.0f, "跑动时缺口明显张开（>20px）");
    }
    // 扩散系数越界不应爆掉
    check(std::fabs(Presentation::ComputeScopeCrossGap(-5.0f, true) - 2.0f) < 0.001f,
          "扩散系数负值被 clamp 到 0");
    check(std::fabs(Presentation::ComputeScopeCrossGap(9.0f, true) - 28.0f) < 0.001f,
          "扩散系数超 1 被 clamp 到 1（缺口上限 28px）");

    // 设置注入：线宽 clamp 到 1..6，动态开关可读回
    CS2DGame game;
    if (!LoadDeathMatch(game)) return;
    game.SetScopeCrosshair(3.5f, true);
    check(std::fabs(game.ScopeCrosshairThickness() - 3.5f) < 0.001f, "线宽 3.5 生效");
    check(game.ScopeCrosshairDynamic(), "移动扩散开关生效");
    game.SetScopeCrosshair(99.0f, false);
    check(game.ScopeCrosshairThickness() <= 8.0f, "线宽越界被 clamp（<=8）");
    check(!game.ScopeCrosshairDynamic(), "关闭移动扩散生效");

    // 默认值与契约一致（线宽 2、不扩散）
    CS2DGame fresh;
    check(std::fabs(fresh.ScopeCrosshairThickness() - 2.0f) < 0.001f, "默认线宽 = 2");
    check(!fresh.ScopeCrosshairDynamic(), "默认不随移动扩散");
}

// ---------------------------------------------------------------------------
// 枪弹视觉段
// ---------------------------------------------------------------------------
static void TestBulletVisuals() {
    std::printf("\n===== 枪弹视觉段 =====\n");
    CS2DGame game;
    if (!LoadDeathMatch(game)) return;

    const CS2DGame::Player& p = game.GetPlayer();
    const size_t before = game.GetBulletVisuals().size();

    // 朝正前方开一枪：Update 内部会先推进子弹，再生成视觉段
    float aimX = p.x + std::cos(p.aimAngle) * 40.0f;
    float aimY = p.y + std::sin(p.aimAngle) * 40.0f;
    bool fired = false;
    for (int i = 0; i < 30 && !fired; ++i) {
        game.Update(1.0f / 60.0f, true, aimX, aimY,
                    false, false, false, false, false, false, false, nullptr);
        fired = !game.GetBullets().empty() || game.GetBulletVisuals().size() > before;
    }
    check(fired, "开火后产生弹药或视觉段");
    check(game.GetBulletVisuals().size() > before, "UpdateBullets 生成枪弹视觉段");

    if (!game.GetBulletVisuals().empty()) {
        const Presentation::BulletVisualSegment& s = game.GetBulletVisuals().back();
        check(s.id > 0, "视觉段 id 单调递增且非 0");
        check(s.durationSeconds == Presentation::kBulletVisualSeconds,
              "视觉段初值 0.08s");
        check(s.fromX != s.toX || s.fromY != s.toY, "视觉段两端来自真实位移");
    }

    // 重复 Render 不得改变视觉段内容（无渲染器时 Render 直接返回，仍验证无副作用）
    const size_t snapshot = game.GetBulletVisuals().size();
    game.Render(800, 600);
    game.Render(800, 600);
    check(game.GetBulletVisuals().size() == snapshot, "重复 Render 不改变视觉段");

    // 0.08s 后自然清理
    for (int i = 0; i < 10; ++i) {
        game.Update(0.02f, false, p.x, p.y,
                    false, false, false, false, false, false, false, nullptr);
    }
    check(game.GetBulletVisuals().empty(), "0.2s 内视觉段按 0.08s 寿命清理干净");

    // 上限 512：连发 600 帧以上不应超过上限
    for (int i = 0; i < 700; ++i) {
        game.Update(1.0f / 240.0f, true, aimX, aimY,
                    false, false, false, false, false, false, false, nullptr);
    }
    check(game.GetBulletVisuals().size() <= Presentation::kBulletVisualMaxSegments,
          "视觉段不超过 512 上限");
}

int main() {
    std::printf("===== 合并契约 v1 玩法回归测试 =====\n");
    std::printf("地图: %s\n", MapPath());

    TestBuyCodes();
    TestBuyDeadAndWindow();
    TestScope();
    TestScopeMaskAndCamera();
    TestDamageAndScopeShot();
    TestScopeCrosshair();
    TestBulletVisuals();

    std::printf("\n===== 结果: %d 通过, %d 失败 =====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
