#include "SaveManager.hpp"
#include "../Core/IniFile.hpp"
#include "../Core/PathUtils.hpp"
#include "../Core/BuildInfo.hpp"
#include <iostream>
#include <algorithm>

namespace {
// 存档按渠道隔离：dev/beta 用 cs2d_save_dev.ini（beta 为 _beta），避免开发档覆盖玩家档
const std::string kSaveFile =
    std::string("cs2d_save") + std::string(BuildInfo::SaveSuffix()) + ".ini";
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
    settings_.fogOpacityPct    = ini.GetInt  ("settings", "fog_opacity", 100);
    settings_.sniperDelayedScopeClose =
        ini.GetBool("settings", "sniper_delayed_scope_close", false);
    settings_.scopeSensitivity    = ini.GetFloat("settings", "scope_sensitivity", 1.0f);
    settings_.scopeCrossThickness = ini.GetFloat("settings", "scope_cross_thickness", 2.0f);
    settings_.scopeCrossDynamic   = ini.GetBool ("settings", "scope_cross_dynamic", false);

    // ---- 准星 ----
    settings_.crossR         = ini.GetInt  ("crosshair", "r", 255);
    settings_.crossG         = ini.GetInt  ("crosshair", "g", 255);
    settings_.crossB         = ini.GetInt  ("crosshair", "b", 255);
    settings_.crossA         = ini.GetInt  ("crosshair", "a", 200);
    settings_.crossThickness = ini.GetFloat("crosshair", "thickness", 1.5f);
    settings_.crossLength    = ini.GetFloat("crosshair", "length", 12.0f);
    settings_.crossGap       = ini.GetFloat("crosshair", "gap", 3.0f);
    settings_.crossCenterDot = ini.GetBool ("crosshair", "center_dot", true);
    settings_.aimLine = ini.GetBool("crosshair", "aim_line", true);
    settings_.crossDynamic   = ini.GetBool ("crosshair", "dynamic", true);

    auto loadColor = [&](const std::string& prefix, SDL_Color fallback) {
        return SDL_Color{
            static_cast<Uint8>(std::clamp(ini.GetInt("aim_line", prefix + "_r", fallback.r), 0, 255)),
            static_cast<Uint8>(std::clamp(ini.GetInt("aim_line", prefix + "_g", fallback.g), 0, 255)),
            static_cast<Uint8>(std::clamp(ini.GetInt("aim_line", prefix + "_b", fallback.b), 0, 255)),
            static_cast<Uint8>(std::clamp(ini.GetInt("aim_line", prefix + "_a", fallback.a), 0, 255))};
    };
    settings_.aimLineStyle = AimLineStyle{};
    auto& line = settings_.aimLineStyle;
    line.solid = loadColor("solid", line.solid);
    line.hidden = loadColor("hidden", line.hidden);
    line.solidThickness = ini.GetFloat("aim_line", "solid_thickness", line.solidThickness);
    line.hiddenThickness = ini.GetFloat("aim_line", "hidden_thickness", line.hiddenThickness);
    line.dashLength = ini.GetFloat("aim_line", "dash_length", line.dashLength);
    line.gapLength = ini.GetFloat("aim_line", "gap_length", line.gapLength);
    line.Normalize();

    // ---- 雷达 ----
    settings_.radarPlayerCentered   = ini.GetBool ("radar", "player_centered", true);
    settings_.radarRotateWithPlayer = ini.GetBool ("radar", "rotate_with_player", false);
    settings_.radarBlendBg          = ini.GetBool ("radar", "blend_bg", false);
    settings_.radarBgAlpha          = ini.GetInt  ("radar", "bg_alpha", 160);
    settings_.radarHudSize          = ini.GetInt  ("radar", "hud_size", 120);
    settings_.radarMapZoom          = ini.GetFloat("radar", "map_zoom", 0.005f);

