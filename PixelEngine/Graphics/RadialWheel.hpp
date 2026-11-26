#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ============================================================================
// RadialWheel — 共享双层扇形轮盘（几何 + 状态机 + ImGui 材质绘制）
//
// 用于主菜单小鸡定制（3 扇区 / 3-4-7 扇区）与局内购买轮盘（6 扇区 / 动态条目）。
// 布局、数字提示与命中共用同一组起止角；首扇区中心固定在十二点方向 (-PI/2)，
// 其余顺时针排列。内半径默认等于外半径的 30%，中心独立处理返回。
// ============================================================================

namespace RadialWheel {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr float kDefaultInnerRatio = 0.30f;

enum class HitRegion {
    Outside = 0,
    Center,
    Gap,
    Sector
};

struct HitResult {
    HitRegion region = HitRegion::Outside;
    int       sector = -1;
};

struct SectorAngles {
    float start = 0.0f;
    float end   = 0.0f;
    float mid   = 0.0f;
};

struct Layout {
    float centerX      = 0.0f;
    float centerY      = 0.0f;
    float innerRadius  = 60.0f;
    float outerRadius  = 200.0f;
    int   sectorCount  = 0;
    float gapHalfAngle = 0.022f; // 扇区半间隙（弧度）；N<=1 时忽略

    SectorAngles AnglesFor(int index) const {
        SectorAngles out;
        if (sectorCount <= 0) return out;
        const float step = kTwoPi / static_cast<float>(sectorCount);
        const float gap  = (sectorCount > 1) ? std::min(gapHalfAngle, step * 0.18f) : 0.0f;
        out.mid   = -kPi * 0.5f + static_cast<float>(index) * step;
        out.start = out.mid - step * 0.5f + gap;
        out.end   = out.mid + step * 0.5f - gap;
        return out;
    }

    HitResult HitTest(float px, float py) const {
        const float dx = px - centerX;
        const float dy = py - centerY;
        const float dist = std::hypot(dx, dy);
        if (dist <= innerRadius) return { HitRegion::Center, -1 };
        if (dist > outerRadius || sectorCount <= 0) return { HitRegion::Outside, -1 };

        const float step = kTwoPi / static_cast<float>(sectorCount);
        const float gap  = (sectorCount > 1) ? std::min(gapHalfAngle, step * 0.18f) : 0.0f;

        // 相对首扇区起始边界 (-PI/2 - step/2) 归约到 [0, 2*PI)，消除十二点接缝跳变
        const float start0 = -kPi * 0.5f - step * 0.5f;
        float rel = std::fmod(std::atan2(dy, dx) - start0, kTwoPi);
        if (rel < 0.0f) rel += kTwoPi;

        int idx = static_cast<int>(std::floor(rel / step));
        idx = std::clamp(idx, 0, sectorCount - 1);

        const float localFromMid = rel - (static_cast<float>(idx) + 0.5f) * step;
        if (std::fabs(localFromMid) > (step * 0.5f - gap)) {
            return { HitRegion::Gap, -1 };
        }
        return { HitRegion::Sector, idx };
    }
};

struct InputSnapshot {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool  leftDown = false;
    bool  leftPressed = false;
    bool  leftReleased = false;
    bool  rightDown = false;
    bool  rightPressed = false;
    bool  rightReleased = false;
    bool  escPressed = false;
    bool  enterPressed = false;
    int   dirStep = 0;        // -1=逆时针(Left/Up), +1=顺时针(Right/Down)
    int   digitPressed = 0;   // 1..9，0=无
    bool  windowFocused = true;
    bool  suppressNav = false; // 开闭快捷键恰为导航键时，本帧只开关不触发导航
};

struct Action {
    enum class Type {
        None = 0,
        EnterCategory,
        SelectItem,
        RefundItem,
        Back,
        Close
    };
    Type type = Type::None;
    int  sector = -1;
    int  hoveredSector = -1;
    bool hoveredCenter = false;
};

struct WheelState {
    int       level = 0;            // 0=一级 1=二级
    int       activeCategory = -1;  // 进入二级时选中的一级下标
    int       kbIndex = -1;         // 方向键轮选下标（-1=无）
    bool      armed = false;        // 打开后至少观测过一次左键未按，忽略打开瞬间残留松键
    int       pressedButton = 0;    // 0=无 1=左键 2=右键
    int       pressedLevel = 0;
    HitRegion pressedRegion = HitRegion::Outside;
    int       pressedSector = -1;

