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
        // 整块内容（标题 + 副标题 + 按钮组 + 战绩）垂直居中
        const float vpH = viewport->WorkSize.y;
        const float itemSpacing = ImGui::GetStyle().ItemSpacing.y;
        const int   nBtn = 7;

        const float titleH = UI(46.0f);
        const float subH   = UI(30.0f);
        const float gap1   = UI(26.0f);
        const float statsH = UI(28.0f);
        const float gap2   = UI(26.0f);
        const float blockH = titleH + subH + gap1 + nBtn * buttonH +
                             (nBtn - 1) * itemSpacing + gap2 + statsH;

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
            // 压暗 + 上下渐变，让文字浮出来
            mdl->AddRectFilledMultiColor(
                viewport->WorkPos, ImVec2(viewport->WorkPos.x + sw, viewport->WorkPos.y + sh),
                Palette::U32(ImVec4(0.02f, 0.03f, 0.045f, 0.88f)),
                Palette::U32(ImVec4(0.02f, 0.03f, 0.045f, 0.88f)),
                Palette::U32(ImVec4(0.03f, 0.045f, 0.06f, 0.94f)),
                Palette::U32(ImVec4(0.03f, 0.045f, 0.06f, 0.94f)));
        }
        DrawMenuBackdrop(mdl, viewport->WorkPos, viewport->WorkSize);
        DrawImGuiTextCentered(imGuiUI_, "CS2D", yScreen, 255, 255, 255, 255, 34);
        DrawTitleAccent(ImGui::GetWindowDrawList(), centerX, yScreen + UI(44.0f), UI(46.0f));
        DrawImGuiTextCentered(imGuiUI_, "Counter-Strike 2D 单机版", yScreen + titleH,
                              160, 190, 210, 255, 20);

        ImGui::SetCursorPosY(yLocal + titleH + subH + gap1);
        if (bigButton("团队歼灭")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::TeamElimination)); });
        if (bigButton("爆破模式")) doClick([&] { actions.goState(MAP_SELECT); });
        if (bigButton("人质营救")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::HostageRescue)); });
        if (bigButton("死斗模式")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::DeathMatch)); });
        if (bigButton("历史记录")) doClick([&] { actions.goState(HISTORY); });
        if (bigButton("设置"))     doClick([&] { actions.openSettings(MAIN_MENU); });
        if (bigButton("退出游戏")) doClick([&] { actions.quitToDesktop(); });

        // 累计战绩（贴在内容块底部）
        char buf[96];
        snprintf(buf, sizeof(buf), "累计击杀 %d   累计阵亡 %d", ctx.statKills, ctx.statDeaths);
        DrawImGuiTextCentered(imGuiUI_, buf, yScreen + blockH - statsH,
                              150, 170, 185, 255, 18);
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
        DrawImGuiTextCentered(imGuiUI_, "当前对局进度将会被保存",
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
        const float vpH = viewport->WorkSize.y;
        DrawImGuiTextCentered(imGuiUI_, "设置", viewport->WorkPos.y + UI(18.0f), 255, 255, 255, 255, 26);

        int master  = ctx.masterVolume;
        int music   = ctx.musicVolume;
        int effects = ctx.effectsVolume;

        const float labelW = UI(90.0f);
        const float sliderW = UI(360.0f);
        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const float startX = (vpW - (labelW + gap + sliderW)) * 0.5f;

        // 滚动区高度自适应：占满「标题下方」到「返回按钮上方」之间的空间
        const float topLocal    = UI(66.0f);
        const float footerH     = UI(70.0f);
        const float scrollH     = std::max(UI(160.0f), vpH - topLocal - footerH);

        ImGui::SetCursorPosY(topLocal);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.078f, 0.118f, 0.157f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.05f, 0.07f, 0.10f, 0.60f));
        ImGui::BeginChild("SettingsScroll", ImVec2(vpW, scrollH), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        auto sliderRow = [&](const char* label, const char* id, float x) {
            ImGui::SetCursorPosX(x);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(x + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
        };

        // ---- 音频 ----
        sliderRow("主音量", "##master", startX);
        if (ImGui::SliderInt("##master", &master, 0, 100)) actions.setMasterVolume(master);
        sliderRow("音乐", "##music", startX);
        if (ImGui::SliderInt("##music", &music, 0, 100)) actions.setMusicVolume(music);
        sliderRow("音效", "##effects", startX);
        if (ImGui::SliderInt("##effects", &effects, 0, 100)) actions.setEffectsVolume(effects);

        ImGui::SetCursorPosX((vpW - UI(220.0f)) * 0.5f);
        if (ImGui::Button(ctx.muted ? "已静音（点击取消）" : "切换静音", ImVec2(UI(220.0f), 0.0f))) {
            actions.toggleMute();
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // ---- 操作 ----
        {
            float sens = ctx.mouseSensitivity;
            sliderRow("鼠标灵敏度", "##sens", startX);
            if (ImGui::SliderFloat("##sens", &sens, 0.1f, 3.0f, "%.1f")) {
                actions.setMouseSensitivity(sens);
            }
        }
        {
            sliderRow("帧率上限", "##fps", startX);
            const char* fpsLabels[] = { "30", "60", "120" };
            int fpsIdx = (ctx.targetFPS >= 120) ? 2 : (ctx.targetFPS >= 60) ? 1 : 0;
            if (ImGui::Combo("##fps", &fpsIdx, fpsLabels, 3)) {
                actions.setTargetFPS(fpsIdx == 2 ? 120 : fpsIdx == 1 ? 60 : 30);
            }
        }
        {
            sliderRow("分辨率", "##res", startX);
            const char* resLabels[kResolutionCount];
            for (int i = 0; i < kResolutionCount; ++i) {
                resLabels[i] = kResolutions[i].label;
            }
            int resIdx = std::clamp(ctx.resIdx, 0, kResolutionCount - 1);
            if (ImGui::Combo("##res", &resIdx, resLabels, kResolutionCount)) {
                actions.setResolution(kResolutions[resIdx].w,
                                      kResolutions[resIdx].h, resIdx);
            }
        }
        {
            ImGui::SetCursorPosX(startX);
            bool fs = ctx.isFullscreen;
            if (ImGui::Checkbox("全屏 (F11)", &fs)) actions.setFullscreen(fs);
        }
        {
            sliderRow("渲染缩放", "##rscale", startX);
            const char* scaleLabels[] = { "50%  (省性能)", "75%", "100% (最清晰)" };
            int idx = (ctx.renderScalePct >= 100) ? 2 : (ctx.renderScalePct >= 75) ? 1 : 0;
            if (ImGui::Combo("##rscale", &idx, scaleLabels, 3)) {
                actions.setRenderScale(idx == 2 ? 100 : idx == 1 ? 75 : 50);
            }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // ---- 准星 ----
        ImGui::SetCursorPosX(startX);
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "准星");
        ImGui::Spacing();
        {
            float col[4] = { ctx.crosshairR / 255.0f, ctx.crosshairG / 255.0f,
                             ctx.crosshairB / 255.0f, ctx.crosshairA / 255.0f };
            sliderRow("颜色", "##crossColor", startX);
            if (ImGui::ColorEdit4("##crossColor", col,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                actions.setCrosshairColor(static_cast<Uint8>(col[0] * 255),
                                          static_cast<Uint8>(col[1] * 255),
                                          static_cast<Uint8>(col[2] * 255),
                                          static_cast<Uint8>(col[3] * 255));
            }
        }
        {
            float t = ctx.crosshairThickness;
            sliderRow("粗细", "##crossThick", startX);
            if (ImGui::SliderFloat("##crossThick", &t, 0.5f, 4.0f, "%.1f")) actions.setCrosshairThickness(t);
        }
        {
            float len = ctx.crosshairLength;
            sliderRow("长度", "##crossLen", startX);
            if (ImGui::SliderFloat("##crossLen", &len, 4.0f, 30.0f, "%.0f")) actions.setCrosshairLength(len);
        }
        {
            float gp = ctx.crosshairGap;
            sliderRow("中心间隔", "##crossGap", startX);
            if (ImGui::SliderFloat("##crossGap", &gp, 0.0f, 12.0f, "%.1f")) actions.setCrosshairGap(gp);
        }
        {
            ImGui::SetCursorPosX(startX);
            bool dot = ctx.crosshairCenterDot;
            if (ImGui::Checkbox("显示中心点", &dot)) actions.setCrosshairCenterDot(dot);
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // ---- 雷达 ----
        ImGui::SetCursorPosX(startX);
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "雷达");
        ImGui::Spacing();
        {
            ImGui::SetCursorPosX(startX);
            bool pc = ctx.radarPlayerCentered;
            if (ImGui::Checkbox("玩家居中", &pc)) actions.setRadarPlayerCentered(pc);
        }
        {
            ImGui::SetCursorPosX(startX);
            bool rwp = ctx.radarRotateWithPlayer;
            if (ImGui::Checkbox("随玩家旋转", &rwp)) actions.setRadarRotateWithPlayer(rwp);
        }
        {
            ImGui::SetCursorPosX(startX);
            bool blend = ctx.radarBlendBg;
            if (ImGui::Checkbox("半透明背景", &blend)) actions.setRadarBlendBg(blend);
        }
        {
            int alpha = ctx.radarBgAlpha;
            sliderRow("背景透明度", "##radarAlpha", startX);
            if (ImGui::SliderInt("##radarAlpha", &alpha, 0, 255)) actions.setRadarBgAlpha(alpha);
        }
        {
            int size = ctx.radarHudSize;
            sliderRow("雷达大小", "##radarSize", startX);
            if (ImGui::SliderInt("##radarSize", &size, 60, 220)) actions.setRadarHudSize(size);
        }
        {
            float zoom = ctx.radarMapZoom;
            sliderRow("地图缩放", "##radarZoom", startX);
            if (ImGui::SliderFloat("##radarZoom", &zoom, 1.0f, 8.0f, "%.2f")) actions.setRadarMapZoom(zoom);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        // 返回按钮固定在底部（不再依赖固定 540 坐标，避免被裁掉）
        ImGui::SetCursorPosY(vpH - footerH + UI(8.0f));
        if (bigButton("返回")) doClick([&] { actions.goState(ctx.settingsReturnState); });
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
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
    const float centerX = wp.x + ws.x * 0.5f;
    const float vpH = ws.y;

    DrawMenuBackdrop(dl, wp, ws);

    DrawImGuiTextCentered(imGuiUI_, "历史记录", wp.y + UI(34.0f), 240, 246, 255, 255, 32);
    DrawTitleAccent(dl, centerX, wp.y + UI(80.0f), UI(50.0f));

    const int count = ctx.records ? static_cast<int>(ctx.records->size()) : 0;
    char sub[96];
    snprintf(sub, sizeof(sub), "最近 %d 场对局（最多保留 50 场）", count);
    DrawImGuiTextCentered(imGuiUI_, sub, wp.y + UI(92.0f), 150, 178, 200, 255, 16);

    // 内容区：占满标题下方到页脚上方
    const float topLocal = UI(126.0f);
    const float footerH  = UI(70.0f);
    const float listH    = std::max(UI(160.0f), vpH - topLocal - footerH);
    const float panelW   = std::min(UI(980.0f), ws.x - UI(80.0f));

    ImGui::SetCursorPos(ImVec2((ws.x - panelW) * 0.5f, topLocal));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.075f, 0.100f, 0.92f));
    ImGui::BeginChild("##historyList", ImVec2(panelW, listH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (count == 0) {
        ImGui::SetCursorPosY(listH * 0.35f);
        DrawImGuiTextCentered(imGuiUI_, "还没有对局记录", wp.y + topLocal + listH * 0.35f,
                              150, 170, 190, 255, 20);
        DrawImGuiTextCentered(imGuiUI_, "完成一场比赛后就会出现在这里",
                              wp.y + topLocal + listH * 0.35f + UI(30.0f),
                              120, 140, 160, 255, 16);
    } else if (ImGui::BeginTable("##history", 8,
                   ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                   ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("时间",   ImGuiTableColumnFlags_WidthFixed, UI(150.0f));
        ImGui::TableSetupColumn("模式",   ImGuiTableColumnFlags_WidthFixed, UI(110.0f));
        ImGui::TableSetupColumn("地图",   ImGuiTableColumnFlags_WidthFixed, UI(110.0f));
        ImGui::TableSetupColumn("比分",   ImGuiTableColumnFlags_WidthFixed, UI(80.0f));
        ImGui::TableSetupColumn("结果",   ImGuiTableColumnFlags_WidthFixed, UI(80.0f));
        ImGui::TableSetupColumn("K / D",  ImGuiTableColumnFlags_WidthFixed, UI(90.0f));
        ImGui::TableSetupColumn("回合",   ImGuiTableColumnFlags_WidthFixed, UI(70.0f));
        ImGui::TableSetupColumn("时长",   ImGuiTableColumnFlags_WidthFixed, UI(90.0f));
        ImGui::TableHeadersRow();

        // 最新在上
        for (int i = count - 1; i >= 0; --i) {
            const MatchRecord& r = (*ctx.records)[static_cast<size_t>(i)];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.time.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(MatchHistory::ModeLabel(r.modeId));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(MatchHistory::MapLabel(r.mapId));

            ImGui::TableNextColumn();
            ImGui::Text("%d : %d", r.scoreT, r.scoreCT);

            ImGui::TableNextColumn();
            if (r.result == "win") {
                ImGui::TextColored(Palette::Ally(), "%s", MatchHistory::ResultLabel(r.result));
            } else if (r.result == "lose") {
                ImGui::TextColored(Palette::Danger(), "%s", MatchHistory::ResultLabel(r.result));
            } else {
                ImGui::TextColored(Palette::TextDim(), "%s", MatchHistory::ResultLabel(r.result));
            }

            ImGui::TableNextColumn();
            ImGui::Text("%d / %d", r.kills, r.deaths);

            ImGui::TableNextColumn();
            ImGui::Text("%d", r.rounds);

            ImGui::TableNextColumn();
            ImGui::Text("%02d:%02d", r.durationSec / 60, r.durationSec % 60);
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // 页脚
    ImGui::SetCursorPosY(vpH - footerH + UI(8.0f));
    ImGui::SetCursorPosX(centerX - wp.x - UI(110.0f));
    if (ImGui::Button("返回主菜单", ImVec2(UI(220.0f), UI(46.0f)))) {
        if (actions.playClick) actions.playClick();
        if (actions.goState) actions.goState(MAIN_MENU);
    }

    DrawImGuiTextCentered(imGuiUI_, "Esc 返回", vpH - footerH + UI(60.0f),
                          118, 142, 164, 235, 15);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (actions.goState) actions.goState(MAIN_MENU);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void GameUI::DrawBuyMenu(const BuyMenuCtx& ctx, const std::function<void(int)>& onBuy) {
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

    ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.35f, 1.0f), "购买菜单");
    ImGui::SameLine(0.0f, UI(24.0f));
    ImGui::Text("余额: $%d", ctx.money);
    ImGui::SameLine(0.0f, UI(24.0f));
    ImGui::TextDisabled("[B] 关闭");

    if (!ctx.canBuy && !ctx.hint.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s", ctx.hint.c_str());
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

            const bool enabled = ctx.canBuy && it.affordable && !it.owned;
            if (!enabled) ImGui::BeginDisabled();

            char label[128];
            snprintf(label, sizeof(label), "%s  $%d%s",
                     it.name.c_str(), it.price, it.owned ? " (已装备)" : "");
            if (ImGui::Button(label, ImVec2(UI(182.0f), 0.0f))) {
                if (onBuy) onBuy(it.weaponId);
            }
            if (!enabled) ImGui::EndDisabled();

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
    } else if (ctx.objective == 2) {   // 人质
        char buf[64];
        snprintf(buf, sizeof(buf), "人质 %d / %d",
                 ctx.hostagesRescued, ctx.hostagesTotal);
        DrawImGuiTextCentered(imGuiUI, buf, UI(92.0f), 120, 220, 255, 255, 22);
        if (ctx.rescueProgress > 0.0f) {
            char rb[64];
            snprintf(rb, sizeof(rb), "营救中 %d%%", static_cast<int>(ctx.rescueProgress * 100));
            DrawImGuiTextCentered(imGuiUI, rb, UI(118.0f), 230, 240, 160, 255, 18);
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
        std::string line = ctx.weaponName;
        if (ctx.magazineSize > 0) {
            line += " " + std::to_string(ctx.ammo) + "/" + std::to_string(ctx.magazineSize);
        } else if (ctx.grenadeCount > 0) {
            // 投掷物：显示背包里还剩几个（按 4 切换）
            line += " (" + std::to_string(ctx.grenadeCount) + ")";
        }
        // 与 CS2DRenderer 的面板对齐：面板高 56*scale、右下留 10*scale
        const float vpH = ImGui::GetIO().DisplaySize.y;
        const float panelH = UI(56.0f);
        const float margin = UI(10.0f);
        const float textX = vpW - UI(160.0f) + UI(50.0f);
        const float textY = vpH - panelH - margin + UI(18.0f);
        DrawImGuiText(imGuiUI, line.c_str(), textX, textY, 230, 238, 235, 255, 18);
    }

    // 提示（换弹等）
    if (ctx.hasNoWeaponHint && !ctx.hintText.empty()) {
        DrawImGuiTextCentered(imGuiUI, ctx.hintText.c_str(), 512.0f, 245, 200, 90, 255, 22);
    }
}
