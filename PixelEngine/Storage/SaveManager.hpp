#pragma once
#include <string>

// ============================================================================
// SaveManager — CS2D 单机版存档
//
// 持久化内容:
//   [stats]    玩家累计击杀 / 阵亡 / 回合胜负
//   [settings] 灵敏度、分辨率、帧率、全屏
//   [crosshair] 准星颜色/粗细/长度/间隔/中心点
//   [radar]    雷达设置
//
// 落盘位置: exe 同级 cs2d_save.ini（ResolveWritablePath）
// ============================================================================

struct PlayerStats {
    int kills     = 0;
    int deaths    = 0;
    int roundsWon = 0;
    int roundsLost = 0;

    void AddKill()  { ++kills; }
    void AddDeath() { ++deaths; }
    void AddRound(bool won) { if (won) ++roundsWon; else ++roundsLost; }
    void Reset() { kills = deaths = roundsWon = roundsLost = 0; }
};

struct GameSettings {
    float mouseSensitivity = 1.0f;
    int   resIdx           = 0;
    bool  fullscreen       = false;
    int   targetFPS        = 60;

    // 准星
    int   crossR = 255, crossG = 255, crossB = 255, crossA = 200;
    float crossThickness = 1.5f;
    float crossLength    = 12.0f;
    float crossGap       = 3.0f;
    bool  crossCenterDot = true;

    // 雷达
    bool  radarPlayerCentered   = true;
    bool  radarRotateWithPlayer = false;
    bool  radarBlendBg          = false;
    int   radarBgAlpha          = 160;
    int   radarHudSize          = 120;
    float radarMapZoom          = 0.005f;
};

class SaveManager {
public:
    SaveManager() = default;

    // 读取 / 写入 cs2d_save.ini
    void Load();
    void Save();

    const PlayerStats& GetStats() const { return stats_; }
    void AddKill()  { stats_.AddKill(); }
    void AddDeath() { stats_.AddDeath(); }
    void AddRoundResult(bool won) { stats_.AddRound(won); }
    void ResetStats() { stats_.Reset(); }

    GameSettings&       MutableSettings()       { return settings_; }
    const GameSettings& GetSettings() const     { return settings_; }

private:
    PlayerStats  stats_;
    GameSettings settings_;
    bool loaded_ = false;
};
