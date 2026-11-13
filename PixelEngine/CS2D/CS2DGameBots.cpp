#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// BOT 单体执行：视野判定、路径跟随、每帧决策与开火
// 回答「这个 BOT 这一帧做什么」
// ============================================================================

// ---------------------------------------------------------------------------
// 目标唯一写入入口
//
// 背景：教练计划写 ai.planHold*，小队轮转会写 squad_[i].hold*，两边各写各的，
// 导致同一 BOT 同时存在两个相距很远的"驻守点"（dual_goal 冲突），
// 决策树只能在两套字段间隐式挑一个，行为随刷新时序抖动。
// 现在：所有写入都走这里，来源优先级 Coach > Squad，低优先级不覆盖高优先级，
// 并把 squad_ 镜像字段一起更新，保证只存在一个真源。
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 决策 trace：只在「决策发生变化」时写一行，因此体积小、可读性强。
// 记录内容：目标 / 姿态 / 机动 / 驻守点（含来源）/ 拆包状态。
// 关闭时（decisionTrace_ == nullptr）整个函数只有一次判空。
// ---------------------------------------------------------------------------
void CS2DGame::EnableDecisionTrace(const std::string& path) {
    CloseDecisionTrace();
    decisionTrace_ = std::fopen(path.c_str(), "wb");
    if (!decisionTrace_) {
        std::cerr << "[Trace] 无法写入: " << path << std::endl;
        return;
    }
    std::fprintf(decisionTrace_,
                 "tick,round,bot,team,kind,key,area,x,y\n");
    std::cout << "[Trace] 决策 trace 已开启: " << path << std::endl;
}

void CS2DGame::CloseDecisionTrace() {
    if (!decisionTrace_) return;
    std::fclose(decisionTrace_);
    decisionTrace_ = nullptr;
}

void CS2DGame::TraceBotDecision(size_t i, const Player* target) {
    if (!decisionTrace_ || i >= bots_.size()) return;
    auto& ai = AiAt(i);
    const Player& b = bots_[i];

    int targetIdx = -2;
    if (target) {
        if (target == &player_) targetIdx = -1;
        else {
            for (size_t j = 0; j < bots_.size(); ++j) {
                if (&bots_[j] == target) { targetIdx = static_cast<int>(j); break; }
            }
        }
    }
    const int stance   = static_cast<int>(ai.stance);
    const int maneuver = static_cast<int>(ai.maneuver);
    const int goalSrc  = static_cast<int>(ai.goalSource);
    const int team     = static_cast<int>(b.team);

    auto emit = [&](const char* kind, const std::string& key) {
        std::fprintf(decisionTrace_, "%llu,%d,%zu,%d,%s,%s,%s,%.1f,%.1f\n",
                     static_cast<unsigned long long>(traceTick_), GetRoundNumber(),
                     i, team, kind, key.c_str(), ai.goalArea.c_str(),
                     ai.planHoldX, ai.planHoldY);
    };

    if (targetIdx != ai.traceTarget) {
        emit("target", targetIdx < 0 ? "none" : std::to_string(targetIdx));
        ai.traceTarget = targetIdx;
    }
    if (stance != ai.traceStance) {
        emit("stance", std::to_string(stance));
        ai.traceStance = stance;
    }
    if (maneuver != ai.traceManeuver) {
        emit("maneuver", std::to_string(maneuver));
        ai.traceManeuver = maneuver;
    }
    const bool goalChanged =
        goalSrc != ai.traceGoalSrc ||
        std::fabs(ai.planHoldX - ai.traceGoalX) > 1.0f ||
        std::fabs(ai.planHoldY - ai.traceGoalY) > 1.0f;
    if (goalChanged) {
        emit("goal", ai.hasPlanHold ? std::to_string(goalSrc) : "none");
        ai.traceGoalSrc = goalSrc;
        ai.traceGoalX   = ai.planHoldX;
        ai.traceGoalY   = ai.planHoldY;
    }
    if (ai.defusing != ai.traceDefusing) {
        emit("defusing", ai.defusing ? "1" : "0");
        ai.traceDefusing = ai.defusing;
    }
}
// ---------------------------------------------------------------------------
// 投掷前校验：2D 模拟里手雷会撞墙反弹，所以必须确认"到落点有视线 + 落点可站"。
// 失败就左右偏转几个角度重试；全都不可行时返回 false（宁可不扔，也别扔墙上弹回来）。
// ---------------------------------------------------------------------------
bool CS2DGame::PlanGrenadeThrow(const Player& bot, float tx, float ty, float& outAngle) const {
    if (!world_) return false;
    const float kNominalDist = std::clamp(std::hypot(tx - bot.x, ty - bot.y), Tuning::NadeThrowSpeedMin / Tuning::NadeFriction, Tuning::NadeThrowSpeedMax / Tuning::NadeFriction);
    const float baseAng = std::atan2(ty - bot.y, tx - bot.x);
    const float half = bot.size * 0.5f;
    const float mapW = world_->GetMapPixelW();
    const float mapH = world_->GetMapPixelH();

    for (int i = 0; i < Tuning::ai::NadeValidTries; ++i) {
        float offDeg = 0.0f;
        if (i > 0) {
            const int step = (i + 1) / 2;
            const float sign = (i % 2) ? 1.0f : -1.0f;
            offDeg = sign * step * Tuning::ai::NadeValidAngleStep;
        }
        const float ang = baseAng + offDeg * 3.14159265f / 180.0f;
        const float lx = bot.x + std::cos(ang) * kNominalDist;
        const float ly = bot.y + std::sin(ang) * kNominalDist;

        if (lx < 1.0f || ly < 1.0f || lx > mapW - 1.0f || ly > mapH - 1.0f) continue;
        if (world_->CheckRectCollision(lx - half, ly - half, bot.size, bot.size)) continue;
        if (!visibility_.HasLineOfSight(bot.x, bot.y, lx, ly)) continue;

        outAngle = ang;
        return true;
    }
    return false;
}
void CS2DGame::SetBotGoal(size_t i, float x, float y,
                          BotRuntime::GoalSource src, bool force) {
    if (i >= bots_.size()) return;
    auto& ai = AiAt(i);

    // T 进攻期（未下包）允许小队轮转覆盖教练的"区域站位点"：
    // 教练给的往往是集结/架枪区域，真正往里压的包点由小队轮转持续校正。
    // CT 与下包后的守包阶段则以教练计划为准，不被轮转拉回。
    const bool tAttacking = (bots_[i].team == Team::T && !bombPlanted_);

    if (!force && ai.hasPlanHold && !tAttacking &&
        static_cast<int>(src) < static_cast<int>(ai.goalSource)) {
        return;   // 教练已指定驻守点，低优先级来源不得覆盖
    }
    ai.hasPlanHold = true;
    ai.goalSource  = src;
    ai.planHoldX   = x;
    ai.planHoldY   = y;

    // 守点朝向：战术表 [look.<area>] 优先，否则朝敌方出生点质心（避免面壁）
    ai.lookAngle = -99.0f;
    if (!ai.goalArea.empty()) {
        if (const auto* dirs = tactics_.Look(ai.goalArea); dirs && !dirs->empty()) {
            ai.lookAngle = (*dirs)[0];
        }
    }
    if (ai.lookAngle < -90.0f) {
        float ex = 0.0f, ey = 0.0f;
        if (EnemySpawnCentroid(bots_[i].team, ex, ey)) {
            ai.lookAngle = std::atan2(ey - bots_[i].y, ex - bots_[i].x);
        }
    }

    if (i < squad_.size()) {
        squad_[i].hasHold = true;
        squad_[i].holdX   = x;
        squad_[i].holdY   = y;
    }
}

void CS2DGame::ClearBotGoal(size_t i) {
    if (i >= bots_.size()) return;
    auto& ai = AiAt(i);
    ai.hasPlanHold = false;
    ai.goalSource  = BotRuntime::GoalSource::None;
    ai.goalArea.clear();
    if (i < squad_.size()) squad_[i].hasHold = false;
}
bool CS2DGame::BotCanSeePlayer(const Player& bot) const {
    if (!player_.alive || !IsHostile(player_, bot)) return false;   // 死斗 FFA：所有人都敌对
    const float dx = player_.x - bot.x;
    const float dy = player_.y - bot.y;
    const auto& p = tactics_.perception[std::clamp(AiAt(AiIndexOf(bot)).difficulty, 0, 3)];
    if (!InViewCone(bot.x, bot.y, bot.EffectiveAim(), player_.x, player_.y, BOT_VIEW_RADIUS * p.radius, p.angle)) return false;
    if (!visibility_.World()) return true;   // 无世界数据时保持宽松语义（测试/初始化阶段）
    return visibility_.HasLineOfSight(bot.x, bot.y, player_.x, player_.y);
}

