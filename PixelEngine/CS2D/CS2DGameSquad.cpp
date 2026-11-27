#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include "Visibility/BodyGeometry.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// BOT 运行时状态 / 团队情报 / 小队决策 / 局部避障
// 回答「这支队伍该怎么打」
// ============================================================================

void CS2DGame::EnsureBotAi() {
    const size_t n = actors_.size();
    for (size_t i = 0; i < n; ++i) {
        actors_[i].id = static_cast<ActorId>(i);
    }
    if (actorAi_.size() != n) actorAi_.resize(n);
    if (actorSquad_.size() != n) actorSquad_.resize(n);
    if (roleChangedAt_.size() != n) roleChangedAt_.assign(n, -100.0f);
    if (roleFromMul_.size() != n) roleFromMul_.assign(n, Tuning::SpeedMulSupport);
}

CS2DGame::BotRuntime& CS2DGame::AiAt(size_t i) {
    EnsureBotAi();
    if (actorAi_.empty()) { static BotRuntime fallback; return fallback; }
    if (i >= actorAi_.size()) i = actorAi_.size() - 1;
    return actorAi_[i];
}

const CS2DGame::BotRuntime& CS2DGame::AiAt(size_t i) const {
    if (actorAi_.empty()) { static const BotRuntime fallback; return fallback; }
    if (i >= actorAi_.size()) i = actorAi_.size() - 1;
    return actorAi_[i];
}

size_t CS2DGame::AiIndexOf(const Player& p) const {
    const ActorId id = ActorIdOf(p);
    return id >= 0 ? static_cast<size_t>(id) : 0;
}

void CS2DGame::ResetBotAi(size_t i) {
    EnsureBotAi();
    if (i < actorAi_.size()) {
        const int difficulty = actorAi_[i].difficulty;
        actorAi_[i] = BotRuntime{};
        actorAi_[i].difficulty = difficulty;
    }
}

// ============================================================================
// BOT AI
// ============================================================================

bool CS2DGame::FindCoverPoint(const Player& bot, float threatX, float threatY,
                              float& outX, float& outY) const {
    if (!pathFinder_.IsBuilt()) return false;

    const float radii[3] = { Tuning::CoverRadiusNear, Tuning::CoverRadiusMid,
                             Tuning::CoverRadiusFar };
    float bestD2 = 1e18f;
    bool  found  = false;

    for (int r = 0; r < 3; ++r) {
        for (int a = 0; a < 16; ++a) {
            const float ang = static_cast<float>(a) * 6.28318531f / 16.0f;
            const float px = bot.x + std::cos(ang) * radii[r];
            const float py = bot.y + std::sin(ang) * radii[r];

            if (!pathFinder_.IsWalkableWorld(px, py)) continue;
            // 掩体要求：从该点看不见威胁位置
            if (pathFinder_.HasLineOfSight(px, py, threatX, threatY)) continue;

            const float dx = px - bot.x;
            const float dy = py - bot.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; outX = px; outY = py; found = true; }
        }
    }
    return found;
}

// 敌方出生点质心：与地图朝向无关的推进参考点

bool CS2DGame::EnemySpawnCentroid(Team myTeam, float& outX, float& outY) const {
    if (!world_) return false;

    const bool enemyIsCT = (myTeam == Team::T);
    auto spawns = world_->SpawnsForTeam(enemyIsCT);
    if (spawns.empty()) return false;

    float sx = 0.0f, sy = 0.0f;
    for (const auto* sp : spawns) {
        sx += sp->x;
        sy += sp->y;
    }
    const float n = static_cast<float>(spawns.size());
    outX = sx / n;
    outY = sy / n;
    return true;
}

// P3-savegun: own spawn centroid (mirror of EnemySpawnCentroid).
bool CS2DGame::OwnSpawnCentroid(Team myTeam, float& outX, float& outY) const {
    if (!world_) return false;

    auto spawns = world_->SpawnsForTeam(myTeam == Team::CT);
    if (spawns.empty()) return false;

    float sx = 0.0f, sy = 0.0f;
    for (const auto* sp : spawns) {
        sx += sp->x;
        sy += sp->y;
    }
    const float n = static_cast<float>(spawns.size());
    outX = sx / n;
    outY = sy / n;
    return true;
}

