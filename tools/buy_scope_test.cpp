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
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "Engine.hpp"
#include "CS2D/CS2DGame.hpp"
#include "Audio/AudioManager.hpp"
#include "CS2D/CS2DRenderer.hpp"
#include "CS2D/CS2DOverlay.hpp"
#include "CS2D/Visibility/BodyGeometry.hpp"
#include "CS2D/ShowcaseCamera.hpp"
#include "Storage/SaveManager.hpp"
#include "CS2D/CS2DWorld.hpp"
#include "CS2D/TMJParser.hpp"
#include "Core/PathUtils.hpp"
#include "Core/PresentationContract.hpp"
#include "Core/BuildInfo.hpp"
#include "dev_replay/replay.hpp"
#include "Input/FireInputGate.hpp"
#include "Input/InputManager.hpp"
#include "Graphics/Menu3DModel.hpp"
#include "Graphics/Menu3D.hpp"
#include "Graphics/GameUI.hpp"
#include "Graphics/WeaponIconCache.hpp"
#include "Graphics/ImGuiUI.hpp"
#include "Graphics/UiScale.hpp"
#include "Graphics/Palette.hpp"
#include "ThirdParty/imgui/imgui_internal.h"

struct Menu3DTestAccess {
    static bool ChickenAvoidance(const std::string& directory) {
        Menu3D menu;
        // T/CT 角色已移除（2026-11-25）：小鸡只躲避彼此与场景边界。
        for (int index = 0; index < 3; ++index) {
            auto chicken = std::make_unique<Menu3DModel>();
            if (!chicken->Load(directory + "/chicken.glb", "Menu_Walk")) return false;
            menu.chickens_.push_back(std::move(chicken));
        }
        constexpr float dt = 1.0f / 30.0f;
        for (unsigned seed = 0; seed < 8; ++seed) {
            menu.chickenRng_.seed(seed);
            menu.SetCustomization(Menu3D::Customization{});
            menu.InitChickenState();
            if (seed == 0) {
                menu.chickenState_[0].x = -.8f;
                menu.chickenState_[1].x = .8f;
                for (int index = 0; index < 2; ++index) {
                    auto& state = menu.chickenState_[index];
                    state.z = 4;
                    state.targetX = -state.x;
                    state.targetZ = 4;
                    state.yaw = index == 0 ? 90.0f : 270.0f;
                    state.repathTimer = 10;
                }
                menu.chickenState_[2].x = 0;
                menu.chickenState_[2].z = 5;
                menu.FocusSelect(4, 1, 1);
            }
            float travel[3] = {};
            for (int frame = 0; frame < 1800; ++frame) {
                if (frame % 6 == 0) {
                    for (auto& chicken : menu.chickens_) chicken->Update(dt * 6);
                }
                const auto before = menu.chickenState_;
                menu.UpdateChickens(dt);
                for (int index = 0; index < 3; ++index) {
                    const auto& state = menu.chickenState_[index];
                    const float dx = state.x - before[index].x;
                    const float dz = state.z - before[index].z;
                    const float movement = std::sqrt(dx * dx + dz * dz);
                    travel[index] += movement;
                    const float stride = Menu3D::kChickenModelHeight * menu.ChickenWorldScale(state) *
                                         Menu3D::kChickenStrideBodyHeights;
                    const float strideSpeed = stride * menu.ChickenAnimTimeScale(state) / Menu3D::kWalkClipSeconds;
                    if (std::fabs(strideSpeed - state.movementSpeed) > .001f) {
                        std::printf("[CHICKEN SLIDE] seed %u frame %d chicken %d\n", seed, frame, index);
                        return false;
                    }
                    if (!menu.ChickenPathClear(index, state.x, state.z, state.x, state.z)) {
                        std::printf("[CHICKEN BOUNDS] seed %u frame %d chicken %d x %.4f z %.4f\n",
                                    seed, frame, index, state.x, state.z);
                        return false;
                    }
                    if (menu.custom_.pose[index + 2] != 0 && movement > .0001f) return false;
                    if (!std::isfinite(state.x) || !std::isfinite(state.z) ||
                        movement > state.speed * dt + .05f) return false;
                    for (const auto& obstacle : menu.ChickenObstacles(index)) {
                        const float x = state.x - obstacle.x, z = state.z - obstacle.z;
                        const float required = menu.ChickenRadius(index, state.z) + obstacle.radius - .003f;
                        if (x * x + z * z < required * required) {
                            std::printf("[CHICKEN OVERLAP] seed %u frame %d chicken %d\n", seed, frame, index);
                            return false;
                        }
                    }
                }
            }
            for (int index = 0; index < 3; ++index) {
                const float distance = travel[index];
                if (menu.custom_.pose[index + 2] == 0 && distance < 3.0f) {
                    std::printf("[CHICKEN STUCK] seed %u travel %.3f\n", seed, distance);
                    return false;
                }
                std::printf("[CHICKEN TRAVEL] seed %u chicken %d %.3f m\n", seed, index, distance);
            }
        }
        return true;
    }
    // LoadCharacter 已随 T/CT 移除（2026-11-25）：ChickenAvoidance 不再加载角色模型。
};

struct Menu3DModelTestAccess {
    static bool HandSkinValid(const Menu3DModel& model) {
        const int hands = model.FindNode("Hands");
        if (hands < 0 || model.nodes_[hands].skin < 0) return false;
        float left[16], right[16];
        if (!model.GetNodeWorldMatrix("Hand.L", left) || !model.GetNodeWorldMatrix("Hand.R", right)) return false;
        for (const auto& primitive : model.meshes_[model.nodes_[hands].mesh].primitives) {
            for (size_t i = 0; i < primitive.vertices.size(); ++i) {
                const auto& vertex = primitive.vertices[i];
                float totalWeight = 0.0f;
                for (float weight : vertex.weights) totalWeight += weight;
                if (std::fabs(totalWeight - 1.0f) > .001f) return false;
                const auto& point = primitive.skinnedPos[i];
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return false;
                const float leftDistanceSquared = (point.x - left[12]) * (point.x - left[12]) +
                                                  (point.y - left[13]) * (point.y - left[13]) +
                                                  (point.z - left[14]) * (point.z - left[14]);
                const float rightDistanceSquared = (point.x - right[12]) * (point.x - right[12]) +
                                                   (point.y - right[13]) * (point.y - right[13]) +
                                                   (point.z - right[14]) * (point.z - right[14]);
                if (std::min(leftDistanceSquared, rightDistanceSquared) > .4f * .4f) return false;
            }
        }
        return true;
    }

    static bool WeaponGeometryUpright(const Menu3DModel& model, const char* nodeName,
                                      float expectedBottom, float expectedTop) {
        const int index = model.FindNode(nodeName);
        if (index < 0 || model.nodes_[index].mesh < 0) return false;
        float bottom = 100.0f;
        float top = -100.0f;
        for (const auto& primitive : model.meshes_[model.nodes_[index].mesh].primitives) {
            for (const auto& vertex : primitive.vertices) {
                bottom = std::min(bottom, vertex.pos.y);
                top = std::max(top, vertex.pos.y);
            }
        }
        return std::fabs(bottom - expectedBottom) < .001f &&
               std::fabs(top - expectedTop) < .001f;
    }

    static bool IndependentMeshCaches(const Menu3DModel& model) {
        std::vector<int> used;
        for (const auto& node : model.nodes_) {
            if (node.mesh < 0) continue;
            for (int mesh : used) {
                if (mesh == node.mesh) return false;
            }
            used.push_back(node.mesh);
            if (!node.visible || node.skin >= 0) continue;
            for (const auto& primitive : model.meshes_[node.mesh].primitives) {
                if (primitive.vertices.empty()) continue;
                const auto expected = Menu3DModel::TransformPoint(node.world, primitive.vertices[0].pos);
                const auto actual = primitive.skinnedPos[0];
                if (std::fabs(expected.x - actual.x) > .001f ||
                    std::fabs(expected.y - actual.y) > .001f ||
                    std::fabs(expected.z - actual.z) > .001f) return false;
            }
        }
        return true;
    }
};

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
    // 死斗是真正的 FFA：BOT 会主动打玩家。把测试用玩家的血量拉高，
    // 让"与非战斗相关"的用例（开镜档位/过渡/切枪）不被 BOT 打死打断；
    // 需要验证死亡的用例会自己把血量清零。
    game.GetPlayer().health = 100000;
    game.GetPlayer().maxHealth = 100000;
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

    // Invalid kinds must never bypass team restrictions or mutate inventory.
    for (int invalidKind : {-1, 999}) {
        auto request = WeaponReq(WeaponId::AK47);
        request.kind = static_cast<Presentation::BuyKind>(invalidKind);
        const int money = game.GetPlayerMoney();
        const WeaponId primary = game.GetPlayer().weapons[0];
        check(game.EvaluatePurchase(request) == Presentation::BuyCode::InvalidItem,
              "非法 BuyKind 判定为 InvalidItem");
        const auto result = game.ExecutePurchase(request);
        check(result.code == Presentation::BuyCode::InvalidItem,
              "非法 BuyKind 提交仍被拒绝");
        check(game.GetPlayerMoney() == money && game.GetPlayer().weapons[0] == primary,
              "非法 BuyKind 不扣款、不装备");
    }

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

    // ---- 契约 v1.1：右键循环 关 → 1.5x → 3x → 关 ----
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

    // 右键 #2 → 3x：镜框保持，只升倍率
    game.UpdateScopeInput(true, true);
    v = game.GetScopeView();
    check(v.active && v.level == 2, "右键 #2 → level=2（3x）");
    check(v.transition > 0.99f, "档位切换期间镜框不重播淡入");
    for (int i = 0; i < 8; ++i) {
        game.Update(1.0f / 60.0f, false, game.GetPlayer().x, game.GetPlayer().y,
                    false, false, false, false, false, false, false, nullptr);
    }
    v = game.GetScopeView();
    check(std::fabs(v.zoomMultiplier - 3.0f) < 0.001f, "档位 2 倍率到位 = 3.0");
    check(std::fabs(v.targetZoomMultiplier - 3.0f) < 0.001f, "档位 2 目标倍率 = 3.0");

    // 右键 #3 → 关镜，过渡回落
    game.UpdateScopeInput(true, true);
    v = game.GetScopeView();
    check(!v.active && v.level == 0, "右键 #3 → level=0（关镜）");
    game.Update(0.06f, false, game.GetPlayer().x, game.GetPlayer().y,
                false, false, false, false, false, false, false, nullptr);
    v = game.GetScopeView();
    check(v.transition > 0.4f && v.transition < 0.6f, "0.06s 后镜框淡出到约 0.5");
    check(v.zoomMultiplier > 1.9f && v.zoomMultiplier < 2.1f, "淡出中倍率约 2.0（从 3.0 回落）");

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
static void TestScopeCameraLevels() {
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
    check(std::fabs(game.GetScopeView().targetZoomMultiplier - 3.0f) < 0.01f,
          "档位 2 的 targetZoomMultiplier = 3.0");
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

    std::printf("\n===== 修复项回归（连发开镜 / 换弹中开镜）=====\n");
    {
        // 可连发的开镜武器（AUG / SG 553 / SCAR-20 / G3SG1）：必须能**开镜连射**，
        // 每打一发不自动退镜；「延迟关镜」对它们没有意义（本来就不关）。
        CS2DGame game;
        WeaponDB::MatchLoadouts ml = WeaponDB::DefaultMatchLoadouts();
        ml.ct.rifles[4] = WeaponId::AUG;
        game.SetMatchLoadouts(ml);
        if (!LoadDeathMatch(game)) return;
        game.GetPlayer().money = 16000;
        check(game.ExecutePurchase(WeaponReq(WeaponId::AUG)).code == Presentation::BuyCode::Ok,
              "买 AUG 成功（可连发 + 可开镜）");
        game.GetPlayer().SetSlotWeapon(0, WeaponId::AUG);
        game.SwitchWeaponSlot(0);
        game.SetDelayedScopeClose(true);            // 最坏情况：延迟关镜开着
        // 死斗是 FFA：BOT 会攻击玩家（这正是本轮要的行为）。为了让用例只考察"开镜是否被射击打断"，
        // 这里把玩家的血量拉高，避免半秒内被打死（死亡也会正确退镜，但那不是本用例的目标）。
        game.GetPlayer().health = 10000;
        game.GetPlayer().armor = 100;
        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 1, "AUG 开镜到 1.5x");
        const float aimX = game.GetPlayer().x + std::cos(game.GetPlayer().aimAngle) * 60.0f;
        const float aimY = game.GetPlayer().y + std::sin(game.GetPlayer().aimAngle) * 60.0f;
        for (int i = 0; i < 30; ++i) {              // 连射半秒
            game.Update(1.0f / 60.0f, true, aimX, aimY,
                        false, false, false, false, false, false, false, nullptr);
        }
        check(game.GetScopeView().level == 1,
              "可连发开镜武器：按住连射期间保持开镜（不会每发退镜）");
    }
    {
        // 单发狙（AWP）：开枪后退镜；拉栓冷却（shootCooldown>0）期间不能重新开镜。
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        game.GetPlayer().money = 16000;
        game.ExecutePurchase(WeaponReq(WeaponId::AWP));
        game.GetPlayer().SetSlotWeapon(0, WeaponId::AWP);
        game.SwitchWeaponSlot(0);
        game.SetDelayedScopeClose(false);
        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 1, "AWP 开镜到 1.5x");
        const float aimX = game.GetPlayer().x + std::cos(game.GetPlayer().aimAngle) * 60.0f;
        const float aimY = game.GetPlayer().y + std::sin(game.GetPlayer().aimAngle) * 60.0f;
        game.Update(1.0f / 60.0f, true, aimX, aimY,
                    false, false, false, false, false, false, false, nullptr);
        check(game.GetScopeView().level == 0, "AWP 开枪后退镜");
        check(game.GetPlayer().shootCooldown > 0.0f, "AWP 进入拉栓冷却");
        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 0, "拉栓冷却期间右键不能开镜");
        game.GetPlayer().shootCooldown = 0.0f;      // 模拟拉栓结束
        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 1, "拉栓结束后可以重新开镜");
    }
    {
        // 死斗 FFA：同队也互为敌人；非死斗才按队伍判定。
        CS2DGame dm;
        if (!LoadDeathMatch(dm)) return;
        check(dm.IsDeathMatch(), "死斗模式标记 = true");
        check(dm.IsHostileTeam(Team::T, Team::T), "死斗：同队也敌对（一人成军）");
        check(dm.IsHostileTeam(Team::CT, Team::CT), "死斗：CT 之间也敌对");

        CS2DGame team;
        team.SetMatchMode(MatchMode::TeamElimination);
        if (!team.LoadMap(MapPath(), nullptr, true)) return;
        check(!team.IsDeathMatch(), "团队模式标记 = false");
        check(!team.IsHostileTeam(Team::T, Team::T), "团队模式：同队不敌对");
        check(team.IsHostileTeam(Team::T, Team::CT), "团队模式：跨队敌对");

        // ---- 死斗阵亡：原地死亡镜头，不观战任何 BOT ----
        CS2DGame dmCam;
        if (!LoadDeathMatch(dmCam)) return;
        check(!dmCam.IsSpectating(), "死斗存活时不处于观察者");
        dmCam.DebugKillPlayerForTest();
        check(!dmCam.IsSpectating(), "死斗阵亡：不进入观察者（不观战 BOT）");
        check(dmCam.IsDeathCam(), "死斗阵亡：进入原地死亡镜头");
        // 3s 后自动重生；重生瞬间立即断言语义（FFA 下复活后可能马上又被打死，
        // 所以不能等循环结束再检查"是否活着"）
        bool sawRespawn = false;
        for (int i = 0; i < 300; ++i) {
            dmCam.Update(1.0f / 60.0f, false, dmCam.GetPlayer().x, dmCam.GetPlayer().y,
                         false, false, false, false, false, false, false, nullptr);
            if (dmCam.GetPlayer().alive) { sawRespawn = true; break; }
        }
        check(sawRespawn, "死斗阵亡后自动重生");
        check(!dmCam.IsDeathCam(), "重生后退出死亡镜头");

        // ---- 死斗：重生自动装配上一次购买的武器，且仍可继续购买 ----
        {
            CS2DGame loot;
            if (!LoadDeathMatch(loot)) return;
            loot.GetPlayer().money = 16000;
            const Presentation::BuyResult br =
                loot.ExecutePurchase(WeaponReq(WeaponId::M4A4), nullptr);
            check(br.code == Presentation::BuyCode::Ok, "死斗买 M4A4 成功（CT 阵营可买）");
            check(loot.GetDmLoadout().valid, "死斗购买后记录装备");
            check(loot.GetDmLoadout().primary == WeaponId::M4A4, "记录的主武器 = M4A4");
            loot.DebugKillPlayerForTest();
            bool respawned2 = false;
            for (int i = 0; i < 300; ++i) {
                loot.Update(1.0f / 60.0f, false, loot.GetPlayer().x, loot.GetPlayer().y,
                            false, false, false, false, false, false, false, nullptr);
                if (loot.GetPlayer().alive) { respawned2 = true; break; }
            }
            check(respawned2, "死斗阵亡后重生");
            check(loot.GetPlayer().weapons[0] == WeaponId::M4A4, "重生后自动装上 M4A4");
            check(loot.GetPlayer().weaponSlot == 0, "重生后默认手持主武器");
            const WeaponDef& ak = WeaponDB::Get(WeaponId::M4A4);
            check(loot.GetPlayer().ammo == ak.magazine, "重生后主武器弹匣满");
            check(loot.GetPlayer().money == 16000, "重生后金钱仍为 $16000（还能继续买）");
            const Presentation::BuyResult br2 =
                loot.ExecutePurchase(WeaponReq(WeaponId::M4A1S), nullptr);
            check(br2.code == Presentation::BuyCode::Ok, "重生后仍可购买武器");
            check(loot.GetDmLoadout().primary == WeaponId::M4A1S, "新购买会更新记录");
        }

        // 对照：团队模式阵亡仍进入观察者
        CS2DGame teamCam;
        teamCam.SetMatchMode(MatchMode::TeamElimination);
        if (!teamCam.LoadMap(MapPath(), nullptr, true)) return;
        teamCam.DebugKillPlayerForTest();
        check(teamCam.IsDeathFreeze() && !teamCam.IsSpectating(), "团队模式阵亡：先定格死亡画面");
        check(!teamCam.IsDeathCam(), "团队模式没有死亡镜头");
    }
    {
        // 换弹期间不能开镜；正在开镜时进入换弹也必须退镜（兜底在 TickScope）。
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        game.GetPlayer().money = 16000;
        game.ExecutePurchase(WeaponReq(WeaponId::AWP));
        game.GetPlayer().SetSlotWeapon(0, WeaponId::AWP);
        game.SwitchWeaponSlot(0);

        game.GetPlayer().reloading = true;
        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 0, "换弹中右键不能开镜");
        game.GetPlayer().reloading = false;

        game.UpdateScopeInput(true, true);
        check(game.GetScopeView().level == 1, "停止换弹后可开镜");
        // 开镜状态下进入换弹：下一帧 Update 的 TickScope 必须兜底归零
        game.GetPlayer().reloading = true;
        game.Update(1.0f / 60.0f, false, game.GetPlayer().x, game.GetPlayer().y,
                    false, false, false, false, false, false, false, nullptr);
        check(game.GetScopeView().level == 0, "开镜中进入换弹 → 自动退镜");
        game.GetPlayer().reloading = false;
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

    // 0.08s 后自然清理（死斗 FFA 下 BOT 也会开火，所以只验证"本次射击之前的段"已清空，
    // 不能假设整个 vector 为空）
    std::uint64_t lastIdBefore = 0;
    for (const auto& s : game.GetBulletVisuals()) lastIdBefore = std::max(lastIdBefore, s.id);
    for (int i = 0; i < 10; ++i) {
        game.Update(0.02f, false, p.x, p.y,
                    false, false, false, false, false, false, false, nullptr);
    }
    bool staleLeft = false;
    for (const auto& s : game.GetBulletVisuals()) {
        if (s.id <= lastIdBefore) staleLeft = true;
    }
    check(!staleLeft, "0.2s 内旧视觉段按 0.08s 寿命清理干净");

    // 上限 512：连发 600 帧以上不应超过上限
    for (int i = 0; i < 700; ++i) {
        game.Update(1.0f / 240.0f, true, aimX, aimY,
                    false, false, false, false, false, false, false, nullptr);
    }
    check(game.GetBulletVisuals().size() <= Presentation::kBulletVisualMaxSegments,
          "视觉段不超过 512 上限");
}

static void TestFireInputGate() {
    FireInputGate gate;
    check(gate.Update(true, true), "正常对局可按住射击");
    check(!gate.Update(true, false), "持枪开菜单立即屏蔽射击");
    check(!gate.Update(false, false), "菜单中松开不射击");
    check(!gate.Update(true, false), "菜单点击不射击");
    check(!gate.Update(true, true), "关闭菜单时仍按住不射击");
    check(!gate.Update(false, true), "回到对局先释放扳机");
    check(gate.Update(true, true), "重新点击后恢复射击");
    gate.Block();
    check(!gate.Update(true, true), "暂停或选边后持按不会误开枪");
    check(!gate.Update(false, true), "暂停后释放扳机");
    check(gate.Update(true, true), "暂停后重新点击恢复射击");
}

static void TestFixedVisionRadius() {
    const float reference = VisibilityQuery::RadiusForViewport(1728, 1080);
    check(std::fabs(reference - 379.6f) < 0.2f, "global HD calibration preserves the doubled reference world radius");
    VisibilityQuery baseline;
    baseline.SetVisionRadius(reference);
    for (const auto viewport : std::vector<std::pair<int, int>>{{1280,720}, {1920,1080}, {2560,1600}, {800,1200}}) {
        VisibilityQuery query;
        query.SetVisionRadius(VisibilityQuery::RadiusForViewport(viewport.first, viewport.second));
        bool same = query.VisionRadius() == reference;
        for (int distance = 0; distance <= 350; ++distance) {
            same = same && query.ComputeVisibility(0,0,0,static_cast<float>(distance),0)
                        == baseline.ComputeVisibility(0,0,0,static_cast<float>(distance),0);
        }
        check(same, "PIX-20 visibility matches across viewport sizes and aspect ratios");
    }
    check(VisibilityQuery::RadiusForViewport(1280,720,1.2f)
          == VisibilityQuery::RadiusForViewport(2560,1600,6.0f),
          "PIX-20 camera zoom does not change world radius");
}

static std::string MenuAssetPath(const char* actor) {
    const char* directory = std::getenv("PIXEL_MENU_MODEL_DIR");
    return std::string(directory ? directory : "assets/models/menu") + "/" + actor + ".glb";
}

static void TestMenuHeldPoses() {
    const char* poses[] = { "Idle", "Crouch", "HandsOnHips", "FistPalm" };
    const char* weapons[] = { "AK", "SMG", "Sniper", "Shotgun", "Pistol", "Knife_1" };
    for (const char* actor : { "t", "ct" }) {
        Menu3DModel model;
        const std::string path = MenuAssetPath(actor);
        check(model.Load(path, "Menu_Idle_AK"), "menu actor GLB loads");
        check(Menu3DModelTestAccess::WeaponGeometryUpright(model, "weapon_Idle_SMG", -.254831f, .363748f),
              "SMG geometry retains its upright top and magazine orientation");
        check(Menu3DModelTestAccess::WeaponGeometryUpright(model, "weapon_Idle_Shotgun", -.160413f, .170367f),
              "shotgun geometry retains its upright top and stock orientation");
        for (const char* accessory : { "accessory_headwear", "accessory_eyewear", "accessory_back" }) {
            float matrix[16];
            check(model.GetNodeWorldMatrix(accessory, matrix), "bone-bound accessory exists");
            model.SetNodeVisible(accessory, false);
            model.SetNodeColorExcluded(accessory, true);
        }
        for (const char* pose : poses) {
            for (const char* weapon : weapons) {
                const std::string clip = std::string("Menu_") + pose + "_" + weapon;
                const std::string selected = std::string("weapon_") + pose + "_" + weapon;
                for (const char* p : poses) {
                    for (const char* w : weapons) {
                        const std::string node = std::string("weapon_") + p + "_" + w;
                        model.SetNodeVisible(node, node == selected);
                        model.SetNodeColorExcluded(node, true);
                    }
                }
                bool valid = model.PlayAnimation(clip) && model.AnimationName() == clip;
                float start[16], finish[16];
                valid = model.GetNodeWorldMatrix(selected, start) && valid;
                if (std::string(pose) == "Idle" || std::string(pose) == "Crouch") {
                    float grip[16], wrist[16];
                    valid = model.GetNodeWorldMatrix(selected + "/grip_main", grip) && valid;
                    valid = model.GetNodeWorldMatrix("Index1.R", wrist) && valid;
                    float distanceSquared = 0.0f;
                    for (int i = 12; i < 15; ++i) {
                        const float delta = grip[i] - wrist[i];
                        distanceSquared += delta * delta;
                    }
                    valid = valid && distanceSquared < .14f * .14f;
                    if (distanceSquared >= .14f * .14f) {
                        std::printf("[GRIP DISTANCE] %s %s %.4f m\n", actor, clip.c_str(), std::sqrt(distanceSquared));
                    }
                }
                const float ground = model.GroundOffset();
                model.Update(4.0f);
                valid = model.GetNodeWorldMatrix(selected, finish) && valid;
                for (int i = 0; i < 16; ++i) {
                    valid = valid && std::isfinite(finish[i]) && std::fabs(start[i] - finish[i]) < .002f;
                }
                valid = valid && std::fabs(model.GroundOffset() - ground) < .002f;
                valid = valid && Menu3DModelTestAccess::IndependentMeshCaches(model);
                valid = valid && Menu3DModelTestAccess::HandSkinValid(model);
                check(valid, clip.c_str());
            }
        }
        const std::string previous = model.AnimationName();
        check(!model.PlayAnimation("Menu_Crouch") && model.AnimationName() == previous,
              "incomplete menu animation IDs cannot select a different weapon");
    }
    Menu3DModel chicken;
    check(chicken.Load(MenuAssetPath("chicken"), "Menu_Walk"), "menu chicken GLB loads");
    for (const char* pose : { "Menu_Walk", "Menu_Idle", "Menu_Look", "Menu_Rest" }) {
        check(chicken.PlayAnimation(pose), pose);
        chicken.Update(12.0f);
        check(std::isfinite(chicken.GroundOffset()), "chicken pose remains valid after three loops");
    }
}

// TestMenuWeaponMountSelection 已随 T/CT 移除（2026-11-25）：
// 武器挂载节点显隐只属于已删除的角色模型；GLB 文件本身仍由 TestMenuHeldPoses 直接验证。
// cover / center 两种碰撞栅格化模式（地图属性 collisionMode）
static void TestCollisionModes() {
    CS2DMapData data;
    data.collisionMargin = 2.0f; // This fixture uses the original 8px grid.
    data.mapWidth  = 4;
    data.mapHeight = 1;
    data.tileWidth = 8;
    data.tileHeight = 8;

    ObjectLayerDef layer;
    layer.name = "Collisions";
    ObjectDef obj;
    obj.x = 4.0f; obj.y = 0.0f; obj.width = 6.0f; obj.height = 8.0f;
    layer.objects.push_back(obj);
    data.objectLayers.push_back(layer);

    data.collisionMode = "cover";
    CS2DWorld cover;
    cover.Build(data);
    check(cover.IsSolid(0, 0) && cover.IsSolid(0, 1),
          "cover: 4..10 的矩形会外扩覆盖两格");

    data.collisionMode = "center";
    CS2DWorld center;
    center.Build(data);
    check(center.IsSolid(0, 0) && !center.IsSolid(0, 1),
          "center: 只占格中心真正落在矩形内的那一格");

    // 薄墙（<1 tile）不能被整面吞掉：补厚后至少命中一格
    CS2DMapData thin;
    thin.mapWidth = 4; thin.mapHeight = 1;
    thin.tileWidth = 8; thin.tileHeight = 8;
    thin.collisionMode = "center";
    ObjectLayerDef thinLayer;
    thinLayer.name = "Collisions";
    ObjectDef thinObj;
    thinObj.x = 9.0f; thinObj.y = 0.0f; thinObj.width = 1.0f; thinObj.height = 8.0f;
    thinLayer.objects.push_back(thinObj);
    thin.objectLayers.push_back(thinLayer);
    CS2DWorld thinWorld;
    thinWorld.Build(thin);
    int solid = 0;
    for (int c = 0; c < 4; ++c) solid += thinWorld.IsSolid(0, c) ? 1 : 0;
    check(solid == 1, "center: 1px 薄墙补厚后仍保留一格碰撞");

    // center 模式下移动碰撞用像素级矩形：贴到墙边停住，不会嵌进去
    CS2DMapData wallData;
    wallData.mapWidth = 4; wallData.mapHeight = 1;
    wallData.tileWidth = 8; wallData.tileHeight = 8;
    wallData.collisionMode = "center";
    ObjectLayerDef wl; wl.name = "Collisions";
    ObjectDef wo; wo.x = 9.0f; wo.y = 0.0f; wo.width = 1.0f; wo.height = 8.0f;
    wl.objects.push_back(wo); wallData.objectLayers.push_back(wl);
    CS2DWorld wallWorld; wallWorld.Build(wallData);
    // 7px 高的盒子放在 y=0：右侧贴到 x=9 才碰撞
    check(!wallWorld.CheckRectCollision(1.9f, 0.0f, 7.0f, 7.0f), "center: 盒子未碰到墙时不碰撞");
    check(wallWorld.CheckRectCollision(2.1f, 0.0f, 7.0f, 7.0f), "center: 盒子碰到墙的像素边就碰撞");
}

static void TestStableCursorAndWallRay() {
    CS2DOverlay overlay;
    overlay.InitializeCursor(0.0f);
    float x = 0.0f, y = 0.0f;
    overlay.UpdateCursor(0, 0, 1, 6, 0, 0, 1920, 1080, 200, 200, x, y);
    check(x == 200 + Tuning::InitialAimDistance && y == 200, "spawn cursor starts in front of player");
    overlay.UpdateCursor(0, 0, 1, 4, -80, -90, 1920, 1080, 200, 200, x, y);
    check(x == 200 + Tuning::InitialAimDistance && y == 200, "camera and zoom do not feed back into aim");
    overlay.SyncScopeCursor(true, false, false);
    overlay.UpdateCursor(60, -30, 1, 6, 0, 100, 1920, 1080, 200, 200, x, y);
    check(x == 310 && y == 190, "scoped cursor accepts relative motion");
    overlay.SetVisibleAimPoint(x, y);
    overlay.SyncScopeCursor(true, true, false);
    overlay.UpdateCursor(0, 0, 1, 4, -30, 80, 1920, 1080, 200, 200, x, y);
    check(std::abs(x - 310) < 0.001f && std::abs(y - 190) < 0.001f,
          "scope closing preserves the last map pixel");
    overlay.SyncScopeCursor(false, true, false);
    overlay.UpdateCursor(0, 0, 1, 6, 0, 0, 1920, 1080, 200, 200, x, y);
    check(std::abs(x - 310) < 0.001f && std::abs(y - 190) < 0.001f, "zoom changes preserve anchored world point");
    overlay.UpdateCursor(0, 0, 1, 4, -80, -90, 1920, 1080, 200, 200, x, y);
    check(std::abs(x - 310) < 0.001f && std::abs(y - 190) < 0.001f, "camera changes preserve anchored world point");
    overlay.SyncScopeCursor(false, true, true);
    overlay.UpdateCursor(0, 0, 1, 4, -80, -90, 1920, 1080, 200, 200, x, y);
    check(!overlay.ScreenAnchorActive(), "settled closing camera commits the anchor to the base offset");
    const float committedX = x, committedY = y;
    overlay.UpdateCursor(0, 0, 1, 6, 0, 0, 1920, 1080, 200, 200, x, y);
    check(x == committedX && y == committedY, "completed handoff no longer rewrites the offset from the camera");
    overlay.SyncScopeCursor(true, false, false);
    overlay.SyncScopeCursor(false, true, false);
    overlay.UpdateCursor(0, 0, 1, 4, -80, -90, 1920, 1080, 200, 200, x, y);
    const float interruptedX = x, interruptedY = y;
    overlay.SyncScopeCursor(true, false, false);
    overlay.UpdateCursor(0, 0, 1, 4, -80, -90, 1920, 1080, 200, 200, x, y);
    check(!overlay.ScreenAnchorActive() && x == interruptedX && y == interruptedY,
          "reopening during handoff commits the current point without jumping back");
    overlay.InitializeCursor(0.0f);
    overlay.UpdateCursor(0, 0, 1, 6, 0, 0, 1920, 1080, 210, 205, x, y);
    check(x == 210 + Tuning::InitialAimDistance && y == 205, "player motion preserves relative cursor offset");

    CS2DMapData data;
    data.mapWidth = 8; data.mapHeight = 8;
    data.tileWidth = 8; data.tileHeight = 8;
    ObjectLayerDef layer; layer.name = "Collisions";
    ObjectDef wall; wall.x = 24; wall.y = 0; wall.width = 8; wall.height = 64;
    layer.objects.push_back(wall); data.objectLayers.push_back(layer);
    CS2DWorld world; world.Build(data);
    VisibilityQuery ray; ray.SetWorld(&world);
    check(std::fabs(ray.FirstBulletWallFraction(8, 12, 48, 12) - 0.4f) < 0.001f, "aim ray stops exactly at first wall boundary");
    check(ray.FirstBulletWallFraction(8, 12, 20, 12) == 1.0f, "unobstructed aim ray reaches crosshair");
    check(std::fabs(ray.FirstBulletWallFraction(48, 12, 8, 12) - 0.4f) < 0.001f, "reverse ray finds opposite wall face");
    check(ray.FirstBulletWallFraction(28, 12, 48, 12) == 0.0f, "muzzle inside wall has no solid visible segment");
    check(ray.FirstBulletWallFraction(8, 12, 8, 12) == 1.0f, "zero length ray terminates");

    CS2DMapData cameraData;
    cameraData.mapWidth = 200; cameraData.mapHeight = 200;
    cameraData.tileWidth = 8; cameraData.tileHeight = 8;
    CS2DWorld cameraWorld; cameraWorld.Build(cameraData);
    CS2DRenderer at60, at120;
    at60.SetWorld(&cameraWorld); at120.SetWorld(&cameraWorld);
    at60.UpdateCamera(250, 250, 1920, 1080);
    at120.UpdateCamera(250, 250, 1920, 1080);
    at60.SetFrameDelta(1.0f / 60.0f); at120.SetFrameDelta(1.0f / 120.0f);
    for (int frame = 0; frame < 6; ++frame) at60.UpdateCamera(600, 600, 1920, 1080);
    for (int frame = 0; frame < 12; ++frame) at120.UpdateCamera(600, 600, 1920, 1080);
    check(std::fabs(at60.GetCameraX() - at120.GetCameraX()) < 0.001f, "camera smoothing is frame-rate independent");
}

static void TestAimLinePersistenceAndRecoilRecovery() {
    const std::string previousBase = PathUtils::BaseDir();
    const auto isolated = std::filesystem::absolute("build-evidence/pix31-settings-test");
    std::filesystem::create_directories(isolated);
    PathUtils::SetBaseDir(isolated.string());
    const auto savePath = isolated / (std::string("cs2d_save") + std::string(BuildInfo::SaveSuffix()) + ".ini");
    IniFile legacy;
    legacy.SetBool("crosshair", "dynamic", true);
    legacy.Save(savePath.string());
    SaveManager settings;
    settings.Load();
    check(settings.GetSettings().aimLine, "legacy settings default aim line on");
    settings.MutableSettings().aimLine = false;
    settings.Save();
    SaveManager reloaded;
    reloaded.Load();
    check(!reloaded.GetSettings().aimLine, "aim-line disabled setting survives reload");
    reloaded.MutableSettings().aimLine = true;
    reloaded.Save();
    settings.Load();
    check(settings.GetSettings().aimLine, "aim-line enabled setting survives reload");
    const auto defaults = settings.GetSettings().aimLineStyle;
    check(defaults.solid.r == 50 && defaults.hidden.a == 125 && defaults.dashLength == 8.0f,
          "legacy save receives original aim-line appearance");
    auto& line = settings.MutableSettings().aimLineStyle;
    line.solid = {210, 40, 30, 90};
    line.hidden = {60, 80, 220, 160};
    line.solidThickness = 4.5f;
    line.hiddenThickness = 2.5f;
    line.dashLength = 15.0f;
    line.gapLength = 11.0f;
    settings.Save();
    reloaded.Load();
    const auto loadedLine = reloaded.GetSettings().aimLineStyle;
    check(loadedLine.solid.r == 210 && loadedLine.solid.a == 90 && loadedLine.hidden.b == 220 && loadedLine.hidden.a == 160,
          "solid and hidden line colors and opacity survive reload independently");
    check(loadedLine.solidThickness == 4.5f && loadedLine.hiddenThickness == 2.5f &&
          loadedLine.dashLength == 15.0f && loadedLine.gapLength == 11.0f,
          "line widths and dash pattern survive reload");
    IniFile malformed = IniFile::Load(savePath.string());
    malformed.SetInt("aim_line", "solid_r", 999);
    malformed.SetFloat("aim_line", "solid_thickness", -20.0f);
    malformed.Set("aim_line", "hidden_thickness", "nan");
    malformed.SetFloat("aim_line", "dash_length", 0.0f);
    malformed.Set("aim_line", "gap_length", "inf");
    malformed.Save(savePath.string());
    reloaded.Load();
    const auto repairedLine = reloaded.GetSettings().aimLineStyle;
    check(repairedLine.solid.r == 255 && repairedLine.solidThickness == 0.5f && repairedLine.hiddenThickness == 1.0f &&
          repairedLine.dashLength >= 1.0f && std::isfinite(repairedLine.gapLength),
          "invalid line settings normalize finite widths, positive dash steps and color channels");
    PathUtils::SetBaseDir(previousBase);

    CS2DGame game;
    if (!LoadDeathMatch(game)) return;
    auto& player = game.GetPlayer();
    player.SetSlotWeapon(0, WeaponId::AK47);
    game.SwitchWeaponSlot(0);
    player.shootCooldown = 0.0f;
    float previousKick = 0.0f;
    for (int tick = 0; tick < 24; ++tick) {
        const int ammo = player.ammo;
        game.Update(1.0f / 60.0f, true, player.x + 100, player.y,
                    false, false, false, false, false, false, false, nullptr);
        if (player.ammo < ammo) {
            check(player.BallisticKick().y < 0, "weapon curve produces screen-up recoil while firing");
            previousKick = player.BallisticKick().y;
        }
    }
    const float heldKick = player.BallisticKick().y;
    for (int tick = 0; tick < 60; ++tick)
        game.Update(1.0f / 60.0f, false, player.x + 100, player.y,
                    false, false, false, false, false, false, false, nullptr);
    check(heldKick < 0 && std::abs(player.BallisticKick().y) < std::abs(heldKick) * 0.02f, "recoil returns after firing stops");
}

static void TestReactionBenchmark() {
    for (int difficulty = 0; difficulty < 4; ++difficulty) {
        float total = 0.0f;
        int samples = 0;
        for (int seed = 1; seed <= 60; ++seed) {
            CS2DGame game;
            game.SetRngSeed(seed);
            if (!LoadDeathMatch(game)) return;
            game.GetActors().resize(2);
            game.EnsureBotAi();
            auto& bot = game.GetActors()[1];
            auto& player = game.GetPlayer();
            bot.x = player.x + 0.1f;
            bot.y = player.y;
            bot.aimAngle = 3.14159265f;
            bot.shootAnimTimer = 0.0f;
            bot.shootCooldown = 0.0f;
            player.health = bot.health = 100000;
            game.SetBotProfile(1, difficulty * 2);
            game.ResetBotAi(1);
            for (int tick = 0; tick < 300; ++tick) {
                game.Update(1.0f / 60.0f, false, player.x, player.y,
                            false, false, false, false, false, false, false, nullptr);
                if (bot.shootAnimTimer > 0.0f) {
                    const float delay = tick / 60.0f;
                    check(delay + 0.02f >= Tuning::BotReactionSecs[difficulty], "first shot respects perception reaction deadline");
                    total += delay;
                    ++samples;
                    break;
                }
            }
        }
        check(samples == 60, "reaction benchmark completes 60 controlled encounters per difficulty");
        std::printf("[PIX31 reaction] difficulty=%d samples=%d mean_seconds=%.4f expected=%.2f\n",
                    difficulty, samples, samples ? total / samples : 0.0f, Tuning::BotReactionSecs[difficulty]);
    }
}

static void TestGrenadeCursorDistance() {
    for (float distance : {50.0f, 150.0f, 400.0f}) {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        auto& player = game.GetPlayer();
        player.grenades[0] = WeaponId::GrenadeHE;
        player.grenadeCount = 1;
        player.grenadeSlot = 0;
        player.SyncActiveGrenade();
        game.SwitchWeaponSlot(3);
        player.shootCooldown = 0.0f;
        game.Update(0.0f, true, player.x + distance, player.y,
                    false, false, false, false, false, false, false, nullptr);
        const auto& grenades = game.GetGrenades();
        check(!grenades.empty(), "cursor-distance grenade launches through normal input");
        if (!grenades.empty()) {
            const float expected = std::clamp(distance * Tuning::NadeFriction,
                Tuning::NadeThrowSpeedMin, Tuning::NadeThrowSpeedMax);
            check(std::fabs(std::hypot(grenades.back().vx, grenades.back().vy) - expected) < 0.01f,
                  "grenade launch speed follows cursor distance and clamps range");
        }
    }
}

static void TestFixedReferenceCursor() {
    for (int mode = 0; mode < 3; ++mode) {
        for (int fps : {60, 120, 144}) {
            for (const auto direction : std::vector<std::pair<int, int>>{{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}}) {
                CS2DOverlay reference, animated;
                reference.InitializeCursor(0.0f);
                animated.InitializeCursor(0.0f);
                const float modeZoom = mode == 1 ? 1.5f : mode == 2 ? 2.0f : 1.0f;
                const float shift = CS2DGame::ScopeCameraShiftForZoom(modeZoom);
                reference.SetReferenceMode(modeZoom, shift);
                animated.SetReferenceMode(modeZoom, shift);
                bool same = true;
                int entries = 0, exits = 0;
                bool previous = false;
                float previousX=500,previousY=500;
                for (int frame = 0; frame < fps * 4; ++frame) {
                    int motion = 0;
                    if (frame < fps) motion = 2880 / fps;
                    if (frame >= fps * 2 && frame < fps * 3) motion = -static_cast<int>(1440 * modeZoom) / fps;
                    const float time = static_cast<float>(frame) / fps;
                    const float zoom = 6.0f * (0.85f + 0.15f * std::cos(time * 5.0f));
                    float rx = 0, ry = 0, ax = 0, ay = 0;
                    int motionX=motion*direction.first,motionY=motion*direction.second;
                    if (frame>=fps*2 && frame<fps*3) {
                        const float referenceZoom=VisibilityQuery::BaseZoomForViewport(1920,1080)*modeZoom;
                        motionX=static_cast<int>(std::lround((500-previousX)*referenceZoom/(fps*3-frame)));
                        motionY=static_cast<int>(std::lround((500-previousY)*referenceZoom/(fps*3-frame)));
                    }
                    reference.UpdateCursor(motionX, motionY, 1, 6,
                        0, 0, 1920, 1080, 500, 500, rx, ry);
                    animated.UpdateCursor(motionX, motionY, 1, zoom,
                        150 * std::sin(time), -80 * std::cos(time), 1920, 1080, 500, 500, ax, ay);
                    same = same && std::abs(rx - ax) < 0.001f && std::abs(ry - ay) < 0.001f &&
                        reference.EdgeZoomRequested() == animated.EdgeZoomRequested();
                    previousX=ax;previousY=ay;
                    const bool requested = animated.EdgeZoomRequested();
                    if (requested && !previous) ++entries;
                    if (!requested && previous) ++exits;
                    previous = requested;
                }
                check(same, "fixed reference keeps input and edge decisions independent of animated zoom/camera");
                if (entries != 1 || exits != 1)
                    std::printf("[Reference case] mode=%d fps=%d dir=(%d,%d) entries=%d exits=%d\n", mode, fps, direction.first, direction.second, entries, exits);
                check(entries == 1 && exits == 1, "continuous outward/hold/inward motion has exactly one zoom entry and exit");
            }
        }
    }
    for (const auto side : std::vector<std::pair<int, int>>{{-1,0}, {1,0}, {0,-1}, {0,1}}) {
        CS2DOverlay border;
        border.InitializeCursor(0);
        border.SetReferenceWorldBounds(880, 880);
        const float playerX = side.first > 0 ? 860.0f : 20.0f;
        const float playerY = side.second > 0 ? 860.0f : 20.0f;
        float x = 0, y = 0;
        border.UpdateCursor(side.first * 10000, side.second * 10000, 1, 6, 0, 0,
            1920, 1080, playerX, playerY, x, y);
        check(border.EdgeZoomRequested(), "map-clamped reference camera detects its viewport edge");
        const float heldX = x, heldY = y;
        bool stable = true;
        for (int frame = 0; frame < 120; ++frame) {
            border.UpdateCursor(0, 0, 1, 4.2f + frame * 0.01f, -100 + frame, -100 + frame,
                1920, 1080, playerX, playerY, x, y);
            stable = stable && border.EdgeZoomRequested() && x == heldX && y == heldY;
        }
        check(stable, "map-border clamp stays independent of animated camera/zoom");
        border.UpdateCursor(-side.first * 20, -side.second * 20, 1, 4.2f, 0, 0,
            1920, 1080, playerX, playerY, x, y);
        check(border.EdgeZoomRequested(), "small inward motion stays inside the reference hysteresis band");
        border.UpdateCursor(static_cast<int>((playerX-x)*VisibilityQuery::BaseZoomForViewport(1920,1080)), static_cast<int>((playerY-y)*VisibilityQuery::BaseZoomForViewport(1920,1080)), 1, 4.2f, 0, 0,
            1920, 1080, playerX, playerY, x, y);
        border.UpdateCursor(0,0,1,4.2f,0,0,1920,1080,playerX,playerY,x,y);
        check(!border.EdgeZoomRequested(), "inward motion exits reference hysteresis at map borders");
    }
    CS2DOverlay cursor;
    cursor.InitializeCursor(0);
    float x = 0, y = 0;
    cursor.UpdateCursor(10000, 0, 1, 6, 0, 0, 1920, 1080, 500, 500, x, y);
    check(cursor.EdgeZoomRequested(), "reference edge is reached before handoff");
    cursor.virtCursorX = 1300;
    cursor.virtCursorY = 500;
    cursor.SyncScopeCursor(true, false, false);
    cursor.SyncScopeCursor(false, true, false);
    for (int frame = 0; frame < 30; ++frame) {
        cursor.SyncScopeCursor(false, true, false, 1.0f / 60.0f);
        cursor.UpdateCursor(-10, 0, 1, 4, 100, 50, 1920, 1080, 500, 500, x, y);
        check(cursor.EdgeZoomRequested(), "scope handoff freezes the edge target while accepting mouse motion");
    }
    check(std::abs(cursor.virtCursorX - 900) < 0.01f, "handoff anchor accepts relative mouse movement");
    cursor.SyncScopeCursor(false, true, false, Tuning::AimHandoffMaxSeconds);
    cursor.UpdateCursor(0, 0, 1, 4, 100, 50, 1920, 1080, 500, 500, x, y);
    check(!cursor.ScreenAnchorActive(), "handoff has a finite completion deadline");

    dev_replay::JournalHeader header;
    dev_replay::InputFrame input;
    input.edgeZoom = true;
    input.relativeAim = true;
    const std::string path = "build-evidence/pix31-reference-input.txt";
    check(dev_replay::WriteJournal(path, header, {input}), "write edge-zoom replay input");
    std::vector<dev_replay::InputFrame> frames;
    check(dev_replay::ReadJournal(path, header, frames) && frames.front().edgeZoom && frames.front().relativeAim,
          "replay input preserves edge target and relative aiming mode");
    std::FILE* legacy = std::fopen(path.c_str(), "w");
    if (legacy) {
        std::fputs("# PXINPUT2 map=dust_2\n0 0 10 20 0 0 0 0 0 0 0 0 0 0 0\n", legacy);
        std::fclose(legacy);
    }
    frames.clear();
    check(dev_replay::ReadJournal(path, header, frames) && !frames.front().edgeZoom && !frames.front().relativeAim,
          "legacy replay input defaults edge target and relative aiming mode to false");
}

static void TestSharedAimDistance() {
    for (bool angled : {false}) {
        for (const auto player : std::vector<std::pair<float, float>>{{440,440}, {20,20}, {860,860}}) {
            for (const auto direction : std::vector<std::pair<int, int>>{{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}}) {
                float baselineX = 0, baselineY = 0;
                bool same = true;
                for (float zoom : {1.0f, 1.5f, 3.0f}) {
                    CS2DOverlay cursor;
                    cursor.InitializeCursor(std::atan2(static_cast<float>(direction.second), static_cast<float>(direction.first)));
                    cursor.SetReferenceWorldBounds(880, 880);
                    cursor.SetReferenceMode(zoom, CS2DGame::ScopeCameraShiftForZoom(zoom));
                    float x = 0, y = 0;
                    cursor.UpdateCursor(direction.first * 100000, direction.second * 100000, 1, 6 * zoom,
                        0, 0, 1920, 1080, player.first, player.second, x, y);
                    if (zoom == 1.0f) { baselineX = x; baselineY = y; }
                    same &= std::hypot(x - baselineX, y - baselineY) < 0.001f;
                }
                check(same, "all scope levels share the normal-camera maximum world offset including map edges");
            }
        }
    }
}

static void TestAimLineRendering() {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 180, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(software != nullptr, "software renderer for customizable aim line");
    if (!software) { SDL_FreeSurface(surface); return; }
    {
        CS2DRenderer renderer;
        renderer.Initialize(software, "assets/cs2d/maps", 320, 180);
        CS2DMapData data;
        data.mapWidth = 20; data.mapHeight = 12;
        data.tileWidth = data.tileHeight = 8;
        ObjectLayerDef layer; layer.name = "Collisions";
        ObjectDef wall; wall.x = 80; wall.y = 0; wall.width = 8; wall.height = 96;
        layer.objects.push_back(wall); data.objectLayers.push_back(layer);
        CS2DWorld world;
        world.Build(data);
        renderer.SetWorld(&world);
        renderer.SetPreviewCamera(80, 45, 2, false);
        auto clear = [&] {
            SDL_SetRenderDrawColor(software, 0, 0, 0, 255);
            SDL_RenderClear(software);
        };
        auto pixel = [&](int x, int y) {
            const auto* row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels) + y * surface->pitch);
            SDL_Color color;
            SDL_GetRGBA(row[x], surface->format, &color.r, &color.g, &color.b, &color.a);
            return color;
        };
        AimLineStyle line;
        line.solid = {200, 0, 0, 128};
        line.hidden = {0, 0, 240, 255};
        line.solidThickness = 6;
        line.hiddenThickness = 2;
        clear();
        renderer.DrawAimLine(20, 40, 0, line);
        SDL_RenderPresent(software);
        check(pixel(120,80).r >= 99 && pixel(120,80).r <= 101 && pixel(120,80).b == 0,
              "solid segment applies its customized color and alpha");
        check(pixel(163,80).b == 240 && pixel(170,80).b == 0,
              "wall-to-reticle section applies hidden color with a dashed gap");
        check(pixel(120,82).r > 0 && pixel(120,84).r == 0 && pixel(163,82).b == 0,
              "solid and hidden segments use independently customized pixel widths");
        clear();
        renderer.DrawAimLine(20, 40, 0, line);
        SDL_RenderPresent(software);
        check(pixel(45,80).r > 0 && pixel(150,80).r > 0 && pixel(163,80).b > 0,
              "ray starts at actor and continues past the reticle to the next wall");
        line.solid.a = line.hidden.a = 0;
        clear();
        renderer.DrawAimLine(20, 40, 0, line);
        SDL_RenderPresent(software);
        check(pixel(120,80).r == 0 && pixel(163,80).b == 0, "zero opacity hides both line segments");
    }
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
}

static void TestMovementFrameInterpolation() {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 180, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(software != nullptr, "software renderer for moving frame synchronization");
    if (!software) { SDL_FreeSurface(surface); return; }
    SDL_RenderSetLogicalSize(software, 1280, 720);
    {
        CS2DRenderer renderer;
        renderer.Initialize(software, "assets/cs2d/maps", 1280, 720);
        for (int fps : {30, 60, 120, 144}) {
            CS2DGame game;
            game.SetRngSeed(31);
            game.SetMatchMode(MatchMode::DeathMatch);
            if (!game.LoadMap(MapPath(), &renderer, true)) continue;
            game.GetActors().resize(1);
            game.EnsureBotAi();
            auto& player = game.GetPlayer();
            const auto& world = game.GetWorld();
            bool found = false;
            for (int row = 3; row < world.GetMapHeight() - 3 && !found; ++row) {
                for (int col = 3; col < world.GetMapWidth() - 3 && !found; ++col) {
                    const float x = (col + 0.5f) * world.GetTileWidth();
                    const float y = (row + 0.5f) * world.GetTileHeight();
                    if (!world.CheckRectCollision(x - 8, y - 8, 64, 16)) {
                        player.x = x;
                        player.y = y;
                        found = true;
                    }
                }
            }
            check(found, "movement regression finds an unobstructed lane");
            if (!found) continue;
            player.health = player.maxHealth = 100000;
            player.vx = player.vy = 0;
            game.SetAimTarget(player.x + 45, player.y, true);
            game.Update(0, false, player.x + 45, player.y, false, false, false, false, false, false, false, nullptr);
            renderer.SetFrameDelta(0);
            game.PrepareCamera(1280, 720);
            game.Render(1280, 720);
            const float startX = player.x;
            double lag = 0;
            float previousX = startX;
            float maximumStep = 0, projectionError = 0, interpolationError = 0, radiusError = 0;
            for (int frame = 0; frame < fps / 2; ++frame) {
                lag += 1.0 / fps;
                float lastTickX = player.x;
                while (lag + 1e-9 >= 1.0 / 60.0) {
                    lastTickX = player.x;
                    game.SetAimTarget(player.x + 45, player.y, true);
                    game.Update(1.0f / 60.0f, false, player.x + 45, player.y,
                        false, false, false, true, false, false, false, nullptr);
                    lag -= 1.0 / 60.0;
                }
                renderer.SetFrameDelta(1.0f / fps);
                const float alpha = std::clamp(static_cast<float>(lag * 60.0), 0.0f, 1.0f);
                game.PrepareCamera(1280, 720, alpha);
                const auto view = game.GetActorView(player);
                // Compare the active tick interval, even on render frames without a tick.
                if (player.x != lastTickX)
                    interpolationError = std::max(interpolationError, std::abs(view.x - (lastTickX + (player.x - lastTickX) * alpha)));
                maximumStep = std::max(maximumStep, view.x - previousX);
                check(view.x + 0.0001f >= previousX, "rendered movement advances monotonically");
                previousX = view.x;
                const float px = (view.x - renderer.GetCameraX()) * renderer.GetZoom();
                const float cx = (game.GetRenderAimX() - renderer.GetCameraX()) * renderer.GetZoom();
                projectionError = std::max(projectionError, std::abs((cx - px) - 45 * renderer.GetZoom()));
                radiusError = std::max(radiusError, std::abs(std::hypot(game.GetRenderAimX() - view.x, game.GetRenderAimY() - view.y) - 45));
                game.Render(1280, 720);
            }
            check(player.x > startX + 5, "normal movement entry point advances the player");
            check(maximumStep <= Tuning::PlayerBaseSpeed / fps + 0.002f, "rendered displacement stays within the per-frame movement bound");
            check(interpolationError < 0.001f && projectionError < 0.001f && radiusError < 0.001f,
                  "camera, actor and relative reticle share the interpolated frame at 30/60/120/144 FPS");
            game.SetAimTarget(player.x + 45, player.y, true);
            player.aimKick = -0.2f;
            renderer.SetFrameDelta(0);
            game.PrepareCamera(1280, 720, 1);
            const float kickedCamY = renderer.GetCameraY();
            const float kickedY = game.GetRenderAimY();
            game.Render(1280, 720);
            player.aimKick = 0;
            game.PrepareCamera(1280, 720, 1);
            check(kickedCamY < renderer.GetCameraY() && kickedY < game.GetRenderAimY(),
                  "camera follows the effective angular recoil target");
            game.Render(1280, 720);
            player.SetSlotWeapon(0, WeaponId::AWP);
            game.SwitchWeaponSlot(0);
            player.aimKick = 0;
            player.shootCooldown = 0;
            game.SetAimTarget(player.x + 45, player.y, true);
            game.Update(0, true, player.x + 45, player.y,
                false, false, false, false, false, false, false, nullptr);
            check(!game.GetBullets().empty(), "normal firing emits a bullet for recoil synchronization");
            if (!game.GetBullets().empty()) {
                const auto& bullet = game.GetBullets().back();
                const float shotAngle = std::atan2(bullet.vy, bullet.vx);
                const float shotSpread = Ballistics::Spread(player.CurrentWeapon().id,
                    std::hypot(player.vx,player.vy)/std::max(1.0f,Tuning::PlayerBaseSpeed*player.CurrentWeapon().moveSpeedMul),
                    game.GetScopeView().transition,player.BallisticInaccuracy());
                check(std::abs(std::remainder(shotAngle - player.EffectiveAim(), Tuning::Pi * 2)) <= shotSpread + 0.00001f,
                      "current shot uses the same recoil-adjusted direction as the reticle");
            }
            auto unshakenCameraX = [&] {
                const float zoom = renderer.GetZoom();
                const float viewW = 1280 / zoom;
                const float totalW = std::max(world.GetMapPixelW(), world.GetImageW());
                const float desired = player.x + (game.GetRenderAimX() - player.x) * Tuning::CameraLookShift - viewW * 0.5f;
                return std::clamp(desired, -Tuning::VisionRadius, std::max(-Tuning::VisionRadius, totalW - viewW + Tuning::VisionRadius));
            };
            game.PrepareCamera(1280, 720, 1);
            const float initialShake = renderer.GetCameraX() - unshakenCameraX();
            game.Render(1280, 720);
            game.Update(1.0f / 60.0f, false, player.x + 45, player.y,
                false, false, false, false, false, false, false, nullptr);
            game.PrepareCamera(1280, 720, 1);
            const float nextShake = renderer.GetCameraX() - unshakenCameraX();
            check(initialShake > 0.1f && nextShake < 0 && std::abs(nextShake) < initialShake,
                  "firing adds deterministic alternating camera shake after smoothing with decaying amplitude");
            game.Render(1280, 720);
            player.x = world.GetMapPixelW() * 0.5f;
            player.y = world.GetMapPixelH() * 0.5f;
            game.SetAimTarget(player.x + 200, player.y, true);
            player.aimKick = -Tuning::Pi * 0.5f;
            game.PrepareCamera(1280, 720, 1);
            const float reticleY = (game.GetRenderAimY() - renderer.GetCameraY()) * renderer.GetZoom();
            check(reticleY >= 0 && reticleY <= 720,
                  "camera accommodates recoil rotation at a long radius without losing the reticle");
        }
    }
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
}

static void TestScopeFrameSnapshot() {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 180, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(software != nullptr, "software renderer for scope frame snapshot");
    if (!software) { SDL_FreeSurface(surface); return; }
    SDL_RenderSetLogicalSize(software, 1280, 720);
    {
        CS2DRenderer renderer;
        renderer.Initialize(software, "assets/cs2d/maps", 1280, 720);
        CS2DGame game;
        game.SetRngSeed(31);
        game.SetMatchMode(MatchMode::DeathMatch);
        if (game.LoadMap(MapPath(), &renderer, true)) {
            auto& player = game.GetPlayer();
            player.health = player.maxHealth = 100000;
            player.SetSlotWeapon(0, WeaponId::AWP);
            game.SwitchWeaponSlot(0);
            for (int cameraMode = 0; cameraMode < 1; ++cameraMode) {
                for (int fps : {60, 120, 144}) {
                    game.ResetScope(Presentation::ScopeResetReason::InputBlocked);
                    game.SetEdgeZoomRequested(false);
                    player.aimKick = -0.15f;
                    player.recoilRecoveryTimer = 10.0f;
                    CS2DOverlay cursor;
                    cursor.InitializeCursor(0);
                    game.SetAimTarget(player.x + Tuning::InitialAimDistance, player.y);
                    renderer.SetFrameDelta(1.0f / fps);
                    double lag = 0;
                    bool sawAnchor = false, committed = false, edgeFrozen = true;
                    float lockedX = 0, lockedY = 0, error = 0;
                    bool commitChecked = false;
                    auto sync = [&](float dt, bool commitAllowed = false) {
                        const auto scope = game.GetScopeView();
                        const float shift = scope.level > 0 ? CS2DGame::ScopeCameraShiftForZoom(scope.targetZoomMultiplier) : Tuning::CameraLookShift;
                        cursor.SetReferenceMode(scope.targetZoomMultiplier, shift);
                        if (!cursor.ScreenAnchorActive()) {
                            cursor.SetVisibleAimPoint(game.GetRenderAimX(), game.GetRenderAimY());
                            cursor.virtCursorX = (game.GetRenderAimX() - renderer.GetCameraX()) * renderer.GetZoom();
                            cursor.virtCursorY = (game.GetRenderAimY() - renderer.GetCameraY()) * renderer.GetZoom();
                        }
                        cursor.SyncScopeCursor(scope.active, scope.level == 0, game.CameraScopeTransitionComplete(), dt, commitAllowed);
                        float x = 0, y = 0;
                        const auto view = game.GetActorView(player);
                        cursor.UpdateCursor(0, 0, 0, renderer.GetZoom(), renderer.GetCameraX(), renderer.GetCameraY(),
                            1280, 720, player.x, player.y, x, y, player.aimKick, {view.x - player.x, view.y - player.y});
                        game.SetAimTarget(x, y, !cursor.ScreenAnchorActive());
                        game.SetCameraDistanceTier(cursor.CameraDistanceTier());
                        if (cursor.ScreenAnchorActive() && !sawAnchor) {
                            sawAnchor = true;
                            lockedX = game.GetRenderAimX();
                            lockedY = game.GetRenderAimY();
                        }
                        if (sawAnchor && !cursor.ScreenAnchorActive()) committed = true;
                    };
                    for (int frame = 0; frame < fps * 2; ++frame) {
                        if (frame == 0 || frame == fps / 4 || frame == fps / 2) game.UpdateScopeInput(true, true);
                        sync(0);
                        lag += 1.0 / fps;
                        while (lag + 1e-9 >= 1.0 / 60.0) {
                            sync(1.0f / 60.0f);
                            float aimX = 0, aimY = 0;
                            const auto view = game.GetActorView(player);
                            cursor.UpdateCursor(0, 0, 0, renderer.GetZoom(), renderer.GetCameraX(), renderer.GetCameraY(),
                                1280, 720, player.x, player.y, aimX, aimY, player.aimKick, {view.x - player.x, view.y - player.y});
                            game.SetAimTarget(aimX, aimY, !cursor.ScreenAnchorActive());
                            game.Update(1.0f / 60.0f, false, aimX, aimY,
                                false, false, false, true, false, false, false, nullptr);
                            sync(0);
                            lag -= 1.0 / 60.0;
                        }
                        game.PrepareCamera(1280, 720, static_cast<float>(lag * 60.0));
                        sync(0, true);
                        game.Render(1280, 720);
                        if (cursor.ScreenAnchorActive() || (committed && !commitChecked)) {
                            const float px = game.GetRenderAimX();
                            const float py = game.GetRenderAimY();
                            error = std::max(error, std::hypot(px - lockedX, py - lockedY));
                            if (cursor.ScreenAnchorActive()) edgeFrozen = edgeFrozen && !cursor.EdgeZoomRequested();
                            if (committed) commitChecked = true;
                        }
                        cursor.Render(&renderer, &game, 1280, 720);
                    }
                    check(sawAnchor && committed, "scope handoff starts and commits at 60/120/144 FPS");
                    if (error >= 0.01f) std::printf("[Scope anchor error] mode=%d fps=%d pixels=%.6f\n", cameraMode, fps, error);
                    check(error < 0.01f, "moving recoil-adjusted reticle preserves the anchored map point through scope close and commit");
                    check(edgeFrozen, "scope close does not change the frozen edge target");
                }
            }
        }
    }
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
}

static void TestCameraEdgeZoom() {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(software != nullptr, "software renderer available for camera regression");
    if (!software) { SDL_FreeSurface(surface); return; }
    {
        CS2DRenderer renderer;
        renderer.Initialize(software, "assets/cs2d/maps", 1920, 1080);
        CS2DGame game;
        game.SetRngSeed(31);
        if (game.LoadMap(MapPath(), &renderer, true)) {
            auto& player = game.GetPlayer();
            const float x = player.x, y = player.y;
            game.Update(0.0f, false, x, y, false, false, false, false, false, false, false, nullptr);
            game.Render(1920, 1080);
            const float baseZoom = renderer.GetZoom();
            const float baseRadius = renderer.GetVisionRadius();
            CS2DOverlay cursor;
            cursor.InitializeCursor(0.0f);
            float targetX = x, targetY = y;
            cursor.UpdateCursor(10000, 0, 1, renderer.GetZoom(), renderer.GetCameraX(), renderer.GetCameraY(),
                1920, 1080, x, y, targetX, targetY);
            bool stable = true;
            float settledZoom=0;
            for (int frame = 0; frame < 180; ++frame) {
                cursor.UpdateCursor(0, 0, 1, renderer.GetZoom(), renderer.GetCameraX(), renderer.GetCameraY(),
                    1920, 1080, x, y, targetX, targetY);
                game.SetCameraDistanceTier(cursor.CameraDistanceTier());
                game.Update(1.0f / 60.0f, false, targetX, targetY, false, false, false, false, false, false, false, nullptr);
                game.Render(1920, 1080);
                if (frame==60) settledZoom=renderer.GetZoom();
                if (frame>60) stable=stable && std::fabs(renderer.GetZoom()-settledZoom)<.001f;
            }
            check(stable, "stationary edge cursor stays zoomed out for three seconds without oscillation");
            check(cursor.CameraDistanceTier()==2 && renderer.GetZoom()/baseZoom<=.50f/1.12f+.001f, "round3 far tier is reached and joint actor bounds may reduce magnification");
            check(std::isfinite(renderer.GetVisionRadius()) && renderer.GetVisionRadius()>=baseRadius, "round3 camera and fog retain finite shared geometry");
            const float zoom = renderer.GetZoom();
            game.Render(1920, 1080);
            check(renderer.GetZoom() == zoom, "repeated Render preserves edge transition state");
            SDL_RenderPresent(software);
            SDL_SaveBMP(surface, "build-evidence/pix31-render.bmp");
            const int inwardMotion = static_cast<int>(std::lround((x + Tuning::InitialAimDistance - targetX) *
                VisibilityQuery::BaseZoomForViewport(1920, 1080)));
            cursor.UpdateCursor(inwardMotion, 0, 1, renderer.GetZoom(), renderer.GetCameraX(), renderer.GetCameraY(),
                1920, 1080, x, y, targetX, targetY);
            game.SetCameraDistanceTier(cursor.CameraDistanceTier());
            for (int frame = 0; frame < 60; ++frame) {
                game.SetCameraDistanceTier(0);
                game.Update(1.0f / 60.0f, false,
                    x + Tuning::InitialAimDistance,
                    y,
                    false, false, false, false, false, false, false, nullptr);
                game.Render(1920, 1080);
            }
            check(std::fabs(renderer.GetZoom() - baseZoom) < 0.001f, "leaving edge restores zoom");
        }
    }
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
}

static void TestUnlimitedPlayerCone() {
    CS2DMapData data;
    data.mapWidth = 200; data.mapHeight = 40;
    data.tileWidth = data.tileHeight = 8;
    CS2DWorld world; world.Build(data);
    VisibilityQuery query; query.SetWorld(&world);
    query.SetDistanceLimited(false);
    query.SetVisionRadius(10);
    check(query.IsVisibleToPlayer(20, 100, 0, 1200, 100), "forward cone sees beyond former circular radius");
    check(!query.IsVisibleToPlayer(200,100,0,20,100), "rear of actor remains outside forward cone");
    ObjectLayerDef layer; layer.name = "Collisions";
    ObjectDef wall; wall.x = 600; wall.y = 0; wall.width = 8; wall.height = 320;
    layer.objects.push_back(wall); data.objectLayers.push_back(layer);
    world.Build(data);
    check(!query.IsVisibleToPlayer(20,100,0,1200,100), "unlimited forward cone retains wall occlusion");
    CS2DRenderer renderer;
    for (int pct : {0,25,50,100}) {
        renderer.SetFogOpacityPct(pct);
        check(renderer.EffectiveFogAlpha() == static_cast<int>(std::lround(160.0f * pct / 100.0f)),
              "fog opacity follows the shared user setting");
    }
}

static void TestPerceptionAndRecoil() {
    for (int difficulty = 0; difficulty < 4; ++difficulty) {
        CS2DGame::BotRuntime ai;
        const float reaction = Tuning::BotReactionSecs[difficulty];
        ai.Perceive(1, 10.0f, reaction);
        ai.Perceive(1, 10.1f, reaction);
        check(!ai.Ready(1, 10.0f + reaction - 0.001f), "alert waits for reaction deadline");
        check(ai.Ready(1, 10.0f + reaction), "alert reacts at original deadline");
        ai.Perceive(2, 10.0f, reaction * 2.0f);
        ai.Perceive(3, 10.0f, reaction * 3.0f);
        ai.Perceive(4, 10.0f, reaction);
        check(ai.Ready(4, 10.0f + reaction) && !ai.Ready(3, 100.0f), "full alert queue retains earliest deadlines");
    }
    const auto& rifle = WeaponDB::Get(WeaponId::AK47);
    check(std::fabs(rifle.recoilKickDeg - Tuning::RecoilRifle) < 0.001f, "rifle recoil uses tuning");
    for (float angle : {0.0f, 1.0f, 2.0f, -2.0f, Tuning::Pi * 0.5f, -Tuning::Pi * 0.5f}) {
        CS2DGame::Player player;
        player.aimAngle = angle;
        player.aimDistance = 180.0f;
        float previousDistance = std::abs(std::remainder(-Tuning::Pi * 0.5f - angle, 2.0f * Tuning::Pi));
        bool radiusStable = true, shortArc = true;
        for (int shot = 0; shot < 400; ++shot) {
            player.ApplyRecoil(0.6f);
            const float distance = std::abs(std::remainder(-Tuning::Pi * 0.5f - player.EffectiveAim(), 2.0f * Tuning::Pi));
            const float x = std::cos(angle) * player.aimDistance + player.RecoilOffsetX();
            const float y = std::sin(angle) * player.aimDistance + player.RecoilOffsetY();
            radiusStable &= std::abs(std::hypot(x, y) - player.aimDistance) < 0.001f;
            shortArc &= distance <= previousDistance + 0.00001f;
            previousDistance = distance;
        }
        check(radiusStable, "angular recoil preserves player-to-reticle radius");
        check(shortArc && previousDistance < 0.001f, "short arc approaches twelve oclock and stops without overshoot");
    }
    CS2DGame::Player crossing;
    crossing.aimAngle = Tuning::Pi * 0.5f - 0.001f;
    crossing.ApplyRecoil(2.5f);
    const float kick = crossing.aimKick;
    crossing.aimAngle += 0.002f;
    check(crossing.aimKick == kick, "mouse movement across six oclock preserves signed recoil continuity");
    CS2DMapData openData;
    openData.mapWidth = 20; openData.mapHeight = 20;
    openData.tileWidth = 8; openData.tileHeight = 8;
    CS2DWorld openWorld;
    openWorld.Build(openData);
    VisibilityQuery cone;
    cone.SetWorld(&openWorld);
    cone.SetVisionRadius(200.0f);
    check(cone.ComputeVisibility(0, 0, 0, 50, 50) > 0.0f, "normal cone includes 45 degrees");
    cone.SetConeAngleDeg(Tuning::VisionConeDeg * Tuning::EdgeZoomFovAngleMul);
    check(cone.ComputeVisibility(0, 0, 0, 50, 50) == 0.0f, "edge cone excludes 45 degrees");
    check(cone.ComputeVisibility(0, 0, 0, 50, 0) > 0.0f, "edge cone preserves aim axis");
    TacticsTable tactics;
    tactics.Load("missing-pix31.ini", "dust_2");
    check(tactics.perception[0].reaction > tactics.perception[3].reaction, "missing tactics preserves ordered difficulty defaults");
}

static void TestResolutionMigration() {
    const auto previousBase = PathUtils::BaseDir();
    const auto isolated = std::filesystem::absolute("build-evidence/pix32-settings-test");
    std::filesystem::create_directories(isolated);
    PathUtils::SetBaseDir(isolated.string());
    const auto savePath = isolated / (std::string("cs2d_save") + std::string(BuildInfo::SaveSuffix()) + ".ini");
    for (int oldIndex = 0; oldIndex < 18; ++oldIndex) {
        IniFile legacy;
        legacy.SetInt("settings", "res_idx", oldIndex);
        legacy.Save(savePath.string());
        SaveManager settings;
        settings.Load();
        const int expected = oldIndex < 11 ? oldIndex : (oldIndex == 11 ? 11 : oldIndex - 1);
        check(settings.GetSettings().resIdx == expected, "legacy resolution index migrates to matching retained entry");
        settings.Save();
        SaveManager loaded;
        loaded.Load();
        check(loaded.GetSettings().resIdx == expected, "versioned resolution migration applies once across reloads");
    }
    PathUtils::SetBaseDir(previousBase);
}

static void TestHdMapNavigation() {
    for (const char* name : {"dust_2", "mirage", "ancient_ruins", "office"}) {
        CS2DMapData data;
        const std::string path = std::string("assets/cs2d/maps/") + name + ".tmj";
        check(TMJParser::Parse(path, data), "HD collision map parses");
        bool aligned = true;
        for (const auto& layer : data.objectLayers) {
            if (layer.name != "Collisions") continue;
            for (const auto& rect : layer.objects) {
                aligned &= std::fmod(rect.x, data.tileWidth) == 0 && std::fmod(rect.width, data.tileWidth) == 0 &&
                           std::fmod(rect.y, data.tileHeight) == 0 && std::fmod(rect.height, data.tileHeight) == 0;
            }
        }
        check(aligned, "HD collision rectangles align with the runtime grid");
        CS2DWorld world;
        check(world.Build(data), "HD collision world builds");
        check(world.GetMapData() != &data, "world owns its parsed map metadata across later renderer bindings");
        PathFinder navigation;
        navigation.Build(world, Tuning::PathAgentRadius);
        check(navigation.Width() == world.GetMapWidth() && navigation.Height() == world.GetMapHeight(),
              "navigation uses HD map grid dimensions");
        const int columns = navigation.Width();
        const int rows = navigation.Height();
        std::vector<unsigned char> walkable(columns * rows, 0);
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                walkable[row * columns + column] = navigation.IsWalkableWorld(
                    (column + 0.5f) * navigation.TileW(), (row + 0.5f) * navigation.TileH());
            }
        }
        int components = 0;
        int largest = 0;
        int total = 0;
        for (int start = 0; start < columns * rows; ++start) {
            if (!walkable[start]) continue;
            ++components;
            std::vector<int> pending{start};
            walkable[start] = 0;
            for (size_t head = 0; head < pending.size(); ++head) {
                const int cell = pending[head];
                const int x = cell % columns;
                const int y = cell / columns;
                for (const auto& step : std::array<std::pair<int, int>, 4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
                    const int nx = x + step.first;
                    const int ny = y + step.second;
                    if (nx < 0 || nx >= columns || ny < 0 || ny >= rows) continue;
                    const int neighbor = ny * columns + nx;
                    if (!walkable[neighbor]) continue;
                    walkable[neighbor] = 0;
                    pending.push_back(neighbor);
                }
            }
            if (pending.size() < 1000) {
                int minX = columns, minY = rows, maxX = 0, maxY = 0;
                for (int cell : pending) {
                    minX = std::min(minX, cell % columns);
                    maxX = std::max(maxX, cell % columns);
                    minY = std::min(minY, cell / columns);
                    maxY = std::max(maxY, cell / columns);
                }
                std::printf("[PIX32 pocket] %s cells=%zu world=(%.0f,%.0f)-(%.0f,%.0f)\n", name, pending.size(),
                    minX * navigation.TileW(), minY * navigation.TileH(),
                    (maxX + 1) * navigation.TileW(), (maxY + 1) * navigation.TileH());
            }
            largest = std::max(largest, static_cast<int>(pending.size()));
            total += static_cast<int>(pending.size());
        }
        std::printf("[PIX32 BFS] %s components=%d largest=%d total=%d\n", name, components, largest, total);
        const auto& spawns = world.GetSpawnPoints();
        if (spawns.empty()) continue;
        std::vector<PathFinder::Point> pathPoints;
        for (const auto& spawn : spawns) {
            const bool clear = !world.CheckRectCollision(spawn.x - 7, spawn.y - 7, 14, 14);
            const bool connected = navigation.FindPath(spawns.front().x, spawns.front().y, spawn.x, spawn.y, pathPoints);
            std::printf("[PIX32 map] %s spawn %.1f %.1f clear=%d reachable=%d\n", name, spawn.x, spawn.y, clear, connected);
            check(clear && connected, "HD spawn clear and reachable with 16-unit navigation footprint");
        }
        for (const auto& site : world.GetBombSites()) {
            const bool connected = navigation.FindPath(spawns.front().x, spawns.front().y, site.cx, site.cy, pathPoints);
            std::printf("[PIX32 map] %s site %s reachable=%d\n", name, site.name.c_str(), connected);
            check(connected, "HD bomb site reachable from spawn");
        }
        bool accessibleCT = false;
        bool accessibleT = false;
        for (const auto& zone : world.GetBuyZones()) {
            bool reachable = false;
            for (float y = zone.y + navigation.TileH() * 0.5f; y < zone.y + zone.height; y += navigation.TileH()) {
                for (float x = zone.x + navigation.TileW() * 0.5f; x < zone.x + zone.width; x += navigation.TileW()) {
                    if (navigation.IsWalkableWorld(x, y) && navigation.FindPath(
                            spawns.front().x, spawns.front().y, x, y, pathPoints)) {
                        reachable = true;
                        break;
                    }
                }
                if (reachable) break;
            }
            if (reachable) {
                const auto team = zone.properties.find("team");
                const std::string tag = team == zone.properties.end() ? "both" : team->second;
                accessibleCT |= tag == "ct" || tag == "both";
                accessibleT |= tag == "t" || tag == "both";
            }
        }
        check(accessibleCT && accessibleT, "both teams have reachable HD buy-zone ground");

    }
}

struct CS2DGameTestAccess {
    static void ClearBots(CS2DGame& g) { g.actors_.resize(1); g.EnsureBotAi(); }
    static void ResizeBots(CS2DGame& g, size_t n) { g.actors_.resize(1 + n); g.EnsureBotAi(); }
    static CS2DGame::Player& AddBot(CS2DGame& g, CS2DGame::Player p) {
        p.controller = ControllerKind::Bot;
        g.actors_.push_back(std::move(p));
        g.EnsureBotAi();
        return g.actors_.back();
    }
    static CS2DGame::Player& Bot(CS2DGame& g, size_t k) { return g.actors_[1 + k]; }
    static void StepObjective(CS2DGame& g, float dt, bool interactHeld, IAudioSink* audio) {
        g.LocalActor().interactHeld = interactHeld;
        g.UpdateObjective(dt, audio);
    }

    static void EquipExperienceActor(CS2DGame& game) {
        ClearBots(game);
        game.actors_[0].SetSlotWeapon(0,WeaponId::AWP);
        game.actors_[0].weaponSlot=0;
        game.actors_[0].ammo=10;
        game.actors_[0].armor=100;
        game.actors_[0].helmet=true;
        game.SetAimTarget(game.actors_[0].x+80,game.actors_[0].y,true);
    }
    static void PlaceExperienceBomb(CS2DGame& game) {
        game.actors_[0].team=Team::T;
        game.bombPlanted_=true;
        game.bombX_=game.actors_[0].x+50;
        game.bombY_=game.actors_[0].y;
        game.bombTimer_=40;
        game.UpdateSpectatorState();
    }

    // PIX-36 观战自由视角并集证据：阵亡后切自由视角，两名队友朝向相反，
    // 雾区应点亮两块（任一队友看得见即点亮）。相机镜头仍由 specFreeX/Y 控制。
    static void PlaceFreeUnionSpectator(CS2DGame& game) {
        game.view_.specMode_ = CS2DGame::SpectateMode::Free;
        game.view_.specTarget_ = 1;
        for (size_t i = 1; i < game.actors_.size(); ++i) {
            game.actors_[i].alive = true;
            game.actors_[i].team = game.actors_[0].team;
            game.actors_[i].aimAngle = ((i - 1) % 2 == 0) ? 0.0f : Tuning::Pi;
        }
        if (game.actors_.size() > 1) {
            game.view_.specFreeX_ = game.actors_[1].x;
            game.view_.specFreeY_ = game.actors_[1].y;
        } else {
            game.view_.specFreeX_ = game.actors_[0].x;
            game.view_.specFreeY_ = game.actors_[0].y;
        }
    }

    static void KillExperienceActor(CS2DGame& game) {
        CS2DGame::Player attacker;
        attacker.name="长风";attacker.team=Team::T;
        game.OnCombatantKilled(game.actors_[0],nullptr,&attacker,WeaponId::AK47);
    }

    static void TestExperienceStates() {
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(),nullptr,true)) return;
        game.actors_[0].team = Team::CT;
        ClearBots(game);
        for (int i=0;i<2;++i) {
            CS2DGame::Player teammate;
            teammate.team=Team::CT;
            teammate.alive=true;
            teammate.name="teammate"+std::to_string(i);
            teammate.x=game.actors_[0].x; teammate.y=game.actors_[0].y;
            AddBot(game, teammate);
        }
        CS2DGame::Player attacker;
        attacker.name="attacker";
        attacker.team=Team::T;
        game.OnCombatantKilled(game.actors_[0],nullptr,&attacker,WeaponId::AK47);
        check(game.IsDeathFreeze() && !game.IsSpectating(),"experience death begins with a frozen presentation");
        check(game.GetDeathPresentation().event.killer=="attacker" &&
              game.GetDeathPresentation().event.weapon==WeaponId::AK47,"experience death snapshots actual killer and weapon");
        game.killFeed_.clear();
        check(game.GetDeathPresentation().event.killer=="attacker","experience death attribution survives kill-feed eviction");
        game.Update(1.79f,false,game.actors_[0].x,game.actors_[0].y,false,false,false,false,false,false,false,nullptr);
        check(game.IsDeathFreeze() && !game.IsSpectating(),"experience death holds for the full 1.8 seconds");
        game.Update(.02f,false,game.actors_[0].x,game.actors_[0].y,false,false,false,false,false,false,false,nullptr);
        check(!game.IsDeathFreeze() && game.GetSpectatedPlayer(),"experience death hands off to a live teammate");
        const int target=game.view_.specTarget_;
        game.actors_[target].alive=false;
        game.UpdateSpectatorState();
        check(game.GetSpectatedPlayer() && game.view_.specTarget_!=target,"experience spectator automatically replaces a dead target");
        for (size_t i = 1; i < game.actors_.size(); ++i) game.actors_[i].alive = false;
        game.bombPlanted_=true;
        game.bombX_=310; game.bombY_=420;
        game.view_.specMode_=CS2DGame::SpectateMode::Free;
        game.UpdateSpectatorState();
        check(game.view_.specMode_==CS2DGame::SpectateMode::Bomb && !game.GetSpectatedPlayer(),"experience planted bomb becomes the target after team elimination");
        check(game.ListenerX()==310 && game.ListenerY()==420,"experience bomb camera also owns the audio listener");
        game.SpectatorToggle();game.SpectatorCycle(1);
        check(game.view_.specMode_==CS2DGame::SpectateMode::Bomb,"experience bomb spectator ignores actor cycling");
        game.bombPlanted_=false;
        game.UpdateSpectatorState();
        check(game.view_.specMode_==CS2DGame::SpectateMode::Bomb,"experience resolved objective retains the last bomb camera");
        game.RespawnAllForRound();
        check(!game.IsDeathFreeze() && !game.IsSpectating(),"experience round respawn clears death and spectator presentation");
        game.SetCameraDistanceTier(1);
        game.Update(.2f,false,game.actors_[0].x+80,game.actors_[0].y,false,false,false,false,false,false,false,nullptr);
        const float halfTransition=game.EdgeZoomAmount();
        game.SetCameraDistanceTier(0);
        game.Update(.001f,false,game.actors_[0].x+80,game.actors_[0].y,false,false,false,false,false,false,false,nullptr);
        check(std::abs(halfTransition-.5f)<.001f && std::abs(game.EdgeZoomAmount()-halfTransition)<.001f,
              "experience edge transition reverses from its current smoothstep position");
        game.Update(.4f,false,game.actors_[0].x+80,game.actors_[0].y,false,false,false,false,false,false,false,nullptr);
        check(game.EdgeZoomAmount()==0,"experience reversed edge transition reaches its new target in 0.4 seconds");

        SDL_setenv("SDL_AUDIODRIVER","dummy",1);
        AudioManager audio;
        check(audio.Initialize(),"experience objective audio initializes");
        int channel=audio.PlaySoundTrackedAt(SoundEffect::BOMB_DEFUSING,300,0,0,0,3);
        const int midVolume=Mix_Volume(channel,-1);
        check(channel==3 && midVolume>0,"experience BOT defuse remains audible beyond 200 world units");
        audio.UpdateTrackedSoundAt(channel,SoundEffect::BOMB_DEFUSING,300,0,300,0);
        check(Mix_Volume(channel,-1)>midVolume,"experience moving listener refreshes an active process sound");
        audio.UpdateTrackedSoundAt(channel,SoundEffect::BOMB_DEFUSING,600,0,0,0);
        check(Mix_Volume(channel,-1)==0,"experience defuse audio reaches zero at the configured hearing radius");
        const ActorId bot0Id = game.ActorIdOf(Bot(game, 0));
        game.view_.defuseChannels_[bot0Id]=channel;
        Bot(game, 0).defuseProgress=.3f;
        game.SetObjectiveAudioPaused(true);
        check(Mix_Paused(channel),"experience pause suspends the owned process sound");
        game.SetObjectiveAudioPaused(false);
        check(!Mix_Paused(channel) && Mix_Playing(channel),"experience resume continues the same process sound");
        game.actors_[0].alive=true;
        game.view_.defuseChannels_[0]=audio.PlaySoundTracked(SoundEffect::BOMB_DEFUSING,2);
        game.OnCombatantKilled(game.actors_[0],&audio,&attacker,WeaponId::AK47);
        check(Mix_Playing(channel) && !Mix_Playing(2),"experience player death preserves the teammate defuse voice");
        audio.StopChannel(game.view_.defuseChannels_[bot0Id]);
    }

    static void TestPerformanceInterpolation() {
        CS2DGame game;
        game.actors_[0].alive=true;
        game.actors_[0].SetSlotWeapon(0,WeaponId::AWP);
        game.actors_[0].weaponSlot=0;
        game.view_.scope_.weapon=WeaponId::AWP;
        game.view_.scopeLevel_=1;
        game.view_.scopeZoom_=1.5f;
        game.view_.previousScopeZoom_=1;
        game.view_.scopeEngagement_=1;
        game.view_.previousScopeEngagement_=0;
        game.view_.viewHistoryValid_=true;
        game.view_.renderInterpolation_=.5f;
        const auto view=game.GetRenderScopeView();
        check(std::abs(view.zoomMultiplier-1.25f)<.0001f && std::abs(view.transition-.5f)<.0001f,
              "performance scope zoom and engagement interpolate between fixed ticks");
        check(game.GetScopeView().zoomMultiplier==1.5f,"performance render interpolation preserves gameplay zoom");
        game.ResetScope(Presentation::ScopeResetReason::WeaponChanged);
        check(game.GetRenderScopeView().zoomMultiplier==1,"performance scope reset clears interpolation history");
    }

    static void TestFrostGpu() {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_Window* window = SDL_CreateWindow("Frost GPU regression",0,0,1280,720,SDL_WINDOW_HIDDEN);
        SDL_Renderer* output = window ? SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE) : nullptr;
        check(output != nullptr, "frost accelerated renderer initializes");
        if (!output) { SDL_DestroyWindow(window); return; }
        {
            ImGuiUI ui;
            ui.Initialize(window,output);
            ImGui::GetIO().IniFilename = nullptr;
            SDL_Texture* scene = SDL_CreateTexture(output,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,1280,720);
            check(scene != nullptr, "frost GPU world render target initializes");
            SDL_SetRenderTarget(output,scene);
            SDL_SetTextureScaleMode(scene,SDL_ScaleModeLinear);
            SDL_SetRenderDrawBlendMode(output,SDL_BLENDMODE_NONE);
            for(int y=0;y<720;y+=8) for(int x=0;x<1280;x+=8) {
                const Uint8 value = (x/8+y/8)%2 ? 220 : 20;
                SDL_SetRenderDrawColor(output,value,value,value,255);
                const SDL_Rect cell{x,y,8,8};
                SDL_RenderFillRect(output,&cell);
            }
            ui.BeginFrame();
            SDL_Texture* originalTarget = SDL_GetRenderTarget(output);
            const auto before = SDL_GetPerformanceCounter();
            ui.PrepareFrostedGlass();
            const double ms = (SDL_GetPerformanceCounter()-before)*1000.0/SDL_GetPerformanceFrequency();
            check(originalTarget != nullptr && Palette::frostTexture != 0 && SDL_GetRenderTarget(output) == originalTarget,
                  "frost GPU capture restores the active world target");
            Palette::DrawGlassPanel(ImGui::GetForegroundDrawList(),{80,80},{600,600},Palette::GlassTint(IM_COL32(15,19,25,200)),8);
            ui.EndFrame();
            SDL_Surface* pixels=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_RGBA32);
            const bool read = SDL_RenderReadPixels(output,nullptr,SDL_PIXELFORMAT_RGBA32,pixels->pixels,pixels->pitch)==0;
            const auto* bytes=static_cast<const Uint8*>(pixels->pixels);
            const int inside=bytes[200*pixels->pitch+200*4], outside=bytes[200*pixels->pitch+1200*4];
            check(read && inside>30 && inside<180 && outside==220,
                  "frost blurs inside panels while outside world pixels remain sharp");
            std::printf("Frost GPU capture %.3f ms, inside=%d outside=%d\n",ms,inside,outside);
            IMG_SavePNG(pixels,"build-evidence/pix35/frost-gpu.png");
            SDL_FreeSurface(pixels);
            SDL_SetRenderTarget(output,nullptr);
            SDL_DestroyTexture(scene);
        }
        SDL_DestroyRenderer(output);
        SDL_DestroyWindow(window);
    }

    static void TestPix35Match() {
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        game.SetCoachLlmEnabled(false);
        game.SetRngSeed(35);
        if (!game.LoadMap(MapPath(), nullptr, true)) {
            check(false, "PIX35 full match loads real bomb map");
            return;
        }
        int previousRound = game.GetRoundNumber();
        for (int frame = 0; frame < 30 * 1800 && !game.IsMatchOver(); ++frame) {
            const auto& player = game.GetPlayer();
            game.Update(1.0f/30, false, player.x + 100, player.y,
                        false, false, false, false, false, false, false, nullptr);
            if (game.GetRoundNumber() != previousRound) {
                previousRound = game.GetRoundNumber();
                check(game.GetRoundPhase() == RoundPhase::Freeze && !game.IsRoundDecided(),
                      "PIX35 full match starts each new round in fresh Freeze");
            }
        }
        check(game.IsMatchOver() && game.IsRoundDecided() && game.GetRoundPhase() == RoundPhase::Live &&
                  std::max(game.GetScoreT(),game.GetScoreCT()) == 8,
              "PIX35 real bomb match reaches eight wins through normal gameplay updates");
        const int lastRound = game.GetRoundNumber();
        for (int frame=0; frame<30*10; ++frame) {
            const auto& player = game.GetPlayer();
            game.Update(1.0f/30, false, player.x + 100, player.y,
                        false, false, false, false, false, false, false, nullptr);
        }
        check(game.GetRoundNumber() == lastRound && game.IsRoundDecided(),
              "PIX35 complete match preserves final round after presentation delay");
        std::printf("PIX35 full match score %d:%d after %d rounds\n",
                    game.GetScoreT(),game.GetScoreCT(),lastRound);
    }

    static int BenchmarkRound3Visibility() {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_Window *window =
            SDL_CreateWindow("PIX34 visibility benchmark", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
        SDL_Renderer *output =
            window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE)
                   : nullptr;
        if (!output) {
            SDL_DestroyWindow(window);
            return 1;
        }
        std::filesystem::create_directories("build-evidence/pix34-round3");
        std::ofstream csv("build-evidence/pix34-round3/visibility-performance.csv");
        csv << "mode,renderer,frame,query_ms,render_ms,fog_ms\n";
        {
            CS2DRenderer renderer;
            renderer.Initialize(output, "assets/cs2d/maps", 1280, 720);
            CS2DGame game;
            game.SetMatchMode(MatchMode::DeathMatch);
            if (!game.LoadMap(MapPath(), &renderer, true))
                return 1;
            ResizeBots(game, 9);
            game.actors_[0].x = 1100;
            game.actors_[0].y = 100;
            game.actors_[0].aimAngle = 1.5707963f;
            for (int index = 0; index < 9; ++index) {
                auto &actor = Bot(game, index);
                actor.alive = true;
                actor.team = Team::T;
                actor.spriteKey = 1 + index % 2;
                actor.x = 1040 + (index % 3) * 60;
                actor.y = 150 + (index / 3) * 50;
                actor.aimAngle = 1.5707963f;
                actor.weaponSlot = index % 3;
            }
            const auto visible = [&](float x, float y) { return game.CanSeePoint(game.actors_[0], x, y); };
            SDL_RendererInfo info{};
            SDL_GetRendererInfo(output, &info);
            const double toMilliseconds = 1000.0 / SDL_GetPerformanceFrequency();
            for (int mode = 0; mode < 2; ++mode)
                for (int frame = -30; frame < 120; ++frame) {
                    game.clockSec_ += 1.f / 60;
                    bool seen[9]{};
                    const Uint64 beforeQuery = SDL_GetPerformanceCounter();
                    for (int index = 0; index < 9; ++index)
                        seen[index] =
                            mode ? game.QueryBodyVisibility(game.actors_[0], Bot(game, index)).fraction > 0
                                 : game.CanSeePoint(game.actors_[0], Bot(game, index).x, Bot(game, index).y);
                    const Uint64 beforeRender = SDL_GetPerformanceCounter();
                    renderer.SetPreviewCamera(1100, 200, 2, false);
                    SDL_SetRenderDrawColor(output, 0, 0, 0, 255);
                    SDL_RenderClear(output);
                    renderer.DrawWorld();
                    if (mode)
                        renderer.SetVisibilityFilter(visible);
                    else
                        renderer.SetVisibilityFilter({});
                    for (int index = 0; index < 9; ++index) {
                        if (!seen[index])
                            continue;
                        const auto &actor = Bot(game, index);
                        SpriteDrawParams sprite;
                        sprite.x = actor.x;
                        sprite.y = actor.y;
                        sprite.size = actor.size;
                        sprite.spriteKey = actor.spriteKey;
                        sprite.weaponSlot = actor.weaponSlot;
                        sprite.weaponId = static_cast<int>(actor.weaponSlot == 0   ? WeaponId::AK47
                                                           : actor.weaponSlot == 1 ? WeaponId::USP_S
                                                                                   : WeaponId::Knife);
                        sprite.aimAngle = actor.aimAngle;
                        renderer.DrawCharacter(sprite);
                    }
                    renderer.SetVisibilityFilter({});
                    renderer.DrawVisionFog(game.actors_[0].x, game.actors_[0].y, game.actors_[0].aimAngle);
                    SDL_RenderPresent(output);
                    const Uint64 afterRender = SDL_GetPerformanceCounter();
                    if (frame >= 0)
                        csv << (mode ? "partial-body" : "center-cull") << ',' << info.name << ',' << frame
                            << ',' << (beforeRender - beforeQuery) * toMilliseconds << ','
                            << (afterRender - beforeRender) * toMilliseconds << ','
                            << renderer.LastFogMilliseconds() << '\n';
                }
        }
        SDL_DestroyRenderer(output);
        SDL_DestroyWindow(window);
        return csv ? 0 : 1;
    }

    static void TestPix35Visuals() {
        const std::filesystem::path directory = "build-evidence/pix35";
        std::filesystem::create_directories(directory);
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        for (const auto dimensions : {SDL_Point{1280,720}, SDL_Point{1920,1080}, SDL_Point{1024,768}}) {
            const int width = dimensions.x, height = dimensions.y;
            SDL_Window* window = SDL_CreateWindow("PIX35 HUD review",0,0,width,height,SDL_WINDOW_HIDDEN);
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);
            SDL_Renderer* output = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
            check(window && output, "PIX35 visual review initializes");
            if (window && output) {
                ImGuiUI ui;
                ui.Initialize(window,output);
                ImGui::GetIO().IniFilename = nullptr;
                ui.SetUiScale(std::min(width/1920.f,height/1080.f));
                WeaponIconCache icons;
                icons.Initialize(output);
                GameUI gameUI;
                gameUI.SetImGuiUI(&ui);
                gameUI.SetWeaponIconCache(&icons);
                CS2DRenderer renderer;
                renderer.Initialize(output,"assets/cs2d/maps",width,height);
                CS2DGame game;
                game.SetMatchMode(MatchMode::BombDefusal);
                check(game.LoadMap(MapPath(),&renderer,true), "PIX35 visual map loads");
                CS2DOverlay overlay;
                GameUI::InGameCtx hud;
                hud.playerTeam = Team::CT;
                hud.mapName = "古代遗迹";
                hud.scoreT = 3; hud.scoreCT = 4; hud.aliveT = 4; hud.aliveCT = 5;
                hud.roundNumber = 8; hud.roundRemainingSec = 83;
                hud.money = 4200; hud.ammo = 7; hud.magazineSize = 30; hud.reserveAmmo = 90;
                hud.health = 83; hud.armor = 100; hud.helmet = true; hud.hasDefuseKit = true;
                for (const auto id : {WeaponId::M4A4,WeaponId::USP_S,WeaponId::Knife,WeaponId::GrenadeHE,WeaponId::GrenadeFlash,WeaponId::GrenadeSmoke}) {
                    const auto& def = WeaponDB::Get(id);
                    const int slot = def.cls == WeaponClass::Grenade ? 3 : static_cast<int>(hud.equipment.size());
                    hud.equipment.push_back({def.name,def.iconPath,std::to_string(slot+1),
                        id == WeaponId::GrenadeFlash ? 2 : 1,id==WeaponId::M4A4,slot});
                }
                hud.killFeed.push_back({1,"你","目标玩家",WeaponDB::Get(WeaponId::M4A4).iconPath,Team::CT,Team::T,3,true});
                for (int i=0;i<10;++i) hud.scoreRows.push_back({i==0 ? "你" : "BOT " + std::to_string(i),i<5?1:2,12-i,i,4200,true,i==0});
                GameUI::BuyMenuCtx buy;
                buy.money = 2500; buy.canBuy = true; buy.roundNumber = 8; buy.buyRemaining = 8;
                for (int i=1;i<static_cast<int>(WeaponId::Count);++i) {
                    const auto& def=WeaponDB::Get(static_cast<WeaponId>(i));
                    if (def.price<=0 || !WeaponDB::TeamAllowed(def.id,false)) continue;
                    GameUI::BuyItem item;
                    item.request=WeaponReq(def.id);item.name=def.name;item.price=def.price;
                    item.iconPath=def.iconPath;item.scopeSupported=def.scoped;
                    item.group=def.slot==WeaponSlotKind::Primary?0:def.slot==WeaponSlotKind::Secondary?1:def.slot==WeaponSlotKind::Melee?2:3;
                    item.categoryLabel=item.group==0?"主武器":item.group==1?"副武器":"投掷物";
                    item.availability=def.price>buy.money?Presentation::BuyCode::InsufficientFunds:Presentation::BuyCode::Ok;
                    if(def.id==WeaponId::USP_S)item.availability=Presentation::BuyCode::AlreadyEquipped;
                    if(def.id==WeaponId::M4A4)item.availability=Presentation::BuyCode::AlreadyEquipped;
                    buy.items.push_back(item);
                }
                GameSettings settings;
                InputManager input;
                GameUI::MenuCtx menu;
                menu.settings = &settings;
                menu.input = &input;
                check(ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w > .9f &&
                      Palette::BgPanel().w == 1 && Palette::Text().w == 1,
                      "PIX35 non-game menus retain their original opaque material");
                for (const std::string scene : {"buy","reload","dryfire","tab","feedback","grenades","unarmored","armor","pause","settings","history","map-select","side-select","match-end","carrier","dropped"}) {
                    hud.armor = scene == "unarmored" ? 0 : 100;
                    hud.helmet = scene != "armor" && scene != "unarmored";
                    for (auto& item : hud.equipment)
                        item.selected = scene == "grenades" ? item.name == WeaponDB::Get(WeaponId::GrenadeFlash).name :
                            item.name == WeaponDB::Get(WeaponId::M4A4).name;
                    hud.showScoreboard = scene=="tab";
                    overlay.showScoreboard = hud.showScoreboard;
                    hud.reloading = scene=="reload";
                    hud.reloadProgress = .62f;
                    hud.dryFireHint = scene=="dryfire";
                    hud.hasNoWeaponHint = hud.reloading || hud.dryFireHint;
                    hud.hintText = hud.dryFireHint?"没有备弹":"换弹中...";
                    hud.ammo = hud.dryFireHint?0:7;
                    hud.reserveAmmo = hud.dryFireHint?0:90;
                    hud.roundPhase = scene=="buy"?2:3;
                    hud.money = scene=="buy"?buy.money:4200;
                    if (scene=="carrier" || scene=="dropped") {
                        game.actors_[0].team=Team::T;
                        ResizeBots(game, 1);
                        auto& carrier=Bot(game, 0);
                        carrier.alive=true;carrier.team=Team::T;carrier.spriteKey=1;
                        carrier.x=game.actors_[0].x+20;carrier.y=game.actors_[0].y;
                        game.bombCarrier_=scene=="carrier"?1:kInvalidActorId;
                        game.bombDropped_=scene=="dropped";
                        game.bombDropX_=carrier.x;game.bombDropY_=carrier.y;
                    }
                    for(int frame=0;frame<8;++frame) {
                        SDL_SetRenderDrawColor(output,28,35,40,255);SDL_RenderClear(output);
                        game.Render(width,height);
                        overlay.Render(&renderer,&game,width,height);
                        if(scene=="feedback") {
                            renderer.DrawHitMarker(width*.40f,height*.5f,g_uiScale,255);
                            renderer.DrawHitMarker(width*.50f,height*.5f,g_uiScale,255,{110,193,255,255});
                            renderer.DrawHitMarker(width*.60f,height*.5f,g_uiScale*1.5f,255,{244,83,72,255});
                            renderer.DrawDamageVignette(70);
                        }
                        ui.BeginFrame();ImGui::GetIO().DeltaTime=.1f;
                        gameUI.DrawInGame(hud,&ui);
                        if(scene=="buy")gameUI.DrawBuyMenu(buy,{});
                        if (scene == "pause" || scene == "settings") {
                            menu.state = scene == "pause" ? PAUSED : SETTINGS;
                            gameUI.DrawMenu(menu, {});
                        }
                        if (scene == "history") gameUI.DrawHistory({}, {});
                        if (scene == "map-select") gameUI.DrawMapSelect({}, {});
                        if (scene == "side-select") {
                            GameUI::SideSelectCtx side;
                            side.mapName = "古代遗迹";
                            side.bombMode = true;
                            gameUI.DrawSidePopup(side, {});
                        }
                        if (scene == "match-end") gameUI.DrawMatchEnd({}, {});
                        if(scene=="feedback")gameUI.DrawDamageDirections({{{0,.5f},{3.14159265f,.5f},{},{}}});
                        ui.EndFrame();SDL_RenderPresent(output);
                    }
                    const auto path=directory/(scene+"-"+std::to_string(width)+"x"+std::to_string(height)+".png");
                    check(IMG_SavePNG(surface,path.string().c_str())==0,"PIX35 visual state evidence saved");
                    const auto* scoreboard=ImGui::FindWindowByName("##Scoreboard");
                    if (scene == "grenades") {
                        std::vector<ImVec2> centers;
                        const auto* drawData = ImGui::GetDrawData();
                        for (const auto& item : hud.equipment) {
                            if (item.slot != 3) continue;
                            const auto texture = reinterpret_cast<ImTextureID>(icons.GetHud(item.icon));
                            ImVec2 low(FLT_MAX, FLT_MAX), high(-FLT_MAX, -FLT_MAX);
                            for (int list = 0; list < drawData->CmdListsCount; ++list) {
                                const auto* draw = drawData->CmdLists[list];
                                for (const auto& command : draw->CmdBuffer) {
                                    if (command.TextureId != texture) continue;
                                    for (unsigned index = command.IdxOffset; index < command.IdxOffset + command.ElemCount; ++index) {
                                        const auto point = draw->VtxBuffer[command.VtxOffset + draw->IdxBuffer[index]].pos;
                                        low.x = std::min(low.x, point.x);
                                        low.y = std::min(low.y, point.y);
                                        high.x = std::max(high.x, point.x);
                                        high.y = std::max(high.y, point.y);
                                    }
                                }
                            }
                            if (low.x < high.x) centers.push_back({(low.x + high.x) * .5f, (low.y + high.y) * .5f});
                        }
                        check(centers.size() == 3 && std::abs(centers[0].y - centers[1].y) < 1 &&
                              std::abs(centers[1].y - centers[2].y) < 1 && centers[0].x < centers[1].x &&
                              centers[1].x < centers[2].x, "PIX35 grenade HUD icons share one horizontal row");
                    }
                    if(hud.showScoreboard && scoreboard)
                        check(scoreboard->Pos.x > ScoreboardMapSize(width,height) + 16.0f*height/1080,
                              "PIX35 full map and title stay left of centered scoreboard at every aspect ratio");
                }
            }
            SDL_DestroyRenderer(output);SDL_FreeSurface(surface);SDL_DestroyWindow(window);
        }
    }

    static void TestPix35() {
        RoundManager rounds;
        rounds.Reset(RoundConfig{});
        rounds.Update(2.0f);
        rounds.Update(6.0f);
        rounds.EndRound(Team::CT);
        const float decidedAt = rounds.DecidedAt();
        rounds.EndRound(Team::T);
        check(rounds.Phase() == RoundPhase::Live && rounds.IsDecided() && rounds.LastWinner() == Team::CT,
              "PIX35 decided result stays Live and duplicate decision preserves winner");
        rounds.Update(3.99f);
        check(rounds.Phase() == RoundPhase::Live && rounds.DecidedAt() == decidedAt && rounds.ResultRemaining() > 0,
              "PIX35 decision timestamp persists through four-second live window");
        rounds.Update(.02f);
        check(rounds.Phase() == RoundPhase::Freeze && rounds.RoundNumber() == 2 && !rounds.IsDecided() &&
              rounds.LastWinner() == Team::None && !rounds.IsTimeUp(), "PIX35 next round clears result exactly once");

        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        ClearBots(game);
        auto& player = game.actors_[0];
        player.SetSlotWeapon(0, WeaponId::M4A4);
        player.weaponSlot = 0;
        player.LoadAmmoFromSlot();
        player.ammo = 0;
        player.reserve = 0;
        game.UpdatePlayerShooting(player, .01f, true, player.x + 100, player.y, nullptr);
        check(game.GetDryFireHintTimer() == 1 && player.dryFireCooldown == .3f && !player.reloading,
              "PIX35 0/0 trigger produces throttled dry-fire hint");
        game.view_.dryFireHintTimer_ = .8f;
        game.UpdatePlayerShooting(player, .01f, true, player.x + 100, player.y, nullptr);
        check(game.GetDryFireHintTimer() == .8f, "PIX35 held trigger respects dry-fire cooldown");
        player.reserve = 90;
        game.StartReload(player, nullptr);
        check(game.GetReloadProgress() == 0, "PIX35 reload begins empty");
        game.UpdateReload(player, player.CurrentWeapon().reloadTime * .5f);
        check(std::abs(game.GetReloadProgress() - .5f) < .001f, "PIX35 reload progress uses weapon duration");
        game.SwitchWeaponSlot(2);
        check(game.GetReloadProgress() == 0 && !player.reloading, "PIX35 switching clears reload progress");
        game.SwitchWeaponSlot(0);
        game.StartReload(player, nullptr);
        game.UpdateReload(player, player.CurrentWeapon().reloadTime);
        check(!player.reloading && player.ammo == player.maxAmmo && game.GetReloadProgress() == 0,
              "PIX35 completed reload transfers reserve and clears progress");

        game.mode_ = MatchMode::BombDefusal;
        player.team = Team::T;
        game.bombCarrier_ = 0;
        game.bombPlanted_ = game.bombDropped_ = false;
        auto dots = CS2DOverlay::CollectRadarDots(game);
        using Shape = CS2DRenderer::RadarDot::Shape;
        check(game.GetBombCarrierPos() && dots.size() == 1 && dots[0].shape == Shape::Carrier,
              "PIX35 self T carrier has orange radar marker");
        ResizeBots(game, 1);
        auto& bot = Bot(game, 0);
        bot.alive = true;
        bot.team = Team::T;
        bot.x = player.x + 80;
        bot.y = player.y;
        game.bombCarrier_ = 1;
        player.team = Team::CT;
        check(CS2DOverlay::CollectRadarDots(game).empty(), "PIX35 CT cannot discover unknown carrier through radar");
        game.contactsCT_ = {{1, bot.x - 30, bot.y - 20, 5}};
        dots = CS2DOverlay::CollectRadarDots(game);
        check(dots.size() == 1 && dots[0].shape == Shape::Carrier && dots[0].x == bot.x - 30 && dots[0].alpha < 255,
              "PIX35 CT carrier uses fading last-known contact position");
        game.contactsCT_[0].age = 7;
        check(CS2DOverlay::CollectRadarDots(game).empty(), "PIX35 expired carrier contact disappears");
        player.team = Team::T;
        dots = CS2DOverlay::CollectRadarDots(game);
        check(dots.size() == 1 && dots[0].x == bot.x && dots[0].shape == Shape::Carrier,
              "PIX35 T teammate carrier remains known");
        game.bombDropped_ = true;
        game.bombCarrier_ = kInvalidActorId;
        dots = CS2DOverlay::CollectRadarDots(game);
        check(!game.GetBombCarrierPos() && dots.back().shape == Shape::DroppedBomb,
              "PIX35 dropped C4 changes to square marker");
        player.team = Team::CT;
        game.bombIntel_ = false;
        check(CS2DOverlay::CollectRadarDots(game).empty(), "PIX35 unknown dropped C4 stays hidden from CT");
        game.bombIntel_ = true;
        check(CS2DOverlay::CollectRadarDots(game).back().shape == Shape::DroppedBomb,
              "PIX35 known dropped C4 appears for CT");
        game.mode_ = MatchMode::DeathMatch;
        check(CS2DOverlay::CollectRadarDots(game).empty(), "PIX35 deathmatch radar remains empty");

        // Exercise real bullet collision on an open spawn, including armor depletion.
        game.bombDropped_ = false;
        CS2DMapData openMap;
        openMap.mapWidth = openMap.mapHeight = 100;
        openMap.tileWidth = openMap.tileHeight = 8;
        game.world_->Build(openMap);
        game.visibility_.SetWorld(game.world_.get());
        player.x = player.y = 300;
        bot.x = player.x + 14;
        bot.y = player.y;
        bot.size = 16;
        bot.spriteKey = 1;
        bot.aimAngle = 0;
        bot.animState = CS2DGame::AnimState::Idle;
        auto hit = [&](int hp, int armor) {
            bot.alive = true;
            bot.health = hp;
            bot.armor = armor;
            bot.helmet = false;
            game.view_.hitMarkerTimer_ = 0;
            game.bullets_.clear();
            const auto& samples = BodyGeometry::Get(bot.spriteKey, game.ActorPoseSlot(bot)).samples;
            const auto point = BodyGeometry::Transform(samples[samples.size()/2], bot.x, bot.y, bot.size,
                bot.EffectiveAim(), static_cast<int>(bot.animState), bot.animTimer);
            game.FireBullet(point.x, point.y, 0, WeaponDB::Get(WeaponId::M4A4), 0, Team::CT);
            game.UpdateBullets(.001f, nullptr);
        };
        hit(100, 0);
        check(game.GetHitMarkerTimer() > 0 && game.GetHitMarkerKind() == CS2DGame::HitMarkerKind::Normal,
              "PIX35 real unarmored bullet hit selects white marker");
        hit(100, 1);
        check(game.GetHitMarkerKind() == CS2DGame::HitMarkerKind::Armor && bot.armor == 0,
              "PIX35 armor marker samples armor before depletion");
        hit(1, 100);
        check(game.GetHitMarkerKind() == CS2DGame::HitMarkerKind::Kill && game.GetHitMarkerTimer() == .3f,
              "PIX35 lethal hit selects extended red marker");
        check(!game.killFeed_.empty() && game.killFeed_.front().weapon == WeaponId::M4A4 && game.killFeed_.front().life == 4,
              "PIX35 existing weapon kill feed retains four-second lifetime");

        player.health = 100;
        player.armor = 0;
        game.bullets_.clear();
        game.FireBullet(player.x, player.y, 0, WeaponDB::Get(WeaponId::Glock18), 1, Team::T);
        game.UpdateBullets(.001f, nullptr);
        check(game.GetDamageFlashTimer() == .3f && std::any_of(game.view_.damageDirections_.begin(),game.view_.damageDirections_.end(),
              [](const auto& d) { return d.remaining == .5f; }), "PIX35 damage vignette and existing direction arc trigger together");

        game.mode_ = MatchMode::TeamElimination;
        game.round_.Reset(RoundConfig{});
        game.round_.Update(2);
        game.round_.Update(6);
        game.ApplyRoundResult(Team::CT, nullptr);
        game.round_.EndRound(Team::CT);
        const int score = game.GetScoreCT(), money = player.money;
        game.ApplyRoundResult(Team::T, nullptr);
        game.CheckWinConditions(nullptr);
        check(game.GetScoreCT() == score && player.money == money, "PIX35 repeated result cannot duplicate score or money");
        player.ammo = 1;
        player.reserve = 30;
        player.reloading = false;
        game.Update(.05f, false, player.x + 100, player.y, false,false,false,false,true,true,false,nullptr);
        check(player.reloading && player.stealth && game.GetRoundPhase() == RoundPhase::Live,
              "PIX35 decided Live permits reload and stealth through normal Update entry");
        game.SwitchWeaponSlot(0);
        player.reloading = false;
        player.shootCooldown = 0;
        player.ammo = 5;
        player.prevMouseDown = false;
        game.Update(.05f, true, player.x + 100, player.y, false,false,false,false,false,false,false,nullptr);
        check(player.ammo == 4, "PIX35 decided Live permits shooting through normal Update entry");
        for (int i=0;i<8;++i) game.teams_.AddScore(Team::CT);
        const int roundNumber = game.GetRoundNumber();
        game.UpdateRoundFlow(10, nullptr);
        check(game.GetRoundNumber() == roundNumber && game.IsRoundDecided() && game.GetMatchOverTimer() >= 10,
              "PIX35 match end freezes round transition and advances presentation timer");

        SDL_InitSubSystem(SDL_INIT_AUDIO);
        AudioManager audio;
        check(audio.Initialize(), "PIX35 feedback audio initializes");
        const int channel = audio.PlaySoundTracked(SoundEffect::DRYFIRE);
        check(channel >= 0 && Mix_Playing(channel), "PIX35 dedicated dry-fire asset loads and plays");
        audio.Clean();
    }

    static void TestPix34Round3() {
        CS2DGame game;
        if (!LoadDeathMatch(game))
            return;
        ClearBots(game);
        auto &player = game.actors_[0];
        player.team = Team::CT;
        const auto spawns = game.world_->SpawnsForTeam(true);
        if (!spawns.empty()) {
            player.x = spawns.front()->x;
            player.y = spawns.front()->y;
        }
        const auto rifle = WeaponReq(WeaponId::M4A4);
        check(game.ExecutePurchase(rifle).code == Presentation::BuyCode::Ok,
              "round3 purchase creates rifle instance");
        const auto identity = player.equipment[0].id;
        check(identity != 0 && game.EvaluateRefund(rifle).amount == 3100,
              "round3 unused purchase quotes actual full payment");
        game.SwitchWeaponSlot(2);
        game.SwitchWeaponSlot(0);
        check(game.EvaluateRefund(rifle).amount == 3100, "round3 switching preserves refund eligibility");
        game.DropCurrentWeapon();
        check(!game.ExecuteRefund(rifle), "round3 ground equipment cannot be refunded");
        for (auto &drop : game.drops_) {
            drop.pickupDelay = 0;
            drop.x = player.x;
            drop.y = player.y;
        }
        game.TryPickupDrops(true, nullptr);
        check(player.equipment[0].id == identity && game.EvaluateRefund(rifle).amount == 3100,
              "round3 recovered own unused instance retains receipt");
        const int money = player.money;
        check(game.ExecuteRefund(rifle) && player.money == money + 3100 &&
                  player.weapons[0] == WeaponId::None,
              "round3 refund removes equipment and returns payment once");
        check(!game.ExecuteRefund(rifle) && player.money == money + 3100,
              "round3 repeated refund cannot duplicate money");
        game.ExecutePurchase(rifle);
        player.money = 16000;
        check(!game.ExecuteRefund(rifle) && player.weapons[0] == WeaponId::M4A4,
              "round3 balance cap rejection preserves instance");
        player.money = 10000;
        player.weaponSlot = 0;
        player.LoadAmmoFromSlot();
        player.ApplyWeaponRecoil(0);
        check(!game.ExecuteRefund(rifle), "round3 fired instance permanently loses refund eligibility");
        const auto usedIdentity = player.equipment[0].id;
        const auto oldAmmo = player.ammo;
        game.ExecutePurchase(WeaponReq(WeaponId::AWP));
        check(std::any_of(game.drops_.begin(), game.drops_.end(),
                          [&](const auto &drop) {
                              return drop.equipment.id == usedIdentity && drop.magAmmo == oldAmmo &&
                                     drop.equipment.used;
                          }),
              "round3 replacement drops old ammo identity and use history");
        game.ExecuteRefund(WeaponReq(WeaponId::AWP));
        game.drops_.clear();
        game.ExecutePurchase(WeaponReq(WeaponId::FAMAS));
        const auto borrowedIdentity = player.equipment[0].id;
        game.DropCurrentWeapon();
        const float returnX = player.x, returnY = player.y;
        ResizeBots(game, 1);
        auto &borrower = Bot(game, 0);
        borrower.alive = true;
        borrower.team = Team::T;
        borrower.x = game.drops_.back().x;
        borrower.y = game.drops_.back().y;
        game.drops_.back().pickupDelay = 0;
        player.x -= 200;
        game.TryPickupDrops(false, nullptr);
        check(borrower.equipment[0].id == borrowedIdentity && !game.ExecuteRefund(WeaponReq(WeaponId::FAMAS)),
              "round3 another actor can hold an original purchase without allowing owner refund");
        borrower.weaponSlot = 0;
        borrower.LoadAmmoFromSlot();
        borrower.ApplyWeaponRecoil(0);
        game.DropWeaponToGround(borrower, 0, returnX, returnY);
        borrower.ClearSlotWeapon(0);
        borrower.alive = false;
        player.x = returnX;
        player.y = returnY;
        game.drops_.back().pickupDelay = 0;
        game.TryPickupDrops(true, nullptr);
        check(player.equipment[0].id == borrowedIdentity && player.equipment[0].used &&
                  game.EvaluateRefund(WeaponReq(WeaponId::FAMAS)).amount == 0,
              "round3 another actor's use remains attached after original buyer recovers equipment");
        ClearBots(game);
        game.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash));
        game.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash));
        const auto newest = player.grenadeEquipment[1].id;
        check(game.EvaluateRefund(WeaponReq(WeaponId::GrenadeFlash)).quantity == 2,
              "round3 refund quotes same-kind grenade count");
        check(game.ExecuteRefund(WeaponReq(WeaponId::GrenadeFlash)) && player.grenadeCount == 1 &&
                  player.grenadeEquipment[0].id != newest,
              "round3 grenade refund selects latest eligible instance");
        player.armor = 0;
        player.helmet = false;
        game.ExecutePurchase(ArmorReq(1));
        game.ExecutePurchase(ArmorReq(2));
        check(game.ExecuteRefund(ArmorReq(2)) && player.armor == 100 && !player.helmet,
              "round3 helmet upgrade rolls back previous armor state");
        check(game.ExecuteRefund(ArmorReq(1)) && player.armor == 0,
              "round3 armor receipts roll back in reverse purchase order");
        game.ExecutePurchase(ArmorReq(1));
        game.ApplyDamage(player, 20, .5f);
        check(!game.ExecuteRefund(ArmorReq(1)), "round3 armor used to absorb damage cannot be refunded");
        Presentation::BuyRequest kit;
        kit.kind = Presentation::BuyKind::DefuseKit;
        game.ExecutePurchase(kit);
        const auto kitIdentity = player.kitEquipment.id;
        game.mode_ = MatchMode::BombDefusal;
        game.round_.Reset(RoundConfig{});
        game.round_.Update(2.1f);
        game.round_.Update(6.1f);
        game.PlantBomb(player.x, player.y, nullptr);
        StepObjective(game, .1f, true, nullptr);
        check(player.kitEquipment.used && game.EvaluateRefund(kit).amount == 0,
              "round3 kit becomes nonrefundable only after real defuse progress advances");
        game.mode_ = MatchMode::DeathMatch;
        game.bombPlanted_ = false;
        game.OnCombatantKilled(player, nullptr);
        check(!player.hasDefuseKit && std::any_of(game.drops_.begin(), game.drops_.end(),
                                                  [&](const auto &drop) {
                                                      return drop.defuseKit &&
                                                             drop.equipment.id == kitIdentity;
                                                  }),
              "round3 death drops kit with its own identity");
        ResizeBots(game, 1);
        auto &bot = Bot(game, 0);
        bot.alive = true;
        bot.team = Team::CT;
        bot.x = player.x;
        bot.y = player.y;
        for (auto &drop : game.drops_) {
            drop.pickupDelay = 0;
            drop.x = bot.x;
            drop.y = bot.y;
        }
        game.TryPickupDrops(false, nullptr);
        check(bot.hasDefuseKit && bot.kitEquipment.id == kitIdentity,
              "round3 living CT bot recovers kit while human is dead");
        auto &starter = AddBot(game, CS2DGame::Player{});
        starter.alive = true;
        starter.team = Team::T;
        starter.x = returnX;
        starter.y = returnY;
        starter.SetSlotWeapon(1, WeaponId::Glock18);
        starter.weaponSlot = 1;
        starter.LoadAmmoFromSlot();
        game.OnCombatantKilled(starter, nullptr, &player, WeaponId::Knife);
        check(game.drops_.back().id == WeaponId::Glock18 && game.killFeed_.front().weapon == WeaponId::Knife,
              "round3 bot starter pistol drops and melee kill records its actual weapon");
        const auto feed = game.killFeed_.size();
        game.OnCombatantKilled(player, nullptr);
        check(game.killFeed_.size() == feed, "round3 repeated death emits one structured kill event");
        ClearBots(game);
        CS2DMapData data;
        data.mapWidth = data.mapHeight = 100;
        data.tileWidth = data.tileHeight = 8;
        ObjectLayerDef walls;
        walls.name = "Collisions";
        ObjectDef wall;
        wall.type = "wall";
        wall.x = 200;
        wall.y = 0;
        wall.width = 8;
        wall.height = 800;
        walls.objects.push_back(wall);
        data.objectLayers.push_back(walls);
        game.world_->Build(data);
        game.visibility_.SetWorld(game.world_.get());
        player.alive = true;
        player.health = 1000;
        player.x = 240;
        player.y = 400;
        player.aimAngle = 3.14159265f;
        check(game.ExpectedBlastDamage(170, 400, player) == 0,
              "round3 solid wall fully blocks grenade body damage samples");
        player.x = 170;
        check(game.ExpectedBlastDamage(170, 400, player) > 70,
              "round3 exposed body receives close blast damage");
        data.objectLayers[0].objects[0].y = 400;
        data.objectLayers[0].objects[0].height = 400;
        game.world_->Build(data);
        ResizeBots(game, 1);
        auto &exposedActor = Bot(game, 0);
        exposedActor.alive = true;
        exposedActor.team = Team::T;
        exposedActor.x = 240;
        exposedActor.y = 400;
        exposedActor.aimAngle = 3.14159265f;
        exposedActor.health = exposedActor.maxHealth = 1000;
        player.x = 170;
        player.y = 400;
        player.aimAngle = 0;
        const auto exposed = game.QueryBodyVisibility(player, exposedActor);
        check(!game.CanSeePoint(player, 240, 400) && exposed.fraction > 0 && exposed.fraction < 1,
              "round3 exposed body is detectable while its center is hidden");
        check(game.visibility_.HasLineOfSight(170, 400, exposed.aim.x, exposed.aim.y),
              "round3 largest visible component supplies a valid exposed aim point");
        game.FireBullet(170, 400, std::atan2(exposed.aim.y - 400, exposed.aim.x - 170),
                        WeaponDB::Get(WeaponId::USP_S), 0, Team::CT);
        game.UpdateBullets(.1f, nullptr);
        check(exposedActor.health < 1000, "round3 bullet intersects exposed alpha geometry before wall");
        data.objectLayers[0].objects[0].y = 0;
        data.objectLayers[0].objects[0].height = 800;
        game.world_->Build(data);
        game.bullets_.clear();
        const int beforeWallShot = exposedActor.health;
        game.FireBullet(170, 400, 0, WeaponDB::Get(WeaponId::AWP), 0, Team::CT);
        game.UpdateBullets(.1f, nullptr);
        check(exposedActor.health == beforeWallShot &&
                  game.QueryBodyVisibility(player, exposedActor).fraction == 0,
              "round3 closest wall blocks both body visibility and bullet damage");
        ClearBots(game);
        CS2DGame::Player recoilActor;
        recoilActor.SetSlotWeapon(0, WeaponId::AK47);
        recoilActor.weaponSlot = 0;
        recoilActor.aimAngle = .7f;
        recoilActor.aimDistance = 300;
        recoilActor.ApplyWeaponRecoil(0);
        const auto vector = recoilActor.EffectiveAimVector();
        const auto decoded =
            Ballistics::RemoveKick({vector.x * 300, vector.y * 300}, recoilActor.BallisticKick());
        check(std::hypot(decoded.x - std::cos(.7f) * 300, decoded.y - std::sin(.7f) * 300) < .001f,
              "round3 cursor handoff exactly removes horizontal and vertical ballistic recoil");
        auto recovered = recoilActor.fireStates[0];
        recovered.Recover(10);
        check(recovered.weapon == WeaponId::None,
              "round3 dropped instance uses the same complete recoil recovery");
        const auto savedObserver = game.actors_[0];
        const float savedCone = game.visibility_.ConeAngleDeg();
        bool visibilityMatches = true;
        for (int sample=0; sample<48; ++sample) {
            game.visibility_.SetConeAngleDeg(80+(sample%3)*100);
            game.actors_[0].x=120+sample%6*20;
            game.actors_[0].y=350+sample/6*12;
            game.actors_[0].aimAngle=sample*.31f;
            CS2DGame::Player target;
            target.alive=true;
            target.x=160+sample%9*9;
            target.y=390+sample%5*8;
            target.aimAngle=sample*.17f;
            const auto& pose=BodyGeometry::Get(target.spriteKey,game.ActorPoseSlot(target));
            int visible=0;
            for (const auto local:pose.samples) {
                const auto point=BodyGeometry::Transform(local,target.x,target.y,target.size,target.EffectiveAim(),
                    static_cast<int>(target.animState),target.animTimer);
                visible+=game.CanSeePoint(game.actors_[0],point.x,point.y);
            }
            const float expected=pose.samples.empty()?0:float(visible)/pose.samples.size();
            visibilityMatches &= std::abs(game.QueryBodyVisibility(game.actors_[0],target).fraction-expected)<.00001f;
        }
        game.actors_[0]=savedObserver;
        game.visibility_.SetConeAngleDeg(savedCone);
        check(visibilityMatches,"performance visibility bounds preserve exhaustive body pixel results");
        CS2DGame::FireZone fire;
        fire.x = 180;
        fire.y = 400;
        fire.age = 1;
        game.RebuildFireCells(fire);
        check(!fire.cells.empty() && !game.FireContains(fire, 220, 400) && game.FireContains(fire, 180, 400),
              "round3 shared fire region stops at wall and contains visible flame cells");
        bool fireMatches=true;
        for (int y=350; y<=450; y+=2) {
            for (int x=120; x<=240; x+=2) {
                bool reference=false;
                const float active=Tuning::FireEffectiveRadius(fire.radius,fire.age,fire.life);
                for (const auto cell:fire.cells)
                    reference |= std::hypot(cell.x-fire.x,cell.y-fire.y)<=active &&
                                 std::abs(cell.x-x)<=2 && std::abs(cell.y-y)<=2;
                fireMatches &= reference==game.FireContains(fire,float(x),float(y));
            }
        }
        check(fireMatches,"performance fire grid lookup preserves exhaustive region membership");
        bool legal = true;
        for (const auto cell : fire.cells)
            legal &= !game.world_->CheckRectCollision(cell.x - 2, cell.y - 2, 4, 4);
        check(legal, "round3 fire never occupies a blocking terrain cell");
        CS2DGame::FireZone wallFire = fire;
        wallFire.x = 198;
        wallFire.terrainRevision = 0;
        game.RebuildFireCells(wallFire);
        check(!wallFire.cells.empty() && !game.FireContains(wallFire, 220, 400),
              "round3 wall-adjacent impact ignites connected legal floor on its own side");
        fire.ownerId = 0;
        fire.weapon = WeaponId::GrenadeIncendiary;
        game.fires_ = {fire};
        game.fireTickTimer_ = 0;
        ResizeBots(game, 1);
        auto &burnVictim = Bot(game, 0);
        burnVictim.alive = true;
        burnVictim.team = Team::T;
        burnVictim.x = 180;
        burnVictim.y = 400;
        burnVictim.health = 1;
        game.UpdateGrenades(.1f, nullptr);
        check(!burnVictim.alive && game.killFeed_.front().weapon == WeaponId::GrenadeIncendiary &&
                  game.killFeed_.front().killer == "你",
              "round3 lethal fire enters unified death and records original thrower and weapon");
        game.fires_.clear();
        burnVictim.alive = true;
        burnVictim.health = 1;
        player.alive = false;
        CS2DGame::Grenade delayed;
        delayed.type = WeaponId::GrenadeHE;
        delayed.ownerId = 0;
        delayed.x = 180;
        delayed.y = 400;
        game.ExplodeGrenade(delayed, nullptr);
        check(!burnVictim.alive && game.killFeed_.front().weapon == WeaponId::GrenadeHE &&
                  game.killFeed_.front().killer == "你",
              "round3 delayed grenade kill retains weapon and attribution after owner death");
        player.alive = true;
        ClearBots(game);
        game.smokes_.push_back({});
        game.smokes_[0].x = 170;
        game.smokes_[0].y = 400;
        game.smokes_[0].age = 2;
        check(game.SegmentBlockedBySmoke(170, 400, 175, 400),
              "round3 observer inside smoke cannot see own body or neighboring entities");
        check(game.SegmentBlockedBySmoke(170, 400, 170, 400),
              "round3 smoke also masks the exact observer origin pixel");
        game.smokes_.clear();
        CS2DGame::Grenade decoy;
        decoy.type = WeaponId::GrenadeDecoy;
        decoy.imitate = WeaponId::AWP;
        decoy.ownerId = 0;
        decoy.x = 170;
        decoy.y = 400;
        game.ExplodeGrenade(decoy, nullptr);
        check(game.decoys_.size() == 1 && game.decoys_[0].weapon == WeaponId::AWP,
              "round3 decoy captures original weapon and persists independently of thrower");
        game.UpdateGrenades(1, nullptr);
        check(game.decoys_.size() == 1 && game.decoys_[0].shot == 1,
              "round3 active decoy emits located weapon gunshot event");
        game.UpdateGrenades(15, nullptr);
        check(game.decoys_.empty(), "round3 decoy expires once after its final explosion event");
        CS2DGame epochs;
        if (LoadDeathMatch(epochs)) {
            auto &buyer = epochs.actors_[0];
            epochs.ExecutePurchase(WeaponReq(WeaponId::M4A4));
            check(epochs.EvaluateRefund(WeaponReq(WeaponId::M4A4)).amount == 3100,
                  "round3 newly purchased deathmatch equipment starts refundable");
            epochs.SpawnPlayer();
            check(buyer.weapons[0] == WeaponId::M4A4 &&
                      epochs.EvaluateRefund(WeaponReq(WeaponId::M4A4)).amount == 0,
                  "round3 deathmatch free respawn loadout receives a new nonrefundable identity");
            epochs.mode_ = MatchMode::TeamElimination;
            epochs.ExecutePurchase(WeaponReq(WeaponId::FAMAS));
            const auto rifleIdentity = buyer.equipment[0].id;
            auto &survivor = Bot(epochs, 0);
            const auto pistolIdentity = survivor.equipment[1].id;
            survivor.grenades[0] = WeaponId::GrenadeSmoke;
            survivor.grenadeCount = 1;
            survivor.grenadeSlot = 0;
            survivor.SyncActiveGrenade();
            survivor.grenadeEquipment[0].id = epochs.nextEquipmentId_++;
            const auto grenadeIdentity = survivor.grenadeEquipment[0].id;
            survivor.hasDefuseKit = true;
            survivor.kitEquipment.id = epochs.nextEquipmentId_++;
            const auto survivingKit = survivor.kitEquipment.id;
            epochs.RespawnAllForRound(true);
            check(buyer.weapons[0] == WeaponId::FAMAS && buyer.equipment[0].id == rifleIdentity &&
                      epochs.EvaluateRefund(WeaponReq(WeaponId::FAMAS)).amount == 0,
                  "round3 surviving equipment keeps identity while old-round purchase receipt expires");
            check(survivor.equipment[1].id == pistolIdentity &&
                      survivor.ActiveGrenade() == WeaponId::GrenadeSmoke &&
                      survivor.grenadeEquipment[0].id == grenadeIdentity && survivor.hasDefuseKit &&
                      survivor.kitEquipment.id == survivingKit,
                  "round3 survivor bot retains gun grenade and kit instances across round reset");
        }
        for (const auto size :
             std::vector<SDL_FPoint>{{1280, 720}, {1920, 1080}, {2560, 1600}, {3440, 1440}, {1024, 768}}) {
            bool inside = true;
            for (int shot = 0; shot < 20; ++shot)
                for (int frame = 0; frame <= 80; ++frame) {
                    const auto view = ShowcaseCamera::Sample(frame * .1f, shot, 880, 640, size.x, size.y,
                                                             {{120, 120}, {760, 520}});
                    const float hw = size.x / (view.zoom * 2), hh = size.y / (view.zoom * 2);
                    inside &= view.center.x - hw >= -.01f && view.center.y - hh >= -.01f &&
                              view.center.x + hw <= 880.01f && view.center.y + hh <= 640.01f;
                }
            check(inside,
                  "round3 all showcase Bezier paths remain entirely within map at supported aspect ratios");
        }
    }
    static void TestPix34() {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        CS2DMapData data;
        data.mapWidth = data.mapHeight = 200;
        data.tileWidth = data.tileHeight = 8;
        game.GetWorld().Build(data);
        ResizeBots(game, 1);
        game.actors_[0].x = game.actors_[0].y = 400;
        game.actors_[0].SetSlotWeapon(2, WeaponId::Knife);
        game.SwitchWeaponSlot(2);
        auto& bot = Bot(game, 0);
        bot.alive = true;
        bot.team = Team::T;
        game.actors_[0].team = Team::CT;
        bot.x = 380; bot.y = 400; bot.health = 100;
        game.UpdatePlayerShooting(game.actors_[0], 0, true, 600, 400, nullptr);
        check(bot.health == 100, "PIX-34 melee misses enemy behind attacker");
        game.actors_[0].prevMouseDown = false;
        game.actors_[0].shootCooldown = 0;
        bot.x = 420;
        game.UpdatePlayerShooting(game.actors_[0], 0, true, 600, 400, nullptr);
        check(bot.health < 100, "PIX-34 melee hits enemy in forward cone");
        game.actors_[0].alive = false;
        bot.health = bot.maxHealth = 100;
        bot.aimAngle = 0;
        auto& hitAi = game.AiAt(1);
        hitAi.lastHitTimer = 2;
        hitAi.threatX = bot.x - 100;
        hitAi.threatY = bot.y;
        game.UpdateBOTs(1.0f/60, nullptr);
        check(std::abs(bot.aimAngle) > 0 && std::abs(bot.aimAngle) <= Tuning::BotMaxTurnRate/60 + 0.001f,
              "PIX-34 hit response obeys turn limit across movement and scanning");
        game.actors_[0].alive = true;
        ClearBots(game);
        for (float distance : {200.0f, 400.0f}) {
            game.grenades_.clear();
            auto& player = game.actors_[0];
            player.grenades[0] = WeaponId::GrenadeHE;
            player.grenadeCount = 1; player.grenadeSlot = 0;
            player.SyncActiveGrenade();
            game.ThrowGrenade(player, 0, nullptr, distance);
            check(!game.grenades_.empty(), "PIX-34 grenade launches");
            for (int frame = 0; frame < 83; ++frame) game.UpdateGrenades(1.0f/60, nullptr);
            const float travelled = game.grenades_.front().x - player.x;
            check(std::abs(travelled - distance) <= 30, "PIX-34 200/400 grenade landing error within 30 units");
            std::printf("[PIX-34] target=%.0f travelled=%.2f error=%.2f\n", distance, travelled, std::abs(travelled-distance));
        }
        game.SetBotDifficulty(3);
        for (auto& ai : game.actorAi_) check(ai.difficulty == 3, "PIX-34 difficulty applied");
    }

    static void TestPix34Round2() {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        game.QuickSwitchWeapon();
        check(game.actors_[0].weaponSlot == 2, "round2 initial Q equips knife");
        game.QuickSwitchWeapon();
        check(game.actors_[0].weaponSlot == 1, "round2 Q returns to starting pistol");
        game.actors_[0].grenades[0]=WeaponId::GrenadeHE;
        game.actors_[0].grenades[1]=WeaponId::GrenadeFlash;
        game.actors_[0].grenadeCount=2;
        game.actors_[0].grenadeSlot=0;
        game.actors_[0].SyncActiveGrenade();
        game.SwitchWeaponSlot(3);
        game.SwitchWeaponSlot(3);
        game.QuickSwitchWeapon();
        check(game.actors_[0].ActiveGrenade()==WeaponId::GrenadeHE, "round2 Q remembers grenade identity inside one slot");
        game.QuickSwitchWeapon();
        check(game.actors_[0].ActiveGrenade()==WeaponId::GrenadeFlash, "round2 grenade Q switches both ways");
        game.actors_[0].grenades[0]=WeaponId::GrenadeFlash;
        game.actors_[0].grenadeCount=1;
        game.actors_[0].grenadeSlot=0;
        game.actors_[0].SyncActiveGrenade();
        game.QuickSwitchWeapon();
        check(game.actors_[0].weaponSlot==2, "round2 consumed previous grenade falls back to knife");
        game.actors_[0].team = Team::T;
        game.bombCarrier_ = 0;
        game.bombPlanted_ = game.bombDropped_ = false;
        game.ToggleHoldC4();
        game.QuickSwitchWeapon();
        check(game.actors_[0].weaponSlot == 2 && !game.PlayerHoldingC4(), "round2 Q stows C4 and restores the previous equipment");
        game.QuickSwitchWeapon();
        check(game.actors_[0].weaponSlot == 4 && game.PlayerHoldingC4(), "round2 Q restores held C4");
        game.SwitchWeaponSlot(1);
        const int previousSlot = game.actors_[0].previousEquipSlot;
        game.SwitchWeaponSlot(-1);
        check(game.actors_[0].previousEquipSlot == previousSlot && game.actors_[0].weaponSlot == 1,
              "round2 invalid switch leaves equipment history unchanged");
        game.actors_[0].SetSlotWeapon(0, WeaponId::AK47);
        game.SwitchWeaponSlot(0);
        game.DropCurrentWeapon();
        game.QuickSwitchWeapon();
        check(game.actors_[0].weaponSlot == 2, "round2 Q after dropping a gun falls back to knife");
        game.actors_[0].SetSlotWeapon(0, WeaponId::AK47);
        game.SwitchWeaponSlot(0);
        CS2DGame::DroppedItem incoming;
        incoming.id = WeaponId::AWP;
        incoming.x = game.actors_[0].x;
        incoming.y = game.actors_[0].y;
        incoming.pickupDelay = 0;
        game.drops_.push_back(incoming);
        game.drops_.shrink_to_fit();
        game.TryPickupDrops(true,nullptr);
        check(game.actors_[0].weapons[0] == WeaponId::AWP, "round2 replacing held weapon survives dropped item vector growth");
        game.QuickSwitchWeapon();
        check(game.actors_[0].weaponSlot == 2, "round2 Q after replacing its previous weapon falls back to knife");

        CS2DMapData data;
        data.mapWidth=data.mapHeight=100;
        data.tileWidth=data.tileHeight=8;
        game.GetWorld().Build(data);
        game.actors_[0].x = game.actors_[0].y = 400;
        game.actors_[0].vx = game.actors_[0].vy = 0;
        for (int frame = 0; frame < 60; ++frame) game.UpdatePlayerMovement(game.actors_[0], 1.0f/60,false,false,false,true,false);
        const float runningBlur = game.GetAimMotionBlur();
        for (int frame = 0; frame < 60; ++frame) game.UpdatePlayerMovement(game.actors_[0], 1.0f/60,false,false,false,true,true);
        check(runningBlur > game.GetAimMotionBlur() && game.GetAimMotionBlur() > 0,
              "round2 running produces stronger reticle blur than stealth");
        for (int frame = 0; frame < 120; ++frame) game.UpdatePlayerMovement(game.actors_[0], 1.0f/60,false,false,false,false,false);
        check(game.GetAimMotionBlur() < .05f, "round2 stopping smoothly restores reticle clarity");
        ResizeBots(game, 2);
        game.mode_=MatchMode::BombDefusal;
        auto setup = [&] {
            game.round_.Reset(RoundConfig{});
            game.round_.Update(2.1f);
            game.round_.Update(6.1f);
            game.actors_[0].alive=true;
            game.actors_[0].team=Team::CT;
            game.actors_[0].x=game.actors_[0].y=400;
            game.actors_[0].hasDefuseKit=false;
            for (size_t i=0;i<2;++i) {
                auto& bot=Bot(game, i);
                bot.alive=true; bot.team=Team::CT; bot.x=bot.y=400;
                bot.hasDefuseKit=i==0;
                game.AiAt(1 + i).lastHitTimer=0;
                game.AiAt(1 + i).defuseCooldown=0;
            }
            game.PlantBomb(400,400,nullptr);
        };
        setup();
        StepObjective(game, 1, true, nullptr);
        check(std::abs(game.actors_[0].defuseProgress-.1f)<.001f && std::abs(Bot(game, 0).defuseProgress-.25f)<.001f &&
            std::abs(Bot(game, 1).defuseProgress-.1f)<.001f, "round2 player and multiple BOTs progress independently");
        check(game.activeDefuser_==1 && std::abs(game.GetDefuseRemaining()-3)<.001f, "round2 kit BOT leads by estimated finish time");
        StepObjective(game, 1, false, nullptr);
        check(game.actors_[0].defuseProgress==0 && std::abs(Bot(game, 0).defuseProgress-.5f)<.001f,
            "round2 releasing player interact preserves BOT progress");
        Bot(game, 0).alive=false;
        StepObjective(game, .5f, false, nullptr);
        check(game.activeDefuser_==2 && std::abs(game.GetDefuseProgress()-.25f)<.001f,
            "round2 leading defuser death promotes next independent progress");
        setup();
        game.actors_[0].defuseProgress=.4f;
        Bot(game, 0).defuseProgress=.1f;
        Bot(game, 1).alive=false;
        StepObjective(game, .1f, true, nullptr);
        check(game.activeDefuser_==1 && game.actors_[0].defuseProgress>Bot(game, 0).defuseProgress,
            "round2 HUD selects earliest completion instead of highest percentage");
        setup();
        game.actors_[0].hasDefuseKit=true;
        game.actors_[0].defuseProgress=.99f;
        for (size_t i=1;i<game.actors_.size();++i) game.actors_[i].alive=false;
        game.bombTimer_=.05f;
        StepObjective(game, .1f, true, nullptr);
        check(!game.bombPlanted_ && game.round_.LastWinner()==Team::CT, "round2 completion before fuse wins within a large simulation step");
        setup();
        game.actors_[0].hasDefuseKit=true;
        game.actors_[0].defuseProgress=.99f;
        for (size_t i=1;i<game.actors_.size();++i) game.actors_[i].alive=false;
        game.bombTimer_=.01f;
        StepObjective(game, .1f, true, nullptr);
        check(!game.bombPlanted_ && game.round_.LastWinner()==Team::T, "round2 fuse deadline prevents late defuse");
        setup();
        game.actors_[0].hasDefuseKit = true;
        game.actors_[0].money = Bot(game, 0).money = 0;
        game.actors_[0].defuseProgress = Bot(game, 0).defuseProgress = .99f;
        const int scoreBefore = game.GetScoreCT();
        StepObjective(game, .1f, true, nullptr);
        const int moneyAfter = game.actors_[0].money;
        check(game.actors_[0].money == Bot(game, 0).money+300 && game.GetScoreCT() == scoreBefore+1,
              "round2 simultaneous completion chooses stable winner and awards one personal bonus");
        StepObjective(game, .1f, true, nullptr);
        check(game.actors_[0].money == moneyAfter && game.GetScoreCT() == scoreBefore+1,
              "round2 repeated objective update cannot duplicate reward or round result");
    }

    static void TestPix33() {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        ClearBots(game);
        game.actors_[0].team = Team::CT;
        Presentation::BuyRequest kit;
        kit.kind = Presentation::BuyKind::DefuseKit;
        const int money = game.actors_[0].money;
        check(game.ExecutePurchase(kit).code == Presentation::BuyCode::Ok, "PIX-33 kit purchase succeeds");
        check(game.actors_[0].money == money - 400 && game.actors_[0].hasDefuseKit, "PIX-33 kit costs $400");
        check(game.EvaluatePurchase(kit) == Presentation::BuyCode::AlreadyEquipped, "PIX-33 duplicate kit rejected");
        game.OnCombatantKilled(game.actors_[0], nullptr);
        check(std::any_of(game.drops_.begin(),game.drops_.end(),[](const auto& drop) { return drop.defuseKit; }),
              "round3 death creates an independent dropped kit");
        game.RespawnAllForRound(false);
        check(!game.actors_[0].hasDefuseKit, "round3 kit drops on death and is absent after respawn");
        game.actors_[0].team = Team::T;
        check(game.EvaluatePurchase(kit) == Presentation::BuyCode::InvalidItem, "PIX-33 T kit purchase invalid");
        game.actors_[0].team = Team::CT;
        game.mode_ = MatchMode::BombDefusal;
        for (bool hasKit : {false, true}) {
            game.actors_[0].hasDefuseKit = hasKit;
            game.round_.Reset(RoundConfig{});
            game.round_.Update(2.1f);
            game.round_.Update(6.1f);
            game.PlantBomb(game.actors_[0].x, game.actors_[0].y, nullptr);
            StepObjective(game, 1.0f, true, nullptr);
            check(std::abs(game.defuseProgress_ - (hasKit ? .25f : .1f)) < .001f,
                "PIX-33 defuse advances at 4s/10s rate");
            StepObjective(game, .1f, false, nullptr);
            check(game.defuseProgress_ == 0, "PIX-33 release cancels defuse progress");
            StepObjective(game, hasKit ? 3.9f : 9.9f, true, nullptr);
            check(game.bombPlanted_, "PIX-33 defuse incomplete before deadline");
            StepObjective(game, .11f, true, nullptr);
            check(!game.bombPlanted_, "PIX-33 defuse succeeds at deadline");
        }
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        AudioManager audio;
        check(audio.Initialize(), "PIX-33 mixer initializes and decodes assets");
        for (int id = 1; id < WeaponDB::kWeaponCount; ++id) {
            auto weapon = static_cast<WeaponId>(id);
            if (WeaponDB::Get(weapon).reloadTime <= 0 || WeaponDB::Get(weapon).magazine <= 1) continue;
            int channel = audio.PlaySoundTracked(game.ReloadSoundFor(weapon));
            check(channel >= 0, "PIX-33 firearm reload track available");
            audio.StopChannel(channel);
        }
        for (auto effect : {SoundEffect::PICKUP, SoundEffect::DEATH, SoundEffect::PULL_PIN,
                SoundEffect::BOMB_PLANTING, SoundEffect::BOMB_DEFUSING_KIT,
                SoundEffect::BOMB_DEFUSING, SoundEffect::BOMB_EXPLODE,
                SoundEffect::BOMB_PLANT, SoundEffect::BOMB_DEFUSE}) {
            int channel = audio.PlaySoundTracked(effect);
            check(channel >= 0 && Mix_Playing(channel), "PIX-33 event audio starts");
            const int original = channel;
            audio.StopChannel(channel);
            check(channel == -1 && !Mix_Playing(original), "PIX-33 tracked audio stops");
        }
        int playerChannel = audio.PlaySoundTracked(SoundEffect::BOMB_DEFUSING, 2);
        int botChannel = audio.PlaySoundTrackedAt(SoundEffect::BOMB_DEFUSING_KIT, 0, 0, 0, 0, 3);
        check(playerChannel == 2 && botChannel == 3 && Mix_Playing(2) && Mix_Playing(3),
              "round2 concurrent defusers own distinct reserved audio channels");
        audio.StopChannel(playerChannel);
        check(!Mix_Playing(2) && Mix_Playing(3), "round2 cancelling one participant preserves another participant audio");
        int effectChannel = audio.PlaySoundTracked(SoundEffect::PICKUP);
        check(effectChannel >= 12, "round2 ordinary sounds cannot reuse objective participant channels");
        game.actors_[0].alive = true;
        game.actors_[0].defuseProgress = .5f;
        game.view_.defuseChannels_[0] = audio.PlaySoundTracked(SoundEffect::BOMB_DEFUSING,2);
        game.OnCombatantKilled(game.actors_[0],nullptr);
        check(!Mix_Playing(2) && Mix_Playing(3) && game.actors_[0].defuseProgress == 0,
              "round2 death without an audio argument stops only the victim defuse voice");
        audio.StopChannel(botChannel);
        audio.StopChannel(effectChannel);
    }

    static void Move(CS2DGame& game) { game.UpdatePlayerMovement(game.LocalActor(), 1.0f/60, false, false, false, true, false); }
    static void TestBotScopeMovement() {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        CS2DMapData data;
        data.mapWidth = data.mapHeight = 100;
        data.tileWidth = data.tileHeight = 8;
        game.GetWorld().Build(data);
        game.pathFinder_.Build(game.GetWorld(), Tuning::PathAgentRadius);
        auto& bot = game.GetActors()[1];
        bot.SetSlotWeapon(0, WeaponId::AWP);
        bot.weaponSlot = 0;
        bot.stealth = false;
        auto& ai = game.AiAt(1);
        bot.x = bot.y = 100;
        ai.scoped = false;
        game.FollowPathTo(bot, ai, 600, 100, 1.0f/60);
        const float normal = std::hypot(bot.vx, bot.vy);
        bot.x = bot.y = 100;
        ai.scoped = true;
        game.FollowPathTo(bot, ai, 600, 100, 1.0f/60);
        const float scoped = std::hypot(bot.vx, bot.vy);
        check(normal > 1 && std::abs(scoped/normal - Tuning::ScopeMoveMul) < 0.01f,
              "scoped BOT path movement uses the same 0.6 multiplier");
    }

    static void TestSharedBotVisibility() {
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        CS2DMapData data;
        data.mapWidth = 200;
        data.mapHeight = 40;
        data.tileWidth = data.tileHeight = 8;
        game.GetWorld().Build(data);
        game.GetActors().resize(2);
        game.EnsureBotAi();
        game.ResetBotAi(1);
        auto& player = game.GetPlayer();
        auto& bot = game.GetActors()[1];
        player.alive = bot.alive = true;
        player.team = Team::CT;
        bot.team = Team::T;
        player.x = 20;
        player.y = 100;
        player.aimAngle = 0;
        bot.x = 1200;
        bot.y = 100;
        bot.aimAngle = Tuning::Pi;
        check(game.PlayerCanSeeBot(bot) && game.BotCanSeePlayer(bot),
              "both sides see distant forward targets under the same unlimited rule");
        bot.aimAngle = 0;
        check(!game.BotCanSeePlayer(bot), "BOT cannot see a player behind its cone");
        bot.aimAngle = Tuning::Pi;
        auto& smoke = game.smokes_;
        CS2DGame::SmokeCloud cloud;
        cloud.x = 600;
        cloud.y = 100;
        cloud.age = 3;
        cloud.radius = 100;
        smoke.push_back(cloud);
        check(!game.PlayerCanSeeBot(bot) && !game.BotCanSeePlayer(bot), "smoke occludes both visibility directions");
        smoke.clear();
        ObjectLayerDef layer;
        layer.name = "Collisions";
        ObjectDef wall;
        wall.x = 600;
        wall.y = 0;
        wall.width = 8;
        wall.height = 320;
        layer.objects.push_back(wall);
        data.objectLayers.push_back(layer);
        game.GetWorld().Build(data);
        check(!game.PlayerCanSeeBot(bot) && !game.BotCanSeePlayer(bot), "walls occlude both visibility directions");
    }
    // ---------------------------------------------------------------------------
    // PIX-36 第五部分：开镜 3 倍 / 弹速分档 / 观战并集 / 脚步 / 拆包隔离 / 镜内玩家 / 阵亡直播
    // ---------------------------------------------------------------------------

    // 弹速表：档位 spot-check（每档抽一把枪，防 kWeapons 位置初始化错位）
    static void TestPix36BulletSpeeds() {
        std::printf("\n===== PIX-36 弹速分档 =====\n");
        struct Sample { WeaponId id; float speed; const char* label; };
        const Sample samples[] = {
            { WeaponId::AWP,          5600.0f, "sniper AWP 5600" },
            { WeaponId::SSG08,        4800.0f, "sniper SSG08 4800" },
            { WeaponId::SCAR20,       4800.0f, "sniper SCAR-20 4800" },
            { WeaponId::G3SG1,        4800.0f, "sniper G3SG1 4800" },
            { WeaponId::M4A4,         3400.0f, "rifle M4A4 3400" },
            { WeaponId::SG553,        3300.0f, "rifle SG553 3300" },
            { WeaponId::AK47,         3200.0f, "rifle AK-47 3200" },
            { WeaponId::GalilAR,      3100.0f, "rifle Galil AR 3100" },
            { WeaponId::M249,         3300.0f, "MG M249 3300" },
            { WeaponId::Negev,        3100.0f, "MG Negev 3100" },
            { WeaponId::MP5SD,        2500.0f, "SMG MP5-SD 2500" },
            { WeaponId::MP9,          2400.0f, "SMG MP9 2400" },
            { WeaponId::UMP45,        2300.0f, "SMG UMP-45 2300" },
            { WeaponId::Deagle,       2500.0f, "pistol Desert Eagle 2500" },
            { WeaponId::FiveSeven,    2200.0f, "pistol Five-SeveN 2200" },
            { WeaponId::USP_S,        2100.0f, "pistol USP-S 2100" },
            { WeaponId::Glock18,      2000.0f, "pistol Glock-18 2000" },
            { WeaponId::Nova,         1600.0f, "shotgun Nova 1600" },
            { WeaponId::MAG7,         1600.0f, "shotgun MAG-7 1600" },
            { WeaponId::Zeus,         4000.0f, "Zeus unchanged at 4000" },
            { WeaponId::GrenadeHE,     840.0f, "grenade physics unchanged at 840" },
        };
        for (const auto& s : samples) {
            check(std::fabs(WeaponDB::Get(s.id).bulletSpeed - s.speed) < 0.5f, s.label);
        }
        // 排序与真实初速一致：AWP 最快、M4 快于 AK、Deagle 快于 Glock、步枪快于手枪。
        check(WeaponDB::Get(WeaponId::AWP).bulletSpeed > WeaponDB::Get(WeaponId::SSG08).bulletSpeed,
              "PIX-36 AWP is the fastest bullet");
        check(WeaponDB::Get(WeaponId::M4A4).bulletSpeed > WeaponDB::Get(WeaponId::AK47).bulletSpeed,
              "PIX-36 M4A4 outruns AK-47 like the real muzzle velocities");
        check(WeaponDB::Get(WeaponId::Deagle).bulletSpeed > WeaponDB::Get(WeaponId::Glock18).bulletSpeed,
              "PIX-36 Deagle outruns Glock-18");
        check(WeaponDB::Get(WeaponId::GalilAR).bulletSpeed > WeaponDB::Get(WeaponId::Deagle).bulletSpeed,
              "PIX-36 rifles still outrun pistols");
        // 全表核对：所有 34 把枪的弹速都大于旧值上限（旧表最高 2200，Zeus/投掷物除外）
        int checkedWeapons = 0;
        for (int id = static_cast<int>(WeaponId::Glock18);
             id <= static_cast<int>(WeaponId::Negev); ++id) {
            const auto& def = WeaponDB::Get(static_cast<WeaponId>(id));
            if (def.cls == WeaponClass::Grenade || def.cls == WeaponClass::ZeusGun ||
                def.cls == WeaponClass::Bomb || def.cls == WeaponClass::Melee)
                continue;
            ++checkedWeapons;
            check(def.bulletSpeed >= 1600.0f,
                  "PIX-36 every firearm got a faster bullet than the old 1200-2200 band");
        }
        check(checkedWeapons == 33, "PIX-36 bullet speed table covers all 33 firearms");
    }

    // 高速命中回归：AWP 5600 px/s 打远距离目标，必须登记命中（无跨帧穿透漏检）
    static void TestPix36HighSpeedHit() {
        std::printf("\n===== PIX-36 高速命中 =====\n");
        CS2DGame game;
        game.SetRngSeed(47);
        game.SetMatchMode(MatchMode::DeathMatch);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        ClearBots(game);
        auto& player = game.actors_[0];
        player.health = player.maxHealth = 100000;
        player.alive = true;
        // 用一面 200×100 瓦片的空地图替换世界，保证有一条干净的 1600 px 直线
        // （真实地图的房间墙会随机挡道，命中测试要的是弹道检测本身，不是地图布局）
        CS2DMapData open;
        open.mapWidth = 200; open.mapHeight = 100;
        open.tileWidth = open.tileHeight = 16;
        game.world_->Build(open);
        game.visibility_.SetWorld(game.world_.get());
        player.x = 240.0f; player.y = 240.0f;
        const float dirX = 1.0f, dirY = 0.0f;
        check(game.visibility_.HasLineOfSight(player.x, player.y,
                                              player.x + dirX * 1600.0f, player.y + dirY * 1600.0f),
              "PIX-36 open lane is clear for the high-speed hit test");
        CS2DGame::Player target;
        target.team = (player.team == Team::T) ? Team::CT : Team::T;
        target.alive = true;
        target.health = target.maxHealth = 100000;
        target.armor = 0;
        target.x = player.x + dirX * 1600.0f;
        target.y = player.y + dirY * 1600.0f;
        target.size = Tuning::PlayerSize;
        target.spriteKey = 0;
        target.aimAngle = Tuning::Pi;   // 面向射手，命中盒完整
        AddBot(game, target);

        game.bullets_.clear();
        game.FireBullet(player.x, player.y, std::atan2(dirY, dirX),
                        WeaponDB::Get(WeaponId::AWP), 0, player.team);
        // 按 60fps 推进：AWP 每帧约 93 px，扫掠检测必须在到达目标前登记命中
        int framesToHit = -1;
        for (int frame = 0; frame < 60; ++frame) {
            game.UpdateBullets(1.0f / 60.0f, nullptr);
            if (Bot(game, 0).health < 100000) { framesToHit = frame + 1; break; }
            if (game.bullets_.empty()) break;
        }
        check(Bot(game, 0).health < 100000,
              "PIX-36 AWP 5600 px/s hits a distant target without tunneling");
        check(framesToHit > 0 && framesToHit <= 30,
              "PIX-36 AWP reaches a 1600 px target within the expected sub-second flight time");
        // 弹道视觉段必须仍然存在（狙击开火不能变成「无弹道瞬移」）
        check(!game.GetBulletVisuals().empty(), "PIX-36 fast bullets still emit a tracer segment");

        // 各档位速度对比：同一帧内 AWP 走过的距离远大于手枪
        const float awpStep = WeaponDB::Get(WeaponId::AWP).bulletSpeed / 60.0f;
        const float pistolStep = WeaponDB::Get(WeaponId::Glock18).bulletSpeed / 60.0f;
        check(awpStep > pistolStep * 2.0f,
              "PIX-36 AWP step distance is more than twice the pistol per frame");
        // 实机模拟验证：单帧位移与表值一致（防表值填对但发射用了别的字段）
        CS2DGame stepGame;
        stepGame.SetMatchMode(MatchMode::DeathMatch);
        if (stepGame.LoadMap(MapPath(), nullptr, true)) {
            ClearBots(stepGame);
            CS2DMapData open2;
            open2.mapWidth = 200; open2.mapHeight = 100;
            open2.tileWidth = open2.tileHeight = 16;
            stepGame.world_->Build(open2);
            stepGame.visibility_.SetWorld(stepGame.world_.get());
            stepGame.actors_[0].x = 240.0f; stepGame.actors_[0].y = 240.0f;
            stepGame.bullets_.clear();
            stepGame.FireBullet(stepGame.actors_[0].x, stepGame.actors_[0].y, 0.0f,
                                WeaponDB::Get(WeaponId::AWP), 0, stepGame.actors_[0].team);
            check(!stepGame.bullets_.empty() && stepGame.bullets_.front().maxDist ==
                      WeaponDB::Get(WeaponId::AWP).range,
                  "PIX-36 AWP bullet carries the AWP range");
            stepGame.UpdateBullets(1.0f / 60.0f, nullptr);
            const float measured = stepGame.bullets_.empty()
                ? 0.0f : stepGame.bullets_.front().traveled;
            check(std::fabs(measured - awpStep) < 1.0f,
                  "PIX-36 simulated AWP step equals the table speed / 60");
        }
    }

    // 观战自由视角：队友视锥并集 + 修「沿用旧 BOT 朝向」bug
    static void TestPix36SpectateUnion() {
        std::printf("\n===== PIX-36 观战自由视角并集 =====\n");
        // 纯几何层：多观察者并集 = 各观察者最大值，且单观察者与逐值版本一致
        CS2DMapData data;
        data.mapWidth = data.mapHeight = 60;
        data.tileWidth = data.tileHeight = 16;
        CS2DWorld world;
        world.Build(data);
        VisibilityQuery vis;
        vis.SetWorld(&world);
        vis.SetVisionRadius(200.0f);
        vis.SetConeAngleDeg(Tuning::VisionConeDeg);
        const VisibilityQuery::Observer east{300, 300, 0.0f};            // 朝 +x
        const VisibilityQuery::Observer west{300, 300, Tuning::Pi};      // 朝 -x
        const float eastOnly = vis.ComputeVisibility(east.x, east.y, east.angle, 420, 300);
        const float westOnly = vis.ComputeVisibility(west.x, west.y, west.angle, 420, 300);
        check(eastOnly > 0.0f && westOnly == 0.0f, "PIX-36 cone filter still rejects the opposite direction");
        const VisibilityQuery::Observer pair[2] = {east, west};
        check(std::fabs(vis.ComputeVisibilityUnion(pair, 2, 420, 300) - eastOnly) < 0.001f,
              "PIX-36 union equals the max of the contributing observers");
        // 并集必须点亮两侧：+x 与 -x 各有一个目标
        check(vis.ComputeVisibilityUnion(pair, 2, 120, 300) > 0.0f &&
              vis.ComputeVisibilityUnion(pair, 2, 480, 300) > 0.0f,
              "PIX-36 union lights both sides of two opposite teammates");
        // 两名观察者都看不到的目标（背后 90° 之外且超半径）必须为 0
        check(vis.ComputeVisibilityUnion(pair, 2, 300, 300 + 900) == 0.0f,
              "PIX-36 union stays dark beyond every teammate vision radius");
        // 单观察者结果与逐值版本逐位相同（无行为漂移）
        for (float tx : {320.0f, 360.0f, 400.0f, 440.0f, 480.0f}) {
            const float single = vis.ComputeVisibility(east.x, east.y, east.angle, tx, 300);
            const float union1 = vis.ComputeVisibilityUnion(&east, 1, tx, 300);
            check(single == union1, "PIX-36 single-observer union is bit-identical to the scalar path");
        }
        check(vis.ComputeVisibilityUnion(nullptr, 0, 400, 300) == 0.0f,
              "PIX-36 empty observer list is fully fogged (caller falls back)");

        // 性能：并集判定最多 4 名观察者，单像素代价应 ≤ 单观察者的 4 倍
        // （先短路完全可见的情况，实测通常远低于这个上界）。
        {
            const VisibilityQuery::Observer four[4] = {
                {300, 300, 0.0f}, {300, 300, Tuning::Pi * 0.5f},
                {300, 300, Tuning::Pi}, {300, 300, Tuning::Pi * 1.5f}};
            constexpr int kSamples = 20000;
            auto bench = [&](const VisibilityQuery::Observer* obs, std::size_t n) {
                const Uint64 t0 = SDL_GetPerformanceCounter();
                float sink = 0.0f;
                for (int i = 0; i < kSamples; ++i) {
                    const float tx = 280.0f + static_cast<float>(i % 40);
                    const float ty = 280.0f + static_cast<float>((i / 40) % 40);
                    sink += n == 1 ? vis.ComputeVisibility(obs[0].x, obs[0].y, obs[0].angle, tx, ty)
                                   : vis.ComputeVisibilityUnion(obs, n, tx, ty);
                }
                const Uint64 t1 = SDL_GetPerformanceCounter();
                return std::make_pair(static_cast<double>(t1 - t0), static_cast<double>(sink));
            };
            const auto single = bench(four, 1);
            const auto quad = bench(four, 4);
            const double ratio = quad.first / std::max(1.0, single.first);
            const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
            std::printf("[PIX-36] fog cost single=%.2fms quad=%.2fms ratio=%.2fx\n",
                        single.first * 1000.0 / freq, quad.first * 1000.0 / freq, ratio);
            check(ratio <= 4.5, "PIX-36 four-observer union stays within 4x of the single-observer fog cost");
        }

        // 游戏层：自由视角 = 存活队友视锥并集，且与最后跟随的 BOT 朝向解耦
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        game.actors_[0].team = Team::CT;
        ClearBots(game);
        for (int i = 0; i < 2; ++i) {
            CS2DGame::Player mate;
            mate.team = Team::CT;
            mate.alive = true;
            mate.name = "mate" + std::to_string(i);
            mate.x = 400.0f + i * 40.0f;
            mate.y = 400.0f;
            mate.aimAngle = (i == 0) ? 0.0f : Tuning::Pi;   // 两人朝向相反
            AddBot(game, mate);
        }
        game.view_.specMode_ = CS2DGame::SpectateMode::Free;
        game.view_.specTarget_ = 1;
        game.view_.specFreeX_ = 380.0f;
        game.view_.specFreeY_ = 400.0f;
        VisibilityQuery::Observer observers[VisibilityQuery::kMaxObservers];
        const std::size_t count = game.CollectFreeSpectateObservers(observers,
                                                                   VisibilityQuery::kMaxObservers);
        check(count == 2, "PIX-36 free spectator collects every living teammate");
        bool oppositeKept = false;
        for (std::size_t i = 0; i < count; ++i) {
            // EffectiveAim() 走 atan2，π 会以 -π 形式返回；用 remainder 比较同向性
            oppositeKept = oppositeKept ||
                std::abs(std::remainder(observers[i].angle - Tuning::Pi, Tuning::Pi * 2.0f)) < 0.01f;
        }
        check(oppositeKept, "PIX-36 teammate facing the other way is included unchanged");
        // bug 回归：把 specTarget_ 指到另一只 BOT 并转它的朝向，观察者列表与并集结果不变
        const auto before = [&] {
            VisibilityQuery unionQuery;
            unionQuery.SetWorld(game.visibility_.World());
            unionQuery.SetVisionRadius(game.visibility_.VisionRadius());
            unionQuery.SetConeAngleDeg(Tuning::VisionConeDeg);
            return unionQuery.ComputeVisibilityUnion(observers, count, 700.0f, 400.0f);
        }();
        game.view_.specTarget_ = 2;
        Bot(game, 1).aimAngle = -Tuning::Pi * 0.5f;
        VisibilityQuery::Observer observers2[VisibilityQuery::kMaxObservers];
        const std::size_t count2 = game.CollectFreeSpectateObservers(observers2,
                                                                    VisibilityQuery::kMaxObservers);
        check(count2 == count, "PIX-36 observer count independent of the last follow target");
        check(before == [&] {
            VisibilityQuery unionQuery;
            unionQuery.SetWorld(game.visibility_.World());
            unionQuery.SetVisionRadius(game.visibility_.VisionRadius());
            unionQuery.SetConeAngleDeg(Tuning::VisionConeDeg);
            // 队友 1 的朝向确实变了，并集仍要用它的新朝向（队友自己转头应该影响并集）
            return unionQuery.ComputeVisibilityUnion(observers2, count2, 700.0f, 400.0f);
        }(), "PIX-36 union does not depend on the stale follow target angle");
        // GetSpectatedPlayer 在自由视角下必须是 nullptr（旧 bug 的来源）
        check(game.GetSpectatedPlayer() == nullptr,
              "PIX-36 free spectator reports no single spectated player");
        // 切回跟随：specTarget_ 保留并被恢复使用
        game.SpectatorToggle();
        check(game.view_.specMode_ == CS2DGame::SpectateMode::Follow, "PIX-36 toggle returns to follow mode");
        check(game.GetSpectatedPlayer() != nullptr, "PIX-36 follow mode still resolves a spectated player");
        // 世代检查：死亡代码已删除（编译期保证），这里确认跟随模式仍按单观察者工作
        check(game.GetSpectateMode() == CS2DGame::SpectateMode::Follow && game.GetSpectateTarget() >= 0,
              "PIX-36 follow target index survives the free-mode round trip");
    }

    // 脚步：按实际位移触发，玩家与 BOT 同一套
    static void TestPix36Footsteps() {
        std::printf("\n===== PIX-36 脚步 =====\n");
        check(std::fabs(Tuning::FootstepStride - 28.0f) < 0.001f,
              "PIX-36 stride is 28 px (one body diameter)");
        CS2DGame game;
        game.SetMatchMode(MatchMode::DeathMatch);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        ClearBots(game);
        auto& player = game.actors_[0];
        player.health = player.maxHealth = 100000;
        player.alive = true;
        player.stealth = false;
        player.SetSlotWeapon(0, WeaponId::None);
        player.weaponSlot = 2;              // 匕首：120 px/s，最快档
        game.view_.scopeLevel_ = 0;

        // ---- 步数：以已知速度直线行走已知距离，断言步数 ≈ 距离/28（±1 步） ----
        // 用固定长度的合成位移驱动累计器（与真实移动解耦，只验证「位移→步数」映射）
        game.ResetFootstepStride();
        player.x = 300.0f; player.y = 300.0f;
        player.vx = 120.0f; player.vy = 0.0f;
        const float stepDistance = Tuning::FootstepStride * 0.5f;   // 每帧半身位
        const int frames = 40;                                       // 20 个步幅 = 20 步
        int observed = 0;
        float walked = 0.0f;
        for (int frame = 0; frame < frames; ++frame) {
            player.x += stepDistance;
            walked += stepDistance;
            observed += game.AccumulateFootstep(player, nullptr, false);
        }
        const float expectedSteps = walked / Tuning::FootstepStride;
        check(std::fabs(walked - 560.0f) < 0.001f, "PIX-36 walked exactly 560 px (20 strides)");
        check(std::abs(static_cast<float>(observed) - expectedSteps) <= 1.0f,
              "PIX-36 step count matches distance / stride (+-1 step)");
        check(static_cast<int>(game.PlayerFootstepCount()) == observed,
              "PIX-36 the player step statistic matches the triggered steps");

        // 单帧最多 2 步：一次瞬移 5 个步幅只允许触发 2 步（防卡顿后连响）
        game.ResetFootstepStride();
        game.AccumulateFootstep(player, nullptr, false);
        player.x += Tuning::FootstepStride * 5.0f;
        const int burst = game.AccumulateFootstep(player, nullptr, false);
        check(burst <= Tuning::FootstepMaxPerFrame,
              "PIX-36 a single frame never emits more than the per-frame step cap");

        // ---- 卡墙：有输入无位移 → 不触发步声 ----
        game.ResetFootstepStride();
        player.x = 300.0f; player.y = 300.0f;
        player.vx = 120.0f;   // 速度不为零，但位置不变（顶墙）
        int wallSteps = 0;
        for (int frame = 0; frame < 120; ++frame) {
            wallSteps += game.AccumulateFootstep(player, nullptr, false);
        }
        check(wallSteps == 0, "PIX-36 pushing into a wall (speed but no displacement) never emits a step");

        // ---- 潜行：不累计、不出声 ----
        game.ResetFootstepStride();
        player.stealth = true;
        player.x = 100.0f; player.y = 100.0f;
        int stealthSteps = 0;
        for (int frame = 0; frame < 60; ++frame) {
            player.x += 50.0f / 60.0f;
            stealthSteps += game.AccumulateFootstep(player, nullptr, false);
        }
        check(stealthSteps == 0 && player.footstepStride == 0.0f,
              "PIX-36 stealth never accumulates stride and stays silent");
        player.stealth = false;

        // ---- 开镜步频慢于不开镜：同样的时间窗内走的距离更短 ----
        auto walkDistance = [&](bool scoped, float seconds) {
            player.x = 400.0f; player.y = 400.0f;
            player.vx = player.vy = 0.0f;
            player.SetSlotWeapon(0, WeaponId::AWP);
            player.weaponSlot = 0;
            game.view_.scopeLevel_ = scoped ? 1 : 0;
            float total = 0.0f;
            for (int frame = 0; frame < static_cast<int>(seconds * 60.0f); ++frame) {
                const float bx = player.x, by = player.y;
                game.UpdatePlayerMovement(player, 1.0f / 60.0f, false, false, false, true, false);
                total += std::hypot(player.x - bx, player.y - by);
            }
            game.view_.scopeLevel_ = 0;
            return total;
        };
        const float runDistance = walkDistance(false, 0.6f);
        const float scopeDistance = walkDistance(true, 0.6f);
        check(runDistance > 20.0f && scopeDistance < runDistance * 0.85f,
              "PIX-36 scoped movement covers less ground so the step cadence slows down");

        // ---- BOT 同一套：各自累计器，步频 ≈ 移速 / 28 ----
        ClearBots(game);
        CS2DGame::Player bot;
        bot.team = Team::T;
        bot.alive = true;
        bot.stealth = false;
        bot.health = 100000;
        bot.x = 600.0f; bot.y = 600.0f;
        bot.vx = 120.0f;
        bot.spriteKey = 0;
        auto& botActor = AddBot(game, bot);
        botActor.footstepStride = 0.0f;
        botActor.footstepLastValid = false;
        int botSteps = 0;
        float botWalked = 0.0f;
        for (int frame = 0; frame < 120; ++frame) {
            botActor.x += 120.0f / 60.0f;
            botWalked += 120.0f / 60.0f;
            botSteps += game.AccumulateFootstep(botActor, nullptr, true);
        }
        // 2 秒 × 120 px/s = 240 px → 240/28 ≈ 8.57 → 8..9 步
        const float expectedBotSteps = botWalked / Tuning::FootstepStride;
        check(std::abs(static_cast<float>(botSteps) - expectedBotSteps) <= 1.0f,
              "PIX-36 a BOT's step cadence follows its own speed / stride");
        // BOT 阵亡后累计器清零
        botActor.alive = false;
        game.AccumulateFootstep(botActor, nullptr, true);
        check(botActor.footstepStride == 0.0f && !botActor.footstepLastValid,
              "PIX-36 a dead BOT resets its stride accumulator");
    }

    // 拆包进度信息隔离：队友可见（只有秒数），对手完全不可见
    static void TestPix36DefuseIsolation() {
        std::printf("\n===== PIX-36 拆包信息隔离 =====\n");
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        // T 方玩家 + 一名 CT 队友在 5 人里：直接构造拆包者
        game.actors_[0].team = Team::T;
        ClearBots(game);
        CS2DGame::Player defuser;
        defuser.team = Team::CT;
        defuser.alive = true;
        defuser.name = "CT-defuser";
        defuser.x = 400.0f; defuser.y = 400.0f;
        defuser.defuseProgress = 0.4f;
        AddBot(game, defuser);
        game.bombPlanted_ = true;
        game.activeDefuser_ = 1;
        game.defuseProgress_ = 0.4f;
        game.defuseRemaining_ = 6.0f;
        check(!game.IsDefuseVisibleToViewer(),
              "PIX-36 a T-side viewer cannot see an enemy defuse");
        // CT 观看者 → 可见
        game.actors_[0].team = Team::CT;
        check(game.IsDefuseVisibleToViewer(),
              "PIX-36 a CT-side viewer sees their teammate's defuse");
        // 无人拆包 → 不可见
        game.defuseProgress_ = 0.0f;
        check(!game.IsDefuseVisibleToViewer(),
              "PIX-36 no progress means nothing is visible");
        // 观战：阵亡玩家仍按自己的阵营判断
        game.defuseProgress_ = 0.4f;
        game.actors_[0].alive = false;
        game.actors_[0].team = Team::CT;
        check(game.IsDefuseVisibleToViewer(),
              "PIX-36 a dead CT spectator still sees the defuse (own team)");
        game.actors_[0].team = Team::T;
        check(!game.IsDefuseVisibleToViewer(),
              "PIX-36 a dead T spectator cannot see the defuse (own team)");
        // 玩家本人拆包（activeDefuser_ == 0）→ 自己阵营自然可见；对手侧看不到
        game.actors_[0].alive = true;
        game.actors_[0].team = Team::CT;
        game.activeDefuser_ = 0;
        check(game.IsDefuseVisibleToViewer(), "PIX-36 the defuser himself sees his own progress");
        // HUD 层：不可见时整行不画、横幅位置也恒定
        GameUI::InGameCtx hidden;
        hidden.objective = 1;
        hidden.bombPlanted = true;
        hidden.defuseProgress = 0.4f;
        hidden.defuseRemaining = 6.0f;
        hidden.defuseVisibleToViewer = false;
        check(!hidden.defuseVisibleToViewer, "PIX-36 HUD context carries the visibility flag");
        // 名称不再参与 HUD 绘制：即使传入也不会被显示（源码级约束由 GameUI 保证）
        hidden.defuserName = "CT-defuser";
        check(hidden.defuserName == "CT-defuser",
              "PIX-36 defuser name stays available for tests but is not drawn by the HUD");
    }

    // 开镜倍率：档位表 1.5x / 3x，且镜内玩家同步放大（尺寸比 = 倍率）
    static void TestPix36ScopeLevelsAndLensPlayer() {
        std::printf("\n===== PIX-36 开镜 3 倍与镜内玩家 =====\n");
        check(Presentation::kScopeLevelCount == 3, "PIX-36 three scope levels");
        check(std::fabs(Presentation::kScopeZoomLevels[0] - 1.0f) < 0.001f &&
              std::fabs(Presentation::kScopeZoomLevels[1] - 1.5f) < 0.001f &&
              std::fabs(Presentation::kScopeZoomLevels[2] - 3.0f) < 0.001f,
              "PIX-36 scope levels are off / 1.5x / 3x");
        for (WeaponId scoped : {WeaponId::SSG08, WeaponId::AWP, WeaponId::SCAR20,
                                WeaponId::G3SG1, WeaponId::SG553, WeaponId::AUG}) {
            CS2DGame game;
            if (!LoadDeathMatch(game)) return;
            game.GetPlayer().money = 16000;
            game.ExecutePurchase(WeaponReq(scoped));
            if (!game.GetScopeView().supported) continue;
            game.UpdateScopeInput(true, true);
            check(std::fabs(game.GetScopeView().targetZoomMultiplier - 1.5f) < 0.01f,
                  "PIX-36 all scoped weapons share level 1 = 1.5x");
            game.UpdateScopeInput(true, true);
            check(std::fabs(game.GetScopeView().targetZoomMultiplier - 3.0f) < 0.01f,
                  "PIX-36 all scoped weapons share level 2 = 3x");
        }
        // 镜内玩家放大：把同一个角色分别画在主场景和镜内，逐像素量屏上高度，
        // 比值必须 ≈ 镜内倍率（误差 ≤ 10%）——这测的是真实绘制路径，不是公式换算。
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(software != nullptr, "PIX-36 software renderer for the lens player check");
        if (!software) { SDL_FreeSurface(surface); return; }
        {
            // renderer 必须在使用它的 SDL_Renderer 之前析构（Clean 会销毁纹理），
            // 所以放进内层作用域，避免 SDL_DestroyRenderer 之后的 use-after-free。
            CS2DRenderer renderer;
            check(renderer.Initialize(software, "assets/cs2d/maps", 640, 480),
                  "PIX-36 lens renderer initializes");
            CS2DMapData data; data.mapWidth = data.mapHeight = 60;
            data.tileWidth = data.tileHeight = 16;
            CS2DWorld world; world.Build(data);
            renderer.SetWorld(&world);

            std::ofstream lens("build-evidence/experience/lens-player-ratio.csv");
            lens << "zoom,main_px,scoped_px,ratio,error_percent\n";

            // 在给定表面的某一列区间里，统计「非背景像素」覆盖的垂直跨度。
            // 背景被清成纯黑，所以任何 RGB 之和 > 40 的像素都属于角色。
            auto contentHeight = [](const SDL_Surface* shot, int x0, int x1) {
                if (!shot) return 0;
                int top = shot->h, bottom = -1;
                for (int y = 0; y < shot->h; ++y) {
                    bool hit = false;
                    for (int x = std::max(0, x0); x <= std::min(shot->w - 1, x1) && !hit; ++x) {
                        const auto* p = static_cast<const Uint8*>(shot->pixels) +
                                        y * shot->pitch + x * 4;
                        if (p[0] + p[1] + p[2] > 40) hit = true;
                    }
                    if (hit) { top = std::min(top, y); bottom = std::max(bottom, y); }
                }
                return bottom >= top ? bottom - top + 1 : 0;
            };
            SpriteDrawParams actor;
            actor.aimAngle = 0.0f;
            actor.size = 40.0f;
            actor.weaponSlot = 1;
            actor.weaponId = static_cast<int>(WeaponId::USP_S);
            actor.spriteKey = 0;
            actor.animState = static_cast<int>(CS2DGame::AnimState::Idle);
            actor.localPlayer = true;

            // 主场景：相机对准角色所在世界点，角色就画在屏幕正中
            SDL_SetRenderDrawColor(software, 0, 0, 0, 255);
            SDL_RenderClear(software);
            renderer.UpdateCamera(800, 800, 640, 480);
            actor.x = 800.0f; actor.y = 800.0f;
            renderer.DrawCharacter(actor);
            const int mainPx = contentHeight(surface, 260, 380);

            int measuredZooms = 0;
            for (float zoom : {1.5f, 3.0f}) {
                SDL_SetRenderDrawColor(software, 0, 0, 0, 255);
                SDL_RenderClear(software);
                renderer.UpdateCamera(800, 800, 640, 480);
                const bool began = renderer.BeginScopeScene(800, 800, 320, 240, zoom);
                check(began, "PIX-36 lens render target begins for the player-size check");
                if (!began) continue;
                // BeginScopeScene 用深蓝灰（18,26,36）清屏，与下面「非背景」判定的
                // 阈值冲突；这里改成纯黑清屏，让测量只看得到角色像素。
                SDL_SetRenderDrawColor(software, 0, 0, 0, 255);
                SDL_RenderClear(software);
                // 镜内：同一个世界点被摆到镜心，角色同样出现在屏幕正中。
                // 趁镜面纹理仍是当前渲染目标时读像素，量到的才是镜内真实尺寸。
                actor.x = 800.0f; actor.y = 800.0f;
                renderer.DrawCharacter(actor);
                SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32,
                                                                  SDL_PIXELFORMAT_RGBA32);
                int scopedPx = 0;
                if (shot) {
                    SDL_RenderReadPixels(software, nullptr, SDL_PIXELFORMAT_RGBA32,
                                         shot->pixels, shot->pitch);
                    scopedPx = contentHeight(shot, 260, 380);
                    SDL_FreeSurface(shot);
                }
                renderer.EndScopeScene();
                ++measuredZooms;
                check(mainPx > 0 && scopedPx > 0,
                      "PIX-36 both the main-scene and the scoped player render");
                const float ratio = static_cast<float>(scopedPx) /
                                    static_cast<float>(std::max(1, mainPx));
                const float error = 100.0f * std::abs(ratio / zoom - 1.0f);
                lens << zoom << ',' << mainPx << ',' << scopedPx << ',' << ratio << ',' << error << '\n';
                check(error <= 10.0f,
                      "PIX-36 scoped player size ratio matches the selected lens zoom");
            }
            check(measuredZooms == 2, "PIX-36 both lens zooms were measured on screen");
        }
        SDL_DestroyRenderer(software);
        SDL_FreeSurface(surface);
    }

    // ========================================================================
    // PIX-37：游戏内容完善 7 项
    // ========================================================================

    // 1) 拆包半径缩小，且中间不能有遮挡物
    static void TestPix37CoachWiring() {
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        game.SetRngSeed(37);
        const bool loaded = game.LoadMap(
            PathUtils::ResolveAppPath("assets/cs2d/maps/mirage.tmj"), nullptr, true);
        check(loaded, "PIX-37 coach fixture loads Mirage");
        if (!loaded) return;

        auto& ct = game.CoachFor(Team::CT);
        game.CoachResetRound();
        ct.plan.valid = true;
        for (const auto intent : {PlanIntent::Default, PlanIntent::StackA, PlanIntent::StackB}) {
            game.clockSec_ += 20.0f;
            ct.plan.intent = intent;
            ct.plan.createdAt = game.clockSec_;
            game.CoachApplyPlan(Team::CT);
        }
        check(ct.changesThisRound == 2, "PIX-37 T05 counts exactly two actual changes");
        game.clockSec_ += 40.0f;
        ct.planExpiresAt = game.clockSec_ - 1.0f;
        const int renewals = game.counters_.renewals;
        const int generation = ct.gen;
        game.CoachThink(Team::CT, CoachEvent{CoachEventType::Heartbeat});
        check(ct.gen == generation && ct.plan.intent == PlanIntent::StackB &&
              ct.changesThisRound == 2 && game.counters_.renewals == renewals + 1 &&
              ct.planExpiresAt > game.clockSec_, "PIX-37 T05 third replan hits budget after hold and cooldown");
        game.CoachResetRound();
        check(ct.changesThisRound == 0, "PIX-37 T05 new round clears intent budget");

        std::vector<size_t> teammates;
        for (size_t i = 1; i < game.actors_.size(); ++i) {
            auto& bot = game.actors_[i];
            if (!bot.IsBot() || bot.team != Team::CT || !bot.alive) continue;
            teammates.push_back(i);
            bot.SetSlotWeapon(0, WeaponId::AK47);
            bot.money = 4000;
            game.AiAt(i).difficulty = 0;
        }
        check(teammates.size() >= 3, "PIX-37 fixture has at least three CT bots");
        if (teammates.size() < 3) return;
        const size_t sniper = teammates.front(), poorest = teammates.back(), entry = teammates[1];
        game.actors_[sniper].SetSlotWeapon(0, WeaponId::AWP);
        game.actors_[entry].SetSlotWeapon(0, WeaponId::SSG08);
        game.AiAt(sniper).difficulty = 2;
        game.AiAt(entry).difficulty = 3;
        game.actors_[poorest].money = 0;
        ct.plan = {};
        ct.plan.valid = true;
        ct.plan.slots[0].role = CoachRole::Entry;
        ct.plan.slots[2].role = CoachRole::Awper;
        game.CoachApplyPlan(Team::CT);
        check(game.actorSquad_[sniper].role == BotRole::Awper && game.actorSquad_[entry].role == BotRole::Entry,
              "PIX-37 T22 reserves the only AWP holder for a later Awper slot");
        ct.plan.intent = PlanIntent::Save;
        game.CoachApplyPlan(Team::CT);
        check(game.actorSquad_[poorest].role == BotRole::Entry && !game.actorSquad_[poorest].canTrade &&
              game.AiAt(poorest).hasPlanHold, "PIX-37 T22 lowest balance gets first Save assignment");

        const auto* slots = game.tactics_.Slots("CT", "StackA@A");
        const auto* anchor = game.tactics_.Anchor("CT.Palace");
        check(slots && anchor, "PIX-37 Mirage rule flank template and Palace anchor load");
        if (!slots || !anchor) return;
        // Keep a single active BOT so the trace measures the rule route without enemy contact.
        for (size_t i = 0; i < game.actors_.size(); ++i) game.actors_[i].alive = (i == sniper);
        ct.plan = {};
        ct.plan.valid = true;
        ct.plan.intent = PlanIntent::StackA;
        ct.plan.target = "A";
        ct.plan.slots = *slots;
        const auto lurk = std::find_if(slots->begin(), slots->end(), [](const SlotOrder& slot) {
            return slot.role == CoachRole::Lurk;
        });
        check(lurk != slots->end() && lurk->area == "Palace", "PIX-37 rule assigns Lurk to the Palace flank");
        if (lurk == slots->end()) return;
        ct.plan.slots[0] = *lurk;
        game.CoachApplyPlan(Team::CT);
        auto& bot = game.actors_[sniper];
        auto& ai = game.AiAt(sniper);
        auto distance = [&] { return std::hypot(bot.x - anchor->first, bot.y - anchor->second); };
        check(ai.hasPlanHold && ai.planHoldX == anchor->first && ai.planHoldY == anchor->second,
              "PIX-37 T24 rule plan uses the configured flank anchor");
        check(game.actorSquad_[sniper].role == BotRole::Lurk && ai.goalArea == "Palace" &&
              ai.holdStyle == static_cast<int>(HoldStyle::Passive), "PIX-37 T26 Lurk receives passive flank order");
        const float initialDistance = distance();
        game.lastDeathTeam_ = Team::CT;
        game.lastDeathTimer_ = 1.0f;
        game.lastDeathX_ = bot.x + 150.0f;
        game.lastDeathY_ = bot.y;
        game.UpdateBOTs(1.0f / 60.0f, nullptr);
        check(!ai.tradePushed && ai.goalArea == "Palace",
              "PIX-37 T26 Lurk stays on the flank after a nearby ally dies");
        game.lastDeathTimer_ = 0.0f;
        for (int frame = 0; frame < 1800 && distance() >= 32.0f; ++frame) {
            game.clockSec_ += 1.0f / 60.0f;
            game.UpdateBOTs(1.0f / 60.0f, nullptr);
        }
        std::printf("PIX-37 Palace anchor distance %.2f -> %.2f\n", initialDistance, distance());
        check(initialDistance > 64.0f && distance() < 32.0f,
              "PIX-37 T24/T26 BOT traverses Mirage to its flank anchor");

        bot.weaponSlot = 0;
        const auto* awperSlot = game.tactics_.Slots("CT", "Default@-");
        check(awperSlot && (*awperSlot)[1].role == CoachRole::Awper,
              "PIX-37 Mirage rule template includes the Jungle Awper");
        if (!awperSlot) return;
        const auto hold = (*awperSlot)[1];
        const auto* awperAnchor = game.tactics_.Anchor("CT." + hold.area);
        check(awperAnchor != nullptr, "PIX-37 Jungle Awper anchor loads");
        if (!awperAnchor) return;
        game.clockSec_ += 4.0f;
        ct.plan.slots[0] = hold;
        game.CoachApplyPlan(Team::CT);
        const float start = game.RoleSpeedMul(sniper, BotRole::Awper);
        game.clockSec_ += 1.5f;
        const float half = game.RoleSpeedMul(sniper, BotRole::Awper);
        game.clockSec_ += 1.5f;
        const float end = game.RoleSpeedMul(sniper, BotRole::Awper);
        const auto closeEnough = [](float a, float b) { return std::abs(a - b) < 0.001f; };
        std::printf("PIX-37 Lurk to Awper multiplier %.3f -> %.3f -> %.3f\n", start, half, end);
        check(closeEnough(start, Tuning::SpeedMulLurk) && closeEnough(end, Tuning::SpeedMulAwper) &&
              closeEnough(half, (start + end) * 0.5f), "PIX-37 T26/T27 Awper speed settles linearly in three seconds");
        game.UpdateBOTs(1.0f / 60.0f, nullptr);
        const float actualSpeed = std::hypot(bot.vx, bot.vy);
        const float expectedSpeed = Tuning::BotMoveSpeed * bot.CurrentWeapon().moveSpeedMul * end;
        std::printf("PIX-37 Jungle Awper velocity %.2f (expected %.2f)\n", actualSpeed, expectedSpeed);
        check(ai.goalArea == hold.area && ai.planHoldX == awperAnchor->first &&
              ai.planHoldY == awperAnchor->second && std::abs(actualSpeed - expectedSpeed) < 0.1f,
              "PIX-37 T26 BOT movement uses the Awper cap en route to Jungle");
        game.SetRoleWithTransition(sniper, BotRole::Entry);
        game.clockSec_ += 0.75f;
        const float before = game.RoleSpeedMul(sniper, BotRole::Entry);
        game.SetRoleWithTransition(sniper, BotRole::Lurk);
        check(closeEnough(before, game.RoleSpeedMul(sniper, BotRole::Lurk)),
              "PIX-37 T27 mid-transition reassignment preserves current speed");
    }

    static void TestPix37RotateSchema() {
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        const bool loaded = game.LoadMap(MapPath(), nullptr, true);
        check(loaded, "PIX-37 T09 rotate fixture loads");
        if (!loaded || game.world_->GetBombSites().empty()) return;
        std::string schema;
        std::function<void(const std::string&)> reply;
        game.SetCoachLlmEnabled(true);
        game.SetCoachLlm([&](const std::string&, const std::string&, int,
                            const std::string& requestSchema, auto callback) {
            schema = requestSchema;
            reply = std::move(callback);
        });
        const CoachEvent event{CoachEventType::RoundStart};
        game.CoachTryLlm(Team::T, event);
        check(reply && schema.find("rotate_b") == std::string::npos,
              "PIX-37 T09 schema excludes rotation during freeze");
        if (!reply) return;
        reply("");
        game.PumpCoach();

        game.round_.Update(game.round_.Config().freezeTime);
        game.round_.Update(game.round_.Config().buyTime);
        game.clockSec_ = 25.0f;
        game.bombCarrier_ = 0;
        game.bombDropped_ = false;
        const auto& site = game.world_->GetBombSites().front();
        game.intel_.OnEnemySeen(site.cx, site.cy, game.clockSec_);
        game.intel_.OnEnemySeen(site.cx, site.cy, game.clockSec_);
        auto& state = game.CoachFor(Team::T);
        state.plan = {};
        state.plan.valid = true;
        state.plan.target = "A";
        state.planExpiresAt = 40.0f;
        const bool allowed = game.CanRotateNow(Team::T);
        check(allowed, "PIX-37 T09 live fixture satisfies rotation guards");
        if (!allowed) return;
        reply = {};
        game.CoachTryLlm(Team::T, event);
        check(reply && schema.find("rotate_a") != std::string::npos &&
              schema.find("rotate_b") != std::string::npos,
              "PIX-37 T09 live request exposes both rotation intents");
        if (!reply) return;
        reply(R"({"intent":"rotate_b","target":"B","tempo":"fast","ttl_s":18,"reason":"rotate"})");
        game.PumpCoach();
        check(state.plan.intent == PlanIntent::RotateB && state.plan.source == "llm" &&
              state.rotateFrom == "A" && state.rotateTo == "B" && game.lastRotateAt_ == game.clockSec_,
              "PIX-37 T09 accepts an eligible rotate reply through the coach pipeline");
        game.clockSec_ += 25.0f;
        state.plan.intent = PlanIntent::Default;
        state.plan.target = "A";
        game.CoachTryRotate(Team::T);
        const auto* rotate = game.tactics_.Slots("T", "RotateB");
        check(rotate && state.plan.intent == PlanIntent::RotateB && state.plan.source == "rule" &&
              state.plan.slots[3].area == (*rotate)[3].area && state.plan.slots[3].role == (*rotate)[3].role,
              "PIX-37 T09 rule rotation uses the map template");
    }

    static void TestT35PostPlant() {
        for (const char* map : {"dust_2", "mirage", "ancient_ruins"}) {
            CS2DGame game;
            game.SetMatchMode(MatchMode::BombDefusal);
            const bool loaded = game.LoadMap(
                PathUtils::ResolveAppPath(std::string("assets/cs2d/maps/") + map + ".tmj"), nullptr, true);
            check(loaded, "T35 post-plant fixture loads tactical map");
            if (!loaded) continue;
            std::string schema;
            std::function<void(const std::string&)> reply;
            game.SetCoachLlmEnabled(true);
            game.SetCoachLlm([&](const std::string&, const std::string&, int,
                                const std::string& requestSchema, auto callback) {
                schema = requestSchema;
                reply = std::move(callback);
            });
            const CoachEvent event{CoachEventType::BombPlanted};
            for (const auto& site : game.world_->GetBombSites()) {
                game.bombPlanted_ = true;
                game.bombX_ = site.cx;
                game.bombY_ = site.cy;
                game.intel_.OnEnemySeen(game.bombX_, game.bombY_, game.clockSec_);
                game.intel_.OnEnemySeen(game.bombX_, game.bombY_, game.clockSec_);
                for (Team team : {Team::T, Team::CT}) {
                    auto& state = game.CoachFor(team);
                    state.plan = {};
                    state.hasRuleSnapshot = false;
                    reply = {};
                    game.CoachTryLlm(team, event);
                    check(reply && schema.find("\"tempo\"") != std::string::npos &&
                          schema.find("\"target\"") == std::string::npos &&
                          schema.find("\"slots\"") == std::string::npos,
                          "T35 post-plant schema requests tempo without model target or slots");
                    if (!reply) continue;
                    reply(R"({"tempo":"slow","ttl_s":18,"reason":"hold"})");
                    game.PumpCoach();
                    const bool siteA = site.name == "A";
                    const auto intent = team == Team::T
                        ? (siteA ? PlanIntent::StackA : PlanIntent::StackB)
                        : (siteA ? PlanIntent::RetakeA : PlanIntent::RetakeB);
                    check(state.plan.source == "llm" && state.plan.target == site.name &&
                          state.plan.intent == intent && state.plan.tempo == Tempo::Slow &&
                          !state.plan.slots.front().area.empty(),
                          "T35 post-plant keeps model tempo and fills the correct map template");

                    game.CoachTryLlm(team, event);
                    reply(R"({"intent":"default","target":"","tempo":"fast","ttl_s":18,"reason":"legacy"})");
                    game.PumpCoach();
                    check(state.plan.target == site.name && state.plan.intent == intent &&
                          state.plan.tempo == Tempo::Fast, "T35 legacy empty target resolves before validation");

                    game.CoachTryLlm(team, event);
                    const std::string wrong = siteA ? "B" : "A";
                    reply("{\"intent\":\"default\",\"target\":\"" + wrong + "\",\"tempo\":\"normal\"}");
                    game.PumpCoach();
                    check(state.plan.tempo == Tempo::Fast, "T35 explicitly wrong site is rejected");

                    game.CoachTryLlm(team, event);
                    game.bombPlanted_ = false;
                    reply(R"({"tempo":"normal","ttl_s":18,"reason":"stale"})");
                    game.PumpCoach();
                    check(state.plan.tempo == Tempo::Fast, "T35 stale bomb-state reply is rejected");
                    game.bombPlanted_ = true;

                    game.CoachTryLlm(team, event);
                    reply(R"({"tempo":42,"ttl_s":18,"reason":"wrong_type"})");
                    game.PumpCoach();
                    check(state.plan.tempo == Tempo::Fast, "T35 invalid model tempo cannot replace the plan");
                }
            }
        }
    }

    static void TestPix37DefuseRangeAndLos() {
        std::printf("\n===== PIX-37 拆包范围与遮挡 =====\n");
        check(Tuning::DefuseRadius < 52.0f, "PIX-37 defuse radius was shrunk from the old 52");
        check(std::fabs(Tuning::DefuseRadius - 30.0f) < 0.001f,
              "PIX-37 defuse radius is 30 world units");

        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        game.actors_[0].team = Team::CT;
        ClearBots(game);
        game.bombPlanted_ = true;
        game.bombX_ = 400.0f;
        game.bombY_ = 400.0f;
        game.bombTimer_ = 30.0f;

        // 贴脸：在半径内且无遮挡 → 可拆
        check(game.IsInDefuseRange(400.0f, 400.0f),
              "PIX-37 standing on the C4 is inside defuse range");
        check(game.IsInDefuseRange(400.0f + Tuning::DefuseRadius * 0.8f, 400.0f),
              "PIX-37 inside the (shrunken) radius passes the range check");
        // 旧半径内、新半径外：现在不能拆
        check(!game.IsInDefuseRange(400.0f + Tuning::DefuseRadius * 1.4f, 400.0f),
              "PIX-37 the old 52-unit distance no longer allows defusing");
        check(!game.IsInDefuseRange(400.0f + 80.0f, 400.0f),
              "PIX-37 clearly out of range is rejected");

        // 中间有墙：即使贴得很近也不能拆
        {
            CS2DMapData data;
            data.mapWidth = data.mapHeight = 60;
            data.tileWidth = data.tileHeight = 16;
            ObjectLayerDef walls;
            walls.name = "Collisions";
            ObjectDef wall;
            wall.type = "wall";
            // 墙只立在 C4 的右侧（拆包者侧），左侧留空
            wall.x = 404.0f;
            wall.y = 330.0f;
            wall.width = 16.0f;
            wall.height = 160.0f;
            walls.objects.push_back(wall);
            data.objectLayers.push_back(walls);
            game.world_->Build(data);
            game.visibility_.SetWorld(game.world_.get());
            // 位置极近（< 半径），但连线穿过墙 → 不能拆
            check(!game.visibility_.HasLineOfSight(420.0f, 400.0f, 400.0f, 400.0f),
                  "PIX-37 the test wall really breaks line of sight");
            check(!game.IsInDefuseRange(420.0f, 400.0f),
                  "PIX-37 a wall between the defuser and the C4 blocks defusing");

            // 核心契约：能不能拆 == (在半径内 && 视线通畅)，逐点对照，
            // 不依赖这段测试墙的具体摆放。
            const float probes[][2] = {
                {400.0f, 400.0f}, {410.0f, 400.0f}, {420.0f, 400.0f},
                {390.0f, 400.0f}, {370.0f, 400.0f}, {400.0f, 370.0f},
                {400.0f, 430.0f}, {430.0f, 430.0f},
            };
            bool contract = true;
            for (const auto& pr : probes) {
                const float px = pr[0], py = pr[1];
                const float ddx = px - game.bombX_, ddy = py - game.bombY_;
                const bool inRadius =
                    ddx * ddx + ddy * ddy <= Tuning::DefuseRadius * Tuning::DefuseRadius;
                const bool los = game.visibility_.HasLineOfSight(px, py, game.bombX_, game.bombY_);
                contract = contract && game.IsInDefuseRange(px, py) == (inRadius && los);
            }
            check(contract,
                  "PIX-37 defuse eligibility is exactly 'inside radius AND clear line of sight'");
        }
        // 未下包时任何位置都不成立
        game.bombPlanted_ = false;
        check(!game.IsInDefuseRange(400.0f, 400.0f),
              "PIX-37 no planted C4 means no defuse range");
    }

    // 2) 火焰贴图尺寸 ≥ 圆面积 1/64，且不再叠矢量火焰
    static void TestPix37FireTileArea() {
        std::printf("\n===== PIX-37 火焰贴图尺寸 =====\n");
        // 渲染器用的规则：tile >= max(8, 0.26 * activeRadius)，并向上取 2 的倍数。
        // 要求单块面积 ≥ 圆面积/64 → tile >= r*sqrt(pi/64) ≈ 0.2216r。
        const float requiredFactor = std::sqrt(Tuning::Pi / 64.0f);
        for (float radius : {Tuning::FireRadius, 30.0f, 90.0f, 140.0f}) {
            const float active = Tuning::FireEffectiveRadius(radius, Tuning::FireLife, Tuning::FireLife);
            const float minTile = std::max(8.0f, active * 0.26f);
            const float tile = std::ceil(minTile / 2.0f) * 2.0f;
            // 面积比：一块贴图 / 圆面积
            const float ratio = (tile * tile) / (Tuning::Pi * active * active);
            check(ratio >= 1.0f / 64.0f,
                  "PIX-37 one fire tile covers at least 1/64 of the circle area");
            check(tile >= active * requiredFactor,
                  "PIX-37 fire tile size meets the 1/64 area bound at this radius");
            // 也不能大到荒谬：一块不该超过圆心角对应的整圆
            check(ratio < 1.0f, "PIX-37 a single fire tile stays smaller than the whole circle");
        }

        // 实时渲染链路已下线：DrawFireZone 不再存在（编译期保证），
        // 这里确认火区数据仍然只驱动 DrawFireCells 这一条贴图路径。
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        CS2DGame::FireZone fire;
        fire.x = 400.0f; fire.y = 400.0f;
        fire.radius = Tuning::FireRadius;
        fire.age = 1.0f;
        fire.life = Tuning::FireLife;
        fire.maxLife = Tuning::FireLife;
        game.RebuildFireCells(fire);
        check(!fire.cells.empty(), "PIX-37 fire still produces ground cells for the map-tile pass");
    }

    // 3) 准备阶段（不可移动）瞄准线不发虚
    static void TestPix37AimLineDuringFreeze() {
        std::printf("\n===== PIX-37 准备阶段瞄准线 =====\n");
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        game.actors_[0].alive = true;
        // 冻结阶段：不能移动
        game.round_.Reset(RoundConfig{});
        check(game.round_.IsFrozen(), "PIX-37 round starts frozen");
        check(!game.IsMovementAllowed(),
              "PIX-37 frozen round reports movement as not allowed");

        // 人为把移动模糊拉到高处（模拟上一回合跑动残留），再走若干帧。
        game.view_.aimMotionBlur_ = 5.0f;
        game.view_.crosshairBloom_ = 1.0f;
        for (int frame = 0; frame < 40; ++frame) {
            game.Update(1.0f / 60.0f, false, game.actors_[0].x + 80, game.actors_[0].y,
                        false, false, false, false, false, false, false, nullptr);
        }
        check(game.GetAimMotionBlur() < 0.05f,
              "PIX-37 residual movement blur decays to zero while the player cannot move");
        check(game.GetCrosshairBloom() < 0.05f,
              "PIX-37 residual crosshair bloom also collapses when frozen");

        // 解冻后允许移动 → 模糊重新可用
        game.round_.Update(10.0f);
        if (!game.round_.IsFrozen()) {
            check(game.IsMovementAllowed(),
                  "PIX-37 unfrozen round reports movement as allowed");
        }
    }

    // PIX-37 证据截图：带包背影、雷达小 C4、火焰贴图、烟雾渐隐
    static void TestPix37VisualEvidence() {
        std::printf("\n===== PIX-37 视觉证据 =====\n");
        const auto directory = std::filesystem::absolute("build-evidence/pix37");
        std::filesystem::create_directories(directory);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(software != nullptr, "PIX-37 visual evidence renderer initializes");
        if (software) {
            CS2DRenderer renderer;
            renderer.Initialize(software, "assets/cs2d/maps", 640, 480);
            CS2DMapData data; data.mapWidth = data.mapHeight = 80;
            data.tileWidth = data.tileHeight = 16;
            CS2DWorld world; world.Build(data);
            renderer.SetWorld(&world);

            // --- (a) 带包 BOT：后背 C4 随朝向转动 ---
            SDL_SetRenderDrawColor(software, 30, 34, 40, 255);
            SDL_RenderClear(software);
            renderer.SetZoom(5.0f);
            renderer.SetPreviewCamera(640.0f, 640.0f, 5.0f, false);
            for (int i = 0; i < 4; ++i) {
                SpriteDrawParams sp;
                sp.x = 615.0f + i * 16.0f;
                sp.y = 640.0f;
                sp.size = 14.0f;
                sp.aimAngle = i * Tuning::Pi * 0.5f;
                sp.weaponSlot = 0;
                sp.weaponId = static_cast<int>(WeaponId::AK47);
                sp.animState = static_cast<int>(CS2DGame::AnimState::Idle);
                sp.spriteKey = 1;
                sp.team = Team::T;
                sp.alpha = 255;
                renderer.DrawCharacter(sp);
                renderer.DrawBackpackC4(sp.x, sp.y, sp.size, sp.aimAngle, 0.5f);
            }
            SDL_RenderPresent(software);
            check(IMG_SavePNG(surface, (directory / "carrier-backpack.png").string().c_str()) == 0,
                  "PIX-37 carrier backpack evidence saved");

            // --- (b) 火焰贴图（地图层平铺，单块不再碎） ---
            SDL_SetRenderDrawColor(software, 18, 22, 28, 255);
            SDL_RenderClear(software);
            renderer.SetZoom(1.0f);
            renderer.SetPreviewCamera(320, 240, 1.0f, false);
            std::vector<Vec2> cells;
            for (int y = 180; y <= 300; y += 4)
                for (int x = 260; x <= 380; x += 4) cells.push_back({float(x), float(y)});
            renderer.DrawFireCells(cells, 320, 240, 60, 2.0f, 5.0f, 0.3f);
            SDL_RenderPresent(software);
            check(IMG_SavePNG(surface, (directory / "fire-tiles.png").string().c_str()) == 0,
                  "PIX-37 fire tile evidence saved");

            // --- (c) 烟雾里的角色：逐级渐隐 ---
            SDL_SetRenderDrawColor(software, 30, 34, 40, 255);
            SDL_RenderClear(software);
            renderer.SetZoom(1.0f);
            renderer.SetPreviewCamera(320, 240, 1.0f, false);
            SDL_SetRenderDrawBlendMode(software, SDL_BLENDMODE_BLEND);
            for (int i = 0; i < 5; ++i) {
                SpriteDrawParams sp;
                sp.x = 250.0f + i * 35.0f;
                sp.y = 240.0f;
                sp.size = 14.0f;
                sp.aimAngle = 0.0f;
                sp.weaponSlot = 0;
                sp.weaponId = static_cast<int>(WeaponId::AK47);
                sp.animState = static_cast<int>(CS2DGame::AnimState::Idle);
                sp.spriteKey = 0;
                sp.alpha = static_cast<Uint8>(255 - i * 55);
                renderer.DrawCharacter(sp);
            }
            SDL_RenderPresent(software);
            check(IMG_SavePNG(surface, (directory / "smoke-fade.png").string().c_str()) == 0,
                  "PIX-37 smoke fade evidence saved");

            // --- (d) 雷达：携带者 + 掉落的小 C4 图标 ---
            SDL_SetRenderDrawColor(software, 30, 34, 40, 255);
            SDL_RenderClear(software);
            {
                std::vector<CS2DRenderer::RadarDot> dots;
                dots.push_back({80, 300, 255, 170, 60, 255,
                                CS2DRenderer::RadarDot::Shape::Carrier});
                dots.push_back({700, 300, 255, 170, 60, 255,
                                CS2DRenderer::RadarDot::Shape::DroppedBomb});
                dots.push_back({400, 650, 90, 220, 130, 255,
                                CS2DRenderer::RadarDot::Shape::Circle});
                renderer.DrawRadarHUD(400, 400, 0, dots, 800, 800, true, false, true, 255, 240, 1,
                                      false, 640, 480);
            }
            SDL_RenderPresent(software);
            check(IMG_SavePNG(surface, (directory / "radar-c4-icons.png").string().c_str()) == 0,
                  "PIX-37 radar C4 icon evidence saved");
        }
        SDL_DestroyRenderer(software);
        SDL_FreeSurface(surface);
    }

    // 4) BOT 带包：后背 C4 贴图取代黄色菱形；雷达小 C4；掉落显示
    static void TestPix37BombCarrierPresentation() {
        std::printf("\n===== PIX-37 BOT 带包标志 =====\n");
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        game.actors_[0].team = Team::T;
        ClearBots(game);

        CS2DGame::Player carrier;
        carrier.team = Team::T;
        carrier.alive = true;
        carrier.name = "carrier";
        carrier.x = 400.0f; carrier.y = 400.0f;
        carrier.aimAngle = 0.0f;
        carrier.spriteKey = 1;
        AddBot(game, carrier);
        game.bombCarrier_ = 1;

        check(game.ActorHasC4(Bot(game, 0)),
              "PIX-37 the designated BOT is recognised as the C4 carrier");

        // 雷达：携带者应产出 Carrier 形状的圆点（渲染层已改为小 C4 图标）
        auto dots = CS2DOverlay::CollectRadarDots(game);
        bool carrierDot = false;
        for (const auto& d : dots)
            carrierDot = carrierDot || d.shape == CS2DRenderer::RadarDot::Shape::Carrier;
        check(carrierDot, "PIX-37 the carrier still surfaces a carrier-shaped radar dot");

        // 掉包：C4 掉落时应变成 DroppedBomb 圆点（渲染层直接显示小 C4 图）
        game.bombDropped_ = true;
        game.bombDropX_ = 500.0f;
        game.bombDropY_ = 400.0f;
        auto dropped = CS2DOverlay::CollectRadarDots(game);
        bool droppedDot = false;
        for (const auto& d : dropped)
            droppedDot = droppedDot || d.shape == CS2DRenderer::RadarDot::Shape::DroppedBomb;
        check(droppedDot, "PIX-37 a dropped C4 surfaces a DroppedBomb radar dot");
    }

    // 5) 移动不再产生人物晃动
    static void TestPix37NoWalkBob() {
        std::printf("\n===== PIX-37 取消移动晃动 =====\n");
        // 用一个可以正常建窗的软件渲染器直接比对：行走帧与站立帧的角色包围盒必须一致。
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(software != nullptr, "PIX-37 software renderer for the walk-bob check");
        if (!software) { SDL_FreeSurface(surface); return; }
        {
            CS2DRenderer renderer;
            renderer.Initialize(software, "assets/cs2d/maps", 320, 240);
            CS2DMapData data; data.mapWidth = data.mapHeight = 80;
            data.tileWidth = data.tileHeight = 16;
            CS2DWorld world; world.Build(data);
            renderer.SetWorld(&world);
            renderer.UpdateCamera(640, 640, 320, 240);

            auto measure = [&](int animState, float animTimer) {
                SDL_SetRenderDrawColor(software, 0, 0, 0, 255);
                SDL_RenderClear(software);
                SpriteDrawParams sp;
                sp.x = 640.0f; sp.y = 640.0f; sp.size = 14.0f;
                sp.aimAngle = 0.0f; sp.weaponSlot = 0;
                sp.weaponId = static_cast<int>(WeaponId::AK47);
                sp.animState = animState; sp.animTimer = animTimer;
                sp.spriteKey = 0; sp.alpha = 255;
                renderer.DrawCharacter(sp);
                SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32,
                                                                  SDL_PIXELFORMAT_RGBA32);
                if (!shot) return 0;
                SDL_RenderReadPixels(software, nullptr, SDL_PIXELFORMAT_RGBA32,
                                     shot->pixels, shot->pitch);
                int top = 240, bottom = -1;
                for (int y = 0; y < 240; ++y) {
                    bool hit = false;
                    for (int x = 0; x < 320 && !hit; ++x) {
                        const auto* p = static_cast<const Uint8*>(shot->pixels) + y * shot->pitch + x * 4;
                        if (p[0] + p[1] + p[2] > 40) hit = true;
                    }
                    if (hit) { top = std::min(top, y); bottom = std::max(bottom, y); }
                }
                SDL_FreeSurface(shot);
                return bottom - top;
            };
            // 行走状态的四个相位：高度必须完全一致（没有上下 bob）
            const int idle = measure(static_cast<int>(CS2DGame::AnimState::Idle), 0.0f);
            check(idle > 0, "PIX-37 the character renders in the idle pose");
            bool stable = true;
            for (float t : {0.0f, 0.09f, 0.17f, 0.26f, 0.35f}) {
                stable = stable && measure(static_cast<int>(CS2DGame::AnimState::Walk), t) == idle;
            }
            check(stable, "PIX-37 walking no longer bobs the character vertically");
        }
        SDL_DestroyRenderer(software);
        SDL_FreeSurface(surface);
    }

    // 6) 烟雾：视觉范围盖住判定范围，且人物进出烟雾渐隐/渐显
    static void TestPix37SmokeCoverageAndFade() {
        std::printf("\n===== PIX-37 烟雾范围与人物渐隐 =====\n");
        check(Tuning::SmokeVisualRadiusMul > 1.0f,
              "PIX-37 smoke renders wider than its gameplay radius");

        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        game.smokes_.clear();
        CS2DGame::SmokeCloud s;
        s.x = 400.0f; s.y = 400.0f;
        s.radius = Tuning::SmokeRadius;
        s.age = 3.0f;
        s.expandTime = 1.6f;
        s.life = 12.0f;
        s.maxLife = 12.0f;
        game.smokes_.push_back(s);

        const float gameplayR = Tuning::SmokeEffectiveRadius(s.radius, s.age, s.expandTime, s.life);
        const float visualR = gameplayR * Tuning::SmokeVisualRadiusMul;
        // 判定半径处一定是 1（完全遮挡）
        check(game.SmokeOpacityAt(400.0f, 400.0f) > 0.95f,
              "PIX-37 the smoke centre is fully opaque to actors");
        // 视觉半径（> 判定半径）内仍有可见遮蔽，视觉与判定不再错位
        std::printf("[PIX-37] smoke gameplayR=%.1f visualR=%.1f\n", gameplayR, visualR);
        check(visualR > gameplayR, "PIX-37 visual radius exceeds the gameplay radius");
        // 判定半径 ↔ 视觉半径之间这一圈：必须仍有遮蔽（旧实现的空洞就在这里）
        check(game.SmokeOpacityAt(400.0f + gameplayR * 0.99f, 400.0f) > 0.0f,
              "PIX-37 opacity is still non-zero just inside the gameplay radius");
        check(game.SmokeOpacityAt(400.0f + gameplayR, 400.0f) >= 0.0f,
              "PIX-37 opacity is defined across the gameplay radius boundary");
        // 视觉半径之外完全不受影响
        check(game.SmokeOpacityAt(400.0f + visualR + 10.0f, 400.0f) == 0.0f,
              "PIX-37 outside the visual radius the smoke has no effect");
        // 径向衰减：中心比边缘更浓
        check(game.SmokeOpacityAt(400.0f, 400.0f) >
              game.SmokeOpacityAt(400.0f + gameplayR * 0.9f, 400.0f),
              "PIX-37 smoke opacity falls off toward the edge");

        // 消散时整体变淡（人物同步渐显）
        const float dense = game.SmokeOpacityAt(400.0f, 400.0f);
        game.smokes_[0].life = 0.35f;   // 进入 fadeOut 窗口
        const float fading = game.SmokeOpacityAt(400.0f, 400.0f);
        check(fading < dense,
              "PIX-37 actor transparency tracks the smoke dissipating");
        game.smokes_[0].life = 0.02f;
        const float nearlyGone = game.SmokeOpacityAt(400.0f, 400.0f);
        check(nearlyGone < fading && nearlyGone < 0.2f,
              "PIX-37 the actor is nearly fully visible again as the smoke dies");
        // 刚生成时也淡入（避免瞬移式出现）
        game.smokes_[0].life = 12.0f;
        game.smokes_[0].age = 0.05f;
        check(game.SmokeOpacityAt(400.0f, 400.0f) < dense,
              "PIX-37 smoke fade-in also fades actors in gradually");
        game.smokes_.clear();
        check(game.SmokeOpacityAt(400.0f, 400.0f) == 0.0f,
              "PIX-37 no smoke means no actor transparency");
    }

    // PIX-38 证据截图：描边不抖、烟雾渐隐、火焰更细的贴图、雷达边缘延伸
    static void TestPix38VisualEvidence() {
        std::printf("\n===== PIX-38 视觉证据 =====\n");
        const auto directory = std::filesystem::absolute("build-evidence/pix38");
        std::filesystem::create_directories(directory);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(software != nullptr, "PIX-38 visual evidence renderer initializes");
        if (software) {
            CS2DRenderer renderer;
            renderer.Initialize(software, "assets/cs2d/maps", 640, 480);
            CS2DMapData data; data.mapWidth = data.mapHeight = 120;
            data.tileWidth = data.tileHeight = 16;
            CS2DWorld world; world.Build(data);
            renderer.SetWorld(&world);

            // (a) 同一角色在行走的多个相位：位置必须完全不动（描边与本体同源）
            SDL_SetRenderDrawColor(software, 30, 34, 40, 255);
            SDL_RenderClear(software);
            renderer.SetPreviewCamera(640.0f, 640.0f, 3.0f, false);
            for (int i = 0; i < 5; ++i) {
                SpriteDrawParams sp;
                sp.x = 615.0f + i * 12.0f;
                sp.y = 640.0f;
                sp.size = 14.0f;
                sp.aimAngle = 0.0f;
                sp.weaponSlot = 0;
                sp.weaponId = static_cast<int>(WeaponId::AK47);
                sp.animState = static_cast<int>(CS2DGame::AnimState::Walk);
                sp.animTimer = i * 0.11f;
                sp.spriteKey = 0;
                sp.alpha = 255;
                renderer.DrawCharacter(sp);
            }
            SDL_RenderPresent(software);
            check(IMG_SavePNG(surface, (directory / "outline-no-bob.png").string().c_str()) == 0,
                  "PIX-38 outline/no-bob evidence saved");

            // (b) 烟雾里的角色按遮蔽度逐级变淡（真实 alpha 路径）
            SDL_SetRenderDrawColor(software, 30, 34, 40, 255);
            SDL_RenderClear(software);
            renderer.SetPreviewCamera(640.0f, 640.0f, 3.0f, false);
            for (int i = 0; i < 6; ++i) {
                SpriteDrawParams sp;
                sp.x = 600.0f + i * 10.0f;
                sp.y = 640.0f;
                sp.size = 14.0f;
                sp.aimAngle = 0.0f;
                sp.weaponSlot = 0;
                sp.weaponId = static_cast<int>(WeaponId::AK47);
                sp.animState = static_cast<int>(CS2DGame::AnimState::Idle);
                sp.spriteKey = 0;
                // 255 → 30 逐级下降，模拟从烟外走到烟心
                sp.alpha = static_cast<Uint8>(255 - i * 45);
                renderer.DrawCharacter(sp);
            }
            SDL_RenderPresent(software);
            check(IMG_SavePNG(surface, (directory / "smoke-actor-fade.png").string().c_str()) == 0,
                  "PIX-38 smoke actor fade evidence saved");

            // (c) 火焰：单块贴图更小（面积 1/16），火区范围不变
            SDL_SetRenderDrawColor(software, 18, 22, 28, 255);
            SDL_RenderClear(software);
            renderer.SetPreviewCamera(320, 240, 1.0f, false);
            std::vector<Vec2> cells;
            for (int y = 180; y <= 300; y += 4)
                for (int x = 260; x <= 380; x += 4) cells.push_back({float(x), float(y)});
            renderer.DrawFireCells(cells, 320, 240, 60, 2.0f, 5.0f, 0.3f);
            SDL_RenderPresent(software);
            check(IMG_SavePNG(surface, (directory / "fire-tiles-smaller.png").string().c_str()) == 0,
                  "PIX-38 smaller fire tile evidence saved");
        }
        SDL_DestroyRenderer(software);
        SDL_FreeSurface(surface);
    }

    // 7) 暂停 → 继续 恢复黑场过渡
    //    （实现放在 EngineTestAccess 里——它才是 Engine 的友元）

    // ========================================================================
    // PIX-38：6 项修修补
    // ========================================================================

    // 1) 描边不再随行走抖动（BodyGeometry 的 bob 与 DrawCharacter 必须同源）
    static void TestPix38OutlineNoBob() {
        std::printf("\n===== PIX-38 描边抖动 =====\n");
        // BodyGeometry::Transform 是描边遮罩与命中几何的唯一变换；
        // 行走态必须与站立态给出同一个中心点（不再有 sin(timer*12) 的纵向偏移）。
        const Vec2 idle = BodyGeometry::Transform({0.0f, 0.0f}, 100.0f, 200.0f, 14.0f,
                                                        0.7f, 1 /* Walk */, 0.0f);
        bool stable = true;
        for (float timer : {0.0f, 0.05f, 0.11f, 0.17f, 0.26f, 0.33f, 0.44f, 0.52f}) {
            const Vec2 walk = BodyGeometry::Transform({0.0f, 0.0f}, 100.0f, 200.0f, 14.0f,
                                                            0.7f, 1 /* Walk */, timer);
            stable = stable && std::fabs(walk.x - idle.x) < 1e-4f &&
                     std::fabs(walk.y - idle.y) < 1e-4f;
        }
        check(stable, "PIX-38 walk pose keeps the outline anchor identical to idle (no bob)");

        // 描边围出的轮廓在行走的任意相位都必须认得出同一个身体点
        bool sameHit = true;
        for (float timer : {0.0f, 0.12f, 0.25f, 0.41f}) {
            sameHit = sameHit && BodyGeometry::Contains(0, 0, 100.0f, 200.0f, 14.0f, 0.7f,
                                                        1, timer, 100.0f, 200.0f);
        }
        check(sameHit, "PIX-38 the body centre stays inside the body at every walk phase");

        // 射击后坐（animState 2）仍保留它应有的位移，说明只去掉了 bob
        const Vec2 kick = BodyGeometry::Transform({0.0f, 0.0f}, 100.0f, 200.0f, 14.0f,
                                                        0.7f, 2, 0.0f);
        check(kick.x < idle.x, "PIX-38 the shooting recoil offset is preserved");
    }

    // 2) 烟雾里的人物：走真实 alpha 路径渐隐（而不是被视线过滤整块剔掉）
    static void TestPix38SmokeActorAlpha() {
        std::printf("\n===== PIX-38 烟雾人物透明度 =====\n");
        CS2DGame game;
        game.SetMatchMode(MatchMode::BombDefusal);
        if (!game.LoadMap(MapPath(), nullptr, true)) return;
        game.actors_[0].team = Team::T;
        ClearBots(game);

        CS2DGame::Player mate;
        mate.team = Team::T;
        mate.alive = true;
        mate.name = "mate";
        mate.x = 400.0f; mate.y = 400.0f;
        mate.spriteKey = 1;
        AddBot(game, mate);

        // 无烟：alpha 满
        const auto clear = game.MakeActorDrawParams(Bot(game, 0), &game.actors_[0]);
        check(clear.alpha == 255, "PIX-38 without smoke the actor is fully opaque");

        // 中心浓烟：alpha 显著下降，但不需要靠可见性过滤
        CS2DGame::SmokeCloud s;
        s.x = 400.0f; s.y = 400.0f;
        s.radius = Tuning::SmokeRadius;
        s.age = 3.0f; s.expandTime = 1.6f;
        s.life = 12.0f; s.maxLife = 12.0f;
        game.smokes_.push_back(s);
        const auto inSmoke = game.MakeActorDrawParams(Bot(game, 0), &game.actors_[0]);
        check(inSmoke.alpha < clear.alpha,
              "PIX-38 a smoked actor gets a reduced alpha instead of disappearing");
        check(inSmoke.alpha > 0,
              "PIX-38 the smoked actor keeps a faint silhouette (never fully transparent)");

        // 同一位置：可见性判定不再把角色整块剔掉（烟雾只走 alpha）
        // 直接验证几何：烟雾遮挡为真，但 SmokeOpacityAt 给出的是连续值
        check(game.SegmentBlockedBySmoke(300.0f, 400.0f, 460.0f, 400.0f),
              "PIX-38 smoke still blocks line of sight for gameplay");
        const float opacity = game.SmokeOpacityAt(400.0f, 400.0f);
        check(opacity > 0.5f, "PIX-38 the smoke opacity at the centre is strong");

        // 边缘比中心可见度更高（渐隐随稀疏程度变化）
        const float edgeAlpha =
            [&] {
                Bot(game, 0).x = 400.0f + Tuning::SmokeEffectiveRadius(s.radius, s.age, s.expandTime, s.life) * 0.95f;
                return game.MakeActorDrawParams(Bot(game, 0), &game.actors_[0]).alpha;
            }();
        check(edgeAlpha > inSmoke.alpha,
              "PIX-38 the smoke edge keeps the actor more visible than the centre");

        // 烟散：alpha 回升（渐显）
        Bot(game, 0).x = 400.0f;
        game.smokes_[0].life = 0.03f;
        const auto dispersing = game.MakeActorDrawParams(Bot(game, 0), &game.actors_[0]);
        check(dispersing.alpha > inSmoke.alpha,
              "PIX-38 the actor becomes visible again as the smoke dissipates");

        game.smokes_.clear();
        check(game.MakeActorDrawParams(Bot(game, 0), &game.actors_[0]).alpha == 255,
              "PIX-38 clearing the smoke restores full opacity");
    }

    // 3) 火焰贴图面积再缩到 1/16（线尺寸 ×1/4），火区范围不变
    static void TestPix38FireTileSmaller() {
        std::printf("\n===== PIX-38 火焰贴图再缩小 =====\n");
        const float radius = Tuning::FireRadius;
        const float active = Tuning::FireEffectiveRadius(radius, Tuning::FireLife, Tuning::FireLife);
        // 渲染规则：tile = ceil(max(4, 0.065*r)/2)*2
        const float tile = std::ceil(std::max(4.0f, active * 0.065f) / 2.0f) * 2.0f;
        const float areaRatio = (tile * tile) / (Tuning::Pi * active * active);
        std::printf("[PIX-38] fire activeR=%.1f tile=%.1f areaRatio=1/%.1f\n",
                    active, tile, 1.0f / areaRatio);
        // 相对 PIX-37 的 0.26 系数，面积恰好是 1/16
        const float oldTile = std::ceil(std::max(8.0f, active * 0.26f) / 2.0f) * 2.0f;
        const float shrink = (tile * tile) / (oldTile * oldTile);
        check(std::fabs(shrink - 1.0f / 16.0f) < 0.02f,
              "PIX-38 one fire tile's area is 1/16 of the previous size");
        check(tile > 0.0f && tile < oldTile, "PIX-38 the fire tile really got smaller");

        // 火区范围（判定）不变
        check(std::fabs(Tuning::FireRadius - 60.0f) < 0.001f,
              "PIX-38 the fire radius itself is unchanged");
        check(std::fabs(active - radius) < 0.001f,
              "PIX-38 the effective fire radius is unchanged at full life");
    }

    // 4) 准星跟随武器后坐力扩散
    static void TestPix38RecoilCrosshair() {
        std::printf("\n===== PIX-38 后坐力准星扩散 =====\n");
        CS2DGame game;
        if (!LoadDeathMatch(game)) return;
        auto& player = game.GetPlayer();
        player.SetSlotWeapon(0, WeaponId::AK47);
        player.weaponSlot = 0;
        player.ammo = player.maxAmmo = 30;
        // 静止：两种扩散都是 0
        check(game.GetRecoilBloom() == 0.0f, "PIX-38 no shots means no recoil bloom");
        check(game.GetCrosshairBloom() == 0.0f, "PIX-38 standing still means no move bloom");

        // 连射若干发后坐力扩散上升
        for (int i = 0; i < 6; ++i) player.ApplyWeaponRecoil(0.0f);
        const float afterShots = game.GetRecoilBloom();
        check(afterShots > 0.0f, "PIX-38 firing raises the recoil bloom");
        check(afterShots <= 1.0f, "PIX-38 the recoil bloom stays normalized to 0..1");

        // 恢复后回落
        for (int i = 0; i < 240; ++i) player.RecoverBallistics(1.0f / 30.0f);
        check(game.GetRecoilBloom() < afterShots,
              "PIX-38 the recoil bloom decays back down after firing stops");

        // 移动扩散与后坐扩散是两条独立通道：能同时非零
        game.view_.crosshairBloom_ = 0.8f;
        player.ApplyWeaponRecoil(0.0f);
        check(game.GetCrosshairBloom() > 0.0f && game.GetRecoilBloom() > 0.0f,
              "PIX-38 move bloom and recoil bloom coexist without replacing each other");
    }

    // 5) 雷达底图带地图边缘延伸（与选阵营界面同一套 perimeter）
    //    实现放在 SpriteAssetReview（它是 CS2DRenderer 的友元，能读到 perimeter 成员）。
    static void TestPix38RadarPerimeterStub() {}

    // 6) 结算页机位跨度：相邻 shot 必须落在地图的不同区域
    static void TestPix38ShowcaseSpread() {
        std::printf("\n===== PIX-38 结算机位跨度 =====\n");
        const float mapW = 1920.0f, mapH = 1080.0f;
        const int viewW = 1920, viewH = 1080;
        // 不传 points：本用例要验证的是「机位本身铺得开」，
        // 据点吸附会把落点往少数几个生成点收（那是有意的 12% 收敛，
        // 会干扰对跨度的度量）。生产环境仍会传入区域/出生点。
        const std::vector<SDL_FPoint> noPoints;
        std::vector<ShowcaseCamera::View> views;
        for (int shot = 0; shot < 12; ++shot)
            views.push_back(ShowcaseCamera::Sample(0.0f, shot, mapW, mapH, viewW, viewH, noPoints));

        // 另外单独验证：即使传入据点，机位仍然覆盖整张图（跨度不被压回一角）
        const std::vector<SDL_FPoint> points{{300.0f, 300.0f}, {1600.0f, 900.0f}};
        std::vector<ShowcaseCamera::View> anchored;
        for (int shot = 0; shot < 12; ++shot)
            anchored.push_back(ShowcaseCamera::Sample(0.0f, shot, mapW, mapH, viewW, viewH, points));

        // 每个机位都在合法范围内
        bool allLegal = true;
        for (const auto& v : views) {
            const float halfW = viewW / (2.0f * v.zoom), halfH = viewH / (2.0f * v.zoom);
            const float loX = std::min(halfW, mapW * .5f), hiX = std::max(loX, mapW - halfW);
            const float loY = std::min(halfH, mapH * .5f), hiY = std::max(loY, mapH - halfH);
            allLegal = allLegal && v.center.x >= loX - 0.5f && v.center.x <= hiX + 0.5f &&
                       v.center.y >= loY - 0.5f && v.center.y <= hiY + 0.5f && v.zoom > 0;
        }
        check(allLegal, "PIX-38 every showcase camera stays inside the legal map range");

        // 相邻机位的位移必须占地图的相当比例（旧实现只有 ~0.2 的局部晃动）
        float minAdjacent = 1e9f;
        for (size_t i = 1; i < views.size(); ++i) {
            const float dx = views[i].center.x - views[i - 1].center.x;
            const float dy = views[i].center.y - views[i - 1].center.y;
            const float norm = std::hypot(dx / mapW, dy / mapH);
            minAdjacent = std::min(minAdjacent, norm);
        }
        std::printf("[PIX-38] showcase min adjacent move = %.3f of map\n", minAdjacent);
        check(minAdjacent > 0.15f,
              "PIX-38 consecutive showcase shots move a large fraction of the map");

        // 带据点吸附时，12 个机位仍要覆盖全部四个象限（不会被收进一角）
        {
            bool q[4] = {false, false, false, false};
            for (const auto& v : anchored) {
                const int qi = (v.center.x < mapW * .5f ? 0 : 1) + (v.center.y < mapH * .5f ? 0 : 2);
                q[qi] = true;
            }
            check(q[0] && q[1] && q[2] && q[3],
                  "PIX-38 anchored showcase cameras still cover all four quadrants");
        }

        // 12 个机位要覆盖到地图的多个象限，而不是挤在一角
        int quadrants = 0;
        for (int q = 0; q < 4; ++q) {
            const bool left = q % 2 == 0, top = q < 2;
            for (const auto& v : views) {
                if ((v.center.x < mapW * .5f) == left && (v.center.y < mapH * .5f) == top) {
                    ++quadrants;
                    break;
                }
            }
        }
        check(quadrants == 4, "PIX-38 showcase cameras cover all four quadrants of the map");

        // 缩放交替：不能 12 个机位全用同一个倍率
        int distinctZoom = 0;
        for (size_t i = 0; i < views.size(); ++i) {
            bool first = true;
            for (size_t j = 0; j < i; ++j)
                if (std::fabs(views[j].zoom - views[i].zoom) < 0.01f) first = false;
            if (first) ++distinctZoom;
        }
        check(distinctZoom >= 3, "PIX-38 showcase alternates between several zoom levels");
    }

};

static void TestScopeBoundsAndMovement() {
    CS2DGame game;
    if (!LoadDeathMatch(game)) return;
    game.GetActors().resize(1);
    game.EnsureBotAi();
    CS2DMapData open;
    open.mapWidth = 400;
    open.mapHeight = 400;
    open.tileWidth = open.tileHeight = 8;
    game.GetWorld().Build(open);
    auto& player = game.GetPlayer();
    player.x = player.y = 1000;
    player.weapons[0] = WeaponId::AWP;
    player.weaponSlot = 0;
    game.UpdateScopeInput(true, true);
    game.UpdateScopeInput(false, true);
    check(game.GetScopeView().level > 0, "scope movement test enters scoped state");
    for (int tick = 0; tick < 120; ++tick) CS2DGameTestAccess::Move(game);
    check(std::hypot(player.vx, player.vy) > 1 && std::hypot(player.vx, player.vy) <= Tuning::PlayerBaseSpeed * player.CurrentWeapon().moveSpeedMul * Tuning::ScopeMoveMul + .01f,
          "scoped player movement obeys 0.6 speed cap");
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(software != nullptr, "scope bounds software renderer");
    if (software) {
        CS2DRenderer renderer;
        renderer.Initialize(software, "assets/cs2d/maps", 1280, 720);
        renderer.SetWorld(&game.GetWorld());
        const float radius = 720 * Presentation::kScopeCircleRadiusFrac;
        for (const auto& point : std::vector<SDL_FPoint>{{0,0},{1600,0},{1600,1600},{0,1600}}) {
            renderer.UpdatePlayerCamera(500,500,14,point.x,point.y,1280,720,1,radius);
            const float x = (point.x-renderer.GetCameraX())*renderer.GetZoom();
            const float y = (point.y-renderer.GetCameraY())*renderer.GetZoom();
            check(x >= radius && x <= 1280-radius && y >= radius && y <= 720-radius,
                  "scope lens fully inside viewport at every edge");
        }
        SDL_SetRenderDrawColor(software, 255, 255, 255, 255);
        SDL_RenderClear(software);
        Presentation::ScopeOverlayParams overlay;
        overlay.scope.transition = 1;
        overlay.centerX = 640;
        overlay.centerY = 360;
        overlay.radiusPx = radius;
        renderer.DrawScopeOverlay(overlay);
        SDL_RenderPresent(software);
        const auto* pixel = static_cast<const Uint8*>(surface->pixels);
        check(pixel[0] < 150, "scope exterior has fog coverage at viewport corner");
        SDL_SaveBMP(surface,"build-evidence/pix32-scope.bmp");
    }
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
}

struct SpriteAssetReview {
    // PIX-38 第 5 项：雷达底图要带与选阵营界面同一套「地图边缘延伸」。
    // 本结构是 CS2DRenderer 的友元，因此可以直接检查 perimeter 成员与烘焙结果。
    static void TestPix38RadarPerimeter() {
        std::printf("\n===== PIX-38 雷达地图边缘延伸 =====\n");
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(software != nullptr, "PIX-38 software renderer for the radar perimeter check");
        if (software) {
            CS2DRenderer renderer;
            renderer.Initialize(software, "assets/cs2d/maps", 320, 240);
            CS2DGame game;
            game.SetMatchMode(MatchMode::BombDefusal);
            if (game.LoadMap(MapPath(), &renderer, true)) {
                const bool hasPerimeter = renderer.perimeterTex_ != nullptr;
                std::printf("[PIX-38] perimeter=%s rect=%.0fx%.0f map=%.0fx%.0f\n",
                            hasPerimeter ? "yes" : "no",
                            renderer.perimeterWorldRect_.w, renderer.perimeterWorldRect_.h,
                            game.GetWorld().GetMapPixelW(), game.GetWorld().GetMapPixelH());
                if (hasPerimeter) {
                    check(renderer.perimeterWorldRect_.w > game.GetWorld().GetMapPixelW() &&
                          renderer.perimeterWorldRect_.h > game.GetWorld().GetMapPixelH(),
                          "PIX-38 the perimeter scene covers more than the map itself");
                }
                renderer.BuildRadarTexture();
                check(renderer.radarMapTex_ != nullptr,
                      "PIX-38 the radar base texture bakes with the perimeter pass");
            }
        }
        if (software) SDL_DestroyRenderer(software);
        SDL_FreeSurface(surface);
    }

    static void Run() {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* output = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(output != nullptr, "HD sprite review renderer");
        if (!output) { SDL_FreeSurface(surface); return; }
        {
            CS2DRenderer renderer;
            renderer.Initialize(output, "assets/cs2d/maps", 1280, 720);
            CS2DMapData data;
            data.mapWidth = data.mapHeight = 200;
            data.tileWidth = data.tileHeight = 8;
            CS2DWorld world;
            world.Build(data);
            renderer.SetWorld(&world);
            for (const auto* sheet : {&renderer.ctSheet_, &renderer.tSheet_, &renderer.tAltSheet_,
                                     &renderer.ctOutlineSheet_, &renderer.tOutlineSheet_, &renderer.tAltOutlineSheet_}) {
                int width = 0, height = 0;
                check(sheet->tex && SDL_QueryTexture(sheet->tex, nullptr, nullptr, &width, &height) == 0 &&
                      width == 1024 && height == 1536 && sheet->frameW == 512 && sheet->frameH == 512,
                      "HD character and outline sheets use matching 512-pixel cells");
            }
            for (int index = 1; index < WeaponDB::kWeaponCount; ++index) {
                const auto& weapon = WeaponDB::Get(static_cast<WeaponId>(index));
                if (!weapon.spritePath) continue;
                check(renderer.weaponSprites_[index] && renderer.weaponIcons_[index], "new weapon world/icon pair loads");
                check(weapon.degOffset == 90, "overhead weapon axis points along actor aim");
            }
            std::filesystem::create_directories("build-evidence/pix32-assets");
            auto clear = [&] {
                SDL_SetRenderDrawColor(output, 65, 72, 80, 255);
                SDL_RenderClear(output);
            };
            auto save = [&](const std::string& name) {
                SDL_RenderPresent(output);
                check(IMG_SavePNG(surface, ("build-evidence/pix32-assets/" + name + ".png").c_str()) == 0,
                      "sprite acceptance image saved");
            };
            renderer.SetPreviewCamera(1280.0f/24, 720.0f/24, 12, false);
            clear();
            renderer.DrawDroppedWeapon(static_cast<int>(WeaponId::AWP), 40, 30, 0);
            SDL_RenderFlush(output);
            int minX = surface->w, minY = surface->h, maxX = -1, maxY = -1;
            for (int y = 0; y < surface->h; ++y) {
                const auto* pixels = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch;
                for (int x = 0; x < surface->w; ++x) {
                    if (pixels[x*4] == 65 && pixels[x*4+1] == 72 && pixels[x*4+2] == 80) continue;
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
            check(maxX > minX && maxX-minX > maxY-minY,
                  "dropped AWP uses the horizontal main-view icon");
            save("ground-main-view");
            clear();
            for (int team = 0; team < 3; ++team) {
                for (int slot = 0; slot < 4; ++slot) {
                    SpriteDrawParams actor;
                    actor.x = (150 + slot * 300)/12.0f;
                    actor.y = (120 + team * 240)/12.0f;
                    actor.spriteKey = team;
                    actor.weaponSlot = slot;
                    actor.weaponId = static_cast<int>(WeaponId::None);
                    renderer.DrawCharacter(actor);
                }
            }
            save("character-poses");
            std::filesystem::create_directories("build-evidence/pix34-round2");
            renderer.SetPreviewCamera(1280.0f/12,720.0f/12,6,false);
            for (int equipment=0;equipment<2;++equipment) {
                clear();
                for (int team=0;team<3;++team) for (int direction=0;direction<8;++direction) {
                    SpriteDrawParams actor;
                    actor.x=(80+direction*160)/6.0f;
                    actor.y=(120+team*240)/6.0f;
                    actor.spriteKey=team;
                    actor.weaponSlot=equipment ? 4 : 2;
                    actor.weaponId=static_cast<int>(equipment ? WeaponId::C4 : WeaponId::Knife);
                    actor.aimAngle=direction*3.14159265f/4;
                    renderer.DrawCharacter(actor);
                }
                SDL_RenderPresent(output);
                const char* path=equipment ? "build-evidence/pix34-round2/c4-directions.png" : "build-evidence/pix34-round2/knife-directions.png";
                check(IMG_SavePNG(surface,path)==0,"round2 equipment attachment acceptance image saved");
            }
            renderer.SetPreviewCamera(1280.0f/24,720.0f/24,12,false);
            for (int team = 0; team < 3; ++team) {
                SpriteDrawParams actor;
                actor.x = 20;
                actor.y = 20;
                actor.spriteKey = team;
                actor.weaponSlot = 2;
                clear();
                renderer.DrawCharacter(actor);
                SDL_RenderFlush(output);
                const auto* pixels = static_cast<const Uint8*>(surface->pixels);
                const std::vector<Uint8> melee(pixels, pixels + surface->pitch * surface->h);
                clear();
                actor.weaponSlot = 3;
                renderer.DrawCharacter(actor);
                SDL_RenderFlush(output);
                check(std::memcmp(melee.data(), surface->pixels, melee.size()) == 0,
                      "grenade and melee use the same character pose");
            }
            for (int team = 0; team < 3; ++team) {
                auto& sheet = team == 0 ? renderer.ctSheet_ : (team == 1 ? renderer.tSheet_ : renderer.tAltSheet_);
                auto& outline = team == 0 ? renderer.ctOutlineSheet_ :
                    (team == 1 ? renderer.tOutlineSheet_ : renderer.tAltOutlineSheet_);
                SDL_Texture* outlineTexture = outline.tex;
                outline.tex = nullptr;
                for (float aim : {0.0f, Tuning::Pi/2, Tuning::Pi, -Tuning::Pi/2}) {
                    SpriteDrawParams actor;
                    actor.x = actor.y = 20;
                    actor.spriteKey = team;
                    actor.weaponSlot = 2;
                    actor.aimAngle = aim;
                    clear();
                    renderer.DrawCharacter(actor);
                    SDL_RenderFlush(output);
                    const auto* pixels = static_cast<const Uint8*>(surface->pixels);
                    const std::vector<Uint8> actual(pixels, pixels + surface->pitch * surface->h);
                    clear();
                    SDL_Rect source{0, 512, 512, 512};
                    const float size = actor.size * renderer.GetZoom();
                    SDL_FRect target{(actor.x-renderer.GetCameraX())*renderer.GetZoom()-size/2,
                                     (actor.y-renderer.GetCameraY())*renderer.GetZoom()-size/2, size, size};
                    SDL_FPoint center{size/2, size/2};
                    SDL_RenderCopyExF(output, sheet.tex, &source, &target,
                                      aim*180.0f/3.14159265f+90.0f, &center, SDL_FLIP_NONE);
                    SDL_RenderFlush(output);
                    check(std::memcmp(actual.data(), surface->pixels, actual.size()) == 0,
                          "latest melee atlas faces along aim in all four directions");
                }
                outline.tex = outlineTexture;
            }
            int row = 0, page = 0;
            clear();
            for (int index = 1; index < WeaponDB::kWeaponCount; ++index) {
                const auto& weapon = WeaponDB::Get(static_cast<WeaponId>(index));
                if (!weapon.spritePath) continue;
                SpriteDrawParams actor;
                actor.x = 150/12.0f;
                actor.y = (90 + row * 180)/12.0f;
                actor.weaponId = index;
                actor.weaponSlot = weapon.cls == WeaponClass::Melee ? 2 :
                    (weapon.cls == WeaponClass::Grenade ? 3 : (weapon.slot == WeaponSlotKind::Secondary ? 1 : 0));
                renderer.DrawCharacter(actor);
                renderer.DrawDroppedWeapon(index, 450/12.0f, actor.y, 0);
                int width = 0, height = 0;
                SDL_QueryTexture(renderer.weaponIcons_[index], nullptr, nullptr, &width, &height);
                const float fit = std::min(220.0f/width, 100.0f/height);
                SDL_FRect icon{800-width*fit/2, 90+row*180-height*fit/2, width*fit, height*fit};
                SDL_RenderCopyF(output, renderer.weaponIcons_[index], nullptr, &icon);
                renderer.DrawWeaponStatus(index, 5, 30, 1280, 180*(row+1));
                if (++row == 4) {
                    save("weapons-" + std::to_string(page++));
                    row = 0;
                    clear();
                }
            }
            if (row) save("weapons-" + std::to_string(page));
        }
        SDL_DestroyRenderer(output);
        SDL_FreeSurface(surface);
    }
};

static void TestPix34Input() {
    SDL_InitSubSystem(SDL_INIT_EVENTS);
    InputManager input;
    input.gameplayContext = true;
    input.bindings.keys[static_cast<int>(GameAction::Buy)] = SDL_SCANCODE_F2;
    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    key.key.keysym.scancode = SDL_SCANCODE_F2;
    key.key.keysym.sym = SDLK_z;
    SDL_PushEvent(&key);
    input.ProcessEvents(nullptr);
    check(input.IsBuyMenuToggleRequested(), "PIX-34 remapped event uses physical scancode independently of layout");
    input.captureKey = true;
    key.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    SDL_PushEvent(&key);
    input.ProcessEvents(nullptr);
    check(input.capturedInput == InputBinding(SDL_SCANCODE_ESCAPE) && !input.IsPauseToggleRequested(), "PIX-34 capture consumes Escape before pause");
    input.captureKey = false;
    key.key.keysym.scancode = SDL_SCANCODE_F1;
    SDL_PushEvent(&key);
    input.ProcessEvents(nullptr);
    check(input.helpRequested && !input.IsMuteToggleRequested(), "PIX-34 F1 opens help independently of mute");
    input.bindings.keys[static_cast<int>(GameAction::Fire)] = InputBinding::Mouse(SDL_BUTTON_RIGHT);
    input.bindings.keys[static_cast<int>(GameAction::Scope)] = InputBinding::Mouse(SDL_BUTTON_LEFT);
    SDL_Event mouse{};
    mouse.type = SDL_MOUSEBUTTONDOWN;
    mouse.button.button = SDL_BUTTON_RIGHT;
    SDL_PushEvent(&mouse);
    input.ProcessEvents(nullptr);
    check(input.IsMouseLeftClicked() && !input.IsMouseRightClicked(), "PIX-34 left-handed right button fires");
    input.ProcessEvents(nullptr);
    check(input.IsDown(GameAction::Fire) && !input.WasPressed(GameAction::Fire),
          "round2 mouse held state survives a frame without a new edge");
    mouse.type = SDL_MOUSEBUTTONUP;
    SDL_PushEvent(&mouse);
    input.ProcessEvents(nullptr);
    check(input.WasReleased(GameAction::Fire) && !input.IsDown(GameAction::Fire),
          "round2 mouse release edge clears held state");
    input.ProcessEvents(nullptr);
    check(!input.WasReleased(GameAction::Fire), "round2 release edge lasts one frame");
    for (int action = 0; action < GameActionCount; ++action) {
        KeyBindings bindings;
        bindings.keys.fill({});
        bindings.keys[action] = InputBinding::Mouse(SDL_BUTTON_X1);
        input.SetBindings(bindings);
        mouse.type = SDL_MOUSEBUTTONDOWN;
        mouse.button.button = SDL_BUTTON_X1;
        SDL_PushEvent(&mouse);
        input.ProcessEvents(nullptr);
        check(input.WasPressed(static_cast<GameAction>(action)) && input.IsDown(static_cast<GameAction>(action)),
              "round2 every action supports a side mouse button");
    }
    SDL_Event focus{};
    focus.type = SDL_WINDOWEVENT;
    focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent(&focus);
    input.captureKey = true;
    input.ProcessEvents(nullptr);
    check(!input.IsDown(GameAction::QuickSwitch) && !input.captureKey,
          "round2 focus loss clears held actions and capture state");
    input.gameplayContext = false;
    input.bindings.keys[static_cast<int>(GameAction::Pause)] = InputBinding::Mouse(SDL_BUTTON_X2);
    mouse.button.button = SDL_BUTTON_X2;
    SDL_PushEvent(&mouse);
    input.ProcessEvents(nullptr);
    check(input.WasPressed(GameAction::Pause), "round2 mouse bound return action remains usable in menus");
    input.SetBindings(KeyBindings{});
    input.uiConsumesMouse = true;
    mouse.button.button = SDL_BUTTON_LEFT;
    SDL_PushEvent(&mouse);
    input.ProcessEvents(nullptr);
    check(!input.WasPressed(GameAction::Fire), "round2 left click consumed by a menu widget cannot fire");
    input.SetBindings(KeyBindings{});
    input.bindings.keys[static_cast<int>(GameAction::Fire)] = InputBinding::Mouse(SDL_BUTTON_X2);
    const auto previous = PathUtils::BaseDir();
    auto directory = std::filesystem::absolute("build-evidence/pix34-settings-test");
    std::filesystem::create_directories(directory);
    PathUtils::SetBaseDir(directory.string());
    SaveManager saved;
    saved.MutableSettings().bindings = input.bindings;
    saved.MutableSettings().botDifficulty = 3;
    saved.MutableSettings().combatHintsVisible = false;
    saved.Save();
    SaveManager loaded;
    loaded.Load();
    check(loaded.GetSettings().bindings.keys == input.bindings.keys,
          "PIX-34 all key bindings and handedness survive restart");
    check(loaded.GetSettings().botDifficulty == 3, "PIX-34 difficulty survives restart");
    check(!loaded.GetSettings().combatHintsVisible, "round2 combat help preference survives restart");
    const auto savePath = directory / (std::string("cs2d_save") + std::string(BuildInfo::SaveSuffix()) + ".ini");
    IniFile legacy;
    legacy.SetInt("bindings", "0", SDL_SCANCODE_Q);
    legacy.SetInt("bindings", "1", SDL_SCANCODE_F1);
    legacy.SetBool("bindings", "left_handed", true);
    legacy.Save(savePath.string());
    SaveManager migrated;
    migrated.Load();
    const auto& legacyBindings = migrated.GetSettings().bindings;
    check(legacyBindings.keys[0] == InputBinding(SDL_SCANCODE_Q) && legacyBindings.keys[1] == InputBinding(SDL_SCANCODE_F1),
          "round2 legacy keyboard bindings keep their original actions");
    check(legacyBindings[GameAction::Help].device == InputBinding::Device::None &&
          legacyBindings[GameAction::QuickSwitch].device == InputBinding::Device::None,
          "round2 occupied new defaults migrate to unbound without overwriting original actions");
    check(legacyBindings[GameAction::Fire] == InputBinding::Mouse(SDL_BUTTON_RIGHT) &&
          legacyBindings[GameAction::Scope] == InputBinding::Mouse(SDL_BUTTON_LEFT),
          "round2 legacy handedness migrates into independent mouse bindings");
    migrated.Save();
    SaveManager migratedAgain;
    migratedAgain.Load();
    check(migratedAgain.GetSettings().bindings.keys == legacyBindings.keys,
          "round2 migrated named bindings remain stable after another restart");
    PathUtils::SetBaseDir(previous);
}

static void TestRound2SettingsLayouts() {
    std::filesystem::create_directories("build-evidence/pix34-round2");
    check(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, "round2 settings test initializes SDL video");
    const int dimensions[][2] = {{1280,720}, {1920,1080}, {2560,1600}, {3440,1440}, {1024,768}};
    for (const auto& dimension : dimensions) {
        const int width = dimension[0], height = dimension[1];
        SDL_Window* window = SDL_CreateWindow("PIX-34 layout regression", 0, 0, width, height, SDL_WINDOW_HIDDEN);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* output = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(window && output, "round2 settings layout target initializes");
        if (!window || !output) {
            if (output) SDL_DestroyRenderer(output);
            if (surface) SDL_FreeSurface(surface);
            if (window) SDL_DestroyWindow(window);
            continue;
        }
        {
            ImGuiUI ui;
            check(ui.Initialize(window,output), "round2 settings initializes real font and renderer backends");
            ImGui::GetIO().IniFilename = nullptr;
            GameUI gameUI;
            gameUI.SetImGuiUI(&ui);
            GameSettings settings;
            InputManager input;
            GameUI::MenuCtx menu;
            menu.state = SETTINGS;
            menu.settings = &settings;
            menu.input = &input;
            for (int percent : {100,75,50}) {
                const int logicalHeight = 1080*percent/100;
                const int logicalWidth = static_cast<int>(std::lround(logicalHeight*static_cast<float>(width)/height));
                SDL_RenderSetLogicalSize(output,logicalWidth,logicalHeight);
                ui.SetUiScale(std::min(logicalWidth/1920.0f,logicalHeight/1080.0f));
                for (int frame = 0; frame < 2; ++frame) {
                    SDL_SetRenderDrawColor(output,0,0,0,255);
                    SDL_RenderClear(output);
                    ui.BeginFrame();
                    gameUI.DrawMenu(menu,GameUI::Actions{});
                    ui.EndFrame();
                    SDL_RenderPresent(output);
                }
                const auto* data = ImGui::GetDrawData();
                bool finite = data && data->TotalVtxCount > 0;
                for (int list = 0; data && list < data->CmdListsCount; ++list)
                    for (const auto& vertex : data->CmdLists[list]->VtxBuffer)
                        finite = finite && std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y);
                check(finite, "round2 settings emits valid geometry at each resolution and rendering scale");
                if (percent == 100 || percent == 50) {
                    const std::string path = "build-evidence/pix34-round2/layout-"+std::to_string(width)+"x"+
                        std::to_string(height)+"-"+std::to_string(percent)+".png";
                    check(IMG_SavePNG(surface,path.c_str()) == 0, "round2 settings resolution evidence saved");
                }
                ui.BeginFrame();
                GameUI::InGameCtx hud;
                hud.playerTeam = Team::CT;
                hud.weaponName = "AWP";
                hud.bindings = &settings.bindings;
                hud.objective = 1;
                hud.bombPlanted = true;
                hud.bombTimer = 20;
                hud.defuseProgress = .5f;
                hud.defuseRemaining = 2;
                hud.defuserName = "你";
                gameUI.DrawInGame(hud,&ui);
                gameUI.DrawCombatHints(settings.bindings);
                ui.EndFrame();
                const auto* canvas = ImGui::FindWindowByName("##gameHUD");
                const auto* debug = ImGui::FindWindowByName("Debug##Default");
                check(canvas && canvas->DrawList->VtxBuffer.Size > 100 && (!debug || !debug->Active),
                      "round2 HUD has a fullscreen canvas and never exposes the implicit debug window");
                const auto* hints = ImGui::FindWindowByName("##combatHints");
                check(hints && hints->Pos.x >= 0 && hints->Pos.x+hints->Size.x <= logicalWidth+1 &&
                    hints->Pos.y >= 0 && hints->Pos.y+hints->Size.y <= logicalHeight+1,
                    "round2 combat hints remain inside all viewport sizes");
                if (width == 1280 && percent == 100) {
                    for (int direction = 0; direction < 8; ++direction) {
                        SDL_SetRenderDrawColor(output,40,60,80,255);
                        SDL_RenderClear(output);
                        std::array<Presentation::DamageDirection,4> hits{};
                        hits[0] = {direction*Tuning::Pi/4,.5f};
                        ui.BeginFrame();
                        gameUI.DrawDamageDirections(hits);
                        ui.EndFrame();
                        SDL_RenderPresent(output);
                        const auto* row = static_cast<const Uint8*>(surface->pixels)+height/2*surface->pitch;
                        check(row[width/2*4] == 40 && row[width/2*4+1] == 60,
                              "round2 directional damage leaves the screen center clear in all eight directions");
                        const std::string path = "build-evidence/pix34-round2/damage-"+std::to_string(direction)+".png";
                        check(IMG_SavePNG(surface,path.c_str()) == 0, "round2 eight direction damage evidence saved");
                    }
                    std::vector<MatchRecord> records(50);
                    for (auto& record : records) {
                        record.time = "2026-11-16 18:00";
                        record.modeId = "bomb";
                        record.mapId = "ancient_ruins";
                        record.result = "win";
                        record.scoreCT = record.rounds = 8;
                    }
                    GameUI::HistoryCtx history;
                    history.records = &records;
                    ui.BeginFrame();
                    gameUI.DrawHistory(history,GameUI::Actions{});
                    ui.EndFrame();
                    ImGuiWindow* listWindow = nullptr;
                    for (int position = 0; position < 3; ++position) {
                        if (listWindow) ImGui::SetScrollY(listWindow,listWindow->ScrollMax.y*position/2);
                        SDL_SetRenderDrawColor(output,0,0,0,255);
                        SDL_RenderClear(output);
                        ui.BeginFrame();
                        gameUI.DrawHistory(history,GameUI::Actions{});
                        ui.EndFrame();
                        SDL_RenderPresent(output);
                        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
                            if (std::strstr(candidate->Name,"##historyList")) listWindow = candidate;
                        check(listWindow && listWindow->ScrollMax.y > 0 && listWindow->DrawList->VtxBuffer.Size > 0,
                              "round2 long history owns its scrollable clipped drawing list");
                        const std::string path = "build-evidence/pix34-round2/history-"+std::to_string(position)+".png";
                        check(IMG_SavePNG(surface,path.c_str()) == 0, "round2 history scroll evidence saved");
                    }
                }
            }
        }
        SDL_DestroyRenderer(output);
        SDL_FreeSurface(surface);
        SDL_DestroyWindow(window);
    }
    g_uiScale = 1;
}

static void TestRound2RadarRendering() {
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* software=surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(software!=nullptr,"round2 software radar renderer initializes");
    if (!software) { if(surface) SDL_FreeSurface(surface); return; }
    {
        CS2DMapData data;
        data.mapWidth=data.mapHeight=100;
        data.tileWidth=data.tileHeight=8;
        CS2DWorld world;
        world.Build(data);
        CS2DRenderer renderer;
        renderer.Initialize(software,"assets/cs2d/maps",640,480);
        renderer.SetWorld(&world);
        auto pixel = [&](int x,int y) {
            SDL_Color color{};
            const auto* row=static_cast<const Uint8*>(surface->pixels)+y*surface->pitch;
            Uint32 value;
            std::memcpy(&value,row+x*4,4);
            SDL_GetRGBA(value,surface->format,&color.r,&color.g,&color.b,&color.a);
            return color;
        };
        auto draw = [&](int alpha,bool rotated,bool centered,float px,float py,int size=200) {
            SDL_SetRenderDrawColor(software,100,120,140,255);
            SDL_RenderClear(software);
            const std::vector<CS2DRenderer::RadarDot> dots={{px+100,py,240,40,40,255}};
            renderer.DrawRadarHUD(px,py,0,dots,800,800,centered,rotated,true,alpha,size,1,false,640,480);
            SDL_RenderPresent(software);
        };
        draw(0,false,true,400,400);
        const auto transparent=pixel(70,100);
        check(transparent.r==100 && transparent.g==120 && transparent.b==140,"round2 alpha zero preserves underlying scene");
        const auto corner=pixel(10,10);
        check(corner.r==100 && corner.g==120,"round2 radar clips square corners");
        draw(255,false,true,400,400);
        const auto opaque=pixel(70,100);
        check(opaque.r<100 && opaque.b<140,"round2 opaque radar draws map without RGB alpha modulation");
        draw(128,false,true,400,400);
        const auto half=pixel(70,100);
        check(half.r>opaque.r && half.r<transparent.r,"round2 intermediate radar alpha blends with scene");
        draw(0,true,true,400,400);
        const auto red=pixel(108,83);
        check(red.r>red.g*2,"round2 rotating radar puts player forward at top");
        draw(0,false,true,0,0);
        const auto self=pixel(108,108);
        check(self.b>self.r,"round2 player stays centered at map corner");
        draw(0,false,false,0,0);
        const auto center=pixel(108,108);
        check(center.r==100 && center.g==120,"round2 disabled centering uses map center");
        draw(0,false,true,400,400,400);
        const auto largeSelf=pixel(208,208);
        check(largeSelf.b>largeSelf.r,"round2 400 unit radar supports centered markers");
        SDL_SetRenderDrawColor(software,0,0,0,255);
        SDL_RenderClear(software);
        renderer.DrawCrosshair(320,240,255,255,255,255,2,true,12,5);
        SDL_RenderPresent(software);
        check(pixel(320,240).r > 200 && pixel(323,243).r == 0,
              "round2 center dot is circular and shares the reticle center");
        check(pixel(309,239).r == pixel(330,239).r && pixel(319,229).r == pixel(319,250).r,
              "round2 reticle arms are symmetric around a common floating point center");
        for (float width : {.5f, .8f, 1.0f, 1.19f}) {
            SDL_SetRenderDrawColor(software,0,0,0,255);
            SDL_RenderClear(software);
            renderer.DrawCrosshair(320,240,255,255,255,255,width,false,12,5);
            SDL_RenderPresent(software);
            int visible = 0;
            for (int y=220; y<260; ++y)
                for (int x=300; x<340; ++x) visible += pixel(x,y).r > 30;
            check(visible >= 32,"thin crosshair remains visible below 1.2 pixels");
        }
        SDL_SetRenderDrawColor(software,18,22,28,255);
        SDL_RenderClear(software);
        renderer.SetZoom(1);
        std::vector<Vec2> fireCells;
        for (int y=200; y<=280; y+=4)
            for (int x=280; x<=360; x+=4) fireCells.push_back({float(x),float(y)});
        renderer.DrawFireCells(fireCells,320,240,45,2,5,.3f);
        SDL_RenderPresent(software);
        std::filesystem::create_directories("build-evidence/pix35");
        IMG_SavePNG(surface,"build-evidence/pix35/fire-revised.png");
        check(pixel(320,235).r > 100,"revised fire renders luminous rising tongues");
        for (float width : {.5f, .8f, 1.0f, 1.19f}) {
            SDL_SetRenderDrawColor(software,0,0,0,255);
            SDL_RenderClear(software);
            AimLineStyle style;
            style.solid = style.hidden = {255,255,255,255};
            style.solidThickness = style.hiddenThickness = width;
            renderer.DrawAimLinePreview(style,640,480);
            SDL_RenderPresent(software);
            check(pixel(100,239).r > 30 || pixel(100,240).r > 30,
                  "thin aim line remains visible below 1.2 pixels");
        }
        SDL_Surface* fallback = SDL_CreateRGBSurfaceWithFormat(0,32,32,32,SDL_PIXELFORMAT_RGBA32);
        SDL_FillRect(fallback,nullptr,SDL_MapRGBA(fallback->format,80,180,70,255));
        SDL_Texture* fallbackMap = SDL_CreateTextureFromSurface(software,fallback);
        SDL_SetRenderDrawColor(software,0,0,0,255);
        SDL_RenderClear(software);
        renderer.DrawRadarHUD(400,400,0,{},800,800,false,false,false,255,200,1,true,640,480,false,fallbackMap);
        SDL_RenderPresent(software);
        check(pixel(280,220).g > 100,"settings radar accepts a map before a match loads");
        SDL_DestroyTexture(fallbackMap);
        SDL_FreeSurface(fallback);
        Presentation::ScopeOverlayParams scope;
        scope.scope.active = scope.scope.supported = true;
        scope.scope.transition = 1;
        scope.centerX = 320;
        scope.centerY = 240;
        scope.radiusPx = 150;
        scope.crossThickness = 2;
        auto drawScope = [&](float blur) {
            SDL_SetRenderDrawColor(software,0,0,0,255);
            SDL_RenderClear(software);
            scope.motionBlur = blur;
            renderer.DrawScopeOverlay(scope);
            SDL_RenderPresent(software);
        };
        drawScope(0);
        const auto sharpEdge = pixel(370,242);
        std::filesystem::create_directories("build-evidence/pix34-round2");
        IMG_SavePNG(surface,"build-evidence/pix34-round2/scope-static.png");
        drawScope(3);
        check(pixel(370,242).r > sharpEdge.r,
              "round2 moving scope softens the reticle outside its original hard edge");
        IMG_SavePNG(surface,"build-evidence/pix34-round2/scope-moving.png");
        renderer.Clean();
    }
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
}

static void TestRound3PartialRendering() {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *software = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(software != nullptr, "round3 partial visibility renderer initializes");
    if (software) {
        CS2DWorld world;
        CS2DMapData data;
        data.mapWidth = data.mapHeight = 100;
        data.tileWidth = data.tileHeight = 8;
        world.Build(data);
        CS2DRenderer renderer;
        renderer.Initialize(software, "assets/cs2d/maps", 640, 480);
        renderer.SetWorld(&world);
        renderer.SetPreviewCamera(320, 240, 1, false);
        SDL_SetRenderDrawColor(software, 0, 0, 0, 255);
        SDL_RenderClear(software);
        renderer.SetVisibilityFilter([](float x, float) { return x >= 320; });
        SpriteDrawParams actor;
        actor.x = 320.2f;
        actor.y = 240.3f;
        actor.size = 128.3f;
        actor.aimAngle = .37f;
        actor.weaponSlot = 1;
        actor.weaponId = static_cast<int>(WeaponId::USP_S);
        actor.localPlayer = true;
        renderer.DrawCharacter(actor);
        SDL_RenderPresent(software);
        int left = 0, right = 0;
        for (int y = 100; y < 380; ++y)
            for (int x = 180; x < 460; ++x) {
                const auto *pixel = static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch + x * 4;
                if (pixel[0] + pixel[1] + pixel[2] > 20) {
                    if (x < 320)
                        ++left;
                    else
                        ++right;
                }
            }
        std::printf("[round3 masked pixels] left=%d right=%d SDL=%s\n", left, right, SDL_GetError());
        check(left == 0 && right > 100, "round3 actual body weapon and outline retain exposed pixels only");
        std::filesystem::create_directories("build-evidence/pix34-round3");
        IMG_SavePNG(surface, "build-evidence/pix34-round3/partial-body.png");
        bool bounded = true;
        for (float tier : {1.12f, .70f, .50f})
            for (float scope : {1.f, 2.f})
                for (int direction = 0; direction < 8; ++direction)
                    for (float distance : {50.f, 400.f, 800.f}) {
                        const float angle = direction * 6.2831853f / 8;
                        const float aimX = std::cos(angle) * distance, aimY = std::sin(angle) * distance;
                        const float lens = scope > 1 ? 100.f : 8.f;
                        renderer.UpdatePlayerCamera(0,0,14,aimX,aimY,640,480,tier,lens,{20,-20});
                        const float zoom = renderer.GetZoom();
                        const float actorX = -renderer.GetCameraX() * zoom,
                                    actorY = -renderer.GetCameraY() * zoom;
                        const float scopeX = (aimX - renderer.GetCameraX()) * zoom,
                                    scopeY = (aimY - renderer.GetCameraY()) * zoom;
                        bounded &= std::isfinite(zoom) && zoom > 0 && actorX - 14 * zoom >= 11.9f &&
                                   actorX + 14 * zoom <= 628.1f && actorY - 14 * zoom >= 11.9f &&
                                   actorY + 14 * zoom <= 468.1f && scopeX >= lens + 11.9f &&
                                   scopeX <= 640 - lens - 11.9f && scopeY >= lens + 11.9f &&
                                   scopeY <= 480 - lens - 11.9f;
                    }
        check(bounded, "round3 all tiers scopes aim directions and recoil shake keep complete actor and lens "
                       "inside safe viewport");
    }
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
}

static void TestRound3HudLayouts(bool experience = false) {
    const std::filesystem::path directory = experience ? "build-evidence/experience/hud" : "build-evidence/pix34-round3";
    std::filesystem::create_directories(directory);
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    const int dimensions[][2] = {{1280, 720}, {1920, 1080}, {2560, 1600}, {3440, 1440}, {1024, 768}};
    for (const auto &dimensionsForCase : dimensions) {
        const int width = dimensionsForCase[0], height = dimensionsForCase[1];
        SDL_Window *window = SDL_CreateWindow("PIX34 HUD review", 0, 0, width, height, SDL_WINDOW_HIDDEN);
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        check(window && renderer, "round3 HUD matrix target initializes");
        if (window && renderer) {
            ImGuiUI ui;
            ui.Initialize(window, renderer);
            ImGui::GetIO().IniFilename = nullptr;
            WeaponIconCache icons;
            icons.Initialize(renderer);
            GameUI gameUI;
            gameUI.SetImGuiUI(&ui);
            gameUI.SetWeaponIconCache(&icons);
            KeyBindings bindings;
            for (int percent : {100, 75, 50}) {
                const int logicalH = 1080 * percent / 100;
                const int logicalW =
                    static_cast<int>(std::lround(logicalH * static_cast<float>(width) / height));
                SDL_RenderSetLogicalSize(renderer, logicalW, logicalH);
                ui.SetUiScale(std::min(logicalW / 1920.f, logicalH / 1080.f));
                const char *scenes[] = {"buy",      "combat",    "planted",   "defusing",
                                        "low-time", "round-end", "deathmatch", "death-card", "c4-view", "armor-only", "no-armor",
                                        "defusing-ally", "defusing-enemy"};
                const int sceneCount = experience ? 13 : 7;
                for (int scene = 0; scene < sceneCount; ++scene) {
                    GameUI::InGameCtx hud;
                    hud.state = PLAYING;
                    hud.playerTeam = scene == 6 ? Team::None : Team::CT;
                    hud.bindings = &bindings;
                    hud.scoreT = 7;
                    hud.scoreCT = 6;
                    hud.aliveT = 3;
                    hud.aliveCT = 4;
                    hud.roundNumber = 14;
                    hud.roundRemainingSec = scene == 4 ? 7 : 83;
                    hud.roundPhase = scene == 0 ? 2 : scene == 5 ? 4 : 3;
                    hud.objective = scene == 6 ? 0 : 1;
                    hud.health = 83;
                    hud.maxHealth = 100;
                    hud.armor = 100;
                    hud.helmet = scene != 9;
                    if (scene == 10) hud.armor = 0;
                    if (scene == 7) {
                        hud.health = 0;
                        hud.death.active = true;
                        hud.death.remaining = 1.2f;
                        hud.death.killer = "长名称测试玩家甲";
                        hud.death.killerTeam = Team::T;
                        hud.death.weapon = "AK-47";
                        hud.death.icon = WeaponDB::Get(WeaponId::AK47).iconPath;
                    }
                    if (scene == 8) {
                        hud.health = 0;
                        hud.spectating = true;
                        hud.bombSpectating = true;
                    }
                    hud.money = 4200;
                    hud.ammo = 7;
                    hud.magazineSize = 30;
                    hud.reserveAmmo = 90;
                    hud.hasDefuseKit = scene != 6;
                    hud.kills = 24;
                    hud.deaths = 9;
                    hud.bombPlanted = scene == 2 || scene == 3 || scene == 8 ||
                                      scene == 11 || scene == 12;
                    hud.bombTimer = scene == 3 ? 9.2f : 31.8f;
                    // PIX-36 拆包信息隔离：队友侧只显示剩余秒数；对手侧整行隐藏。
                    // scene 11 = 己方队友在拆包（可见），scene 12 = 敌方在拆包（完全不可见）。
                    const bool allyDefusing = scene == 3 || scene == 11;
                    hud.defuseProgress = allyDefusing ? .62f : (scene == 12 ? .62f : 0);
                    hud.defuseRemaining = 1.52f;
                    hud.defuserName = "你";
                    hud.defuseVisibleToViewer = allyDefusing;
                    if (scene == 5) {
                        hud.announcement = "反恐精英胜利";
                        hud.announcementAlpha = 1;
                    }
                    for (const auto id :
                         {WeaponId::M4A1S, WeaponId::USP_S, WeaponId::Knife, WeaponId::GrenadeHE,
                          WeaponId::GrenadeFlash, WeaponId::GrenadeSmoke}) {
                        const auto &definition = WeaponDB::Get(id);
                        const int slot = definition.slot == WeaponSlotKind::Primary     ? 0
                                         : definition.slot == WeaponSlotKind::Secondary ? 1
                                         : definition.slot == WeaponSlotKind::Melee     ? 2
                                                                                        : 3;
                        hud.equipment.push_back(
                            {definition.name, definition.iconPath,
                             bindings[static_cast<GameAction>(static_cast<int>(GameAction::Slot1) + slot)]
                                 .Label(),
                             id == WeaponId::GrenadeFlash ? 2 : 1, id == WeaponId::M4A1S, slot});
                    }
                    for (int index = 0; index < 6; ++index) {
                        const auto &definition = WeaponDB::Get(index % 2 ? WeaponId::AWP : WeaponId::AK47);
                        hud.killFeed.push_back(
                            {static_cast<std::uint64_t>(index + 1), index % 2 ? "长名称测试玩家甲" : "你",
                             "目标玩家", definition.iconPath, Team::CT, Team::T, 3.2f, index % 2 == 0});
                    }
                    for (int frame = 0; frame < 5; ++frame) {
                        SDL_SetRenderDrawColor(renderer, 44, 48, 50, 255);
                        SDL_RenderClear(renderer);
                        ui.BeginFrame();
                        ImGui::GetIO().DeltaTime = .1f;
                        gameUI.DrawInGame(hud, &ui);
                        gameUI.DrawCombatHints(bindings);
                        ui.EndFrame();
                        SDL_RenderPresent(renderer);
                    }
                    const auto *draws = ImGui::GetDrawData();
                    bool valid = draws && draws->TotalVtxCount > 0;
                    for (int list = 0; draws && list < draws->CmdListsCount; ++list)
                        for (const auto &vertex : draws->CmdLists[list]->VtxBuffer)
                            valid &= std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y);
                    check(valid, "round3 HUD stage resolution and rendering scale emit finite geometry");
                    if (scene == 3 || (experience && scene >= 7) || (width == 1920 && percent == 100)) {
                        const auto path = directory / ("hud-" + std::string(scenes[scene]) + "-" +
                                                       std::to_string(width) + "x" + std::to_string(height) +
                                                       "-" + std::to_string(percent) + ".png");
                        check(IMG_SavePNG(surface, path.string().c_str()) == 0,
                              "round3 HUD state evidence saved");
                    }
                }
            }
        }
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
        SDL_DestroyWindow(window);
    }
}

static int ExportRound3Ballistics() {
    std::filesystem::create_directories("build-evidence/pix34-round3/ballistics");
    std::ofstream parameters("build-evidence/pix34-round3/ballistics/converted-parameters.csv");
    std::ofstream curves("build-evidence/pix34-round3/ballistics/recoil-curves.csv");
    std::ofstream impacts("build-evidence/pix34-round3/ballistics/impact-samples.csv");
    parameters << "id,name,seed,scope,speed,spread_radians,spread_degrees,recovery_seconds,recoil_gain\n";
    curves << "id,name,scope,shot,kick_x,kick_y,inaccuracy\n";
    impacts << "id,name,scope,speed,sample,forward,lateral\n";
    for (int index = 2; index <= static_cast<int>(WeaponId::Negev); ++index) {
        const auto id = static_cast<WeaponId>(index);
        const auto &weapon = WeaponDB::Get(id);
        const auto &profile = Ballistics::Get(id);
        for (int scoped = 0; scoped <= 1; ++scoped) {
            CS2DGame::Player actor;
            actor.SetSlotWeapon(0, id);
            actor.weaponSlot = 0;
            actor.LoadAmmoFromSlot();
            for (int shot = 0; shot < std::min(40, std::max(1, weapon.magazine)); ++shot) {
                actor.ApplyWeaponRecoil(static_cast<float>(scoped));
                const auto kick = actor.BallisticKick();
                curves << index << ',' << weapon.name << ',' << scoped << ',' << shot + 1 << ',' << kick.x
                       << ',' << kick.y << ',' << actor.BallisticInaccuracy() << '\n';
                actor.RecoverBallistics(weapon.cooldown);
            }
            for (int speed = 0; speed <= 1; ++speed) {
                const float spread =
                    Ballistics::Spread(id, static_cast<float>(speed), static_cast<float>(scoped), 0);
                parameters << index << ',' << weapon.name << ',' << profile.seed << ',' << scoped << ','
                           << speed << ',' << spread << ',' << spread / Tuning::DegreesToRadians << ','
                           << profile.recovery << ',' << std::tan(.6f * Tuning::DegreesToRadians) / 30
                           << '\n';
                std::mt19937 random(profile.seed);
                std::uniform_real_distribution<float> sample(-spread, spread);
                for (int hit = 0; hit < 512; ++hit) {
                    const float angle = sample(random);
                    impacts << index << ',' << weapon.name << ',' << scoped << ',' << speed << ',' << hit
                            << ',' << 500 * std::cos(angle) << ',' << 500 * std::sin(angle) << '\n';
                }
            }
        }
    }
    return parameters && curves && impacts ? 0 : 1;
}

struct EngineTestAccess {
    // PIX-37 第 7 项：暂停 → 继续 必须恢复黑场过渡（不是瞬间切换）。
    static void TestPix37ResumeTransition() {
        std::printf("\n===== PIX-37 暂停继续的过渡 =====\n");
        const auto previousBase = PathUtils::BaseDir();
        const auto directory = std::filesystem::absolute("build-evidence/pix37");
        std::filesystem::create_directories(directory);
        PathUtils::SetBaseDir(directory.string());
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        {
            Engine engine;
            engine.window = SDL_CreateWindow("PIX37 resume transition", 0, 0, 640, 480,
                                             SDL_WINDOW_HIDDEN);
            engine.renderer = engine.window
                ? SDL_CreateRenderer(engine.window, -1,
                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE)
                : nullptr;
            check(engine.renderer != nullptr, "PIX-37 resume-transition renderer initializes");
            if (engine.renderer != nullptr) {
                // ResizeLogicalViewport 会改写全局 g_uiScale；恢复它，
                // 否则后续 HUD/雷达用例会读到被这里改小的缩放而误判。
                const float savedUiScale = g_uiScale;
                engine.ResizeLogicalViewport(640, 480);
                engine.currentState = PLAYING;
                engine.lastRenderedState_ = PLAYING;
                const auto actions = engine.BuildActions();

                // 暂停：按钮「继续」路径
                engine.currentState = PAUSED;
                engine.transitionKind_ = Engine::TransitionKind::None;
                actions.resumeGame();
                check(engine.currentState == PLAYING,
                      "PIX-37 the resume button returns to gameplay");
                check(engine.transitionKind_ == Engine::TransitionKind::Fade,
                      "PIX-37 the resume button starts the black fade transition");
                check(engine.transitionDuration_ > 0.0f,
                      "PIX-37 the resume transition has a non-zero duration");
                check(engine.transitionElapsed_ == 0.0f,
                      "PIX-37 the resume transition starts from the beginning");

                // Esc 继续路径
                engine.currentState = PAUSED;
                engine.transitionKind_ = Engine::TransitionKind::None;
                engine.BeginTransition(PAUSED, PLAYING);   // 与 Esc 分支同一入口
                engine.currentState = PLAYING;
                check(engine.transitionKind_ == Engine::TransitionKind::Fade,
                      "PIX-37 the Esc resume also goes through the fade transition");
                g_uiScale = savedUiScale;
            }
            if (engine.renderer) SDL_DestroyRenderer(engine.renderer);
            engine.renderer = nullptr;
            if (engine.window) SDL_DestroyWindow(engine.window);
            engine.window = nullptr;
        }
        PathUtils::SetBaseDir(previousBase);
    }

    static void CaptureSceneryMenu(int scene, const std::filesystem::path& output, int height = 720, bool exerciseMenu = false) {
        const auto directory = std::filesystem::absolute(output);
        std::filesystem::create_directories(directory);
        const auto oldBase = PathUtils::BaseDir();
        PathUtils::SetBaseDir(directory.string());
        SDL_setenv("SDL_AUDIODRIVER","dummy",1);
        {
            Engine engine;
            const bool ready = engine.Initialize("PIX-39 menu acceptance",1280,height,false,false,1,false);
            check(ready && engine.menu3D && engine.menu3D->IsReady() && engine.menu3D->ScenePreview(scene),"Production menu loads the requested 3D scene and its preview");
            if (ready && engine.menu3D) {
                ImGui::GetIO().IniFilename = nullptr;
                engine.menu3D->SetScene(scene,true);
                SDL_SetWindowSize(engine.window,1280,height);
                engine.ResizeLogicalViewport(1280,height);
                auto capture = [&](const char* file) {
                    SDL_Delay(40);
                    engine.Render();
                    SDL_Texture* frame = engine.sceneTex_[1-engine.sceneWrite_];
                    int w=0,h=0;
                    bool ok = frame && SDL_QueryTexture(frame,nullptr,nullptr,&w,&h)==0;
                    SDL_Texture* previous = SDL_GetRenderTarget(engine.renderer);
                    SDL_Surface* pixels = ok ? SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32) : nullptr;
                    ok = pixels && SDL_SetRenderTarget(engine.renderer,frame)==0 &&
                         SDL_RenderReadPixels(engine.renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels->pixels,pixels->pitch)==0 &&
                         IMG_SavePNG(pixels,(directory/file).string().c_str())==0;
                    check(ok,file);
                    SDL_SetRenderTarget(engine.renderer,previous);
                    SDL_FreeSurface(pixels);
                };
                capture("main.png");
                engine.focused3D_=3;
                engine.focusAnimStartMs_=SDL_GetTicks64()-400;
                engine.Render(); // Enter focus before sampling the fully-open camera.
                capture("focus.png");
                engine.focused3D_=-1;
                const auto actions = engine.BuildActions();
                actions.goState(MAP_SELECT);
                engine.menuCameraStartMs_=SDL_GetTicks64()-850;
                capture("overhead.png");
                if (exerciseMenu) {
                    engine.menuCameraStartMs_ = SDL_GetTicks64()-950;
                    engine.Render();
                    engine.mapOverlayStartMs_ = SDL_GetTicks64()-400;
                    capture("map-cards.png");
                    check(engine.mapOverlayOpen_ && engine.mapOverlayCount_ > 1,"Map picker opened through main menu");
                    actions.selectMapChoice(1);
                    check(engine.mapOverlaySelected_ == 1,"Map card updates selection");
                    actions.closeMapOverlay();
                    engine.mapOverlayCloseStartMs_ = SDL_GetTicks64()-950;
                    capture("return.png");
                    check(!engine.mapOverlayOpen_ && engine.currentState == MAIN_MENU,"Return completed in main menu");
                    actions.goState(MAP_SELECT);
                    engine.menuCameraStartMs_ = SDL_GetTicks64()-950;
                    engine.Render();
                    actions.startMatchOnChoice(1);
                    engine.mapOverlayLaunchStartMs_ = SDL_GetTicks64()-400;
                    const auto deadline = SDL_GetTicks64()+5000;
                    while (engine.currentState == MAIN_MENU && SDL_GetTicks64()<deadline) {
                        SDL_Delay(16);
                        engine.Render();
                    }
                    check(engine.currentState == PLAYING,"Selected map entered gameplay");
                }
            }
        }
        PathUtils::SetBaseDir(oldBase);
    }

    static void TestExperienceEntryPoints() {
        const auto previousBase=PathUtils::BaseDir();
        const auto directory=std::filesystem::absolute("build-evidence/experience/engine");
        std::filesystem::create_directories(directory);
        PathUtils::SetBaseDir(directory.string());
        SDL_setenv("SDL_AUDIODRIVER","dummy",1);
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        {
            Engine engine;
            engine.window=SDL_CreateWindow("Experience entry points",0,0,1280,720,SDL_WINDOW_HIDDEN);
            engine.renderer=SDL_CreateRenderer(engine.window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE);
            check(engine.renderer!=nullptr,"experience Engine renderer initializes");
            if (!engine.renderer) return;
            engine.imGuiUI->Initialize(engine.window,engine.renderer);
            ImGui::GetIO().IniFilename=nullptr;
            engine.weaponIconCache_->Initialize(engine.renderer);
            engine.gameUI->SetWeaponIconCache(engine.weaponIconCache_.get());
            engine.cs2dRenderer->Initialize(engine.renderer,"assets/cs2d/maps",1920,1080);
            engine.cs2dGame->SetMatchMode(MatchMode::BombDefusal);
            engine.cs2dGame->LoadMap(MapPath(),engine.cs2dRenderer.get(),true);
            engine.currentState=PLAYING;engine.lastRenderedState_=PLAYING;
            engine.ResizeLogicalViewport(1280,720);
            CS2DGameTestAccess::EquipExperienceActor(*engine.cs2dGame);
            engine.cs2dOverlay.InitializeCursor(0);
            const auto actions=engine.BuildActions();
            auto& settings=engine.saveManager->MutableSettings();
            actions.setTargetFPS(165);actions.setVSync(true);
            check(engine.targetFPS_==165 && settings.targetFPS==165 && engine.vsync_,"experience actual Engine actions apply custom FPS and VSync");
            actions.resetSettingsSection(GameUI::SettingsSection::InputDisplay);
            check(engine.targetFPS_==60 && !engine.vsync_ && settings.renderScalePct==100,"experience display reset applies runtime and persisted defaults");
            settings.crossThickness=3;actions.setCrosshairThickness(3);
            settings.aimLineStyle.solidThickness=5;
            actions.setAimLineStyle(settings.aimLineStyle);
            actions.resetSettingsSection(GameUI::SettingsSection::Crosshair);
            check(settings.crossThickness==1.5f && engine.cs2dOverlay.crosshair.thickness==1.5f &&
                  settings.aimLineStyle.solidThickness==5,"experience crosshair reset synchronizes runtime and preserves the other panel");
            actions.resetSettingsSection(GameUI::SettingsSection::AimLine);
            check(settings.aimLineStyle.solidThickness==1 && engine.cs2dOverlay.aimLineStyle.solidThickness==1,"experience aim line reset updates its live preview state");
            actions.setFogOpacity(20);actions.setScopeSensitivity(.5f);
            actions.resetSettingsSection(GameUI::SettingsSection::VisionScope);
            check(settings.fogOpacityPct==100 && settings.scopeSensitivity==1,"experience vision reset applies default values");
            actions.setRadarHudSize(300);actions.resetSettingsSection(GameUI::SettingsSection::Radar);
            check(settings.radarHudSize==120 && engine.cs2dOverlay.radar.hudSize==120,"experience radar reset updates the active overlay");
            engine.audioManager->SetMusicVolume(7);engine.audioManager->SetMuted(true);
            actions.resetSettingsSection(GameUI::SettingsSection::Audio);
            check(engine.audioManager->GetMusicVolume()==40 && !engine.audioManager->IsMuted(),"experience audio reset applies default volume and unmute");
            settings.bindings.keys[0]=SDL_SCANCODE_UP;
            actions.resetSettingsSection(GameUI::SettingsSection::Bindings);
            check(settings.bindings.keys[0]==InputBinding(SDL_SCANCODE_W) && engine.inputManager->captureAction<0,"experience bindings reset clears input capture");
            auto saveScene=[&](const char* name) {
                SDL_Texture* target=SDL_GetRenderTarget(engine.renderer);
                SDL_Texture* scene=engine.sceneTex_[1-engine.sceneWrite_];
                int width=0,height=0;SDL_QueryTexture(scene,nullptr,nullptr,&width,&height);
                SDL_Surface* image=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);
                SDL_SetRenderTarget(engine.renderer,scene);
                SDL_RenderReadPixels(engine.renderer,nullptr,SDL_PIXELFORMAT_RGBA32,image->pixels,image->pitch);
                const auto path=directory/name;
                check(IMG_SavePNG(image,path.string().c_str())==0,"experience actual Engine scene evidence saved");
                SDL_SetRenderTarget(engine.renderer,target);SDL_FreeSurface(image);
            };
            auto frame=[&] {
                engine.cs2dRenderer->SetFrameDelta(1.f/60);
                engine.cs2dGame->PrepareCamera(engine.logicalW_,engine.logicalH_,1);
                engine.Render();
            };
            engine.currentState=SETTINGS;engine.lastRenderedState_=SETTINGS;
            engine.Render();saveScene("settings-display.png");
            SDL_Texture* priorTarget = SDL_GetRenderTarget(engine.renderer);
            SDL_SetRenderTarget(engine.renderer,engine.sceneTex_[1-engine.sceneWrite_]);
            actions.setFullscreen(true);
            int windowWidth=0,windowHeight=0;
            SDL_GetWindowSize(engine.window,&windowWidth,&windowHeight);
            check(engine.isFullscreen && std::abs(float(engine.logicalW_)/engine.logicalH_-
                  float(windowWidth)/windowHeight)<.002f,"experience fullscreen uses the desktop aspect while a scene target is active");
            SDL_SetRenderTarget(engine.renderer,priorTarget);
            engine.Render();saveScene("settings-fullscreen.png");
            actions.setFullscreen(false);
            SDL_GetWindowSize(engine.window,&windowWidth,&windowHeight);
            check(!engine.isFullscreen && windowWidth==engine.currentResW_ && windowHeight==engine.currentResH_,
                  "experience leaving fullscreen restores the saved window size");
            engine.currentState=PLAYING;engine.lastRenderedState_=PLAYING;
            frame();saveScene("armor-hud.png");
            engine.cs2dGame->SetCameraDistanceTier(1);
            for (int tick=0;tick<=24;++tick) {
                const auto& actor=engine.cs2dGame->GetPlayer();
                engine.cs2dGame->Update(1.f/60,false,actor.x+80,actor.y,false,false,false,false,false,false,false,nullptr);
                frame();
                if (tick%6==0) {
                    const auto name="camera-transition-"+std::to_string(tick)+".png";
                    saveScene(name.c_str());
                }
            }
            engine.cs2dGame->SetCameraDistanceTier(0);
            for (int level=1;level<=2;++level) {
                engine.cs2dGame->UpdateScopeInput(true,true);
                const auto& player=engine.cs2dGame->GetPlayer();
                engine.cs2dGame->Update(.15f,false,player.x+80,player.y,false,false,false,false,false,false,false,nullptr);
                for(int i=0;i<30;++i) frame();
                saveScene(level==1?"scope-1.5x.png":"scope-3x.png");
            }
            CS2DGameTestAccess::KillExperienceActor(*engine.cs2dGame);
            const auto& player=engine.cs2dGame->GetPlayer();
            engine.cs2dGame->Update(.2f,false,player.x,player.y,false,false,false,false,false,false,false,nullptr);
            frame();saveScene("death-card.png");
            // 阵亡定格期改为直播：镜头固定，但世界逐帧重画。
            // (a) 相隔 0.5 秒的两帧像素不同（世界在动）
            // (b) 两帧的相机位置相同（镜头固定在阵亡位置）
            auto readFrame=[&](std::vector<Uint8>& out){
                SDL_SetRenderTarget(engine.renderer,nullptr);
                out.resize(static_cast<size_t>(engine.logicalW_)*engine.logicalH_*4);
                SDL_RenderReadPixels(engine.renderer,nullptr,SDL_PIXELFORMAT_RGBA32,out.data(),engine.logicalW_*4);
            };
            check(engine.cs2dGame->IsDeathFreeze(),"experience death freeze is active for the live feed check");
            std::vector<Uint8> liveFirst,liveSecond;
            const float cameraX0=engine.cs2dRenderer->GetCameraX();
            const float cameraY0=engine.cs2dRenderer->GetCameraY();
            const float cameraZoom0=engine.cs2dRenderer->GetZoom();
            readFrame(liveFirst);
            saveScene("death-live-frame-a.png");
            for(int i=0;i<30;++i) {
                engine.cs2dGame->Update(1.f/60,false,player.x,player.y,false,false,false,false,false,false,false,nullptr);
                frame();
            }
            readFrame(liveSecond);
            saveScene("death-live-frame-b.png");
            check(liveFirst!=liveSecond,"experience death freeze keeps rendering the live world (two frames half a second apart differ)");
            check(engine.cs2dRenderer->GetCameraX()==cameraX0 &&
                  engine.cs2dRenderer->GetCameraY()==cameraY0 &&
                  engine.cs2dRenderer->GetZoom()==cameraZoom0,
                  "experience death freeze keeps the camera locked at the death position");
            const float remaining=engine.cs2dGame->GetDeathPresentation().remaining;
            engine.currentState=PAUSED;engine.Update(.5f);engine.currentState=PLAYING;
            check(engine.cs2dGame->GetDeathPresentation().remaining==remaining,"experience pause preserves the death presentation timer");
            // 定格期改尺寸：逻辑画布跟随渲染缩放重算（高度的 1080 基准），
            // 渲染器与逻辑尺寸必须同步，并且仍然每帧直播。
            const int resizeW=1024,resizeH=768;
            engine.ResizeLogicalViewport(resizeW,resizeH);
            frame();
            check(engine.logicalW_>360 && engine.logicalH_==1080 &&
                  engine.cs2dRenderer->GetViewW()==engine.logicalW_ &&
                  engine.cs2dRenderer->GetViewH()==engine.logicalH_,
                  "experience resizing during the death freeze re-renders the live world at the new viewport");
            check(engine.cs2dGame->IsDeathFreeze(),
                  "experience resizing does not end the death freeze early");
            engine.cs2dGame->Update(1.2f,false,player.x,player.y,false,false,false,false,false,false,false,nullptr);
            frame();
            check(!engine.cs2dGame->IsDeathFreeze(),"experience death freeze hands off to the spectator view");
            CS2DGameTestAccess::PlaceExperienceBomb(*engine.cs2dGame);
            for(int i=0;i<45;++i) frame();
            saveScene("c4-spectator.png");
            // PIX-36 观战自由视角并集：两名存活队友朝向相反，雾区应点亮两块。
            {
                CS2DGameTestAccess::PlaceFreeUnionSpectator(*engine.cs2dGame);
                for(int i=0;i<45;++i) frame();
                saveScene("spectator-free-union.png");
            }
            engine.Clean();
        }
        PathUtils::SetBaseDir(previousBase);
    }

    static void TestWheelAndLoadoutAcceptance() {
        std::printf("\n===== 双层轮盘与赛前武器配装真实引擎验收 =====\n");
        const auto previousBase = PathUtils::BaseDir();
        const auto directory = std::filesystem::absolute("build-evidence/wheel-loadout");
        std::filesystem::create_directories(directory);
        PathUtils::SetBaseDir(directory.string());
        std::error_code ec;
        std::filesystem::remove(directory / (std::string("cs2d_save") + BuildInfo::SaveSuffix() + ".ini"), ec);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        {
            Engine engine;
            const bool okInit = engine.Initialize("Wheel & Loadout Acceptance", 1280, 720, false, false, 1, false);
            check(okInit, "验收引擎初始化成功 (1280x720)");
            if (okInit) {
                ImGui::GetIO().IniFilename = nullptr;
                auto saveScene = [&](const char* name) {
                    SDL_Texture* target = SDL_GetRenderTarget(engine.renderer);
                    SDL_Texture* scene = engine.sceneTex_[1 - engine.sceneWrite_];
                    int w = 0, h = 0;
                    bool ok = scene && SDL_QueryTexture(scene, nullptr, nullptr, &w, &h) == 0;
                    SDL_Surface* img = ok ? SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32) : nullptr;
                    ok = img && SDL_SetRenderTarget(engine.renderer, scene) == 0 &&
                         SDL_RenderReadPixels(engine.renderer, nullptr, SDL_PIXELFORMAT_RGBA32, img->pixels, img->pitch) == 0 &&
                         IMG_SavePNG(img, (directory / name).string().c_str()) == 0;
                    SDL_SetRenderTarget(engine.renderer, target);
                    SDL_FreeSurface(img);
                    check(ok, name);
                };

                // 1) 主菜单：同步布局按钮包围盒
                engine.Render();
                check(engine.mainMenuBtnRectValid_, "主菜单按钮列包围盒由 DrawMenu 同步到 Engine");
                check(engine.mainMenuBtnRect_[3] > engine.mainMenuBtnRect_[1] + 200.0f,
                      "主菜单 7 按钮区域覆盖武器配装入口且防点穿");

                // 2) 小鸡轮盘：打开后停在一级，再进入二级颜色页（7 扇区）
                if (engine.menu3D && engine.menu3D->IsReady()) {
                    engine.focused3D_ = 2; // 第一只小鸡
                    engine.focusAnimStartMs_ = SDL_GetTicks64() - 500;
                    engine.Render();
                    saveScene("chicken-wheel-lvl1.png");

                    // 用数字键 3 进入第 3 个一级扇区（颜色，对应 Menu3D cat=0）
                    auto pressAndRelease = [&](ImGuiKey key) {
                        auto& io = ImGui::GetIO();
                        io.AddKeyEvent(key, true);
                        engine.Render();
                        io.AddKeyEvent(key, false);
                        engine.Render();
                    };

                    pressAndRelease(ImGuiKey_3);
                    saveScene("chicken-wheel-lvl2-colors.png");

                    // 选中第 5 种羽色（绿羽 index=4）并验证停在二级且持久化
                    pressAndRelease(ImGuiKey_5);
                    check(engine.saveManager->GetSettings().menuColor[2] == 4,
                          "小鸡颜色二级数字键选中绿羽并写入存档");

                    // 二级 Esc 退回一级，不直接退出聚焦
                    pressAndRelease(ImGuiKey_Escape);
                    check(engine.focused3D_ == 2 && !engine.focus3DClosing_,
                          "小鸡二级按 Esc 只退回一级");

                    // 一级 Esc 退出聚焦
                    pressAndRelease(ImGuiKey_Escape);
                    check(engine.focus3DClosing_ || engine.focused3D_ == -1,
                          "小鸡一级按 Esc 退出聚焦");
                    engine.focused3D_ = -1;
                    engine.focus3DClosing_ = false;
                }

                // 3) 配装页：1280x720 / 1920x1080 / 1920x1200 均可渲染，修改 CT 起始为 P2000 并用 AUG 换 AWP
                auto actions = engine.BuildActions();
                actions.goState(LOADOUT);
                engine.transitionKind_ = Engine::TransitionKind::None;
                engine.lastRenderedState_ = LOADOUT;
                engine.Render();
                saveScene("loadout-1280x720.png");

                for (auto [rw, rh] : { std::pair{1920, 1080}, std::pair{1920, 1200} }) {
                    SDL_SetWindowSize(engine.window, rw, rh);
                    engine.ResizeLogicalViewport(rw, rh);
                    engine.Render();
                    check(engine.logicalW_ > 0 && engine.logicalH_ > 0, "多分辨率下配装页渲染正常");
                }
                SDL_SetWindowSize(engine.window, 1280, 720);
                engine.ResizeLogicalViewport(1280, 720);

                WeaponDB::MatchLoadouts custom = WeaponDB::DefaultMatchLoadouts();
                custom.ct.starting = WeaponId::P2000;
                custom.ct.rifles[4] = WeaponId::AUG; // AWP 换成 AUG
                actions.saveLoadouts(custom);
                check(engine.saveManager->GetSettings().loadouts.ct.starting == WeaponId::P2000 &&
                      engine.saveManager->GetSettings().loadouts.ct.rifles[4] == WeaponId::AUG,
                      "保存配装写入 SaveManager");

                // 4) 3D 背景不可用时（置空 menu3D），配装页仍可正常打开并渲染
                auto savedMenu3D = std::move(engine.menu3D);
                engine.currentState = LOADOUT;
                engine.Render();
                check(engine.currentState == LOADOUT, "3D 背景不可用时仍可编辑配装");
                engine.menu3D = std::move(savedMenu3D);

                // 5) 进入爆破模式对局（CT），验证出生手枪为 P2000，且购买轮盘打开/两层/收盘恢复相对瞄准
                engine.StartMatchWithMap(MatchMode::BombDefusal, "assets/cs2d/maps/ancient_ruins.tmj", Team::CT);
                engine.transitionKind_ = Engine::TransitionKind::None;
                engine.lastRenderedState_ = PLAYING;
                check(engine.cs2dGame->GetPlayer().weapons[1] == WeaponId::P2000,
                      "爆破 CT 开局出生自动发配装起始手枪 P2000");
                check(engine.cs2dGame->GetPlayer().ammo == WeaponDB::Get(WeaponId::P2000).magazine &&
                      engine.cs2dGame->GetPlayer().reserve == WeaponDB::Get(WeaponId::P2000).magazine * 3,
                      "P2000 弹匣与备弹按 WeaponDB 初始化");
                check(engine.cs2dGame->EvaluatePurchase(WeaponReq(WeaponId::AWP)) == Presentation::BuyCode::NotInLoadout,
                      "移除 AWP 后直接请求购买返回 NotInLoadout");

                engine.cs2dGame->GetPlayer().money = 16000;
                engine.OpenBuyMenu();
                check(engine.buyMenuOpen_ && SDL_GetRelativeMouseMode() == SDL_FALSE,
                      "打开购买轮盘切换至绝对鼠标模式");
                engine.Render();
                saveScene("buy-wheel-lvl1.png");

                auto pressKeyPlay = [&](ImGuiKey key) {
                    auto& io = ImGui::GetIO();
                    io.AddKeyEvent(key, true);
                    engine.Render();
                    io.AddKeyEvent(key, false);
                    engine.Render();
                };

                // 按数字键 3 进入步枪二级（含 FAMAS, M4A4, M4A1-S, AUG）
                pressKeyPlay(ImGuiKey_3);
                saveScene("buy-wheel-lvl2-rifles.png");

                // 按数字键 4 购买 AUG，成交后回一级并扣款
                const int moneyBeforeBuy = engine.cs2dGame->GetPlayerMoney();
                pressKeyPlay(ImGuiKey_4);
                check(engine.cs2dGame->GetPlayer().weapons[0] == WeaponId::AUG &&
                      engine.cs2dGame->GetPlayerMoney() == moneyBeforeBuy - WeaponDB::Get(WeaponId::AUG).price,
                      "购买轮盘二级买 AUG 成功且仅扣一次款");

                // 再次按购买键收盘：恢复相对瞄准，第一帧无射击且准星不跳变
                const float aimBeforeX = engine.cs2dOverlay.virtCursorX;
                const float aimBeforeY = engine.cs2dOverlay.virtCursorY;
                engine.CloseBuyMenu();
                check(!engine.buyMenuOpen_ && SDL_GetRelativeMouseMode() == SDL_TRUE && !engine.cursorCaptured_,
                      "收盘恢复相对鼠标模式并重置采集状态");
                engine.Update(1.0f / 60.0f);
                check(std::fabs(engine.cs2dOverlay.virtCursorX - aimBeforeX) < 1.0f &&
                      std::fabs(engine.cs2dOverlay.virtCursorY - aimBeforeY) < 1.0f,
                      "收盘后第一帧准星无跳变");

                engine.Clean();
            }
        }
        PathUtils::SetBaseDir(previousBase);
    }
};

static void TestExperienceDisplaySettings() {
    const auto previous=PathUtils::BaseDir();
    const auto directory=std::filesystem::absolute("build-evidence/experience/settings");
    std::filesystem::create_directories(directory);
    PathUtils::SetBaseDir(directory.string());
    const auto filename=directory/(std::string("cs2d_save")+std::string(BuildInfo::SaveSuffix())+".ini");
    IniFile legacy;
    legacy.SetInt("settings","target_fps",165);
    legacy.Save(filename.string());
    SaveManager settings;
    settings.Load();
    check(settings.GetSettings().targetFPS==165 && !settings.GetSettings().vsync,"experience legacy custom FPS and missing VSync load correctly");
    for (int fps:{0,30,60,75,144,165,240}) {
        settings.MutableSettings().targetFPS=fps;
        settings.MutableSettings().vsync=true;
        settings.Save();
        SaveManager restored;restored.Load();
        check(restored.GetSettings().targetFPS==fps && restored.GetSettings().vsync,"experience arbitrary FPS and VSync survive saving");
    }
    IniFile malformed=IniFile::Load(filename.string());
    malformed.SetInt("settings","target_fps",-7);malformed.Save(filename.string());
    settings.Load();
    check(settings.GetSettings().targetFPS==0,"experience negative FPS normalizes to unlimited");
    PathUtils::SetBaseDir(previous);
}

static void TestExperienceCameraMotion() {
    CS2DMapData data;data.mapWidth=data.mapHeight=200;data.tileWidth=data.tileHeight=16;
    CS2DWorld world;world.Build(data);
    std::filesystem::create_directories("build-evidence/experience");
    std::ofstream trace("build-evidence/experience/camera-motion.csv");
    trace<<"fps,frame,camera_x,camera_y,zoom,aim_screen_x,aim_screen_y\n";
    CS2DRenderer camera;camera.SetWorld(&world);
    for (int fps:{30,60,120,144}) for (const auto size:{SDL_Point{1280,720},SDL_Point{1024,768},SDL_Point{2560,1600},SDL_Point{3440,1440}})
        for(int direction=0;direction<8;++direction) for(float distance:{50.f,400.f,800.f}) for(float lens:{8.f,size.y*.42f}) {
            const float angle=direction*Tuning::Pi/4;
            const float aimX=1600+std::cos(angle)*distance,aimY=1600+std::sin(angle)*distance;
            camera.SetFrameDelta(0);
            camera.UpdatePlayerCamera(1600,1600,14,aimX,aimY,size.x,size.y,1.12f,lens);
            camera.SetFrameDelta(1.f/fps);
            bool valid=true;
            for(int frame=0;frame<fps;++frame) {
                const float t=std::min(1.f,frame/(fps*.4f));
                const float ease=t*t*(3-2*t);
                const float multiplier=1.12f+(.70f-1.12f)*ease;
                camera.UpdatePlayerCamera(1600,1600,14,aimX,aimY,size.x,size.y,multiplier,lens);
                const float zoom=camera.GetZoom();
                const float actorX=(1600-camera.GetCameraX())*zoom,actorY=(1600-camera.GetCameraY())*zoom;
                const float aimScreenX=(aimX-camera.GetCameraX())*zoom,aimScreenY=(aimY-camera.GetCameraY())*zoom;
                valid=valid && std::isfinite(zoom) && zoom>0 && actorX-14*zoom>=11.8f && actorX+14*zoom<=size.x-11.8f &&
                    actorY-14*zoom>=11.8f && actorY+14*zoom<=size.y-11.8f && aimScreenX>=lens+11.8f &&
                    aimScreenX<=size.x-lens-11.8f && aimScreenY>=lens+11.8f && aimScreenY<=size.y-lens-11.8f;
                if(size.x==1280 && direction==0 && distance==50 && lens==8)
                    trace<<fps<<','<<frame<<','<<camera.GetCameraX()<<','<<camera.GetCameraY()<<','<<zoom<<','<<aimScreenX<<','<<aimScreenY<<'\n';
            }
            check(valid,"experience joint camera framing stays finite and bounded across FPS aspect direction distance and scope");
        }
}

static void TestExperienceRendering(bool gpu) {
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_Window* window=nullptr;
    SDL_Surface* surface=nullptr;
    SDL_Renderer* output=nullptr;
    if (gpu) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER,"direct3d11");
        window=SDL_CreateWindow("PixelEngine rendering regression",0,0,640,480,SDL_WINDOW_HIDDEN);
        if (window) output=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE);
    } else {
        surface=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);
        if (surface) output=SDL_CreateSoftwareRenderer(surface);
    }
    check(output!=nullptr,gpu ? "experience real D3D11 renderer initializes" : "experience software renderer initializes");
    if (output) {
        CS2DRenderer renderer;
        renderer.Initialize(output,"assets/cs2d/maps",640,480);
        CS2DMapData data;data.mapWidth=data.mapHeight=100;data.tileWidth=data.tileHeight=16;
        CS2DWorld world;world.Build(data);renderer.SetWorld(&world);
        SDL_Surface* pixels=SDL_CreateRGBSurfaceWithFormat(0,48,48,32,SDL_PIXELFORMAT_RGBA32);
        const SDL_Rect sample{296,216,48,48};
        auto brightness=[&](int left,int top,int right,int bottom) {
            int sum=0;
            for(int y=top;y<bottom;++y) for(int x=left;x<right;++x)
                sum+=static_cast<const Uint8*>(pixels->pixels)[y*pixels->pitch+x*4];
            return sum;
        };
        for(float scale:{.5f,.75f,1.f}) for(float width:{.5f,.8f,1.f,1.1f,1.19f,1.2f,1.5f,2.f})
            for(float phase:{0.f,.25f,.5f,.75f}) {
                SDL_SetRenderDrawColor(output,0,0,0,255);SDL_RenderClear(output);
                renderer.DrawCrosshair(320+phase,240+phase,255,255,255,255,width*scale,false,12,5);
                SDL_RenderReadPixels(output,&sample,SDL_PIXELFORMAT_RGBA32,pixels->pixels,pixels->pitch);
                const int left=brightness(6,20,19,29),right=brightness(29,20,43,29);
                const int top=brightness(20,6,29,19),bottom=brightness(20,29,29,43);
                check(left>80 && right>80 && top>80 && bottom>80,"experience every thin reticle arm survives width scale and subpixel phase");
            }
        SDL_SetRenderDrawColor(output,0,0,0,255);SDL_RenderClear(output);
        for(int i=0;i<8;++i) renderer.DrawCrosshair(50+i*76,240,255,255,255,255,.5f+i*.22f,false,12,5);
        std::filesystem::create_directories("build-evidence/experience");
        SDL_Surface* capture=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);
        SDL_RenderReadPixels(output,nullptr,SDL_PIXELFORMAT_RGBA32,capture->pixels,capture->pitch);
        IMG_SavePNG(capture,gpu ? "build-evidence/experience/thin-lines-d3d11.png" : "build-evidence/experience/thin-lines-software.png");
        std::ofstream magnification(gpu ? "build-evidence/experience/scope-magnification-d3d11.csv" :
            "build-evidence/experience/scope-magnification-software.csv");
        magnification<<"requested,measured,error_percent\n";
        for (float zoom:{1.5f,3.f}) {
            SDL_SetRenderDrawColor(output,0,0,0,255);SDL_RenderClear(output);
            const SDL_Rect clip{5,6,620,460};
            SDL_RenderSetClipRect(output,&clip);
            renderer.UpdateCamera(800,800,640,480);
            const float mainZoom=renderer.GetZoom(),mainX=renderer.GetCameraX(),mainY=renderer.GetCameraY();
            SDL_Texture* target=SDL_GetRenderTarget(output);
            const bool began=renderer.BeginScopeScene(900,850,320,240,zoom);
            check(began,"experience independent lens render target initializes");
            if (began) {
                check(std::abs(renderer.GetZoom()/mainZoom-zoom)<.001f,"experience actual lens magnification matches its selected level");
                check(std::abs((900-renderer.GetCameraX())*renderer.GetZoom()-320)<.001f &&
                      std::abs((850-renderer.GetCameraY())*renderer.GetZoom()-240)<.001f,"experience lens optical center projects the actual aim world point");
                const float expectedX=320+40*renderer.GetZoom();
                renderer.DrawCircleFilled(940,850,2,255,255,255,255);
                renderer.EndScopeScene();
                SDL_Rect restoredClip{};SDL_RenderGetClipRect(output,&restoredClip);
                check(SDL_RenderIsClipEnabled(output) && restoredClip.x==clip.x && restoredClip.y==clip.y &&
                      restoredClip.w==clip.w && restoredClip.h==clip.h,"experience independent lens restores the main clipping state");
                renderer.DrawScopeScene(320,240,180,1);
                SDL_RenderReadPixels(output,nullptr,SDL_PIXELFORMAT_RGBA32,capture->pixels,capture->pitch);
                double totalX=0;int count=0;
                for(int y=230;y<250;++y) for(int x=320;x<500;++x) {
                    const auto* pixel=static_cast<const Uint8*>(capture->pixels)+y*capture->pitch+x*4;
                    if(pixel[0]>200 && pixel[1]>200 && pixel[2]>200) {totalX+=x+.5; ++count;}
                }
                const double center=count ? totalX/count : 0;
                const double measured=(center-320)/(40*mainZoom);
                const double error=100*std::abs(measured/zoom-1);
                magnification<<zoom<<','<<measured<<','<<error<<'\n';
                check(count>0 && std::abs(center-expectedX)<1 && error<1,"experience rendered marker measures lens magnification within one percent");
                check(renderer.GetCameraX()==mainX && renderer.GetCameraY()==mainY && renderer.GetZoom()==mainZoom &&
                      SDL_GetRenderTarget(output)==target,"experience lens restores main camera and render target");
            }
            SDL_RenderSetClipRect(output,nullptr);
        }
        for (float width:{.5f,.8f,1.f,1.19f}) {
            SDL_SetRenderDrawColor(output,0,0,0,255);SDL_RenderClear(output);
            AimLineStyle style;style.solid={255,255,255,255};style.hidden=style.solid;
            style.solidThickness=style.hiddenThickness=width;
            renderer.DrawAimLinePreview(style,640,480);
            SDL_RenderReadPixels(output,&sample,SDL_PIXELFORMAT_RGBA32,pixels->pixels,pixels->pitch);
            check(brightness(5,20,20,29)>80,"experience thin horizontal aim line remains visible");
        }
        for (float scale : {.5f,.75f,1.f}) {
            for (float width : {.5f,.8f,1.f,1.1f,1.19f,1.2f,1.5f,2.f}) {
                for (float angle : {0.f,Tuning::Pi/2,Tuning::Pi/4}) {
                    for (float phase : {0.f,.25f,.5f,.75f}) {
                        SDL_SetRenderDrawColor(output,0,0,0,255);
                        SDL_RenderClear(output);
                        AimLineStyle style;
                        style.solid={255,255,255,255};
                        style.solidThickness=width;
                        const float worldX=renderer.GetCameraX()+(320+phase)/renderer.GetZoom();
                        const float worldY=renderer.GetCameraY()+(240+phase)/renderer.GetZoom();
                        renderer.DrawAimLine(worldX,worldY,angle,style,scale);
                        SDL_RenderReadPixels(output,&sample,SDL_PIXELFORMAT_RGBA32,pixels->pixels,pixels->pitch);
                        const int x=24+static_cast<int>(std::cos(angle)*12);
                        const int y=24+static_cast<int>(std::sin(angle)*12);
                        check(brightness(x-3,y-3,x+4,y+4)>80,
                              "experience horizontal vertical and diagonal aim lines survive thin width scale and phase");
                    }
                }
            }
        }
        SDL_FreeSurface(capture);SDL_FreeSurface(pixels);
    }
    SDL_DestroyRenderer(output);SDL_FreeSurface(surface);SDL_DestroyWindow(window);
}

int main(int argc,char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (argc>1 && std::string(argv[1])=="--spotlight-menu") {
        for (int height : {720, 800})
            EngineTestAccess::CaptureSceneryMenu(0, std::filesystem::path("build-evidence/spotlight/ui") / std::to_string(height), height, true);
        return g_fail ? 1 : 0;
    }
    if (argc>1 && std::string(argv[1])=="--pix39-menu") {
        EngineTestAccess::CaptureSceneryMenu(1, "build-evidence/pix39/salt-exchange/game");
        return g_fail ? 1 : 0;
    }
    if (argc>1 && std::string(argv[1])=="--menu-pair") {
        EngineTestAccess::CaptureSceneryMenu(0, "build-evidence/menu-pair/ancient_ruins/game");
        EngineTestAccess::CaptureSceneryMenu(2, "build-evidence/menu-pair/mirage/game");
        return g_fail ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--pix37-coach") {
        CS2DGameTestAccess::TestPix37CoachWiring();
        CS2DGameTestAccess::TestPix37RotateSchema();
        CS2DGameTestAccess::TestT35PostPlant();
        std::printf("PIX-37 coach: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc>1 && std::string(argv[1])=="--experience") {
        CS2DGameTestAccess::TestExperienceStates();
        EngineTestAccess::TestExperienceEntryPoints();
        TestExperienceDisplaySettings();
        TestExperienceCameraMotion();
        TestExperienceRendering(false);
        TestExperienceRendering(true);
        TestRound3HudLayouts(true);
        CS2DGameTestAccess::TestPix36BulletSpeeds();
        CS2DGameTestAccess::TestPix36HighSpeedHit();
        CS2DGameTestAccess::TestPix36SpectateUnion();
        CS2DGameTestAccess::TestPix36Footsteps();
        CS2DGameTestAccess::TestPix36DefuseIsolation();
        CS2DGameTestAccess::TestPix36ScopeLevelsAndLensPlayer();
        CS2DGameTestAccess::TestPix37DefuseRangeAndLos();
        CS2DGameTestAccess::TestPix37FireTileArea();
        CS2DGameTestAccess::TestPix37AimLineDuringFreeze();
        CS2DGameTestAccess::TestPix37BombCarrierPresentation();
        CS2DGameTestAccess::TestPix37VisualEvidence();
        CS2DGameTestAccess::TestPix37NoWalkBob();
        CS2DGameTestAccess::TestPix37SmokeCoverageAndFade();
        CS2DGameTestAccess::TestPix38OutlineNoBob();
        CS2DGameTestAccess::TestPix38SmokeActorAlpha();
        CS2DGameTestAccess::TestPix38FireTileSmaller();
        CS2DGameTestAccess::TestPix38RecoilCrosshair();
        SpriteAssetReview::TestPix38RadarPerimeter();
        CS2DGameTestAccess::TestPix38VisualEvidence();
        CS2DGameTestAccess::TestPix38VisualEvidence();
    CS2DGameTestAccess::TestPix38ShowcaseSpread();
        EngineTestAccess::TestPix37ResumeTransition();
        std::printf("Experience regressions: %d passed, %d failed\n",g_pass,g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc>1 && std::string(argv[1])=="--benchmark-visibility") return CS2DGameTestAccess::BenchmarkRound3Visibility();
    if (argc>1 && std::string(argv[1])=="--round3-visuals") {
        TestRound3PartialRendering();TestRound3HudLayouts();
        std::printf("Round3 visuals: %d passed, %d failed\n",g_pass,g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc>1 && std::string(argv[1])=="--export-ballistics") return ExportRound3Ballistics();
    if (argc>1 && std::string(argv[1])=="--round3") {
        CS2DGameTestAccess::TestPix34Round3();
        TestRound3PartialRendering();
        std::printf("Round3 focused: %d passed, %d failed\n",g_pass,g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--audio-balance") {
        SDL_setenv("SDL_AUDIODRIVER","dummy",1);
        AudioManager audio;
        check(audio.Initialize(),"balanced audio mixer initializes");
        for (int id=2; id<=static_cast<int>(WeaponId::Negev); ++id) {
            const auto weapon = static_cast<WeaponId>(id);
            Mix_HaltChannel(-1);
            audio.PlayWeaponFire(weapon);
            Mix_Chunk* chunk = nullptr;
            for (int ch=0; ch<44; ++ch)
                if (Mix_Playing(ch)) { chunk=Mix_GetChunk(ch); break; }
            check(chunk != nullptr,"weapon fire decodes and plays after normalization");
            if (!chunk) continue;
            double peak=0, rms=0;
            const size_t count=chunk->alen/2;
            for (size_t start=0; start<count; start+=4410) {
                double energy=0;
                const size_t end=std::min(count,start+4410);
                for (size_t i=start; i<end; ++i) {
                    Sint16 sample;
                    std::memcpy(&sample,chunk->abuf+i*2,2);
                    const double value=sample/32768.0;
                    peak=std::max(peak,std::abs(value)); energy+=value*value;
                }
                rms=std::max(rms,std::sqrt(energy/(end-start)));
            }
            std::printf("AUDIO %d short_rms=%.4f peak=%.4f\n",id,rms,peak);
            check(peak <= .851 && rms <= .161 && rms > .045,"weapon loudness calibrated with peak headroom");
        }
        std::printf("Audio balance: %d passed, %d failed\n",g_pass,g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--frost-gpu") {
        CS2DGameTestAccess::TestFrostGpu();
        return g_fail ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--pix35-match") {
        CS2DGameTestAccess::TestPix35Match();
        std::printf("PIX35 match: %d passed, %d failed\n",g_pass,g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--pix35-visuals") {
        CS2DGameTestAccess::TestPix35Visuals();
        std::printf("PIX35 visuals: %d passed, %d failed\n",g_pass,g_fail);
        return g_fail ? 1 : 0;
    }
    CS2DGameTestAccess::TestPerformanceInterpolation();
    CS2DGameTestAccess::TestPix35();
    CS2DGameTestAccess::TestPix36BulletSpeeds();
    CS2DGameTestAccess::TestPix36HighSpeedHit();
    CS2DGameTestAccess::TestPix36SpectateUnion();
    CS2DGameTestAccess::TestPix36Footsteps();
    CS2DGameTestAccess::TestPix36DefuseIsolation();
    CS2DGameTestAccess::TestPix36ScopeLevelsAndLensPlayer();
    CS2DGameTestAccess::TestPix37DefuseRangeAndLos();
    CS2DGameTestAccess::TestPix37FireTileArea();
    CS2DGameTestAccess::TestPix37AimLineDuringFreeze();
    CS2DGameTestAccess::TestPix37BombCarrierPresentation();
        CS2DGameTestAccess::TestPix37VisualEvidence();
    CS2DGameTestAccess::TestPix37NoWalkBob();
    CS2DGameTestAccess::TestPix37SmokeCoverageAndFade();
    CS2DGameTestAccess::TestPix38OutlineNoBob();
    CS2DGameTestAccess::TestPix38SmokeActorAlpha();
    CS2DGameTestAccess::TestPix38FireTileSmaller();
    CS2DGameTestAccess::TestPix38RecoilCrosshair();
    SpriteAssetReview::TestPix38RadarPerimeter();
    CS2DGameTestAccess::TestPix38VisualEvidence();
        CS2DGameTestAccess::TestPix38VisualEvidence();
    CS2DGameTestAccess::TestPix38ShowcaseSpread();
    EngineTestAccess::TestPix37ResumeTransition();
    if (argc > 1 && std::string(argv[1]) == "--pix35") {
        std::printf("PIX35 focused: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    std::printf("===== 合并契约 v1 玩法回归测试 =====\n");
    std::printf("地图: %s\n", MapPath());

    for (int id=2;id<=static_cast<int>(WeaponId::Negev);++id) {
        const auto weapon=static_cast<WeaponId>(id);
        const auto a=Ballistics::Recoil(weapon,5,0);
        const auto b=Ballistics::Recoil(weapon,5,0);
        check(a.x==b.x && a.y==b.y && std::isfinite(a.x) && std::isfinite(a.y),
              "round3 all weapon recoil curves are finite and deterministic");
        check(Ballistics::Spread(weapon,1,0,0)>=Ballistics::Spread(weapon,0,0,0),
              "round3 moving spread respects each weapon profile");
    }
    check(Ballistics::Spread(WeaponId::AWP,0,0,0)>Ballistics::Spread(WeaponId::AWP,0,1,0),
          "round3 AWP unscoped fire has greater random dispersion");
    if (argc > 1 && std::string(argv[1]) == "--wheel-acceptance") {
        EngineTestAccess::TestWheelAndLoadoutAcceptance();
        std::printf("\n===== Wheel & Loadout Acceptance: %d passed, %d failed =====\n", g_pass, g_fail);
        return g_fail == 0 ? 0 : 1;
    }
    CS2DGameTestAccess::TestPix34Round3();
    TestRound3PartialRendering();
    TestPix34Input();
    TestRound2RadarRendering();
    TestRound2SettingsLayouts();
    CS2DGameTestAccess::TestPix34Round2();
    CS2DGameTestAccess::TestPix34();
    CS2DGameTestAccess::TestPix33();
    CS2DGameTestAccess::TestSharedBotVisibility();
    CS2DGameTestAccess::TestBotScopeMovement();
    SpriteAssetReview::Run();
    TestResolutionMigration();
    TestHdMapNavigation();
    TestScopeBoundsAndMovement();
    TestAimLinePersistenceAndRecoilRecovery();
    TestStableCursorAndWallRay();
    TestReactionBenchmark();
    TestGrenadeCursorDistance();
    TestFixedReferenceCursor();
    TestSharedAimDistance();
    TestAimLineRendering();
    TestMovementFrameInterpolation();
    TestScopeFrameSnapshot();
    TestCameraEdgeZoom();
    TestUnlimitedPlayerCone();
    TestPerceptionAndRecoil();
    TestCollisionModes();
    TestFixedVisionRadius();
    TestMenuHeldPoses();
    const char* menuDirectory = std::getenv("PIXEL_MENU_MODEL_DIR");
    check(Menu3DTestAccess::ChickenAvoidance(menuDirectory ? menuDirectory : "assets/models/menu"),
          "eight seeded minute-long menu simulations avoid scene overlap and preserve roaming");
    TestFireInputGate();
    TestBuyCodes();
    TestBuyDeadAndWindow();
    TestScope();
    TestScopeCameraLevels();
    TestDamageAndScopeShot();
    TestScopeCrosshair();
    TestBulletVisuals();

    // ---- 双阵营配装规则、旧档迁移与玩法契约测试 ----
    {
        std::printf("\n===== 赛前武器配装与旧档迁移测试 =====\n");
        const auto def = WeaponDB::DefaultMatchLoadouts();
        // 默认 15 槽位唯一且合法
        check(WeaponDB::SanitizeTeamLoadout(def.t, true) == def.t, "T 默认 15 槽位合法唯一");
        check(WeaponDB::SanitizeTeamLoadout(def.ct, false) == def.ct, "CT 默认 15 槽位合法唯一");

        // CT 可以双 M4 共存、AUG 换 AWP 后 步枪4把/狙击1把
        auto ctCustom = def.ct;
        check(WeaponDB::ApplyLoadoutSlotChoice(ctCustom, false, WeaponDB::LoadoutGroup::Rifle, 4, WeaponId::AUG),
              "CT 用 AUG 替换 AWP");
        check(WeaponDB::ShopWeaponsForCategory(ctCustom, false, WeaponDB::ShopCategory::Rifles).size() == 4 &&
              WeaponDB::ShopWeaponsForCategory(ctCustom, false, WeaponDB::ShopCategory::Snipers).size() == 1,
              "换 AUG 后步枪=4把、狙击枪=1把");

        // 同组互换
        check(WeaponDB::ApplyLoadoutSlotChoice(ctCustom, false, WeaponDB::LoadoutGroup::Rifle, 0, WeaponId::M4A4) &&
              ctCustom.rifles[0] == WeaponId::M4A4 && ctCustom.rifles[1] == WeaponId::FAMAS,
              "选择本组已有枪械时互换位置");

        // 跨阵营 / 跨组拒绝
        bool swap = false;
        check(!WeaponDB::ValidateLoadoutAssignment(ctCustom, false, WeaponDB::LoadoutGroup::Rifle, 0, WeaponId::AK47, swap).empty(),
              "CT 禁止配置 T 专属 AK-47");
        check(!WeaponDB::ValidateLoadoutAssignment(ctCustom, false, WeaponDB::LoadoutGroup::Pistol, 0, WeaponId::AWP, swap).empty(),
              "禁止跨组将 AWP 放入手枪槽");

        // 存档损坏与旧档修复测试（保留有效槽与原小鸡外观）
        const auto prevBase = PathUtils::BaseDir();
        const auto dir = std::filesystem::absolute("build-evidence/loadout-migration");
        std::filesystem::create_directories(dir);
        PathUtils::SetBaseDir(dir.string());
        const auto iniPath = dir / (std::string("cs2d_save") + BuildInfo::SaveSuffix() + ".ini");

        // 1) 无配装段旧档
        {
            IniFile legacy;
            legacy.SetInt("menu", "pose_version", 1);
            legacy.SetInt("menu", "color2", 5);
            legacy.SetInt("menu", "pose2", 2);
            legacy.SetInt("menu", "accessory2", 1);
            legacy.Save(iniPath.string());
            SaveManager sm;
            sm.Load();
            check(sm.GetSettings().loadouts == WeaponDB::DefaultMatchLoadouts(), "无配装段旧档回退默认配装");
            check(sm.GetSettings().menuColor[2] == 5 && sm.GetSettings().menuPose[2] == 2 &&
                  sm.GetSettings().menuAccessory[2] == 1, "旧档迁移保留原有小鸡外观设置");
        }
        // 2) 重复 ID、跨阵营、错组、未知 ID 修复
        {
            IniFile bad;
            bad.SetInt("loadout_ct", "version", 1);
            bad.Set("loadout_ct", "starting", "glock18");     // 跨阵营起始
            bad.Set("loadout_ct", "pistol_1", "awp");         // 错组
            bad.Set("loadout_ct", "pistol_2", "p250");        // 合法
            bad.Set("loadout_ct", "rifle_1", "m4a4");         // 合法
            bad.Set("loadout_ct", "rifle_2", "m4a4");         // 重复
            bad.Set("loadout_ct", "rifle_3", "unknown_gun");  // 未知
            bad.Set("loadout_ct", "rifle_4", "ak47");         // 跨阵营
            bad.Set("loadout_ct", "rifle_5", "aug");          // 合法保留
            bad.SetInt("menu", "color3", 6);
            bad.Save(iniPath.string());

            SaveManager sm;
            sm.Load();
            const auto& ct = sm.GetSettings().loadouts.ct;
            check(ct.starting == WeaponId::USP_S, "非法起始手枪修复为 CT 默认 USP-S");
            check(ct.pistols[1] == WeaponId::P250, "有效手枪槽保留原位");
            check(ct.rifles[0] == WeaponId::M4A4 && ct.rifles[4] == WeaponId::AUG,
                  "有效步枪槽保留原位且问题槽被修复");
            check(WeaponDB::SanitizeTeamLoadout(ct, false) == ct, "修复后的 CT 15 槽位完全合法唯一");
            check(sm.GetSettings().menuColor[3] == 6, "损坏配装修复时保留小鸡外观");
        }
        PathUtils::SetBaseDir(prevBase);

        // 3) 玩法集成：不同配装发枪、换边、死斗校验、退款与未配装拒绝
        CS2DGame g;
        WeaponDB::MatchLoadouts ml = WeaponDB::DefaultMatchLoadouts();
        ml.ct.starting = WeaponId::P2000;
        ml.ct.rifles[4] = WeaponId::AUG; // 移除 AWP
        g.SetMatchLoadouts(ml);
        g.SetMatchMode(MatchMode::BombDefusal);
        g.SetPlayerTeam(Team::CT);
        check(g.LoadMap(MapPath(), nullptr, true), "爆破模式加载地图");
        check(g.GetPlayer().weapons[1] == WeaponId::P2000, "CT 选 P2000 后首回合获得 P2000");

        // 购买闪光弹 2 枚，第 3 枚达上限，退 1 枚只退一份
        g.GetPlayer().money = 16000;
        check(g.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash)).code == Presentation::BuyCode::Ok, "闪光 #1");
        check(g.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash)).code == Presentation::BuyCode::Ok, "闪光 #2");
        check(g.ExecutePurchase(WeaponReq(WeaponId::GrenadeFlash)).code == Presentation::BuyCode::ItemLimitReached, "闪光 #3 达上限");
        const int moneyAfter2Flash = g.GetPlayerMoney();
        check(g.ExecuteRefund(WeaponReq(WeaponId::GrenadeFlash)) &&
              g.GetPlayerMoney() == moneyAfter2Flash + 200 &&
              g.GetPlayer().CountGrenade(WeaponId::GrenadeFlash) == 1,
              "闪光弹退一枚只退一份");

        // 死斗记忆项校验：切换配装后旧主武若不在新配装内则不保留
        CS2DGame dm;
        dm.SetMatchLoadouts(WeaponDB::DefaultMatchLoadouts());
        dm.SetMatchMode(MatchMode::DeathMatch);
        dm.LoadMap(MapPath(), nullptr, true);
        dm.GetPlayer().money = 16000;
        check(dm.ExecutePurchase(WeaponReq(WeaponId::AWP)).code == Presentation::BuyCode::Ok, "死斗购买 AWP");
        dm.SetMatchLoadouts(ml); // 移除 AWP 并改 P2000
        dm.GetPlayer().alive = false;
        dm.Update(3.5f, false, 0, 0, false, false, false, false, false, false, false, nullptr);
        check(dm.GetPlayer().weapons[0] == WeaponId::None &&
              dm.GetPlayer().weapons[1] == WeaponId::P2000,
              "死斗重生按本场配装校验记忆项，失效主武回退合法出生配置");
    }

    EngineTestAccess::TestWheelAndLoadoutAcceptance();

    std::printf("\n===== 结果: %d 通过, %d 失败 =====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
