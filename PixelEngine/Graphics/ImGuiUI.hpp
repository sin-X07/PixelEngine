#pragma once
#include <SDL2/SDL.h>

struct ImGuiContext;

// Dear ImGui 封装：负责 context 生命周期、SDL2 + SDL_Renderer 后端接入、中文字体与每帧边界。
// 菜单窗口的实际绘制由 Engine 在 BeginFrame()/EndFrame() 之间调用 ImGui API 完成。
class ImGuiUI {
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    ImGuiContext* context = nullptr;
    bool initialized = false;

public:
    ImGuiUI() = default;
    ~ImGuiUI() { Shutdown(); }

    bool Initialize(SDL_Window* win, SDL_Renderer* ren);
    void Shutdown();

    void BeginFrame();   // ImGui_ImplSDL2_NewFrame + ImGui_ImplSDLRenderer2_NewFrame + ImGui::NewFrame
    void EndFrame();     // ImGui::Render + ImGui_ImplSDLRenderer2_RenderDrawData

    bool IsInitialized() const { return initialized; }
};
