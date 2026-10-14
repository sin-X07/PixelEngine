#include "GameUI.hpp"
#include "../ThirdParty/imgui/imgui.h"
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
        x = (800.0f - sz.x) * 0.5f;
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
    const float x = (800.0f - sz.x) / 2.0f;
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

    const float centerX = 400.0f;
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
            x = (800.0f - ImGui::CalcTextSize(text).x) * 0.5f;
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
        ImGui::SetCursorPosY(170.0f);
        if (bigButton("选择模式")) doClick([&]{ actions.goState(MODE_SELECT); });
        if (bigButton("历史记录")) doClick([&]{ actions.goState(HISTORY); });
        if (bigButton("按键介绍")) doClick([&]{ actions.goState(CONTROLS); });
        if (bigButton("设置")) doClick([&]{ actions.openSettings(MAIN_MENU); });
        if (bigButton("退出游戏")) doClick([&]{ actions.quitToDesktop(); });
    } else if (ctx.state == MODE_SELECT) {
        ImGui::SetCursorPosY(160.0f);
        if (bigButton("模式一  限时挑战")) doClick([&]{ actions.startGame(TIME_ATTACK); });
        if (bigButton("模式二  迷宫探索")) doClick([&]{ actions.startGame(MAZE_MODE); });
        ImGui::SetCursorPosY(400.0f);
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
                ImGui::TextUnformatted(buf);

                const float barW = rec.score > 0 ? scoreBarMaxW * static_cast<float>(rec.score) / static_cast<float>(maxScore) : 0.0f;
                const ImU32 barCol = rec.success ? IM_COL32(80, 200, 120, 255) : IM_COL32(220, 80, 80, 255);
                dl->AddRectFilled(ImVec2(scoreBarX, barY), ImVec2(scoreBarX + barW, barY + barH), barCol);
                dl->AddRect(ImVec2(scoreBarX, barY), ImVec2(scoreBarX + scoreBarMaxW, barY + barH), IM_COL32(255, 255, 255, 60), 0.0f, 0, 1.0f);
            }
            ImGui::SetCursorPos(ImVec2(426.0f, y));
            if (timeHistory && i < timeHistory->size()) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%02d  %.2f 秒", static_cast<int>(i + 1), (*timeHistory)[i]);
                ImGui::TextUnformatted(buf);

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
        // 多行 key：只参数化 x（y 保持 ImGui 自然流）
        const TextStyle ls = ResolveStyle(cfg_, "menu_controls_lines", TextStyle{80.0f, 0.0f, 20, 255, 255, 255, 255, 0, false});
        auto drawLine = [&](const char* text) {
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
        drawLine("游戏中");
        drawLine("W / A / S / D  或  方向键：移动");
        drawLine("Shift：蓄力冲刺；Ctrl：聚焦视野");
        drawLine("鼠标左键/空格：攻击；1/2/3：切换武器；F：拾取/丢弃");
        drawLine("ESC：暂停游戏；F11：全屏；F1：静音");
        ImGui::SetCursorPosY(500.0f);
        if (bigButton("返回")) doClick([&]{ actions.goState(MAIN_MENU); });
    } else if (ctx.state == SETTINGS) {
        drawStatic("menu_settings_title", "设置", TextStyle{0.0f, 120.0f, 20, 255, 255, 255, 255, 0, false});
        ImGui::Separator();

        int master = ctx.masterVolume;
        int music = ctx.musicVolume;
        int effects = ctx.effectsVolume;

        // 固定标签列宽 + 滑条列宽，整体水平居中，避免行内标签宽度不同导致滑条起点漂移。
        const float labelW = 90.0f;
        const float sliderW = 360.0f;
        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const float startX = (800.0f - (labelW + gap + sliderW)) * 0.5f;

        auto volumeSlider = [&](const char* label, const char* id, int* value, const std::function<void(int)>& apply) {
            ImGui::SetCursorPosX(startX);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(startX + labelW + gap);
            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderInt(id, value, 0, 100)) apply(*value);
        };
        volumeSlider("主音量", "##master", &master, actions.setMasterVolume);
        volumeSlider("音乐", "##music", &music, actions.setMusicVolume);
        volumeSlider("音效", "##effects", &effects, actions.setEffectsVolume);

        const float muteW = 220.0f;
        ImGui::SetCursorPosX((800.0f - muteW) * 0.5f);
        if (ImGui::Button(ctx.muted ? "已静音（点击取消）" : "切换静音", ImVec2(muteW, 0.0f))) {
            actions.toggleMute();
        }
        ImGui::SetCursorPosY(500.0f);
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
        ImGui::SetCursorPosX(145.0f);
        if (ImGui::Button("算了，那我玩完吧", ImVec2(245, 58))) doClick([&]{ actions.goState(PAUSED); });
        ImGui::SetCursorPosX(410.0f);
        if (ImGui::Button("老子不玩了", ImVec2(245, 58))) doClick([&]{ actions.quitApp(); });
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void GameUI::DrawInGame(const InGameCtx& ctx, ImGuiUI* imGuiUI) {
    if (ctx.state == PLAYING) {
        // 武器状态文字：panel = {640,536,150,54}
        std::string line = ctx.weaponName;
        if (ctx.magazineSize > 0) {
            line += " " + std::to_string(ctx.ammo) + "/" + std::to_string(ctx.magazineSize);
        }
        const TextStyle ws = ResolveStyle(cfg_, "weapon_status", TextStyle{0.0f, 549.0f, 18, 230, 238, 235, 255, 0, true});
        const float textX = ws.anchored ? (640.0f + (ctx.hasWeaponIcon ? 50.0f : 12.0f)) : ws.x;
        DrawImGuiText(imGuiUI, line.c_str(), textX, ws.y, ws.r, ws.g, ws.b, ws.a, ws.size);

        // 无武器提示（居中，y=512）
        if (ctx.hasNoWeaponHint) {
            const TextStyle nh = ResolveStyle(cfg_, "no_weapon_hint", TextStyle{0.0f, 512.0f, 22, 245, 200, 90, 255, 1, false});
            if (nh.alignX == 1) {
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
            DrawImGuiText(imGuiUI, "失  败", ft.x, ft.y, ft.r, ft.g, ft.b, ft.a, ft.size, ft.alignX);
            DrawImGuiText(imGuiUI, "被怪物击杀，惨遭致死！", fs.x, fs.y, fs.r, fs.g, fs.b, fs.a, fs.size, fs.alignX);
        } else if (ctx.mode == TIME_ATTACK && ctx.diedByTrap) {
            DrawImGuiText(imGuiUI, "失  败", ft.x, ft.y, ft.r, ft.g, ft.b, ft.a, ft.size, ft.alignX);
            DrawImGuiText(imGuiUI, "触发致命陷阱，惨遭致死！", fs.x, fs.y, fs.r, fs.g, fs.b, fs.a, fs.size, fs.alignX);
        } else {
            DrawImGuiText(imGuiUI, "本局结束", et.x, et.y, et.r, et.g, et.b, et.a, et.size, et.alignX);
        }

        std::string result;
        if (ctx.mode == TIME_ATTACK) {
            result = "本局得分：" + std::to_string(ctx.score);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "通关用时：%.2f 秒", ctx.gameTimer);
            result = buf;
        }
        DrawImGuiText(imGuiUI, result.c_str(), res.x, res.y, res.r, res.g, res.b, res.a, res.size, res.alignX);
        DrawImGuiText(imGuiUI, "Enter / 空格：返回模式选择", ht.x, ht.y, ht.r, ht.g, ht.b, ht.a, ht.size, ht.alignX);
    }
}
