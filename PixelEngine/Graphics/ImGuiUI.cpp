#include "ImGuiUI.hpp"
#include "../Core/UiTextChars.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"
#include <iostream>
#include <cstdlib>

bool ImGuiUI::Initialize(SDL_Window* win, SDL_Renderer* ren) {
    window = win;
    renderer = ren;

    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

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
        // 用 GlyphRangesBuilder 精确构建字形表：基础 ASCII + 全角标点 + 项目全部汉字。
        // 比 GetGlyphRangesChineseSimplifiedCommon(2500 常用字) 更稳：不含生僻字缺字，也不浪费图集。
        ImFontGlyphRangesBuilder builder;
        ImWchar baseRanges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
            0x2000, 0x206F, // General Punctuation
            0x3000, 0x303F, // CJK Symbols and Punctuation
            0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
            0,
        };
        builder.AddRanges(baseRanges);
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
