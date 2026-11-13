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
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#include "CS2D/CS2DGame.hpp"
#include "CS2D/CS2DRenderer.hpp"
#include "CS2D/CS2DOverlay.hpp"
#include "Storage/SaveManager.hpp"
#include "CS2D/CS2DWorld.hpp"
#include "Core/PathUtils.hpp"
#include "Core/PresentationContract.hpp"
#include "Core/BuildInfo.hpp"
#include "dev_replay/replay.hpp"
#include "Input/FireInputGate.hpp"
#include "Graphics/Menu3DModel.hpp"
#include "Graphics/Menu3D.hpp"

struct Menu3DTestAccess {
    static bool ChickenAvoidance(const std::string& directory) {
        Menu3D menu;
        if (!LoadCharacter(menu, 0, directory + "/t.glb") ||
            !LoadCharacter(menu, 1, directory + "/ct.glb")) return false;
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
                if (frame % 300 == 0) {
                    for (int actor = 0; actor < 2; ++actor) {
                        menu.FocusSelect(actor, 0, (frame / 300 + actor) % 6);
                        menu.FocusSelect(actor, 2, (frame / 300) % 4);
                        menu.FocusSelect(actor, 3, (frame / 300) % 4);
                    }
                }
                if (frame % 6 == 0) {
                    menu.tModel_->Update(dt * 6);
                    menu.ctModel_->Update(dt * 6);
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
    static Menu3DModel* LoadCharacter(Menu3D& menu, int actor, const std::string& path) {
        auto model = std::make_unique<Menu3DModel>();
        if (!model->Load(path, "Menu_Idle_AK")) return nullptr;
        auto& target = actor == 0 ? menu.tModel_ : menu.ctModel_;
        target = std::move(model);
        return target.get();
    }
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

    std::printf("\n===== 修复项回归（连发开镜 / 换弹中开镜）=====\n");
    {
        // 可连发的开镜武器（AUG / SG 553 / SCAR-20 / G3SG1）：必须能**开镜连射**，
        // 每打一发不自动退镜；「延迟关镜」对它们没有意义（本来就不关）。
        CS2DGame game;
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
        check(teamCam.IsSpectating(), "团队模式阵亡：进入观察者");
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
    check(std::fabs(reference - 189.8f) < 0.1f, "PIX-20 preserves reference world radius");
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

static void TestMenuWeaponMountSelection() {
    const char* poses[] = { "Idle", "Crouch", "HandsOnHips", "FistPalm" };
    const char* weapons[] = { "AK", "SMG", "Sniper", "Shotgun", "Pistol", "Knife_1" };
    for (int actor = 0; actor < 2; ++actor) {
        Menu3D menu;
        Menu3DModel* model = Menu3DTestAccess::LoadCharacter(menu, actor, MenuAssetPath(actor == 0 ? "t" : "ct"));
        bool valid = model != nullptr;
        if (!model) {
            check(false, "menu mount-selection model loads");
            continue;
        }
        for (int pose = 0; pose < 4; ++pose) {
            menu.FocusSelect(actor, 2, pose);
            for (int weapon = 0; weapon < 6; ++weapon) {
                menu.FocusSelect(actor, 0, weapon);
                for (int accessory : { 3, 0, 1, 2, 3, 0 }) {
                    menu.FocusSelect(actor, 3, accessory);
                    for (int p = 0; p < 4; ++p) {
                        for (int w = 0; w < 6; ++w) {
                            const std::string node = std::string("weapon_") + poses[p] + "_" + weapons[w];
                            const bool selected = p == pose && w == weapon;
                            const bool packMount = accessory == 3 && model->HasNode(node + "_Backpack");
                            valid = valid && model->GetNodeVisible(node) == (selected && !packMount);
                            valid = valid && model->GetNodeVisible(node + "_Backpack") == (selected && packMount);
                        }
                    }
                }
            }
        }
        check(valid, "normal menu selectors keep exactly one compatible weapon mount visible");
    }
}

// cover / center 两种碰撞栅格化模式（地图属性 collisionMode）
static void TestCollisionModes() {
    CS2DMapData data;
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
    check(x == 255 && y == 195, "scoped cursor accepts relative motion");
    overlay.SetVisibleAimPoint(x, y);
    overlay.SyncScopeCursor(true, true, false);
    overlay.UpdateCursor(0, 0, 1, 4, -30, 80, 1920, 1080, 200, 200, x, y);
    check(std::abs(x - 255) < 0.001f && std::abs(y - 195) < 0.001f,
          "scope closing preserves the last map pixel");
    overlay.SyncScopeCursor(false, true, false);
    overlay.UpdateCursor(0, 0, 1, 6, 0, 0, 1920, 1080, 200, 200, x, y);
    check(std::abs(x - 255) < 0.001f && std::abs(y - 195) < 0.001f, "zoom changes preserve anchored world point");
    overlay.UpdateCursor(0, 0, 1, 4, -80, -90, 1920, 1080, 200, 200, x, y);
    check(std::abs(x - 255) < 0.001f && std::abs(y - 195) < 0.001f, "camera changes preserve anchored world point");
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
    check(x == 255 && y == 205, "player motion preserves relative cursor offset");

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
            check(player.aimKick < previousKick, "continuous shots rotate rightward aim toward twelve oclock");
            previousKick = player.aimKick;
        }
    }
    const float heldKick = player.aimKick;
    for (int tick = 0; tick < 60; ++tick)
        game.Update(1.0f / 60.0f, false, player.x + 100, player.y,
                    false, false, false, false, false, false, false, nullptr);
    check(heldKick < 0 && std::abs(player.aimKick) < std::abs(heldKick) * 0.02f, "recoil returns after firing stops");
}

static void TestReactionBenchmark() {
    for (int difficulty = 0; difficulty < 4; ++difficulty) {
        float total = 0.0f;
        int samples = 0;
        for (int seed = 1; seed <= 60; ++seed) {
            CS2DGame game;
            game.SetRngSeed(seed);
            if (!LoadDeathMatch(game)) return;
            game.GetBOTs().resize(1);
            auto& bot = game.GetBOTs()[0];
            auto& player = game.GetPlayer();
            bot.x = player.x + 0.1f;
            bot.y = player.y;
            bot.aimAngle = 3.14159265f;
            bot.shootAnimTimer = 0.0f;
            bot.shootCooldown = 0.0f;
            player.health = bot.health = 100000;
            game.SetBotProfile(0, difficulty * 2);
            game.ResetBotAi(0);
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
    for (int mode = 0; mode < 4; ++mode) {
        for (int fps : {60, 120, 144}) {
            for (const auto direction : std::vector<std::pair<int, int>>{{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}}) {
                CS2DOverlay reference, animated;
                reference.InitializeCursor(0.0f);
                animated.InitializeCursor(0.0f);
                const float modeZoom = mode == 1 ? 1.5f : mode == 2 ? 2.0f : 1.0f;
                const float shift = CS2DGame::ScopeCameraShiftForZoom(modeZoom);
                reference.SetReferenceMode(modeZoom, shift, mode == 3);
                animated.SetReferenceMode(modeZoom, shift, mode == 3);
                bool same = true;
                int entries = 0, exits = 0;
                bool previous = false;
                for (int frame = 0; frame < fps * 4; ++frame) {
                    int motion = 0;
                    if (frame < fps) motion = 2880 / fps;
                    if (frame >= fps * 2 && frame < fps * 3) motion = -static_cast<int>(1440 * modeZoom) / fps;
                    const float time = static_cast<float>(frame) / fps;
                    const float zoom = 6.0f * (0.85f + 0.15f * std::cos(time * 5.0f));
                    float rx = 0, ry = 0, ax = 0, ay = 0;
                    reference.UpdateCursor(motion * direction.first, motion * direction.second, 1, 6,
                        0, 0, 1920, 1080, 500, 500, rx, ry);
                    animated.UpdateCursor(motion * direction.first, motion * direction.second, 1, zoom,
                        150 * std::sin(time), -80 * std::cos(time), 1920, 1080, 500, 500, ax, ay);
                    same = same && std::abs(rx - ax) < 0.001f && std::abs(ry - ay) < 0.001f &&
                        reference.EdgeZoomRequested() == animated.EdgeZoomRequested();
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
        border.UpdateCursor(-side.first * 80, -side.second * 80, 1, 4.2f, 0, 0,
            1920, 1080, playerX, playerY, x, y);
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
    check(std::abs(cursor.virtCursorX - 1100) < 0.01f, "handoff anchor accepts relative mouse movement");
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
    for (bool angled : {false, true}) {
        for (const auto player : std::vector<std::pair<float, float>>{{440,440}, {20,20}, {860,860}}) {
            for (const auto direction : std::vector<std::pair<int, int>>{{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}}) {
                float baselineX = 0, baselineY = 0;
                bool same = true;
                for (float zoom : {1.0f, 1.5f, 2.0f}) {
                    CS2DOverlay cursor;
                    cursor.InitializeCursor(std::atan2(static_cast<float>(direction.second), static_cast<float>(direction.first)));
                    cursor.SetReferenceWorldBounds(880, 880);
                    cursor.SetReferenceMode(zoom, CS2DGame::ScopeCameraShiftForZoom(zoom), zoom == 1.0f && angled, angled);
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
        renderer.DrawAimLine(20, 40, 7, 0, 0, 140, 40, line);
        SDL_RenderPresent(software);
        check(pixel(120,80).r >= 99 && pixel(120,80).r <= 101 && pixel(120,80).b == 0,
              "solid segment applies its customized color and alpha");
        check(pixel(163,80).b == 240 && pixel(170,80).b == 0,
              "wall-to-reticle section applies hidden color with a dashed gap");
        check(pixel(120,82).r > 0 && pixel(120,84).r == 0 && pixel(163,82).b == 0,
              "solid and hidden segments use independently customized pixel widths");
        clear();
        renderer.DrawAimLine(20, 40, 7, 0, 0, 60, 40, line);
        SDL_RenderPresent(software);
        check(pixel(45,80).r > 0 && pixel(150,80).r > 0 && pixel(163,80).b > 0,
              "ray starts at actor and continues past the reticle to the next wall");
        line.solid.a = line.hidden.a = 0;
        clear();
        renderer.DrawAimLine(20, 40, 7, 0, 0, 140, 40, line);
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
            game.GetBOTs().clear();
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
                const float shotSpread = player.CurrentWeapon().spreadBase *
                    (1.0f + game.GetCrosshairBloom() * (Tuning::MoveSpreadMaxMul - 1.0f));
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
            for (int cameraMode = 0; cameraMode < 2; ++cameraMode) {
                if (cameraMode > 0) game.ToggleCameraMode();
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
                        cursor.SetReferenceMode(scope.targetZoomMultiplier, shift, scope.level == 0 && game.AngledCameraEnabled(), game.AngledCameraEnabled());
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
                        game.SetEdgeZoomRequested(cursor.EdgeZoomRequested());
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
            for (int frame = 0; frame < 180; ++frame) {
                cursor.UpdateCursor(0, 0, 1, renderer.GetZoom(), renderer.GetCameraX(), renderer.GetCameraY(),
                    1920, 1080, x, y, targetX, targetY);
                game.SetEdgeZoomRequested(cursor.EdgeZoomRequested());
                game.Update(1.0f / 60.0f, false, targetX, targetY, false, false, false, false, false, false, false, nullptr);
                game.Render(1920, 1080);
                if (frame >= 30) stable = stable && std::fabs(renderer.GetZoom() / baseZoom - Tuning::EdgeZoomOutMin) < 0.001f;
            }
            check(stable, "stationary edge cursor stays zoomed out for three seconds without oscillation");
            check(std::fabs(renderer.GetZoom() / baseZoom - Tuning::EdgeZoomOutMin) < 0.001f, "edge zoom reaches 0.7 within half a second");
            check(std::fabs(renderer.GetVisionRadius() * Tuning::EdgeZoomOutMin - baseRadius) < 0.01f, "edge zoom and fog share final multiplier");
            const float zoom = renderer.GetZoom();
            game.Render(1920, 1080);
            check(renderer.GetZoom() == zoom, "repeated Render preserves edge transition state");
            SDL_RenderPresent(software);
            SDL_SaveBMP(surface, "build-evidence/pix31-render.bmp");
            cursor.UpdateCursor(-1000, 0, 1, renderer.GetZoom(), renderer.GetCameraX(), renderer.GetCameraY(),
                1920, 1080, x, y, targetX, targetY);
            game.SetEdgeZoomRequested(cursor.EdgeZoomRequested());
            for (int frame = 0; frame < 25; ++frame) {
                game.Update(1.0f / 60.0f, false,
                    renderer.GetCameraX() + 960.0f / renderer.GetZoom(),
                    renderer.GetCameraY() + 540.0f / renderer.GetZoom(),
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
        renderer.SetScopeMask(false,0,0,0);
        const int normal = renderer.EffectiveFogAlpha();
        renderer.SetScopeMask(true,50,50,20);
        check(normal == renderer.EffectiveFogAlpha(), "scope and unscoped fog use identical configured opacity");
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

int main() {
    std::printf("===== 合并契约 v1 玩法回归测试 =====\n");
    std::printf("地图: %s\n", MapPath());

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
    TestMenuWeaponMountSelection();
    const char* menuDirectory = std::getenv("PIXEL_MENU_MODEL_DIR");
    check(Menu3DTestAccess::ChickenAvoidance(menuDirectory ? menuDirectory : "assets/models/menu"),
          "eight seeded minute-long menu simulations avoid scene overlap and preserve roaming");
    TestFireInputGate();
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
