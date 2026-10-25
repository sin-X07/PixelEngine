#include "ImGuiUI.hpp"
#include "UiScale.hpp"
#include "Palette.hpp"
#include <cmath>
#include "../Core/UiTextChars.hpp"
#include "../Core/PathUtils.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"
#include <iostream>
#include <cstdlib>
#include <string>

bool ImGuiUI::Initialize(SDL_Window* win, SDL_Renderer* ren) {
    window = win;
    renderer = ren;

    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 锁定 imgui.ini 到 exe 目录，避免多目录启动导致窗口布局丢失
    static std::string iniPath;
    iniPath = PathUtils::ResolveWritablePath("imgui.ini");
    io.IniFilename = iniPath.c_str();

    // —— 游戏风格深色主题（暗蓝灰调） ——
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.FrameRounding    = 7.0f;
        style.FrameBorderSize  = 1.0f;
        style.WindowRounding   = 0.0f;
        style.GrabRounding     = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.TabRounding      = 5.0f;
        style.ItemSpacing      = ImVec2(10.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 4.0f);
        style.FramePadding     = ImVec2(12.0f, 7.0f);

        ImVec4* colors = style.Colors;

        // 统一走 Palette（深海军蓝 + 沙漠金）
        colors[ImGuiCol_WindowBg]             = ImVec4(0.043f, 0.059f, 0.078f, 0.97f);
        colors[ImGuiCol_ChildBg]              = Palette::BgPanel();
        colors[ImGuiCol_PopupBg]              = ImVec4(0.075f, 0.098f, 0.125f, 0.98f);

        colors[ImGuiCol_Border]               = Palette::Border();
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_TitleBg]              = Palette::BgDeep();
        colors[ImGuiCol_TitleBgActive]        = Palette::BgPanel();
        colors[ImGuiCol_TitleBgCollapsed]     = Palette::BgDeep();

        // 按钮：暗金属底，悬停偏金
        colors[ImGuiCol_Button]               = ImVec4(0.120f, 0.150f, 0.190f, 1.00f);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.220f, 0.195f, 0.135f, 1.00f);
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.300f, 0.250f, 0.150f, 1.00f);

        colors[ImGuiCol_SliderGrab]           = Palette::AccentDim();
        colors[ImGuiCol_SliderGrabActive]     = Palette::Accent();
        colors[ImGuiCol_FrameBg]              = ImVec4(0.090f, 0.115f, 0.150f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.130f, 0.165f, 0.215f, 1.00f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.170f, 0.215f, 0.280f, 1.00f);

        colors[ImGuiCol_Text]                 = Palette::Text();
        colors[ImGuiCol_TextDisabled]         = Palette::TextFaint();

        colors[ImGuiCol_Header]               = ImVec4(0.150f, 0.185f, 0.235f, 1.00f);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.210f, 0.250f, 0.310f, 1.00f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.260f, 0.300f, 0.360f, 1.00f);
        colors[ImGuiCol_CheckMark]            = Palette::Ally();

        colors[ImGuiCol_Tab]                  = Palette::BgPanel();
        colors[ImGuiCol_TabHovered]           = Palette::BgHover();
        colors[ImGuiCol_TabActive]            = ImVec4(0.140f, 0.175f, 0.220f, 1.00f);
        colors[ImGuiCol_TabUnfocused]         = Palette::BgDeep();
        colors[ImGuiCol_TabUnfocusedActive]   = Palette::BgPanel();

        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.030f, 0.045f, 0.060f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.180f, 0.220f, 0.280f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.300f, 0.370f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = Palette::AccentDim();

        colors[ImGuiCol_Separator]            = ImVec4(0.180f, 0.220f, 0.280f, 0.80f);
        colors[ImGuiCol_SeparatorHovered]     = Palette::AccentSoft();
        colors[ImGuiCol_SeparatorActive]      = Palette::Accent();
    }

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        std::cerr << "ImGui SDL2 后端初始化失败" << std::endl;
        return false;
    }
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        std::cerr << "ImGui SDL_Renderer 后端初始化失败" << std::endl;
        return false;
    }

    // 固定字号档位（20 为默认，保持菜单现状）
    fontSizes = {16, 18, 20, 22, 24, 30, 34};
    fonts.assign(fontSizes.size(), nullptr);

    // 显式指定图集打包宽度：避免走启发式路径导致图集超单纹理上限、字形丢失变方块
    io.Fonts->TexDesiredWidth = 4096;

    // 加载中文字体（加载失败则回退 ImGui 内置字体，中文可能显示为方块，但不影响功能）
    const char* fontPaths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc"
    };
    // [global] font= 指定路径优先（若有效）
    std::string chosen;
    if (!fontPathOverride.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, fontPathOverride.c_str(), "rb") == 0 && f) {
            fclose(f);
            chosen = fontPathOverride;
        }
    }
    if (chosen.empty()) {
        for (const char* p : fontPaths) {
            FILE* f = nullptr;
            if (fopen_s(&f, p, "rb") == 0 && f) {
                fclose(f);
                chosen = p;
                break;
            }
        }
    }

    if (!chosen.empty()) {
        // 字形覆盖策略：
        //   1) ImGui 内置的「2500 最常用简体汉字」范围 —— 覆盖日常文本 99% 以上，
        //      不再需要遇到生字就往 kUiChineseChars 里补（那是不可维护的）
        //   2) 仍合并项目自定义字符表，兜住生僻字/符号
        //   3) 附加 ASCII、中英文标点、全角形式
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

        ImWchar extraRanges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
            0x2000, 0x206F, // General Punctuation
            0x2190, 0x21FF, // Arrows
            0x2460, 0x24FF, // Enclosed Alphanumerics
            0x25A0, 0x25FF, // Geometric Shapes
            0x2600, 0x26FF, // Misc Symbols
            0x3000, 0x303F, // CJK Symbols and Punctuation
            0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
            0,
        };
        builder.AddRanges(extraRanges);
        builder.AddText(kUiChineseChars);

        ImVector<ImWchar> ranges;
        builder.BuildRanges(&ranges);

        // 拷贝到成员，保证 GlyphRanges 指针在 ImGui 延迟构建字体时仍有效（否则悬空指针断言崩溃）。
        glyphRanges.clear();
        glyphRanges.assign(ranges.begin(), ranges.end());
        const ImWchar* rangeData = glyphRanges.empty()
            ? io.Fonts->GetGlyphRangesDefault()
            : reinterpret_cast<const ImWchar*>(glyphRanges.data());

        for (size_t i = 0; i < fontSizes.size(); ++i) {
            fonts[i] = io.Fonts->AddFontFromFileTTF(chosen.c_str(), static_cast<float>(fontSizes[i]),
                                                    nullptr, rangeData);
        }
    } else {
        io.Fonts->AddFontDefault();
    }

    // 记录基准样式，并按「启动时算好的 UI 缩放」应用一次
    // （这段必须在 return 之前，否则永远不会执行 → 字号/内边距不缩放）
    baseStyle_ = ImGui::GetStyle();
    styleReady_ = true;
    uiScaleApplied_ = 0.0f;
    const float pendingScale = g_uiScale;
    g_uiScale = 1.0f;
    if (pendingScale > 0.0f) SetUiScale(pendingScale);


    initialized = true;
    return true;
}

