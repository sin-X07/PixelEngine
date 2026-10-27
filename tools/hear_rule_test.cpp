// 听声扑人规则自检：验证「下包声→CT 优先扑安装者」「拆包声→T 优先扑拆包者」
// 以及两个可听半径、命中率加成的数值关系。
//
// 这些是纯数值/布尔判定，可在不启动引擎的情况下验证。
#include <cstdio>
#include <cmath>
#include "CS2D/Tuning.hpp"
#include "CS2D/Weapons/WeaponDef.hpp"

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++g_fail;
}

// 复刻 BOT 决策树里的听声判定（与 CS2DGameBots.cpp 保持一致）
static bool BotHearsSound(int botTeam, bool isPlant, float dist,
                          float plantR, float defuseR) {
    const bool hearThis = (isPlant && botTeam == /*CT*/ 1)
                       || (!isPlant && botTeam == /*T*/ 0);
    if (!hearThis) return false;
    const float hearR = isPlant ? plantR : defuseR;
    return dist <= hearR;
}

static float SpreadMulFor(bool heard, bool isPlant,
                          float base, float plantMul, float defuseMul) {
    return heard ? base * (isPlant ? plantMul : defuseMul) : base;
}

int main() {
    constexpr int T = 0, CT = 1;

    // ---- 1. 阵营方向：只有对方能听到 ----
    check(BotHearsSound(CT, true,  100.0f, Tuning::PlantHearRadius,
                        Tuning::DefuseHearRadius),
          "T 下包 → CT 能听到");
    check(!BotHearsSound(T, true, 100.0f, Tuning::PlantHearRadius,
                         Tuning::DefuseHearRadius),
          "T 下包 → T 自己听不到（不互相残杀）");
    check(BotHearsSound(T, false, 100.0f, Tuning::PlantHearRadius,
                        Tuning::DefuseHearRadius),
          "CT 拆包 → T 能听到");
    check(!BotHearsSound(CT, false, 100.0f, Tuning::PlantHearRadius,
                         Tuning::DefuseHearRadius),
          "CT 拆包 → CT 自己听不到");

    // ---- 2. 可听半径边界 ----
    check(BotHearsSound(CT, true, Tuning::PlantHearRadius,
                        Tuning::PlantHearRadius, Tuning::DefuseHearRadius),
          "下包声：恰好在半径上仍能听到");
    check(!BotHearsSound(CT, true, Tuning::PlantHearRadius + 0.01f,
                         Tuning::PlantHearRadius, Tuning::DefuseHearRadius),
          "下包声：超出半径听不到");
    check(BotHearsSound(T, false, Tuning::DefuseHearRadius,
                        Tuning::PlantHearRadius, Tuning::DefuseHearRadius),
          "拆包声：恰好在半径上仍能听到");
    check(!BotHearsSound(T, false, Tuning::DefuseHearRadius + 0.01f,
                         Tuning::PlantHearRadius, Tuning::DefuseHearRadius),
          "拆包声：超出半径听不到");

    // ---- 3. 听觉半径必须大于视野半径（"听见但还看不见"）----
    check(Tuning::PlantHearRadius  > Tuning::VisionRadius,
          "下包可听半径 > BOT 视野半径");
    check(Tuning::DefuseHearRadius > Tuning::VisionRadius,
          "拆包可听半径 > BOT 视野半径");

    // ---- 4. 命中率加成：散布乘数必须 <1（更准），且不能激进到 0 ----
    check(Tuning::PlantHearSpreadMul  < 1.0f, "听下包：散布乘数 < 1（更准）");
    check(Tuning::DefuseHearSpreadMul < 1.0f, "听拆包：散布乘数 < 1（更准）");
    check(Tuning::PlantHearSpreadMul  > 0.3f, "听下包：散布仍有下限（不至于百发百中）");
    check(Tuning::DefuseHearSpreadMul > 0.3f, "听拆包：散布仍有下限");

    // ---- 5. 加成生效后散布确实更小 ----
    {
        const float base = 0.8f;                       // 非精确射击的基准散布
        const float normal  = SpreadMulFor(false, false, base,
                                           Tuning::PlantHearSpreadMul,
                                           Tuning::DefuseHearSpreadMul);
        const float plant   = SpreadMulFor(true, true, base,
                                           Tuning::PlantHearSpreadMul,
                                           Tuning::DefuseHearSpreadMul);
        const float defuse  = SpreadMulFor(true, false, base,
                                           Tuning::PlantHearSpreadMul,
                                           Tuning::DefuseHearSpreadMul);
        check(normal == base, "未听到声音：散布 = 基准");
        check(plant  < normal, "听到下包：散布小于基准");
        check(defuse < normal, "听到拆包：散布小于基准");
        std::printf("        基准散布=%.3f 听下包=%.3f 听拆包=%.3f\n",
                    normal, plant, defuse);
    }

    // ---- 6. 半径换算成"地图尺度"的合理性 ----
    // mirage tile 8px，可听半径 300px ≈ 37 tile，跨越多条走廊，符合"听到远处动作"
    check(Tuning::PlantHearRadius / 8.0f > 20.0f,
          "300px ≈ 37 tile，确实能覆盖多条走廊");

    std::printf("\n%s：%d 项失败\n", g_fail == 0 ? "全部通过" : "存在失败", g_fail);
    return g_fail == 0 ? 0 : 1;
}
