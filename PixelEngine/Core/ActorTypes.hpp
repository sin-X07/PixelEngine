#pragma once

#include <cstdint>

namespace PixelEngine {
}

using ActorId = int;
constexpr ActorId kInvalidActorId     = -1; // 无效 / 无人
constexpr ActorId kEnvironmentActorId = -2; // 环境声源 / 无主来源
constexpr ActorId kAlertSoundSentinel = -3; // 无源警戒声音哨兵
constexpr ActorId kAlertDodgeSentinel = -4; // 近弹闪避哨兵
constexpr int     kMaxActors          = 10;

enum class ControllerKind : std::uint8_t {
    Empty = 0,
    Human = 1,
    Bot   = 2
};

struct ActorInput {
    bool  moveUp      = false;
    bool  moveLeft    = false;
    bool  moveDown    = false;
    bool  moveRight   = false;
    bool  stealth     = false;
    bool  fire        = false;
    bool  reload      = false;
    bool  interact    = false;
    float aimWorldX     = 0.0f;
    float aimWorldY     = 0.0f;
    bool  hasAimWorld   = true;
    bool  rightClick    = false;
    bool  aimIsRelative = false;
};