void ImGuiUI::Shutdown() {
    if (!initialized) return;
    initialized = false;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    if (context) {
        ImGui::DestroyContext(context);
        context = nullptr;
    }
}

ImFont* ImGuiUI::GetFont(int size) const {
    if (fonts.empty()) return nullptr;
    // 就近取档
    int bestIndex = 0;
    int bestDiff = std::abs(fontSizes[0] - size);
    for (size_t i = 1; i < fontSizes.size(); ++i) {
        const int diff = std::abs(fontSizes[i] - size);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = static_cast<int>(i);
        }
    }
    return fonts[bestIndex];
}

void ImGuiUI::BeginFrame() {
    if (!initialized) return;
    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplSDLRenderer2_NewFrame();

    // 引擎以固定逻辑分辨率（800x600）渲染，全屏时 SDL 会把逻辑画布等比放大到窗口；
    // 但 imgui_impl_sdl2 默认把 DisplaySize 设为窗口物理尺寸、鼠标坐标使用窗口像素，
    // 与 SDL_Renderer 的逻辑坐标系不一致，导致全屏后按钮的鼠标命中区停留在左上角的
    // 800x600 区域。这里把 ImGui 统一改回渲染逻辑坐标系：
    //   1) DisplaySize 使用渲染器的逻辑尺寸；
    //   2) 每帧用 SDL_RenderWindowToLogical 把窗口鼠标坐标换算成逻辑坐标。
    int logicalW = 0;
    int logicalH = 0;
    SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
    if (logicalW > 0 && logicalH > 0) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(logicalW), static_cast<float>(logicalH));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        // 后端每帧会用"全局鼠标位置 - 窗口位置"回填窗口像素坐标，这里在 NewFrame 后
        // 覆盖成逻辑坐标（最后写入者生效，ImGui::NewFrame 读取的就是换算后的值）。
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS) {
            int mouseX = 0;
            int mouseY = 0;
            SDL_GetMouseState(&mouseX, &mouseY);
            float logicalX = 0.0f;
            float logicalY = 0.0f;
            SDL_RenderWindowToLogical(renderer, mouseX, mouseY, &logicalX, &logicalY);
            io.AddMousePosEvent(logicalX, logicalY);
        }
    }

    ImGui::NewFrame();
}

void ImGuiUI::EndFrame() {
    if (!initialized) return;
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}


// ---------------------------------------------------------------------------
// 全局 UI 缩放：字体与控件尺寸一起缩放
// ---------------------------------------------------------------------------
void ImGuiUI::SetUiScale(float scale) {
    if (scale < 0.2f) scale = 0.2f;
    if (scale > 4.0f) scale = 4.0f;

    // ImGui 上下文还没创建时只记下缩放值，等 Initialize 之后由这里补上
    if (!styleReady_) { g_uiScale = scale; uiScaleApplied_ = 0.0f; return; }
    // 注意：调用方会先更新 g_uiScale，所以这里必须跟「已应用的缩放」比较，
    // 否则真实的窗口尺寸第三次调用会被误判为「没变化」而跳过。
    if (std::abs(scale - uiScaleApplied_) < 0.001f) return;

    g_uiScale = scale;
    uiScaleApplied_ = scale;

    // 每次从基准样式重新缩放，避免反复乘算累积
    ImGui::GetStyle() = baseStyle_;
    ImGui::GetStyle().ScaleAllSizes(scale);

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = scale;

    std::cout << "[UI] 缩放=" << scale
              << " 内边距=" << ImGui::GetStyle().FramePadding.y
              << " 行距=" << ImGui::GetStyle().ItemSpacing.y << std::endl;
}
