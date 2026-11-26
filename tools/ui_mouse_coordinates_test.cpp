#undef NDEBUG
#include <SDL.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
int main(){
    SDL_SetHint(SDL_HINT_VIDEODRIVER,"dummy");
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    for(auto size: {1280,1600}) {
        auto*w=SDL_CreateWindow("test",0,0,size,720,SDL_WINDOW_HIDDEN);assert(w);
        auto*r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(r);
        assert(SDL_RenderSetLogicalSize(r,640,360)==0);
        SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(w);e.motion.x=size/2;e.motion.y=360;SDL_PushEvent(&e);
        bool seen=false;
        while(SDL_PollEvent(&e)){if(e.type==SDL_MOUSEMOTION){assert(e.motion.x==320&&e.motion.y==180);seen=true;}}
        assert(seen);
        float x,y;SDL_RenderWindowToLogical(r,size/2,360,&x,&y);assert(std::abs(x-320)<1&&std::abs(y-180)<1);
        float twiceX,twiceY;SDL_RenderWindowToLogical(r,320,180,&twiceX,&twiceY);assert(std::abs(twiceX-320)>1);
        printf("%dx720: event and poll agree at (320,180); double conversion diverges\n",size);
        auto* target=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,640,360);assert(target);
        assert(SDL_SetRenderTarget(r,target)==0);
        float badX,badY;SDL_RenderWindowToLogical(r,size/2,360,&badX,&badY);assert(std::abs(badX-320)>1);
        auto* saved=SDL_GetRenderTarget(r);SDL_SetRenderTarget(r,nullptr);SDL_RenderWindowToLogical(r,size/2,360,&x,&y);SDL_SetRenderTarget(r,saved);
        assert(std::abs(x-320)<1&&std::abs(y-180)<1);assert(SDL_GetRenderTarget(r)==target);
        printf("offscreen target: old poll=(%.0f,%.0f), fixed=(%.0f,%.0f), target restored\n",badX,badY,x,y);
        SDL_SetRenderTarget(r,nullptr);SDL_DestroyTexture(target);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);
    }

    // 验证多分辨率与渲染缩放下：开盘逻辑圆心 -> 窗口坐标居中 + 相对鼠标模式切换
    struct ModeCase { int winW, winH, logW, logH; };
    const ModeCase cases[] = {
        { 1280, 720, 1920, 1080 },
        { 1920, 1080, 1920, 1080 },
        { 1920, 1200, 1728, 1080 },
        { 1280, 720, 960, 540 } // 50% 渲染缩放
    };
    for (const auto& c : cases) {
        auto* w = SDL_CreateWindow("wheel_center_test", 0, 0, c.winW, c.winH, SDL_WINDOW_HIDDEN);
        assert(w);
        auto* r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
        assert(r);
        assert(SDL_RenderSetLogicalSize(r, c.logW, c.logH) == 0);

        const float centerLogX = c.logW * 0.5f;
        const float centerLogY = c.logH * 0.5f;
        int winX = 0, winY = 0;
        SDL_RenderLogicalToWindow(r, centerLogX, centerLogY, &winX, &winY);
        assert(std::abs(winX - c.winW / 2) <= 1 && std::abs(winY - c.winH / 2) <= 1);

        // 切至绝对模式并居中到盘心
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_WarpMouseInWindow(w, winX, winY);
        float backLogX = 0.0f, backLogY = 0.0f;
        SDL_RenderWindowToLogical(r, winX, winY, &backLogX, &backLogY);
        assert(std::abs(backLogX - centerLogX) < 2.0f && std::abs(backLogY - centerLogY) < 2.0f);

        // 关盘切回相对模式后，丢弃第一帧残留位移
        SDL_SetRelativeMouseMode(SDL_TRUE);
        int rx = 0, ry = 0;
        SDL_GetRelativeMouseState(&rx, &ry);
        SDL_GetRelativeMouseState(&rx, &ry);
        assert(rx == 0 && ry == 0);

        printf("wheel center %dx%d (logical %dx%d): win=(%d,%d) -> logical=(%.1f,%.1f)\n",
               c.winW, c.winH, c.logW, c.logH, winX, winY, backLogX, backLogY);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(w);
    }
    SDL_Quit();
}
