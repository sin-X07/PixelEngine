#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// BOT 运行时状态 / 团队情报 / 小队决策 / 局部避障
// 回答「这支队伍该怎么打」
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
    const float probe = 14.0f;

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

void CS2DGame::ReportContact(Team team, int id, float x, float y) {
    if (team == Team::None) return;

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
            // 上报同样受朝向限制：背后看不到的东西不能报告
            if (!InViewCone(b.x, b.y, b.aimAngle, e.x, e.y, BOT_SPOT_RADIUS)) return;
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

    // 玩家自己的视野同样接入情报网：他看到的人立刻共享给所有队友（雷达/集火都会用）
    if (player_.alive && flashTimer_ <= 0.0f) {
        for (size_t j = 0; j < bots_.size(); ++j) {
            const Player& e = bots_[j];
            if (!e.alive || e.team == player_.team) continue;
            if (!PlayerCanSeeBot(e)) continue;      // 距离 + 视野扇形 + 烟雾 + LOS
            ReportContact(player_.team, static_cast<int>(j), e.x, e.y);
        }
    }
}

// 真实可见敌人（交火距离 + 烟雾 + LOS）

// 目标是否在以 from 为顶点、aim 为中轴的视野扇形内（含角度软边的有效边界）
bool CS2DGame::InViewCone(float fromX, float fromY, float aim,
                          float toX, float toY, float maxDist) const {
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    const float d2 = dx * dx + dy * dy;
    if (d2 > maxDist * maxDist) return false;
    if (d2 < 0.0001f) return true;

    float diff = std::atan2(dy, dx) - aim;
    while (diff >  3.14159265f) diff -= 6.28318531f;
    while (diff < -3.14159265f) diff += 6.28318531f;

    return std::abs(diff) <= Tuning::VisionHalfAngleRad;
}

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
        // 视野扇形：和玩家同一套几何，背后看不见
        if (!InViewCone(self.x, self.y, self.aimAngle, e.x, e.y, BOT_VIEW_RADIUS)) return;
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
            sx += s.cx;
            sy += s.cy;
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
                squad_[i].holdX = sites[site].cx;
                squad_[i].holdY = sites[site].cy;
            } else {
                // CT：A / B / 中路 三线轮转
                const int slot = ctIdx % (siteCount + 1);
                ++ctIdx;
                if (slot < siteCount) {
                    squad_[i].site = slot;
                    squad_[i].hasHold = true;
                    squad_[i].holdX = sites[slot].cx;
                    squad_[i].holdY = sites[slot].cy;
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
    {
        auto isSide = [&](int idx, const char* side) {
            return idx >= 0 && idx < siteCount && sites[idx].name == side;
        };
        int tA = 0, tB = 0, ctA = 0, ctB = 0, ctMid = 0;
        for (size_t i = 0; i < bots_.size() && i < squad_.size(); ++i) {
            if (!bots_[i].alive || !squad_[i].hasHold) continue;
            if (bots_[i].team == Team::T) {
                if (isSide(squad_[i].site, "A")) ++tA;
                else if (isSide(squad_[i].site, "B")) ++tB;
            } else {
                if (isSide(squad_[i].site, "A")) ++ctA;
                else if (isSide(squad_[i].site, "B")) ++ctB;
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
    squadFocusTimer_ = Tuning::FocusUpdateInterval;

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

            // 守包优先：已下包时，靠近包点的敌人（很可能在拆包）最优先
            if (mode_ == MatchMode::BombDefusal && bombPlanted_ && bot.team == Team::T) {
                const float bdx = c.x - bombX_;
                const float bdy = c.y - bombY_;
                if (bdx * bdx + bdy * bdy < 140.0f * 140.0f) score -= 150.0f;
            }

            // 集火偏好：情报点周围队友越多越优先
            int mates = 0;
            for (const auto& m : bots_) {
                if (&m == &bot || !m.alive || m.team != bot.team) continue;
                const float mx = m.x - c.x, my = m.y - c.y;
                if (mx * mx + my * my < Tuning::FocusMateRadius * Tuning::FocusMateRadius) ++mates;
            }
            score -= static_cast<float>(mates) * Tuning::FocusMateWeight;

            // 掩护偏好：正在换弹或低血的队友附近的敌人优先
            for (const auto& m : bots_) {
                if (&m == &bot || !m.alive || m.team != bot.team) continue;
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
constexpr float SEP_RADIUS    = Tuning::SepRadius;    // 排斥半径
constexpr float SEP_MAX_SPEED = Tuning::SepMaxSpeed;  // 与 BOT 移动上限一致
}

void CS2DGame::AccumulateSeparation(const Player& self, float& outX, float& outY) const {
    outX = 0.0f;
    outY = 0.0f;

    auto consider = [&](const Player& other) {
        if (&other == &self || !other.alive) return;

        const float dx = self.x - other.x;
        const float dy = self.y - other.y;
        const float d2 = dx * dx + dy * dy;
        // 死区：只有真的靠近（< 55% 半径）才推。原来 26px 就开始推，
        // 驻守时队友稍微靠近就互相顶，表现为原地抖动。
        constexpr float kSepDeadZone = SEP_RADIUS * 0.55f;
        if (d2 >= kSepDeadZone * kSepDeadZone) return;

        // 完全重叠时给一个确定性的偏移，避免除零与抖动
        if (d2 < 0.01f) {
            outX += (self.team == Team::T) ? 1.0f : -1.0f;
            outY += 1.0f;
            return;
        }

        const float d = std::sqrt(d2);
        // 线性衰减权重：越近越强
        // 平方衰减：死区边界处从 0 平滑增长，避免推力突变造成抽搐
        const float t = 1.0f - d / kSepDeadZone;
        const float w = t * t;
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

