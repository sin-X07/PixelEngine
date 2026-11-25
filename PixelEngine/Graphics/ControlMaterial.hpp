#pragma once

#include "ImGuiUI.hpp"
#include "UiScale.hpp"
#include "../ThirdParty/imgui/imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace Controls {

enum class Finish { Smoke, Steel, Champagne, Danger };

struct State {
    bool hover = false;
    bool down = false;
    bool selected = false;
    bool disabled = false;
};

inline State ItemState(bool selected = false) {
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    const bool hover = !disabled && ImGui::IsItemHovered();
    return {hover, hover && ImGui::IsItemActive(), selected, disabled};
}

inline ImU32 Tint(int r, int g, int b, int a, float opacity) {
    return ImGui::GetColorU32(ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f * opacity));
}

inline void Grain(ImGuiUI* ui, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float opacity) {
    if (!ui || !ui->ControlGrain() || opacity <= 0.f) return;
    const float span = UI(512);
    const ImVec2 uv(std::min(1.f, (b.x - a.x) / span), std::min(1.f, (b.y - a.y) / span));
    dl->AddImageRounded(reinterpret_cast<ImTextureID>(ui->ControlGrain()), a, b, ImVec2(0, 0), uv,
                        Tint(255, 255, 255, 255, opacity), radius);
}

inline void Surface(ImGuiUI* ui, ImDrawList* dl, ImVec2 a, ImVec2 b, Finish finish,
                    State state = {}, float opacity = 1.f, float radius = UI(5)) {
    if (opacity <= 0.f || !dl || b.x <= a.x || b.y <= a.y) return;
    struct FinishColors {
        ImU32 top, bottom;
        int opacity;
        float grain;
        int edge, highlight;
    };
    static constexpr FinishColors colors[] = {
        {IM_COL32(52, 60, 66, 255),   IM_COL32(22, 28, 34, 255),   216, .21f, 54, 38}, // Smoke
        {IM_COL32(79, 89, 97, 255),   IM_COL32(32, 41, 49, 255),   248, .39f, 85, 81}, // Steel
        {IM_COL32(228, 201, 146, 255), IM_COL32(163, 129, 78, 255), 248, .39f, 85, 81}, // Champagne
        {IM_COL32(125, 65, 59, 255),  IM_COL32(78, 36, 35, 255),   248, .39f, 85, 81}, // Danger
    };
    const auto& material = colors[static_cast<int>(finish)];
    // All palette channels leave room for the 12-level hover lift.
    const ImU32 lift = state.hover ? IM_COL32(12, 12, 12, 0) : 0;
    const ImU32 top = material.top + lift;
    const ImU32 bottom = material.bottom + (finish == Finish::Champagne ? lift : 0);
    const float rim = std::max(1.f, UI(1));
    // Shade the rounded silhouette itself so antialiased corners and fade opacity survive.
    const int first = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, Tint(255, 255, 255, material.opacity, opacity), radius);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, first, dl->VtxBuffer.Size, a,
        ImVec2(a.x + (b.y - a.y) * .12f, b.y), state.down ? bottom : top, state.down ? top : bottom);
    Grain(ui, dl, a, b, radius, opacity * (state.disabled ? .15f : material.grain));

    const int edge = state.disabled ? 33 : state.hover ? 132 : material.edge;
    dl->AddRect(a, b, Tint(215, 222, 218, edge, opacity), radius, 0, rim);
    const float inset = radius + rim;
    const float hiY = state.down ? b.y - 2 * rim : a.y + rim;
    dl->AddLine(ImVec2(a.x + inset, hiY), ImVec2(b.x - inset, hiY),
                Tint(252, 244, 222, state.disabled ? 16 : state.hover ? 118 : material.highlight, opacity), rim);
    if (!state.down)
        dl->AddLine(ImVec2(a.x + inset, b.y - 2 * rim), ImVec2(b.x - inset, b.y - 2 * rim), Tint(5, 9, 13, 110, opacity), rim);
    if (state.selected || (finish == Finish::Steel && state.hover)) {
        const float pad = std::min(UI(8), (b.y - a.y) * .22f);
        dl->AddRectFilled(ImVec2(a.x + rim, a.y + pad), ImVec2(a.x + rim + UI(3), b.y - pad),
                          Tint(232, 189, 113, 250, opacity), rim);
    }
}

inline bool Button(ImGuiUI* ui, const char* label, ImVec2 size, Finish finish = Finish::Smoke,
                   bool selected = false) {
    if (ImGui::GetCurrentWindowRead()->SkipItems) return false;
    // Submit the real widget once. Its ID, layout, focus, repeat and release rules stay native.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    if (!ImGui::IsItemVisible()) return pressed;
    const State state = ItemState(selected);
    const ImRect box(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    Surface(ui, ImGui::GetWindowDrawList(), box.Min, box.Max, finish, state);
    ImGui::RenderNavCursor(box, ImGui::GetItemID());
    const float offset = state.down ? std::max(1.f, UI(1)) : 0.f;
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    ImU32 ink = finish == Finish::Champagne ? IM_COL32(38, 31, 24, 255)
              : selected ? IM_COL32(250, 212, 148, 255)
              : state.hover ? IM_COL32(255, 255, 250, 255) : IM_COL32(232, 236, 237, 255);
    ImGui::PushStyleColor(ImGuiCol_Text, ink);
    ImGui::RenderTextClipped(ImVec2(box.Min.x + pad.x, box.Min.y + pad.y + offset),
        ImVec2(box.Max.x - pad.x, box.Max.y - pad.y + offset), label, nullptr, nullptr, ImGui::GetStyle().ButtonTextAlign, &box);
    ImGui::PopStyleColor();
    return pressed;
}

// Thin edge finishes for native tab headers and input frames. Leave labels, colour
// swatches, slider grabs and checkmarks entirely under the widget's own renderer.
enum class FieldShape { Frame, Square, Tab };

inline bool Field(ImGuiUI* ui, bool result, FieldShape shape = FieldShape::Frame) {
    if (!ImGui::IsItemVisible()) return result;
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (shape == FieldShape::Square) b.x = a.x + ImGui::GetFrameHeight();
    const State state = ItemState();
    const float r = std::max(1.f, UI(1));
    if (b.x - a.x <= 10 * r || b.y - a.y <= 6 * r) return result;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float opacity = state.disabled ? .38f : 1.f;
    const float y = state.down ? b.y - 2 * r : a.y + r;
    const ImVec2 start(a.x + UI(4), a.y + r), end(b.x - UI(4), a.y + 4 * r);
    dl->AddRectFilledMultiColor(start, end, Tint(215, 226, 233, 33, opacity), Tint(215, 226, 233, 24, opacity), 0, 0);
    Grain(ui, dl, start, end, 0, .35f * opacity);
    dl->AddLine(ImVec2(start.x, y), ImVec2(end.x, y), Tint(215, 227, 232, state.hover ? 115 : 62, opacity), r);
    if (shape == FieldShape::Tab && result)
        dl->AddLine(ImVec2(start.x, b.y - r), ImVec2(end.x, b.y - r), Tint(232, 189, 113, 255, opacity), 2 * r);
    return result;
}

} // namespace Controls
