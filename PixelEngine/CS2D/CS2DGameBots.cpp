#include "CS2DGame.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// BOT 运行时状态（与 bots_ 同下标）
// ============================================================================

void CS2DGame::EnsureBotAi() {
    if (botAi_.size() != bots_.size()) botAi_.resize(bots_.size());
}

CS2DGame::BotRuntime& CS2DGame::AiAt(size_t i) {
    EnsureBotAi();
    if (botAi_.empty()) { static BotRuntime fallback; return fallback; }
    if (i >= botAi_.size()) i = botAi_.size() - 1;
    return botAi_[i];
}

const CS2DGame::BotRuntime& CS2DGame::AiAt(size_t i) const {
    if (botAi_.empty()) { static const BotRuntime fallback; return fallback; }
    if (i >= botAi_.size()) i = botAi_.size() - 1;
    return botAi_[i];
}

size_t CS2DGame::AiIndexOf(const Player& p) const {
    if (bots_.empty()) return 0;
    const Player* base = bots_.data();
    const Player* ptr  = &p;
    if (ptr < base || ptr >= base + bots_.size()) return 0;
    return static_cast<size_t>(ptr - base);
}

void CS2DGame::ResetBotAi(size_t i) {
    EnsureBotAi();
    if (i < botAi_.size()) botAi_[i] = BotRuntime{};
}

// ============================================================================
// BOT AI
// ============================================================================

bool CS2DGame::BotCanSeePlayer(const Player& bot) const {
    if (!player_.alive || player_.team == bot.team) return false;
    const float dx = player_.x - bot.x;
    const float dy = player_.y - bot.y;
    if (std::sqrt(dx * dx + dy * dy) > BOT_VIEW_RADIUS) return false;
    if (!renderer_) return true;
    return renderer_->HasLineOfSight(bot.x, bot.y, player_.x, player_.y);
}

bool CS2DGame::PlayerCanSeeBot(const Player& bot) const {
    if (!bot.alive) return false;
    // 烟雾遮挡
    if (SegmentBlockedBySmoke(player_.x, player_.y, bot.x, bot.y)) return false;
    if (!renderer_) return true;
    return renderer_->IsVisibleToPlayer(player_.x, player_.y, player_.aimAngle,
                                        bot.x, bot.y);
}

// 找最近的存活敌人（玩家或其它 BOT）
// 已废弃：BOT 不再全图感知，改用 FindVisibleEnemy + 团队情报。

// ============================================================================
// 受击撤退：找掩体
// ============================================================================

bool CS2DGame::FindCoverPoint(const Player& bot, float threatX, float threatY,
                              float& outX, float& outY) const {
    if (!pathFinder_.IsBuilt()) return false;

    const float radii[3] = { 55.0f, 95.0f, 135.0f };
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

// ============================================================================
// 团队情报共享（探点）
// ============================================================================

std::vector<CS2DGame::EnemyContact>& CS2DGame::ContactsOf(Team t) {
    return (t == Team::T) ? contactsT_ : contactsCT_;
}

const std::vector<CS2DGame::EnemyContact>& CS2DGame::ContactsOf(Team t) const {
    return (t == Team::T) ? contactsT_ : contactsCT_;
}

void CS2DGame::ReportContact(Team team, int id, float x, float y) {
    if (team == Team::None) return;

    auto& list = ContactsOf(team);
    for (auto& c : list) {
        if (c.id == id) { c.x = x; c.y = y; c.age = 0.0f; return; }
    }
    list.push_back(EnemyContact{ id, x, y, 0.0f });
}

const CS2DGame::EnemyContact* CS2DGame::FindNearestContact(Team team, float x, float y) const {
    const auto& list = ContactsOf(team);
    const EnemyContact* best = nullptr;
    float bestD2 = 1e18f;

    for (const auto& c : list) {
        // 目标必须仍然存活，否则情报作废
        bool aliveTarget = false;
        if (c.id == -1) {
            aliveTarget = player_.alive && player_.team != team;
        } else if (c.id >= 0 && c.id < static_cast<int>(bots_.size())) {
            aliveTarget = bots_[c.id].alive && bots_[c.id].team != team;
        }
        if (!aliveTarget) continue;

        const float dx = c.x - x;
        const float dy = c.y - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = &c; }
    }
    return best;
}

