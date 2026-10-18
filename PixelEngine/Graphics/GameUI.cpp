#include "GameUI.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include "../Core/Roles.hpp"
#include "ImGuiUI.hpp"
#include "TextConfig.hpp"
#include "../Storage/SaveManager.hpp"
#include <cstdio>

// —— 私有 helper：用指定字号档位在给定坐标绘制文字 ——
static void DrawImGuiText(ImGuiUI* ui, const char* text, float x, float y,
                          Uint8 r, Uint8 g, Uint8 b, Uint8 a, int size, int alignX = 0) {
    if (!ui) return;
    ImFont* font = ui->GetFont(size);
    if (font) ImGui::PushFont(font);
    if (alignX == 1) {
        const ImVec2 sz = ImGui::CalcTextSize(text);
        x = (ImGui::GetIO().DisplaySize.x - sz.x) * 0.5f;
    }
    ImGui::GetForegroundDrawList()->AddText(ImVec2(x, y), IM_COL32(r, g, b, a), text);
    if (font) ImGui::PopFont();
}

// 居中绘制：先 CalcTextSize 再算 x
static void DrawImGuiTextCentered(ImGuiUI* ui, const char* text, float centerY,
                                  Uint8 r, Uint8 g, Uint8 b, Uint8 a, int size) {
    if (!ui) return;
    ImFont* font = ui->GetFont(size);
    if (font) ImGui::PushFont(font);
    const ImVec2 sz = ImGui::CalcTextSize(text);
    const float x = (ImGui::GetIO().DisplaySize.x - sz.x) / 2.0f;
    ImGui::GetForegroundDrawList()->AddText(ImVec2(x, centerY), IM_COL32(r, g, b, a), text);
    if (font) ImGui::PopFont();
}

