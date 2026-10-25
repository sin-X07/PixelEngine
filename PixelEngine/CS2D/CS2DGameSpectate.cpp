#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include <algorithm>
#include <cmath>

// ============================================================================
// 观察者子系统
//
//   Follow 模式：镜头跟随某位存活队友，鼠标左右键切换队友
//   Free   模式：镜头自由移动（WASD），但敌人只在「队友能看到」时才绘制
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
    specMode_ = SpectateMode::Follow;
    specFreeX_ = player_.x;
    specFreeY_ = player_.y;
    const int found = NextTeammateBot(-1, +1);
    specTarget_ = (found >= 0) ? found : -1;
}

void CS2DGame::SpectatorEnd() {
    specMode_ = SpectateMode::None;
    specTarget_ = -1;
}

void CS2DGame::SpectatorToggle() {
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
    if (specMode_ != SpectateMode::Follow) return;
    const int found = NextTeammateBot(specTarget_, dir);
    if (found >= 0) specTarget_ = found;
}

void CS2DGame::SpectatorMoveFree(float dx, float dy, float dt) {
    if (specMode_ != SpectateMode::Free || !world_) return;

    const float speed = 420.0f;
    specFreeX_ += dx * speed * dt;
    specFreeY_ += dy * speed * dt;
    specFreeX_ = std::clamp(specFreeX_, 0.0f, world_->GetMapPixelW());
    specFreeY_ = std::clamp(specFreeY_, 0.0f, world_->GetMapPixelH());
}

// 只有队友能看到的敌人才允许出现在观察者画面里
bool CS2DGame::SpectatorCanSeeEnemy(const Player& e) const {
    if (!e.alive || e.team == player_.team) return true;   // 友军始终可见

    auto seenBy = [&](const Player& f) {
        if (!f.alive || f.team == e.team) return false;
        const float dx = e.x - f.x;
        const float dy = e.y - f.y;
        if (dx * dx + dy * dy > Tuning::VisionRadius * Tuning::VisionRadius) return false;
        if (!InViewCone(f.x, f.y, f.aimAngle, e.x, e.y, Tuning::VisionRadius)) return false;
        if (SegmentBlockedBySmoke(f.x, f.y, e.x, e.y)) return false;
        if (renderer_ && !renderer_->HasLineOfSight(f.x, f.y, e.x, e.y)) return false;
        return true;
    };

    if (specMode_ == SpectateMode::Follow &&
        specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
        return seenBy(bots_[static_cast<size_t>(specTarget_)]);
    }

    for (const auto& f : bots_) {
        if (seenBy(f)) return true;
    }
    return false;
}

// 音频听者：观战跟随 → 被观战者；自由视角 → 镜头中心；否则玩家自己
float CS2DGame::ListenerX() const {
    if (specMode_ == SpectateMode::Follow &&
        specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
        return bots_[static_cast<size_t>(specTarget_)].x;
    }
    if (specMode_ == SpectateMode::Free) return specFreeX_;
    return player_.x;
}

float CS2DGame::ListenerY() const {
    if (specMode_ == SpectateMode::Follow &&
        specTarget_ >= 0 && specTarget_ < static_cast<int>(bots_.size())) {
        return bots_[static_cast<size_t>(specTarget_)].y;
    }
    if (specMode_ == SpectateMode::Free) return specFreeY_;
    return player_.y;
}