    // ---- 主菜单 3D 自定义 ----
    const int menuPoseVersion = ini.GetInt("menu", "pose_version", 0);
    for (int i = 0; i < 5; ++i) {
        const std::string ck = "color" + std::to_string(i);
        const std::string pk = "pose" + std::to_string(i);
        const std::string ak = "accessory" + std::to_string(i);
        settings_.menuColor[i]     = ini.GetInt("menu", ck.c_str(), 0);
        settings_.menuPose[i]      = ini.GetInt("menu", pk.c_str(), 0);
        settings_.menuAccessory[i] = ini.GetInt("menu", ak.c_str(), 0);
        if (menuPoseVersion < 1) {
            const int oldPose = settings_.menuPose[i];
            settings_.menuPose[i] = i < 2 ? (oldPose == 3 ? 1 : 0)
                                         : (oldPose == 0 || oldPose == 2 ? 0 : 1);
        }
        settings_.menuPose[i] = std::clamp(settings_.menuPose[i], 0, 3);
    }
    settings_.menuScene = std::clamp(ini.GetInt("menu", "scene", 1), 0, 2);
    settings_.menuWeapon[0] = ini.GetInt("menu", "weapon0", 0);
    settings_.menuWeapon[1] = ini.GetInt("menu", "weapon1", 0);

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
    ini.SetInt  ("settings", "fog_opacity", settings_.fogOpacityPct);
    ini.SetBool ("settings", "sniper_delayed_scope_close", settings_.sniperDelayedScopeClose);
    ini.SetFloat("settings", "scope_sensitivity", settings_.scopeSensitivity);
    ini.SetFloat("settings", "scope_cross_thickness", settings_.scopeCrossThickness);
    ini.SetBool ("settings", "scope_cross_dynamic", settings_.scopeCrossDynamic);

    ini.SetInt  ("crosshair", "r",          settings_.crossR);
    ini.SetInt  ("crosshair", "g",          settings_.crossG);
    ini.SetInt  ("crosshair", "b",          settings_.crossB);
    ini.SetInt  ("crosshair", "a",          settings_.crossA);
    ini.SetFloat("crosshair", "thickness",  settings_.crossThickness);
    ini.SetFloat("crosshair", "length",     settings_.crossLength);
    ini.SetFloat("crosshair", "gap",        settings_.crossGap);
    ini.SetBool ("crosshair", "center_dot", settings_.crossCenterDot);
    ini.SetBool ("crosshair", "aim_line", settings_.aimLine);
    ini.SetBool ("crosshair", "dynamic",    settings_.crossDynamic);

    auto saveColor = [&](const std::string& prefix, SDL_Color color) {
        ini.SetInt("aim_line", prefix + "_r", color.r);
        ini.SetInt("aim_line", prefix + "_g", color.g);
        ini.SetInt("aim_line", prefix + "_b", color.b);
        ini.SetInt("aim_line", prefix + "_a", color.a);
    };
    auto& line = settings_.aimLineStyle;
    line.Normalize();
    saveColor("solid", line.solid);
    saveColor("hidden", line.hidden);
    ini.SetFloat("aim_line", "solid_thickness", line.solidThickness);
    ini.SetFloat("aim_line", "hidden_thickness", line.hiddenThickness);
    ini.SetFloat("aim_line", "dash_length", line.dashLength);
    ini.SetFloat("aim_line", "gap_length", line.gapLength);

    ini.SetBool ("radar", "player_centered",    settings_.radarPlayerCentered);
    ini.SetBool ("radar", "rotate_with_player", settings_.radarRotateWithPlayer);
    ini.SetBool ("radar", "blend_bg",           settings_.radarBlendBg);
    ini.SetInt  ("radar", "bg_alpha",           settings_.radarBgAlpha);
    ini.SetInt  ("radar", "hud_size",           settings_.radarHudSize);
    ini.SetFloat("radar", "map_zoom",           settings_.radarMapZoom);

    // ---- 主菜单 3D 自定义 ----
    ini.SetInt("menu", "pose_version", 1);
    for (int i = 0; i < 5; ++i) {
        const std::string ck = "color" + std::to_string(i);
        const std::string pk = "pose" + std::to_string(i);
        const std::string ak = "accessory" + std::to_string(i);
        ini.SetInt("menu", ck.c_str(), settings_.menuColor[i]);
        ini.SetInt("menu", pk.c_str(), settings_.menuPose[i]);
        ini.SetInt("menu", ak.c_str(), settings_.menuAccessory[i]);
    }
    ini.SetInt("menu", "scene", std::clamp(settings_.menuScene, 0, 2));
    ini.SetInt("menu", "weapon0", settings_.menuWeapon[0]);
    ini.SetInt("menu", "weapon1", settings_.menuWeapon[1]);

    const std::string path = PathUtils::ResolveWritablePath(kSaveFile);
    if (!ini.Save(path)) {
        std::cerr << "[Save] 写入失败: " << path << std::endl;
    }
}