bool CS2DGame::PlayerCanSeeBot(const Player& bot) const {
    if (!bot.alive) return false;
    // 烟雾遮挡
    if (SegmentBlockedBySmoke(player_.x, player_.y, bot.x, bot.y)) return false;
    if (!visibility_.World()) return true;   // 无世界数据时保持宽松语义（测试/初始化阶段）
    return visibility_.IsVisibleToPlayer(player_.x, player_.y, player_.EffectiveAim(),
                                        bot.x, bot.y);
}

// 拆包安全判定：CT BOT 只有在「没有可见敌人、最近没挨打」时才拆包；
// 炸弹最后 6 秒进入强制拆包窗口，必须顶着火力尝试。
bool CS2DGame::BotCanDefuse(size_t i, const Player& bot) const {
    if (mode_ != MatchMode::BombDefusal || !bombPlanted_) return false;
    if (!bot.alive || bot.team != Team::CT) return false;

    const float dx = bot.x - bombX_;
    const float dy = bot.y - bombY_;
    if (dx * dx + dy * dy > Tuning::DefuseRadius * Tuning::DefuseRadius) return false;
    if (bombTimer_ <= 0.0f) return false;

    // 时间不够时没有"先反击"的余裕：必须拆。
    if (bombTimer_ <= 6.0f) return true;

    const BotRuntime& ai = AiAt(i);
    if (ai.defuseCooldown > 0.0f) return false;  // 刚放弃拆包，先处理威胁
    if (ai.lastHitTimer > 0.0f) return false;    // 正在挨打：先反击/找掩体

    float enemyDist = 0.0f;
    if (FindVisibleEnemy(bot, enemyDist)) return false;   // 看得见敌人：先打

    // 不再因为「团队情报里包点附近有 T」就直接禁止拆包：
    // 那会让 CT 全部站在炸弹上等到最后 6 秒。现在由拆包手尝试，
    // 其他 CT 去清情报点/架枪；拆包手被看见或挨打时会自动放弃。
    return true;
}

// 下包后指定拆包手：优先选「当前没挨打、没在冷却、没看见敌人」的最近 CT；
// 最后 6 秒退化为最近的存活 CT（必须强拆）。
int CS2DGame::PickDefuseBot() const {
    if (mode_ != MatchMode::BombDefusal || !bombPlanted_) return -2;

    const bool mustDefuse = (bombTimer_ <= 6.0f);
    int   best        = -2;
    int   fallback    = -2;
    float bestD2      = 1e36f;
    float fallbackD2  = 1e36f;

    for (size_t i = 0; i < bots_.size(); ++i) {
        const Player& b = bots_[i];
        if (!b.alive || b.team != Team::CT) continue;

        const float dx = b.x - bombX_;
        const float dy = b.y - bombY_;
        const float d2 = dx * dx + dy * dy;
        if (d2 < fallbackD2) { fallbackD2 = d2; fallback = static_cast<int>(i); }

        if (!mustDefuse) {
            const BotRuntime& ai = AiAt(i);
            if (ai.lastHitTimer > 0.0f || ai.defuseCooldown > 0.0f) continue;

            float enemyDist = 0.0f;
            if (FindVisibleEnemy(b, enemyDist)) continue;
        }

        if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(i); }
    }

    if (best >= 0) return best;
    return mustDefuse ? fallback : -2;
}

// 找最近的存活敌人（玩家或其它 BOT）
// 已废弃：BOT 不再全图感知，改用 FindVisibleEnemy + 团队情报。

// ============================================================================
// 受击撤退：找掩体
// ============================================================================

bool CS2DGame::FollowPathTo(Player& bot, BotRuntime& ai, float destX, float destY,
                            float dt, float speedMul) {
    if (!pathFinder_.IsBuilt()) return false;

    // 到达死区：距目标很近时直接停下，别再为几个像素反复微调
    // （驻守点位时队友的分离力会把人推开，下一帧又走回来 → 视觉上一直抖）
    {
        const float adx = destX - bot.x;
        const float ady = destY - bot.y;
        constexpr float kArriveR = 14.0f;
        if (adx * adx + ady * ady < kArriveR * kArriveR) {
            ai.goalX = destX;
            ai.goalY = destY;
            ai.path.clear();
            ai.pathIdx = 0;
            bot.vx = 0.0f;
            bot.vy = 0.0f;
            // false = arrived / not pathing; callers must not continue forever
            return false;
        }
    }

    // 目标明显移动 / 定时 / 路径走完 → 重算
    const float gdx = destX - ai.goalX;
    const float gdy = destY - ai.goalY;
    const bool goalMoved = (gdx * gdx + gdy * gdy) >
                           Tuning::RepathGoalMoved * Tuning::RepathGoalMoved;

    ai.repathTimer -= dt;
    const bool exhausted = (ai.pathIdx >= static_cast<int>(ai.path.size()));
    if (goalMoved || exhausted || ai.repathTimer <= 0.0f) {
        ai.goalX = destX;
        ai.goalY = destY;
        ai.repathTimer = Tuning::RepathInterval;
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
        if (dx * dx + dy * dy <= Tuning::WaypointReach * Tuning::WaypointReach) { ++ai.pathIdx; continue; }
        break;
    }
    if (ai.pathIdx >= static_cast<int>(ai.path.size())) {
        ai.path.clear();
        ai.pathIdx = 0;
        return false;
    }

    const auto& p = ai.path[ai.pathIdx];
    const float dx = p.first - bot.x;
    const float dy = p.second - bot.y;
    const float d  = std::sqrt(dx * dx + dy * dy);
    if (d < 0.001f) return false;

    const float weaponMul = bot.CurrentWeapon().moveSpeedMul;
    // 教练节奏：fast 略快、slow 略慢（只影响推进欲望，不影响武器惩罚）
    const Tempo tempo = GetCoachPlan(bot.team).tempo;
    const float tempoMul = (tempo == Tempo::Fast) ? 1.08f
                         : (tempo == Tempo::Slow) ? 0.92f : 1.0f;
    const float speed = Tuning::BotMoveSpeed * weaponMul * tempoMul
                      * std::clamp(speedMul, Tuning::SpeedMulMin, 1.0f);
    const float moveSpeed = bot.stealth ? std::min(speed, Tuning::BotStealthSpeed) : speed;
    // 绕行状态：卡住后沿侧向方向走一小段时间（不再瞬移）
    if (ai.detourTimer > 0.0f) {
        ai.detourTimer -= dt;
        bot.vx = ai.detourX * moveSpeed;
        bot.vy = ai.detourY * moveSpeed;
        MoveWithCollision(bot, dt);
        return true;
    }

    // ---- 角点转向：直行被墙挡住时，依次尝试左右偏转 ----
    // 原来只会朝路点直线走，撞墙就把速度清零（然后 0.7s 后瞬移），
    // 在拐角处表现为「卡住 → 向后瞬移 → 再卡住」的死循环。
    float dirX = dx / d, dirY = dy / d;
    {
        const float half = bot.size * 0.5f;
        const float probe = std::max(8.0f, moveSpeed * dt * 2.0f);
        static const float kOffsets[7] = {
            0.0f,
             0.5235988f, -0.5235988f,   // ±30°
             1.0471976f, -1.0471976f,   // ±60°
             1.5707963f, -1.5707963f    // ±90°
        };
        for (float off : kOffsets) {
            const float ang = std::atan2(dirY, dirX) + off;
            const float px = bot.x + std::cos(ang) * probe;
            const float py = bot.y + std::sin(ang) * probe;
            if (!world_->CheckRectCollision(px - half, py - half, bot.size, bot.size)) {
                dirX = std::cos(ang);
                dirY = std::sin(ang);
                break;
            }
        }
    }

    bot.vx = dirX * moveSpeed;
    bot.vy = dirY * moveSpeed;

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
            if (s2 > moveSpeed) {
                bot.vx = bot.vx / s2 * moveSpeed;
                bot.vy = bot.vy / s2 * moveSpeed;
            }
        }
    }

    MoveWithCollision(bot, dt);

    // 朝向：只在真正移动时朝「速度方向」，并限制转向速率。
    // 之前朝航点算，航点落在身后时会让 BOT 一直转圈找方向。
    const float speed2 = bot.vx * bot.vx + bot.vy * bot.vy;
    if (speed2 > 25.0f) {
        const float moveAng = std::atan2(bot.vy, bot.vx);
        float desired = moveAng
                      + std::sin(ai.scanPhase * 2.2f) * Tuning::BotScanAmplitudeRad;
        // 听到敌对声音、且声源大致在前进方向上：边推进边把枪口偏向声源
        if (ai.alertTimer > 0.0f) {
            const float want = std::atan2(ai.alertY - bot.y, ai.alertX - bot.x);
            float dd = want - moveAng;
            while (dd >  3.14159265f) dd -= 6.28318531f;
            while (dd < -3.14159265f) dd += 6.28318531f;
            if (std::abs(dd) < 1.9f) {   // 前方 ±109° 内才偏头，避免倒着走
                desired = want + std::sin(ai.scanPhase * 2.2f)
                                * Tuning::BotScanAmplitudeRad * 0.35f;
            }
        }
        float delta = desired - bot.aimAngle;
        while (delta >  3.14159265f) delta -= 6.28318531f;
        while (delta < -3.14159265f) delta += 6.28318531f;
        const float maxTurn = Tuning::BotMaxTurnRate * dt;
        bot.aimAngle += std::clamp(delta, -maxTurn, maxTurn);
    }
    // 被墙卡住（速度被碰撞清零）→ 立刻失效当前路径，下一帧重算
    // 撞墙不再清空路径：由角点转向与绕行处理，避免每帧重算路径

    // 卡墙脱困：只在「本该沿路径移动」的帧判定，避免站桩/拆包/守点被误挪
    {
        const float dxm = bot.x - ai.lastX;
        const float dym = bot.y - ai.lastY;
        if (dxm * dxm + dym * dym > 1.0f) {
            ai.lastX = bot.x;
            ai.lastY = bot.y;
            ai.stuckTimer = 0.0f;
        } else {
            ai.stuckTimer += dt;
            if (ai.stuckTimer > 0.55f) {
                // 只设置一个侧向绕行方向，绝不瞬移
                ai.stuckTimer = 0.0f;
                if (NudgeOutOfStuck(bot, ai)) {
                    ai.path.clear();
                    ai.pathIdx = 0;
                    ai.repathTimer = 0.0f;
                }
            }
        }
    }
    return true;
}

