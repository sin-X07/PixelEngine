#include "CS2DGame.hpp"
#include "SoundEvents.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

// ============================================================================
// 听觉：声音传播与接收
// ============================================================================

namespace {
constexpr size_t kMaxHeard = 8;

// 会触发「朝向声源警戒」的声音类型
bool AlertsListener(SoundEventType t) {
    switch (t) {
        case SoundEventType::Footstep:
        case SoundEventType::Gunshot:
        case SoundEventType::Reload:
        case SoundEventType::KnifeSwing:
        case SoundEventType::GrenadeThrow:
        case SoundEventType::GrenadeBounce:
        case SoundEventType::Explosion:
        case SoundEventType::Flashbang:
        case SoundEventType::BombBeep:
            return true;
        default:
            return false;
    }
}
} // namespace

void CS2DGame::EmitSound(SoundEventType type, float x, float y, Team team, ActorId sourceId, float radius,
                         WeaponId hazardWeapon, float dangerDelay) {
    if (radius <= 0.0f)
        radius = SoundRadius(type);
    if (radius <= 0.0f)
        return;

    const bool hazard = type == SoundEventType::GrenadeBounce &&
                        (hazardWeapon == WeaponId::GrenadeHE || hazardWeapon == WeaponId::GrenadeMolotov ||
                         hazardWeapon == WeaponId::GrenadeIncendiary);
    const float r2 = radius * radius;
    EnsureBotAi();
    for (size_t i = 0; i < actors_.size(); ++i) {
        Player& listener = actors_[i];
        if (!listener.IsBot() || !listener.alive)
            continue;
        // 不听自己发出的声音
        if (sourceId >= 0 && listener.id == sourceId && !hazard)
            continue;

        const float dx = x - listener.x;
        const float dy = y - listener.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 > r2)
            continue;

        const float dist = std::sqrt(d2);
        const bool hostile = (team != Team::None) && (team != listener.team);

        if (!hostile && !hazard)
            continue;

        auto& ai = AiAt(i);
        // 同类型覆盖：脚步/枪声这类高频声音只保留最新一条
        ai.heard.erase(std::remove_if(ai.heard.begin(), ai.heard.end(),
                                      [type](const HeardSound& h) { return h.type == type; }),
                       ai.heard.end());
        HeardSound h;
        h.type = type;
        h.hazardWeapon = hazardWeapon;
        h.dangerDelay = dangerDelay;
        h.x = x;
        h.y = y;
        h.age = 0.0f;
        h.strength = 1.0f - dist / radius;
        h.team = team;
        h.sourceId = sourceId;
        h.hostile = hostile;
        ai.heard.push_back(h);
        if (ai.heard.size() > kMaxHeard)
            ai.heard.erase(ai.heard.begin());

        if (hostile && AlertsListener(type)) {
            ai.Perceive(sourceId >= 0 ? sourceId : kAlertSoundSentinel, clockSec_,
                        tactics_.perception[std::clamp(ai.difficulty, 0, 3)].reaction);
            ai.alertX = x;
            ai.alertY = y;
            ai.alertTimer = Tuning::SoundAlertMemory;
        }

        // 调试日志：只记敌对声音，每只 BOT 每 2 秒最多一行
        if (ai.heardLogCooldown <= 0.0f) {
            ai.heardLogCooldown = 2.0f;
            std::cout << "[Hear] #" << i << " " << SoundEventName(type) << " 距离 " << static_cast<int>(dist)
                      << " 半径 " << static_cast<int>(radius) << std::endl;
        }
    }
}

const HeardSound* CS2DGame::NewestHeard(size_t botIdx, SoundEventType type,
                                        float maxAge) const {
    if (botIdx >= actorAi_.size()) return nullptr;
    const auto& ai = AiAt(botIdx);
    const HeardSound* best = nullptr;
    for (const auto& h : ai.heard) {
        if (h.type != type || h.age > maxAge) continue;
        if (!best || h.age < best->age) best = &h;
    }
    return best;
}

void CS2DGame::AgeHeardSounds(BotRuntime& ai, float dt) {
    if (ai.alertTimer > 0.0f) ai.alertTimer = std::max(0.0f, ai.alertTimer - dt);
    if (ai.reactionLogCooldown > 0.0f) {
        ai.reactionLogCooldown = std::max(0.0f, ai.reactionLogCooldown - dt);
    }
    for (auto& h : ai.heard) h.age += dt;
    ai.heard.erase(std::remove_if(ai.heard.begin(), ai.heard.end(),
                                  [](const HeardSound& h) {
                                      return h.age > Tuning::SoundMemory;
                                  }),
                   ai.heard.end());
}

// 脚步：静步完全不出声；其余交给统一的听觉分发
void CS2DGame::OnFootstepSound(const Player& source, float x, float y) {
    if (source.stealth) return;
    EmitSound(SoundEventType::Footstep, x, y, source.team, ActorIdOf(source));
}
