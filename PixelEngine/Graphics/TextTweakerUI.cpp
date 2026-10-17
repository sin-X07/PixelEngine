#include "TextTweakerUI.hpp"
#include "TextConfig.hpp"
#include "../Core/PathUtils.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include <cstdlib>

void TextTweakerUI::Draw(TextConfig& config) {
    ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("文字调参面板 (F2 关闭)", nullptr,
                 ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextUnformatted("拖动下方控件实时生效；字号就近取档 {16,18,20,22,24,30,34}");
    ImGui::Separator();

    // 顶部操作
    if (ImGui::Button("保存全部")) {
        config.Save(PathUtils::ResolveWritablePath("assets/config/text.ini"));
    }
    ImGui::SameLine();
    if (ImGui::Button("重置全部")) {
        config.ResetAll();
    }
    ImGui::Separator();

    // [global] 区：字体文件（只读，需重启生效）
    ImGui::TextUnformatted("[global] 字体文件（改动需重启生效）");
    ImGui::TextWrapped("%s", config.GlobalFontPath().c_str());
    ImGui::Separator();

    // 每 key 折叠头
    for (const std::string& key : config.Keys()) {
        TextStyle& s = config.GetMutable(key);
        if (!ImGui::CollapsingHeader(key.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        ImGui::PushID(key.c_str());

        // anchored 时禁用 X/Y
        if (s.anchored) {
            ImGui::BeginDisabled();
            ImGui::SliderFloat("X", &s.x, 0.0f, 800.0f, "%.0f");
            ImGui::SliderFloat("Y", &s.y, 0.0f, 600.0f, "%.0f");
            ImGui::EndDisabled();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(anchored: X/Y 由调用点决定)");
        } else {
            ImGui::SliderFloat("X", &s.x, 0.0f, 800.0f, "%.0f");
            ImGui::SliderFloat("Y", &s.y, 0.0f, 600.0f, "%.0f");
        }

        // 字号档位：16~34 步进 2
        int size = s.size;
        if (ImGui::SliderInt("字号", &size, 16, 34)) {
            // 就近取档位
            const int steps[] = {16, 18, 20, 22, 24, 30, 34};
            int best = steps[0];
            int bestDiff = std::abs(steps[0] - size);
            for (int v : steps) {
                int d = std::abs(v - size);
                if (d < bestDiff) { bestDiff = d; best = v; }
            }
            s.size = best;
        }

        // 颜色 ColorEdit4
        float col[4] = { s.r / 255.0f, s.g / 255.0f, s.b / 255.0f, s.a / 255.0f };
        if (ImGui::ColorEdit4("颜色", col, ImGuiColorEditFlags_AlphaBar)) {
            s.r = static_cast<Uint8>(col[0] * 255.0f);
            s.g = static_cast<Uint8>(col[1] * 255.0f);
            s.b = static_cast<Uint8>(col[2] * 255.0f);
            s.a = static_cast<Uint8>(col[3] * 255.0f);
        }

        // alignX 与 anchored 勾选
        bool align = (s.alignX != 0);
        if (ImGui::Checkbox("水平居中 (alignX)", &align)) {
            s.alignX = align ? 1 : 0;
        }
        ImGui::Checkbox("anchored", &s.anchored);

        if (ImGui::Button("重置本项")) {
            TextConfig def;
            s = def.Get(key);
        }
        ImGui::PopID();
    }

    ImGui::End();
}