// 每帧：情报老化 + 各 BOT 在探点半径内上报
void CS2DGame::UpdateTeamContacts(float dt) {
    for (int side = 0; side < 2; ++side) {
        auto& list = (side == 0) ? contactsT_ : contactsCT_;
        for (auto& c : list) c.age += dt;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [](const EnemyContact& c) { return c.age > CONTACT_MEMORY; }),
                   list.end());
    }

    for (size_t i = 0; i < bots_.size(); ++i) {
        const Player& b = bots_[i];
        if (!b.alive) continue;
        if (AiAt(i).blindTimer > 0.0f) continue;   // 致盲：看不见任何东西，也无法上报情报

        auto scan = [&](const Player& e, int id) {
            if (!e.alive || e.team == b.team) return;

            const float dx = e.x - b.x;
            const float dy = e.y - b.y;
            if (dx * dx + dy * dy > BOT_SPOT_RADIUS * BOT_SPOT_RADIUS) return;
            if (SegmentBlockedBySmoke(b.x, b.y, e.x, e.y)) return;
            if (renderer_ && !renderer_->HasLineOfSight(b.x, b.y, e.x, e.y)) return;

            ReportContact(b.team, id, e.x, e.y);
        };

        scan(player_, -1);
        for (size_t j = 0; j < bots_.size(); ++j) {
            if (j == i) continue;
            scan(bots_[j], static_cast<int>(j));
        }
    }
}

// 真实可见敌人（交火距离 + 烟雾 + LOS）
const CS2DGame::Player* CS2DGame::FindVisibleEnemy(const Player& self, float& outDist) const {
    outDist = 0.0f;
    if (AiAt(AiIndexOf(self)).blindTimer > 0.0f) return nullptr;   // 被闪光致盲，看不到任何人

    const Player* best = nullptr;
    float bestD2 = 1e18f;

    auto consider = [&](const Player& e) {
        if (!e.alive || e.team == self.team) return;

        const float dx = e.x - self.x;
        const float dy = e.y - self.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 > BOT_VIEW_RADIUS * BOT_VIEW_RADIUS) return;
        if (SegmentBlockedBySmoke(self.x, self.y, e.x, e.y)) return;
        if (renderer_ && !renderer_->HasLineOfSight(self.x, self.y, e.x, e.y)) return;

        if (d2 < bestD2) { bestD2 = d2; best = &e; }
    };

    consider(player_);
    for (const auto& b : bots_) {
        if (&b == &self) continue;
        consider(b);
    }

    outDist = best ? std::sqrt(bestD2) : 0.0f;
    return best;
}

// ============================================================================
// 小队协同：分工与集火
// ============================================================================

