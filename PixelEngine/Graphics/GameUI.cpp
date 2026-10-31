#include "GameUI.hpp"
#include <algorithm>
#include <iostream>
#include "UiScale.hpp"
#include "Palette.hpp"
#include "../Core/Resolutions.hpp"
#include "../CS2D/MapCatalog.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include "ImGuiUI.hpp"
#include "../CS2D/CS2DGame.hpp"   // MatchMode
#include <cstdio>
#include <cmath>

// ============================================================================
// 私有 helper
// ============================================================================

namespace {

void DrawImGuiText(ImGuiUI* ui, const char* text, float x, float y,
                   Uint8 r, Uint8 g, Uint8 b, Uint8 a, int size) {
    if (!ui) return;
    ImFont* font = ui->GetFont(size);
    if (font) ImGui::PushFont(font);
    ImGui::GetForegroundDrawList()->AddText(ImVec2(x, y), IM_COL32(r, g, b, a), text);
    if (font) ImGui::PopFont();
}

void DrawImGuiTextCentered(ImGuiUI* ui, const char* text, float centerY,
                           Uint8 r, Uint8 g, Uint8 b, Uint8 a, int size) {
    if (!ui) return;
    ImFont* font = ui->GetFont(size);
    if (font) ImGui::PushFont(font);
    const ImVec2 sz = ImGui::CalcTextSize(text);
    const float x = (ImGui::GetIO().DisplaySize.x - sz.x) / 2.0f;
    ImGui::GetForegroundDrawList()->AddText(ImVec2(x, centerY), IM_COL32(r, g, b, a), text);
    if (font) ImGui::PopFont();
}

} // anonymous namespace

// ============================================================================
// 菜单
// ============================================================================

namespace {

// 菜单背景：上深下浅渐变 + 顶部金线 + 底部暗角，让页面有"产品感"而非纯黑
void DrawMenuBackdrop(ImDrawList* dl, const ImVec2& pos, const ImVec2& size) {
    const ImVec2 e(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilledMultiColor(pos, e,
        Palette::U32(ImVec4(0.035f, 0.048f, 0.065f, 1.0f)),
        Palette::U32(ImVec4(0.035f, 0.048f, 0.065f, 1.0f)),
        Palette::U32(ImVec4(0.078f, 0.100f, 0.128f, 1.0f)),
        Palette::U32(ImVec4(0.078f, 0.100f, 0.128f, 1.0f)));

    dl->AddRectFilled(pos, ImVec2(e.x, pos.y + std::max(1.0f, UI(2.0f))),
                      Palette::U32(Palette::Accent()));
    dl->AddRectFilled(ImVec2(pos.x, pos.y + std::max(2.0f, UI(3.0f))),
                      ImVec2(e.x, pos.y + std::max(4.0f, UI(6.0f))),
                      Palette::U32(ImVec4(0.92f, 0.74f, 0.33f, 0.16f)));

    dl->AddRectFilledMultiColor(
        ImVec2(pos.x, pos.y + size.y * 0.68f), e,
        Palette::U32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)),
        Palette::U32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)),
        Palette::U32(ImVec4(0.0f, 0.0f, 0.0f, 0.45f)),
        Palette::U32(ImVec4(0.0f, 0.0f, 0.0f, 0.45f)));
}

// 标题下方的短金线
void DrawTitleAccent(ImDrawList* dl, float centerX, float y, float halfW) {
    dl->AddRectFilled(ImVec2(centerX - halfW, y),
                      ImVec2(centerX + halfW, y + std::max(2.0f, UI(3.0f))),
                      Palette::U32(Palette::Accent()));
}

// 右对齐文本（用同一字体测量宽度）
void DrawImGuiTextRight(ImGuiUI* ui, const char* text, float rightX, float y,
                        Uint8 r, Uint8 g, Uint8 b, Uint8 a, int size) {
    if (!ui) return;
    ImFont* font = ui->GetFont(size);
    if (font) ImGui::PushFont(font);
    const ImVec2 sz = ImGui::CalcTextSize(text);
    ImGui::GetForegroundDrawList()->AddText(ImVec2(rightX - sz.x, y), IM_COL32(r, g, b, a), text);
    if (font) ImGui::PopFont();
}
// ---- 设置 / 历史页通用视觉 ----
// 页面背景：深色渐变 + 顶部冷光 + 细网格 + 斜向光束 + 底部暗角
void DrawPageBackdrop(ImDrawList* dl, const ImVec2& pos, const ImVec2& size) {
    const ImVec2 e(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilledMultiColor(pos, e,
        IM_COL32(7, 11, 16, 255), IM_COL32(7, 11, 16, 255),
        IM_COL32(12, 18, 26, 255), IM_COL32(12, 18, 26, 255));

    // 顶部冷光带
    dl->AddRectFilledMultiColor(pos, ImVec2(e.x, pos.y + size.y * 0.45f),
        IM_COL32(34, 54, 78, 72), IM_COL32(34, 54, 78, 72),
        IM_COL32(34, 54, 78, 0), IM_COL32(34, 54, 78, 0));

    // 细网格（很低的存在感，给版面一个"工程图"的底）
    for (float x = pos.x; x < e.x; x += UI(72.0f)) {
        dl->AddLine(ImVec2(x, pos.y), ImVec2(x, e.y), IM_COL32(92, 114, 140, 13));
    }
    for (float y = pos.y; y < e.y; y += UI(72.0f)) {
        dl->AddLine(ImVec2(pos.x, y), ImVec2(e.x, y), IM_COL32(92, 114, 140, 13));
    }

    // 斜向体积光（两条，很淡）
    {
        const float w = size.x;
        dl->AddQuadFilled(
            ImVec2(pos.x + w * 0.10f, pos.y - size.y * 0.1f),
            ImVec2(pos.x + w * 0.24f, pos.y - size.y * 0.1f),
            ImVec2(pos.x + w * 0.52f, e.y),
            ImVec2(pos.x + w * 0.30f, e.y),
            IM_COL32(120, 160, 210, 10));
        dl->AddQuadFilled(
            ImVec2(pos.x + w * 0.62f, pos.y - size.y * 0.1f),
            ImVec2(pos.x + w * 0.70f, pos.y - size.y * 0.1f),
            ImVec2(pos.x + w * 0.98f, e.y),
            ImVec2(pos.x + w * 0.86f, e.y),
            IM_COL32(120, 160, 210, 8));
    }

    // 底部暗角
    dl->AddRectFilledMultiColor(ImVec2(pos.x, pos.y + size.y * 0.70f), e,
        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150));
}

// 页面标题：左对齐的小标签 + 主标题 + 金色短线 + 副标题 + 右侧提示
void DrawPageHeader(ImGuiUI* ui, ImDrawList* dl, const ImVec2& pos, const ImVec2& size,
                    const char* kicker, const char* title, const char* subtitle,
                    const char* hint) {
    const float x = pos.x + UI(72.0f);
    DrawImGuiText(ui, kicker, x, pos.y + UI(32.0f), 126, 150, 176, 255, 12);
    DrawImGuiText(ui, title, x, pos.y + UI(50.0f), 242, 246, 252, 255, 32);
    dl->AddRectFilled(ImVec2(x, pos.y + UI(92.0f)),
                      ImVec2(x + UI(46.0f), pos.y + UI(95.0f)),
                      Palette::U32(Palette::Accent()));
    DrawImGuiText(ui, subtitle, x, pos.y + UI(102.0f), 126, 146, 168, 255, 14);
    if (hint) {
        DrawImGuiTextRight(ui, hint, pos.x + size.x - UI(72.0f), pos.y + UI(56.0f),
                           136, 156, 178, 235, 13);
    }
}

// 统计小卡片（历史页顶部）
void DrawStatChip(ImGuiUI* ui, ImDrawList* dl, float x, float y, float w, float h,
                  const char* value, const char* label, ImU32 accent) {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(12, 18, 25, 236), UI(10.0f));
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(52, 68, 86, 170),
                UI(10.0f), 0, UI(1.0f));
    dl->AddRectFilled(ImVec2(x + UI(18.0f), y + UI(16.0f)),
                      ImVec2(x + UI(21.0f), y + UI(34.0f)), accent, UI(2.0f));
    DrawImGuiText(ui, value, x + UI(30.0f), y + UI(10.0f), 238, 243, 250, 255, 24);
    DrawImGuiText(ui, label, x + UI(30.0f), y + UI(44.0f), 126, 146, 168, 255, 13);
}

// 选图浮层的探照光：与主页面的体积光保持一致，
// 只保留一束冷白锥形光，不绘制光源光晕和末尾小圆圈。
void DrawMapBeam(ImDrawList* dl, float lightProgress,
                 const ImVec2& wp, const ImVec2& ws,
                 float fade, bool overInteractive) {
    if (fade <= 0.02f) return;
    const float lp = std::clamp(lightProgress, 0.0f, 1.0f);
    const ImVec2 origin(wp.x + ws.x * 0.5f,
                        wp.y + (ws.y - UI(8.0f)) * lp + UI(4.0f));
    ImVec2 target = ImGui::GetIO().MousePos;
    target.x = std::clamp(target.x, wp.x + UI(12.0f), wp.x + ws.x - UI(12.0f));
    target.y = std::clamp(target.y, wp.y + UI(12.0f), wp.y + ws.y - UI(24.0f));

    ImVec2 dir(target.x - origin.x, target.y - origin.y);
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len <= 1.0f) return;

    dir.x /= len;
    dir.y /= len;
    const ImVec2 perp(-dir.y, dir.x);
    const float maxDim = std::max(ws.x, ws.y);
    const float distance01 = std::clamp(len / maxDim, 0.0f, 1.0f);
    const float nearHalf = UI(1.2f);
    const float farHalf = UI(26.0f + 34.0f * distance01) *
                          (overInteractive ? 1.18f : 1.0f);
    const ImU32 col = IM_COL32(184, 219, 255, static_cast<int>(84 * fade));

    // 把当前俯视图当成地面：光斑随光源到鼠标的距离缩放，
    // 并沿光束方向压扁成椭圆，形成透视景深。
    const float angle = std::atan2(dir.y, dir.x);
    const float longR = UI(52.0f) * (0.55f + 1.25f * distance01) *
                        (overInteractive ? 1.22f : 1.0f);
    const float shortR = longR * 0.42f;
    dl->AddEllipseFilled(target, ImVec2(longR * 1.35f, shortR * 1.35f),
                         IM_COL32(86, 145, 220, static_cast<int>(11 * fade)),
                         angle, 36);
    dl->AddEllipseFilled(target, ImVec2(longR, shortR),
                         IM_COL32(132, 194, 255, static_cast<int>(19 * fade)),
                         angle, 36);
    dl->AddEllipseFilled(target, ImVec2(longR * 0.64f, shortR * 0.64f),
                         IM_COL32(196, 231, 255, static_cast<int>(28 * fade)),
                         angle, 32);

    dl->AddQuadFilled(
        ImVec2(origin.x + perp.x * nearHalf, origin.y + perp.y * nearHalf),
        ImVec2(origin.x - perp.x * nearHalf, origin.y - perp.y * nearHalf),
        ImVec2(target.x - perp.x * farHalf, target.y - perp.y * farHalf),
        ImVec2(target.x + perp.x * farHalf, target.y + perp.y * farHalf),
        col);
}

} // namespace

