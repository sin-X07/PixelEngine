#undef NDEBUG // Keep regression assertions active in Release builds.
#include "imgui.h"
#define RADIAL_WHEEL_LOGIC_ONLY
#include "../PixelEngine/Graphics/RadialWheel.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

bool frame(bool down, int* address, bool stable) {
 auto& io=ImGui::GetIO();io.MousePos=ImVec2(40,40);io.MouseDown[0]=down;
 ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(400,300));
 ImGui::Begin("test",nullptr,ImGuiWindowFlags_NoTitleBar);ImGui::SetCursorScreenPos(ImVec2(20,20));
 if(stable){ImGui::PushID(0);ImGui::PushID(7);}else ImGui::PushID(address);
 bool click=ImGui::InvisibleButton("##card",ImVec2(100,70));ImGui::PopID();if(stable)ImGui::PopID();
 ImGui::End();ImGui::Render();return click;
}
bool run(bool stable){ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(640,480);io.DeltaTime=1.f/60;unsigned char* p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);int a=1,b=2;frame(false,&a,stable);frame(false,&a,stable);frame(true,&a,stable);bool clicked=frame(false,&b,stable);ImGui::DestroyContext();return clicked;}

static void TestRadialWheelGeometryAndInput() {
    using namespace RadialWheel;

    // 1) 覆盖 3, 4, 5, 6, 7 个扇区的起止角、12点首扇区、顺时针排序、中心30%死区与间隙
    for (int n : {3, 4, 5, 6, 7}) {
        Layout lay;
        lay.centerX = 500.0f;
        lay.centerY = 400.0f;
        lay.outerRadius = 200.0f;
        lay.innerRadius = lay.outerRadius * kDefaultInnerRatio; // 60px
        lay.sectorCount = n;
        lay.gapHalfAngle = (n >= 7) ? 0.016f : 0.022f;

        // 中心死区
        assert(lay.HitTest(500.0f, 400.0f).region == HitRegion::Center);
        assert(lay.HitTest(500.0f, 459.0f).region == HitRegion::Center);
        // 外圈以外
        assert(lay.HitTest(500.0f, 199.0f).region == HitRegion::Outside);
        assert(lay.HitTest(701.0f, 400.0f).region == HitRegion::Outside);

        // 首扇区中心必在 12 点 (-PI/2)
        const SectorAngles a0 = lay.AnglesFor(0);
        assert(std::fabs(a0.mid - (-kPi * 0.5f)) < 1e-5f);

        // 十二点左右两侧微偏均在扇区 0（无接缝盲区）
        const float rMid = (lay.innerRadius + lay.outerRadius) * 0.5f;
        for (float eps : {-0.01f, 0.0f, 0.01f}) {
            const float ang = -kPi * 0.5f + eps;
            const HitResult h = lay.HitTest(lay.centerX + std::cos(ang) * rMid,
                                            lay.centerY + std::sin(ang) * rMid);
            assert(h.region == HitRegion::Sector && h.sector == 0);
        }

        // 所有扇区顺时针精确命中；扇区间隙命中 Gap
        for (int s = 0; s < n; ++s) {
            const SectorAngles sa = lay.AnglesFor(s);
            const HitResult hs = lay.HitTest(lay.centerX + std::cos(sa.mid) * rMid,
                                             lay.centerY + std::sin(sa.mid) * rMid);
            assert(hs.region == HitRegion::Sector && hs.sector == s);

            // 扇区与下一扇区的边界中点处于 Gap
            const float boundary = sa.end + lay.gapHalfAngle;
            const HitResult hg = lay.HitTest(lay.centerX + std::cos(boundary) * rMid,
                                             lay.centerY + std::sin(boundary) * rMid);
            assert(hg.region == HitRegion::Gap);
        }
    }

    // 2) 状态机：打开抑制点鸡松键、按下A松开B取消、移出外圈取消、切层防同帧连点、鼠标原位再点、右键退款
    Layout lay;
    lay.centerX = 500.0f;
    lay.centerY = 400.0f;
    lay.outerRadius = 200.0f;
    lay.innerRadius = 60.0f;
    lay.sectorCount = 6;
    const float rMid = 130.0f;
    auto ptOf = [&](int sector) {
        const float a = lay.AnglesFor(sector).mid;
        return std::pair<float, float>{lay.centerX + std::cos(a) * rMid,
                                        lay.centerY + std::sin(a) * rMid};
    };

    WheelState st;
    // 模拟点鸡打开瞬间左键仍按着
    st.ResetOnOpen(true);
    assert(!st.armed);
    {
        auto [px, py] = ptOf(0);
        InputSnapshot snap{};
        snap.mouseX = px; snap.mouseY = py;
        snap.leftDown = false; snap.leftReleased = true; // 打开时的残留松键
        Action a = st.Step(lay, snap);
        assert(a.type == Action::Type::None);
        assert(!st.armed); // 本帧仍有 released，尚未 arm
    }
    {
        auto [px, py] = ptOf(0);
        InputSnapshot snap{};
        snap.mouseX = px; snap.mouseY = py; // 下一帧鼠标全松开 -> armed
        Action a = st.Step(lay, snap);
        assert(a.type == Action::Type::None);
        assert(st.armed);
    }

    // 按下扇区0，移到扇区1松开 -> 不触发
    {
        auto [p0x, p0y] = ptOf(0);
        InputSnapshot s1{}; s1.mouseX = p0x; s1.mouseY = p0y; s1.leftDown = true; s1.leftPressed = true;
        assert(st.Step(lay, s1).type == Action::Type::None);

        auto [p1x, p1y] = ptOf(1);
        InputSnapshot s2{}; s2.mouseX = p1x; s2.mouseY = p1y; s2.leftReleased = true;
        assert(st.Step(lay, s2).type == Action::Type::None);
    }

    // 按下扇区0，移出外圈松开 -> 不触发
    {
        auto [p0x, p0y] = ptOf(0);
        InputSnapshot s1{}; s1.mouseX = p0x; s1.mouseY = p0y; s1.leftDown = true; s1.leftPressed = true;
        st.Step(lay, s1);
        InputSnapshot s2{}; s2.mouseX = 900.0f; s2.mouseY = 900.0f; s2.leftReleased = true;
        assert(st.Step(lay, s2).type == Action::Type::None);
    }

    // 同一扇区完整按下+松开进入二级，且切层后清空旧按下，停在原方向可继续点击二级
    {
        auto [p0x, p0y] = ptOf(0);
        InputSnapshot s1{}; s1.mouseX = p0x; s1.mouseY = p0y; s1.leftDown = true; s1.leftPressed = true;
        assert(st.Step(lay, s1).type == Action::Type::None);

        InputSnapshot s2{}; s2.mouseX = p0x; s2.mouseY = p0y; s2.leftReleased = true;
        Action a = st.Step(lay, s2);
        assert(a.type == Action::Type::EnterCategory && a.sector == 0);
        st.EnterSecondLevel(a.sector);
        assert(st.level == 1 && st.pressedButton == 0);

        // 同帧或未重新按下，松开不会触发二级
        InputSnapshot sNoPress{}; sNoPress.mouseX = p0x; sNoPress.mouseY = p0y; sNoPress.leftReleased = true;
        assert(st.Step(lay, sNoPress).type == Action::Type::None);

        // 鼠标停在原方向重新按下并松开 -> 二级选中（即使该项在 UI 层是禁用态，也可交由业务返回原因或右键退款）
        lay.sectorCount = 5;
        auto [p2x, p2y] = std::pair<float, float>{lay.centerX + std::cos(lay.AnglesFor(0).mid) * rMid,
                                                   lay.centerY + std::sin(lay.AnglesFor(0).mid) * rMid};
        InputSnapshot s3{}; s3.mouseX = p2x; s3.mouseY = p2y; s3.leftDown = true; s3.leftPressed = true;
        assert(st.Step(lay, s3, {false, true, true, true, true}).type == Action::Type::None);
        InputSnapshot s4{}; s4.mouseX = p2x; s4.mouseY = p2y; s4.leftReleased = true;
        Action sel = st.Step(lay, s4, {false, true, true, true, true});
        assert(sel.type == Action::Type::SelectItem && sel.sector == 0);

        // 禁用项右键按下+释放仍触发退款 RefundItem
        InputSnapshot r1{}; r1.mouseX = p2x; r1.mouseY = p2y; r1.rightDown = true; r1.rightPressed = true;
        assert(st.Step(lay, r1, {false, true, true, true, true}).type == Action::Type::None);
        InputSnapshot r2{}; r2.mouseX = p2x; r2.mouseY = p2y; r2.rightReleased = true;
        Action ref = st.Step(lay, r2, {false, true, true, true, true});
        assert(ref.type == Action::Type::RefundItem && ref.sector == 0);
    }

    // 3) 键盘导航 + Esc 二级退一级 / 一级收盘 + 失焦清理 + suppressNav
    {
        InputSnapshot sup{}; sup.digitPressed = 2; sup.suppressNav = true;
        assert(st.Step(lay, sup).type == Action::Type::None);

        InputSnapshot dir{}; dir.dirStep = 1;
        st.Step(lay, dir);
        assert(st.kbIndex == 0);
        st.Step(lay, dir);
        assert(st.kbIndex == 1);

        InputSnapshot ent{}; ent.enterPressed = true;
        Action a = st.Step(lay, ent);
        assert(a.type == Action::Type::SelectItem && a.sector == 1);

        InputSnapshot esc1{}; esc1.escPressed = true;
        assert(st.Step(lay, esc1).type == Action::Type::Back);
        assert(st.level == 0);

        InputSnapshot esc2{}; esc2.escPressed = true;
        assert(st.Step(lay, esc2).type == Action::Type::Close);

        // 失焦清除已按下状态
        InputSnapshot p{}; p.mouseX = ptOf(0).first; p.mouseY = ptOf(0).second; p.leftDown = true; p.leftPressed = true;
        st.Step(lay, p);
        assert(st.pressedButton == 1);
        InputSnapshot lost{}; lost.windowFocused = false;
        st.Step(lay, lost);
        assert(st.pressedButton == 0 && !st.armed);
    }
    std::printf("RadialWheel geometry & state machine assertions passed!\n");
}

int main(){
    bool old=run(false),fixed=run(true);
    printf("Changed allocation: old click=%d, stable ID click=%d\n",old,fixed);
    assert(!old&&fixed);
    TestRadialWheelGeometryAndInput();
}