// P3-savegun: retreat point near the CT spawn, spread per bot so savers do not
// stack on one point (P1-5 lesson), and clear of the blast radius.
bool CS2DGame::SaveGunRetreatPoint(size_t i, float& outX, float& outY) const {
    if (!world_ || !pathFinder_.IsBuilt()) return false;
    float cx = 0.0f, cy = 0.0f;
    if (!OwnSpawnCentroid(Team::CT, cx, cy)) return false;

    static const float kRing[8] = { 0.0f, 0.79f, 1.57f, 2.36f,
                                    3.14f, 3.93f, 4.71f, 5.50f };
    const float ang = kRing[i % 8];
    const float radii[3] = { 0.0f, Tuning::SaveGunRetreatRing,
                             Tuning::SaveGunRetreatRing * 2.0f };
    const float minD2 = Tuning::SaveGunMinBombDist * Tuning::SaveGunMinBombDist;
    for (int pass = 0; pass < 2; ++pass) {
        for (float r : radii) {
            const float px = cx + std::cos(ang) * r;
            const float py = cy + std::sin(ang) * r;
            if (!pathFinder_.IsWalkableWorld(px, py)) continue;
            if (pass == 0) {
                const float dx = px - bombX_;
                const float dy = py - bombY_;
                if (dx * dx + dy * dy < minD2) continue;
            }
            outX = px;
            outY = py;
            return true;
        }
    }
    return false;
}


// ============================================================================
// 卡墙脱困
// ============================================================================

// BOT 贴着墙/墙角时，寻路给出的方向可能一直把它顶在墙上。
// 这里按「垂直于去路 → 斜后方 → 后方」的顺序找一个能站下的点，直接挪过去，
// 并清空当前路径强制重算，避免在同一位置反复撞墙。
bool CS2DGame::NudgeOutOfStuck(Player& bot, BotRuntime& ai) {
    if (!world_) return false;

    const float half = bot.size * 0.5f;
    const float baseAng = std::atan2(ai.goalY - bot.y, ai.goalX - bot.x);

    // 只在「侧向」找可走方向；绝不把 BOT 瞬移到别的位置
    const float offsets[4] = { 1.5707963f, -1.5707963f, 0.7853982f, -0.7853982f };
    const float probe = 28.0f;

    for (float off : offsets) {
        const float ang = baseAng + off;
        const float px = bot.x + std::cos(ang) * probe;
        const float py = bot.y + std::sin(ang) * probe;
        if (world_->CheckRectCollision(px - half, py - half, bot.size, bot.size)) {
            continue;
        }
        ai.detourX = std::cos(ang);
        ai.detourY = std::sin(ang);
        ai.detourTimer = 0.45f;      // 沿这个方向走一小段，再回到寻路
        bot.vx = ai.detourX * Tuning::BotMoveSpeed;
        bot.vy = ai.detourY * Tuning::BotMoveSpeed;
        return true;
    }
    return false;
}

// ============================================================================
// 团队情报共享（探点）
// ============================================================================

std::vector<CS2DGame::EnemyContact>& CS2DGame::ContactsOf(Team t) {
    return (t == Team::T) ? contactsT_ : contactsCT_;
}

const std::vector<CS2DGame::EnemyContact>& CS2DGame::ContactsOf(Team t) const {
    return (t == Team::T) ? contactsT_ : contactsCT_;
}

void CS2DGame::ReportContact(Team team, ActorId id, float x, float y) {
    if (team == Team::None || id == kInvalidActorId) return;

    auto& list = ContactsOf(team);
    for (auto& c : list) {
        if (c.id == id) {
            // 同一个人隔了 2 秒以上再次被看到，算一次新的观察（喂给威胁度）
            if (c.age > 2.0f) intel_.OnEnemySeen(x, y, clockSec_);
            c.x = x; c.y = y; c.age = 0.0f;
            return;
        }
    }
    list.push_back(EnemyContact{ id, x, y, 0.0f });
    intel_.OnEnemySeen(x, y, clockSec_);
}

