#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include <algorithm>
#include <cmath>

// ============================================================================
// 观察者子系统
//
//   Follow 模式：镜头跟随某位存活队友（真人或 BOT），鼠标左右键切换队友
//   Free   模式：镜头自由移动（WASD），视野 = 存活队友视锥的并集
//   Bomb   模式：镜头锁在 C4 上方，360° 直线视野
// ============================================================================

ActorId CS2DGame::NextTeammateBot(ActorId from, int dir) const {
    const int n = static_cast<int>(actors_.size());
    if (n <= 0) return kInvalidActorId;
    const Team myTeam = LocalActor().team;
    const int start = (from >= 0 && from < n) ? from : -1;
    for (int step = 1; step <= n; ++step) {
        const ActorId idx = ((start + dir * step) % n + n) % n;
        const auto& a = actors_[static_cast<size_t>(idx)];
        if (idx != localActorId_ && a.IsActive() && a.alive && a.team == myTeam) {
            return idx;
        }
    }
    return kInvalidActorId;
}

void CS2DGame::SpectatorBegin() {
    if (mode_ == MatchMode::DeathMatch) return;   // 死斗：死亡镜头，不观战队友
    specMode_ = SpectateMode::Follow;
    const auto& local = LocalActor();
    specFreeX_ = local.x;
    specFreeY_ = local.y;
    specTarget_ = NextTeammateBot(kInvalidActorId, +1);
    UpdateSpectatorState();
}

void CS2DGame::UpdateSpectatorState() {
    const auto& local = LocalActor();
    if (local.alive || IsDeathFreeze() || mode_ == MatchMode::DeathMatch) return;
    if (bombPlanted_ && AliveCount(local.team) == 0) {
        specMode_ = SpectateMode::Bomb;
        specTarget_ = kInvalidActorId;
        return;
    }
    if (specMode_ == SpectateMode::Bomb) return;
    if (specMode_ == SpectateMode::None) {
        specFreeX_ = local.x;
        specFreeY_ = local.y;
        specMode_ = SpectateMode::Follow;
    }
    if (specMode_ == SpectateMode::Follow && !GetSpectatedPlayer()) {
        specTarget_ = NextTeammateBot(specTarget_, 1);
        if (specTarget_ == kInvalidActorId) specMode_ = SpectateMode::Free;
    }
}

void CS2DGame::SpectatorEnd() {
    specMode_ = SpectateMode::None;
    specTarget_ = kInvalidActorId;
}

void CS2DGame::SpectatorToggle() {
    if (IsDeathFreeze() || specMode_ == SpectateMode::Bomb) return;
    if (mode_ == MatchMode::DeathMatch) return;   // 死斗不允许切观战视角
    if (specMode_ == SpectateMode::Follow) {
        // 切到自由视角：从当前跟随目标的位置起步
        if (const Player* target = FindActor(specTarget_)) {
            specFreeX_ = target->x;
            specFreeY_ = target->y;
        }
        specMode_ = SpectateMode::Free;
    } else {
        specMode_ = SpectateMode::Follow;
        const Player* target = FindActor(specTarget_);
        if (!target || !target->IsActive() || !target->alive || target->id == localActorId_) {
            specTarget_ = NextTeammateBot(kInvalidActorId, +1);
        }
    }
}

void CS2DGame::SpectatorCycle(int dir) {
    if (mode_ == MatchMode::DeathMatch) return;
    if (specMode_ != SpectateMode::Follow) return;
    const ActorId found = NextTeammateBot(specTarget_, dir);
    if (found != kInvalidActorId) specTarget_ = found;
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

// 自由视角的观察者列表：存活队友（与本地玩家同阵营且非本人）。
std::size_t CS2DGame::CollectFreeSpectateObservers(VisibilityQuery::Observer* out,
                                                  std::size_t capacity) const {
    if (!out || capacity == 0) return 0;
    std::size_t count = 0;
    const Team myTeam = LocalActor().team;
    for (const auto& actor : actors_) {
        if (count >= capacity) break;
        if (!actor.IsActive() || !actor.alive || actor.id == localActorId_ || actor.team != myTeam) continue;
        const auto view = GetActorView(actor);
        out[count++] = {view.x, view.y, view.aimAngle};
    }
    return count;
}

// 音频听者：观战跟随 → 被观战者；自由视角 → 镜头中心；否则本地玩家自己
float CS2DGame::ListenerX() const {
    if (specMode_ == SpectateMode::Bomb) return bombX_;
    if (specMode_ == SpectateMode::Follow) {
        if (const Player* target = FindActor(specTarget_)) return target->x;
    }
    if (specMode_ == SpectateMode::Free) return specFreeX_;
    return LocalActor().x;
}

float CS2DGame::ListenerY() const {
    if (specMode_ == SpectateMode::Bomb) return bombY_;
    if (specMode_ == SpectateMode::Follow) {
        if (const Player* target = FindActor(specTarget_)) return target->y;
    }
    if (specMode_ == SpectateMode::Free) return specFreeY_;
    return LocalActor().y;
}
