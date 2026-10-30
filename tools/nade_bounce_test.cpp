// 投掷物撞墙反弹自检：验证「分轴反弹、切向速度保留、动能损失、低速停止」。
// 纯数学判定，不启动引擎。
#include <cstdio>
#include <cmath>
#include "CS2D/NadePhysics.hpp"
#include "CS2D/Tuning.hpp"

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++g_fail;
}

int main() {
    const float rest = Tuning::NadeBounceRestitution;
    const float vmin = Tuning::NadeMinBounceSpeed;

    // ---- 1. 参数本身 ----
    check(rest > 0.0f && rest < 1.0f, "恢复系数在 (0,1)：会反弹且损失动能");
    check(vmin > 0.0f, "低速停止阈值为正");

    // ---- 2. 正面撞竖直墙：只反转 vx ----
    {
        float vx = 400.0f, vy = 0.0f;
        const bool hit = ApplyNadeBounce(vx, vy, true, false, rest, vmin);
        check(hit, "命中 X 轴墙：返回 true");
        check(vx < 0.0f, "命中 X 轴墙：vx 反向");
        check(std::abs(std::abs(vx) - 400.0f * rest) < 0.01f,
              "命中 X 轴墙：|vx| = |v0| × 恢复系数");
        check(vy == 0.0f, "命中 X 轴墙：vy 不变");
    }

    // ---- 3. 正面撞水平墙：只反转 vy ----
    {
        float vx = 0.0f, vy = 300.0f;
        ApplyNadeBounce(vx, vy, false, true, rest, vmin);
        check(vy < 0.0f && std::abs(std::abs(vy) - 300.0f * rest) < 0.01f,
              "命中 Y 轴墙：vy 反向并衰减");
        check(vx == 0.0f, "命中 Y 轴墙：vx 不变");
    }

    // ---- 4. 斜撞竖直墙：切向速度保留（沿墙滑行）----
    {
        float vx = 300.0f, vy = 150.0f;
        ApplyNadeBounce(vx, vy, true, false, rest, vmin);
        check(vx < 0.0f, "斜撞竖直墙：vx 反向");
        check(std::abs(vy - 150.0f) < 0.0001f, "斜撞竖直墙：切向 vy 完全保留");
    }

    // ---- 5. 撞墙角：两个分量都反向 ----
    {
        float vx = 200.0f, vy = 200.0f;
        ApplyNadeBounce(vx, vy, true, true, rest, vmin);
        check(vx < 0.0f && vy < 0.0f, "撞墙角：vx / vy 同时反向");
    }

    // ---- 6. 动能损失：v² 必须下降 ----
    {
        float vx = 400.0f, vy = 0.0f;
        const float e0 = vx * vx + vy * vy;
        ApplyNadeBounce(vx, vy, true, false, rest, vmin);
        const float e1 = vx * vx + vy * vy;
        check(e1 < e0, "反弹后动能下降");
        check(std::abs(e1 - e0 * rest * rest) < 0.5f,
              "动能按恢复系数的平方衰减");
        std::printf("        动能 %.0f → %.0f（保留 %.0f%%）\n",
                    e0, e1, e1 / e0 * 100.0f);
    }

    // ---- 7. 连续反弹逐次衰减 ----
    {
        float vx = 420.0f, vy = 0.0f;
        float prev = vx * vx;
        int bounces = 0;
        for (int i = 0; i < 5; ++i) {
            ApplyNadeBounce(vx, vy, true, false, rest, vmin);
            const float e = vx * vx + vy * vy;
            if (e <= 0.0f) break;
            check(e < prev, "连续反弹：每次动能都更小");
            prev = e;
            ++bounces;
        }
        check(bounces >= 2, "至少能连续反弹两次");
        std::printf("        连续反弹 %d 次后速度 = %.1f\n", bounces, std::abs(vx));
    }

    // ---- 8. 反弹后速度过低 → 直接停下 ----
    {
        float vx = 30.0f, vy = 0.0f;
        ApplyNadeBounce(vx, vy, true, false, rest, vmin);
        check(vx == 0.0f && vy == 0.0f, "反弹后低于阈值：完全停下（不贴墙抖动）");
    }

    // ---- 9. 没有命中时不动 ----
    {
        float vx = 123.0f, vy = 45.0f;
        const bool hit = ApplyNadeBounce(vx, vy, false, false, rest, vmin);
        check(!hit && vx == 123.0f && vy == 45.0f, "未命中：速度保持不变");
    }

    std::printf("\n%s：%d 项失败\n", g_fail == 0 ? "全部通过" : "存在失败", g_fail);
    return g_fail == 0 ? 0 : 1;
}