const CS2DGame::EnemyContact* CS2DGame::FindNearestContact(Team team, float x, float y) const {
    const auto& list = ContactsOf(team);
    const EnemyContact* best = nullptr;
    float bestD2 = 1e18f;

    for (const auto& c : list) {
        const Player* target = FindActor(c.id);
        if (!target || !target->IsActive() || !target->alive || target->team == team) continue;

        const float dx = c.x - x;
        const float dy = c.y - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = &c; }
    }
    return best;
}

// 每帧：情报老化 + 全体存活单位上报视野内敌情
void CS2DGame::UpdateTeamContacts(float dt) {
    for (int side = 0; side < 2; ++side) {
        auto& list = (side == 0) ? contactsT_ : contactsCT_;
        for (auto& c : list) c.age += dt;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [](const EnemyContact& c) { return c.age > CONTACT_MEMORY; }),
                   list.end());
    }

    for (size_t i = 0; i < actors_.size(); ++i) {
        const Player& observer = actors_[i];
        if (!observer.IsActive() || !observer.alive) continue;
        if (observer.flashTimer > 0.0f) continue;   // 致盲：看不见任何东西，也无法上报情报

        for (size_t j = 0; j < actors_.size(); ++j) {
            if (j == i) continue;
            const Player& enemy = actors_[j];
            if (!enemy.IsActive() || !enemy.alive || enemy.team == observer.team) continue;
            if (!CanSeeActor(observer, enemy)) continue;
            ReportContact(observer.team, enemy.id, enemy.x, enemy.y);
        }
    }
}

// 真实可见敌人（交火距离 + 烟雾 + LOS）