// 取样式（cfg 为空时回退默认字面量）
static TextStyle ResolveStyle(const TextConfig* cfg, const std::string& key, const TextStyle& fallback) {
    return cfg ? cfg->Get(key) : fallback;
}

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
    // 绘制静态文字（查表，cfg 为空时用 fallback 默认）
    auto drawStatic = [&](const std::string& key, const char* text, const TextStyle& fallback) {
        const TextStyle s = ResolveStyle(cfg_, key, fallback);
        ImFont* font = imGuiUI_ ? imGuiUI_->GetFont(s.size) : nullptr;
        if (font) ImGui::PushFont(font);

        float x = s.x;
        if (s.alignX == 1) {
            x = (vpW - ImGui::CalcTextSize(text).x) * 0.5f;
            // 实时把居中计算出的 x 写回配置，保证保存时落盘的是真实坐标（持久化）
            if (cfg_) cfg_->GetMutable(key).x = x;
        }
        ImGui::SetCursorPosX(x);
        ImGui::SetCursorPosY(s.y);

        if (font) {
            ImGui::TextColored(ImVec4(s.r / 255.0f, s.g / 255.0f, s.b / 255.0f, s.a / 255.0f), "%s", text);
        } else {
            ImGui::TextUnformatted(text);
        }
        if (font) ImGui::PopFont();
    };

    if (ctx.state == MAIN_MENU) {
        drawStatic("menu_main_title", "PIXEL ENGINE", TextStyle{350.0f, 96.0f, 20, 255, 255, 255, 255, 0, false});
        ImGui::SetCursorPosY(150.0f);
        if (bigButton("选择角色")) doClick([&]{ actions.goState(ROLE_SELECT); });
        if (bigButton("选择模式")) doClick([&]{ actions.goState(MODE_SELECT); });
        if (bigButton("历史记录")) doClick([&]{ actions.goState(HISTORY); });
        if (bigButton("按键介绍")) doClick([&]{ actions.goState(CONTROLS); });
        if (bigButton("设置")) doClick([&]{ actions.openSettings(MAIN_MENU); });
        if (bigButton("退出游戏")) doClick([&]{ actions.quitToDesktop(); });
    } else if (ctx.state == MODE_SELECT) {
        ImGui::SetCursorPosY(160.0f);
        if (bigButton("模式一  限时挑战")) doClick([&]{ actions.startGame(TIME_ATTACK); });
        if (bigButton("模式二  迷宫探索")) doClick([&]{ actions.startGame(MAZE_MODE); });
        if (bigButton("模式三  CS2D")) doClick([&]{ actions.startGame(CS2D_MODE); });
        ImGui::SetCursorPosY(440.0f);
        if (bigButton("返回")) doClick([&]{ actions.goState(MAIN_MENU); });
    } else if (ctx.state == ROLE_SELECT) {
        drawStatic("menu_role_title", "选择角色", TextStyle{0.0f, 60.0f, 26, 255, 255, 255, 255, 0, false});
        ImGui::Separator();

        // 卡片矩形由 Core/Roles.hpp::GetRoleCardRect 提供，与 Engine 的立绘预览严格对齐；
        // 预览区占卡片上部（Engine 已画好），按钮画在卡片下部。
        for (int i = 0; i < ROLE_COUNT; ++i) {
            const SDL_FRect card = GetRoleCardRect(i, ImGui::GetIO().DisplaySize.x);
            const float buttonH = 84.0f;
            ImGui::SetCursorPos(ImVec2(card.x + 6.0f, card.y + card.h - buttonH - 6.0f));
            ImGui::PushID(i);

            const bool isActive = (ctx.selectedRole == i);
            if (isActive) {
                // 选中项：绿色描边高亮 + "使用中"标记
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.55f, 0.34f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.38f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.95f, 0.55f, 1.0f));
            }
            const float buttonW = card.w - 12.0f;
            char label[128];
            snprintf(label, sizeof(label), "%s%s\n%s", GetRoles()[i].name,
                     isActive ? "  ✔ 使用中" : "", GetRoles()[i].tagline);
            if (ImGui::Button(label, ImVec2(buttonW, buttonH))) {
                doClick([&]{ actions.selectRole(i); });
            }
            if (isActive) {
                ImGui::PopStyleColor(4);
            }
            ImGui::PopID();
        }

        ImGui::SetCursorPosY(480.0f);
        if (bigButton("开始冒险")) doClick([&]{ actions.goState(MODE_SELECT); });
        ImGui::SetCursorPosY(540.0f);
        if (bigButton("返回")) doClick([&]{ actions.goState(MAIN_MENU); });
    } else if (ctx.state == HISTORY) {
        drawStatic("menu_history_title", "历史记录", TextStyle{0.0f, 120.0f, 20, 255, 255, 255, 255, 0, false});
        ImGui::Separator();
        drawStatic("menu_history_mode1", "模式一  限时挑战", TextStyle{60.0f, 0.0f, 20, 255, 255, 255, 255, 0, false});
        drawStatic("menu_history_mode2", "模式二  迷宫探索", TextStyle{460.0f, 0.0f, 20, 255, 255, 255, 255, 0, false});

        const std::vector<ScoreRecord>* scoreHistory = nullptr;
        const std::vector<float>* timeHistory = nullptr;
        if (ctx.saves) {
            scoreHistory = &ctx.saves->GetScoreHistory();
            timeHistory = &ctx.saves->GetTimeHistory();
        }

        // 柱状图归一化：模式一按最高分、模式二按最大耗时，分别归一到满槽。
        int maxScore = 0;
        if (scoreHistory) {
            for (const ScoreRecord& rec : *scoreHistory) {
                if (rec.score > maxScore) maxScore = rec.score;
            }
        }
        float minTime = FLT_MAX;
        if (timeHistory) {
            for (float t : *timeHistory) {
                if (t < minTime) minTime = t;
            }
        }

        // 柱条几何：两列均 16px 高、行内垂直居中，槽宽固定并留出相邻列间隙。
        const float barH = 16.0f;
        const float scoreBarX = 160.0f;   // 模式一槽起点（终点 400）
        const float scoreBarMaxW = 180.0f;
        const float timeBarX = 540.0f;    // 模式二槽起点（终点 750）
        const float timeBarMaxW = 190.0f;

        for (size_t i = 0; i < 10; ++i) {
            const float y = 140.0f + i * 34.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImGui::SetCursorPos(ImVec2(36.0f, y));
            // 柱条 y 基准用屏幕坐标（GetCursorScreenPos 已含滚动偏移），保证与文字一起随滚轮滚动。
            const float barY = ImGui::GetCursorScreenPos().y + (34.0f - barH) * 0.1f;
            if (scoreHistory && i < scoreHistory->size()) {
                const ScoreRecord& rec = (*scoreHistory)[i];
                char buf[64];
                snprintf(buf, sizeof(buf), "%02d  %d 分  %s", static_cast<int>(i + 1),
                         rec.score, rec.success ? "成功" : "失败");
                const TextStyle rs = ResolveStyle(cfg_, "menu_history_score_row", TextStyle{36.0f, 0.0f, 20, 255, 255, 255, 255, 0, false});
                if (imGuiUI_) {
                    ImFont* font = imGuiUI_->GetFont(rs.size);
                    if (font) ImGui::PushFont(font);
                    ImGui::TextColored(ImVec4(rs.r / 255.0f, rs.g / 255.0f, rs.b / 255.0f, rs.a / 255.0f), "%s", buf);
                    if (font) ImGui::PopFont();
                } else {
                    ImGui::TextUnformatted(buf);
                }

                const float barW = rec.score > 0 ? scoreBarMaxW * static_cast<float>(rec.score) / static_cast<float>(maxScore) : 0.0f;
                const ImU32 barCol = rec.success ? IM_COL32(80, 200, 120, 255) : IM_COL32(220, 80, 80, 255);
                dl->AddRectFilled(ImVec2(scoreBarX, barY), ImVec2(scoreBarX + barW, barY + barH), barCol);
                dl->AddRect(ImVec2(scoreBarX, barY), ImVec2(scoreBarX + scoreBarMaxW, barY + barH), IM_COL32(255, 255, 255, 60), 0.0f, 0, 1.0f);
            }
            ImGui::SetCursorPos(ImVec2(426.0f, y));
            if (timeHistory && i < timeHistory->size()) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%02d  %.2f 秒", static_cast<int>(i + 1), (*timeHistory)[i]);
                const TextStyle rt = ResolveStyle(cfg_, "menu_history_time_row", TextStyle{426.0f, 0.0f, 20, 255, 255, 255, 255, 0, false});
                if (imGuiUI_) {
                    ImFont* font = imGuiUI_->GetFont(rt.size);
                    if (font) ImGui::PushFont(font);
                    ImGui::TextColored(ImVec4(rt.r / 255.0f, rt.g / 255.0f, rt.b / 255.0f, rt.a / 255.0f), "%s", buf);
                    if (font) ImGui::PopFont();
                } else {
                    ImGui::TextUnformatted(buf);
                }

                const float t = (*timeHistory)[i];
                const float barW = t > 0.0f ? minTime / t * timeBarMaxW : 0.0f;
                dl->AddRectFilled(ImVec2(timeBarX, barY), ImVec2(timeBarX + barW, barY + barH), IM_COL32(80, 180, 220, 255));
                dl->AddRect(ImVec2(timeBarX, barY), ImVec2(timeBarX + timeBarMaxW, barY + barH), IM_COL32(255, 255, 255, 60), 0.0f, 0, 1.0f);
            }
        }

        ImGui::SetCursorPosY(500.0f);
        if (bigButton("返回")) doClick([&]{ actions.goState(MAIN_MENU); });
    } else if (ctx.state == CONTROLS) {
        drawStatic("menu_controls_title", "操作说明", TextStyle{0.0f, 120.0f, 20, 255, 255, 255, 255, 0, false});
        ImGui::Separator();
        // 每行独立 key：可单独调 x / 字号 / 颜色（y 保持 ImGui 自然流）
        auto drawLine = [&](const std::string& key, const char* text) {
            const TextStyle ls = ResolveStyle(cfg_, key, TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false});
            ImGui::SetCursorPosX(ls.x);
            if (imGuiUI_) {
                ImFont* font = imGuiUI_->GetFont(ls.size);
                if (font) ImGui::PushFont(font);
                ImGui::TextColored(ImVec4(ls.r / 255.0f, ls.g / 255.0f, ls.b / 255.0f, ls.a / 255.0f), "%s", text);
                if (font) ImGui::PopFont();
            } else {
                ImGui::TextUnformatted(text);
            }
        };
        drawLine("menu_controls_line1", "游戏中");
        drawLine("menu_controls_line2", "W / A / S / D  或  方向键：移动");
        drawLine("menu_controls_line3", "Shift：蓄力冲刺；Ctrl：聚焦视野");
        drawLine("menu_controls_line4", "鼠标左键/空格：攻击；1/2/3：切换武器；F：拾取/丢弃");
        drawLine("menu_controls_line5", "ESC：暂停游戏；F11：全屏；F1：静音");
        drawLine("menu_controls_line6", "选择角色页：数字键 1-4 快捷选中对应角色");
        ImGui::SetCursorPosY(500.0f);
        if (bigButton("返回")) doClick([&]{ actions.goState(MAIN_MENU); });
    } else if (ctx.state == SETTINGS) {
        drawStatic("menu_settings_title", "设置", TextStyle{0.0f, 40.0f, 20, 255, 255, 255, 255, 0, false});

        int master = ctx.masterVolume;
        int music = ctx.musicVolume;
        int effects = ctx.effectsVolume;

        // 固定标签列宽 + 滑条列宽，整体水平居中，避免行内标签宽度不同导致滑条起点漂移。
        const float vpW = viewport->WorkSize.x;
        const float labelW = 90.0f;
        const float sliderW = 360.0f;
        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const float startX = (vpW - (labelW + gap + sliderW)) * 0.5f;

        // —— 可滚动区域：主内容在上方滚动，底部按钮固定 ——
        ImGui::SetCursorPosY(70.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.078f, 0.118f, 0.157f, 1.0f));  // 与页面背景 (20,30,40) 一致
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.05f, 0.07f, 0.10f, 0.60f));
        ImGui::BeginChild("SettingsScroll", ImVec2(vpW, 460.0f), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        auto volumeSlider = [&](const char* label, const std::string& key, const char* id, int* value, const std::function<void(int)>& apply) {
            const TextStyle ls = ResolveStyle(cfg_, key, TextStyle{0.0f, 0.0f, 20, 255, 255, 255, 255, 0, false});
            ImGui::SetCursorPosX(startX);
            if (imGuiUI_) {
                ImFont* font = imGuiUI_->GetFont(ls.size);
                if (font) ImGui::PushFont(font);
                ImGui::TextColored(ImVec4(ls.r / 255.0f, ls.g / 255.0f, ls.b / 255.0f, ls.a / 255.0f), "%s", label);
                if (font) ImGui::PopFont();
            } else {
                ImGui::TextUnformatted(label);
            }
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderInt(id, value, 0, 100)) apply(*value);
        };
        volumeSlider("主音量", "menu_settings_label_master", "##master", &master, actions.setMasterVolume);
        volumeSlider("音乐", "menu_settings_label_music", "##music", &music, actions.setMusicVolume);
        volumeSlider("音效", "menu_settings_label_effects", "##effects", &effects, actions.setEffectsVolume);

        const float muteW = 220.0f;
        ImGui::SetCursorPosX((vpW - muteW) * 0.5f);
        if (ImGui::Button(ctx.muted ? "已静音（点击取消）" : "切换静音", ImVec2(muteW, 0.0f))) {
            actions.toggleMute();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ====== CS2D 设置 ======
        // 鼠标灵敏度
        {
            float sens = ctx.mouseSensitivity;
            ImGui::SetCursorPosX(startX);
            ImGui::Text("鼠标灵敏度");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderFloat("##sens", &sens, 0.1f, 3.0f, "%.1f")) {
                actions.setMouseSensitivity(sens);
            }
        }

        // 帧率
        {
            ImGui::SetCursorPosX(startX);
            ImGui::Text("帧率上限");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            int fps = ctx.targetFPS;
            const char* fpsLabels[] = { "30", "60", "120" };
            int fpsIdx = (fps >= 120) ? 2 : (fps >= 60) ? 1 : 0;
            if (ImGui::Combo("##fps", &fpsIdx, fpsLabels, 3)) {
                const int newFps = (fpsIdx == 2) ? 120 : (fpsIdx == 1) ? 60 : 30;
                actions.setTargetFPS(newFps);
            }
        }

        // 分辨率
        {
            ImGui::SetCursorPosX(startX);
            ImGui::Text("分辨率");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            const char* resLabels[] = { "800x600", "1024x768", "1280x960", "1920x1080", "2560x1440", "2560x1600" };
            int resIdx = ctx.resIdx;
            if (ImGui::Combo("##res", &resIdx, resLabels, 6)) {
                const int resW[] = { 800, 1024, 1280, 1920, 2560, 2560 };
                const int resH[] = { 600, 768, 960, 1080, 1440, 1600 };
                actions.setResolution(resW[resIdx], resH[resIdx], resIdx);
            }
        }

        // 全屏
        {
            ImGui::SetCursorPosX(startX);
            bool fs = ctx.isFullscreen;
            if (ImGui::Checkbox("全屏 (F11)", &fs)) {
                actions.setFullscreen(fs);
            }
            ImGui::SameLine(startX + labelW + gap + 160.0f);
            ImGui::TextDisabled("(F11)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ====== 准星设置 ======
        ImGui::SetCursorPosX(startX);
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "准星设置");
        ImGui::Spacing();

        // 颜色
        {
            float col[4] = { ctx.crosshairR / 255.0f, ctx.crosshairG / 255.0f, ctx.crosshairB / 255.0f, ctx.crosshairA / 255.0f };
            ImGui::SetCursorPosX(startX);
            ImGui::Text("颜色");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::ColorEdit4("##crossColor", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                actions.setCrosshairColor(
                    static_cast<Uint8>(col[0] * 255),
                    static_cast<Uint8>(col[1] * 255),
                    static_cast<Uint8>(col[2] * 255),
                    static_cast<Uint8>(col[3] * 255));
            }
        }

        // 粗细
        {
            float t = ctx.crosshairThickness;
            ImGui::SetCursorPosX(startX);
            ImGui::Text("粗细");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderFloat("##crossThick", &t, 0.5f, 4.0f, "%.1f")) actions.setCrosshairThickness(t);
        }

        // 长度
        {
            float len = ctx.crosshairLength;
            ImGui::SetCursorPosX(startX);
            ImGui::Text("长度");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderFloat("##crossLen", &len, 4.0f, 30.0f, "%.0f")) actions.setCrosshairLength(len);
        }

        // 间隔
        {
            float g = ctx.crosshairGap;
            ImGui::SetCursorPosX(startX);
            ImGui::Text("中心间隔");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderFloat("##crossGap", &g, 0.0f, 12.0f, "%.1f")) actions.setCrosshairGap(g);
        }

        // 中心点
        {
            bool dot = ctx.crosshairCenterDot;
            ImGui::SetCursorPosX(startX);
            if (ImGui::Checkbox("显示中心点", &dot)) actions.setCrosshairCenterDot(dot);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ====== 雷达设置 ======
        ImGui::SetCursorPosX(startX);
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "雷达设置");
        ImGui::Spacing();

        {
            bool pc = ctx.radarPlayerCentered;
            ImGui::SetCursorPosX(startX);
            if (ImGui::Checkbox("玩家居中（否则固定在左上角）", &pc)) actions.setRadarPlayerCentered(pc);
        }
        {
            bool rwp = ctx.radarRotateWithPlayer;
            ImGui::SetCursorPosX(startX);
            if (ImGui::Checkbox("雷达随玩家旋转", &rwp)) actions.setRadarRotateWithPlayer(rwp);
        }
        {
            bool blend = ctx.radarBlendBg;
            ImGui::SetCursorPosX(startX);
            if (ImGui::Checkbox("半透明背景", &blend)) actions.setRadarBlendBg(blend);
        }
        {
            int alpha = ctx.radarBgAlpha;
            ImGui::SetCursorPosX(startX);
            ImGui::Text("背景透明度");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderInt("##radarAlpha", &alpha, 0, 255)) actions.setRadarBgAlpha(alpha);
        }
        {
            int size = ctx.radarHudSize;
            ImGui::SetCursorPosX(startX);
            ImGui::Text("雷达大小");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderInt("##radarSize", &size, 60, 220)) actions.setRadarHudSize(size);
        }
        {
            float zoom = ctx.radarMapZoom;
            ImGui::SetCursorPosX(startX);
            ImGui::Text("地图缩放");
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderFloat("##radarZoom", &zoom, 0.001f, 0.01f, "%.3f")) actions.setRadarMapZoom(zoom);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        ImGui::SetCursorPosY(540.0f);
        if (bigButton("返回")) doClick([&]{ actions.goState(ctx.settingsReturnState); });
    } else if (ctx.state == PAUSED) {
        ImGui::SetCursorPosY(150.0f);
        if (bigButton("继续游戏")) doClick([&]{ actions.resumeGame(); });
        if (bigButton("重新开始")) doClick([&]{ actions.startGame(ctx.currentMode); });
        if (bigButton("设置")) doClick([&]{ actions.openSettings(PAUSED); });
        if (bigButton("退出游戏")) doClick([&]{ actions.confirmExit(false); });
        if (bigButton("退出桌面")) doClick([&]{ actions.confirmExit(true); });
    } else if (ctx.state == EXIT_CONFIRM) {
        drawStatic("menu_exit_title", "确认退出", TextStyle{0.0f, 200.0f, 20, 255, 255, 255, 255, 0, false});
        drawStatic("menu_exit_note", "本次游戏数据将不会被保存", TextStyle{0.0f, 270.0f, 20, 255, 255, 255, 255, 0, false});
        ImGui::SetCursorPosY(330.0f);
        ImGui::SetCursorPosX(centerX - buttonW - 15.0f);
        if (ImGui::Button("算了，那我玩完吧", ImVec2(buttonW, buttonH))) doClick([&]{ actions.goState(PAUSED); });
        ImGui::SetCursorPosY(330.0f);
        ImGui::SetCursorPosX(centerX + 15.0f);
        if (ImGui::Button("老子不玩了", ImVec2(buttonW, buttonH))) doClick([&]{ actions.quitApp(); });
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void GameUI::DrawInGame(const InGameCtx& ctx, ImGuiUI* imGuiUI) {
    const float vpW = ImGui::GetIO().DisplaySize.x;
    // 绘制 + 居中写回：alignX==1 时把居中计算出的 x 回写到 cfg_（持久化）
    auto drawWithStyle = [&](const std::string& key, const char* text, const TextStyle& s) {
        float x = s.x;
        if (s.alignX == 1) {
            ImFont* font = imGuiUI ? imGuiUI->GetFont(s.size) : nullptr;
            if (font) ImGui::PushFont(font);
            x = (ImGui::GetIO().DisplaySize.x - ImGui::CalcTextSize(text).x) * 0.5f;
            if (font) ImGui::PopFont();
            if (cfg_) cfg_->GetMutable(key).x = x;
        }
        DrawImGuiText(imGuiUI, text, x, s.y, s.r, s.g, s.b, s.a, s.size);
    };

    if (ctx.state == PLAYING) {
        // CS2D 计分板（顶端正中）
        if (ctx.mode == CS2D_MODE) {
            char buf[128];
            snprintf(buf, sizeof(buf), "人数: %d    击杀: %d    阵亡: %d", ctx.cs2dTotal, ctx.cs2dKills, ctx.cs2dDeaths);
            DrawImGuiTextCentered(imGuiUI, buf, 14.0f, 200, 200, 200, 255, 20);
        }

        // 武器状态文字：panel = {640,536,150,54}
        std::string line = ctx.weaponName;
        if (ctx.magazineSize > 0) {
            line += " " + std::to_string(ctx.ammo) + "/" + std::to_string(ctx.magazineSize);
        }
        const TextStyle ws = ResolveStyle(cfg_, "weapon_status", TextStyle{0.0f, 549.0f, 18, 230, 238, 235, 255, 0, true});
        const float textX = ws.anchored ? (vpW - 160.0f + (ctx.hasWeaponIcon ? 50.0f : 12.0f)) : ws.x;
        DrawImGuiText(imGuiUI, line.c_str(), textX, ws.y, ws.r, ws.g, ws.b, ws.a, ws.size);

        // 无武器提示（居中，y=512）
        if (ctx.hasNoWeaponHint) {
            const TextStyle nh = ResolveStyle(cfg_, "no_weapon_hint", TextStyle{0.0f, 512.0f, 22, 245, 200, 90, 255, 1, false});
            if (nh.alignX == 1) {
                ImFont* font = imGuiUI ? imGuiUI->GetFont(nh.size) : nullptr;
                if (font) ImGui::PushFont(font);
                const float cx = (ImGui::GetIO().DisplaySize.x - ImGui::CalcTextSize(ctx.hintText.c_str()).x) * 0.5f;
                if (font) ImGui::PopFont();
                if (cfg_) cfg_->GetMutable("no_weapon_hint").x = cx;
                DrawImGuiTextCentered(imGuiUI, ctx.hintText.c_str(), nh.y, nh.r, nh.g, nh.b, nh.a, nh.size);
            } else {
                DrawImGuiText(imGuiUI, ctx.hintText.c_str(), nh.x, nh.y, nh.r, nh.g, nh.b, nh.a, nh.size);
            }
        }
    } else if (ctx.state == GAMEOVER) {
        const TextStyle ft  = ResolveStyle(cfg_, "gameover_fail_title", TextStyle{355.0f, 216.0f, 30, 245, 80, 80, 255, 0, false});
        const TextStyle fs  = ResolveStyle(cfg_, "gameover_fail_subtitle", TextStyle{305.0f, 256.0f, 18, 240, 140, 140, 255, 0, false});
        const TextStyle et  = ResolveStyle(cfg_, "gameover_end_title", TextStyle{334.0f, 216.0f, 30, 247, 211, 130, 255, 0, false});
        const TextStyle res = ResolveStyle(cfg_, "gameover_result", TextStyle{285.0f, 280.0f, 24, 230, 238, 235, 255, 0, false});
        const TextStyle ht  = ResolveStyle(cfg_, "gameover_hint", TextStyle{248.0f, 350.0f, 18, 134, 222, 190, 255, 0, false});

        if (ctx.mode == TIME_ATTACK && ctx.diedByMonster) {
            drawWithStyle("gameover_fail_title", "失  败", ft);
            drawWithStyle("gameover_fail_subtitle", "被怪物击杀，惨遭致死！", fs);
        } else if (ctx.mode == TIME_ATTACK && ctx.diedByTrap) {
            drawWithStyle("gameover_fail_title", "失  败", ft);
            drawWithStyle("gameover_fail_subtitle", "触发致命陷阱，惨遭致死！", fs);
        } else {
            drawWithStyle("gameover_end_title", "本局结束", et);
        }

        std::string result;
        if (ctx.mode == TIME_ATTACK) {
            result = "本局得分：" + std::to_string(ctx.score);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "通关用时：%.2f 秒", ctx.gameTimer);
            result = buf;
        }
        drawWithStyle("gameover_result", result.c_str(), res);
        drawWithStyle("gameover_hint", "Enter / 空格：返回模式选择", ht);
    }
}