void GameUI::DrawMenu(const MenuCtx& ctx, const Actions& actions) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("RootWindow", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ctx.cameraTransition) {
        if (ctx.backdropTex && ctx.backdropW > 0 && ctx.backdropH > 0) {
            const ImVec2 wp = viewport->WorkPos;
            const ImVec2 ws = viewport->WorkSize;
            const float texAspect = static_cast<float>(ctx.backdropW) / static_cast<float>(ctx.backdropH);
            const float scrAspect = ws.x / ws.y;
            ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
            if (texAspect > scrAspect) {
                const float frac = scrAspect / texAspect;
                uv0.x = (1.0f - frac) * 0.5f;
                uv1.x = uv0.x + frac;
            } else {
                const float frac = texAspect / scrAspect;
                uv0.y = (1.0f - frac) * 0.5f;
                uv1.y = uv0.y + frac;
            }
            ImGui::GetWindowDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(ctx.backdropTex),
                wp, ImVec2(wp.x + ws.x, wp.y + ws.y), uv0, uv1);
            ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
                wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                Palette::U32(ImVec4(0.02f, 0.03f, 0.045f, 0.50f)),
                Palette::U32(ImVec4(0.02f, 0.03f, 0.045f, 0.50f)),
                Palette::U32(ImVec4(0.03f, 0.045f, 0.06f, 0.66f)),
                Palette::U32(ImVec4(0.03f, 0.045f, 0.06f, 0.66f)));
            DrawMapBeam(ImGui::GetWindowDrawList(), ctx.mapOverlayLightProgress,
                        wp, ws, ctx.cameraBeamFade, false);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    if (ctx.mapOverlayOpen) {
        DrawMapOverlay(ctx, actions);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    const float mainMenuOpen = std::clamp(ctx.focusProgress, 0.0f, 1.0f);
    const float mainMenuAlpha = ctx.focus3D
        ? 1.0f - mainMenuOpen * mainMenuOpen * (3.0f - 2.0f * mainMenuOpen)
        : 1.0f;

    const float vpW = viewport->WorkSize.x;
    const float centerX = vpW * 0.5f;
    const float buttonW = UI(300.0f);
    const float buttonH = UI(46.0f);

    auto bigButton = [&](const char* label) {
        ImGui::SetCursorPosX(centerX - buttonW * 0.5f);
        const bool pressed = ImGui::Button(label, ImVec2(buttonW, buttonH));
        if (ImGui::IsItemHovered()) {
            // 悬停时左侧亮起金色竖条，强化"可点"的反馈
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(a.x, a.y + UI(2.0f)), ImVec2(a.x + UI(3.0f), b.y - UI(2.0f)),
                Palette::U32(Palette::Accent()), UI(2.0f));
        }
        return pressed;
    };
    auto doClick = [&](const std::function<void()>& action) {
        if (actions.playClick) actions.playClick();
        if (action) action();
    };

    // ---------------- 主菜单 ----------------
    if (ctx.state == MAIN_MENU) {
        if (ctx.focus3D) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, mainMenuAlpha);
            ImGui::BeginDisabled(true);
        }
        // 整块内容（标题 + 副标题 + 按钮组）垂直居中
        const float vpH = viewport->WorkSize.y;
        const float itemSpacing = ImGui::GetStyle().ItemSpacing.y;
        const int   nBtn = 6;

        const float titleH = UI(46.0f);
        const float subH   = UI(30.0f);
        const float gap1   = UI(26.0f);
        const float blockH = titleH + subH + gap1 + nBtn * buttonH +
                             (nBtn - 1) * itemSpacing;

        const float yLocal  = std::max(UI(16.0f), (vpH - blockH) * 0.5f);
        const float yScreen = viewport->WorkPos.y + yLocal;

        ImDrawList* mdl = ImGui::GetWindowDrawList();
        if (ctx.backdropTex && ctx.backdropW > 0 && ctx.backdropH > 0) {
            // 用一张地图实景做背景：等比裁切铺满，再压暗，保证标题可读
            const float sw = viewport->WorkSize.x;
            const float sh = viewport->WorkSize.y;
            const float texAspect = static_cast<float>(ctx.backdropW) / static_cast<float>(ctx.backdropH);
            const float scrAspect = sw / sh;
            ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
            if (texAspect > scrAspect) {
                const float frac = scrAspect / texAspect;
                uv0.x = (1.0f - frac) * 0.5f;
                uv1.x = uv0.x + frac;
            } else {
                const float frac = texAspect / scrAspect;
                uv0.y = (1.0f - frac) * 0.5f;
                uv1.y = uv0.y + frac;
            }
            mdl->AddImage(reinterpret_cast<ImTextureID>(ctx.backdropTex),
                          viewport->WorkPos,
                          ImVec2(viewport->WorkPos.x + sw, viewport->WorkPos.y + sh),
                          uv0, uv1);
            // 压暗 + 上下渐变，让文字浮出来；实时 3D 背景保留更多细节
            const float dimTop = ctx.backdrop3D ? 0.50f : 0.88f;
            const float dimBot = ctx.backdrop3D ? 0.66f : 0.94f;
            mdl->AddRectFilledMultiColor(
                viewport->WorkPos, ImVec2(viewport->WorkPos.x + sw, viewport->WorkPos.y + sh),
                Palette::U32(ImVec4(0.02f, 0.03f, 0.045f, dimTop)),
                Palette::U32(ImVec4(0.02f, 0.03f, 0.045f, dimTop)),
                Palette::U32(ImVec4(0.03f, 0.045f, 0.06f, dimBot)),
                Palette::U32(ImVec4(0.03f, 0.045f, 0.06f, dimBot)));
        }
        if (!ctx.backdrop3D) {
            DrawMenuBackdrop(mdl, viewport->WorkPos, viewport->WorkSize);
        }
        const Uint8 titleAlpha = static_cast<Uint8>(255.0f * mainMenuAlpha);
        const Uint8 subAlpha = static_cast<Uint8>(255.0f * mainMenuAlpha);
        DrawImGuiTextCentered(imGuiUI_, "CS2D", yScreen, 255, 255, 255, titleAlpha, 34);
        if (mainMenuAlpha > 0.02f) {
            DrawTitleAccent(ImGui::GetWindowDrawList(), centerX, yScreen + UI(44.0f), UI(46.0f));
        }
        DrawImGuiTextCentered(imGuiUI_, "Counter-Strike 2D 单机版", yScreen + titleH,
                              160, 190, 210, subAlpha, 20);

        ImGui::SetCursorPosY(yLocal + titleH + subH + gap1);
        if (bigButton("团队歼灭")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::TeamElimination)); });
        if (bigButton("爆破模式")) doClick([&] { actions.goState(MAP_SELECT); });
        if (bigButton("死斗模式")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::DeathMatch)); });
        if (bigButton("历史记录")) doClick([&] { actions.goState(HISTORY); });
        if (bigButton("设置"))     doClick([&] { actions.openSettings(MAIN_MENU); });
        if (bigButton("退出游戏")) doClick([&] { actions.quitToDesktop(); });
        if (ctx.focus3D) {
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
        }
    }
    // ---------------- 暂停 ----------------
    else if (ctx.state == PAUSED) {
        // 标题 + 5 个按钮整块垂直居中
        const float vpH = viewport->WorkSize.y;
        const float sp  = ImGui::GetStyle().ItemSpacing.y;
        const int   nBtn = 5;
        const float titleH = UI(40.0f);
        const float gap1   = UI(28.0f);
        const float blockH = titleH + gap1 + nBtn * buttonH + (nBtn - 1) * sp;
        const float yLocal = std::max(UI(16.0f), (vpH - blockH) * 0.5f);

        DrawMenuBackdrop(ImGui::GetWindowDrawList(), viewport->WorkPos, viewport->WorkSize);
        DrawImGuiTextCentered(imGuiUI_, "已暂停", viewport->WorkPos.y + yLocal,
                              255, 255, 255, 255, 30);
        DrawTitleAccent(ImGui::GetWindowDrawList(), centerX,
                        viewport->WorkPos.y + yLocal + UI(38.0f), UI(40.0f));

        ImGui::SetCursorPosY(yLocal + titleH + gap1);
        if (bigButton("继续游戏")) doClick([&] { actions.resumeGame(); });
        if (bigButton("重新开始")) doClick([&] { actions.startMatch(); });
        if (bigButton("设置"))     doClick([&] { actions.openSettings(PAUSED); });
        if (bigButton("退出游戏")) doClick([&] { actions.confirmExit(false); });
        if (bigButton("退出桌面")) doClick([&] { actions.confirmExit(true); });
    }
    // ---------------- 退出确认 ----------------
    else if (ctx.state == EXIT_CONFIRM) {
        const float vpH = viewport->WorkSize.y;
        const float titleH = UI(38.0f);
        const float subH   = UI(28.0f);
        const float gap1   = UI(18.0f);
        const float gap2   = UI(30.0f);
        const float blockH = titleH + subH + gap1 + gap2 + buttonH;
        const float yLocal = std::max(UI(16.0f), (vpH - blockH) * 0.5f);

        DrawMenuBackdrop(ImGui::GetWindowDrawList(), viewport->WorkPos, viewport->WorkSize);
        DrawImGuiTextCentered(imGuiUI_, "确认退出",
                              viewport->WorkPos.y + yLocal, 255, 255, 255, 255, 26);
        DrawTitleAccent(ImGui::GetWindowDrawList(), centerX,
                        viewport->WorkPos.y + yLocal + UI(34.0f), UI(36.0f));
        DrawImGuiTextCentered(imGuiUI_, "当前对局进度将不会被保存",
                              viewport->WorkPos.y + yLocal + titleH, 200, 200, 200, 255, 18);

        const float btnY = yLocal + titleH + subH + gap1 + gap2;
        ImGui::SetCursorPosY(btnY);
        ImGui::SetCursorPosX(centerX - buttonW - UI(15.0f));
        if (ImGui::Button("返回游戏", ImVec2(buttonW, buttonH))) doClick([&] { actions.goState(PAUSED); });
        ImGui::SetCursorPosY(btnY);
        ImGui::SetCursorPosX(centerX + UI(15.0f));
        if (ImGui::Button("退出", ImVec2(buttonW, buttonH))) doClick([&] { actions.quitApp(); });
    }
    // ---------------- 设置 ----------------
    else if (ctx.state == SETTINGS) {
        const ImVec2 wp = viewport->WorkPos;
        const ImVec2 ws = viewport->WorkSize;
        const float vpH = ws.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        DrawPageBackdrop(dl, wp, ws);
        DrawPageHeader(imGuiUI_, dl, wp, ws,
                       "SYSTEM / CONFIGURATION", "设置",
                       "音频 · 操作 · 准星 · 雷达", "ESC  返回");

        // 统一控件配色（暗底 + 金色抓取块）
        ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0.075f, 0.105f, 0.140f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0.105f, 0.145f, 0.190f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0.125f, 0.170f, 0.220f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0.84f, 0.65f, 0.28f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.79f, 0.37f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark,        ImVec4(0.95f, 0.76f, 0.33f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg,          ImVec4(0.055f, 0.075f, 0.100f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Header,           ImVec4(0.25f, 0.20f, 0.10f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,    ImVec4(0.34f, 0.27f, 0.13f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,     ImVec4(0.42f, 0.33f, 0.15f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text,             ImVec4(0.86f, 0.90f, 0.94f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,  UI(4.0f));

        int master  = ctx.masterVolume;
        int music   = ctx.musicVolume;
        int effects = ctx.effectsVolume;

        const float topLocal = UI(126.0f);
        const float footerH  = UI(86.0f);
        ImGui::SetCursorPosY(topLocal);
        ImGui::BeginChild("##settingsBody",
                          ImVec2(ws.x, std::max(UI(200.0f), vpH - topLocal - footerH)),
                          false, ImGuiWindowFlags_NoBackground);

        const float panelW = std::min(UI(1200.0f), ws.x - UI(120.0f));
        const float panelX = (ws.x - panelW) * 0.5f;
        const float colGap = UI(20.0f);
        const float colW   = (panelW - colGap) * 0.5f;
        const float labelX = UI(28.0f);
        const float fieldX = UI(170.0f);
        const float fieldW = colW - fieldX - UI(26.0f);
        const float rowH   = UI(44.0f);
        const float header = UI(62.0f);

        auto rowPos = [&](float x, float y, int i) {
            return ImVec2(x + fieldX, y + header + static_cast<float>(i) * rowH);
        };
        auto rowLabel = [&](float x, float y, int i, const char* text) {
            DrawImGuiText(imGuiUI_, text, x + labelX,
                          y + header + static_cast<float>(i) * rowH + UI(8.0f),
                          186, 200, 216, 255, 15);
        };
        auto drawCard = [&](float x, float y, const char* title, const char* tag,
                            int rows, float extraH, ImU32 accent,
                            const std::function<void(float, float)>& body) -> float {
            const float h = header + static_cast<float>(rows) * rowH + extraH;
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            cdl->AddRectFilled(ImVec2(x, y), ImVec2(x + colW, y + h),
                               IM_COL32(12, 18, 25, 236), UI(10.0f));
            cdl->AddRect(ImVec2(x, y), ImVec2(x + colW, y + h),
                         IM_COL32(52, 68, 86, 170), UI(10.0f), 0, UI(1.0f));
            cdl->AddRectFilled(ImVec2(x + UI(22.0f), y + UI(16.0f)),
                               ImVec2(x + UI(26.0f), y + UI(38.0f)), accent, UI(2.0f));
            DrawImGuiText(imGuiUI_, title, x + UI(34.0f), y + UI(12.0f), 236, 242, 250, 255, 19);
            if (tag) {
                DrawImGuiText(imGuiUI_, tag, x + UI(34.0f), y + UI(36.0f), 116, 136, 156, 220, 12);
            }
            body(x, y);
            return h;
        };

        const float leftX  = panelX;
        const float rightX = panelX + colW + colGap;
        const float topY = ImGui::GetCursorScreenPos().y;
        float leftY = topY;
        float rightY = topY;

        // ---------------- 左列：音频 / 操作 ----------------
        leftY += drawCard(leftX, leftY, "音频", "VOLUME & SOUNDTRACK", 6, 0.0f,
                          Palette::U32(Palette::Accent()), [&](float x, float y) {
            rowLabel(x, y, 0, "主音量");
            ImGui::SetCursorScreenPos(rowPos(x, y, 0));
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::SliderInt("##master", &master, 0, 100)) actions.setMasterVolume(master);

            rowLabel(x, y, 1, "音乐");
            ImGui::SetCursorScreenPos(rowPos(x, y, 1));
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::SliderInt("##music", &music, 0, 100)) actions.setMusicVolume(music);

            rowLabel(x, y, 2, "音效");
            ImGui::SetCursorScreenPos(rowPos(x, y, 2));
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::SliderInt("##effects", &effects, 0, 100)) actions.setEffectsVolume(effects);

            const int musN = std::clamp(ctx.musicTrackCount, 0, 8);
            rowLabel(x, y, 3, "背景音乐");
            if (musN > 0) {
                ImGui::SetCursorScreenPos(rowPos(x, y, 3));
                ImGui::SetNextItemWidth(fieldW);
                int idx = std::clamp(ctx.musicTrack, 0, musN - 1);
                if (ImGui::Combo("##music_track", &idx, ctx.musicTrackLabels, musN)) {
                    if (actions.setMusicTrack) actions.setMusicTrack(idx);
                }
            }

            const int clkN = std::clamp(ctx.clickSoundCount, 0, 8);
            rowLabel(x, y, 4, "按钮音效");
            if (clkN > 0) {
                ImGui::SetCursorScreenPos(rowPos(x, y, 4));
                ImGui::SetNextItemWidth(fieldW);
                int idx = std::clamp(ctx.clickSound, 0, clkN - 1);
                if (ImGui::Combo("##click_sound", &idx, ctx.clickSoundLabels, clkN)) {
                    if (actions.setClickSound) actions.setClickSound(idx);
                }
            }

            ImGui::SetCursorScreenPos(rowPos(x, y, 5));
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.11f, 0.15f, 0.20f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.21f, 0.27f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f, 0.26f, 0.33f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.32f, 0.40f, 0.48f, 0.55f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, UI(1.0f));
            if (ImGui::Button(ctx.muted ? "已静音 · 点击恢复" : "静音",
                              ImVec2(UI(180.0f), UI(32.0f)))) {
                actions.toggleMute();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }) + UI(16.0f);

        leftY += drawCard(leftX, leftY, "操作", "INPUT & DISPLAY", 5, 0.0f,
                          IM_COL32(86, 150, 210, 255), [&](float x, float y) {
            rowLabel(x, y, 0, "鼠标灵敏度");
            ImGui::SetCursorScreenPos(rowPos(x, y, 0));
            ImGui::SetNextItemWidth(fieldW);
            float sens = ctx.mouseSensitivity;
            if (ImGui::SliderFloat("##sens", &sens, 0.1f, 3.0f, "%.1f")) {
                actions.setMouseSensitivity(sens);
            }

            rowLabel(x, y, 1, "帧率上限");
            ImGui::SetCursorScreenPos(rowPos(x, y, 1));
            ImGui::SetNextItemWidth(fieldW);
            const char* fpsLabels[] = { "30", "60", "120" };
            int fpsIdx = (ctx.targetFPS >= 120) ? 2 : (ctx.targetFPS >= 60) ? 1 : 0;
            if (ImGui::Combo("##fps", &fpsIdx, fpsLabels, 3)) {
                actions.setTargetFPS(fpsIdx == 2 ? 120 : fpsIdx == 1 ? 60 : 30);
            }

            rowLabel(x, y, 2, "分辨率");
            ImGui::SetCursorScreenPos(rowPos(x, y, 2));
            ImGui::SetNextItemWidth(fieldW);
            const char* resLabels[kResolutionCount];
            for (int i = 0; i < kResolutionCount; ++i) resLabels[i] = kResolutions[i].label;
            int resIdx = std::clamp(ctx.resIdx, 0, kResolutionCount - 1);
            if (ImGui::Combo("##res", &resIdx, resLabels, kResolutionCount)) {
                actions.setResolution(kResolutions[resIdx].w, kResolutions[resIdx].h, resIdx);
            }

            rowLabel(x, y, 3, "渲染缩放");
            ImGui::SetCursorScreenPos(rowPos(x, y, 3));
            ImGui::SetNextItemWidth(fieldW);
            const char* scaleLabels[] = { "100%（最清晰）", "75%", "50%" };
            int scaleIdx = (ctx.renderScalePct >= 100) ? 0 : (ctx.renderScalePct >= 75) ? 1 : 2;
            if (ImGui::Combo("##rscale", &scaleIdx, scaleLabels, 3)) {
                actions.setRenderScale(scaleIdx == 0 ? 100 : scaleIdx == 1 ? 75 : 50);
            }

            ImGui::SetCursorScreenPos(rowPos(x, y, 4));
            bool fs = ctx.isFullscreen;
            if (ImGui::Checkbox("全屏（F11）", &fs)) actions.setFullscreen(fs);
        }) + UI(16.0f);

        // ---------------- 右列：准星 / 雷达 ----------------
        rightY += drawCard(rightX, rightY, "准星", "CROSSHAIR", 6, UI(30.0f),
                           IM_COL32(120, 196, 210, 255), [&](float x, float y) {
            rowLabel(x, y, 0, "颜色");
            ImGui::SetCursorScreenPos(rowPos(x, y, 0));
            float col[4] = { ctx.crosshairR / 255.0f, ctx.crosshairG / 255.0f,
                             ctx.crosshairB / 255.0f, ctx.crosshairA / 255.0f };
            if (ImGui::ColorEdit4("##crossColor", col,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                actions.setCrosshairColor(static_cast<Uint8>(col[0] * 255),
                                          static_cast<Uint8>(col[1] * 255),
                                          static_cast<Uint8>(col[2] * 255),
                                          static_cast<Uint8>(col[3] * 255));
            }

            rowLabel(x, y, 1, "粗细");
            ImGui::SetCursorScreenPos(rowPos(x, y, 1));
            ImGui::SetNextItemWidth(fieldW);
            float thick = ctx.crosshairThickness;
            if (ImGui::SliderFloat("##crossThick", &thick, 0.5f, 4.0f, "%.1f")) {
                actions.setCrosshairThickness(thick);
            }

            rowLabel(x, y, 2, "长度");
            ImGui::SetCursorScreenPos(rowPos(x, y, 2));
            ImGui::SetNextItemWidth(fieldW);
            float len = ctx.crosshairLength;
            if (ImGui::SliderFloat("##crossLen", &len, 4.0f, 30.0f, "%.0f")) {
                actions.setCrosshairLength(len);
            }

            rowLabel(x, y, 3, "中心间隔");
            ImGui::SetCursorScreenPos(rowPos(x, y, 3));
            ImGui::SetNextItemWidth(fieldW);
            float gap = ctx.crosshairGap;
            if (ImGui::SliderFloat("##crossGap", &gap, 0.0f, 12.0f, "%.1f")) {
                actions.setCrosshairGap(gap);
            }

            ImGui::SetCursorScreenPos(rowPos(x, y, 4));
            bool dot = ctx.crosshairCenterDot;
            if (ImGui::Checkbox("显示中心点", &dot)) actions.setCrosshairCenterDot(dot);

            ImGui::SetCursorScreenPos(rowPos(x, y, 5));
            bool dyn = ctx.crosshairDynamic;
            if (ImGui::Checkbox("动态准星（随移动扩散）", &dyn)) actions.setCrosshairDynamic(dyn);

            DrawImGuiText(imGuiUI_, "关闭后准星固定；开启后静止最准，移动时扩散",
                          x + labelX, y + header + 6.0f * rowH + UI(6.0f),
                          112, 132, 152, 220, 12);
        }) + UI(16.0f);

        rightY += drawCard(rightX, rightY, "雷达", "RADAR", 6, 0.0f,
                           IM_COL32(120, 200, 140, 255), [&](float x, float y) {
            ImGui::SetCursorScreenPos(rowPos(x, y, 0));
            bool pc = ctx.radarPlayerCentered;
            if (ImGui::Checkbox("玩家居中", &pc)) actions.setRadarPlayerCentered(pc);

            ImGui::SetCursorScreenPos(rowPos(x, y, 1));
            bool rwp = ctx.radarRotateWithPlayer;
            if (ImGui::Checkbox("随玩家旋转", &rwp)) actions.setRadarRotateWithPlayer(rwp);

            ImGui::SetCursorScreenPos(rowPos(x, y, 2));
            bool blend = ctx.radarBlendBg;
            if (ImGui::Checkbox("半透明背景", &blend)) actions.setRadarBlendBg(blend);

            rowLabel(x, y, 3, "背景透明度");
            ImGui::SetCursorScreenPos(rowPos(x, y, 3));
            ImGui::SetNextItemWidth(fieldW);
            int alpha = ctx.radarBgAlpha;
            if (ImGui::SliderInt("##radarAlpha", &alpha, 0, 255)) actions.setRadarBgAlpha(alpha);

            rowLabel(x, y, 4, "雷达大小");
            ImGui::SetCursorScreenPos(rowPos(x, y, 4));
            ImGui::SetNextItemWidth(fieldW);
            int size = ctx.radarHudSize;
            if (ImGui::SliderInt("##radarSize", &size, 60, 220)) actions.setRadarHudSize(size);

            rowLabel(x, y, 5, "地图缩放");
            ImGui::SetCursorScreenPos(rowPos(x, y, 5));
            ImGui::SetNextItemWidth(fieldW);
            float zoom = ctx.radarMapZoom;
            if (ImGui::SliderFloat("##radarZoom", &zoom, 1.0f, 8.0f, "%.2f")) {
                actions.setRadarMapZoom(zoom);
            }
        }) + UI(16.0f);

        rightY += drawCard(rightX, rightY, "视野 / 开镜", "VISION & SCOPE", 5, 0.0f,
                           IM_COL32(150, 170, 220, 255), [&](float x, float y) {
            rowLabel(x, y, 0, "迷雾透明度");
            ImGui::SetCursorScreenPos(rowPos(x, y, 0));
            ImGui::SetNextItemWidth(fieldW);
            int fog = ctx.fogOpacityPct;
            if (ImGui::SliderInt("##fogOpacity", &fog, 0, 100, "%d%%")) {
                actions.setFogOpacity(fog);
            }

            ImGui::SetCursorScreenPos(rowPos(x, y, 1));
            bool delayed = ctx.sniperDelayedScopeClose;
            if (ImGui::Checkbox("狙击枪延迟关镜", &delayed)) {
                actions.setSniperDelayedScopeClose(delayed);
            }

            rowLabel(x, y, 2, "开镜灵敏度");
            ImGui::SetCursorScreenPos(rowPos(x, y, 2));
            ImGui::SetNextItemWidth(fieldW);
            float scopeSens = ctx.scopeSensitivity;
            if (ImGui::SliderFloat("##scopeSens", &scopeSens, 0.5f, 2.0f, "%.2f")) {
                actions.setScopeSensitivity(scopeSens);
            }

            rowLabel(x, y, 3, "开镜准星粗细");
            ImGui::SetCursorScreenPos(rowPos(x, y, 3));
            ImGui::SetNextItemWidth(fieldW);
            float scopeCrossThick = ctx.scopeCrossThickness;
            if (ImGui::SliderFloat("##scopeCrossThick", &scopeCrossThick, 1.0f, 6.0f, "%.1f")) {
                actions.setScopeCrossThickness(scopeCrossThick);
            }

            ImGui::SetCursorScreenPos(rowPos(x, y, 4));
            bool crossDyn = ctx.scopeCrossDynamic;
            if (ImGui::Checkbox("开镜准星随移动扩散", &crossDyn)) {
                actions.setScopeCrossDynamic(crossDyn);
            }

            DrawImGuiText(imGuiUI_, "迷雾只影响观感；开镜准星是切分镜筒的大十字，"
                                    "灵敏度按 CS2 zoom_sensitivity_ratio 语义只作用于开镜期间",
                          x + labelX, y + header + 5.0f * rowH + UI(6.0f),
                          112, 132, 152, 220, 12);
        }) + UI(16.0f);

        // 撑出内容高度，保证短分辨率下可滚动
        ImGui::SetCursorScreenPos(ImVec2(panelX, std::max(leftY, rightY)));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        ImGui::EndChild();

        // ---- 页脚 ----
        ImGui::SetCursorScreenPos(ImVec2(wp.x + UI(72.0f), wp.y + vpH - UI(64.0f)));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.14f, 0.19f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.20f, 0.26f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.19f, 0.25f, 0.32f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.34f, 0.42f, 0.50f, 0.55f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, UI(1.0f));
        if (ImGui::Button("返回", ImVec2(UI(150.0f), UI(42.0f)))) {
            doClick([&] { actions.goState(ctx.settingsReturnState); });
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        DrawImGuiTextRight(imGuiUI_, "所有改动即时生效并自动保存",
                           wp.x + ws.x - UI(72.0f), wp.y + vpH - UI(46.0f),
                           112, 132, 152, 210, 12);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(11);
    }

    if (ctx.state == MAIN_MENU && ctx.focus3D) {
        DrawFocusWheel(ctx, actions);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// 俯视地图浮层
// ============================================================================

void GameUI::DrawMapOverlay(const MenuCtx& ctx, const Actions& actions) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 wp = vp->WorkPos;
    const ImVec2 ws = vp->WorkSize;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fade = std::clamp(ctx.mapOverlayFade, 0.0f, 1.0f);
    const bool interactive = (fade > 0.5f && !ctx.mapOverlayClosing &&
                              !ctx.mapOverlayLaunching);
    bool beamOverInteractive = false;

    if (ctx.backdropTex && ctx.backdropW > 0 && ctx.backdropH > 0) {
        const float texAspect = static_cast<float>(ctx.backdropW) / static_cast<float>(ctx.backdropH);
        const float scrAspect = ws.x / ws.y;
        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
        if (texAspect > scrAspect) {
            const float frac = scrAspect / texAspect;
            uv0.x = (1.0f - frac) * 0.5f;
            uv1.x = uv0.x + frac;
        } else {
            const float frac = texAspect / scrAspect;
            uv0.y = (1.0f - frac) * 0.5f;
            uv1.y = uv0.y + frac;
        }
        dl->AddImage(reinterpret_cast<ImTextureID>(ctx.backdropTex),
                     wp, ImVec2(wp.x + ws.x, wp.y + ws.y), uv0, uv1);
    }
    dl->AddRectFilledMultiColor(
        wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
        IM_COL32(5, 8, 12, 128), IM_COL32(5, 8, 12, 128),
        IM_COL32(8, 11, 15, 168), IM_COL32(8, 11, 15, 168));

    const int count = std::max(1, ctx.mapOverlayCount);
    const float cardW = UI(250.0f);
    const float cardH = UI(336.0f);
    const float gap = UI(20.0f);
    const float totalW = count * cardW + (count - 1) * gap;
    const float startX = wp.x + (ws.x - totalW) * 0.5f;
    const float cardY = wp.y + UI(146.0f);
    const int a255 = static_cast<int>(fade * 255.0f);

    DrawImGuiTextCentered(imGuiUI_, ctx.mapOverlayTitle.c_str(), wp.y + UI(42.0f),
                          240, 246, 255, a255, 32);
    DrawImGuiTextCentered(imGuiUI_, ctx.mapOverlaySubtitle.c_str(), wp.y + UI(86.0f),
                          170, 196, 212, a255, 18);

    for (int i = 0; i < count; ++i) {
        const ImVec2 cp(startX + i * (cardW + gap), cardY);
        const ImVec2 cp2(cp.x + cardW, cp.y + cardH);
        const bool selected = (ctx.mapOverlaySelected == i);

        ImGui::SetCursorScreenPos(cp);
        ImGui::PushID(i);
        bool hovered = false;
        if (interactive) {
            ImGui::InvisibleButton("card", ImVec2(cardW, cardH));
            hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                if (actions.playClick) actions.playClick();
                if (actions.selectMapChoice) actions.selectMapChoice(i);
            }
        }
        ImGui::PopID();

        const ImU32 bg = selected ? IM_COL32(28, 38, 50, static_cast<int>(225 * fade))
                                 : IM_COL32(18, 24, 32, static_cast<int>(205 * fade));
        dl->AddRectFilled(cp, cp2, bg, UI(10.0f));
        if (hovered) {
            beamOverInteractive = true;
            dl->AddRectFilled(cp, cp2, IM_COL32(110, 190, 255, static_cast<int>(26 * fade)), UI(10.0f));
        }
        dl->AddRect(cp, cp2,
                    hovered ? IM_COL32(130, 205, 255, a255)
                    : (selected ? IM_COL32(235, 190, 85, a255)
                                : IM_COL32(255, 255, 255, static_cast<int>(30 * fade))),
                    UI(10.0f), 0,
                    hovered ? UI(3.0f) : (selected ? UI(2.5f) : UI(1.0f)));

        const float pad = UI(10.0f);
        const ImVec2 img0(cp.x + pad, cp.y + pad);
        const ImVec2 img1(cp2.x - pad, cp.y + UI(166.0f));
        if (i < 8 && ctx.mapOverlayPreviews[i]) {
            // 预览图同样跟随浮层淡入淡出；否则返回主菜单时卡片已淡出、
            // 地图缩略图仍不透明，直到浮层关闭才整块消失。
            dl->AddImage(reinterpret_cast<ImTextureID>(ctx.mapOverlayPreviews[i]),
                         img0, img1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                         IM_COL32(255, 255, 255, a255));
        } else {
            dl->AddRectFilled(img0, img1, IM_COL32(34, 44, 56, static_cast<int>(230 * fade)), UI(6.0f));
        }
        dl->AddRect(img0, img1, IM_COL32(255, 255, 255, static_cast<int>(30 * fade)), UI(6.0f), 0, UI(1.0f));
        if (selected && actions.setMapPreviewRect) {
            actions.setMapPreviewRect(img0.x, img0.y, img1.x, img1.y);
        }

        if (i < 8) {
            DrawImGuiText(imGuiUI_, ctx.mapOverlayNames[i].c_str(),
                          cp.x + pad + UI(2.0f), cp.y + UI(184.0f),
                          236, 242, 250, a255, 23);
            DrawImGuiText(imGuiUI_, ctx.mapOverlayTaglines[i].c_str(),
                          cp.x + pad + UI(2.0f), cp.y + UI(220.0f),
                          156, 182, 202, a255, 14);
        }
        if (selected) {
            dl->AddRectFilled(ImVec2(cp.x + pad, cp2.y - UI(34.0f)),
                              ImVec2(cp.x + pad + UI(58.0f), cp2.y - UI(14.0f)),
                              IM_COL32(235, 190, 85, static_cast<int>(235 * fade)), UI(4.0f));
            DrawImGuiText(imGuiUI_, "已选", cp.x + pad + UI(12.0f), cp2.y - UI(32.0f),
                          20, 24, 30, a255, 14);
        }
    }

    const float btnW = UI(180.0f), btnH = UI(42.0f);
    const float btnY = cardY + cardH + UI(24.0f);
    if (interactive) {
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x * 0.5f - btnW - UI(12.0f), btnY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.72f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.82f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.62f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
        if (ImGui::Button("开始对局", ImVec2(btnW, btnH))) {
            if (actions.playClick) actions.playClick();
            if (actions.startMatchOnChoice) actions.startMatchOnChoice(ctx.mapOverlaySelected);
        }
        if (ImGui::IsItemHovered()) {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            beamOverInteractive = true;
            dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3),
                        IM_COL32(150, 215, 255, a255), UI(8.0f), 0, UI(2.0f));
        }
        ImGui::PopStyleColor(4);

        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x * 0.5f + UI(12.0f), btnY));
        if (ImGui::Button("返回主菜单", ImVec2(btnW, btnH))) {
            if (actions.playClick) actions.playClick();
            if (actions.closeMapOverlay) actions.closeMapOverlay();
        }
        if (ImGui::IsItemHovered()) {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            beamOverInteractive = true;
            dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3),
                        IM_COL32(150, 215, 255, a255), UI(8.0f), 0, UI(2.0f));
        }
    }

    // 俯视选图页的探照灯：灯源从顶部逐帧移到屏幕正下方，光束始终跟随鼠标
    DrawMapBeam(dl, ctx.mapOverlayLightProgress, wp, ws,
                ctx.mapOverlayBeamFade, beamOverInteractive);

    if (ctx.mapOverlayLaunching) {
        const float launch = std::clamp(ctx.mapOverlayLaunchProgress, 0.0f, 1.0f);
        // 地图外的界面先快速压暗淡出，只留下当前选中的地图预览。
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                          IM_COL32(0, 0, 0, static_cast<int>(launch * 255.0f)));
        const int idx = std::clamp(ctx.mapOverlaySelected, 0, count - 1);
        const ImVec2 cp(startX + idx * (cardW + gap), cardY);
        const ImVec2 img0(cp.x + UI(10.0f), cp.y + UI(10.0f));
        const ImVec2 img1(cp.x + cardW - UI(10.0f), cp.y + UI(166.0f));
        if (idx < 8 && ctx.mapOverlayPreviews[idx]) {
            dl->AddImage(reinterpret_cast<ImTextureID>(ctx.mapOverlayPreviews[idx]), img0, img1);
        } else {
            dl->AddRectFilled(img0, img1, IM_COL32(28, 38, 50, 255), UI(6.0f));
        }
        if (actions.setMapPreviewRect) {
            actions.setMapPreviewRect(img0.x, img0.y, img1.x, img1.y);
        }
        return;
    }

    DrawImGuiTextCentered(imGuiUI_, "← → 切换地图      Enter 开始      Esc 返回",
                          btnY + btnH + UI(18.0f), 130, 154, 176,
                          static_cast<int>(235 * fade), 14);

    if (interactive) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && ctx.mapOverlaySelected > 0) {
            if (actions.selectMapChoice) actions.selectMapChoice(ctx.mapOverlaySelected - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && ctx.mapOverlaySelected + 1 < count) {
            if (actions.selectMapChoice) actions.selectMapChoice(ctx.mapOverlaySelected + 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            if (actions.startMatchOnChoice) actions.startMatchOnChoice(ctx.mapOverlaySelected);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (actions.closeMapOverlay) actions.closeMapOverlay();
        }
    }
}