void CS2DGame::AssignSquadRoles() {
    squad_.assign(bots_.size(), SquadOrder{});
    if (bots_.empty() || !world_) return;

    // ---- 爆破图：T 分队进攻 A/B，CT 分兵守 A/B/中路 ----
    const auto& sites = world_->GetBombSites();
    const int siteCount = static_cast<int>(sites.size());
    float midX = 0.0f, midY = 0.0f;
    if (siteCount >= 2) {
        float sx = 0.0f, sy = 0.0f;
        for (const auto& s : sites) {
            sx += s.x + s.width * 0.5f;
            sy += s.y + s.height * 0.5f;
        }
        midX = sx / siteCount;
        midY = sy / siteCount;
    }

    if (mode_ == MatchMode::BombDefusal && siteCount > 0) {
        int tIdx = 0, ctIdx = 0;
        for (size_t i = 0; i < bots_.size(); ++i) {            Player& b = bots_[i];
            if (b.team == Team::T) {
                // T：轮转分配包点，保证 A/B 都有人去
                const int site = tIdx % siteCount;
                ++tIdx;
                squad_[i].site = site;
                squad_[i].hasHold = true;
                squad_[i].holdX = sites[site].x + sites[site].width * 0.5f;
                squad_[i].holdY = sites[site].y + sites[site].height * 0.5f;
            } else {
                // CT：A / B / 中路 三线轮转
                const int slot = ctIdx % (siteCount + 1);
                ++ctIdx;
                if (slot < siteCount) {
                    squad_[i].site = slot;
                    squad_[i].hasHold = true;
                    squad_[i].holdX = sites[slot].x + sites[slot].width * 0.5f;
                    squad_[i].holdY = sites[slot].y + sites[slot].height * 0.5f;
                } else {
                    squad_[i].site = -1;             // 中路
                    squad_[i].hasHold = true;
                    squad_[i].holdX = midX;
                    squad_[i].holdY = midY;
                }
            }
        }
    }

    // 按阵营分别排序：离敌方质心越近 → 越靠前（Entry）
    for (int side = 0; side < 2; ++side) {
        const Team t = (side == 0) ? Team::T : Team::CT;

        std::vector<size_t> members;
        for (size_t i = 0; i < bots_.size(); ++i) {
            if (bots_[i].team == t) members.push_back(i);
        }
        if (members.empty()) continue;

        // 敌方质心
        float ex = 0.0f, ey = 0.0f;
        int   n  = 0;
        if (player_.alive && player_.team != t) { ex += player_.x; ey += player_.y; ++n; }
        for (const auto& b : bots_) {
            if (b.alive && b.team != t) { ex += b.x; ey += b.y; ++n; }
        }
        if (n > 0) { ex /= static_cast<float>(n); ey /= static_cast<float>(n); }
        else {
            ex = world_->GetMapPixelW() * 0.5f;
            ey = world_->GetMapPixelH() * 0.5f;
        }

        std::sort(members.begin(), members.end(), [&](size_t a, size_t b) {
            auto d2 = [&](size_t i) {
                const float dx = bots_[i].x - ex;
                const float dy = bots_[i].y - ey;
                return dx * dx + dy * dy;
            };
            return d2(a) < d2(b);
        });

        const size_t cnt = members.size();
        const size_t entryCount  = std::max<size_t>(1, (cnt + 2) / 3);
        const size_t anchorCount = std::max<size_t>(1, (cnt + 3) / 4);

        for (size_t k = 0; k < cnt; ++k) {
            BotRole role = BotRole::Support;
            if (k < entryCount)                        role = BotRole::Entry;
            else if (k >= cnt - anchorCount)           role = BotRole::Anchor;
            squad_[members[k]].role = role;
        }
    }
}

// 周期性选集火目标（含集火/掩护偏好），0.5s 一次
void CS2DGame::UpdateSquadFocus(float dt) {
    if (squad_.size() != bots_.size()) squad_.assign(bots_.size(), SquadOrder{});

    squadFocusTimer_ -= dt;
    if (squadFocusTimer_ > 0.0f) return;
    squadFocusTimer_ = 0.5f;

    for (size_t i = 0; i < bots_.size(); ++i) {
        const Player& bot = bots_[i];
        if (!bot.alive) { squad_[i].focus = -2; continue; }

        int   bestFocus = -2;
        float bestScore = 1e18f;

        // 只评估「团队情报」里的敌人（视野外靠队友共享，不再全图透视）
        for (const auto& c : ContactsOf(bot.team)) {
            bool aliveTarget = false;
            if (c.id == -1) {
                aliveTarget = player_.alive && player_.team != bot.team;
            } else if (c.id >= 0 && c.id < static_cast<int>(bots_.size())) {
                aliveTarget = bots_[c.id].alive && bots_[c.id].team != bot.team;
            }
            if (!aliveTarget) continue;

            const float dx = c.x - bot.x;
            const float dy = c.y - bot.y;
            float score = std::sqrt(dx * dx + dy * dy);

            // 集火偏好：情报点周围队友越多越优先
            int mates = 0;
            for (const auto& m : bots_) {
                if (&m == &bot || !m.alive || m.team != bot.team) continue;
                const float mx = m.x - c.x, my = m.y - c.y;
                if (mx * mx + my * my < 160.0f * 160.0f) ++mates;
            }
            score -= static_cast<float>(mates) * 45.0f;

            // 掩护偏好：正在换弹或低血的队友附近的敌人优先
            for (const auto& m : bots_) {
                if (&m == &bot || !m.alive || m.team != bot.team) continue;
                if (!(m.reloading || m.health < 40)) continue;
                const float mx = m.x - c.x, my = m.y - c.y;
                if (mx * mx + my * my < 120.0f * 120.0f) { score -= 60.0f; break; }
            }

            if (score < bestScore) {
                bestScore = score;
                bestFocus = c.id;
            }
        }

        squad_[i].focus = bestFocus;
    }
}

const CS2DGame::Player* CS2DGame::ResolveFocus(const SquadOrder& order) const {
    if (order.focus == -1) return player_.alive ? &player_ : nullptr;
    if (order.focus >= 0 && order.focus < static_cast<int>(bots_.size())) {
        const Player& b = bots_[order.focus];
        if (b.alive) return &b;
    }
    return nullptr;
}

