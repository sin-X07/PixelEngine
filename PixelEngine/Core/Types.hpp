#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>

// 游戏状态枚举
enum GameState {
    MAIN_MENU,
    MODE_SELECT,
    HISTORY,
    CONTROLS,
    SETTINGS,
    PLAYING,
    PAUSED,
    EXIT_CONFIRM,
    GAMEOVER
};

// 游戏模式枚举
enum GameMode {
    TIME_ATTACK, // 模式一：限时挑战
    MAZE_MODE    // 模式二：迷宫探索
};

// 角色朝向枚举
enum Direction {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
};

// 粒子属性
struct Particle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float lifetime = 0.0f;
    float maxLifetime = 0.0f;
    bool active = false;
};

// 障碍物碎片粒子
struct DebrisParticle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float size = 0.0f;
    float lifetime = 0.0f;
    float maxLifetime = 0.0f;
    SDL_Color color = {200, 150, 100, 255};
    bool active = false;
};

// 陷阱类型与属性
enum TrapType {
    TRAP_SPIKES,    // 地刺
    TRAP_BEAR_TRAP  // 老鼠夹
};

struct Trap {
    float x = 0.0f;
    float y = 0.0f;
    float size = 16.0f;
    TrapType type = TRAP_SPIKES;
};

// 战绩记录（含成功/失败标识）
struct ScoreRecord {
    int score = 0;
    bool success = true; // true: 成功（时间耗尽/通关/历史记录），false: 失败（触发陷阱死亡）
};
