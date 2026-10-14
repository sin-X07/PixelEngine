#include "ImGuiUI.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"
#include <iostream>

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

    // 加载中文字体（加载失败则回退 ImGui 内置字体，中文可能显示为方块，但不影响功能）
    const char* fontPaths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc"
    };
    bool fontLoaded = false;
    for (const char* p : fontPaths) {
        FILE* f = nullptr;
        if (fopen_s(&f, p, "rb") == 0 && f) {
            fclose(f);
            io.Fonts->AddFontFromFileTTF(p, 20.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
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

void ImGuiUI::BeginFrame() {
    if (!initialized) return;
    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();
}

void ImGuiUI::EndFrame() {
    if (!initialized) return;
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}
