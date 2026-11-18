#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include <algorithm>
#include <cmath>

// ============================================================================
// 观察者子系统
//
//   Follow 模式：镜头跟随某位存活队友，鼠标左右键切换队友
//   Free   模式：镜头自由移动（WASD），视野 = 存活队友视锥的并集
//   Bomb   模式：镜头锁在 C4 上方，360° 直线视野
// ============================================================================

int CS2DGame::NextTeammateBot(int from, int dir) const {
    const int n = static_cast<int>(bots_.size());
    if (n <= 0) return -1;
    for (int step = 1; step <= n; ++step) {
        const int idx = ((from + dir * step) % n + n) % n;
        if (bots_[static_cast<size_t>(idx)].alive &&
            bots_[static_cast<size_t>(idx)].team == player_.team) {
            return idx;
        }
    }
    return -1;
}

void CS2DGame::SpectatorBegin() {
    if (mode_ == MatchMode::DeathMatch) return;   // 死斗：死亡镜头，不观战 BOT
    specMode_ = SpectateMode::Follow;
    specFreeX_ = player_.x;
    specFreeY_ = player_.y;
    const int found = NextTeammateBot(-1, +1);
    specTarget_ = (found >= 0) ? found : -1;
    UpdateSpectatorState();
}

void CS2DGame::UpdateSpectatorState() {
    if (player_.alive || IsDeathFreeze() || mode_ == MatchMode::DeathMatch) return;
    if (bombPlanted_ && AliveCount(player_.team) == 0) {
        specMode_ = SpectateMode::Bomb;
        specTarget_ = -1;
        return;
    }
    if (specMode_ == SpectateMode::Bomb) return;
    if (specMode_ == SpectateMode::None) {
        specFreeX_ = player_.x;
        specFreeY_ = player_.y;
        specMode_ = SpectateMode::Follow;
    }
    if (specMode_ == SpectateMode::Follow && !GetSpectatedPlayer()) {
        specTarget_ = NextTeammateBot(specTarget_, 1);
        if (specTarget_ < 0) specMode_ = SpectateMode::Free;
    }
}

void CS2DGame::SpectatorEnd() {
    specMode_ = SpectateMode::None;
    specTarget_ = -1;
}

void CS2DGame::SpectatorToggle() {
    if (IsDeathFreeze() || specMode_ == SpectateMode::Bomb) return;
    if (mode_ == MatchMode::DeathMatch) return;   // 死斗不允许切观战视角
    if (specMode_ == SpectateMode::Follow) {
        // 切到自由视角：从当前跟随目标的位置起步
        if (specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
            specFreeX_ = bots_[static_cast<size_t>(specTarget_)].x;
            specFreeY_ = bots_[static_cast<size_t>(specTarget_)].y;
        }
        specMode_ = SpectateMode::Free;
    } else {
        specMode_ = SpectateMode::Follow;
        if (specTarget_ < 0 ||
            specTarget_ >= static_cast<int>(bots_.size()) ||
            !bots_[static_cast<size_t>(specTarget_)].alive) {
            const int found = NextTeammateBot(-1, +1);
            specTarget_ = (found >= 0) ? found : -1;
        }
    }
}

void CS2DGame::SpectatorCycle(int dir) {
    if (mode_ == MatchMode::DeathMatch) return;
    if (specMode_ != SpectateMode::Follow) return;
    const int found = NextTeammateBot(specTarget_, dir);
    if (found >= 0) specTarget_ = found;
}

void CS2DGame::SpectatorMoveFree(float dx, float dy, float dt) {
    if (mode_ == MatchMode::DeathMatch) return;
    if (specMode_ != SpectateMode::Free || !world_) return;

    const float speed = 840.0f;
    specFreeX_ += dx * speed * dt;
    specFreeY_ += dy * speed * dt;
    specFreeX_ = std::clamp(specFreeX_, 0.0f, world_->GetMapPixelW());
    specFreeY_ = std::clamp(specFreeY_, 0.0f, world_->GetMapPixelH());
}

// 只有队友能看到的敌人才允许出现在观察者画面里
// 自由视角的观察者列表：存活队友（与玩家同阵营）。玩家本人已阵亡，不在列表里。
std::size_t CS2DGame::CollectFreeSpectateObservers(VisibilityQuery::Observer* out,
                                                  std::size_t capacity) const {
    if (!out || capacity == 0) return 0;
    std::size_t count = 0;
    for (const auto& bot : bots_) {
        if (count >= capacity) break;
        if (!bot.alive || bot.team != player_.team) continue;
        const auto view = GetActorView(bot);
        out[count++] = {view.x, view.y, view.aimAngle};
    }
    return count;
}

// 音频听者：观战跟随 → 被观战者；自由视角 → 镜头中心；否则玩家自己
float CS2DGame::ListenerX() const {
    if (specMode_ == SpectateMode::Bomb) return bombX_;
    if (specMode_ == SpectateMode::Follow &&
        specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
        return bots_[static_cast<size_t>(specTarget_)].x;
    }
    if (specMode_ == SpectateMode::Free) return specFreeX_;
    return player_.x;
}

float CS2DGame::ListenerY() const {
    if (specMode_ == SpectateMode::Bomb) return bombY_;
    if (specMode_ == SpectateMode::Follow &&
        specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
        return bots_[static_cast<size_t>(specTarget_)].y;
    }
    if (specMode_ == SpectateMode::Free) return specFreeY_;
    return player_.y;
}
