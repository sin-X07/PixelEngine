#undef NDEBUG // Keep regression assertions active in Release builds.
#include "imgui.h"
#include <cassert>
#include <cstdio>
bool frame(bool down, int* address, bool stable) {
 auto& io=ImGui::GetIO();io.MousePos=ImVec2(40,40);io.MouseDown[0]=down;
 ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(400,300));
 ImGui::Begin("test",nullptr,ImGuiWindowFlags_NoTitleBar);ImGui::SetCursorScreenPos(ImVec2(20,20));
 if(stable){ImGui::PushID(0);ImGui::PushID(7);}else ImGui::PushID(address);
 bool click=ImGui::InvisibleButton("##card",ImVec2(100,70));ImGui::PopID();if(stable)ImGui::PopID();
 ImGui::End();ImGui::Render();return click;
}
bool run(bool stable){ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(640,480);io.DeltaTime=1.f/60;unsigned char* p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);int a=1,b=2;frame(false,&a,stable);frame(false,&a,stable);frame(true,&a,stable);bool clicked=frame(false,&b,stable);ImGui::DestroyContext();return clicked;}
int main(){bool old=run(false),fixed=run(true);printf("Changed allocation: old click=%d, stable ID click=%d\n",old,fixed);assert(!old&&fixed);}