// ============================================================================
// 主菜单 3D 聚焦 + 右侧自定义面板
// ============================================================================

void GameUI::DrawFocusWheel(const MenuCtx& ctx, const Actions& actions) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 wp = vp->WorkPos;
    const ImVec2 ws = vp->WorkSize;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float p = std::clamp(ctx.focusProgress, 0.0f, 1.0f);
    const float open = p * p * (3.0f - 2.0f * p);
    const float contentAlpha = std::clamp((open - 0.45f) / 0.55f, 0.0f, 1.0f);

    // 左半屏留给 3D 聚焦模型，只压暗右侧轮盘区域
    if (ctx.backdropTex && ctx.backdropW > 0 && ctx.backdropH > 0) {
        const float texAspect = static_cast<float>(ctx.backdropW) / static_cast<float>(ctx.backdropH);
        const float scrAspect = ws.x / ws.y;
        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
        if (texAspect > scrAspect) {
            const float frac = scrAspect / texAspect;
            uv0.x = (1.0f - frac) * 0.5f;
            uv1.x = uv0.x + frac;
        } else {
            const float frac = texAspect / scrAspect;
            uv0.y = (1.0f - frac) * 0.5f;
            uv1.y = uv0.y + frac;
        }
        dl->AddImage(reinterpret_cast<ImTextureID>(ctx.backdropTex),
                     wp, ImVec2(wp.x + ws.x, wp.y + ws.y), uv0, uv1,
                     IM_COL32(255, 255, 255,
                              static_cast<int>(std::clamp(open, 0.0f, 1.0f) * 255.0f)));
    }
    dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                      IM_COL32(0, 0, 0, static_cast<int>(82.0f * open)));
    dl->AddRectFilled(ImVec2(wp.x + ws.x * 0.48f, wp.y),
                      ImVec2(wp.x + ws.x, wp.y + ws.y),
                      IM_COL32(4, 7, 10, static_cast<int>(150.0f * open)));

    // 左侧标题
    const char* kind = ctx.focusChicken ? "小鸡" : (ctx.focusObject == 0 ? "T" : "CT");
    dl->AddText(ImVec2(wp.x + UI(54.0f), wp.y + UI(44.0f)),
                IM_COL32(240, 216, 156, 255), kind);
    dl->AddText(ImVec2(wp.x + UI(54.0f), wp.y + UI(72.0f)),
                IM_COL32(168, 190, 208, 230), "自定义装备");
    dl->AddText(ImVec2(wp.x + UI(54.0f), wp.y + ws.y - UI(54.0f)),
                IM_COL32(150, 166, 180, 210), "点击右侧轮盘选项 · ESC 返回");

    const int categoryCount = std::clamp(ctx.focusCategoryCount, 0, 4);
    const float arcSpan = 1.34f * open;
    const ImVec2 center(wp.x + ws.x * 0.60f, wp.y + ws.y * 0.52f);

    // 每个类别的环半径：按选项数保证相邻按钮有足够弧距，环与环之间至少留 64。
    // 这样 8 个颜色选项也不会压在一起，同时四环仍能完整落在屏幕内。
    const float btnW = UI(60.0f);
    const float btnH = UI(27.0f);
    float radii[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int cat = 0; cat < categoryCount; ++cat) {
        const int n = std::clamp(ctx.focusOptionCounts[cat], 1, 8);
        const float minArcR = (btnW + UI(10.0f)) * static_cast<float>(n - 1) / (2.0f * 1.34f);
        const float baseR = UI(170.0f) * (0.52f + 0.30f * static_cast<float>(cat));
        const float prevR = (cat > 0) ? radii[cat - 1] + UI(60.0f) : 0.0f;
        radii[cat] = std::max(std::max(baseR, minArcR), prevR);
    }

    // 轮盘底：随 open 从物体后方展开（右半圆）
    const float anim = 0.20f + 0.80f * open;
    if (open > 0.02f && categoryCount > 0) {
        const float outerR = radii[categoryCount - 1] * anim + UI(26.0f);
        dl->PathClear();
        dl->PathArcTo(center, outerR, -1.42f * open, 1.42f * open, 48);
        dl->PathFillConvex(IM_COL32(9, 13, 18, static_cast<int>(225.0f * open)));
        dl->PathClear();
        dl->PathArcTo(center, outerR, -1.42f * open, 1.42f * open, 48);
        dl->PathStroke(IM_COL32(226, 185, 92, static_cast<int>(190.0f * open)),
                       false, UI(2.0f));
    }

    for (int category = 0; category < categoryCount; ++category) {
        const float radius = radii[category] * anim;
        const char* categoryName = ctx.focusCategoryNames[category]
                                 ? ctx.focusCategoryNames[category] : "";
        const ImVec2 catSize = ImGui::CalcTextSize(categoryName);

        if (contentAlpha > 0.05f) {
            dl->PathClear();
            dl->PathArcTo(center, radius, -arcSpan, arcSpan, 32);
            dl->PathStroke(IM_COL32(160, 132, 70, static_cast<int>(120.0f * open)),
                           false, UI(1.0f));
            dl->AddText(ImVec2(center.x - catSize.x - UI(14.0f), center.y - radius - UI(4.0f)),
                        IM_COL32(190, 168, 108, static_cast<int>(235.0f * contentAlpha)),
                        categoryName);
        }

        if (contentAlpha <= 0.05f) continue;
        const int optionCount = std::clamp(ctx.focusOptionCounts[category], 0, 8);
        ImGui::PushID(category);
        for (int option = 0; option < optionCount; ++option) {
            ImGui::PushID(option);
            const float t = (optionCount <= 1)
                ? 0.0f
                : static_cast<float>(option) / static_cast<float>(optionCount - 1);
            const float angle = -arcSpan + 2.0f * arcSpan * t;
            const ImVec2 pos(center.x + std::cos(angle) * radius - btnW * 0.5f,
                             center.y + std::sin(angle) * radius - btnH * 0.5f);
            const bool isSelected = (ctx.focusSelected[category] == option);
            const char* optionLabel = ctx.focusOptions[category][option]
                                    ? ctx.focusOptions[category][option] : "";

            ImGui::PushStyleColor(ImGuiCol_Button,
                isSelected ? ImVec4(0.63f, 0.48f, 0.19f, 0.96f) : ImVec4(0.08f, 0.11f, 0.15f, 0.88f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                isSelected ? ImVec4(0.72f, 0.56f, 0.24f, 1.0f) : ImVec4(0.24f, 0.30f, 0.36f, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                isSelected ? ImVec4(0.80f, 0.62f, 0.28f, 1.0f) : ImVec4(0.30f, 0.38f, 0.46f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                isSelected ? ImVec4(0.08f, 0.06f, 0.04f, 1.0f) : ImVec4(0.88f, 0.90f, 0.93f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI(5.0f));

            bool clicked = false;
            if (open > 0.72f) {
                ImGui::SetCursorScreenPos(pos);
                clicked = ImGui::Button(optionLabel, ImVec2(btnW, btnH));
            } else {
                ImGui::BeginDisabled();
                ImGui::SetCursorScreenPos(pos);
                ImGui::Button(optionLabel, ImVec2(btnW, btnH));
                ImGui::EndDisabled();
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);

            if (clicked) {
                if (actions.playClick) actions.playClick();
                if (actions.focus3DSelect) actions.focus3DSelect(category, option);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    // 关闭
    if (open > 0.80f) {
        const float closeW = UI(130.0f), closeH = UI(38.0f);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - closeW - UI(28.0f),
                                         wp.y + ws.y - closeH - UI(28.0f)));
        if (ImGui::Button("返回大厅", ImVec2(closeW, closeH))) {
            if (actions.playClick) actions.playClick();
            if (actions.close3DFocus) actions.close3DFocus();
        }
    }
}

// ============================================================================
// 买枪菜单
// ============================================================================

void GameUI::DrawMapSelect(const MapSelectCtx& ctx, const Actions& actions) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("MapSelectRoot", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = viewport->WorkPos;
    const ImVec2 ws = viewport->WorkSize;
    const float vpW = ws.x;
    const float centerX = wp.x + vpW * 0.5f;

    // ---------- 背景：上下渐变 + 底部微光 ----------
    dl->AddRectFilledMultiColor(
        wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
        IM_COL32(10, 14, 20, 255), IM_COL32(10, 14, 20, 255),
        IM_COL32(26, 37, 50, 255), IM_COL32(26, 37, 50, 255));
    // 顶部装饰光带
    dl->AddRectFilledMultiColor(
        ImVec2(wp.x, wp.y), ImVec2(wp.x + ws.x, wp.y + 4.0f),
        IM_COL32(230, 185, 80, 200), IM_COL32(230, 185, 80, 200),
        IM_COL32(230, 185, 80, 0), IM_COL32(230, 185, 80, 0));

    // ---------- 标题 ----------
    DrawImGuiTextCentered(imGuiUI_, "选择地图", wp.y + UI(42.0f), 240, 246, 255, 255, 32);
    DrawImGuiTextCentered(imGuiUI_, "爆破模式 · 选择本局战场", wp.y + UI(88.0f),
                          150, 178, 200, 255, 18);
    dl->AddRectFilled(ImVec2(centerX - UI(70.0f), wp.y + UI(120.0f)),
                      ImVec2(centerX + UI(70.0f), wp.y + UI(123.0f)),
                      IM_COL32(230, 185, 80, 220), 1.5f);

    // ---------- 地图卡片 ----------
    const float cardW = UI(340.0f);
    const float cardH = UI(372.0f);
    const float gap = UI(30.0f);
    const float totalW = kMapCatalogCount * cardW + (kMapCatalogCount - 1) * gap;
    const float startX = wp.x + (vpW - totalW) * 0.5f;
    const float cardY = wp.y + UI(158.0f);

    for (int i = 0; i < kMapCatalogCount; ++i) {
        const MapEntry& m = kMapCatalog[i];
        const ImVec2 cp(startX + i * (cardW + gap), cardY);
        const ImVec2 cp2(cp.x + cardW, cp.y + cardH);
        const bool selected = (ctx.selected == i);

        // 交互层：整张卡片一个隐形按钮，避免图像/文字抢焦点
        ImGui::SetCursorScreenPos(cp);
        ImGui::PushID(i);
        ImGui::InvisibleButton("card", ImVec2(cardW, cardH));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            if (actions.playClick) actions.playClick();
            if (actions.selectMap) actions.selectMap(i);
        }
        ImGui::PopID();

        // 卡片底
        dl->AddRectFilled(cp, cp2,
                          selected ? IM_COL32(28, 38, 50, 245) : IM_COL32(18, 24, 32, 235),
                          10.0f);
        // 悬停微亮
        if (hovered && !selected) {
            dl->AddRectFilled(cp, cp2, IM_COL32(255, 255, 255, 12), 10.0f);
        }

        // 预览图
        const float pad = UI(12.0f);
        const ImVec2 img0(cp.x + pad, cp.y + pad);
        const ImVec2 img1(cp2.x - pad, cp.y + UI(178.0f));
        if (ctx.previewTex[i]) {
            dl->AddImage(reinterpret_cast<ImTextureID>(ctx.previewTex[i]), img0, img1);
        } else {
            dl->AddRectFilled(img0, img1, IM_COL32(34, 44, 56, 255), 6.0f);
        }
        if (selected && actions.setMapPreviewRect) {
            actions.setMapPreviewRect(img0.x, img0.y, img1.x, img1.y);
        }
        // 预览图暗角 + 描边
        dl->AddRect(img0, img1, IM_COL32(255, 255, 255, 28), 6.0f, 0, 1.0f);

        // 地图名
        DrawImGuiText(imGuiUI_, m.name, cp.x + pad + 2.0f, cp.y + 192.0f,
                      236, 242, 250, 255, 24);
        // 一句话描述
        DrawImGuiText(imGuiUI_, m.tagline, cp.x + pad + 2.0f, cp.y + 228.0f,
                      150, 176, 196, 255, 15);

        // 元信息：包点 / 区域
        char meta[96];
        snprintf(meta, sizeof(meta), "包点 %d    战术区域 %d", m.bombSites, m.areas);
        DrawImGuiText(imGuiUI_, meta, cp.x + pad + 2.0f, cp.y + 258.0f,
                      130, 160, 185, 255, 14);

        // 选中标识
        if (selected) {
            dl->AddRect(cp, cp2, IM_COL32(235, 190, 85, 255), 10.0f, 0, 3.0f);
            dl->AddRectFilled(ImVec2(cp.x + pad, cp2.y - UI(34.0f)),
                              ImVec2(cp.x + pad + UI(58.0f), cp2.y - UI(14.0f)),
                              IM_COL32(235, 190, 85, 235), 4.0f);
            DrawImGuiText(imGuiUI_, "已选", cp.x + pad + UI(12.0f), cp2.y - UI(32.0f),
                          20, 24, 30, 255, 14);
        } else if (hovered) {
            dl->AddRect(cp, cp2, IM_COL32(190, 210, 230, 160), 10.0f, 0, 1.5f);
        } else {
            dl->AddRect(cp, cp2, IM_COL32(255, 255, 255, 26), 10.0f, 0, 1.0f);
        }
    }

    // ---------- 底部操作区 ----------
    const float btnY = cardY + cardH + UI(34.0f);
    const float btnW = UI(190.0f);
    const float btnH = UI(44.0f);

    ImGui::SetCursorScreenPos(ImVec2(centerX - btnW - UI(14.0f), btnY));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.72f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.82f, 0.42f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.62f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
    if (ImGui::Button("开始对局", ImVec2(btnW, btnH))) {
        if (actions.playClick) actions.playClick();
        if (actions.startMatchOnMap) actions.startMatchOnMap(ctx.selected);
    }
    ImGui::PopStyleColor(4);

    ImGui::SetCursorScreenPos(ImVec2(centerX + UI(14.0f), btnY));
    if (ImGui::Button("返回主菜单", ImVec2(btnW, btnH))) {
        if (actions.playClick) actions.playClick();
        if (actions.goState) actions.goState(MAIN_MENU);
    }

    // ---------- 快捷键提示 ----------
    char hint[128];
    snprintf(hint, sizeof(hint), "← → 切换地图      Enter 开始      Esc 返回      (%d / %d)",
             ctx.selected + 1, kMapCatalogCount);
    DrawImGuiTextCentered(imGuiUI_, hint, btnY + btnH + UI(18.0f), 118, 142, 164, 235, 15);

    // ---------- 键盘操作 ----------
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && ctx.selected > 0) {
        if (actions.selectMap) actions.selectMap(ctx.selected - 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && ctx.selected < kMapCatalogCount - 1) {
        if (actions.selectMap) actions.selectMap(ctx.selected + 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        if (actions.startMatchOnMap) actions.startMatchOnMap(ctx.selected);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (actions.goState) actions.goState(MAIN_MENU);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void GameUI::DrawHistory(const HistoryCtx& ctx, const Actions& actions) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("HistoryRoot", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = viewport->WorkPos;
    const ImVec2 ws = viewport->WorkSize;
    const float vpH = ws.y;

    DrawPageBackdrop(dl, wp, ws);

    const int count = ctx.records ? static_cast<int>(ctx.records->size()) : 0;
    int wins = 0, kills = 0, deaths = 0;
    if (ctx.records) {
        for (const MatchRecord& r : *ctx.records) {
            if (r.result == "win") ++wins;
            kills  += r.kills;
            deaths += r.deaths;
        }
    }
    const float winRate = (count > 0) ? (100.0f * static_cast<float>(wins) / static_cast<float>(count)) : 0.0f;
    const float kd = (deaths > 0) ? (static_cast<float>(kills) / static_cast<float>(deaths))
                                  : static_cast<float>(kills);

    char sub[128];
    snprintf(sub, sizeof(sub), "最近 %d 场对局 · 最多保留 50 场", count);
    DrawPageHeader(imGuiUI_, dl, wp, ws, "CS2D / MATCH LOG", "历史记录", sub, "ESC  返回");

    const float panelW = std::min(UI(1000.0f), ws.x - UI(120.0f));
    const float panelX = wp.x + (ws.x - panelW) * 0.5f;
    float y = wp.y + UI(146.0f);

    // ---- 统计卡 ----
    {
        const float gap = UI(16.0f);
        const float chipW = (panelW - gap * 3.0f) / 4.0f;
        char v0[32], v1[32], v2[32], v3[32];
        snprintf(v0, sizeof(v0), "%d", count);
        snprintf(v1, sizeof(v1), "%d", wins);
        snprintf(v2, sizeof(v2), "%.0f%%", winRate);
        snprintf(v3, sizeof(v3), "%.2f", kd);
        DrawStatChip(imGuiUI_, dl, panelX, y, chipW, UI(72.0f), v0, "总场次", Palette::U32(Palette::Accent()));
        DrawStatChip(imGuiUI_, dl, panelX + chipW + gap, y, chipW, UI(72.0f), v1, "胜场", Palette::U32(Palette::Ally()));
        DrawStatChip(imGuiUI_, dl, panelX + (chipW + gap) * 2.0f, y, chipW, UI(72.0f), v2, "胜率", Palette::U32(Palette::Accent()));
        DrawStatChip(imGuiUI_, dl, panelX + (chipW + gap) * 3.0f, y, chipW, UI(72.0f), v3, "平均 K / D", IM_COL32(120, 170, 220, 255));
        y += UI(72.0f) + UI(18.0f);
    }

    // ---- 列表 ----
    const float listH = std::max(UI(200.0f), (wp.y + vpH - UI(86.0f)) - y);
    ImGui::SetCursorScreenPos(ImVec2(panelX, y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.047f, 0.063f, 0.082f, 0.94f));
    ImGui::BeginChild("##historyList", ImVec2(panelW, listH), false, 0);

    const float rowH = UI(56.0f);
    const float colX[8] = {
        UI(22.0f), UI(200.0f), UI(330.0f), UI(450.0f),
        UI(560.0f), UI(660.0f), UI(740.0f), panelW - UI(112.0f)
    };

    if (count == 0) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        DrawImGuiText(imGuiUI_, "还没有对局记录", p.x + panelW * 0.5f - UI(80.0f),
                      p.y + UI(110.0f), 150, 170, 190, 255, 20);
        DrawImGuiText(imGuiUI_, "完成一场比赛后，结果会整理在这里",
                      p.x + panelW * 0.5f - UI(120.0f), p.y + UI(146.0f),
                      110, 130, 150, 220, 14);
        ImGui::Dummy(ImVec2(panelW, UI(220.0f)));
    } else {
        // 表头
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + panelW, p.y + UI(36.0f)),
                              IM_COL32(9, 14, 20, 220));
            dl->AddLine(ImVec2(p.x, p.y + UI(36.0f)), ImVec2(p.x + panelW, p.y + UI(36.0f)),
                        IM_COL32(58, 76, 94, 140));
            const char* heads[8] = { "时间", "模式", "地图", "比分", "K / D", "回合", "时长", "结果" };
            for (int c = 0; c < 8; ++c) {
                DrawImGuiText(imGuiUI_, heads[c], p.x + colX[c], p.y + UI(10.0f),
                              118, 138, 158, 255, 12);
            }
            ImGui::Dummy(ImVec2(panelW, UI(36.0f)));
        }

        // 最新在上
        for (int i = count - 1; i >= 0; --i) {
            const MatchRecord& r = (*ctx.records)[static_cast<size_t>(i)];
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool hovered = ImGui::IsMouseHoveringRect(
                p, ImVec2(p.x + panelW, p.y + rowH));
            const bool win  = (r.result == "win");
            const bool lose = (r.result == "lose");
            const ImU32 accent = win ? Palette::U32(Palette::Ally())
                              : lose ? Palette::U32(Palette::Danger())
                                     : IM_COL32(120, 138, 156, 255);

            if ((i % 2) == 0) {
                dl->AddRectFilled(p, ImVec2(p.x + panelW, p.y + rowH), IM_COL32(255, 255, 255, 6));
            }
            if (hovered) {
                dl->AddRectFilled(p, ImVec2(p.x + panelW, p.y + rowH), IM_COL32(120, 160, 200, 22));
            }
            dl->AddRectFilled(ImVec2(p.x, p.y + UI(12.0f)),
                              ImVec2(p.x + UI(3.0f), p.y + rowH - UI(12.0f)), accent);
            dl->AddLine(ImVec2(p.x, p.y + rowH), ImVec2(p.x + panelW, p.y + rowH),
                        IM_COL32(60, 78, 96, 90));

            DrawImGuiText(imGuiUI_, r.time.c_str(), p.x + colX[0], p.y + UI(18.0f),
                          150, 168, 186, 255, 14);
            DrawImGuiText(imGuiUI_, MatchHistory::ModeLabel(r.modeId), p.x + colX[1], p.y + UI(16.0f),
                          214, 224, 236, 255, 15);
            DrawImGuiText(imGuiUI_, MatchHistory::MapLabel(r.mapId), p.x + colX[2], p.y + UI(16.0f),
                          168, 186, 204, 255, 15);

            char score[32];
            snprintf(score, sizeof(score), "%d : %d", r.scoreT, r.scoreCT);
            DrawImGuiText(imGuiUI_, score, p.x + colX[3], p.y + UI(16.0f),
                          win ? 150 : (lose ? 210 : 180),
                          win ? 210 : (lose ? 110 : 180),
                          win ? 160 : (lose ? 110 : 180), 255, 16);

            char kdText[32];
            snprintf(kdText, sizeof(kdText), "%d / %d", r.kills, r.deaths);
            DrawImGuiText(imGuiUI_, kdText, p.x + colX[4], p.y + UI(16.0f),
                          200, 210, 222, 255, 15);

            char rounds[16];
            snprintf(rounds, sizeof(rounds), "%d", r.rounds);
            DrawImGuiText(imGuiUI_, rounds, p.x + colX[5], p.y + UI(16.0f),
                          168, 186, 204, 255, 15);

            char dur[16];
            snprintf(dur, sizeof(dur), "%02d:%02d", r.durationSec / 60, r.durationSec % 60);
            DrawImGuiText(imGuiUI_, dur, p.x + colX[6], p.y + UI(16.0f),
                          150, 168, 186, 255, 15);

            // 结果胶囊
            {
                const char* label = MatchHistory::ResultLabel(r.result);
                const float pillW = UI(64.0f), pillH = UI(26.0f);
                const ImVec2 a(p.x + colX[7], p.y + UI(15.0f));
                dl->AddRectFilled(a, ImVec2(a.x + pillW, a.y + pillH),
                                  IM_COL32(win ? 46 : lose ? 60 : 40,
                                           win ? 74 : lose ? 30 : 44,
                                           win ? 52 : lose ? 30 : 52, 160), UI(13.0f));
                dl->AddRect(a, ImVec2(a.x + pillW, a.y + pillH), accent, UI(13.0f), 0, UI(1.0f));
                DrawImGuiText(imGuiUI_, label, a.x + UI(14.0f), a.y + UI(5.0f),
                              win ? 170 : lose ? 240 : 190,
                              win ? 230 : lose ? 140 : 190,
                              win ? 180 : lose ? 140 : 190, 255, 13);
            }

            ImGui::Dummy(ImVec2(panelW, rowH));
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---- 页脚 ----
    ImGui::SetCursorScreenPos(ImVec2(wp.x + UI(72.0f), wp.y + vpH - UI(64.0f)));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.14f, 0.19f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.20f, 0.26f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.19f, 0.25f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.34f, 0.42f, 0.50f, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, UI(1.0f));
    if (ImGui::Button("返回主菜单", ImVec2(UI(160.0f), UI(42.0f)))) {
        if (actions.playClick) actions.playClick();
        if (actions.goState) actions.goState(MAIN_MENU);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    if (count > 0) {
        char foot[96];
        snprintf(foot, sizeof(foot), "共 %d 场 · 胜率 %.0f%%", count, winRate);
        DrawImGuiText(imGuiUI_, foot, wp.x + ws.x - UI(72.0f) - UI(190.0f),
                      wp.y + vpH - UI(46.0f), 112, 132, 152, 210, 12);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (actions.goState) actions.goState(MAIN_MENU);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// 购买失败码 → 中文文案（契约 v1 第 3 节：执行期失败也必须显示对应原因）
static const char* BuyCodeText(Presentation::BuyCode code) {
    switch (code) {
        case Presentation::BuyCode::Ok:                return "";
        case Presentation::BuyCode::InvalidItem:       return "该物品不可购买";
        case Presentation::BuyCode::PlayerDead:        return "阵亡状态下无法购买";
        case Presentation::BuyCode::WindowClosed:      return "当前不是购买阶段";
        case Presentation::BuyCode::OutsideBuyZone:    return "不在购买区内";
        case Presentation::BuyCode::TeamRestricted:    return "本阵营无法购买该武器";
        case Presentation::BuyCode::InsufficientFunds: return "余额不足";
        case Presentation::BuyCode::InventoryFull:     return "投掷物已满 4 个";
        case Presentation::BuyCode::ItemLimitReached:  return "该投掷物已达携带上限";
        case Presentation::BuyCode::AlreadyEquipped:   return "已装备";
    }
    return "无法购买";
}

void GameUI::DrawBuyMenu(const BuyMenuCtx& ctx,
    const std::function<Presentation::BuyResult(const Presentation::BuyRequest&)>& onBuy) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float w = UI(600.0f);
    // 高度跟着视口走，小窗口也不会被裁掉；内容超出时右侧出现滚动条
    const float h = std::min(UI(470.0f), viewport->WorkSize.y - UI(48.0f));
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - w) * 0.5f,
                                  viewport->WorkPos.y + (viewport->WorkSize.y - h) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.059f, 0.078f, 0.97f));
    ImGui::Begin("##BuyMenu", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // 一帧最多提交一次购买请求：提交后本帧其余条目只显示旧快照
    bool submittedThisFrame = false;
    int  displayMoney = ctx.money;
    // 上一条结果提示（UI 自己保存，不写 CS2DGame）
    static std::string flashText;
    static float       flashUntil = -1.0f;

    ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.35f, 1.0f), "购买菜单");
    ImGui::SameLine(0.0f, UI(24.0f));
    ImGui::Text("余额: $%d", displayMoney);
    ImGui::SameLine(0.0f, UI(24.0f));
    ImGui::TextDisabled("[B] 关闭");

    if (!ctx.canBuy && !ctx.hint.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s", ctx.hint.c_str());
    }
    if (!flashText.empty() && ImGui::GetTime() < flashUntil) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "%s", flashText.c_str());
    }
    ImGui::Separator();

    static const char* kGroupNames[] = { "主武器", "副武器", "近战", "投掷物", "护甲" };
    for (int g = 0; g < 5; ++g) {
        bool headerDrawn = false;
        int  col = 0;
        for (const auto& it : ctx.items) {
            if (it.group != g) continue;

            if (!headerDrawn) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.95f, 1.0f), "%s", kGroupNames[g]);
                headerDrawn = true;
                col = 0;
            }

            // 禁用判定以新字段 availability 为准（canBuy/affordable/owned 仅兼容）
            const bool enabled = ctx.canBuy && !submittedThisFrame &&
                                 it.availability == Presentation::BuyCode::Ok;
            const char* reason = BuyCodeText(it.availability);

            if (!enabled) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.14f, 0.17f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.14f, 0.17f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.12f, 0.14f, 0.17f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.55f, 0.58f, 0.62f, 1.0f));
            }

            char label[160];
            snprintf(label, sizeof(label), "%s  $%d%s", it.name.c_str(), it.price,
                     (it.availability == Presentation::BuyCode::AlreadyEquipped) ? " (已装备)"
                     : (it.availability == Presentation::BuyCode::ItemLimitReached) ? " (已带满)"
                     : "");
            if (ImGui::Button(label, ImVec2(UI(182.0f), 0.0f)) && onBuy) {
                const Presentation::BuyResult res = onBuy(it.request);
                submittedThisFrame = true;
                if (res.code == Presentation::BuyCode::Ok) {
                    displayMoney = res.moneyAfter;
                    flashText = std::string(it.name) + " 已购买";
                    flashUntil = static_cast<float>(ImGui::GetTime()) + 1.6f;
                } else {
                    flashText = std::string(it.name) + ": " + BuyCodeText(res.code);
                    flashUntil = static_cast<float>(ImGui::GetTime()) + 2.2f;
                }
            }
            // disabled 项也允许悬停读原因
            if (!enabled && !reason[0] && ctx.hint.size()) reason = ctx.hint.c_str();
            if (!enabled && reason[0] &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", reason);
            }

            if (!enabled) ImGui::PopStyleColor(4);

            ++col;
            if (col % 3 != 0) ImGui::SameLine();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