CS2DGame::BodyVisibility CS2DGame::QueryBodyVisibility(const Player &observer, const Player &target) const {
    const auto revision = world_ ? world_->Revision() : 0;
    if (bodyVisibilityClock_ != clockSec_ || bodyVisibilityTerrain_ != revision) {
        bodyVisibilityCache_.clear();
        bodyVisibilityClock_ = clockSec_;
        bodyVisibilityTerrain_ = revision;
    }
    const bool observerIsBot = observer.IsBot();
    const float observerFlash = observer.flashTimer;
    const std::array<float, 15> signature = {
        observer.x,
        observer.y,
        observer.EffectiveAim(),
        target.x,
        target.y,
        target.EffectiveAim(),
        target.size,
        static_cast<float>(target.spriteKey),
        static_cast<float>(ActorPoseSlot(target)),
        static_cast<float>(target.animState),
        target.animTimer,
        observerFlash,
        static_cast<float>(smokes_.size()),
        observer.alive ? 1.f : 0.f,
        observerIsBot ? static_cast<float>(AiAt(AiIndexOf(observer)).difficulty) : 0.f};
    for (const auto &entry : bodyVisibilityCache_)
        if (entry.observer == &observer && entry.target == &target && entry.signature == signature)
            return entry.result;
    BodyVisibility result;
    result.aim = {target.x, target.y};
    const float targetAim = target.EffectiveAim();
    const float observerAim = observer.EffectiveAim();
    const auto center = BodyGeometry::Transform({0,0},target.x,target.y,target.size,targetAim,
                                                static_cast<int>(target.animState),target.animTimer);
    const float bound = target.size * .707107f + .01f;
    const float dx=center.x-observer.x, dy=center.y-observer.y;
    const float distance=std::hypot(dx,dy);
    float cone = visibility_.ConeAngleDeg();
    bool blind = !observer.alive || (observer.flashTimer > 0.0f);
    if (observerIsBot) {
        const auto& ai = AiAt(AiIndexOf(observer));
        cone = Tuning::VisionConeDeg * tactics_.perception[std::clamp(ai.difficulty,0,3)].angle;
    }
    cone = std::clamp(cone,1.0f,360.0f);
    const float halfCone=(cone+VisibilityQuery::kAngleSoftDeg)*Tuning::Pi/360;
    const float delta=std::abs(std::remainder(std::atan2(dy,dx)-observerAim,2*Tuning::Pi));
    bool hidden = blind || (distance > bound && delta > halfCone + std::asin(bound/distance));
    // A single blocking tile covering all four bounds also covers every body pixel.
    if (!hidden && world_ && distance > bound * 2) {
        const float hit=visibility_.FirstBulletWallFraction(observer.x,observer.y,center.x,center.y);
        if (hit < 1) {
            const int col=static_cast<int>(std::floor((observer.x+dx*(hit+.00001f))/world_->GetTileWidth()));
            const int row=static_cast<int>(std::floor((observer.y+dy*(hit+.00001f))/world_->GetTileHeight()));
            const float left=col*world_->GetTileWidth(), top=row*world_->GetTileHeight();
            const float right=left+world_->GetTileWidth(), bottom=top+world_->GetTileHeight();
            auto blocked = [&](float x,float y) {
                float enter=0, leave=1;
                auto slab = [&](float origin,float direction,float lo,float hi) {
                    if (std::abs(direction)<.000001f) return origin>lo && origin<hi;
                    float a=(lo-origin)/direction, b=(hi-origin)/direction;
                    if (a>b) std::swap(a,b);
                    enter=std::max(enter,a); leave=std::min(leave,b);
                    return enter+.00001f<leave;
                };
                return slab(observer.x,x-observer.x,left,right) &&
                       slab(observer.y,y-observer.y,top,bottom) && enter>0 && leave<1;
            };
            hidden = world_->IsBlockingBullet(row,col) &&
                     blocked(center.x-bound,center.y-bound) && blocked(center.x+bound,center.y-bound) &&
                     blocked(center.x-bound,center.y+bound) && blocked(center.x+bound,center.y+bound);
        }
    }
    if (hidden) {
        bodyVisibilityCache_.push_back({&observer,&target,signature,result});
        return result;
    }
    const auto &pose = BodyGeometry::Get(target.spriteKey, ActorPoseSlot(target));
    std::array<bool, BodyGeometry::Resolution * BodyGeometry::Resolution> visible{};
    int count = 0;
    const float cosine=std::cos(targetAim+Tuning::Pi*.5f), sine=std::sin(targetAim+Tuning::Pi*.5f);
    for (const auto local : pose.samples) {
        const Vec2 point{center.x+target.size*(local.x*cosine-local.y*sine),
                         center.y+target.size*(local.x*sine+local.y*cosine)};
        if (!CanSeePoint(observer, point.x, point.y))
            continue;
        const int x = static_cast<int>((local.x + .5f) * BodyGeometry::Resolution);
        const int y = static_cast<int>((local.y + .5f) * BodyGeometry::Resolution);
        visible[y * BodyGeometry::Resolution + x] = true;
        ++count;
    }
    result.fraction = pose.samples.empty() ? 0 : static_cast<float>(count) / pose.samples.size();
    // Select an actual pixel in the largest connected exposed body region.
    int largest = 0;
    std::vector<int> queue;
    for (int start = 0; start < static_cast<int>(visible.size()); ++start) {
        if (!visible[start])
            continue;
        queue.clear();
        queue.push_back(start);
        visible[start] = false;
        float sx = 0, sy = 0;
        for (size_t i = 0; i < queue.size(); ++i) {
            const int at = queue[i], x = at % BodyGeometry::Resolution, y = at / BodyGeometry::Resolution;
            sx += x;
            sy += y;
            const int neighbors[] = {at - 1, at + 1, at - BodyGeometry::Resolution,
                                     at + BodyGeometry::Resolution};
            for (int n : neighbors) {
                if (n < 0 || n >= static_cast<int>(visible.size()) ||
                    std::abs(n % BodyGeometry::Resolution - x) + std::abs(n / BodyGeometry::Resolution - y) !=
                        1 ||
                    !visible[n])
                    continue;
                visible[n] = false;
                queue.push_back(n);
            }
        }
        if (static_cast<int>(queue.size()) <= largest)
            continue;
        largest = static_cast<int>(queue.size());
        sx /= largest;
        sy /= largest;
        int best = queue.front();
        float bestDistance = 1e9f;
        for (int at : queue) {
            const float d =
                std::hypot(at % BodyGeometry::Resolution - sx, at / BodyGeometry::Resolution - sy);
            if (d < bestDistance) {
                bestDistance = d;
                best = at;
            }
        }
        result.aim = BodyGeometry::Transform(
            {(best % BodyGeometry::Resolution + .5f) / BodyGeometry::Resolution - .5f,
             (best / BodyGeometry::Resolution + .5f) / BodyGeometry::Resolution - .5f},
            target.x, target.y, target.size, target.EffectiveAim(), static_cast<int>(target.animState),
            target.animTimer);
    }
    bodyVisibilityCache_.push_back({&observer, &target, signature, result});
    return result;
}