// ============================================================================
// 局部避障（分离力）
// ============================================================================

namespace {
constexpr float SEP_RADIUS   = 26.0f;   // 排斥半径（约 4 倍命中盒）
constexpr float SEP_MAX_SPEED = 60.0f;  // 与 BOT 移动上限一致
}

void CS2DGame::AccumulateSeparation(const Player& self, float& outX, float& outY) const {
    outX = 0.0f;
    outY = 0.0f;

    auto consider = [&](const Player& other) {
        if (&other == &self || !other.alive) return;

        const float dx = self.x - other.x;
        const float dy = self.y - other.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= SEP_RADIUS * SEP_RADIUS) return;

        // 完全重叠时给一个确定性的偏移，避免除零与抖动
        if (d2 < 0.01f) {
            outX += (self.team == Team::T) ? 1.0f : -1.0f;
            outY += 1.0f;
            return;
        }

        const float d = std::sqrt(d2);
        // 线性衰减权重：越近越强
        const float w = 1.0f - d / SEP_RADIUS;
        outX += dx / d * w;
        outY += dy / d * w;
    };

    consider(player_);
    for (const auto& b : bots_) consider(b);

    // 归一化到 0..1，避免人多时力叠加爆表
    const float len = std::sqrt(outX * outX + outY * outY);
    if (len > 1.0f) {
        outX /= len;
        outY /= len;
    }
}

