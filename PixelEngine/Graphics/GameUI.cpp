#include "GameUI.hpp"
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
    const float buttonW = 300.0f;
    const float buttonH = 46.0f;

    auto bigButton = [&](const char* label) {
        ImGui::SetCursorPosX(centerX - buttonW * 0.5f);
        return ImGui::Button(label, ImVec2(buttonW, buttonH));
    };
    auto doClick = [&](const std::function<void()>& action) {
        if (actions.playClick) actions.playClick();
        if (action) action();
    };

    // ---------------- 主菜单 ----------------
    if (ctx.state == MAIN_MENU) {
        DrawImGuiTextCentered(imGuiUI_, "CS2D", 90.0f, 255, 255, 255, 255, 34);
        DrawImGuiTextCentered(imGuiUI_, "Counter-Strike 2D 单机版", 140.0f, 160, 190, 210, 255, 20);

        ImGui::SetCursorPosY(210.0f);
        if (bigButton("团队歼灭")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::TeamElimination)); });
        if (bigButton("爆破模式")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::BombDefusal)); });
        if (bigButton("人质营救")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::HostageRescue)); });
        if (bigButton("死斗模式")) doClick([&] { actions.startMatchMode(static_cast<int>(MatchMode::DeathMatch)); });
        if (bigButton("设置"))     doClick([&] { actions.openSettings(MAIN_MENU); });
        if (bigButton("退出游戏")) doClick([&] { actions.quitToDesktop(); });

        // 累计战绩
        char buf[96];
        snprintf(buf, sizeof(buf), "累计击杀 %d   累计阵亡 %d", ctx.statKills, ctx.statDeaths);
        DrawImGuiTextCentered(imGuiUI_, buf, 470.0f, 150, 170, 185, 255, 18);
    }
    // ---------------- 暂停 ----------------
    else if (ctx.state == PAUSED) {
        DrawImGuiTextCentered(imGuiUI_, "已暂停", 110.0f, 255, 255, 255, 255, 30);

        ImGui::SetCursorPosY(190.0f);
        if (bigButton("继续游戏")) doClick([&] { actions.resumeGame(); });
        if (bigButton("重新开始")) doClick([&] { actions.startMatch(); });
        if (bigButton("设置"))     doClick([&] { actions.openSettings(PAUSED); });
        if (bigButton("退出游戏")) doClick([&] { actions.confirmExit(false); });
        if (bigButton("退出桌面")) doClick([&] { actions.confirmExit(true); });
    }
    // ---------------- 退出确认 ----------------
    else if (ctx.state == EXIT_CONFIRM) {
        DrawImGuiTextCentered(imGuiUI_, "确认退出", 180.0f, 255, 255, 255, 255, 26);
        DrawImGuiTextCentered(imGuiUI_, "当前对局进度将不会被保存", 240.0f, 200, 200, 200, 255, 18);

        ImGui::SetCursorPosY(300.0f);
        ImGui::SetCursorPosX(centerX - buttonW - 15.0f);
        if (ImGui::Button("继续游戏", ImVec2(buttonW, buttonH))) doClick([&] { actions.goState(PAUSED); });
        ImGui::SetCursorPosY(300.0f);
        ImGui::SetCursorPosX(centerX + 15.0f);
        if (ImGui::Button("退出", ImVec2(buttonW, buttonH))) doClick([&] { actions.quitApp(); });
    }
    // ---------------- 设置 ----------------
    else if (ctx.state == SETTINGS) {
        DrawImGuiTextCentered(imGuiUI_, "设置", 28.0f, 255, 255, 255, 255, 26);

        int master  = ctx.masterVolume;
        int music   = ctx.musicVolume;
        int effects = ctx.effectsVolume;

        const float labelW = 90.0f;
        const float sliderW = 360.0f;
        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const float startX = (vpW - (labelW + gap + sliderW)) * 0.5f;

        // 可滚动区
        ImGui::SetCursorPosY(70.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.078f, 0.118f, 0.157f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.05f, 0.07f, 0.10f, 0.60f));
        ImGui::BeginChild("SettingsScroll", ImVec2(vpW, 450.0f), false,
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

        ImGui::SetCursorPosX((vpW - 220.0f) * 0.5f);
        if (ImGui::Button(ctx.muted ? "已静音（点击取消）" : "切换静音", ImVec2(220.0f, 0.0f))) {
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
            const char* resLabels[] = { "800x600", "1024x768", "1280x960", "1920x1080", "2560x1440" };
            int resIdx = ctx.resIdx;
            if (ImGui::Combo("##res", &resIdx, resLabels, 5)) {
                const int rw[] = { 800, 1024, 1280, 1920, 2560 };
                const int rh[] = { 600, 768, 960, 1080, 1440 };
                actions.setResolution(rw[resIdx], rh[resIdx], resIdx);
            }
        }
        {
            ImGui::SetCursorPosX(startX);
            bool fs = ctx.isFullscreen;
            if (ImGui::Checkbox("全屏 (F11)", &fs)) actions.setFullscreen(fs);
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
            if (ImGui::SliderFloat("##radarZoom", &zoom, 0.001f, 0.01f, "%.4f")) actions.setRadarMapZoom(zoom);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        ImGui::SetCursorPosY(540.0f);
        if (bigButton("返回")) doClick([&] { actions.goState(ctx.settingsReturnState); });
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// 买枪菜单
// ============================================================================

void GameUI::DrawBuyMenu(const BuyMenuCtx& ctx, const std::function<void(int)>& onBuy) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float w = 600.0f;
    const float h = 400.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - w) * 0.5f,
                                  viewport->WorkPos.y + (viewport->WorkSize.y - h) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.09f, 0.12f, 0.96f));
    ImGui::Begin("##BuyMenu", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoTitleBar);

    ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.35f, 1.0f), "购买菜单");
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::Text("余额: $%d", ctx.money);
    ImGui::SameLine(0.0f, 24.0f);
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
            if (ImGui::Button(label, ImVec2(182.0f, 0.0f))) {
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

    // 顶部计分板：T 比分 : 比分 CT ｜ 回合 ｜ 倒计时
    {
        char left[64];
        char mid[64];
        snprintf(left, sizeof(left), "T  %d : %d  CT", ctx.scoreT, ctx.scoreCT);
        snprintf(mid, sizeof(mid), "第 %d 回合   %02d:%02d",
                 ctx.roundNumber,
                 ctx.roundRemainingSec / 60, ctx.roundRemainingSec % 60);

        DrawImGuiTextCentered(imGuiUI, left, 12.0f, 235, 235, 235, 255, 22);
        DrawImGuiTextCentered(imGuiUI, mid, 40.0f, 180, 200, 215, 255, 18);

        char alive[64];
        snprintf(alive, sizeof(alive), "T 存活 %d   CT 存活 %d   $%d   甲 %d%s",
                 ctx.aliveT, ctx.aliveCT, ctx.money, ctx.armor,
                 ctx.helmet ? "(盔)" : "");
        DrawImGuiTextCentered(imGuiUI, alive, 64.0f, 160, 175, 185, 255, 16);
    }

    // ---- 左下角血量条 ----
    {
        const float barX = 16.0f;
        const float barY = ImGui::GetIO().DisplaySize.y - 52.0f;
        const float barW = 200.0f;
        const float barH = 18.0f;

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
        DrawImGuiText(imGuiUI, hp, barX + 6.0f, barY - 20.0f,
                      235, 240, 235, 255, 18);
    }

    // 屏幕中央提示：回合开始 / 回合胜负（带淡出）
    if (!ctx.announcement.empty() && ctx.announcementAlpha > 0.0f) {
        const Uint8 a = static_cast<Uint8>(std::clamp(ctx.announcementAlpha, 0.0f, 1.0f) * 255.0f);
        DrawImGuiTextCentered(imGuiUI, ctx.announcement.c_str(), 150.0f,
                              255, 236, 150, a, 34);
    }

    // 比赛结束提示
    if (ctx.matchOver) {
        DrawImGuiTextCentered(imGuiUI,
            ctx.matchWinnerIsCT ? "比赛结束  CT 获胜" : "比赛结束  T 获胜",
            200.0f, 255, 220, 90, 255, 30);
        if (ctx.matchOverReturn >= 0.0f) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d 秒后返回主菜单",
                     static_cast<int>(ctx.matchOverReturn) + 1);
            DrawImGuiTextCentered(imGuiUI, buf, 240.0f, 200, 210, 220, 255, 20);
        }
    }

    // ---- 目标物状态 ----
    if (ctx.objective == 1) {          // 爆破
        if (ctx.bombPlanted) {
            char buf[64];
            snprintf(buf, sizeof(buf), "C4 倒计时 %d 秒", static_cast<int>(ctx.bombTimer));
            DrawImGuiTextCentered(imGuiUI, buf, 92.0f, 255, 90, 90, 255, 22);
            if (ctx.defuseProgress > 0.0f) {
                char db[64];
                snprintf(db, sizeof(db), "拆包中 %d%%", static_cast<int>(ctx.defuseProgress * 100));
                DrawImGuiTextCentered(imGuiUI, db, 118.0f, 120, 220, 255, 255, 18);
            }
        } else {
            DrawImGuiTextCentered(imGuiUI, "配合队友防守包点", 92.0f, 200, 210, 220, 255, 18);
        }
    } else if (ctx.objective == 2) {   // 人质
        char buf[64];
        snprintf(buf, sizeof(buf), "人质 %d / %d",
                 ctx.hostagesRescued, ctx.hostagesTotal);
        DrawImGuiTextCentered(imGuiUI, buf, 92.0f, 120, 220, 255, 255, 22);
        if (ctx.rescueProgress > 0.0f) {
            char rb[64];
            snprintf(rb, sizeof(rb), "营救中 %d%%", static_cast<int>(ctx.rescueProgress * 100));
            DrawImGuiTextCentered(imGuiUI, rb, 118.0f, 230, 240, 160, 255, 18);
        }
    }

    // ---- 击杀播报（右上角）----
    {
        const float x = ImGui::GetIO().DisplaySize.x - 320.0f;
        float y = 96.0f;
        for (const auto& line : ctx.killFeed) {
            DrawImGuiText(imGuiUI, line.c_str(), x, y, 235, 235, 235, 235, 16);
            y += 20.0f;
        }
    }

    // ---- 记分板（Tab 按住）----
    if (ctx.showScoreboard) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float w = 560.0f;
        const float h = 380.0f;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f,
                                      vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.08f, 0.11f, 0.95f));
        ImGui::Begin("##Scoreboard", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoTitleBar);

        ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.35f, 1.0f),
                           "T %d : %d CT", ctx.scoreT, ctx.scoreCT);
        ImGui::Separator();

        if (ImGui::BeginTable("##score", 5,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("阵营", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("K", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("$", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            for (const auto& r : ctx.scoreRows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.team == 2 ? "CT" : (r.team == 1 ? "T" : "-"));
                ImGui::TableNextColumn();
                if (r.isPlayer) {
                    ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.55f, 1.0f), "%s", r.name.c_str());
                } else {
                    ImGui::TextColored(ImVec4(r.alive ? 0.80f : 0.45f,
                                              r.alive ? 0.80f : 0.45f,
                                              r.alive ? 0.80f : 0.45f, 1.0f),
                                       "%s%s", r.name.c_str(), r.alive ? "" : " (阵亡)");
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d", r.kills);
                ImGui::TableNextColumn();
                ImGui::Text("%d", r.deaths);
                ImGui::TableNextColumn();
                ImGui::Text("$%d", r.money);
            }
            ImGui::EndTable();
        }

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
        const float textX = vpW - 160.0f + 50.0f;
        DrawImGuiText(imGuiUI, line.c_str(), textX, 549.0f, 230, 238, 235, 255, 18);
    }

    // 提示（换弹等）
    if (ctx.hasNoWeaponHint && !ctx.hintText.empty()) {
        DrawImGuiTextCentered(imGuiUI, ctx.hintText.c_str(), 512.0f, 245, 200, 90, 255, 22);
    }
}