bool CS2DGame::CanSeeActor(const Player& observer, const Player& target) const {
    return target.alive && QueryBodyVisibility(observer,target).fraction > 0;
}

bool CS2DGame::CanSeePoint(const Player& observer, float x, float y) const {
    if (!observer.alive || observer.flashTimer > 0.0f) return false;
    VisibilityQuery query = visibility_;
    if (observer.IsBot()) {
        const auto& ai = AiAt(AiIndexOf(observer));
        const auto& perception = tactics_.perception[std::clamp(ai.difficulty, 0, 3)];
        query.SetVisionRadius(Tuning::VisionRadius * perception.radius);
        query.SetConeAngleDeg(Tuning::VisionConeDeg * perception.angle);
    }
    if (SegmentBlockedBySmoke(observer.x, observer.y, x, y)) return false;
    if (!query.World()) return true;
    return query.ComputeVisibility(observer.x, observer.y, observer.EffectiveAim(), x, y) > 0.0f;
}

const CS2DGame::Player* CS2DGame::FindVisibleEnemy(const Player& self, float& outDist) const {
    outDist = 0.0f;
    if (self.flashTimer > 0.0f) return nullptr;   // 被闪光致盲，看不到任何人

    const Player* best = nullptr;
    float bestD2 = 1e18f;

    for (const auto& e : actors_) {
        if (&e == &self || !e.IsActive() || !e.alive || !IsHostile(e, self)) continue;
        const float dx = e.x - self.x;
        const float dy = e.y - self.y;
        const float d2 = dx * dx + dy * dy;
        if (!CanSeeActor(self, e)) continue;
        if (d2 < bestD2) { bestD2 = d2; best = &e; }
    }

    outDist = best ? std::sqrt(bestD2) : 0.0f;
    return best;
}

// ============================================================================
// 小队协同：分工与集火
// ============================================================================

void CS2DGame::RefreshRolesForTeam(Team t) {
    if (!world_ || actors_.empty()) return;
    EnsureBotAi();

    const TeamPlan& coachPlan = CoachFor(t).plan;
    if (coachPlan.valid) {
        bool planWantsEntry = false, planWantsAnchor = false;
        for (const auto& so : coachPlan.slots) {
            if (so.role == CoachRole::Entry)  planWantsEntry = true;
            if (so.role == CoachRole::Anchor) planWantsAnchor = true;
        }
        if (!planWantsEntry && !planWantsAnchor) return;
    }

    int entries = 0, anchors = 0;
    for (size_t i = 0; i < actors_.size() && i < actorSquad_.size(); ++i) {
        if (!actors_[i].IsBot() || !actors_[i].alive || actors_[i].team != t) continue;
        if (actorSquad_[i].role == BotRole::Entry)  ++entries;
        if (actorSquad_[i].role == BotRole::Anchor) ++anchors;
    }
    if (entries > 0 && anchors > 0) return;

    float ex = 0.0f, ey = 0.0f;
    if (!EnemySpawnCentroid(t, ex, ey)) return;

    int bestEntry = -1, bestAnchor = -1;
    float bestEntryD2 = 1e18f, bestAnchorD2 = -1.0f;
    for (size_t i = 0; i < actors_.size() && i < actorSquad_.size(); ++i) {
        if (!actors_[i].IsBot() || !actors_[i].alive || actors_[i].team != t) continue;
        if (clockSec_ - roleChangedAt_[i] < Tuning::ai::RoleSwitchCooldown) continue;
        if (actorSquad_[i].role == BotRole::Entry || actorSquad_[i].role == BotRole::Anchor) continue;
        const float dx = actors_[i].x - ex, dy = actors_[i].y - ey;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestEntryD2)  { bestEntryD2 = d2;  bestEntry = static_cast<int>(i); }
        if (d2 > bestAnchorD2) { bestAnchorD2 = d2; bestAnchor = static_cast<int>(i); }
    }

    if (coachPlan.valid) {
        bool planWantsEntry = false, planWantsAnchor = false;
        for (const auto& so : coachPlan.slots) {
            if (so.role == CoachRole::Entry)  planWantsEntry = true;
            if (so.role == CoachRole::Anchor) planWantsAnchor = true;
        }
        if (entries == 0 && bestEntry >= 0 && planWantsEntry) {
            SetRoleWithTransition(static_cast<size_t>(bestEntry), BotRole::Entry);
            ++counters_.roleSwitches;
        }
        if (anchors == 0 && bestAnchor >= 0 && bestAnchor != bestEntry && planWantsAnchor) {
            SetRoleWithTransition(static_cast<size_t>(bestAnchor), BotRole::Anchor);
            ++counters_.roleSwitches;
        }
        return;
    }

    if (entries == 0 && bestEntry >= 0) {
        SetRoleWithTransition(static_cast<size_t>(bestEntry), BotRole::Entry);
        ++counters_.roleSwitches;
    }
    if (anchors == 0 && bestAnchor >= 0 && bestAnchor != bestEntry) {
        SetRoleWithTransition(static_cast<size_t>(bestAnchor), BotRole::Anchor);
        ++counters_.roleSwitches;
    }
}
// 重置搜点白名单（每回合开始调用）：所有区域标记为"未搜"
void CS2DGame::RefreshSearchAreas() {
    for (int t = 0; t < 2; ++t) areaSearchedAt_[t].clear();
    if (!world_) return;
    for (const auto& a : world_->GetAreas()) {
        areaSearchedAt_[0][a.name] = -100.0f;
        areaSearchedAt_[1][a.name] = -100.0f;
    }
}