    void ClearPressed() {
        pressedButton = 0;
        pressedLevel  = level;
        pressedRegion = HitRegion::Outside;
        pressedSector = -1;
    }

    void ResetOnOpen(bool leftMouseCurrentlyDown) {
        level = 0;
        activeCategory = -1;
        kbIndex = -1;
        ClearPressed();
        armed = !leftMouseCurrentlyDown;
    }

    void EnterSecondLevel(int categoryIdx) {
        level = 1;
        activeCategory = categoryIdx;
        kbIndex = -1;
        ClearPressed();
    }

    void BackToFirstLevel() {
        level = 0;
        kbIndex = -1;
        ClearPressed();
    }

    Action Step(const Layout& layout, const InputSnapshot& in,
                const std::vector<bool>& sectorEnabled = {}) {
        Action out;
        if (!in.windowFocused) {
            ClearPressed();
            armed = false;
            return out;
        }

        const HitResult hit = layout.HitTest(in.mouseX, in.mouseY);
        out.hoveredSector = (hit.region == HitRegion::Sector) ? hit.sector : -1;
        out.hoveredCenter = (hit.region == HitRegion::Center);

        if (!armed) {
            if (!in.leftDown && !in.leftPressed && !in.leftReleased) {
                armed = true;
            }
        }

        if (in.suppressNav) {
            if (in.leftReleased || in.rightReleased) ClearPressed();
            return out;
        }

        // 1) Esc：二级退一级，一级关闭
        if (in.escPressed) {
            out.type = (level == 1) ? Action::Type::Back : Action::Type::Close;
            if (level == 1) BackToFirstLevel();
            else ClearPressed();
            return out;
        }

        // 2) 数字快捷键 1..N（每按键只触发一次）
        if (in.digitPressed >= 1 && in.digitPressed <= layout.sectorCount) {
            const int s = in.digitPressed - 1;
            const bool canEnter = (s < static_cast<int>(sectorEnabled.size())) ? sectorEnabled[s] : true;
            if (level == 0) {
                if (canEnter) {
                    out.type = Action::Type::EnterCategory;
                    out.sector = s;
                    return out;
                }
            } else {
                out.type = Action::Type::SelectItem;
                out.sector = s;
                return out;
            }
        }

        // 3) 方向键轮选 + Enter 确认
        if (in.dirStep != 0 && layout.sectorCount > 0) {
            const int n = layout.sectorCount;
            if (kbIndex < 0 || kbIndex >= n) {
                if (out.hoveredSector >= 0) {
                    kbIndex = (out.hoveredSector + in.dirStep + n) % n;
                } else {
                    kbIndex = (in.dirStep > 0) ? 0 : (n - 1);
                }
            } else {
                kbIndex = (kbIndex + in.dirStep + n) % n;
            }
        }
        if (in.enterPressed && kbIndex >= 0 && kbIndex < layout.sectorCount) {
            const bool canEnter = (kbIndex < static_cast<int>(sectorEnabled.size())) ? sectorEnabled[kbIndex] : true;
            if (level == 0) {
                if (canEnter) {
                    out.type = Action::Type::EnterCategory;
                    out.sector = kbIndex;
                    return out;
                }
            } else {
                out.type = Action::Type::SelectItem;
                out.sector = kbIndex;
                return out;
            }
        }

        // 4) 鼠标左键：按下与释放须在同一有效区域（或同一扇区）且未跨层
        if (armed) {
            if (in.leftPressed && pressedButton == 0) {
                pressedButton = 1;
                pressedLevel  = level;
                pressedRegion = hit.region;
                pressedSector = hit.sector;
            }
            if (in.leftReleased) {
                if (pressedButton == 1 && pressedLevel == level) {
                    if (pressedRegion == HitRegion::Center && hit.region == HitRegion::Center) {
                        out.type = (level == 1) ? Action::Type::Back : Action::Type::Close;
                    } else if (pressedRegion == HitRegion::Sector &&
                               hit.region == HitRegion::Sector &&
                               pressedSector == hit.sector) {
                        const bool canEnter = (pressedSector < static_cast<int>(sectorEnabled.size()))
                                            ? sectorEnabled[pressedSector] : true;
                        if (level == 0) {
                            if (canEnter) {
                                out.type = Action::Type::EnterCategory;
                                out.sector = pressedSector;
                            }
                        } else {
                            out.type = Action::Type::SelectItem;
                            out.sector = pressedSector;
                        }
                    }
                }
                ClearPressed();
                if (out.type != Action::Type::None) return out;
            }
        }

        // 5) 鼠标右键：二级扇区上按下并释放触发退款
        if (in.rightPressed && pressedButton == 0 && level == 1) {
            pressedButton = 2;
            pressedLevel  = level;
            pressedRegion = hit.region;
            pressedSector = hit.sector;
        }
        if (in.rightReleased) {
            if (pressedButton == 2 && pressedLevel == level && level == 1 &&
                pressedRegion == HitRegion::Sector && hit.region == HitRegion::Sector &&
                pressedSector == hit.sector) {
                out.type = Action::Type::RefundItem;
                out.sector = pressedSector;
            }
            ClearPressed();
            if (out.type != Action::Type::None) return out;
        }

        return out;
    }
};

} // namespace RadialWheel