void CS2DGame::ApplyAvoidance(float& vx, float& vy, float sepX, float sepY) const {
    // 分离力强度：与当前速度同量级，保证能推开但不至于完全抵消前进
    constexpr float SEP_STRENGTH = 48.0f;

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

bool CS2DGame::FollowPathTo(Player& bot, BotRuntime& ai, float destX, float destY,
                            float dt, float speedMul) {
    if (!pathFinder_.IsBuilt()) return false;

    // 目标明显移动 / 定时 / 路径走完 → 重算
    const float gdx = destX - ai.goalX;
    const float gdy = destY - ai.goalY;
    const bool goalMoved = (gdx * gdx + gdy * gdy) > 70.0f * 70.0f;

    ai.repathTimer -= dt;
    const bool exhausted = (ai.pathIdx >= static_cast<int>(ai.path.size()));
    if (goalMoved || exhausted || ai.repathTimer <= 0.0f) {
        ai.goalX = destX;
        ai.goalY = destY;
        ai.repathTimer = 0.8f;
        ai.path.clear();
        ai.pathIdx = 0;
        if (!pathFinder_.FindPath(bot.x, bot.y, destX, destY, ai.path)) {
            ai.path.clear();
            return false;   // 无路可走 → 由调用方直线兜底
        }
    }

    if (ai.path.empty()) return false;

    // 跳过已到达的路点
    while (ai.pathIdx < static_cast<int>(ai.path.size())) {
        const auto& p = ai.path[ai.pathIdx];
        const float dx = p.first - bot.x;
        const float dy = p.second - bot.y;
        if (dx * dx + dy * dy <= 8.0f * 8.0f) { ++ai.pathIdx; continue; }
        break;
    }
    if (ai.pathIdx >= static_cast<int>(ai.path.size())) {
        ai.path.clear();
        return false;
    }

    const auto& p = ai.path[ai.pathIdx];
    const float dx = p.first - bot.x;
    const float dy = p.second - bot.y;
    const float d  = std::sqrt(dx * dx + dy * dy);
    if (d < 0.001f) return false;

    const float speed = 60.0f * std::clamp(speedMul, 0.3f, 1.0f);
    bot.vx = dx / d * speed;
    bot.vy = dy / d * speed;

    // 局部避障：防止多人挤在同一路线上互相卡住
    {
        float sepX = 0.0f, sepY = 0.0f;
        AccumulateSeparation(bot, sepX, sepY);
        ApplyAvoidance(bot.vx, bot.vy, sepX, sepY);
        // 前进分量被分离力淹没时，保底给一点朝向目标的推力
        const float dot = bot.vx * (dx / d) + bot.vy * (dy / d);
        if (dot < 12.0f) {
            bot.vx += dx / d * 20.0f;
            bot.vy += dy / d * 20.0f;
            const float s2 = std::sqrt(bot.vx * bot.vx + bot.vy * bot.vy);
            if (s2 > 60.0f) {
                bot.vx = bot.vx / s2 * 60.0f;
                bot.vy = bot.vy / s2 * 60.0f;
            }
        }
    }

    MoveWithCollision(bot, dt);
    bot.aimAngle = std::atan2(dy, dx);

    // 被墙卡住（速度被碰撞清零）→ 立刻失效当前路径，下一帧重算
    if (bot.vx == 0.0f && bot.vy == 0.0f) {
        ai.path.clear();
        ai.pathIdx = 0;
        ai.repathTimer = 0.0f;
    }
    return true;
}

void CS2DGame::UpdateBOTs(float dt, AudioManager* audio) {
    while (botExecutors_.size() < bots_.size()) botExecutors_.emplace_back();

    // 小队协同：刷新集火目标（内部 0.5s 节流）
    UpdateSquadFocus(dt);

    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& bot = bots_[i];
        if (!bot.alive) continue;
        auto& ai = AiAt(i);

        // 分工指令
        SquadOrder order;
        if (i < squad_.size()) order = squad_[i];

        // ---- 正在拆包：原地不动（进度由 UpdateObjective 推进）----
        if (ai.defusing) {
            bot.vx = bot.vy = 0.0f;
            ai.llmIntent.Tick(dt);
            continue;
        }

        // ---- 补枪（trade）：队友刚在附近阵亡 → 冲过去换人 ----
        if (lastDeathTimer_ > 0.0f && lastDeathTeam_ == bot.team &&
            !ai.tradePushed && order.role != BotRole::Anchor) {
            const float dx = lastDeathX_ - bot.x;
            const float dy = lastDeathY_ - bot.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > 40.0f * 40.0f && d2 < 350.0f * 350.0f) {
                ai.tradePushed = true;
                ai.llmIntent.Tick(dt);
                if (FollowPathTo(bot, ai, lastDeathX_, lastDeathY_, dt, 1.0f)) continue;
            }
        }

        // ---- 受击撤退：低血时优先找掩体，交火次之 ----
        if (ai.retreatTimer > 0.0f) ai.retreatTimer -= dt;
        if (ai.retreatTimer > 0.0f && bot.health <= 35) {
            float covX = 0.0f, covY = 0.0f;
            if (FindCoverPoint(bot, ai.threatX, ai.threatY, covX, covY)) {
                ai.llmIntent.Tick(dt);
                if (bot.ammo <= 0 && !bot.reloading) StartReload(bot);
                if (FollowPathTo(bot, ai, covX, covY, dt, 1.0f)) continue;
            }
        }

        // ---- 目标选择：只用「看得见」的敌人 + 团队情报 ----
        float targetDist = 0.0f;
        const Player* target = FindVisibleEnemy(bot, targetDist);
        const EnemyContact* intel = target ? nullptr
                                          : FindNearestContact(bot.team, bot.x, bot.y);

        // 集火：若集火目标当前可见，优先打它
        if (target) {
            if (const Player* focus = ResolveFocus(order)) {
                if (focus != target) {
                    const float fdx = focus->x - bot.x;
                    const float fdy = focus->y - bot.y;
                    const float fdist = std::sqrt(fdx * fdx + fdy * fdy);
                    if (fdist <= 340.0f) {
                        target = focus;
                        targetDist = fdist;
                    }
                }
            }
        }

        const bool canSee = (target != nullptr);

        // 交战目标位置（可见用真实位置；仅情报时用情报点）
        float aimX = 0.0f, aimY = 0.0f;
        float engageDist = 0.0f;
        if (target) {
            aimX = target->x;
            aimY = target->y;
            engageDist = targetDist;
        } else if (intel) {
            aimX = intel->x;
            aimY = intel->y;
            const float idx = aimX - bot.x;
            const float idy = aimY - bot.y;
            engageDist = std::sqrt(idx * idx + idy * idy);
        } else {
            engageDist = 1e9f;
        }

        ai.llmIntent.Tick(dt);

        // 闪光致盲倒计时
        if (ai.blindTimer > 0.0f) ai.blindTimer = std::max(0.0f, ai.blindTimer - dt);

        // ---- 换弹决策 ----
        // 脱战就补满；交火中只在弹匣见底（<=25%）才打断射击去换弹
        if (!bot.reloading && bot.ammo < bot.maxAmmo) {
            const int lowAmmo = bot.maxAmmo / 4;
            if (!canSee || bot.ammo <= lowAmmo) StartReload(bot);
        }

        // ---- 手雷：有情报但无直接视野时，向情报点投掷 ----
        if (ai.grenadeCooldown > 0.0f) ai.grenadeCooldown -= dt;
        if (!target && intel && bot.grenadeCount > 0 && ai.grenadeCooldown <= 0.0f) {
            if (engageDist > 90.0f && engageDist < 230.0f) {
                ThrowGrenade(bot, std::atan2(aimY - bot.y, aimX - bot.x));
                ai.grenadeCooldown = 7.0f;
            }
        }

        // ---- 目标引导：A* 寻路推进 ----
        // 角色行进速度：突破手全速、支援略慢、断后明显靠后
        const float roleMul = (order.role == BotRole::Entry)   ? 1.00f
                            : (order.role == BotRole::Anchor)  ? 0.55f
                            :                                    0.80f;
        {
            bool hasDest = false;
            bool destIsObjective = false;
            float destX = 0.0f, destY = 0.0f;

            if (mode_ == MatchMode::BombDefusal) {
                if (bot.team == Team::T) {
                    if (!bombPlanted_) {
                        // 按分队指令进攻指定包点（保证 A/B 都有人去）
                        if (order.hasHold) {
                            destX = order.holdX;
                            destY = order.holdY;
                            hasDest = true;
                            destIsObjective = true;
                        } else {
                            const auto& sites = world_->GetBombSites();
                            float bestD2 = 1e18f;
                            for (const auto& s : sites) {
                                const float cx = s.x + s.width * 0.5f;
                                const float cy = s.y + s.height * 0.5f;
                                const float dx = cx - bot.x, dy = cy - bot.y;
                                const float d2 = dx * dx + dy * dy;
                                if (d2 < bestD2) { bestD2 = d2; destX = cx; destY = cy; hasDest = true; }
                            }
                            destIsObjective = hasDest;
                        }
                    } else {
                        // 已下包：回到包点附近守包
                        destX = bombX_;
                        destY = bombY_;
                        hasDest = true;
                        destIsObjective = true;
                    }
                } else if (bombPlanted_) {
                    destX = bombX_; destY = bombY_;      // 前往拆包
                    hasDest = true;
                    destIsObjective = true;
                } else if (order.hasHold) {
                    // 未下包：各守 A / B / 中路
                    destX = order.holdX;
                    destY = order.holdY;
                    hasDest = true;
                    destIsObjective = true;
                }
            } else if (mode_ == MatchMode::HostageRescue && bot.team == Team::CT) {
                // 最近的未救出人质
                float bestD2 = 1e18f;
                for (const auto& h : hostages_) {
                    if (h.rescued) continue;
                    const float dx = h.x - bot.x, dy = h.y - bot.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) { bestD2 = d2; destX = h.x; destY = h.y; hasDest = true; }
                }
                destIsObjective = hasDest;
            }

            // 无任务目标时：优先去核实团队情报点，其次才追可见敌人
            if (!hasDest && intel) {
                destX = intel->x;
                destY = intel->y;
                hasDest = true;
            }
            if (!hasDest && target) {
                destX = target->x;
                destY = target->y;
                hasDest = true;
            }

            // 巡逻兜底：无任务、无情报、无可见敌人时游走，避免原地发呆
            // 突破/支援位朝「敌方出生点质心」推进（与地图朝向无关），断后位就近游走
            if (!hasDest) {
                // 爆破图有固定防守/进攻位：在其附近小范围活动，避免跑离岗位
                const bool holdPost = (mode_ == MatchMode::BombDefusal && order.hasHold);
                const bool pushToEnemy = (order.role != BotRole::Anchor);
                float ecx = 0.0f, ecy = 0.0f;
                const bool hasEnemyCentroid =
                    !holdPost && pushToEnemy && EnemySpawnCentroid(bot.team, ecx, ecy);

                for (int t = 0; t < 12; ++t) {
                    float rx = 0.0f, ry = 0.0f;
                    if (holdPost) {
                        rx = order.holdX + static_cast<float>(rng_() % 181) - 90.0f;
                        ry = order.holdY + static_cast<float>(rng_() % 181) - 90.0f;
                    } else if (hasEnemyCentroid) {
                        rx = ecx + static_cast<float>(rng_() % 521) - 260.0f;
                        ry = ecy + static_cast<float>(rng_() % 521) - 260.0f;
                    } else {
                        rx = bot.x + static_cast<float>(rng_() % 601) - 300.0f;
                        ry = bot.y + static_cast<float>(rng_() % 601) - 300.0f;
                    }
                    if (pathFinder_.IsWalkableWorld(rx, ry)) {
                        destX = rx;
                        destY = ry;
                        hasDest = true;
                        break;
                    }
                }
            }

            // 断后位不主动远距离追击：没有任务目标时守在原地
            if (!destIsObjective && order.role == BotRole::Anchor && engageDist > 260.0f) {
                hasDest = false;
            }

            if (hasDest) {
                // 近距离且可见 → 交给战术执行器（冲刺/绕侧手感更好）
                const bool closeCombat = canSee && engageDist < 180.0f;
                if (!closeCombat && FollowPathTo(bot, ai, destX, destY, dt, roleMul)) {
                    continue;   // 本帧移动已由寻路接管
                }
            }

            // 没有可见敌人 → 本帧不开火，移动已处理完
            if (!target) {
                bot.vx = bot.vy = 0.0f;
                continue;
            }
        }

        BehaviorOutput out = botExecutors_[i].Execute(
            ai.llmIntent,
            bot.x, bot.y,
            aimX, aimY,
            canSee, engageDist,
            bot.ammo > 0, bot.reloading, bot.weaponSlot,
            rng_, dt);

        bot.vx = out.vx * roleMul;
        bot.vy = out.vy * roleMul;

        // 局部避障（战斗中同样避免叠人）
        {
            float sepX = 0.0f, sepY = 0.0f;
            AccumulateSeparation(bot, sepX, sepY);
            ApplyAvoidance(bot.vx, bot.vy, sepX, sepY);
        }

        MoveWithCollision(bot, dt);

        if (canSee) bot.aimAngle = out.aimAngle;

        const WeaponDef& def = bot.CurrentWeapon();

        // 枪械 / 霰弹（必须真实可见才开火，换弹中不开火）
        if (target && out.shouldShoot && canSee && !bot.reloading &&
            def.cls != WeaponClass::Melee && def.cls != WeaponClass::Grenade) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;

            if (bot.shootCooldown <= 0.0f) {
                if (engageDist < def.range + 200.0f) {
                    if (bot.ammo <= 0) { StartReload(bot); continue; }

                    // 命中倾向：30% 精确（小抖动），70% 打偏（大散布）
                    std::uniform_real_distribution<float> hitRoll(0.0f, 1.0f);
                    const bool precise = hitRoll(rng_) < 0.30f;
                    const float base = precise ? 0.08f : 0.8f;
                    std::uniform_real_distribution<float> spreadDist(
                        -base * out.spreadMul, base * out.spreadMul);

                    const float aimBase = std::atan2(target->y - bot.y, target->x - bot.x);
                    const int pellets = std::max(1, def.pellets);
                    for (int p = 0; p < pellets; ++p) {
                        FireBullet(bot.x, bot.y, aimBase + spreadDist(rng_), def,
                                   static_cast<int>(i), bot.team);
                    }
                    bot.ammo--;
                    bot.shootCooldown = def.cooldown * 1.8f * out.shootCooldownMul;
                    bot.shootAnimTimer = 0.12f;

                    if (audio) {
                        audio->PlaySoundAt(FireSoundFor(def.id), bot.x, bot.y,
                                           player_.x, player_.y);
                    }
                }
            }
        }
        // 近战
        else if (target && def.cls == WeaponClass::Melee && canSee &&
                 engageDist < 20.0f && target->alive) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;
            if (bot.shootCooldown <= 0.0f) {
                if (target == &player_) {
                    if (ApplyDamage(player_, static_cast<int>(def.damage * 0.33f))) {
                        OnCombatantKilled(player_, audio);
                    }
                }
                bot.shootCooldown = def.cooldown * 1.5f * out.shootCooldownMul;
                bot.shootAnimTimer = 0.12f;
            }
        }
    }

    // 死斗模式：BOT 阵亡后自动重生；团队模式由回合开始统一重生
    if (mode_ == MatchMode::DeathMatch) {
        for (size_t i = 0; i < bots_.size(); ++i) {
            auto& bot = bots_[i];
            if (!bot.alive) {
                ResetBotAi(i);
                RespawnBOT(bot);
            }
        }
    }
}