void CS2DGame::AssignSquadRoles(bool dynamic) {
    if (!dynamic) actorSquad_.assign(actors_.size(), SquadOrder{});
    else EnsureBotAi();
    if (actors_.empty() || !world_) return;

    const auto& sites = world_->GetBombSites();
    const int siteCount = static_cast<int>(sites.size());
    float midX = 0.0f, midY = 0.0f;
    if (siteCount >= 2) {
        float sx = 0.0f, sy = 0.0f;
        for (const auto& s : sites) {
            sx += s.cx;
            sy += s.cy;
        }
        midX = sx / siteCount;
        midY = sy / siteCount;
    }

    if (!dynamic && mode_ == MatchMode::BombDefusal && siteCount > 0) {
        int tIdx = 0, ctIdx = 0;
        std::vector<std::pair<float, float>> usedHoldPts;
        std::vector<char> holdReassigned(actors_.size(), 0);
        auto collectUsed = [&](size_t self) {
            std::vector<std::pair<float, float>> used = usedHoldPts;
            for (size_t j = 0; j < actors_.size(); ++j) {
                if (j == self || !actors_[j].IsBot() || !actors_[j].alive) continue;
                if (holdReassigned[j]) continue;
                const auto& aj = AiAt(j);
                if (aj.hasPlanHold) used.emplace_back(aj.planHoldX, aj.planHoldY);
            }
            return used;
        };
        auto tooClose = [](const std::vector<std::pair<float, float>>& used, float x, float y) {
            for (const auto& q : used) {
                const float dx = x - q.first, dy = y - q.second;
                if (dx * dx + dy * dy < 40.0f * 40.0f) return true;
            }
            return false;
        };
        auto spreadIfClose = [&](size_t self, float& gx, float& gy) {
            std::vector<std::pair<float, float>> used = collectUsed(self);
            if (tooClose(used, gx, gy))
                SpreadHoldPoint(gx, gy, static_cast<int>(used.size()), gx, gy, used);
            usedHoldPts.emplace_back(gx, gy);
            holdReassigned[self] = 1;
        };
        for (size_t i = 0; i < actors_.size(); ++i) {
            Player& b = actors_[i];
            if (!b.IsBot()) continue;
            if (b.team == Team::T) {
                const int site = tIdx % siteCount;
                ++tIdx;
                actorSquad_[i].site = site;
                float gx = sites[site].cx, gy = sites[site].cy;
                spreadIfClose(i, gx, gy);
                SetBotGoal(i, gx, gy, BotRuntime::GoalSource::Squad);
            } else {
                const int slot = ctIdx % (siteCount + 1);
                ++ctIdx;
                if (slot < siteCount) {
                    actorSquad_[i].site = slot;
                    float gx = sites[slot].cx, gy = sites[slot].cy;
                    spreadIfClose(i, gx, gy);
                    SetBotGoal(i, gx, gy, BotRuntime::GoalSource::Squad);
                } else {
                    actorSquad_[i].site = -1;             // 中路
                    float gx = midX, gy = midY;
                    spreadIfClose(i, gx, gy);
                    SetBotGoal(i, gx, gy, BotRuntime::GoalSource::Squad);
                }
            }
        }
    }

    {
        auto isSide = [&](int idx, const char* side) {
            return idx >= 0 && idx < siteCount && sites[idx].name == side;
        };
        int tA = 0, tB = 0, ctA = 0, ctB = 0, ctMid = 0;
        for (size_t i = 0; i < actors_.size() && i < actorSquad_.size(); ++i) {
            if (!actors_[i].IsBot() || !actors_[i].alive || !actorSquad_[i].hasHold) continue;
            if (actors_[i].team == Team::T) {
                if (isSide(actorSquad_[i].site, "A")) ++tA;
                else if (isSide(actorSquad_[i].site, "B")) ++tB;
            } else {
                if (isSide(actorSquad_[i].site, "A")) ++ctA;
                else if (isSide(actorSquad_[i].site, "B")) ++ctB;
                else ++ctMid;
            }
        }
        std::cout << "[Squad] T A=" << tA << " B=" << tB
                  << " | CT A=" << ctA << " B=" << ctB
                  << " Mid=" << ctMid << std::endl;
    }

    for (int side = 0; side < 2; ++side) {
        const Team t = (side == 0) ? Team::T : Team::CT;

        std::vector<size_t> members;
        for (size_t i = 0; i < actors_.size(); ++i) {
            if (actors_[i].IsBot() && actors_[i].team == t) members.push_back(i);
        }
        if (members.empty()) continue;

        float ex = 0.0f, ey = 0.0f;
        int   n  = 0;
        for (const auto& b : actors_) {
            if (b.IsActive() && b.alive && b.team != t) { ex += b.x; ey += b.y; ++n; }
        }
        if (n > 0) { ex /= static_cast<float>(n); ey /= static_cast<float>(n); }
        else {
            ex = world_->GetMapPixelW() * 0.5f;
            ey = world_->GetMapPixelH() * 0.5f;
        }

        std::sort(members.begin(), members.end(), [&](size_t a, size_t b) {
            auto d2 = [&](size_t i) {
                const float dx = actors_[i].x - ex;
                const float dy = actors_[i].y - ey;
                return dx * dx + dy * dy;
            };
            return d2(a) < d2(b);
        });

        const size_t cnt = members.size();
        const size_t entryCount  = std::max<size_t>(1, (cnt + 2) / 3);
        const size_t anchorCount = std::max<size_t>(1, (cnt + 3) / 4);

        if (roleChangedAt_.size() != actors_.size()) roleChangedAt_.assign(actors_.size(), -100.0f);
        for (size_t k = 0; k < cnt; ++k) {
            BotRole role = BotRole::Support;
            if (k < entryCount)                        role = BotRole::Entry;
            else if (k >= cnt - anchorCount)           role = BotRole::Anchor;

            const size_t bi = members[k];
            if (dynamic && bi < roleChangedAt_.size() &&
                clockSec_ - roleChangedAt_[bi] < Tuning::ai::RoleSwitchCooldown) {
                continue;
            }
            if (actorSquad_[bi].role != role) {
                SetRoleWithTransition(bi, role);
                if (dynamic) ++counters_.roleSwitches;
            }
            if (bi < roleChangedAt_.size()) roleChangedAt_[bi] = clockSec_;
        }
    }
}

