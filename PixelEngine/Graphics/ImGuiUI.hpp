#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>

#include "../ThirdParty/imgui/imgui.h"

struct ImGuiContext;
struct ImFont;

// Dear ImGui 封装：负责 context 生命周期、SDL2 + SDL_Renderer 后端接入、多字号中文字体与每帧边界。
// 菜单窗口的实际绘制由 Engine/GameUI 在 BeginFrame()/EndFrame() 之间调用 ImGui API 完成。
class ImGuiUI {
private:
    ImGuiStyle baseStyle_{};     // 基准样式（缩放前）
    float      uiScaleApplied_ = 0.0f;
    bool       styleReady_ = false;   // ImGui 上下文创建后才允许改样式
public:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    ImGuiContext* context = nullptr;
    bool initialized = false;

    std::vector<int> fontSizes;   // 固定档位（升序）
    std::vector<ImFont*> fonts;   // 与 fontSizes 一一对应；加载失败对应项为 nullptr
    std::string fontPathOverride; // [global] font= 指定路径（启动加载优先）
    // 字形范围数组须在整个字体构建期间持久（ImGui 延迟到首次 NewFrame 才读取 GlyphRanges），
    // 存为成员避免局部 ImVector 悬空导致断言崩溃。ImWchar == unsigned short。
    std::vector<unsigned short> glyphRanges;

public:
    ImGuiUI() = default;
    ~ImGuiUI() { Shutdown(); }

    bool Initialize(SDL_Window* win, SDL_Renderer* ren);
    void Shutdown();

    // 设置字体文件路径（须在 Initialize 之前调用；运行时修改需重启生效）
    void SetFontPath(const std::string& path) { fontPathOverride = path; }

    void BeginFrame();   // ImGui_ImplSDL2_NewFrame + ImGui_ImplSDLRenderer2_NewFrame + ImGui::NewFrame
    // 设置全局 UI 缩放（字体 + 控件尺寸）
    void SetUiScale(float scale);
    void EndFrame();     // ImGui::Render + ImGui_ImplSDLRenderer2_RenderDrawData

    // 返回最接近 size 的字号档位字体；越界/无字体返回默认字体（nullptr 表示 ImGui 默认字体）
    ImFont* GetFont(int size) const;

    bool IsInitialized() const { return initialized; }
};