// ============================================================================
// 游戏内 HUD
// ============================================================================

void GameUI::DrawSideSelect(const SideSelectCtx& ctx, const Actions& actions) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("##sideSelectRoot", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawMenuBackdrop(dl, vp->WorkPos, vp->WorkSize);

    const float cx = vp->WorkPos.x + vp->WorkSize.x * 0.5f;
    DrawImGuiTextCentered(imGuiUI_, "选择阵营", vp->WorkPos.y + UI(48.0f), 240, 246, 255, 255, 32);
    DrawTitleAccent(dl, cx, vp->WorkPos.y + UI(94.0f), UI(50.0f));

    if (!ctx.mapName.empty()) {
        DrawImGuiTextCentered(imGuiUI_, ctx.mapName.c_str(),
                              vp->WorkPos.y + UI(106.0f), 150, 178, 200, 255, 16);
    }

    const float cardW = UI(300.0f);
    const float cardH = UI(300.0f);
    const float gap   = UI(40.0f);
    const float yTop  = vp->WorkPos.y + (vp->WorkSize.y - cardH) * 0.42f;
    const float xLeft = cx - cardW - gap * 0.5f;

    struct Card { const char* title; const char* sub; const char* desc; ImU32 accent; int side; };
    const Card cards[2] = {
        { "T  恐怖分子", "进攻方", "携带 C4 · 负责下包\n拿到炸弹后按住 E 在包点安装", IM_COL32(240, 170, 70, 255), 0 },
        { "CT  反恐精英", "防守方", "阻止下包 / 拆除 C4\n在包点按住 E 拆弹", IM_COL32(90, 170, 245, 255), 1 },
    };

    for (int i = 0; i < 2; ++i) {
        const Card& c = cards[i];
        ImGui::SetCursorScreenPos(ImVec2(xLeft + i * (cardW + gap), yTop));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.075f, 0.100f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(c.accent));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, UI(2.0f));
        ImGui::BeginChild(i == 0 ? "##sideT" : "##sideCT", ImVec2(cardW, cardH), true);

        ImGui::Dummy(ImVec2(0.0f, UI(24.0f)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(c.accent));
        if (ImFont* f = imGuiUI_ ? imGuiUI_->GetFont(static_cast<int>(UI(26.0f))) : nullptr)
            ImGui::PushFont(f);
        ImGui::TextUnformatted(c.title);
        if (imGuiUI_ && imGuiUI_->GetFont(static_cast<int>(UI(26.0f)))) ImGui::PopFont();
        ImGui::PopStyleColor();

        if (ImFont* f2 = imGuiUI_ ? imGuiUI_->GetFont(static_cast<int>(UI(16.0f))) : nullptr)
            ImGui::PushFont(f2);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette::TextDim());
        ImGui::TextUnformatted(c.sub);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, UI(12.0f)));
        ImGui::PushStyleColor(ImGuiCol_Text, Palette::Text());
        ImGui::TextWrapped("%s", c.desc);
        ImGui::PopStyleColor();
        if (imGuiUI_ && imGuiUI_->GetFont(static_cast<int>(UI(16.0f)))) ImGui::PopFont();

        ImGui::SetCursorPosY(cardH - UI(72.0f));
        if (ImGui::Button(i == 0 ? "以 T 开始" : "以 CT 开始",
                          ImVec2(cardW - UI(28.0f), UI(48.0f)))) {
            if (actions.playClick) actions.playClick();
            if (actions.chooseSide) actions.chooseSide(c.side);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    // 返回
    ImGui::SetCursorScreenPos(ImVec2(cx - UI(110.0f), vp->WorkPos.y + vp->WorkSize.y - UI(80.0f)));
    if (ImGui::Button("返回地图选择", ImVec2(UI(220.0f), UI(46.0f)))) {
        if (actions.playClick) actions.playClick();
        if (actions.goState) actions.goState(MAP_SELECT);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void GameUI::DrawSidePopup(const SideSelectCtx& ctx, const Actions& actions,
                           float alpha) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    // 游戏画面仍然可见，只压暗；弹窗在窗口层，HUD 在弹窗期间隐藏。
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x,
                            vp->WorkPos.y + vp->WorkSize.y),
        IM_COL32(4, 7, 10, static_cast<int>(178.0f * alpha)));

    const float w = UI(680.0f);
    const float h = UI(330.0f);
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.059f, 0.078f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.90f, 0.72f, 0.30f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI(1.5f));
    ImGui::Begin("##sidePopup", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

    const char* title = "选择阵营";
    ImFont* titleFont = imGuiUI_ ? imGuiUI_->GetFont(static_cast<int>(UI(28.0f))) : nullptr;
    if (titleFont) ImGui::PushFont(titleFont);
    const float titleW = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPos(ImVec2((w - titleW) * 0.5f, UI(20.0f)));
    ImGui::TextColored(ImVec4(0.96f, 0.93f, 0.82f, 1.0f), "%s", title);
    if (titleFont) ImGui::PopFont();

    if (!ctx.mapName.empty()) {
        const std::string sub = "地图：" + ctx.mapName;
        const float subW = ImGui::CalcTextSize(sub.c_str()).x;
        ImGui::SetCursorPos(ImVec2((w - subW) * 0.5f, UI(62.0f)));
        ImGui::TextDisabled("%s", sub.c_str());
    }

    const float btnW = (w - UI(72.0f)) * 0.5f;
    const float btnH = UI(112.0f);
    const float btnY = UI(116.0f);

    auto sideCard = [&](int side, const char* name, const char* role,
                        const char* desc, ImVec4 accent, float x) {
        ImGui::SetCursorPos(ImVec2(x, btnY));
        ImGui::BeginGroup();
        ImGui::TextColored(accent, "%s", name);
        ImGui::TextDisabled("%s", role);
        ImGui::TextWrapped("%s", desc);
        ImGui::Dummy(ImVec2(0.0f, UI(8.0f)));
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(accent.x + 0.08f, accent.y + 0.08f, accent.z + 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.08f, 0.10f, 1.0f));
        char label[64];
        snprintf(label, sizeof(label), "以 %s 开始", side == 0 ? "T" : "CT");
        if (ImGui::Button(label, ImVec2(btnW, UI(40.0f)))) {
            if (actions.playClick) actions.playClick();
            if (actions.chooseSide) actions.chooseSide(side);
        }
        ImGui::PopStyleColor(4);
        ImGui::EndGroup();
    };

    sideCard(0, "T  恐怖分子", "进攻方 · 携带 C4",
             "拿到炸弹后按住 E 在包点安装", ImVec4(0.94f, 0.67f, 0.27f, 1.0f),
             UI(24.0f));
    sideCard(1, "CT  反恐精英", "防守方 · 拆除 C4",
             "阻止下包，或在包点按住 E 拆弹", ImVec4(0.35f, 0.67f, 0.96f, 1.0f),
             UI(24.0f) + btnW + UI(24.0f));

    const float rndW = UI(190.0f);
    const float rndH = UI(36.0f);
    ImGui::SetCursorPos(ImVec2((w - rndW) * 0.5f, UI(246.0f)));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.22f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.33f, 0.41f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.18f, 0.23f, 1.0f));
    if (ImGui::Button("随机分配", ImVec2(rndW, rndH))) {
        if (actions.playClick) actions.playClick();
        if (actions.chooseSide) actions.chooseSide(-1);
    }
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPos(ImVec2(0.0f, h - UI(22.0f)));
    const char* hint = "选择后将重建首回合（不重新加载地图）";
    const float hintW = ImGui::CalcTextSize(hint).x;
    ImGui::SetCursorPosX((w - hintW) * 0.5f);
    ImGui::TextDisabled("%s", hint);

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void GameUI::DrawLoadingOverlay(const char* title, const char* subtitle) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float cx = vp->WorkPos.x + vp->WorkSize.x * 0.5f;
    const float cy = vp->WorkPos.y + vp->WorkSize.y * 0.5f;

    DrawImGuiTextCentered(imGuiUI_, title, cy - UI(20.0f),
                          232, 238, 234, 255, 24);
    DrawImGuiTextCentered(imGuiUI_, subtitle, cy + UI(18.0f),
                          145, 170, 190, 240, 15);

    // 缓慢扫过的金色细线，避免黑屏看起来像卡死
    const float barW = UI(210.0f);
    const float barX = cx - barW * 0.5f;
    const float barY = cy + UI(44.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + UI(3.0f)),
                      IM_COL32(255, 255, 255, 26), UI(2.0f));

    const float segW = UI(64.0f);
    const float p = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.65f, 1.0f);
    const float segX = barX + (barW + segW) * p - segW;
    const float x0 = std::max(barX, segX);
    const float x1 = std::min(barX + barW, segX + segW);
    if (x1 > x0) {
        dl->AddRectFilled(ImVec2(x0, barY), ImVec2(x1, barY + UI(3.0f)),
                          IM_COL32(230, 185, 80, 220), UI(2.0f));
    }
}

