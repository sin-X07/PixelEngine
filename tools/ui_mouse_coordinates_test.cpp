#undef NDEBUG
#include <SDL.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
int main(){SDL_SetHint(SDL_HINT_VIDEODRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO)==0);
for(auto size: {1280,1600}) {auto*w=SDL_CreateWindow("test",0,0,size,720,SDL_WINDOW_HIDDEN);assert(w);auto*r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(r);assert(SDL_RenderSetLogicalSize(r,640,360)==0);
SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(w);e.motion.x=size/2;e.motion.y=360;SDL_PushEvent(&e);bool seen=false;while(SDL_PollEvent(&e)){if(e.type==SDL_MOUSEMOTION){assert(e.motion.x==320&&e.motion.y==180);seen=true;}}
assert(seen);float x,y;SDL_RenderWindowToLogical(r,size/2,360,&x,&y);assert(std::abs(x-320)<1&&std::abs(y-180)<1);float twiceX,twiceY;SDL_RenderWindowToLogical(r,320,180,&twiceX,&twiceY);assert(std::abs(twiceX-320)>1);printf("%dx720: event and poll agree at (320,180); double conversion diverges\n",size);auto* target=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,640,360);assert(target);assert(SDL_SetRenderTarget(r,target)==0);
float badX,badY;SDL_RenderWindowToLogical(r,size/2,360,&badX,&badY);assert(std::abs(badX-320)>1);
auto* saved=SDL_GetRenderTarget(r);SDL_SetRenderTarget(r,nullptr);SDL_RenderWindowToLogical(r,size/2,360,&x,&y);SDL_SetRenderTarget(r,saved);assert(std::abs(x-320)<1&&std::abs(y-180)<1);assert(SDL_GetRenderTarget(r)==target);printf("offscreen target: old poll=(%.0f,%.0f), fixed=(%.0f,%.0f), target restored\n",badX,badY,x,y);SDL_SetRenderTarget(r,nullptr);SDL_DestroyTexture(target);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);}SDL_Quit();}