#ifndef RADIAL_WHEEL_LOGIC_ONLY
#include "ControlMaterial.hpp"
#include "Palette.hpp"
#include <SDL2/SDL.h>

namespace RadialWheel {

struct SectorItem {
    std::string  id;
    std::string  label;
    std::string  subLabel;       // 副标题 / 价格 / 数量
    std::string  disabledReason; // 禁用原因（仍可悬停查看）
    SDL_Texture* icon = nullptr; // 借用纹理，不释放
    bool         hasSwatch = false;
    ImU32        swatchColor = 0;
    bool         selected = false;   // 已应用 / 已装备（带勾 + 香槟边）
    bool         enabled = true;     // 一级是否可进入 / 二级是否可购买
    bool         refundable = false; // 可退款标记
    int          refundAmount = 0;
};

struct DrawStyle {
    ImGuiUI*    ui = nullptr;
    float       opacity = 1.0f;
    std::string centerTitle;
    std::string centerSubtitle;
    bool        centerIsBack = false;
    int         hoveredSector = -1;
    bool        hoveredCenter = false;
    int         pressedSector = -1;
    bool        pressedCenter = false;
    int         kbIndex = -1;
};

inline ImVec2 Polar(float cx, float cy, float r, float angle) {
    return ImVec2(cx + std::cos(angle) * r, cy + std::sin(angle) * r);
}

// 用相邻内外弧采样点构成三角形绘制环形扇区（含渐变与颗粒裁切）
inline void DrawAnnularSector(ImDrawList* dl, ImGuiUI* ui,
                              float cx, float cy, float rIn, float rOut,
                              float a0, float a1, int segments,
                              ImU32 colOuter, ImU32 colInner,
                              ImU32 borderCol, float borderThick,
                              float grainOpacity) {
    if (!dl || segments < 2 || a1 <= a0 || rOut <= rIn) return;
    const float da = (a1 - a0) / static_cast<float>(segments);

    // 1) 填充渐变扇环（每段两个三角形）
    dl->PrimReserve(segments * 6, (segments + 1) * 2);
    const ImDrawIdx baseIdx = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    for (int i = 0; i <= segments; ++i) {
        const float a = a0 + da * static_cast<float>(i);
        const float cs = std::cos(a), sn = std::sin(a);
        dl->PrimWriteVtx(ImVec2(cx + cs * rIn,  cy + sn * rIn),  uv, colInner);
        dl->PrimWriteVtx(ImVec2(cx + cs * rOut, cy + sn * rOut), uv, colOuter);
    }
    for (int i = 0; i < segments; ++i) {
        const ImDrawIdx i0 = static_cast<ImDrawIdx>(baseIdx + i * 2);
        const ImDrawIdx i1 = static_cast<ImDrawIdx>(i0 + 1);
        const ImDrawIdx i2 = static_cast<ImDrawIdx>(i0 + 2);
        const ImDrawIdx i3 = static_cast<ImDrawIdx>(i0 + 3);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx(i1); dl->PrimWriteIdx(i3);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx(i3); dl->PrimWriteIdx(i2);
    }

    // 2) 随扇形裁切的细颗粒材质
    if (ui && ui->ControlGrain() && grainOpacity > 0.01f) {
        const ImTextureID grainTex = reinterpret_cast<ImTextureID>(ui->ControlGrain());
        const float span = std::max(1.0f, UI(512.0f));
        const ImU32 gcol = IM_COL32(255, 255, 255,
            static_cast<int>(std::clamp(grainOpacity * 255.0f, 0.0f, 255.0f)));
        dl->PushTextureID(grainTex);
        dl->PrimReserve(segments * 6, (segments + 1) * 2);
        const ImDrawIdx gBase = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        for (int i = 0; i <= segments; ++i) {
            const float a = a0 + da * static_cast<float>(i);
            const float cs = std::cos(a), sn = std::sin(a);
            const ImVec2 pIn (cx + cs * rIn,  cy + sn * rIn);
            const ImVec2 pOut(cx + cs * rOut, cy + sn * rOut);
            const float uIn  = std::clamp((pIn.x  - (cx - rOut)) / span, 0.0f, 1.0f);
            const float vIn  = std::clamp((pIn.y  - (cy - rOut)) / span, 0.0f, 1.0f);
            const float uOut = std::clamp((pOut.x - (cx - rOut)) / span, 0.0f, 1.0f);
            const float vOut = std::clamp((pOut.y - (cy - rOut)) / span, 0.0f, 1.0f);
            dl->PrimWriteVtx(pIn,  ImVec2(uIn,  vIn),  gcol);
            dl->PrimWriteVtx(pOut, ImVec2(uOut, vOut), gcol);
        }
        for (int i = 0; i < segments; ++i) {
            const ImDrawIdx i0 = static_cast<ImDrawIdx>(gBase + i * 2);
            const ImDrawIdx i1 = static_cast<ImDrawIdx>(i0 + 1);
            const ImDrawIdx i2 = static_cast<ImDrawIdx>(i0 + 2);
            const ImDrawIdx i3 = static_cast<ImDrawIdx>(i0 + 3);
            dl->PrimWriteIdx(i0); dl->PrimWriteIdx(i1); dl->PrimWriteIdx(i3);
            dl->PrimWriteIdx(i0); dl->PrimWriteIdx(i3); dl->PrimWriteIdx(i2);
        }
        dl->PopTextureID();
    }

    // 3) 扇区描边（外弧 + 内弧 + 两条侧边）
    if (borderThick > 0.0f) {
        dl->PathClear();
        dl->PathArcTo(ImVec2(cx, cy), rOut, a0, a1, segments);
        dl->PathArcTo(ImVec2(cx, cy), rIn,  a1, a0, segments);
        dl->PathStroke(borderCol, ImDrawFlags_Closed, borderThick);
    }
}

inline std::string FitTextWidth(ImFont* font, float fontSize, const std::string& text, float maxW) {
    if (!font || maxW <= 4.0f) return text;
    if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x <= maxW) return text;
    std::string res = text;
    while (!res.empty()) {
        size_t end = res.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(res[end]) & 0xC0) == 0x80) --end;
        res.resize(end);
        if (res.empty()) break;
        const std::string cand = res + "…";
        if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, cand.c_str()).x <= maxW) return cand;
    }
    return "…";
}