void GameUI::DrawInGame(const InGameCtx& ctx, ImGuiUI* imGuiUI) {
    if (ctx.state != PLAYING) return;

    const float vpW = ImGui::GetIO().DisplaySize.x;
    const float vpH = ImGui::GetIO().DisplaySize.y;
    ImDrawList* fg = ImGui::GetForegroundDrawList();

    // 顶部计分条背板：让分数/时间从"漂浮文字"变成一块 HUD 面板
    {
        const float pw = UI(430.0f);
        const float ph = UI(74.0f);
        const float px = (vpW - pw) * 0.5f;
        const float py = UI(4.0f);
        fg->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph),
                          IM_COL32(8, 12, 17, 170), UI(6.0f));
        fg->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph),
                    IM_COL32(210, 172, 96, 120), UI(6.0f), 0, std::max(1.0f, UI(1.5f)));
        // 顶部一条细金线，呼应菜单风格
        fg->AddRectFilled(ImVec2(px + UI(10.0f), py + UI(1.0f)),
                          ImVec2(px + pw - UI(10.0f), py + std::max(2.0f, UI(3.0f))),
                          IM_COL32(230, 185, 90, 150));
    }

    // 顶部计分板：T 比分 : 比分 CT ｜ 回合 ｜ 倒计时
    {
        char left[64];
        char mid[64];
        char alive[64];
        snprintf(left, sizeof(left), "T  %d : %d  CT", ctx.scoreT, ctx.scoreCT);
        snprintf(alive, sizeof(alive), "%d vs %d  ", ctx.aliveT, ctx.aliveCT);
        snprintf(mid, sizeof(mid), "第 %d 回合   %02d:%02d",
                 ctx.roundNumber,
                 ctx.roundRemainingSec / 60, ctx.roundRemainingSec % 60);

        DrawImGuiTextCentered(imGuiUI, mid, UI(12.0f), 180, 200, 215, 255, 18);
        DrawImGuiTextCentered(imGuiUI, left, UI(40.0f), 235, 235, 235, 255, 22);
        DrawImGuiTextCentered(imGuiUI, alive, UI(64.0f), 160, 175, 185, 255, 16);
    }

    // ---- 左上角雷达下方：教练计划 ----
    if (!ctx.coachReason.empty()) {
        char buf[256];
        snprintf(buf, sizeof(buf), "教练: %s", ctx.coachReason.c_str());
        if (ctx.coachFlash) {
            DrawImGuiText(imGuiUI, buf, UI(12.0f), ImGui::GetIO().DisplaySize.y - UI(118.0f),
                          255, 245, 170, 255, 17);   // 刚变更：更亮更大
        } else {
            DrawImGuiText(imGuiUI, buf, UI(12.0f), ImGui::GetIO().DisplaySize.y - UI(118.0f),
                          255, 225, 140, 235, 15);
        }
    }

    // ---- 左下角血量条 ----
    {
        const float barX = UI(16.0f);
        const float barY = ImGui::GetIO().DisplaySize.y - UI(52.0f);
        const float barW = UI(200.0f);
        const float barH = UI(18.0f);

        const float frac = (ctx.maxHealth > 0)
            ? std::clamp(static_cast<float>(ctx.health) / static_cast<float>(ctx.maxHealth), 0.0f, 1.0f)
            : 0.0f;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        // 底框
        dl->AddRectFilled(ImVec2(barX - 2, barY - 2),
                          ImVec2(barX + barW + 2, barY + barH + 2),
                          IM_COL32(10, 14, 18, 210), 4.0f);
        dl->AddRect(ImVec2(barX - 2, barY - 2),
                    ImVec2(barX + barW + 2, barY + barH + 2),
                    IM_COL32(200, 155, 60, 200), 4.0f);

        // 血量填充：高血量绿、中黄、低红
        ImU32 fill = (frac > 0.6f) ? IM_COL32(70, 190, 90, 235)
                   : (frac > 0.3f) ? IM_COL32(225, 180, 60, 235)
                                   : IM_COL32(215, 65, 60, 235);
        dl->AddRectFilled(ImVec2(barX, barY),
                          ImVec2(barX + barW * frac, barY + barH), fill, 3.0f);

        char hp[32];
        snprintf(hp, sizeof(hp), "HP %d / %d", ctx.health, ctx.maxHealth);
        DrawImGuiText(imGuiUI, hp, barX + UI(6.0f), barY - UI(20.0f),
                      235, 240, 235, 255, 18);

        // 护甲条（与血条同样式，紧贴其下）
        const float armorY = barY + barH + UI(6.0f);
        const float armorFrac = std::clamp(static_cast<float>(ctx.armor) / 100.0f, 0.0f, 1.0f);

        dl->AddRectFilled(ImVec2(barX - 2, armorY - 2),
                          ImVec2(barX + barW + 2, armorY + barH + 2),
                          IM_COL32(10, 14, 18, 210), 4.0f);
        dl->AddRect(ImVec2(barX - 2, armorY - 2),
                    ImVec2(barX + barW + 2, armorY + barH + 2),
                    IM_COL32(110, 170, 220, 200), 4.0f);
        dl->AddRectFilled(ImVec2(barX, armorY),
                          ImVec2(barX + barW * armorFrac, armorY + barH),
                          IM_COL32(90, 160, 225, 235), 3.0f);

        char ar[48];
        snprintf(ar, sizeof(ar), "护甲 %d%s", ctx.armor, ctx.helmet ? " (盔)" : "");
        DrawImGuiText(imGuiUI, ar, barX + barW + UI(12.0f), armorY + 1.0f,
                      190, 215, 240, 255, 16);
    }

    // 屏幕中央提示：回合开始 / 回合胜负（带淡出）
    if (!ctx.announcement.empty() && ctx.announcementAlpha > 0.0f) {
        const Uint8 a = static_cast<Uint8>(std::clamp(ctx.announcementAlpha, 0.0f, 1.0f) * 255.0f);
        DrawImGuiTextCentered(imGuiUI, ctx.announcement.c_str(), ImGui::GetIO().DisplaySize.y * 0.36f,
                              255, 236, 150, a, 34);
    }

    // 比赛结束提示
    if (ctx.matchOver) {
        DrawImGuiTextCentered(imGuiUI,
            ctx.matchWinnerIsCT ? "比赛结束  CT 获胜" : "比赛结束  T 获胜",
            ImGui::GetIO().DisplaySize.y * 0.42f, 255, 220, 90, 255, 30);
        if (ctx.matchOverReturn >= 0.0f) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d 秒后返回主菜单",
                     static_cast<int>(ctx.matchOverReturn) + 1);
            DrawImGuiTextCentered(imGuiUI, buf, ImGui::GetIO().DisplaySize.y * 0.42f + UI(40.0f), 200, 210, 220, 255, 20);
        }
    }

    // ---- 目标物状态 ----
    // 目标状态背板（没下包时不显示，避免空条）
    if ((ctx.objective == 1 && ctx.bombPlanted) || ctx.objective == 2) {
        const float pw = UI(260.0f);
        const float ph = UI(48.0f);
        const float px = (vpW - pw) * 0.5f;
        const float py = UI(84.0f);
        fg->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph),
                          IM_COL32(8, 12, 17, 150), UI(20.0f));
        fg->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph),
                    IM_COL32(220, 80, 70, 140), UI(20.0f), 0, std::max(1.0f, UI(1.5f)));
    }

    if (ctx.objective == 1) {          // 爆破
        if (ctx.bombPlanted) {
            char buf[64];
            snprintf(buf, sizeof(buf), "C4 倒计时 %d 秒", static_cast<int>(ctx.bombTimer));
            DrawImGuiTextCentered(imGuiUI, buf, UI(92.0f), 255, 90, 90, 255, 22);
            if (ctx.defuseProgress > 0.0f) {
                char db[64];
                snprintf(db, sizeof(db), "拆包中 %d%%", static_cast<int>(ctx.defuseProgress * 100));
                DrawImGuiTextCentered(imGuiUI, db, UI(118.0f), 120, 220, 255, 255, 18);
            }
        }
    }

    // ---- 击杀播报（右上角）----
    {
        // 背板：只在有内容时画，避免空面板
        if (!ctx.killFeed.empty()) {
            const float rows = static_cast<float>(ctx.killFeed.size());
            const float pw = UI(320.0f);
            const float ph = rows * UI(20.0f) + UI(10.0f);
            const float px = vpW - pw - UI(8.0f);
            const float py = UI(88.0f);
            fg->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph),
                              IM_COL32(8, 12, 17, 130), UI(5.0f));
            fg->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph),
                        IM_COL32(255, 255, 255, 20), UI(5.0f));
        }
        const float x = ImGui::GetIO().DisplaySize.x - UI(320.0f);
        float y = UI(96.0f);
        for (const auto& line : ctx.killFeed) {
            DrawImGuiText(imGuiUI, line.c_str(), x, y, 235, 235, 235, 235, 16);
            y += UI(20.0f);
        }
    }

    // ---- 记分板（Tab 按住）----
    if (ctx.showScoreboard) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float w = std::min(UI(760.0f), vp->WorkSize.x - UI(40.0f));
        const float h = std::min(UI(560.0f), vp->WorkSize.y - UI(40.0f));
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f,
                                      vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.059f, 0.078f, 0.97f));
        ImGui::Begin("##Scoreboard", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // 顶部分数
        char title[64];
        snprintf(title, sizeof(title), "T  %d : %d  CT", ctx.scoreT, ctx.scoreCT);
        ImGui::TextColored(Palette::Accent(), "%s", title);
        ImGui::Separator();

        // 列宽（设计基准，随 UI 缩放）
        const float colName  = UI(210.0f);
        const float colKill  = UI(80.0f);
        const float colDeath = UI(80.0f);
        const float colMoney = UI(110.0f);
        const float colState = UI(90.0f);

        auto drawHeaderRow = [&]() {
            ImGui::TextColored(Palette::TextFaint(), "%s", "名称");
            ImGui::SameLine(colName);
            ImGui::TextColored(Palette::TextFaint(), "%s", "击杀");
            ImGui::SameLine(colName + colKill);
            ImGui::TextColored(Palette::TextFaint(), "%s", "死亡");
            ImGui::SameLine(colName + colKill + colDeath);
            ImGui::TextColored(Palette::TextFaint(), "%s", "余额");
            ImGui::SameLine(colName + colKill + colDeath + colMoney);
            ImGui::TextColored(Palette::TextFaint(), "%s", "状态");
        };

        // 收集两队并按击杀数排序（同分按死亡数少优先）
        std::vector<InGameCtx::ScoreRowView> tRows, ctRows;
        for (const auto& r : ctx.scoreRows) {
            if (r.team == 1) tRows.push_back(r);
            else if (r.team == 2) ctRows.push_back(r);
        }
        auto byScore = [](const InGameCtx::ScoreRowView& a, const InGameCtx::ScoreRowView& b) {
            if (a.kills != b.kills) return a.kills > b.kills;
            if (a.deaths != b.deaths) return a.deaths < b.deaths;
            return a.name < b.name;
        };
        std::sort(tRows.begin(), tRows.end(), byScore);
        std::sort(ctRows.begin(), ctRows.end(), byScore);

        auto drawTeam = [&](const char* label, const std::vector<InGameCtx::ScoreRowView>& rows,
                            ImVec4 color) {
            ImGui::Spacing();
            ImGui::TextColored(color, "%s", label);
            ImGui::Separator();
            drawHeaderRow();
            for (const auto& r : rows) {
                const ImVec4 c = r.alive ? Palette::Text() : Palette::TextFaint();
                ImGui::TextColored(r.isPlayer ? Palette::Ally() : c, "%s%s",
                                   r.name.c_str(), r.isPlayer ? "（你）" : "");
                ImGui::SameLine(colName);
                ImGui::TextColored(c, "%d", r.kills);
                ImGui::SameLine(colName + colKill);
                ImGui::TextColored(c, "%d", r.deaths);
                ImGui::SameLine(colName + colKill + colDeath);
                ImGui::TextColored(c, "$%d", r.money);
                ImGui::SameLine(colName + colKill + colDeath + colMoney);
                if (r.alive) {
                    ImGui::TextColored(Palette::Ally(), "%s", "存活");
                } else {
                    ImGui::TextColored(Palette::Danger(), "%s", "阵亡");
                }
            }
        };

        drawTeam("T", tRows, ImVec4(0.95f, 0.72f, 0.30f, 1.0f));
        drawTeam("CT", ctRows, ImVec4(0.42f, 0.72f, 0.95f, 1.0f));

        ImGui::End();
        ImGui::PopStyleColor();
    }

    // 右下角武器状态文字（与 CS2DRenderer 的 160×56 面板对齐）
    {
        // 与 CS2DRenderer 的面板对齐：面板高 56*scale、右下留 10*scale
        const float vpH = ImGui::GetIO().DisplaySize.y;
        const float panelH = UI(56.0f);
        const float margin = UI(10.0f);
        const float textX = vpW - UI(160.0f) + UI(50.0f);
        const float panelTop = vpH - panelH - margin;

        // 第一行：武器名
        DrawImGuiText(imGuiUI, ctx.weaponName.c_str(),
                      textX, panelTop + UI(7.0f), 225, 232, 228, 255, 15);

        // 第二行：弹夹内子弹数/备弹数（不是弹匣容量）
        if (ctx.magazineSize > 0) {
            const std::string ammoLine =
                std::to_string(ctx.ammo) + "/" + std::to_string(ctx.reserveAmmo);
            Uint8 ar = 255, ag = 226, ab = 150;
            if (ctx.ammo <= 0) {
                ar = 238; ag = 92; ab = 84;          // 空仓：红
            } else if (ctx.ammo * 4 <= ctx.magazineSize) {
                ar = 255; ag = 170; ab = 88;         // 低弹：橙
            }
            DrawImGuiText(imGuiUI, ammoLine.c_str(),
                          textX, panelTop + UI(28.0f), ar, ag, ab, 255, 21);
        } else if (ctx.grenadeCount > 0) {
            // 投掷物：显示背包里还剩几个（按 4 切换）
            const std::string ammoLine = "x" + std::to_string(ctx.grenadeCount);
            DrawImGuiText(imGuiUI, ammoLine.c_str(),
                          textX, panelTop + UI(28.0f), 255, 226, 150, 255, 18);
        }
    }

    // 提示（换弹等）
    if (ctx.hasNoWeaponHint && !ctx.hintText.empty()) {
        DrawImGuiTextCentered(imGuiUI, ctx.hintText.c_str(), 512.0f, 245, 200, 90, 255, 22);
    }


    // 观察者 / C4 底部提示
    {
        const float yBase = ImGui::GetIO().DisplaySize.y - UI(96.0f);
        if (ctx.spectating) {
            if (ctx.spectateMode == 1) {
                DrawImGuiTextCentered(imGuiUI,
                    "自由视角 · [空格] 回到队友视角 · [WASD] 移动镜头",
                    yBase, 200, 214, 230, 235, 16);
            } else {
                char nameBuf[96];
                snprintf(nameBuf, sizeof(nameBuf), "观战：%s",
                         ctx.spectateTarget.empty() ? "-" : ctx.spectateTarget.c_str());
                DrawImGuiTextCentered(imGuiUI, nameBuf, yBase - UI(4.0f),
                                      255, 225, 140, 255, 20);
                DrawImGuiTextCentered(imGuiUI,
                    "[空格] 自由视角 · [左/右键] 切换 BOT",
                    yBase + UI(22.0f), 195, 210, 220, 220, 14);
            }
        } else if (ctx.holdingC4) {
            DrawImGuiTextCentered(imGuiUI, "手持 C4 · [5] 收起 · [G] 丢下",
                                  yBase, 255, 210, 120, 255, 16);
        } else if (ctx.bombDropped) {
            DrawImGuiTextCentered(imGuiUI,
                ctx.bombIntel ? "C4 掉落在已知位置（地图有标记）" : "C4 掉在地上 · 靠近可拾取",
                yBase, 255, 150, 110, 255, 16);
        } else if (ctx.hasC4) {
            if (ctx.plantProgress > 0.0f) {
                char buf[64];
                snprintf(buf, sizeof(buf), "正在安装 C4 … %d%%",
                         static_cast<int>(ctx.plantProgress * 100.0f));
                DrawImGuiTextCentered(imGuiUI, buf, yBase, 255, 190, 90, 255, 18);
            } else {
                DrawImGuiTextCentered(imGuiUI, "你携带 C4 · 在包点按住 E 安装",
                                      yBase, 255, 190, 90, 255, 16);
            }
        }
    }
}

