#pragma once
// ============================================================================
// Palette — 全局配色（一处改，全 UI 生效）
//
// 主题：深海军蓝底 + 沙漠金强调色，呼应 CS 系列的战术/金属质感。
// ============================================================================

#include "../ThirdParty/imgui/imgui.h"

namespace Palette {

// 背景层级
inline ImVec4 BgDeep()      { return ImVec4(0.043f, 0.059f, 0.078f, 1.00f); }
inline ImVec4 BgPanel()     { return ImVec4(0.075f, 0.098f, 0.125f, 1.00f); }
inline ImVec4 BgPanelHi()   { return ImVec4(0.110f, 0.140f, 0.180f, 1.00f); }
inline ImVec4 BgHover()     { return ImVec4(0.150f, 0.185f, 0.235f, 1.00f); }

// 边框
inline ImVec4 Border()      { return ImVec4(0.220f, 0.260f, 0.320f, 0.65f); }
inline ImVec4 BorderHi()    { return ImVec4(0.360f, 0.420f, 0.500f, 0.90f); }

// 强调色：沙漠金
inline ImVec4 Accent()      { return ImVec4(0.930f, 0.680f, 0.300f, 1.00f); }
inline ImVec4 AccentSoft()  { return ImVec4(0.930f, 0.680f, 0.300f, 0.55f); }
inline ImVec4 AccentDim()   { return ImVec4(0.700f, 0.510f, 0.225f, 1.00f); }

// 文本
inline ImVec4 Text()        { return ImVec4(0.925f, 0.940f, 0.960f, 1.00f); }
inline ImVec4 TextDim()     { return ImVec4(0.560f, 0.625f, 0.700f, 1.00f); }
inline ImVec4 TextFaint()   { return ImVec4(0.400f, 0.455f, 0.520f, 1.00f); }

// 语义色
inline ImVec4 Danger()      { return ImVec4(0.900f, 0.340f, 0.320f, 1.00f); }
inline ImVec4 Ally()        { return ImVec4(0.350f, 0.860f, 0.510f, 1.00f); }
inline ImVec4 Info()        { return ImVec4(0.420f, 0.720f, 0.950f, 1.00f); }

// 供 SDL / ImU32 使用
inline ImU32 U32(ImVec4 c)  { return ImGui::ColorConvertFloat4ToU32(c); }

} // namespace Palette
