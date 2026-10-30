#include "CS2DGame.hpp"
#include "SoundEvents.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

// ============================================================================
// 听觉：声音传播与接收
//
// 发声点调用 EmitSound(...)，半径内的每只存活 BOT 收到一条 HeardSound：
//   - 同类型只保留最新一条（脚步/枪声频率高，避免堆积）
//   - 最多保留 kMaxHeard 条，按 age 老化
//   - 敌对声音会同时刷新「警戒朝向」(alertX/Y)，供没有视野时转头
// 具体怎么反应由 CS2DGameBots.cpp 的决策树读取这些信息决定。
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

void CS2DGame::EmitSound(SoundEventType type, float x, float y, Team team,
                         int sourceIdx, float radius) {
    if (radius <= 0.0f) radius = SoundRadius(type);
    if (radius <= 0.0f) return;

    const float r2 = radius * radius;
    for (size_t i = 0; i < bots_.size(); ++i) {
        Player& listener = bots_[i];
        if (!listener.alive) continue;
        // 不听自己发出的声音
        if (sourceIdx >= 0 && static_cast<int>(i) == sourceIdx) continue;

        const float dx = x - listener.x;
        const float dy = y - listener.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 > r2) continue;

        const float dist = std::sqrt(d2);
        const bool hostile = (team != Team::None) && (team != listener.team);

        // 只记录敌对声音：友军的声音不含威胁信息，而且会把同类型的
        // 「敌人位置」记忆冲掉（例如队友开枪盖掉刚才听到的敌方枪声）。
        if (!hostile) continue;

        auto& ai = AiAt(i);
        // 同类型覆盖：脚步/枪声这类高频声音只保留最新一条
        ai.heard.erase(std::remove_if(ai.heard.begin(), ai.heard.end(),
                                      [type](const HeardSound& h) {
                                          return h.type == type;
                                      }),
                       ai.heard.end());
        HeardSound h;
        h.type      = type;
        h.x         = x;
        h.y         = y;
        h.age       = 0.0f;
        h.strength  = 1.0f - dist / radius;
        h.team      = team;
        h.sourceIdx = sourceIdx;
        h.hostile   = hostile;
        ai.heard.push_back(h);
        if (ai.heard.size() > kMaxHeard) ai.heard.erase(ai.heard.begin());

        if (AlertsListener(type)) {
            ai.alertX = x;
            ai.alertY = y;
            ai.alertTimer = Tuning::SoundAlertMemory;
        }

        // 调试日志：只记敌对声音，每只 BOT 每 2 秒最多一行
        if (ai.heardLogCooldown <= 0.0f) {
            ai.heardLogCooldown = 2.0f;
            std::cout << "[Hear] #" << i << " " << SoundEventName(type)
                      << " 距离 " << static_cast<int>(dist)
                      << " 半径 " << static_cast<int>(radius) << std::endl;
        }
    }
}

const HeardSound* CS2DGame::NewestHeard(size_t botIdx, SoundEventType type,
                                        float maxAge) const {
    if (botIdx >= botAi_.size()) return nullptr;
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
    int idx = -2;
    if (&source == &player_) {
        idx = -1;
    } else if (!bots_.empty() &&
               &source >= bots_.data() &&
               &source < bots_.data() + bots_.size()) {
        idx = static_cast<int>(&source - bots_.data());
    }
    EmitSound(SoundEventType::Footstep, x, y, source.team, idx);
}