void CS2DGame::UpdateBOTs(float dt, AudioManager* audio) {
    // 小队协同：刷新集火目标（内部 0.5s 节流）
    UpdateSquadFocus(dt);

    // 角色动态重算：只有存活人数变化才重算（内部带 8s 换人冷却，避免来回改速度）
    roleRefreshTimer_ -= dt;
    if (roleRefreshTimer_ <= 0.0f) {
        roleRefreshTimer_ = Tuning::ai::RoleRefreshInterval;
        int alive = 0;
        for (const auto& b : bots_) if (b.alive) ++alive;
        if (alive != roleAliveSnapshot_) {
            roleAliveSnapshot_ = alive;
            // 只补"断档"的角色（不整队重排）：整队重排会让角色每 2 秒洗牌，
            // 速度倍率跟着变，表现就是来回跑。
            if (AliveCount(Team::T) >= 2)  RefreshRolesForTeam(Team::T);
            if (AliveCount(Team::CT) >= 2) RefreshRolesForTeam(Team::CT);
        }
    }

    // ---- C4 掉落：预先算出「离掉落点最近的 N 个 T BOT」作为回收者 ----
    // 每帧重算：回收者阵亡 / 有人更近时，指派会自然转移。
    // 其余 T 继续原进攻任务，避免全军挤在掉包点暴露。
    std::vector<int> recoverers;
    if (mode_ == MatchMode::BombDefusal && !bombPlanted_ && bombDropped_) {
        std::vector<std::pair<float, int>> cands;
        cands.reserve(bots_.size());
        for (size_t k = 0; k < bots_.size(); ++k) {
            if (!bots_[k].alive || bots_[k].team != Team::T) continue;
            const float dx = bots_[k].x - bombDropX_;
            const float dy = bots_[k].y - bombDropY_;
            cands.emplace_back(dx * dx + dy * dy, static_cast<int>(k));
        }
        std::sort(cands.begin(), cands.end());
        const int n = std::min(static_cast<int>(cands.size()),
                               Tuning::BombRecoverCount);
        recoverers.reserve(n);
        for (int r = 0; r < n; ++r) recoverers.push_back(cands[r].second);
    }

    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& bot = bots_[i];
        if (!bot.alive) continue;
        // 进入本帧前先把上一帧的实时弹药落到槽位缓存：
        // BOT 阵亡掉落 / 切换武器读取的都是 ammoSlot，必须保持最新。
        bot.SaveAmmoToSlot();
        auto& ai = AiAt(i);
        const auto& perception = tactics_.perception[std::clamp(ai.difficulty, 0, 3)];
        bot.recoilRecoveryTimer = std::max(0.0f, bot.recoilRecoveryTimer - dt);
        if (bot.recoilRecoveryTimer <= 0.0f)
            bot.aimKick *= std::exp(-bot.CurrentWeapon().recoilRecoverPerSec * Tuning::BotFovRadiusMul[std::clamp(ai.difficulty, 0, 3)] * dt);
        ai.settleTimer += dt;
        for (auto& alert : ai.alerts)
            if (alert.enemyId != -2 && clockSec_ > alert.lastPerceivedAt + Tuning::AlertMemory) alert = {};
        if (ai.Ready(-4, clockSec_)) {
            FollowPathTo(bot, ai, ai.dodgeX, ai.dodgeY, dt);
            for (auto& alert : ai.alerts) if (alert.enemyId == -4) alert = {};
            continue;
        }
        const bool moving = std::hypot(bot.vx, bot.vy) > 1.0f;
        if (ai.wasMoving && !moving) ai.settleTimer = 0.0f;
        ai.wasMoving = moving;
        AgeHeardSounds(ai, dt);
        if (ai.blindTimer > 0.0f) ai.blindTimer = std::max(0.0f, ai.blindTimer - dt);
        if (ai.lastHitTimer > 0.0f) ai.lastHitTimer = std::max(0.0f, ai.lastHitTimer - dt);
        if (ai.defuseCooldown > 0.0f) ai.defuseCooldown = std::max(0.0f, ai.defuseCooldown - dt);
        if (ai.heardLogCooldown > 0.0f) ai.heardLogCooldown -= dt;
        ai.scanPhase += dt;
        if (ai.wanderTimer > 0.0f) ai.wanderTimer -= dt;

        // ---- 计划卡里的投掷意图：到位过程中朝目标区域投一颗 ----
        if (ai.utilKind > 0 && !ai.utilDone) {
            if (bot.grenadeCount <= 0) {
                ai.utilDone = true;
            } else {
                const float dx = ai.utilX - bot.x;
                const float dy = ai.utilY - bot.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < 230.0f * 230.0f && d2 > 80.0f * 80.0f) {
                    float ang = 0.0f;
                    if (PlanGrenadeThrow(bot, ai.utilX, ai.utilY, ang)) {
                        ThrowGrenade(bot, ang, audio, std::sqrt(d2));
                        ai.utilDone = true;
                        ++counters_.nadeThrows;
                    }
                }
            }
        }


        // 分工指令
        SquadOrder order;
        if (i < squad_.size()) order = squad_[i];

        // ---- 正在拆包：默认原地不动；一旦看见敌人/挨打就放弃拆包转入反击 ----
        if (ai.defusing) {
            if (!BotCanDefuse(i, bot)) {
                std::cout << "[Defuse] #" << i << " abort (fight/cover)" << std::endl;
                ai.defusing = false;   // 掉到下面的目标选择/开火逻辑
                ai.defuseCooldown = 2.0f;
            } else {
                bot.vx = bot.vy = 0.0f;
                continue;
            }
        }

        // 最近挨打：先把朝向转回威胁来源，否则拆包时被背后打到只会继续贴包。
        if (ai.lastHitTimer > 0.0f || ai.alertTimer > 0.0f) {
            bool reacted = false;
            for (const auto& alert : ai.alerts)
                reacted = reacted || (alert.enemyId != -2 && alert.enemyId != -4 && clockSec_ >= alert.expireAt);
            if (!reacted) continue;
        } else {
            for (auto& alert : ai.alerts) if (alert.enemyId == -3) alert = {};
        }
        if (!ai.defusing && ai.lastHitTimer > 0.0f) {
            const float tdx = ai.threatX - bot.x;
            const float tdy = ai.threatY - bot.y;
            if (tdx * tdx + tdy * tdy > 1.0f) {
                bot.aimAngle = std::atan2(tdy, tdx);
            }
        }

        // ---- 原地扫描：没有目标时缓慢转身 ----
        // 视野是 120° 扇形，若不扫描，背对着敌人的 BOT 会永远发现不了对方
        if (!ai.hadTarget && bot.vx == 0.0f && bot.vy == 0.0f) {
            if (ai.alertTimer > 0.0f) {
                // 刚听到敌对声音：朝声源方向小幅扫视（±16°），而不是回到原扫描中心
                const float ang = std::atan2(ai.alertY - bot.y, ai.alertX - bot.x);
                ai.scanBase = ang;
                ai.scanBaseValid = true;
                bot.aimAngle = ang + std::sin(ai.scanPhase * 3.2f) * 0.28f;
            } else {
                // 原地扫视：绕「进入空闲时的朝向」左右 ±60° 摆动。
                // 中心只在进入空闲时锁定一次，否则 base 会跟着摆角漂移 → 变成整圈旋转。
                // 原先是 aimAngle += 角速度（360° 连续自转），看起来像原地转圈。
                if (!ai.scanBaseValid) {
                    // 守点时优先朝"可能会来人的方向"（战术表 [look] / 敌方出生点质心），
                    // 而不是机械沿用进入空闲那一刻的朝向（之前会面壁站一整回合）。
                    ai.scanBase = (ai.lookAngle > -90.0f) ? ai.lookAngle : bot.aimAngle;
                    ai.scanBaseValid = true;
                }
                bot.aimAngle = ai.scanBase +
                               std::sin(ai.scanPhase * 1.6f) * 1.05f;   // ±60°
            }
        } else {
            ai.scanBaseValid = false;
        }

        // ---- 补枪（trade）：队友刚在附近阵亡 → 冲过去换人 ----
        if (lastDeathTimer_ > 0.0f && lastDeathTeam_ == bot.team &&
            !ai.tradePushed && order.role != BotRole::Anchor && order.canTrade) {
            const float dx = lastDeathX_ - bot.x;
            const float dy = lastDeathY_ - bot.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > Tuning::TradeMinDist * Tuning::TradeMinDist &&
                d2 < Tuning::TradeMaxDist * Tuning::TradeMaxDist) {
                ai.tradePushed = true;
                if (FollowPathTo(bot, ai, lastDeathX_, lastDeathY_, dt, 1.0f)) continue;
            }
        }

        // ---- 受击撤退：低血时优先找掩体，交火次之 ----
        if (ai.retreatTimer > 0.0f) ai.retreatTimer -= dt;
        if (ai.retreatTimer > 0.0f && bot.health <= Tuning::RetreatKeepHealth) {
            float covX = 0.0f, covY = 0.0f;
            if (FindCoverPoint(bot, ai.threatX, ai.threatY, covX, covY)) {
                if (bot.ammo <= 0 && !bot.reloading && bot.reserve > 0) StartReload(bot, audio);
                if (FollowPathTo(bot, ai, covX, covY, dt, 1.0f)) continue;
            }
        }

        // ---- 目标选择：只用「看得见」的敌人 + 团队情报 ----
        float targetDist = 0.0f;
        const Player* visibleTarget = FindVisibleEnemy(bot, targetDist);
        bot.aimDistance = std::max(targetDist, 0.001f);
        const Player* target = visibleTarget;
        if (visibleTarget) {
            const int id = visibleTarget == &player_ ? -1 : static_cast<int>(visibleTarget - bots_.data());
            if (ai.perceivedTarget != id) ai.settleTimer = 0.0f;
            ai.perceivedTarget = id;
            ai.Perceive(id, clockSec_, perception.reaction);
            if (!ai.Ready(id, clockSec_)) continue;
            if (!ai.hadTarget) ai.settleTimer = 0.0f;
        }

        if (!visibleTarget) ai.perceivedTarget = -2;

        // ---- 听声扑人：下包 / 拆包的声音优先级最高 ----
        // T 下包时，能听到的 CT 优先扑向安装者；
        // CT 拆包时，能听到的 T 优先扑向拆包者。
        // 听声目标不需要在视野锥内（声音不分朝向），但必须敌我阵营相反，
        // 且距离在对应可听半径内。
        bool heardSound = false;
        SoundMaker sm;
        if (GetActiveSoundMaker(sm)) {
            const bool hearThis = (sm.isPlant && bot.team == Team::CT)
                               || (!sm.isPlant && bot.team == Team::T);
            if (hearThis && sm.who && sm.who->alive) {
                // 不再重复判断阵营：hearThis 已经把「我方听到敌方的动作」限定好了
                const float sdx = sm.x - bot.x;
                const float sdy = sm.y - bot.y;
                const float sdist = std::sqrt(sdx * sdx + sdy * sdy);
                const float hearR = sm.isPlant ? Tuning::PlantHearRadius
                                               : Tuning::DefuseHearRadius;
                if (sdist <= hearR) {
                    const int id = sm.who == &player_ ? -1 : static_cast<int>(sm.who - bots_.data());
                    ai.Perceive(id, clockSec_, perception.reaction);
                    if (!ai.Ready(id, clockSec_)) continue;
                    heardSound = true;
                    target      = sm.who;
                    targetDist  = sdist;
                    // 每只 BOT 每 2 秒最多打一行，避免刷屏
                    if (ai.heardLogCooldown <= 0.0f) {
                        ai.heardLogCooldown = 2.0f;
                        std::cout << "[Bot] #" << i
                                  << (sm.isPlant ? " 听到下包声" : " 听到拆包声")
                                  << " 距离 " << static_cast<int>(sdist)
                                  << " → 优先扑向执行者" << std::endl;
                    }
                }
            }
        }

        // ---- 听觉：把最近听到的声音整理成本帧要用的信息 ----
        // 只取敌对声音；同类型只保留最新一条，超过记忆时长自动失效。
        auto heardCopy = [&](SoundEventType type, float maxAge, HeardSound& out) -> bool {
            const HeardSound* h = NewestHeard(i, type, maxAge);
            if (!h || !h->hostile) return false;
            out = *h;
            return true;
        };
        HeardSound hearStep, hearShot, hearReload, hearNade;
        const bool bStep   = heardCopy(SoundEventType::Footstep, Tuning::FootstepMemory, hearStep);
        const bool bShot   = heardCopy(SoundEventType::Gunshot, Tuning::SoundAlertMemory, hearShot);
        const bool bReload = heardCopy(SoundEventType::Reload, Tuning::SoundAlertMemory, hearReload);
        const bool bNade   = heardCopy(SoundEventType::GrenadeBounce,
                                       Tuning::SoundAlertMemory, hearNade);

        // 近处枪声/换弹 → 主动摸过去（断后位不追；下包后守包优先）
        const auto dist2Of = [&](const HeardSound& h) {
            const float dx = h.x - bot.x;
            const float dy = h.y - bot.y;
            return dx * dx + dy * dy;
        };
        const bool pushShot = bShot && dist2Of(hearShot) <=
                              Tuning::GunshotPushRadius * Tuning::GunshotPushRadius;
        const bool pushReload = bReload && dist2Of(hearReload) <=
                                Tuning::ReloadPushRadius * Tuning::ReloadPushRadius;
        const bool heardPush = !target && !heardSound && !bombPlanted_ &&
                               order.role != BotRole::Anchor &&
                               (pushShot || pushReload);

        // ---- 听到手雷落地：先躲开落点（活下来比守点重要）----
        if (bNade && !ai.defusing) {
            const float ndx = bot.x - hearNade.x;
            const float ndy = bot.y - hearNade.y;
            const float nd  = std::sqrt(ndx * ndx + ndy * ndy);
            if (nd > 1.0f && nd < Tuning::GrenadeDangerRadius) {
                float ex = bot.x + (ndx / nd) * Tuning::GrenadeDodgeDist;
                float ey = bot.y + (ndy / nd) * Tuning::GrenadeDodgeDist;
                if (world_) {
                    ex = std::clamp(ex, 1.0f, world_->GetMapPixelW() - 1.0f);
                    ey = std::clamp(ey, 1.0f, world_->GetMapPixelH() - 1.0f);
                }
                if (FollowPathTo(bot, ai, ex, ey, dt, 1.0f)) {
                    if (ai.reactionLogCooldown <= 0.0f) {
                        ai.reactionLogCooldown = 2.0f;
                        std::cout << "[Bot] #" << i << " 听到手雷落地 距离 "
                                  << static_cast<int>(nd) << " → 躲开落点" << std::endl;
                    }
                    continue;
                }
            }
        }

        // ---- 支援前压：队友正在交火且处于劣势 → 靠近支援（不是"尸体补枪"）----
        // 比死后补枪更主动：队友要顶不住了才去，避免全员排队送；Anchor 与无补枪许可者不去。
        if (ai.assistCooldown > 0.0f) ai.assistCooldown -= dt;
        if (!target && !ai.defusing && order.role != BotRole::Anchor &&
            ai.holdStyle == 0 && order.canTrade && ai.assistCooldown <= 0.0f) {
            int   bestMate = -1;
            float bestD2 = Tuning::ai::AssistPushRadius * Tuning::ai::AssistPushRadius;
            for (size_t j = 0; j < bots_.size(); ++j) {
                if (j == i) continue;
                const Player& mate = bots_[j];
                if (!mate.alive || mate.team != bot.team) continue;
                const BotRuntime& mai = AiAt(j);
                if (!mai.hadTarget) continue;          // 队友此刻在交火
                const float hpFrac = (mate.maxHealth > 0)
                    ? 100.0f * static_cast<float>(mate.health) /
                      static_cast<float>(mate.maxHealth)
                    : 100.0f;
                const bool losing = hpFrac <= Tuning::ai::AssistPushMaxHealth ||
                                    (mate.ammo <= 0 && !mate.reloading);
                if (!losing) continue;
                const float dx = mate.x - bot.x, dy = mate.y - bot.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < bestD2) { bestD2 = d2; bestMate = static_cast<int>(j); }
            }
            if (bestMate >= 0) {
                const Player& mate = bots_[static_cast<size_t>(bestMate)];
                // 停在队友侧后方 40px，别挤进同一条火线
                const float ang = std::atan2(mate.y - bot.y, mate.x - bot.x) + 0.6f;
                float ax = mate.x + std::cos(ang) * 40.0f;
                float ay = mate.y + std::sin(ang) * 40.0f;
                if (world_) {
                    ax = std::clamp(ax, 1.0f, world_->GetMapPixelW() - 1.0f);
                    ay = std::clamp(ay, 1.0f, world_->GetMapPixelH() - 1.0f);
                }
                if (FollowPathTo(bot, ai, ax, ay, dt, 1.0f)) {
                    ai.assistCooldown = Tuning::ai::AssistPushCooldown;
                    ++counters_.assistPushes;
                    if (ai.reactionLogCooldown <= 0.0f) {
                        ai.reactionLogCooldown = 2.0f;
                        std::cout << "[Bot] #" << i << " 支援前压 #" << bestMate
                                  << "（队友交火劣势）" << std::endl;
                    }
                    continue;
                }
            }
        }

        // 静步时机：lurk 平时慢走；听到脚步去查时慢走；教练 save 时也尽量不出声。
        // 有可见目标时解除静步，正常交火/补枪。
        {
            const bool saving = GetCoachPlan(bot.team).intent == PlanIntent::Save;
            bot.stealth = !target &&
                          (saving || order.canRotate || bStep);
        }

        // 首次接火：每队每回合只报一次（教练据此判断主攻方向）
        if (target && CoachFor(bot.team).firstContactArea.empty()) {
            const std::string ca = world_ ? world_->FindAreaAt(bot.x, bot.y) : std::string();
            CoachFor(bot.team).firstContactArea = ca;
            CoachOnEvent(bot.team, CoachEventType::EnemySpotted, ca);
        }

        // lurk 换点：允许游走的角色在驻守点附近周期性换位，避免站死
        if (ai.hasPlanHold && order.canRotate) {
            ai.rotateTimer -= dt;
            if (ai.rotateTimer <= 0.0f) {
                ai.rotateTimer = 6.0f;
                std::uniform_real_distribution<float> ang(0.0f, 6.2831853f);
                std::uniform_real_distribution<float> rad(30.0f, 80.0f);
                for (int tries = 0; tries < 6; ++tries) {
                    const float a = ang(rng_);
                    const float r = rad(rng_);
                    const float nx = ai.planHoldX + std::cos(a) * r;
                    const float ny = ai.planHoldY + std::sin(a) * r;
                    const float half = bot.size * 0.5f;
                    if (world_ && !world_->CheckRectCollision(nx - half, ny - half,
                                                              bot.size, bot.size)) {
                        SetBotGoal(i, nx, ny, ai.goalSource, /*force=*/true);
                        break;
                    }
                }
            }
        }

        // C4 携带者已经站进包点且没有可见敌人：停下来让 UpdateObjective 推进下包。
        // 否则教练计划/巡逻会让他反复进出包点，下包进度永远清零。
        if (bombCarrier_ == static_cast<int>(i) && !bombPlanted_ && !bombDropped_ &&
            !target && world_ && world_->FindBombSiteAt(bot.x, bot.y)) {
            bot.vx = bot.vy = 0.0f;
            continue;
        }

        // 听到敌人脚步：只有听见的这只 BOT 去查，不写入团队情报。
        // 优先级高于驻守点，但低于已下包 / C4 掉落等目标。
        if (!target && bStep && !bombPlanted_ && !bombDropped_) {
            const float hdx = hearStep.x - bot.x;
            const float hdy = hearStep.y - bot.y;
            constexpr float kHearArrive = 14.0f;
            if (hdx * hdx + hdy * hdy <= kHearArrive * kHearArrive) {
                // 到声源附近：原地压枪警戒，等记忆过期或看到敌人
                bot.vx = bot.vy = 0.0f;
                continue;
            }
            if (FollowPathTo(bot, ai, hearStep.x, hearStep.y, dt, 1.0f)) {
                continue;
            }
        }

        // 教练计划：没有可见敌人时优先去计划的驻守点（有敌人时反应交给决策树）
        // After plant, defer to bomb ring / defuse dest below (do not pin CT on site center).
        // 近处出现枪声/换弹时暂时让位给听觉反应（记忆过期后自然回到计划点）。
        if (!target && ai.hasPlanHold && !bombPlanted_ && !heardPush) {
            const float pdx = ai.planHoldX - bot.x;
            const float pdy = ai.planHoldY - bot.y;
            constexpr float kPlanArrive = 16.0f;
            if (pdx * pdx + pdy * pdy < kPlanArrive * kPlanArrive) {
                // Arrived at plan point: clear hold so peek/patrol/default goals can resume.
                std::cout << "[Bot] #" << i << " 到达计划点，解除驻守"
                          << (ai.goalArea.empty() ? "" : (" " + ai.goalArea)) << std::endl;
                ai.hasPlanHold = false;
            } else {
                const float planMul = (order.role == BotRole::Entry)   ? Tuning::SpeedMulEntry
                                    : (order.role == BotRole::Anchor)  ? Tuning::SpeedMulAnchor
                                                                       : Tuning::SpeedMulSupport;
                if (FollowPathTo(bot, ai, ai.planHoldX, ai.planHoldY, dt, planMul)) {
                    continue;
                }
            }
        }
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

        // 可开火条件 = 目标真的在视野里。听声锁定的目标若恰好可见（或就是刚才
        // 看见的那个人）也允许开火；若隔着墙没视线则只追不打。
        const bool canSee = (visibleTarget != nullptr) &&
                            (!heardSound || visibleTarget == target);
        // hadTarget 保持原语义：是否「看得见」敌人（决定原地扫视与姿态推导）
        ai.hadTarget = canSee;

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

        // ---- 弹药耗尽：主武器弹匣+备弹全空 → 切副武器，再不行切近战 ----
        // （备弹有限，打光后不能再换弹，必须换枪而不是原地空放）
        if (!bot.reloading && bot.ammo <= 0 && bot.reserve <= 0) {
            int fallback = -1;
            if (bot.weaponSlot != 0 && bot.weapons[0] != WeaponId::None &&
                (bot.ammoSlot[0] > 0 || bot.reserveSlot[0] > 0)) {
                fallback = 0;
            } else if (bot.weaponSlot != 1 && bot.weapons[1] != WeaponId::None &&
                       (bot.ammoSlot[1] > 0 || bot.reserveSlot[1] > 0)) {
                fallback = 1;
            } else if (bot.weaponSlot != 2 && bot.weapons[2] != WeaponId::None) {
                fallback = 2;   // 近战不耗弹
            }
            if (fallback >= 0) {
                bot.SaveAmmoToSlot();          // 先把当前槽（已空）写回
                bot.weaponSlot = fallback;
                bot.LoadAmmoFromSlot();
                bot.reloading   = false;
                bot.reloadTimer = 0.0f;
                EmitSound(SoundEventType::WeaponSwitch, bot.x, bot.y, bot.team,
                          static_cast<int>(i));
                if (audio) {
                    audio->PlayWeaponSwitchAt(bot.CurrentWeapon().id, bot.x, bot.y,
                                              ListenerX(), ListenerY());
                }
            }
        }

        // ---- 换弹决策 ----
        // 脱战就补满；交火中只在弹匣见底（<=25%）才打断射击去换弹
        // 备弹为 0 时无法换弹，交给上面的换枪逻辑
        if (!bot.reloading && bot.ammo < bot.maxAmmo && bot.reserve > 0) {
            const int lowAmmo = static_cast<int>(static_cast<float>(bot.maxAmmo) * Tuning::LowAmmoFraction);
            if (!canSee || bot.ammo <= lowAmmo) StartReload(bot, audio);
        }

        // ---- 手雷：有情报但无直接视野时，向情报点投掷（先校验落点，避免隔着墙扔）----
        if (ai.grenadeCooldown > 0.0f) ai.grenadeCooldown -= dt;
        if (!target && intel && bot.grenadeCount > 0 && ai.grenadeCooldown <= 0.0f) {
            if (engageDist > Tuning::NadeThrowMin && engageDist < Tuning::NadeThrowMax) {
                float throwAng = 0.0f;
                if (PlanGrenadeThrow(bot, aimX, aimY, throwAng)) {
                    ThrowGrenade(bot, throwAng, audio, std::hypot(aimX - bot.x, aimY - bot.y));
                    ai.grenadeCooldown = Tuning::NadeCooldown;
                    ++counters_.nadeThrows;
                } else {
                    // 没有可行落点：短冷却后再找机会，避免每帧重试
                    ai.grenadeCooldown = 1.5f;
                    ++counters_.nadeSkips;
                }
            }
        }

        // ---- 目标引导：A* 寻路推进 ----
        // 角色行进速度：突破手全速、支援略慢、断后明显靠后
        const float aggr = TempoAggression(GetCoachPlan(bot.team).tempo);
        const float roleMul = (order.role == BotRole::Entry)   ? Tuning::SpeedMulEntry
                              : (order.role == BotRole::Anchor)  ? Tuning::SpeedMulAnchor
                              :                                    Tuning::SpeedMulSupport;
        // 本帧是否已由寻路接管移动（否则交给下方交火机动处理）
        bool pathTookMove = false;
        {
            bool hasDest = false;
            bool destIsObjective = false;
            bool softHold = false;   // 只是「去驻守点」：听觉情报可以临时把它顶掉
            float destX = 0.0f, destY = 0.0f;

            if (!visibleTarget) ai.perceivedTarget = -2;

        // ---- 听声扑人：优先级最高，直接压过常规驻守/进攻目标 ----
            // 听到下包（CT 方）或拆包（T 方）的声音时，立刻转向发声者，
            // 不再按教练计划守点/推进，避免"听见了却站着不动"。
            if (heardSound && sm.who) {
                destX = sm.x;
                destY = sm.y;
                hasDest = true;
                destIsObjective = true;
            }

            // ---- C4 掉落：回收 / 蹲守（优先级仅次于听声扑人）----
            if (!hasDest && mode_ == MatchMode::BombDefusal && !bombPlanted_ && bombDropped_) {
                if (bot.team == Team::T) {
                    // T：离掉落点最近的 N 个 BOT 去取回 C4（其余继续进攻）
                    const auto it = std::find(recoverers.begin(), recoverers.end(),
                                              static_cast<int>(i));
                    if (it != recoverers.end()) {
                        destX = bombDropX_;
                        destY = bombDropY_;
                        hasDest = true;
                        destIsObjective = true;   // 回收是任务，断后位也要去
                    }
                } else if (bombIntel_) {
                    // CT：看到掉落 C4 → 环形站位蹲守，形成交叉火力
                    static const float kGuardRing[8] = { 0.0f, 0.79f, 1.57f, 2.36f,
                                                         3.14f, 3.93f, 4.71f, 5.50f };
                    const float ang = kGuardRing[i % 8];
                    destX = bombIntelX_ + std::cos(ang) * Tuning::BombGuardRing;
                    destY = bombIntelY_ + std::sin(ang) * Tuning::BombGuardRing;
                    hasDest = true;
                    destIsObjective = true;   // 蹲守是任务，断后位也去
                }
            }

            if (!hasDest && mode_ == MatchMode::BombDefusal) {
                if (bot.team == Team::T) {
                    if (!bombPlanted_) {
                        // 按分队指令进攻指定包点（保证 A/B 都有人去）
                        // 目标唯一真源：ai.hasPlanHold（教练计划与小队轮转都经 SetBotGoal 写入）
                        if (ai.hasPlanHold) {
                            destX = ai.planHoldX;
                            destY = ai.planHoldY;
                            hasDest = true;
                            destIsObjective = true;
                            softHold = true;
                        } else {
                            const auto& sites = world_->GetBombSites();
                            float bestD2 = 1e18f;
                            for (const auto& s : sites) {
                                const float dx = s.cx - bot.x, dy = s.cy - bot.y;
                                const float d2 = dx * dx + dy * dy;
                                if (d2 < bestD2) { bestD2 = d2; destX = s.cx; destY = s.cy; hasDest = true; }
                            }
                            destIsObjective = hasDest;
                        }
                    } else {
                        // 已下包：围绕包点环形站位守包（不再全挤在包上）
                        static const float kRing[8] = { 0.0f, 0.79f, 1.57f, 2.36f,
                                                        3.14f, 3.93f, 4.71f, 5.50f };
                        const float ang = kRing[i % 8];
                        destX = bombX_ + std::cos(ang) * Tuning::BombHoldRing;
                        destY = bombY_ + std::sin(ang) * Tuning::BombHoldRing;
                        hasDest = true;
                        destIsObjective = true;
                    }
                } else if (bombPlanted_) {
                    // 下包后：只让拆包手去炸弹；其他人去清包点附近的情报，
                    // 没有情报就在炸弹外围架枪，不再全员堆在炸弹上。
                    const int defuser = PickDefuseBot();
                    if (defuser == static_cast<int>(i)) {
                        destX = bombX_;
                        destY = bombY_;
                    } else {
                        const EnemyContact* c = FindNearestContact(Team::CT, bombX_, bombY_);
                        const float cdx = c ? (c->x - bombX_) : 0.0f;
                        const float cdy = c ? (c->y - bombY_) : 0.0f;
                        if (c && cdx * cdx + cdy * cdy < 260.0f * 260.0f) {
                            static const float kApproach[8] = {
                                0.0f, 0.79f, 1.57f, 2.36f, 3.14f, 3.93f, 4.71f, 5.50f
                            };
                            const float ang = kApproach[i % 8];
                            destX = c->x + std::cos(ang) * 34.0f;
                            destY = c->y + std::sin(ang) * 34.0f;
                        } else {
                            static const float kCover[8] = {
                                0.0f, 0.79f, 1.57f, 2.36f, 3.14f, 3.93f, 4.71f, 5.50f
                            };
                            const float ang = kCover[i % 8];
                            destX = bombX_ + std::cos(ang) * Tuning::DefuseCoverRing;
                            destY = bombY_ + std::sin(ang) * Tuning::DefuseCoverRing;
                        }
                    }
                    hasDest = true;
                    destIsObjective = true;
                } else if (ai.hasPlanHold) {
                    // 未下包：各守 A / B / 中路（目标唯一真源）
                    destX = ai.planHoldX;
                    destY = ai.planHoldY;
                    hasDest = true;
                    destIsObjective = true;
                    softHold = true;
                }
            }

            // ---- 听觉反应：近处枪声 / 敌人换弹 → 摸过去 ----
            // 只顶掉「去驻守点」这类软目标；C4 回收、守包、拆包等硬目标不动。
            // 断后位不追（守自己的角度），下包后也不再离开包点。
            if (heardPush && (!hasDest || softHold)) {
                if (pushShot) {
                    destX = hearShot.x;
                    destY = hearShot.y;
                } else {
                    destX = hearReload.x;
                    destY = hearReload.y;
                }
                hasDest = true;
                destIsObjective = true;   // 已主动接战，断后位不再撤销
                if (ai.reactionLogCooldown <= 0.0f) {
                    ai.reactionLogCooldown = 2.0f;
                    std::cout << "[Bot] #" << i
                              << (pushShot ? " 听到枪声" : " 听到换弹")
                              << " → 前压声源" << std::endl;
                }
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

            // ---- 搜点：无目标、非断后、回合前 40s → 去最近的未搜区域 ----
            // 搜点主要给进攻方（推点途中清角落）；CT 守点乱跑会把包点让出来
            const bool canSearch = bot.team == Team::T && ai.holdStyle == 0 &&
                !target && order.role != BotRole::Anchor &&
                GetRoundElapsed() > 12.0f &&
                GetRoundElapsed() < Tuning::ai::SearchWindowSec &&
                (!hasDest || (softHold && order.canRotate));
            if (canSearch) {
                float bx = 0.0f, by = 0.0f;
                bool haveSearchDest = false;

                // 已经在搜某个区域：继续朝它走（不要每帧换目标，否则永远到不了）
                if (!ai.searchArea.empty() && AreaCenterByName(ai.searchArea, bx, by)) {
                    const float dx = bx - bot.x, dy = by - bot.y;
                    if (dx * dx + dy * dy >
                        Tuning::ai::SearchAreaRadius * Tuning::ai::SearchAreaRadius) {
                        haveSearchDest = true;
                    }
                } else {
                    ai.searchArea.clear();
                }

                if (!haveSearchDest) {
                    const int ti = (bot.team == Team::T) ? 0 : 1;
                    auto& book = areaSearchedAt_[ti];
                    float bestD2 = Tuning::ai::SearchMaxDetour * Tuning::ai::SearchMaxDetour;
                    std::string bestArea;
                    for (const auto& [name, when] : book) {
                        if (GetRoundElapsed() - when < 5.0f) continue;   // 刚确认过的不重复查
                        float ax = 0.0f, ay = 0.0f;
                        if (!AreaCenterByName(name, ax, ay)) continue;
                        const float dx = ax - bot.x, dy = ay - bot.y;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < bestD2) { bestD2 = d2; bestArea = name; bx = ax; by = ay; }
                    }
                    if (!bestArea.empty()) {
                        if (ai.searchArea != bestArea && ai.reactionLogCooldown <= 0.0f) {
                            ai.reactionLogCooldown = 2.0f;
                            std::cout << "[Bot] #" << i << " 搜点 -> " << bestArea << std::endl;
                        }
                        ai.searchArea = bestArea;
                        ai.searchTimer = 0.0f;
                        haveSearchDest = true;
                    }
                }

                if (haveSearchDest) {
                    destX = bx;
                    destY = by;
                    hasDest = true;
                    softHold = true;             // 听到交战/下包声可随时中断搜点
                }
            }

            // 巡逻兜底：无任务、无情报、无可见敌人时游走，避免原地发呆
            // 突破/支援位朝「敌方出生点质心」推进（与地图朝向无关），断后位就近游走
            if (!hasDest) {
                // 爆破图有固定防守/进攻位：在其附近小范围活动，避免跑离岗位
                const bool holdPost = (mode_ == MatchMode::BombDefusal && ai.hasPlanHold);
                const bool pushToEnemy = (order.role != BotRole::Anchor);
                float ecx = 0.0f, ecy = 0.0f;
                const bool hasEnemyCentroid =
                    !holdPost && pushToEnemy && EnemySpawnCentroid(bot.team, ecx, ecy);

                // 巡逻/驻守目标要「缓存一段时间」再重选。
                // 之前每帧都用 rng 重掷一个 ±90px 的目标，BOT 会一直在原地小幅抖动。
                if (ai.wanderTimer > 0.0f) {
                    destX = ai.wanderX;
                    destY = ai.wanderY;
                    hasDest = true;
                } else {
                    for (int t = 0; t < 12; ++t) {
                        float rx = 0.0f, ry = 0.0f;
                        if (holdPost) {
                            rx = ai.planHoldX + static_cast<float>(rng_() % 181) - Tuning::PatrolHoldSpread;
                            ry = ai.planHoldY + static_cast<float>(rng_() % 181) - Tuning::PatrolHoldSpread;
                        } else if (hasEnemyCentroid) {
                            rx = ecx + static_cast<float>(rng_() % 521) - 260.0f;
                            ry = ecy + static_cast<float>(rng_() % 521) - 260.0f;
                        } else {
                            rx = bot.x + static_cast<float>(rng_() % 601) - 300.0f;
                            ry = bot.y + static_cast<float>(rng_() % 601) - 300.0f;
                        }
                        if (pathFinder_.IsWalkableWorld(rx, ry)) {
                            ai.wanderX = rx;
                            ai.wanderY = ry;
                            ai.wanderTimer = 3.0f;   // 3 秒后换个位置
                            destX = rx;
                            destY = ry;
                            hasDest = true;
                            break;
                        }
                    }
                }
            }

            // 断后位不主动远距离追击：没有任务目标时守在原地
            if (!destIsObjective && order.role == BotRole::Anchor &&
                engageDist > Tuning::AnchorHoldRange * aggr) {
                hasDest = false;
            }

            if (hasDest) {
                // 有明确目的地时一律走 A* 寻路（含拐角转向/卡墙脱困）。
                // 交火中同样寻路：以前的 closeCombat 分支会把移动交给执行器的
                // 写死几何位移，绕开寻路导致撞墙。
                if (FollowPathTo(bot, ai, destX, destY, dt, roleMul)) {
                    pathTookMove = true;   // 本帧移动已由寻路接管
                }
            }

            // 搜点确认：到达未搜区域并停留 SearchConfirmSec 才算"查过"。
            // 注意：无目标的分支下面会 continue，所以确认逻辑必须放在这之前。
            if (!ai.searchArea.empty() && !target) {
                float ax = 0.0f, ay = 0.0f;
                if (AreaCenterByName(ai.searchArea, ax, ay)) {
                    const float dx = ax - bot.x, dy = ay - bot.y;
                    if (dx * dx + dy * dy <
                        Tuning::ai::SearchAreaRadius * Tuning::ai::SearchAreaRadius) {
                        ai.searchTimer += dt;
                        if (ai.searchTimer >= Tuning::ai::SearchConfirmSec) {
                            const int ti = (bot.team == Team::T) ? 0 : 1;
                            areaSearchedAt_[ti][ai.searchArea] = GetRoundElapsed();
                            ++counters_.searchVisits;
                            ai.searchArea.clear();
                            ai.searchTimer = 0.0f;
                        }
                    } else {
                        ai.searchTimer = 0.0f;
                    }
                } else {
                    ai.searchArea.clear();
                    ai.searchTimer = 0.0f;
                }
            }

            // 没有目标 → 本帧不开火，移动已处理完
            if (!target) {
                if (!pathTookMove) bot.vx = bot.vy = 0.0f;
                continue;
            }
        }

        // ---- 决策树姿态推导 ----
        // 到这里必然有目标（上面 !target 已 continue）。姿态只决定
        // 「机动偏好 + 射击节奏」，不接管寻路：移动始终走碰撞检测的位移路径。
        const WeaponDef& def = bot.CurrentWeapon();
        const SquadOrder& ord = squad_[i];

        const float hpFrac = (bot.maxHealth > 0)
            ? static_cast<float>(bot.health) / static_cast<float>(bot.maxHealth)
            : 1.0f;

        const bool mustRetreat = ai.blindTimer > 0.0f ||
                                 hpFrac * 100.0f < Tuning::RetreatHealth ||
                                 (bot.ammo <= 0 && !bot.reloading && bot.reserve <= 0);

        BotStance stance = BotStance::Defensive;
        if (mustRetreat) {
            stance = BotStance::Retreat;
        } else if (ord.role == BotRole::Anchor ||
                   GetCoachPlan(bot.team).tempo == Tempo::Slow) {
            stance = BotStance::Defensive;
        } else if (ord.role == BotRole::Entry ||
                   engageDist < Tuning::CloseCombatRange * 0.6f * aggr) {
            stance = BotStance::Aggressive;
        } else if (ai.hasPlanHold && engageDist > def.range) {
            stance = BotStance::MoveToCover;
        }
        ai.stance = stance;

        // 机动偏好：决定交火时是侧拉、后撤还是保持
        BotManeuver maneuver = BotManeuver::None;
        if (stance == BotStance::Retreat) {
            maneuver = BotManeuver::BackOff;
        } else if (stance == BotStance::Aggressive || stance == BotStance::Defensive) {
            maneuver = BotManeuver::Strafe;
        }
        ai.maneuver = maneuver;

        // ---- 交火机动：只在「没有寻路接管本帧」时生效 ----
        // 之前这里由执行器用写死的几何关系（理想距离 80 / 斜向退 30）算位移，
        // 完全不看墙和地图。现在改为「沿可行方向小步机动 + 碰撞检测」，
        // 有明确目的地时优先让 A* 寻路接管。
        if (!pathTookMove) {
            const int   slot = bot.weaponSlot;
            const bool  meleeSlot = (slot == 2);
            const float meleeReach = Tuning::ai::MeleeReachBase +
                                     bot.size * Tuning::ai::MeleeReachPerSize;
            // 近战要贴脸，远程要拉开；退枪角色则主动后撤
            const float idealDist = meleeSlot ? meleeReach * 0.6f
                                              : (def.range * 0.55f);
            float mvx = 0.0f, mvy = 0.0f;

            const float pdx = aimX - bot.x;
            const float pdy = aimY - bot.y;
            const float pd  = std::max(1.0f, std::sqrt(pdx * pdx + pdy * pdy));
            const float nx  = pdx / pd;
            const float ny  = pdy / pd;

            ai.moveLatchTimer  = std::max(0.0f, ai.moveLatchTimer - dt);
            ai.strafeSideTimer = std::max(0.0f, ai.strafeSideTimer - dt);

            if (maneuver == BotManeuver::BackOff) {
                // 撤退：沿远离方向走，速度略高于常规
                if (pd < idealDist * 1.6f) { mvx = -nx; mvy = -ny; }
                ai.moveLatch = -1;
                ai.moveLatchTimer = Tuning::ai::StrafeDirLatch;
            } else if (maneuver == BotManeuver::Strafe) {
                // 距离调节带滞回：进入"前进/后退"后至少保持 StrafeDirLatch 秒，
                // 避免距离在阈值附近抖动导致前后前后来回走。
                int wanted = 0;
                if (pd > idealDist + Tuning::ai::StrafeForwardGap)      wanted = +1;
                else if (pd < idealDist - Tuning::ai::StrafeBackGap)    wanted = -1;

                if (wanted != 0) {
                    if (ai.moveLatch == 0 || ai.moveLatchTimer <= 0.0f) {
                        ai.moveLatch      = wanted;
                        ai.moveLatchTimer = Tuning::ai::StrafeDirLatch;
                    }
                    mvx = nx * static_cast<float>(ai.moveLatch);
                    mvy = ny * static_cast<float>(ai.moveLatch);
                } else {
                    // 距离合适：侧向拉枪，方向锁定一段时间再考虑换边
                    ai.moveLatch = 0;
                    if (ai.strafeSideTimer <= 0.0f) {
                        ai.strafeSide      = -ai.strafeSide;
                        ai.strafeSideTimer = Tuning::ai::StrafeDirHold;
                    }
                    mvx = -ny * ai.strafeSide;
                    mvy =  nx * ai.strafeSide;
                }
            }

            if (mvx != 0.0f || mvy != 0.0f) {
                // 与寻路保持同一套速度基准：基础速度 × 武器倍率 × 角色倍率
                const float cap = Tuning::BotMoveSpeed * def.moveSpeedMul * roleMul;
                const float spd = (maneuver == BotManeuver::BackOff ? cap : cap * 0.55f);
                float vx = mvx * spd;
                float vy = mvy * spd;

                const float half = bot.size * 0.5f;
                const float probe = std::max(12.0f, spd * dt * 4.0f);
                auto blockedAt = [&](float dx, float dy) {
                    return !world_ || world_->CheckRectCollision(bot.x + dx * probe - half,
                                                                 bot.y + dy * probe - half,
                                                                 bot.size, bot.size);
                };

                if (blockedAt(mvx, mvy)) {
                    // 1) 反侧；2) 沿墙滑动（±45°/±90°）；3) 短程寻路绕行；4) 才停下
                    bool solved = false;
                    if (maneuver == BotManeuver::Strafe) {
                        const float altX = ny * ai.strafeSide;
                        const float altY = -nx * ai.strafeSide;
                        if (!blockedAt(altX, altY)) {
                            vx = altX * spd; vy = altY * spd;
                            ai.strafeSide = -ai.strafeSide;
                            ai.strafeSideTimer = Tuning::ai::StrafeDirHold;
                            solved = true;
                        }
                    }
                    if (!solved) {
                        const float baseAng = std::atan2(mvy, mvx);
                        static const float kOffsets[4] = {0.7854f, -0.7854f, 1.5708f, -1.5708f};
                        for (int k = 0; k < Tuning::ai::StrafeSlideAngles && !solved; ++k) {
                            const float ang = baseAng + kOffsets[k];
                            const float cx = std::cos(ang), cy = std::sin(ang);
                            if (!blockedAt(cx, cy)) {
                                vx = cx * spd; vy = cy * spd;
                                solved = true;
                            }
                        }
                    }
                    if (!solved) {
                        // 贴墙时用一次短程寻路绕开（交火中也允许，代价是一次 A*）
                        const float flankAng = std::atan2(mvy, mvx) + 1.5708f;
                        const float fx = bot.x + std::cos(flankAng) * 70.0f;
                        const float fy = bot.y + std::sin(flankAng) * 70.0f;
                        if (world_ && !world_->CheckRectCollision(fx - half, fy - half,
                                                                  bot.size, bot.size) &&
                            FollowPathTo(bot, ai, fx, fy, dt, roleMul)) {
                            solved = true;
                            pathTookMove = true;   // 位移已由寻路完成
                            vx = 0.0f; vy = 0.0f;
                        }
                    }
                    if (!solved) { vx = 0.0f; vy = 0.0f; }
                }

                // 局部避障（避免与队友叠在一起）
                if (vx != 0.0f || vy != 0.0f) {
                    float sepX = 0.0f, sepY = 0.0f;
                    AccumulateSeparation(bot, sepX, sepY);
                    ApplyAvoidance(vx, vy, sepX, sepY);
                    // 避障会叠加分离力，必须重新钳到持械上限，否则会突然加速
                    const float s2 = std::sqrt(vx * vx + vy * vy);
                    if (s2 > cap && s2 > 0.001f) {
                        vx = vx / s2 * cap;
                        vy = vy / s2 * cap;
                    }
                }
                bot.vx = vx;
                bot.vy = vy;
            } else {
                bot.vx = 0.0f;
                bot.vy = 0.0f;
            }

            ai.prevLatch = ai.moveLatch;
        }

        // 交战朝向：可见时直接瞄准目标（带短促摆头，避免完美锁头）
        if (canSee) {
            const float desired = std::atan2(aimY - bot.y, aimX - bot.x)
                                + std::sin(ai.scanPhase * 2.2f) * Tuning::BotScanAmplitudeRad * 0.5f;
            float delta = desired - bot.aimAngle;
            while (delta >  3.14159265f) delta -= 6.28318531f;
            while (delta < -3.14159265f) delta += 6.28318531f;
            const float maxTurn = Tuning::BotMaxTurnRate * dt;
            bot.aimAngle += std::clamp(delta, -maxTurn, maxTurn);
        }

        // 位移：FollowPathTo 内部已经调用过 MoveWithCollision（含碰撞与卡墙脱困），
        // 这里只在「本帧未走寻路」时补一次，否则会一帧位移两次 → 速度翻倍。
        if (!pathTookMove) {
            MoveWithCollision(bot, dt);
        }

        // ---- 射击纪律（决策树直接给出射速/散布乘数）----
        // HoldFire 换弹中/无弹；Burst 远距离点射（更准更慢）；Auto 近距离连发
        float shootCooldownMul = 1.0f;
        float spreadMul        = 1.0f;
        bool  shouldShoot      = canSee && !bot.reloading && bot.ammo > 0;

        if (bot.reloading || bot.ammo <= 0) {
            shouldShoot = false;
        } else if (engageDist > def.range * 0.8f) {
            // 点射：射速降到 1/2.5，散布减到 40%（准度显著提升）
            shootCooldownMul = 2.5f;
            spreadMul        = 0.4f;
        } else {
            // 连发：按距离分档，近距离更快更准
            if (engageDist < 60.0f)      { shootCooldownMul = 0.8f; spreadMul = 0.7f; }
            else if (engageDist < 150.0f){ shootCooldownMul = 1.0f; spreadMul = 1.0f; }
            else                          { shootCooldownMul = 1.5f; spreadMul = 1.3f; }
        }

        // 撤退/借掩体途中，除非目标贴脸（<50px）否则不开火
        if ((stance == BotStance::Retreat || stance == BotStance::MoveToCover) &&
            engageDist > 50.0f) {
            shouldShoot = false;
        }
        // 近战只在触及范围内出手（按体型算，不再用固定 25px 留出死区）
        const float meleeReachNow = Tuning::ai::MeleeReachBase +
                                    bot.size * Tuning::ai::MeleeReachPerSize;
        if (bot.weaponSlot >= 2 && engageDist > meleeReachNow) shouldShoot = false;

        // 站姿=被动：只打贴脸，不主动拉远枪线
        if (ai.holdStyle == 3 && engageDist > 250.0f) shouldShoot = false;

        // 结算展示期与回合中一致：BOT 同样可以开火
        // 枪械 / 霰弹（必须真实可见才开火，换弹中不开火）
        if (target && shouldShoot && canSee && !bot.reloading &&
            def.cls != WeaponClass::Melee && def.cls != WeaponClass::Grenade) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;

            if (bot.shootCooldown <= 0.0f) {
                if (engageDist < def.range + 200.0f) {
                    if (bot.ammo <= 0) {
                        // 还有备弹就换弹；弹匣+备弹都空则本帧不开火（换枪逻辑在上方处理）
                        if (bot.reserve > 0) StartReload(bot, audio);
                        continue;
                    }

                    // 命中倾向：30% 精确（小抖动），70% 打偏（大散布）
                    std::uniform_real_distribution<float> hitRoll(0.0f, 1.0f);
                    const bool precise = hitRoll(rng_) < 0.30f;
                    const float base = precise ? 0.08f : 0.8f;
                    // 听到下包/拆包声而锁定目标时略微提高命中率（散布更小）
                    if (heardSound) {
                        spreadMul *= sm.isPlant ? Tuning::PlantHearSpreadMul
                                                : Tuning::DefuseHearSpreadMul;
                    }
                    spreadMul *= 1.0f + Tuning::AimSettleMul * std::exp(-ai.settleTimer / perception.settle);
                    std::uniform_real_distribution<float> spreadDist(
                        -base * spreadMul, base * spreadMul);

                    bot.ApplyRecoil(def.recoilKickDeg);
                    const float aimBase = std::atan2(target->y - bot.y, target->x - bot.x) + bot.aimKick;
                    const int pellets = std::max(1, def.pellets);
                    for (int p = 0; p < pellets; ++p) {
                        FireBullet(bot.x, bot.y, aimBase + spreadDist(rng_), def,
                                   static_cast<int>(i), bot.team);
                    }
                    bot.ammo--;
                    bot.shootCooldown = def.cooldown * 1.8f * shootCooldownMul;
                    bot.shootAnimTimer = 0.12f;

                    // 枪声传播：附近敌人按武器类别/消音器得到不同半径
                    EmitSound(SoundEventType::Gunshot, bot.x, bot.y, bot.team,
                              static_cast<int>(i), GunshotRadius(def));
                    if (audio) {
                        audio->PlayWeaponFireAt(def.id, bot.x, bot.y,
                                                ListenerX(), ListenerY());
                    }
                }
            }
        }
        // 近战
        else if (target && def.cls == WeaponClass::Melee && target->alive &&
                 engageDist < meleeReachNow &&
                 visibility_.HasLineOfSight(bot.x, bot.y, target->x, target->y)) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;
            if (bot.shootCooldown <= 0.0f) {
                if (target == &player_) {
                    if (ApplyDamage(player_, static_cast<int>(def.damage * 0.33f), WeaponDB::ArmorPen(def.id))) {
                        OnCombatantKilled(player_, audio, &bot);
                    }
                }
                bot.shootCooldown = def.cooldown * 1.5f * shootCooldownMul;
                bot.shootAnimTimer = 0.12f;
                EmitSound(SoundEventType::KnifeSwing, bot.x, bot.y, bot.team,
                          static_cast<int>(i));
            }
        }

        // 决策 trace（dev-only）：只在决策变化时写一行，关闭时零开销
        TraceBotDecision(i, target);
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

