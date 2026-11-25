#include "ImGuiUI.hpp"
#include "UiScale.hpp"
#include "Palette.hpp"
#include <cmath>
#include <algorithm>
#include "../Core/UiTextChars.hpp"
#include "../Core/PathUtils.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"
#include <iostream>
#include <cstdlib>
#include <string>

namespace {
// A seamless brushed tile. Four-by-four copies keep every sampled UV inside [0,1]
// on SDL renderers that clamp at the texture edge.
SDL_Texture* CreateControlGrain(SDL_Renderer* renderer) {
    constexpr int tile = 128, size = tile * 4;
    std::vector<Uint8> pixels(size * size * 4);
    auto noise = [](unsigned x, unsigned y) {
        unsigned n = x * 0x45d9f3bu + y * 0x27d4eb2du + 11u;
        n ^= n >> 16;
        n *= 0x85ebca6bu;
        return static_cast<int>((n ^ (n >> 13)) & 255);
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int u = x % tile, v = y % tile;
            const int segment = u / 16, fraction = u % 16;
            const int stroke = (noise(segment,v)*(16-fraction) + noise((segment+1)%8,v)*fraction)/16;
            const int value = (3*stroke + noise(u,v))/4 - 128;
            Uint8* pixel = &pixels[(y*size+x)*4];
            pixel[0] = pixel[1] = pixel[2] = value > 0 ? 255 : 0;
            pixel[3] = static_cast<Uint8>(std::abs(value)/3);
        }
    }
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, size, size);
    if (texture && SDL_UpdateTexture(texture, nullptr, pixels.data(), size*4) != 0) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    }
    return texture;
}
}

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
#ifdef _WIN32
    const char* fontPaths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc"
    };
#else
    // macOS 系统自带中文字体（PingFang / 冬青黑 / 华文黑体）
    const char* fontPaths[] = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/Library/Fonts/Arial Unicode.ttf"
    };
