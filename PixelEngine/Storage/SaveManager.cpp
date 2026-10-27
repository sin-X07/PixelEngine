#include "SaveManager.hpp"
#include "../Core/IniFile.hpp"
#include "../Core/PathUtils.hpp"
#include <iostream>

namespace {
constexpr const char* kSaveFile = "cs2d_save.ini";
}

void SaveManager::Load() {
    const std::string path = PathUtils::ResolveWritablePath(kSaveFile);
    IniFile ini = IniFile::Load(path);
    if (!ini.IsLoaded()) {
        std::cout << "[Save] 未找到存档，使用默认设置" << std::endl;
        loaded_ = true;
        return;
    }

    // ---- 通用设置 ----
    settings_.mouseSensitivity = ini.GetFloat("settings", "sensitivity", 1.0f);
    settings_.resIdx           = ini.GetInt  ("settings", "res_idx", 0);
    settings_.fullscreen       = ini.GetBool ("settings", "fullscreen", false);
    settings_.targetFPS        = ini.GetInt  ("settings", "target_fps", 60);
    settings_.renderScalePct   = ini.GetInt  ("settings", "render_scale", 100);

    // ---- 准星 ----
    settings_.crossR         = ini.GetInt  ("crosshair", "r", 255);
    settings_.crossG         = ini.GetInt  ("crosshair", "g", 255);
    settings_.crossB         = ini.GetInt  ("crosshair", "b", 255);
    settings_.crossA         = ini.GetInt  ("crosshair", "a", 200);
    settings_.crossThickness = ini.GetFloat("crosshair", "thickness", 1.5f);
    settings_.crossLength    = ini.GetFloat("crosshair", "length", 12.0f);
    settings_.crossGap       = ini.GetFloat("crosshair", "gap", 3.0f);
    settings_.crossCenterDot = ini.GetBool ("crosshair", "center_dot", true);
    settings_.crossDynamic   = ini.GetBool ("crosshair", "dynamic", true);

    // ---- 雷达 ----
    settings_.radarPlayerCentered   = ini.GetBool ("radar", "player_centered", true);
    settings_.radarRotateWithPlayer = ini.GetBool ("radar", "rotate_with_player", false);
    settings_.radarBlendBg          = ini.GetBool ("radar", "blend_bg", false);
    settings_.radarBgAlpha          = ini.GetInt  ("radar", "bg_alpha", 160);
    settings_.radarHudSize          = ini.GetInt  ("radar", "hud_size", 120);
    settings_.radarMapZoom          = ini.GetFloat("radar", "map_zoom", 0.005f);

    loaded_ = true;
    std::cout << "[Save] 已读取存档" << std::endl;
}

void SaveManager::Save() {
    IniFile ini;

    ini.SetFloat("settings", "sensitivity", settings_.mouseSensitivity);
    ini.SetInt  ("settings", "res_idx",     settings_.resIdx);
    ini.SetBool ("settings", "fullscreen",  settings_.fullscreen);
    ini.SetInt  ("settings", "target_fps",  settings_.targetFPS);
    ini.SetInt  ("settings", "render_scale", settings_.renderScalePct);

    ini.SetInt  ("crosshair", "r",          settings_.crossR);
    ini.SetInt  ("crosshair", "g",          settings_.crossG);
    ini.SetInt  ("crosshair", "b",          settings_.crossB);
    ini.SetInt  ("crosshair", "a",          settings_.crossA);
    ini.SetFloat("crosshair", "thickness",  settings_.crossThickness);
    ini.SetFloat("crosshair", "length",     settings_.crossLength);
    ini.SetFloat("crosshair", "gap",        settings_.crossGap);
    ini.SetBool ("crosshair", "center_dot", settings_.crossCenterDot);
    ini.SetBool ("crosshair", "dynamic",    settings_.crossDynamic);

    ini.SetBool ("radar", "player_centered",    settings_.radarPlayerCentered);
    ini.SetBool ("radar", "rotate_with_player", settings_.radarRotateWithPlayer);
    ini.SetBool ("radar", "blend_bg",           settings_.radarBlendBg);
    ini.SetInt  ("radar", "bg_alpha",           settings_.radarBgAlpha);
    ini.SetInt  ("radar", "hud_size",           settings_.radarHudSize);
    ini.SetFloat("radar", "map_zoom",           settings_.radarMapZoom);

    const std::string path = PathUtils::ResolveWritablePath(kSaveFile);
    if (!ini.Save(path)) {
        std::cerr << "[Save] 写入失败: " << path << std::endl;
    }
}