void CS2DGame::UpdateSquadFocus(float dt) {
    EnsureBotAi();

    squadFocusTimer_ -= dt;
    if (squadFocusTimer_ > 0.0f) return;
    squadFocusTimer_ = Tuning::FocusUpdateInterval;

    for (size_t i = 0; i < actors_.size(); ++i) {
        const Player& bot = actors_[i];
        if (!bot.IsBot() || !bot.alive) { actorSquad_[i].focus = kInvalidActorId; continue; }

        ActorId bestFocus = kInvalidActorId;
        float bestScore = 1e18f;

        for (const auto& c : ContactsOf(bot.team)) {
            const Player* enemy = FindActor(c.id);
            if (!enemy || !enemy->IsActive() || !enemy->alive || enemy->team == bot.team) continue;

            const float dx = c.x - bot.x;
            const float dy = c.y - bot.y;
            float score = std::sqrt(dx * dx + dy * dy);

            if (mode_ == MatchMode::BombDefusal && bombPlanted_ && bot.team == Team::T) {
                const float bdx = c.x - bombX_;
                const float bdy = c.y - bombY_;
                if (bdx * bdx + bdy * bdy < 280.0f * 280.0f) score -= 300.0f;
            }

            int mates = 0;
            for (const auto& m : actors_) {
                if (&m == &bot || !m.IsActive() || !m.alive || m.team != bot.team) continue;
                const float mx = m.x - c.x, my = m.y - c.y;
                if (mx * mx + my * my < Tuning::FocusMateRadius * Tuning::FocusMateRadius) ++mates;
            }
            score -= static_cast<float>(mates) * Tuning::FocusMateWeight;

            for (const auto& m : actors_) {
                if (&m == &bot || !m.IsActive() || !m.alive || m.team != bot.team) continue;
                if (!(m.reloading || m.health < 40)) continue;
                const float mx = m.x - c.x, my = m.y - c.y;
                if (mx * mx + my * my < Tuning::FocusCoverRadius * Tuning::FocusCoverRadius) {
                    score -= Tuning::FocusCoverWeight;
                    break;
                }
            }

            if (score < bestScore) {
                bestScore = score;
                bestFocus = c.id;
            }
        }

        actorSquad_[i].focus = bestFocus;
    }
}

