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
            return true;
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
    // 绕行状态：卡住后沿侧向方向走一小段时间（不再瞬移）
    if (ai.detourTimer > 0.0f) {
        ai.detourTimer -= dt;
        bot.vx = ai.detourX * speed;
        bot.vy = ai.detourY * speed;
        MoveWithCollision(bot, dt);
        return true;
    }

    // ---- 角点转向：直行被墙挡住时，依次尝试左右偏转 ----
    // 原来只会朝路点直线走，撞墙就把速度清零（然后 0.7s 后瞬移），
    // 在拐角处表现为「卡住 → 向后瞬移 → 再卡住」的死循环。
    float dirX = dx / d, dirY = dy / d;
    {
        const float half = bot.size * 0.5f;
        const float probe = std::max(8.0f, speed * dt * 2.0f);
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

    bot.vx = dirX * speed;
    bot.vy = dirY * speed;

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

    // 朝向：只在真正移动时朝「速度方向」，并限制转向速率。
    // 之前朝航点算，航点落在身后时会让 BOT 一直转圈找方向。
    const float speed2 = bot.vx * bot.vx + bot.vy * bot.vy;
    if (speed2 > 25.0f) {
        const float desired = std::atan2(bot.vy, bot.vx)
                            + std::sin(ai.scanPhase * 2.2f) * Tuning::BotScanAmplitudeRad;
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
    while (botExecutors_.size() < bots_.size()) botExecutors_.emplace_back();

    // 小队协同：刷新集火目标（内部 0.5s 节流）
    UpdateSquadFocus(dt);

    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& bot = bots_[i];
        if (!bot.alive) continue;
        auto& ai = AiAt(i);
        ai.scanPhase += dt;
        if (ai.wanderTimer > 0.0f) ai.wanderTimer -= dt;


        // 分工指令
        SquadOrder order;
        if (i < squad_.size()) order = squad_[i];

        // ---- 正在拆包：原地不动（进度由 UpdateObjective 推进）----
        if (ai.defusing) {
            bot.vx = bot.vy = 0.0f;
            ai.llmIntent.Tick(dt);
            continue;
        }

        // ---- 原地扫描：没有目标时缓慢转身 ----
        // 视野是 120° 扇形，若不扫描，背对着敌人的 BOT 会永远发现不了对方
        if (!ai.hadTarget && bot.vx == 0.0f && bot.vy == 0.0f) {
            // 原地扫视：绕「进入空闲时的朝向」左右 ±60° 摆动。
            // 中心只在进入空闲时锁定一次，否则 base 会跟着摆角漂移 → 变成整圈旋转。
            // 原先是 aimAngle += 角速度（360° 连续自转），看起来像原地转圈。
            if (!ai.scanBaseValid) {
                ai.scanBase = bot.aimAngle;
                ai.scanBaseValid = true;
            }
            bot.aimAngle = ai.scanBase +
                           std::sin(ai.scanPhase * 1.6f) * 1.05f;   // ±60°
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
                ai.llmIntent.Tick(dt);
                if (FollowPathTo(bot, ai, lastDeathX_, lastDeathY_, dt, 1.0f)) continue;
            }
        }

        // ---- 受击撤退：低血时优先找掩体，交火次之 ----
        if (ai.retreatTimer > 0.0f) ai.retreatTimer -= dt;
        if (ai.retreatTimer > 0.0f && bot.health <= Tuning::RetreatKeepHealth) {
            float covX = 0.0f, covY = 0.0f;
            if (FindCoverPoint(bot, ai.threatX, ai.threatY, covX, covY)) {
                ai.llmIntent.Tick(dt);
                if (bot.ammo <= 0 && !bot.reloading) StartReload(bot, audio);
                if (FollowPathTo(bot, ai, covX, covY, dt, 1.0f)) continue;
            }
        }

        // ---- 目标选择：只用「看得见」的敌人 + 团队情报 ----
        float targetDist = 0.0f;
        const Player* target = FindVisibleEnemy(bot, targetDist);

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
                        ai.planHoldX = nx;
                        ai.planHoldY = ny;
                        break;
                    }
                }
            }
        }

        // 教练计划：没有可见敌人时优先去计划的驻守点（有敌人时反应交给决策树）
        if (!target && ai.hasPlanHold) {
            const float planMul = (order.role == BotRole::Entry)   ? Tuning::SpeedMulEntry
                                : (order.role == BotRole::Anchor)  ? Tuning::SpeedMulAnchor
                                                                   : Tuning::SpeedMulSupport;
            if (FollowPathTo(bot, ai, ai.planHoldX, ai.planHoldY, dt, planMul)) {
                continue;
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

        const bool canSee = (target != nullptr);
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

        ai.llmIntent.Tick(dt);

        // 闪光致盲倒计时
        if (ai.blindTimer > 0.0f) ai.blindTimer = std::max(0.0f, ai.blindTimer - dt);

        // ---- 换弹决策 ----
        // 脱战就补满；交火中只在弹匣见底（<=25%）才打断射击去换弹
        if (!bot.reloading && bot.ammo < bot.maxAmmo) {
            const int lowAmmo = static_cast<int>(static_cast<float>(bot.maxAmmo) * Tuning::LowAmmoFraction);
            if (!canSee || bot.ammo <= lowAmmo) StartReload(bot, audio);
        }

        // ---- 手雷：有情报但无直接视野时，向情报点投掷 ----
        if (ai.grenadeCooldown > 0.0f) ai.grenadeCooldown -= dt;
        if (!target && intel && bot.grenadeCount > 0 && ai.grenadeCooldown <= 0.0f) {
            if (engageDist > Tuning::NadeThrowMin && engageDist < Tuning::NadeThrowMax) {
                ThrowGrenade(bot, std::atan2(aimY - bot.y, aimX - bot.x), audio);
                ai.grenadeCooldown = Tuning::NadeCooldown;
            }
        }

        // ---- 目标引导：A* 寻路推进 ----
        // 角色行进速度：突破手全速、支援略慢、断后明显靠后
        const float aggr = TempoAggression(GetCoachPlan(bot.team).tempo);
    const float roleMul = (order.role == BotRole::Entry)   ? Tuning::SpeedMulEntry
                            : (order.role == BotRole::Anchor)  ? Tuning::SpeedMulAnchor
                            :                                    Tuning::SpeedMulSupport;
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
                            rx = order.holdX + static_cast<float>(rng_() % 181) - Tuning::PatrolHoldSpread;
                            ry = order.holdY + static_cast<float>(rng_() % 181) - Tuning::PatrolHoldSpread;
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
                // 近距离且可见 → 交给战术执行器（冲刺/绕侧手感更好）
                const bool closeCombat = canSee && engageDist < Tuning::CloseCombatRange;
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

        // ---- 决策树姿态推导（取代原来的 per-BOT LLM 输出）----
        // 这里必然有可见敌人（上面对 !target 已 continue），
        // 所以姿态只在「强攻 / 固守 / 撤退 / 绕向掩体」之间选择。
        {
            BotIntent& it = ai.llmIntent;
            const WeaponDef& wdef = bot.CurrentWeapon();
            const SquadOrder& ord = squad_[i];

            const float hpFrac = (bot.maxHealth > 0)
                ? static_cast<float>(bot.health) / static_cast<float>(bot.maxHealth)
                : 1.0f;

            const bool mustRetreat = ai.blindTimer > 0.0f ||
                                     hpFrac * 100.0f < Tuning::RetreatHealth ||
                                     (bot.ammo <= 0 && !bot.reloading);

            if (mustRetreat) {
                it.action = BotIntent::Action::Retreat;
            } else if (ord.role == BotRole::Anchor ||
                       GetCoachPlan(bot.team).tempo == Tempo::Slow) {
                it.action = BotIntent::Action::Defensive;
            } else if (ord.role == BotRole::Entry ||
                       engageDist < Tuning::CloseCombatRange * 0.6f * aggr) {
                it.action = BotIntent::Action::Aggressive;
            } else if (ai.hasPlanHold && engageDist > wdef.range) {
                it.action = BotIntent::Action::MoveToCover;   // 借掩体接近
            } else {
                it.action = BotIntent::Action::Defensive;
            }

            // 射击纪律
            if (bot.reloading || bot.ammo <= 0) it.shootMode = BotIntent::ShootDiscipline::HoldFire;
            else if (engageDist > wdef.range * 0.8f) it.shootMode = BotIntent::ShootDiscipline::Burst;
            else it.shootMode = BotIntent::ShootDiscipline::Auto;

            it.targetX = aimX;
            it.targetY = aimY;
            it.fresh   = true;
            it.ageSec  = 0.0f;

            ai.stance = static_cast<int>(it.action);   // 供诊断用
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
        // 已经驻守到位且没有敌人：彻底站定，不参与避让推力。
        // 否则队友的分离力会每帧推一下 → 视觉上一直在抖。
        {
            const float gdx = ai.goalX - bot.x;
            const float gdy = ai.goalY - bot.y;
            const bool parked = !canSee &&
                                (gdx * gdx + gdy * gdy) < 18.0f * 18.0f;
            if (parked) {
                bot.vx = 0.0f;
                bot.vy = 0.0f;
            } else {
                float sepX = 0.0f, sepY = 0.0f;
                AccumulateSeparation(bot, sepX, sepY);
                ApplyAvoidance(bot.vx, bot.vy, sepX, sepY);
            }
        }

        // 持械移速上限（与玩家同一套武器倍率）
        {
            const float cap = Tuning::BotMoveSpeed * bot.CurrentWeapon().moveSpeedMul;
            const float sp = std::sqrt(bot.vx * bot.vx + bot.vy * bot.vy);
            if (sp > cap && sp > 0.001f) {
                bot.vx = bot.vx / sp * cap;
                bot.vy = bot.vy / sp * cap;
            }
        }

        MoveWithCollision(bot, dt);

        if (canSee) bot.aimAngle = out.aimAngle;

        const WeaponDef& def = bot.CurrentWeapon();

        // 结算展示期与回合中一致：BOT 同样可以开火
        // 枪械 / 霰弹（必须真实可见才开火，换弹中不开火）
        if (target && out.shouldShoot && canSee && !bot.reloading &&
            def.cls != WeaponClass::Melee && def.cls != WeaponClass::Grenade) {
            if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;

            if (bot.shootCooldown <= 0.0f) {
                if (engageDist < def.range + 200.0f) {
                    if (bot.ammo <= 0) { StartReload(bot, audio); continue; }

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
                                           ListenerX(), ListenerY());
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
                        OnCombatantKilled(player_, audio, &bot);
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