inline void DrawCheckmark(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float thick) {
    const ImVec2 p0(center.x - size * 0.45f, center.y + size * 0.05f);
    const ImVec2 p1(center.x - size * 0.10f, center.y + size * 0.42f);
    const ImVec2 p2(center.x + size * 0.50f, center.y - size * 0.38f);
    dl->AddLine(p0, p1, col, thick);
    dl->AddLine(p1, p2, col, thick);
}

inline void DrawWheel(ImDrawList* dl, const Layout& layout,
                      const std::vector<SectorItem>& items, const DrawStyle& style) {
    if (!dl || layout.sectorCount <= 0) return;
    const float op = std::clamp(style.opacity, 0.0f, 1.0f);
    if (op <= 0.01f) return;

    const float cx = layout.centerX;
    const float cy = layout.centerY;
    const float rIn = layout.innerRadius;
    const float rOut = layout.outerRadius;
    const int n = static_cast<int>(items.size());

    // 底层环晕（柔和分离背景与轮盘）
    dl->AddCircleFilled(ImVec2(cx, cy), rOut + UI(6.0f),
                        Controls::Tint(8, 12, 17, 170, op), 72);
    dl->AddCircle(ImVec2(cx, cy), rOut + UI(6.0f),
                  Controls::Tint(164, 148, 108, 60, op), 72, UI(1.2f));

    ImFont* baseFont = style.ui ? style.ui->GetFont(16) : ImGui::GetFont();
    if (!baseFont) baseFont = ImGui::GetFont();

    const int segs = std::clamp(160 / std::max(1, n), 16, 42);
    const float spanAngle = kTwoPi / static_cast<float>(std::max(1, n));
    const float ringThick = rOut - rIn;

    for (int i = 0; i < n; ++i) {
        const SectorItem& it = items[i];
        const SectorAngles ang = layout.AnglesFor(i);
        const bool hovered = (style.hoveredSector == i) || (style.kbIndex == i);
        const bool pressed = (style.pressedSector == i);

        // 配色：复用 Smoke 烟色半透底 + 香槟色选中/悬停边
        int topR = 52, topG = 60, topB = 66, topA = 218;
        int botR = 22, botG = 28, botB = 34, botA = 228;
        if (!it.enabled) {
            topR = 32; topG = 37; topB = 42; topA = 192;
            botR = 16; botG = 20; botB = 25; botA = 204;
        } else if (it.selected) {
            topR = 78; topG = 72; topB = 54; topA = 232;
            botR = 36; botG = 37; botB = 34; botA = 238;
        }
        if (hovered && it.enabled) {
            topR += 18; topG += 20; topB += 22;
            botR += 12; botG += 14; botB += 16;
        } else if (hovered) {
            topR += 8; topG += 9; topB += 10;
        }
        if (pressed) {
            std::swap(topR, botR);
            std::swap(topG, botG);
            std::swap(topB, botB);
        }

        const ImU32 colOuter = Controls::Tint(topR, topG, topB, topA, op);
        const ImU32 colInner = Controls::Tint(botR, botG, botB, botA, op);
        ImU32 borderCol = Controls::Tint(175, 186, 194, it.enabled ? 78 : 42, op);
        float borderThick = std::max(1.0f, UI(1.2f));
        if (it.selected) {
            borderCol = Controls::Tint(232, 189, 113, 240, op);
            borderThick = UI(2.2f);
        }
        if (hovered) {
            borderCol = it.enabled ? Controls::Tint(246, 212, 138, 255, op)
                                   : Controls::Tint(160, 172, 184, 200, op);
            borderThick = UI(2.4f);
        }

        DrawAnnularSector(dl, style.ui, cx, cy, rIn, rOut, ang.start, ang.end, segs,
                          colOuter, colInner, borderCol, borderThick,
                          (it.enabled ? 0.24f : 0.12f) * op);

        // 选中内弧高光条（香槟色）
        if (it.selected) {
            dl->PathClear();
            dl->PathArcTo(ImVec2(cx, cy), rOut - UI(3.5f), ang.start + 0.02f, ang.end - 0.02f, segs);
            dl->PathStroke(Controls::Tint(232, 189, 113, 210, op), false, UI(3.0f));
        }

        // ---- 扇区内元素布局 ----
        const float rMid = rIn + ringThick * 0.52f;
        const ImVec2 midPt = Polar(cx, cy, rMid, ang.mid);
        // 扇区在 rMid 处的可用弦宽（用于文字和图标防越界）
        const float chordW = 2.0f * rMid * std::sin(std::min(spanAngle * 0.44f, 0.72f));
        const float maxContentW = std::max(UI(46.0f), std::min(chordW * 0.86f, ringThick * 0.92f));

        // 1) 数字角标（靠内半径侧）
        const float rBadge = rIn + std::min(UI(20.0f), ringThick * 0.16f);
        const ImVec2 badgePt = Polar(cx, cy, rBadge, ang.mid);
        char numBuf[8];
        std::snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
        const float numSize = UI(12.0f);
        const ImVec2 numSz = baseFont->CalcTextSizeA(numSize, FLT_MAX, 0.0f, numBuf);
        dl->AddCircleFilled(badgePt, UI(9.5f),
                            Controls::Tint(hovered ? 228 : 26, hovered ? 186 : 34, hovered ? 102 : 42, 220, op), 16);
        dl->AddText(baseFont, numSize,
                    ImVec2(badgePt.x - numSz.x * 0.5f, badgePt.y - numSz.y * 0.5f),
                    hovered ? Controls::Tint(28, 22, 14, 255, op)
                            : Controls::Tint(210, 220, 230, it.enabled ? 235 : 140, op),
                    numBuf);

        // 2) 右上/外侧状态标：已应用勾选 ✓、可退款标记 ↺
        if (it.selected || it.refundable) {
            const float rTag = rOut - UI(18.0f);
            const float aTag = ang.mid + std::min(0.22f, spanAngle * 0.24f);
            const ImVec2 tagPt = Polar(cx, cy, rTag, aTag);
            if (it.selected) {
                dl->AddCircleFilled(tagPt, UI(9.5f), Controls::Tint(46, 86, 62, 235, op), 16);
                dl->AddCircle(tagPt, UI(9.5f), Controls::Tint(138, 226, 168, 240, op), 16, UI(1.2f));
                DrawCheckmark(dl, tagPt, UI(10.0f), Controls::Tint(176, 246, 198, 255, op), UI(2.0f));
            }
            if (it.refundable) {
                const float aRef = ang.mid - std::min(0.22f, spanAngle * 0.24f);
                const ImVec2 refPt = Polar(cx, cy, rTag, aRef);
                dl->AddCircleFilled(refPt, UI(9.5f), Controls::Tint(54, 78, 102, 235, op), 16);
                dl->AddCircle(refPt, UI(9.5f), Controls::Tint(146, 206, 250, 240, op), 16, UI(1.2f));
                const float rSz = UI(11.0f);
                const ImVec2 tSz = baseFont->CalcTextSizeA(rSz, FLT_MAX, 0.0f, "退");
                dl->AddText(baseFont, rSz, ImVec2(refPt.x - tSz.x * 0.5f, refPt.y - tSz.y * 0.5f),
                            Controls::Tint(214, 238, 255, 255, op), "退");
            }
        }

        // 3) 图标 / 色样 + 主标题 + 副标题（整体垂直居中于 midPt.y）
        const float labelSize = UI(n >= 7 ? 14.0f : 15.5f);
        const float subSize   = UI(n >= 7 ? 12.0f : 13.0f);
        const std::string fitLabel = FitTextWidth(baseFont, labelSize, it.label, maxContentW);
        const ImVec2 lblSz = baseFont->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, fitLabel.c_str());
        std::string fitSub;
        ImVec2 subSz(0.0f, 0.0f);
        if (!it.subLabel.empty()) {
            fitSub = FitTextWidth(baseFont, subSize, it.subLabel, maxContentW);
            subSz = baseFont->CalcTextSizeA(subSize, FLT_MAX, 0.0f, fitSub.c_str());
        }

        float iw = 0.0f, ih = 0.0f;
        bool drawIcon = false;
        if (it.icon) {
            int tw = 0, th = 0;
            if (SDL_QueryTexture(it.icon, nullptr, nullptr, &tw, &th) == 0 && tw > 0 && th > 0) {
                const float boxW = std::min(maxContentW * 0.88f, UI(76.0f));
                const float boxH = std::min(ringThick * 0.28f, UI(34.0f));
                const float fit  = std::min(boxW / static_cast<float>(tw), boxH / static_cast<float>(th));
                iw = static_cast<float>(tw) * fit;
                ih = static_cast<float>(th) * fit;
                drawIcon = true;
            }
        }

        const float swR = UI(11.0f);
        const float visualH = it.hasSwatch ? (swR * 2.0f + UI(6.0f)) : (drawIcon ? (ih + UI(4.0f)) : 0.0f);
        const float subH = it.subLabel.empty() ? 0.0f : (subSz.y + UI(2.0f));
        const float totalH = visualH + lblSz.y + subH;
        float curY = midPt.y - totalH * 0.5f;

        if (it.hasSwatch) {
            const ImVec2 swC(midPt.x, curY + swR);
            dl->AddCircleFilled(swC, swR + UI(2.0f), Controls::Tint(12, 16, 20, 220, op), 24);
            dl->AddCircleFilled(swC, swR, it.swatchColor, 24);
            dl->AddCircle(swC, swR, Controls::Tint(255, 255, 255, 190, op), 24, UI(1.2f));
            curY += swR * 2.0f + UI(6.0f);
        } else if (drawIcon) {
            const ImVec2 p0(midPt.x - iw * 0.5f, curY);
            const ImVec2 p1(p0.x + iw, p0.y + ih);
            dl->AddImage(reinterpret_cast<ImTextureID>(it.icon), p0, p1, ImVec2(0, 0), ImVec2(1, 1),
                         Controls::Tint(255, 255, 255, it.enabled ? 245 : 135, op));
            curY += ih + UI(4.0f);
        }

        const ImU32 lblCol = !it.enabled ? Controls::Tint(146, 156, 166, 210, op)
                           : (hovered || it.selected) ? Controls::Tint(252, 232, 182, 255, op)
                                                      : Controls::Tint(232, 238, 244, 255, op);
        dl->AddText(baseFont, labelSize, ImVec2(midPt.x - lblSz.x * 0.5f, curY), lblCol, fitLabel.c_str());

        if (!it.subLabel.empty()) {
            curY += lblSz.y + UI(2.0f);
            const ImU32 subCol = it.selected ? Controls::Tint(142, 218, 168, 245, op)
                               : it.enabled  ? Controls::Tint(218, 186, 114, 240, op)
                                             : Controls::Tint(198, 112, 102, 225, op);
            dl->AddText(baseFont, subSize, ImVec2(midPt.x - subSz.x * 0.5f, curY), subCol, fitSub.c_str());
        }
    }

    // ---- 中心返回区 ----
    const ImU32 hubFill = style.pressedCenter
        ? Controls::Tint(36, 44, 52, 245, op)
        : style.hoveredCenter
            ? Controls::Tint(58, 68, 78, 245, op)
            : Controls::Tint(22, 28, 35, 240, op);
    const ImU32 hubRim = style.hoveredCenter
        ? Controls::Tint(242, 204, 128, 255, op)
        : Controls::Tint(186, 162, 104, 175, op);
    dl->AddCircleFilled(ImVec2(cx, cy), rIn - UI(2.0f), hubFill, 48);
    dl->AddCircle(ImVec2(cx, cy), rIn - UI(2.0f), hubRim, 48, style.hoveredCenter ? UI(2.2f) : UI(1.5f));

    if (!style.centerTitle.empty()) {
        const float cTitleSz = UI(15.0f);
        const float cSubSz   = UI(12.5f);
        const float maxHubW  = std::max(UI(36.0f), (rIn - UI(10.0f)) * 1.75f);
        const std::string fitT = FitTextWidth(baseFont, cTitleSz, style.centerTitle, maxHubW);
        const ImVec2 tSz = baseFont->CalcTextSizeA(cTitleSz, FLT_MAX, 0.0f, fitT.c_str());
        const bool hasSub = !style.centerSubtitle.empty();
        const float tY = cy - (hasSub ? UI(16.0f) : tSz.y * 0.5f);
        dl->AddText(baseFont, cTitleSz, ImVec2(cx - tSz.x * 0.5f, tY),
                    Controls::Tint(244, 246, 250, 255, op), fitT.c_str());
        if (hasSub) {
            const std::string fitS = FitTextWidth(baseFont, cSubSz, style.centerSubtitle, maxHubW);
            const ImVec2 sSz = baseFont->CalcTextSizeA(cSubSz, FLT_MAX, 0.0f, fitS.c_str());
            dl->AddText(baseFont, cSubSz, ImVec2(cx - sSz.x * 0.5f, tY + tSz.y + UI(3.0f)),
                        style.hoveredCenter ? Controls::Tint(246, 212, 138, 255, op)
                                            : Controls::Tint(162, 184, 204, 235, op),
                        fitS.c_str());
        }
    }
}

inline InputSnapshot CollectImGuiInput(bool windowFocused, bool suppressNav) {
    const ImGuiIO& io = ImGui::GetIO();
    InputSnapshot snap;
    snap.mouseX = io.MousePos.x;
    snap.mouseY = io.MousePos.y;
    snap.leftDown     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    snap.leftPressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
    snap.leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    snap.rightDown     = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    snap.rightPressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Right, false);
    snap.rightReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    snap.escPressed    = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    snap.enterPressed  = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        snap.dirStep = -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) ||
               ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        snap.dirStep = +1;
    }
    for (int d = 0; d < 9; ++d) {
        const ImGuiKey mainK = static_cast<ImGuiKey>(ImGuiKey_1 + d);
        const ImGuiKey padK  = static_cast<ImGuiKey>(ImGuiKey_Keypad1 + d);
        if (ImGui::IsKeyPressed(mainK, false) || ImGui::IsKeyPressed(padK, false)) {
            snap.digitPressed = d + 1;
            break;
        }
    }
    snap.windowFocused = windowFocused;
    snap.suppressNav   = suppressNav;
    return snap;
}

} // namespace RadialWheel
#endif