const CS2DGame::Player* CS2DGame::ResolveFocus(const SquadOrder& order) const {
    const Player* p = FindActor(order.focus);
    return (p && p->IsActive() && p->alive) ? p : nullptr;
}

// ============================================================================
// 局部避障（分离力）
// ============================================================================

namespace {
constexpr float SEP_RADIUS    = Tuning::SepRadius;    // 排斥半径
constexpr float SEP_MAX_SPEED = Tuning::SepMaxSpeed;  // 与 BOT 移动上限一致
}

void CS2DGame::AccumulateSeparation(const Player& self, float& outX, float& outY) const {
    outX = 0.0f;
    outY = 0.0f;

    for (const auto& other : actors_) {
        if (&other == &self || !other.IsActive() || !other.alive) continue;

        const float dx = self.x - other.x;
        const float dy = self.y - other.y;
        const float d2 = dx * dx + dy * dy;
        constexpr float kSepDeadZone = SEP_RADIUS * Tuning::SepDeadZoneFrac;
        if (d2 >= kSepDeadZone * kSepDeadZone) continue;

        if (d2 < 0.01f) {
            outX += (self.team == Team::T) ? 1.0f : -1.0f;
            outY += 1.0f;
            return;
        }

        const float d = std::sqrt(d2);
        const float t = 1.0f - d / kSepDeadZone;
        const float w = t * t;
        outX += dx / d * w;
        outY += dy / d * w;
    }

    // 归一化到 0..1，避免人多时力叠加爆表
    const float len = std::sqrt(outX * outX + outY * outY);
    if (len > 1.0f) {
        outX /= len;
        outY /= len;
    }
}

void CS2DGame::ApplyAvoidance(float& vx, float& vy, float sepX, float sepY) const {
    // 分离力强度：与当前速度同量级，保证能推开但不至于完全抵消前进
    constexpr float SEP_STRENGTH = Tuning::SepStrength;

    vx += sepX * SEP_STRENGTH;
    vy += sepY * SEP_STRENGTH;

    const float spd = std::sqrt(vx * vx + vy * vy);
    if (spd > SEP_MAX_SPEED) {
        vx = vx / spd * SEP_MAX_SPEED;
        vy = vy / spd * SEP_MAX_SPEED;
    }
}

// ============================================================================
// A* 路径跟随
// ============================================================================