#endif
    // [global] font= 指定路径优先（若有效）
    std::string chosen;
    if (!fontPathOverride.empty() && PathUtils::FileExists(fontPathOverride)) {
        chosen = fontPathOverride;
    }
    if (chosen.empty()) {
        for (const char* p : fontPaths) {
            if (PathUtils::FileExists(p)) {
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


    // Build and upload the font atlas during loading, before the first gameplay frame.
    if (!ImGui_ImplSDLRenderer2_CreateFontsTexture()) return false;
    controlGrain_ = CreateControlGrain(renderer);
    initialized = true;
    return true;
}

void ImGuiUI::Shutdown() {
    if (!initialized) return;
    initialized = false;
    SDL_DestroyTexture(controlGrain_);
    controlGrain_ = nullptr;
    if (frostCapture_) SDL_DestroyTexture(frostCapture_);
    if (frostTexture_) SDL_DestroyTexture(frostTexture_);
    frostCapture_ = frostTexture_ = nullptr;
    Palette::frostTexture = 0;

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
    frostPrepared_ = false;
    Palette::frostTexture = 0;
    // Input is in window coordinates even while the engine renders a scene
    // texture. SDL_RenderWindowToLogical reads the CURRENT render target's
    // viewport/scale, so poll the platform backend with the window selected.
    SDL_Texture* sceneTarget = SDL_GetRenderTarget(renderer);
    if (sceneTarget) SDL_SetRenderTarget(renderer, nullptr);
    ImGui_ImplSDL2_NewFrame();
    if (sceneTarget) SDL_SetRenderTarget(renderer, sceneTarget);
    ImGui_ImplSDLRenderer2_NewFrame();

    // Match the SDL renderer's logical canvas. Mouse events and the backend's
    // fallback poll both use this same coordinate space (no second override).
    int logicalW = 0;
    int logicalH = 0;
    SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
    if (logicalW > 0 && logicalH > 0) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(logicalW), static_cast<float>(logicalH));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

    ImGui::NewFrame();
}

void ImGuiUI::PrepareFrostedGlass() {
    if (!initialized || frostPrepared_) return;
    frostPrepared_ = true;
    SDL_Texture* target = SDL_GetRenderTarget(renderer);
    int width = 0, height = 0;
    if (target) SDL_QueryTexture(target, nullptr, nullptr, &width, &height);
    else SDL_GetRendererOutputSize(renderer, &width, &height);
    if (width <= 0 || height <= 0) return;
    const int smallW = std::max(1, width / 8), smallH = std::max(1, height / 8);
    if (!frostTexture_ || frostWidth_ != smallW || frostHeight_ != smallH) {
        if (frostCapture_) SDL_DestroyTexture(frostCapture_);
        if (frostTexture_) SDL_DestroyTexture(frostTexture_);
        frostWidth_ = smallW;
        frostHeight_ = smallH;
        frostCapture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, smallW, smallH);
        frostTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, smallW, smallH);
        frostPixels_.resize(smallW * smallH * 4);
        frostScratch_.resize(frostPixels_.size());
        if (frostTexture_) {
            SDL_SetTextureScaleMode(frostTexture_, SDL_ScaleModeLinear);
            SDL_SetTextureBlendMode(frostTexture_, SDL_BLENDMODE_BLEND);
        }
    }
    if (!frostTexture_) return;
    SDL_BlendMode savedBlend;
    SDL_GetRenderDrawBlendMode(renderer, &savedBlend);
    Uint8 savedR, savedG, savedB, savedA;
    SDL_GetRenderDrawColor(renderer, &savedR, &savedG, &savedB, &savedA);
    int result = -1;
    if (target && frostCapture_) {
        SDL_Rect viewport, clip;
        SDL_RenderGetViewport(renderer, &viewport);
        SDL_RenderGetClipRect(renderer, &clip);
        const bool clipped = SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
        float scaleX, scaleY;
        SDL_RenderGetScale(renderer, &scaleX, &scaleY);
        SDL_SetRenderTarget(renderer, frostCapture_);
        SDL_RenderSetViewport(renderer, nullptr);
        SDL_RenderSetClipRect(renderer, nullptr);
        SDL_RenderSetScale(renderer, 1, 1);
        const SDL_Rect destination{0,0,smallW,smallH};
        result = SDL_RenderCopy(renderer, target, nullptr, &destination);
        SDL_SetRenderTarget(renderer, frostTexture_);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetTextureBlendMode(frostCapture_, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(frostCapture_, 10, 10, 10);
        for (int y=-2; y<=2; ++y) {
            for (int x=-2; x<=2; ++x) {
                const SDL_Rect shifted{x,y,smallW,smallH};
                if (SDL_RenderCopy(renderer, frostCapture_, nullptr, &shifted) != 0) result = -1;
            }
        }
        SDL_SetTextureColorMod(frostCapture_,255,255,255);
        SDL_SetTextureBlendMode(frostCapture_,SDL_BLENDMODE_NONE);
        SDL_SetRenderTarget(renderer, target);
        SDL_RenderSetViewport(renderer, &viewport);
        SDL_RenderSetScale(renderer, scaleX, scaleY);
        SDL_RenderSetClipRect(renderer, clipped ? &clip : nullptr);
    } else {
        frostReadback_.resize(width * height * 4);
        result = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, frostReadback_.data(), width * 4);
        if (result == 0) {
            for (int y = 0; y < smallH; ++y)
                for (int x = 0; x < smallW; ++x)
                    std::copy_n(&frostReadback_[((y * height / smallH) * width + x * width / smallW) * 4],
                                4, &frostPixels_[(y * smallW + x) * 4]);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, savedBlend);
    SDL_SetRenderDrawColor(renderer, savedR, savedG, savedB, savedA);
    if (result != 0) return;
    if (target && frostCapture_) {
        Palette::frostTexture = reinterpret_cast<ImTextureID>(frostTexture_);
        Palette::frostCanvas = ImGui::GetIO().DisplaySize;
        return;
    }
    // Two separable box passes blur only the sampled backdrop, never UI text or icons.
    for (int axis = 0; axis < 2; ++axis) {
        for (int y = 0; y < smallH; ++y) {
            for (int x = 0; x < smallW; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    int sum = 0;
                    for (int offset = -2; offset <= 2; ++offset) {
                        const int sx = std::clamp(x + (axis == 0 ? offset : 0), 0, smallW - 1);
                        const int sy = std::clamp(y + (axis == 1 ? offset : 0), 0, smallH - 1);
                        sum += frostPixels_[(sy * smallW + sx) * 4 + channel];
                    }
                    frostScratch_[(y * smallW + x) * 4 + channel] = static_cast<Uint8>(sum / 5);
                }
                frostScratch_[(y * smallW + x) * 4 + 3] = 255;
            }
        }
        frostPixels_.swap(frostScratch_);
    }
    if (SDL_UpdateTexture(frostTexture_, nullptr, frostPixels_.data(), smallW * 4) != 0) return;
    Palette::frostTexture = reinterpret_cast<ImTextureID>(frostTexture_);
    Palette::frostCanvas = ImGui::GetIO().DisplaySize;
}

void ImGuiUI::EndFrame() {
    if (!initialized) return;
    // 所有可交互控件统一显示手型；控件自己设置过光标（输入框、禁用项）时不覆盖
    if (ImGui::IsAnyItemHovered() && ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
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
