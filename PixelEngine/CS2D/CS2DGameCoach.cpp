#include "CS2DSimulation.hpp"
#include "Tuning.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

// ============================================================================
// L3 主教练（当前为规则教练；接入 LLM 后 RuleCoach 作为兜底）
//
// 职责边界：只产出「战术意图 + 角色 + 区域」，不碰坐标 / 瞄准 / 开火
// ============================================================================

namespace {

bool SameNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

const char* IntentKey(PlanIntent i) {
    switch (i) {
        case PlanIntent::RushA:         return "rush_a";
        case PlanIntent::RushB:         return "rush_b";
        case PlanIntent::SplitA:        return "split_a";
        case PlanIntent::SplitB:        return "split_b";
        case PlanIntent::StackA:        return "stack_a";
        case PlanIntent::StackB:        return "stack_b";
        case PlanIntent::RetakeA:       return "retake_a";
        case PlanIntent::RetakeB:       return "retake_b";
        case PlanIntent::Save:          return "save";
        case PlanIntent::FakeThenOther: return "fake";
        case PlanIntent::RotateA:       return "rotate_a";
        case PlanIntent::RotateB:       return "rotate_b";
        default:                        return "default";
    }
}

const char* TempoKey(Tempo tt) {
    switch (tt) {
        case Tempo::Fast: return "fast";
        case Tempo::Slow: return "slow";
        default:          return "normal";
    }
}

const char* IntentName(PlanIntent i) {
    switch (i) {
        case PlanIntent::RushA:         return "速推A";
        case PlanIntent::RushB:         return "速推B";
        case PlanIntent::SplitA:        return "夹击A";
        case PlanIntent::SplitB:        return "夹击B";
        case PlanIntent::StackA:        return "堆A";
        case PlanIntent::StackB:        return "堆B";
        case PlanIntent::RetakeA:       return "回防A";
        case PlanIntent::RetakeB:       return "回防B";
        case PlanIntent::RotateA:       return "转点A";
        case PlanIntent::RotateB:       return "转点B";
        case PlanIntent::Save:          return "保枪";
        case PlanIntent::FakeThenOther: return "假打转点";
        default:                        return "默认分兵";
    }
}

} // namespace

CS2DSimulation::CoachState& CS2DSimulation::CoachFor(Team t) {
    return (t == Team::T) ? coachT_ : coachCT_;
}
const CS2DSimulation::CoachState& CS2DSimulation::CoachFor(Team t) const {
    return (t == Team::T) ? coachT_ : coachCT_;
}

const TeamPlan& CS2DSimulation::GetCoachPlan(Team t) const { return CoachFor(t).plan; }
const std::string& CS2DSimulation::GetCoachReason(Team t) const { return CoachFor(t).reason; }

// ---------------------------------------------------------------------------
// 区域名 → 中心点（按面积加权）
// ---------------------------------------------------------------------------
bool CS2DSimulation::AreaCenterByName(const std::string& name, float& ox, float& oy) const {
    if (!world_ || name.empty()) return false;
    for (const auto& a : world_->GetAreas()) {
        if (!SameNoCase(a.name, name)) continue;
        float sumW = 0.0f, cx = 0.0f, cy = 0.0f;
        for (const auto& r : a.rects) {
            const float w = std::max(1.0f, r.width * r.height);
            cx += (r.x + r.width * 0.5f) * w;
            cy += (r.y + r.height * 0.5f) * w;
            sumW += w;
        }
        if (sumW <= 0.0f) return false;
        ox = cx / sumW;
        oy = cy / sumW;
        return true;
    }
    return false;
}

// 在现有区域里找一个名字包含关键词的区域（不区分大小写）
std::string CS2DSimulation::AreaHint(const std::string& key) const {
    if (!world_ || key.empty()) return std::string();
    // 1) 战术表优先：每张图的实际区域命名不同（Dust2 的 Long/Short、Mirage 的 A_Site 等），
    //    由 assets/config/tactics/<map>.ini 描述，新图接入不用改代码。
    {
        std::string mapped;
        if (tactics_.Area(key, mapped)) {
            for (const auto& a : world_->GetAreas()) {
                if (SameNoCase(a.name, mapped)) return a.name;
            }
        }
    }
    std::string want = key;
    std::transform(want.begin(), want.end(), want.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // 1) exact match first (Long must not become Outside_Long)
    for (const auto& a : world_->GetAreas()) {
        std::string n = a.name;
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (n == want) return a.name;
    }
    // 2) substring fallback
    for (const auto& a : world_->GetAreas()) {
        std::string n = a.name;
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (n.find(want) != std::string::npos) return a.name;
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// 事件入队（0.4s 合并窗口，取最高优先级）
// ---------------------------------------------------------------------------
void CS2DSimulation::CoachOnEvent(Team t, CoachEventType type, const std::string& area) {
    CoachState& st = CoachFor(t);

    CoachEvent ev;
    ev.type  = type;
    ev.area  = area;
    ev.atSec = clockSec_;

    switch (type) {
        case CoachEventType::RoundStart:    ev.priority = 100.0f; break;
        case CoachEventType::BombPlanted:   ev.priority = 95.0f;  break;
        case CoachEventType::DefuseStarted: ev.priority = 90.0f;  break;
        case CoachEventType::HumanDied:     ev.priority = 85.0f;  break;
        case CoachEventType::EnemyDown:     ev.priority = 80.0f;  break;
        case CoachEventType::AllyDown:      ev.priority = 78.0f;  break;
        case CoachEventType::EnemySpotted:  ev.priority = 70.0f;  break;
        case CoachEventType::MatchPoint:    ev.priority = 60.0f;  break;
        case CoachEventType::TimeLow:       ev.priority = 55.0f;  break;
        case CoachEventType::EconomyLow:    ev.priority = 40.0f;  break;
        case CoachEventType::DefuseAborted: ev.priority = 35.0f;  break;
        default:                            ev.priority = 50.0f;  break;
    }

    st.pending.push_back(ev);
    if (st.mergeWindow <= 0.0f) st.mergeWindow = 0.4f;
}

// ---------------------------------------------------------------------------
// 每帧推进
// ---------------------------------------------------------------------------
void CS2DSimulation::CoachUpdate(float dt) {
    CoachDrainLlm();   // 先收 LLM 回包，再处理新事件

    // 转点评估：每 0.75 秒看一次（不依赖事件，事件可能被 KEEP 逻辑吃掉）
    if (mode_ == MatchMode::BombDefusal && !bombPlanted_ && round_.IsLive()) {
        rotateCheckTimer_ += dt;
        if (rotateCheckTimer_ >= 0.75f) {
            rotateCheckTimer_ = 0.0f;
            CoachTryRotate(Team::T);
        }
    }

    // 转点中止：时间不够或突然少两个以上，撤回原目标（避免送人头）
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        if (!st.plan.valid) continue;
        const bool rotating = (st.plan.intent == PlanIntent::RotateA ||
                               st.plan.intent == PlanIntent::RotateB);
        if (!rotating) continue;

        const int mine = AliveCount(t);
        const int their = AliveCount(TeamManager::Opponent(t));
        if (bombPlanted_ || !round_.IsLive() ||
            round_.PhaseRemaining() < 25.0f || mine + 2 <= their) {
            std::cout << "[Rotate] " << TeamName(t) << " 中止(";
            if (bombPlanted_)                          std::cout << "已下包";
            else if (!round_.IsLive())                 std::cout << "非交战期";
            else if (round_.PhaseRemaining() < 25.0f)  std::cout << "时间不足";
            else                                       std::cout << "人数劣势";
            std::cout << ")，回到 " << (st.rotateFrom.empty() ? "-" : st.rotateFrom)
                      << std::endl;

            st.plan.intent = PlanIntent::Default;
            st.plan.source = "rule";        // 转点中止属于规则改写，不算 LLM 计划
            st.plan.latencyWall = 0.0f;
            if (!st.rotateFrom.empty()) st.plan.target = st.rotateFrom;
            st.plan.valid     = true;
            st.plan.expiresAt = clockSec_ + 10.0f;
            st.planExpiresAt  = st.plan.expiresAt;
            coachPlanChangedAt_ = clockSec_;
            CoachApplyPlan(t);
        }
    }


    // 条件分支：到点自动切换战术（时间差打法）
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        if (st.plan.valid && st.plan.hasBranch &&
            clockSec_ - st.plan.createdAt >= st.plan.branchAfterSec) {
            TeamPlan next = st.plan;
            next.hasBranch   = false;            // 只触发一次
            next.intent      = st.plan.branchIntent;
            next.tempo       = st.plan.branchTempo;
            if (!st.plan.branchTarget.empty()) next.target = st.plan.branchTarget;
            next.createdAt   = clockSec_;
            next.expiresAt   = clockSec_ + 20.0f;
            st.planExpiresAt = next.expiresAt;

            // 按新意图重新分配区域
            TeamPlan shaped = RuleCoach(t, CoachEvent{CoachEventType::RoundStart, 0.0f,
                                                      clockSec_, std::string()});
            shaped.intent    = next.intent;
            shaped.target    = next.target;
            shaped.tempo     = next.tempo;
            shaped.hasBranch = false;
            // P2-4: the branched plan must execute the branched intent, not just
            // carry its label -- re-resolve slots from the branch intent's template
            // (Rotate/Fake/Rush). A MID branch falls back to the MID-control shape.
            // No re-shape once the bomb is planted: the fresh plan is already a
            // post-plant Stack/Retake plan and must not be overwritten by an attack.
            if (!bombPlanted_) {
                shaped.templateDefault = true;
                ApplyTacticsSlots(t, shaped);
                if (shaped.intent == PlanIntent::Default && shaped.target == "MID") {
                    // 5b: ApplyTacticsSlots above already applied the ini template
                    // ([slots.<team>.Default@MID]) when the map ships one -- that is
                    // the single source of truth (mirage: 4-mid + B-only anchor).
                    // Only fall back to the hardcoded old 3-mid shape when the map
                    // has no such template; never clobber the ini shape.
                    const char* branchTeamKey = (t == Team::T) ? "T" : "CT";
                    if (!tactics_.Slots(branchTeamKey, "Default@MID")) {
                        const std::string midArea = AreaHint("Mid");
                        const std::string aSiteArea = AreaHint("A_Site");
                        const std::string bSiteArea = AreaHint("B_Site");
                        shaped.slots[0] = { CoachRole::Entry,   midArea,   true, true  };
                        shaped.slots[1] = { CoachRole::Support, midArea,   true, true  };
                        shaped.slots[2] = { CoachRole::Anchor,  aSiteArea, true, false };
                        shaped.slots[3] = { CoachRole::Anchor,  bSiteArea, true, false };
                        shaped.slots[4] = { CoachRole::Lurk,    midArea,   true, true  };
                        shaped.slots[0].lookArea = aSiteArea;
                        shaped.slots[1].lookArea = bSiteArea;
                        shaped.slots[2].lookArea = "CT_Spawn";
                        shaped.slots[3].lookArea = "CT_Spawn";
                        shaped.slots[4].lookArea = "CT_Spawn";
                    }
                }
            }
            shaped.gen       = next.gen;
            shaped.createdAt = next.createdAt;
            shaped.expiresAt = next.expiresAt;
            shaped.valid     = true;
            shaped.reason    = "时间差：切换到 " + std::string(IntentName(next.intent));

            st.plan   = shaped;
            st.reason = shaped.reason;
            coachPlanChangedAt_ = clockSec_;
            std::cout << "[Coach] " << TeamName(t) << " 分支触发 -> "
                      << IntentName(shaped.intent) << std::endl;
            CoachApplyPlan(t);
        }
    }

    // 计划到期：默认续期（没有新信息就不该换指令）；
    // 只有"沉默"超过 SilentSeconds 才主动重新评估一次（交给规则/LLM 思考下一步）。
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        if (!(st.plan.valid && st.planExpiresAt > 0.0f && clockSec_ > st.planExpiresAt)) continue;

        const bool silentTooLong =
            (clockSec_ - st.plan.createdAt) >= Tuning::ai::SilentSeconds;
        if (!silentTooLong) {
            st.planExpiresAt  = clockSec_ + Tuning::ai::IntentRenewSec;
            st.plan.expiresAt = st.planExpiresAt;
            ++counters_.renewals;
            std::cout << "[Coach] " << TeamName(t) << " 计划续期（无新信息）"
                      << " intent=" << IntentName(st.plan.intent) << std::endl;
            continue;
        }
        {
            st.planExpiresAt = clockSec_ + Tuning::ai::IntentRenewSec;
            CoachEvent ev;
            ev.type = CoachEventType::RoundStart;
            ev.priority = 0.0f;
            ev.atSec = clockSec_;
            ++st.gen;
            TeamPlan p = RuleCoach(t, ev);
            p.gen = st.gen;
            p.createdAt = clockSec_;
            p.expiresAt = st.planExpiresAt;
            p.valid = true;
            st.plan = p;
            st.reason = p.reason + "（沉默期，重新评估）";
            st.stickyLlm = false; // plan expiry sticky clear
            coachPlanChangedAt_ = clockSec_;
            std::cout << "[Coach] " << TeamName(t) << " 沉默期重评估 -> "
                      << IntentName(p.intent) << std::endl;
            CoachApplyPlan(t);
        }
    }

    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);

        if (st.mergeWindow > 0.0f) {
            st.mergeWindow -= dt;
            if (st.mergeWindow > 0.0f) continue;
        }
        // 低频心跳：战斗中每 6 秒给一次「例行检查」，保证计划不会僵住
        if (st.lastThinkAt > 0.0f && clockSec_ - st.lastThinkAt > 8.0f &&
            round_.IsLive() && st.pending.empty() &&
            clockSec_ - st.plan.createdAt > 10.0f) {
            st.lastThinkAt = clockSec_;
            CoachEvent hb;
            hb.type = CoachEventType::Heartbeat;
            hb.priority = 30.0f;
            hb.atSec = clockSec_;
            st.pending.push_back(hb);
            st.mergeWindow = 0.0f;
        }

        if (st.pending.empty()) continue;

        auto best = std::max_element(st.pending.begin(), st.pending.end(),
                                     [](const CoachEvent& a, const CoachEvent& b) {
                                         return a.priority < b.priority;
                                     });
        const CoachEvent ev = *best;
        st.pending.clear();

        if (st.callsThisRound >= 8) continue;   // 每队每回合上限 8 次
        ++st.callsThisRound;
        CoachThink(t, ev);
    }
}

void CS2DSimulation::CoachThink(Team t, const CoachEvent& ev) {
    CoachState& st = CoachFor(t);

    // P2-2: the last 20s of a round without a plant is as critical as TimeLow --
    // a replan here must rush (T) / hold sites (CT), never drift back to Default.
    const bool lateRound = !bombPlanted_ && round_.IsLive() && GetRoundRemaining() <= 20.0f;
    const bool criticalEv =
        ev.type == CoachEventType::RoundStart ||
        ev.type == CoachEventType::BombPlanted ||
        ev.type == CoachEventType::DefuseStarted ||
        ev.type == CoachEventType::TimeLow ||
        ev.type == CoachEventType::EconomyLow ||
        ev.type == CoachEventType::MatchPoint ||
        ev.type == CoachEventType::HumanDied || lateRound;

    // P1-1: EnemyDown/AllyDown is no longer a soft event -- a manpower change is
    // a plan re-evaluation trigger (re-score into press/contract plans, not just
    // a tempo tweak). Kill spam was flipping reason every death while intent
    // stayed Default split; that is now held back by the manpower replan
    // cooldown below, the T 18s execution lock, and the change budget.
    const bool manpowerEv =
        ev.type == CoachEventType::EnemyDown ||
        ev.type == CoachEventType::AllyDown;
    // Soft events: keep a still-valid plan (skip full replan (sticky soft)).
    const bool softEv =
        ev.type == CoachEventType::Heartbeat ||
        ev.type == CoachEventType::EnemySpotted ||
        ev.type == CoachEventType::DefuseAborted;

    if (st.plan.valid && clockSec_ < st.planExpiresAt && !bombPlanted_ && softEv) {
        // T16：减员变速解拦截——soft 事件且计划有效时，意图不变、tempo 可调。
        const Tempo before = st.plan.tempo;
        if (ev.type == CoachEventType::EnemyDown) {
            st.plan.tempo = Tempo::Fast;
        } else if (ev.type == CoachEventType::AllyDown) {
            st.plan.tempo = Tempo::Slow;
        }
        if (st.plan.tempo != before) {
            std::cout << "[Coach] " << TeamName(t) << " 减员变速 KEEP "
                      << IntentName(st.plan.intent)
                      << " tempo " << TempoKey(before) << "->" << TempoKey(st.plan.tempo)
                      << " (intent 不变)" << std::endl;
        }
        if (st.callsThisRound > 0) --st.callsThisRound;
        std::cout << "[Coach] " << TeamName(t) << " KEEP plan "
                  << IntentName(st.plan.intent)
                  << " target=" << (st.plan.target.empty() ? "-" : st.plan.target)
                  << " (" << (st.stickyLlm ? "llm" : "rules")
                  << " unexpired, ev#" << static_cast<int>(ev.type)
                  << " no-replan)" << std::endl;
        return;
    }

    // LLM sticky: block RuleCoach overwrite until expiry / critical / plant.
    if (st.stickyLlm && st.plan.valid && clockSec_ < st.planExpiresAt &&
        !bombPlanted_ && !criticalEv) {
        if (st.callsThisRound > 0) --st.callsThisRound;
        std::cout << "[Coach] " << TeamName(t) << " KEEP LLM plan "
                  << IntentName(st.plan.intent)
                  << " (unexpired, ev#" << static_cast<int>(ev.type)
                  << " non-critical)" << std::endl;
        return;
    }

    // T 未下包：主攻侧一旦确定，给 18 秒执行窗口。
    // 这段时间内 HumanDied / 减员 / 心跳都不能把 T 拉到另一侧；
    // 只有回合开始、经济崩（save）、时间不足、赛点、下包这些事件可以重规划。
    if (t == Team::T && !bombPlanted_ && st.plan.valid &&
        (st.plan.target == "A" || st.plan.target == "B") &&
        clockSec_ - st.plan.createdAt < 18.0f &&
        ev.type != CoachEventType::RoundStart &&
        ev.type != CoachEventType::EconomyLow &&
        ev.type != CoachEventType::TimeLow &&
        ev.type != CoachEventType::MatchPoint &&
         ev.type != CoachEventType::BombPlanted && !lateRound) {
        if (st.callsThisRound > 0) --st.callsThisRound;
        std::cout << "[Coach] T 执行锁定 " << st.plan.target
                  << " (ev#" << static_cast<int>(ev.type) << " no-replan)" << std::endl;
        return;
    }

    // ---- 变更预算：意图至少保持 IntentMinHold，每回合最多 IntentMaxChanges 次 ----
    // 治"无头苍蝇"的核心：没有新信息就不换指令（新信息由 criticalEv 放行）。
    // P1-1: manpower events use their own cooldown instead of
    // IntentMinHold/IntentChangeCooldown -- a manpower replan usually keeps
    // intent Default, so it never moves lastIntentChangeSec; without its own
    // timer, back-to-back deaths would replan every time.
    if (!criticalEv && st.plan.valid && !bombPlanted_) {
        const float held = clockSec_ - st.plan.createdAt;
        const bool tooSoon = manpowerEv
            ? (clockSec_ - st.lastManpowerReplanSec) < Tuning::ai::ManpowerReplanCooldown
            : (held < Tuning::ai::IntentMinHold ||
               (clockSec_ - st.lastIntentChangeSec) < Tuning::ai::IntentChangeCooldown);
        const bool budgetOut = st.changesThisRound >= Tuning::ai::IntentMaxChanges;
        if (tooSoon || budgetOut) {
            st.planExpiresAt  = clockSec_ + Tuning::ai::IntentRenewSec;
            st.plan.expiresAt = st.planExpiresAt;
            ++counters_.renewals;
            if (st.callsThisRound > 0) --st.callsThisRound;
            // T05：预算拦截写进结构化日志（guards 字段，验收检测用）。
            TraceWiring(t, budgetOut ? "budget-blocked" : "min-hold", "", nullptr);
            std::cout << "[Coach] " << TeamName(t) << " 变更预算拦截（"
                      << (budgetOut ? "次数已满" : "保持期未到")
                      << "）keep " << IntentName(st.plan.intent) << std::endl;
            return;
        }
    }

    ++st.gen;
    TeamPlan plan = RuleCoach(t, ev);

    // ---- C4 相关决策（规则层）----
    if (mode_ == MatchMode::BombDefusal && !bombPlanted_) {
        if (bombDropped_) {
            const std::string area = world_ ? world_->FindAreaAt(bombDropX_, bombDropY_)
                                            : std::string();
            if (t == Team::T) {
                // T：C4 掉了，先派最近的队友去捡
                plan.intent = PlanIntent::Default;
                plan.target = area;
                plan.tempo  = Tempo::Fast;
                plan.reason = "C4掉落，先取回再下包";
            } else if (bombIntel_) {
                // CT：已知 C4 掉落位置 → 去卡那个区域（T29：按实际掉落侧发 Stack，不再写死 StackA）
                const std::string ia = world_ ? world_->FindAreaAt(bombIntelX_, bombIntelY_)
                                              : std::string();
                const char dropSide = AreaSideOf(ia);
                plan.intent = (dropSide == 'B') ? PlanIntent::StackB : PlanIntent::StackA;
                plan.target = ia;
                plan.tempo  = Tempo::Normal;
                plan.reason = "发现掉落C4，收缩卡点";
            }
        }
    }
    plan.gen       = st.gen;
    plan.createdAt = clockSec_;
    plan.valid     = true;

    // T08：规则计划也过消毒（防重复压点、CT 无情报 stack 兜底）。
    // 已下包时 SanitizeCoachPlan 内部直接返回，不会误伤 retake/stack。
    // 但「反应型」critical 事件（TimeLow/MatchPoint 冲有接触侧）不能再被消毒拉回
    // 默认分兵——那会把"时间不足抓紧进攻/赛点强打"改僵（双重整形，T19 验收点因此失真）。
    const bool reactiveCritical =
        ev.type == CoachEventType::TimeLow || ev.type == CoachEventType::MatchPoint;
    if (!bombPlanted_ && !reactiveCritical) SanitizeCoachPlan(t, plan);

    plan.expiresAt = clockSec_ + 20.0f;   // 20 秒后重新评估
    st.planExpiresAt = plan.expiresAt;
    st.plan   = plan;
    st.reason = plan.reason;
    st.stickyLlm = false;          // rules own this plan
    // 中段规则重算时同步购买档（Save→Eco；否则按当前金钱再判）
    st.buyPlan = (plan.intent == PlanIntent::Save) ? BuyPlan::Eco : DecideBuyPlan(t);
    st.lastThinkAt = clockSec_;
    coachPlanChangedAt_ = clockSec_;
    if (manpowerEv) st.lastManpowerReplanSec = clockSec_;   // P1-1：减员重规划计时
    {   // 规则计划也算「上条指令」，让 LLM 知道自己上一手是什么
        std::ostringstream o;
        o << static_cast<int>(plan.intent);
        if (!plan.target.empty()) o << "->" << plan.target;
        st.lastOrder = o.str();
    }

    std::cout << "[Coach] " << TeamName(t) << " 第" << st.callsThisRound << "次"
              << " 事件#" << static_cast<int>(ev.type)
              << " -> " << IntentName(plan.intent)
              << " 目标=" << (plan.target.empty() ? "-" : plan.target)
              << " | " << plan.reason << std::endl;

        // 记录最近进攻侧（供转点判断用）
    if (t == Team::T && (plan.target == "A" || plan.target == "B")) {
        st.lastAttackSide = plan.target;
    }

    CoachApplyPlan(t);

    // 信息驱动转点：规则已出计划，若有充分情报再改打另一侧
    if (mode_ == MatchMode::BombDefusal && !bombPlanted_ && t == Team::T) {
        CoachTryRotate(t);
    }


    // 规则计划先落地（保证零延迟），再异步向 LLM 要一份更好的
    CoachTryLlm(t, ev);
}

// ---------------------------------------------------------------------------
// 规则教练
// ---------------------------------------------------------------------------
static const char* TacticsIntentKey(PlanIntent intent) {
    switch (intent) {
        case PlanIntent::RushA:   return "RushA";
        case PlanIntent::RushB:   return "RushB";
        case PlanIntent::SplitA:  return "SplitA";
        case PlanIntent::SplitB:  return "SplitB";
        case PlanIntent::StackA:  return "StackA";
        case PlanIntent::StackB:  return "StackB";
        case PlanIntent::RetakeA: return "RetakeA";
        case PlanIntent::RetakeB: return "RetakeB";
        case PlanIntent::Save:    return "Save";
        case PlanIntent::RotateA: return "RotateA";
        case PlanIntent::RotateB: return "RotateB";
        case PlanIntent::FakeThenOther: return "Fake";
        default:                  return "Default";
    }
}

// 战术表覆盖：表里存在该 (队伍, 意图) 的槽位配置时，用它替换代码里的默认分兵。
// 表里没有就保持代码默认 → 战术表是可选增强，不会让旧图行为突变。
// T04：先找 target 专属 @ 模板（T.SplitA@A），再退回 plain（T.SplitA）——与 ApplySlotTemplate 对齐。
void CS2DSimulation::ApplyTacticsSlots(Team t, TeamPlan& plan) {
    if (!tactics_.loaded) return;
    const char* teamKey = (t == Team::T) ? "T" : "CT";
    const std::string intentKey = TacticsIntentKey(plan.intent);
    // P1-2: an explicit @Target template still wins wholesale (T04 behaviour).
    if (const auto* slots = tactics_.Slots(teamKey, intentKey + "@" + plan.target)) {
        if (!plan.templateDefault) return;   // only generic split templates are covered; event-shaped plans stay as-is
        for (size_t i = 0; i < plan.slots.size(); ++i) {
            plan.slots[i] = (*slots)[i];
            plan.slots[i].area = tactics_.ResolveArea(plan.slots[i].area);
        }
        return;
    }
    const auto* slots = tactics_.Slots(teamKey, intentKey);
    if (!slots) return;
    if (!plan.templateDefault) return;   // only generic split templates are covered; event-shaped plans stay as-is
    if (plan.intent == PlanIntent::Default) {
        // P1-2: no Default@<target> template (e.g. Default@MID): do NOT replace
        // the slots RuleCoachCore already built (MID control). Keep role/area,
        // only fill fields the built slots are still missing (look/util/hold).
        for (size_t i = 0; i < plan.slots.size(); ++i) {
            const SlotOrder& tpl = (*slots)[i];
            SlotOrder& so = plan.slots[i];
            if (so.lookArea.empty() && so.lookAngle <= -90.0f) {
                so.lookArea = tpl.lookArea;
                so.lookAngle = tpl.lookAngle;
            }
            if (so.util == 0 && tpl.util != 0) {
                so.util = tpl.util;
                so.utilArea = tpl.utilArea;
            }
            if (so.hold == HoldStyle::Default && tpl.hold != HoldStyle::Default) {
                so.hold = tpl.hold;
            }
        }
        return;
    }
    for (size_t i = 0; i < plan.slots.size(); ++i) {
        plan.slots[i] = (*slots)[i];
        plan.slots[i].area = tactics_.ResolveArea(plan.slots[i].area);
    }
}

TeamPlan CS2DSimulation::RuleCoach(Team t, const CoachEvent& ev) {
    TeamPlan plan = RuleCoachCore(t, ev);
    plan.source = "rule";     // 评估用：区分规则计划与 LLM 计划
    ApplyTacticsSlots(t, plan);
    return plan;
}

TeamPlan CS2DSimulation::RuleCoachCore(Team t, const CoachEvent& ev) {
    CoachState& st = CoachFor(t);
    TeamPlan plan;
    plan.intent = PlanIntent::Default;

    const bool isT = (t == Team::T);

    auto assign = [&](const std::string& a1, const std::string& a2,
                      const std::string& b1, const std::string& b2,
                      const std::string& mid) {
        plan.slots[0] = { CoachRole::Entry,   a1,  true, false };
        plan.slots[1] = { CoachRole::Support, a2,  true, false };
        plan.slots[2] = { CoachRole::Lurk,    mid, true, true  };
        plan.slots[3] = { CoachRole::Anchor,  b1,  true, false };
        plan.slots[4] = { CoachRole::Support, b2,  true, false };
        // 通用分兵模板：允许被 assets/config/tactics/<map>.ini 的 [slots.*] 覆盖
        plan.templateDefault = true;
    };

    // 侧翼区域按「关键词 → 找不到就退回本侧包点」解析：
    // dust_2 的图层名是 Long/Short/Tunnel，其它地图（如 mirage: A_Site/B_Site/Mid）
    // 未必有这些区域，回退后 BOT 至少还能拿到有效驻守点，不会退化成无计划游走。
    const std::string aSite  = AreaHint("A_Site");
    const std::string bSite  = AreaHint("B_Site");
    const std::string mid    = AreaHint("Mid");

    auto areaOr = [](const std::string& prefer, const std::string& fallback) {
        return prefer.empty() ? fallback : prefer;
    };
    const std::string aLong  = areaOr(AreaHint("Long"),  aSite);
    const std::string aShort = areaOr(AreaHint("Short"), aSite);
    const std::string bLong  = areaOr(AreaHint("Tunnel"), bSite);

    // 当前包点区域（未下包就用 A 点占位，避免空字符串）
    const std::string bombArea =
        bombPlanted_ && world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
    const std::string siteArea = bombArea.empty() ? aSite : bombArea;
    // P0-1：下包后 T 的槽位一律站包点外围、看向包点。
    //   area 取入口通道 / 中路（aLong/aShort/bLong/mid 均经 tactics [areas] 映射到
    //   各图的真实区域），lookArea=siteArea 让外围架枪位始终看着包点——
    //   撤的是位置不是防守。只改 T 下包后的槽位，CT 回防（要贴身拆包）不动。
    auto holdSlot = [&](CoachRole role, const std::string& area, bool canRotate) {
        SlotOrder so;
        so.role = role;
        so.area = area;
        so.canTrade = true;
        so.canRotate = canRotate;
        so.lookArea = siteArea;
        return so;
    };

    // ================= 下包后的战术姿态（任何事件都按当前局势重算）=================
    // 参考公开攻略的通用原则：
    //   T ：按用途分散守包（拆包角度 / 回防路线 / 延迟 peek），交叉火力；
    //       残局少打多不硬拼，守包等 CT 上手拆包再抓；时间是 T 的盟友。
    //   CT：先集结再进，utility 先清点，两人互换；拆包需队友掩护；
    //       时间不够（5 秒拆包）就强拆。
    if (bombPlanted_) {
        std::string lower = siteArea;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool onB = (lower.find("b_") != std::string::npos ||
                          lower.find("b site") != std::string::npos);
        const float bombLeft = GetBombTimer();
        const int aliveT  = AliveCount(Team::T);
        const int aliveCT = AliveCount(Team::CT);

        if (isT) {
            if (aliveT < aliveCT) {
                plan.intent = onB ? PlanIntent::StackB : PlanIntent::StackA;
                plan.target = onB ? "B" : "A";
                plan.tempo  = Tempo::Normal;
                plan.reason = "人数落后，守包点架枪等拆包声";
            } else {
                plan.intent = onB ? PlanIntent::StackB : PlanIntent::StackA;
                plan.target = onB ? "B" : "A";
                plan.tempo  = (bombLeft < 15.0f) ? Tempo::Fast : Tempo::Normal;
                plan.reason = "包已下：交叉火力守包";
            }
            plan.slots[0] = holdSlot(CoachRole::Anchor,  onB ? bLong : aLong, false);
            plan.slots[1] = holdSlot(CoachRole::Support, onB ? mid : aShort, false);
            plan.slots[2] = holdSlot(CoachRole::Lurk,    onB ? bLong : aLong, true);
            plan.slots[3] = holdSlot(CoachRole::Entry,   onB ? bLong : aShort, false);
            plan.slots[4] = holdSlot(CoachRole::Support, mid, false);
            return plan;
        }

        // T20：钳子感知——有钳 4s 拆完，mustRush 阈值 5s；无钳 10s 拆完，阈值 11s。
        bool anyKit = false;
        for (const auto& b : actors_) {
            if (b.IsActive() && b.team == Team::CT && b.alive && b.hasDefuseKit) { anyKit = true; break; }
        }
        const float rushThreshold = anyKit ? 5.0f : 11.0f;
        const bool mustRush = (bombLeft <= rushThreshold);
        plan.intent = onB ? PlanIntent::RetakeB : PlanIntent::RetakeA;
        plan.target = onB ? "B" : "A";
        plan.tempo  = mustRush ? Tempo::Fast : Tempo::Normal;
        plan.reason = mustRush ? (anyKit ? "有钳，强拆包点" : "时间不足，强拆包点")
                               : "集结回防，先清点再拆";
        // T21：回防分工交给已校准的 @ 模板（CT.RetakeA@A / CT.RetakeB@B）——2 进点
        // + 3 掩护（其中 1 名为拆包手贴身架枪手，P0-3：掩护拆包、可一步接替拆包，
        // 不再留人出生点被动断后），且自带 look/hold/util。设置 templateDefault 让
        // ApplyTacticsSlots 用 @ 模板覆盖下面的手写兜底槽位。
        plan.templateDefault = true;
        // 兜底槽位（模板缺失时用）：拆包手进点，其余掩护，控路走同侧，不再跨侧。
        const std::string routeA = onB ? bLong : aLong;    // 同侧回防路线
        const std::string routeB = onB ? bSite : aSite;
        plan.slots[0] = { CoachRole::Entry,   siteArea, true, false };   // 拆包手
        plan.slots[1] = { CoachRole::Support, siteArea, true, false };   // 掩护拆包
        plan.slots[2] = { CoachRole::Support, siteArea, true, false };   // 掩护拆包
        plan.slots[3] = { CoachRole::Lurk,    routeA,   true, true  };   // 控同侧回防路线
        plan.slots[4] = { CoachRole::Anchor,  routeB,   true, false };   // 控同侧另一路
        return plan;
    }


    // ---- 事件优先 ----
    if (ev.type == CoachEventType::BombPlanted) {
        plan.tempo = Tempo::Fast;
        // 判断包点在哪一侧
        std::string lower = siteArea;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool onB = (lower.find("b_") != std::string::npos ||
                          lower.find("b site") != std::string::npos);
        if (isT) {
            plan.intent = onB ? PlanIntent::StackB : PlanIntent::StackA;
            plan.target = onB ? "B" : "A";
            plan.reason = "包已下，全员守包";
            plan.slots[0] = holdSlot(CoachRole::Anchor, onB ? bLong : aLong, false);
            plan.slots[1] = holdSlot(CoachRole::Anchor, onB ? mid : aShort, false);
            plan.slots[2] = holdSlot(CoachRole::Anchor, onB ? bLong : aLong, false);
            plan.slots[3] = holdSlot(CoachRole::Anchor, onB ? bLong : aShort, false);
            plan.slots[4] = holdSlot(CoachRole::Anchor, mid, false);
        } else {
            plan.intent = onB ? PlanIntent::RetakeB : PlanIntent::RetakeA;
            plan.target = onB ? "B" : "A";
            plan.reason = "包已下，全员回防";
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, siteArea, true, false };
        }
        return plan;
    }
    if (ev.type == CoachEventType::DefuseStarted) {
        // 按实际包点分侧（原来写死 StackA，B 点会错；CT 也从未进这个分支）
        std::string lower = siteArea;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool onB = (lower.find("b_") != std::string::npos ||
                          lower.find("b site") != std::string::npos);

        plan.tempo = Tempo::Fast;
        if (isT) {
            plan.intent = onB ? PlanIntent::StackB : PlanIntent::StackA;
            plan.target = onB ? "B" : "A";
            plan.reason = "有人在拆包，回防包点";
            plan.slots[0] = holdSlot(CoachRole::Entry, onB ? bLong : aLong, false);
            plan.slots[1] = holdSlot(CoachRole::Entry, onB ? mid : aShort, false);
            plan.slots[2] = holdSlot(CoachRole::Entry, onB ? bLong : aLong, false);
            plan.slots[3] = holdSlot(CoachRole::Entry, onB ? bLong : aShort, false);
            plan.slots[4] = holdSlot(CoachRole::Entry, mid, false);
        } else {
            plan.intent = onB ? PlanIntent::RetakeB : PlanIntent::RetakeA;
            plan.target = onB ? "B" : "A";
            plan.reason = "正在拆包，掩护拆弹";
            for (auto& sl : plan.slots) sl = { CoachRole::Anchor, siteArea, true, false };
        }
        return plan;
    }
    // P1-1: manpower change grows real slots instead of Default split + tempo.
    // Decided by the LIVE manpower diff (not the event alone -- a trade can
    // leave the count even). Slots are written directly, NOT via assign():
    // assign() sets templateDefault=true, and the plain Default tactics
    // template would overwrite these slots wholesale (the P1-2 trap).
    // 5b: the up-a-man MID plan below opts T into the ini template instead
    // (see the flag before its return): ApplyTacticsSlots resolves
    // [slots.T.Default@MID] wholesale, and the @Target lookup precedes the
    // plain Default template, so the P1-2 trap does not apply. The hand-written
    // slots stay as the fallback for maps without a Default@MID template.
    // CT keeps templateDefault=false: no [slots.CT.Default@MID] exists, and
    // flipping it would let the plain CT.Default template leak util/hold into
    // these hand-written slots.
    if (ev.type == CoachEventType::EnemyDown || ev.type == CoachEventType::AllyDown) {
        const int mine   = AliveCount(t);
        const int theirs = AliveCount(TeamManager::Opponent(t));
        if (mine - theirs >= 1) {
            // Up a man: press and control -- 2 push mid, 2 hold A/B sites, 1 free.
            plan.intent = PlanIntent::Default;
            plan.target = "MID";
            plan.tempo  = Tempo::Fast;
            plan.reason = "人数优势，前压控图";
            plan.slots[0] = { CoachRole::Entry,   mid,   true, true  };   // 前压控中
            plan.slots[1] = { CoachRole::Support, mid,   true, true  };   // 前压控中
            plan.slots[2] = { CoachRole::Anchor,  aSite, true, false };   // 架 A 点
            plan.slots[3] = { CoachRole::Anchor,  bSite, true, false };   // 架 B 点
            plan.slots[4] = { CoachRole::Lurk,    mid,   true, true  };   // 自由人，控中补位
            // P1-2: same MID-control look as the round-start MID branch.
            // For T the ini [slots.T.Default@MID] template supplies look now
            // (5b); these hand-written looks remain the fallback when the map
            // has no @MID template, and for CT (which keeps the bypass).
            plan.slots[0].lookArea = aSite;
            plan.slots[1].lookArea = bSite;
            plan.slots[2].lookArea = "CT_Spawn";
            plan.slots[3].lookArea = "CT_Spawn";
            plan.slots[4].lookArea = "CT_Spawn";
            // 5b: T opts into the ini MID template (see the P1-1 comment above).
            // Wholesale [slots.T.Default@MID] replace in ApplyTacticsSlots;
            // hand-written slots above survive only as the no-template fallback.
            if (isT) plan.templateDefault = true;
            return plan;
        }
        if (theirs - mine >= 1) {
            // Down a man: contract -- a pair holds each site, 1 watches mid.
            plan.intent = PlanIntent::Default;
            plan.target.clear();
            plan.tempo  = Tempo::Slow;
            plan.reason = "人数劣势，收缩双人架枪";
            plan.slots[0] = { CoachRole::Anchor,  aSite, true, false };   // 双人架 A
            plan.slots[1] = { CoachRole::Support, aSite, true, false };
            plan.slots[2] = { CoachRole::Anchor,  bSite, true, false };   // 双人架 B
            plan.slots[3] = { CoachRole::Support, bSite, true, false };
            plan.slots[4] = { CoachRole::Anchor,  mid,   true, false };   // 控中路信息
            // P1-2: contract slots carry their own look (site pairs watch the
            // site approaches, mid anchor watches the B exit).
            plan.slots[0].lookArea = aLong;
            plan.slots[1].lookArea = aShort;
            plan.slots[2].lookArea = bLong;
            plan.slots[3].lookArea = mid;
            plan.slots[4].lookArea = bSite;
            return plan;
        }
        // Even count (a trade): keep the old behaviour (Default split + tempo).
        plan.intent = PlanIntent::Default;
        plan.tempo  = (ev.type == CoachEventType::EnemyDown) ? Tempo::Fast : Tempo::Slow;
        plan.reason = (ev.type == CoachEventType::EnemyDown) ? "对面减员，前压抓人"
                                                              : "我方减员，收缩等机会";
        if (ev.type == CoachEventType::EnemyDown) assign(aLong, aSite, bLong, bSite, mid);
        else assign(aSite, aLong, bSite, bLong, mid);
        return plan;
    }
    if (ev.type == CoachEventType::EconomyLow && ShouldSaveNow(t)) {
        plan.intent = PlanIntent::Save;
        plan.tempo  = Tempo::Slow;
        plan.reason = "经济不足，少打少拼保枪";
        // 保枪不等于跑回出生点：留空目标，让 BOT 就地收缩、避免交火
        for (auto& sl : plan.slots) {
            sl = { CoachRole::Anchor, std::string(), false, false };
        }
        return plan;
    }
    if (ev.type == CoachEventType::TimeLow ||
        (!bombPlanted_ && round_.IsLive() && GetRoundRemaining() <= 20.0f)) {
        plan.tempo = Tempo::Fast;
        if (isT) {
            // 跟随当前主攻侧，不要每次 TimeLow 都硬切 A。
            std::string side = st.plan.target;
            if (side != "A" && side != "B") side = st.lastAttackSide;
            // T14：按最新接触区域定主攻侧（现在优先看接触，其次看历史）。
            if (side != "A" && side != "B") {
                const auto& contacts = ContactsOf(t);
                if (!contacts.empty()) {
                    const auto& c = *std::max_element(contacts.begin(), contacts.end(),
                        [](const auto& a, const auto& b) { return a.age < b.age; });
                    const std::string ca = world_ ? world_->FindAreaAt(c.x, c.y) : std::string();
                    if (!ca.empty()) {
                        const char cs = AreaSideOf(ca);
                        if (cs == 'A') side = "A";
                        else if (cs == 'B') side = "B";
                    }
                }
            }
            if (side != "A" && side != "B") side = "A";
            plan.intent = (side == "B") ? PlanIntent::RushB : PlanIntent::RushA;
            plan.target = side;
            plan.reason = "时间不足，抓紧进攻" + side;
            for (auto& sl : plan.slots) {
                sl = { CoachRole::Entry, side == "B" ? bLong : aLong, true, false };
            }
            plan.slots[4] = { CoachRole::Anchor, side == "B" ? bSite : aSite, true, false };
        } else {
            // CT 的时间不足是收位守点，不要套用 T 的分路模板。
            plan.intent = PlanIntent::Default;
            plan.reason = "时间不足，收位守点";
            plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[1] = { CoachRole::Support, aShort, true, false };
            plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
            plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[4] = { CoachRole::Support, bLong,  true, false };
            // P1-2: this branch does not set templateDefault, so no template
            // can supply look -- carry it in the slots (same CT hold shape
            // as the plain [slots.CT.Default] template).
            plan.slots[0].lookArea = aLong;
            plan.slots[1].lookArea = mid;
            plan.slots[2].lookArea = aSite;
            plan.slots[3].lookArea = bLong;
            plan.slots[4].lookArea = mid;
        }
        return plan;
    }

    // T19：赛点分支——强起 + 提速（除非已下包/残局）。
    if (ev.type == CoachEventType::MatchPoint) {
        plan.tempo = Tempo::Fast;
        if (isT) {
            std::string side = st.plan.target;
            if (side != "A" && side != "B") side = st.lastAttackSide;
            if (side != "A" && side != "B") side = "A";
            plan.intent = (side == "B") ? PlanIntent::RushB : PlanIntent::RushA;
            plan.target = side;
            plan.reason = "赛点，全力进攻" + side;
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, side == "B" ? bLong : aLong, true, false };
            plan.slots[4] = { CoachRole::Anchor, side == "B" ? bSite : aSite, true, false };
            plan.templateDefault = true;
        } else {
            plan.intent = PlanIntent::Default;
            plan.reason = "赛点，收位守点";
            plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[1] = { CoachRole::Support, aShort, true, false };
            plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
            plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[4] = { CoachRole::Support, bLong,  true, false };
            plan.templateDefault = true;
        }
        return plan;
    }

    // T17：玩家阵亡——规则重算（T 转收缩/残局，CT 收位守点）。
    if (ev.type == CoachEventType::HumanDied) {
        if (isT) {
            plan.intent = PlanIntent::Default;
            plan.tempo  = Tempo::Slow;
            plan.reason = "玩家阵亡，收缩重组";
            assign(aSite, aLong, bSite, bLong, mid);
        } else {
            plan.intent = PlanIntent::Default;
            plan.tempo  = Tempo::Slow;
            plan.reason = "玩家阵亡，收位守点";
            plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[1] = { CoachRole::Support, aShort, true, false };
            plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
            plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[4] = { CoachRole::Support, bLong,  true, false };
            plan.templateDefault = true;
        }
        return plan;
    }

    // ---- 回合开始：结合记忆选点 ----
    // T10：意图打分制——回合开始对候选意图打分取最高，信号全部取自确定性输入
    // （情报热度、近局习惯、站位分布、合法闭集），不再只靠"己方分侧胜负计数"。
    int aTries = 0, aWins = 0, bTries = 0, bWins = 0;
    for (const auto& m : st.memory) {
        if (m.target == "A") { ++aTries; if (m.won) ++aWins; }
        if (m.target == "B") { ++bTries; if (m.won) ++bWins; }
    }
    const bool avoidA = (aTries >= 2 && aWins == 0);
    const bool avoidB = (bTries >= 2 && bWins == 0);

    // T14：道具明细——烟 ≥1 才能道具开点。
    int smokeCount = 0;
    for (const auto& b : actors_) {
        if (!b.IsActive() || !b.alive || b.team != t) continue;
        for (int gi = 0; gi < b.grenadeCount; ++gi) {
            if (b.grenades[gi] == WeaponId::GrenadeSmoke) ++smokeCount;
        }
    }
    // T15：情报 heat 全量——热侧避开、冷侧主攻。
    const float heatA = intel_.HeatOfSide(world_.get(), "A");
    const float heatB = intel_.HeatOfSide(world_.get(), "B");
    // T32：分侧敌人数（存活 + 近期接触，区别于 heat）。
    int enemyCountA = 0, enemyCountB = 0;
    for (const auto& b : actors_) {
        if (!b.IsActive() || !b.alive || b.team == t) continue;
        const std::string ea = world_ ? world_->FindAreaAt(b.x, b.y) : std::string();
        const char cs = AreaSideOf(ea);
        if (cs == 'A') ++enemyCountA;
        else if (cs == 'B') ++enemyCountB;
    }
    for (const auto& c : ContactsOf(t)) {
        const std::string ca = world_ ? world_->FindAreaAt(c.x, c.y) : std::string();
        const char cs = AreaSideOf(ca);
        if (cs == 'A') ++enemyCountA;
        else if (cs == 'B') ++enemyCountB;
    }
    // T31：我方站位分布——某区我方 ≥1 人存活且无近期接触 → "已控"。
    auto sideAllyCount = [&](char side) -> int {
        int n = 0;
        for (const auto& b : actors_) {
            if (!b.IsActive() || !b.alive || b.team != t) continue;
            const std::string a = world_ ? world_->FindAreaAt(b.x, b.y) : std::string();
            if (AreaSideOf(a) == side) ++n;
        }
        return n;
    };
    const bool controlA = sideAllyCount('A') >= 1;
    const bool controlB = sideAllyCount('B') >= 1;
    const bool controlMid = sideAllyCount('M') >= 1;

    // 经济崩溃就别硬打（回合开始时的第一道判断）
    const bool broke = (ev.type == CoachEventType::RoundStart && ShouldSaveNow(t));
    if (broke) {
        plan.intent = PlanIntent::Save;
        plan.tempo  = Tempo::Slow;
        plan.reason = "经济平均不健康，保枪存钱";
        for (auto& sl : plan.slots) {
            sl = { CoachRole::Anchor, std::string(), false, false };
        }
        return plan;
    }

    // 合法闭集（T10）：穷局 / 存钱 T 禁 rush/split/fake；已下包禁换侧（此分支只在未下包进入）。
    const bool lowEco = LowEconomyAverage(t);
    const bool buyingEco = (st.buyPlan == BuyPlan::Eco);
    auto intentAllowed = [&](PlanIntent i) {
        if (isT) {
            if (i == PlanIntent::RushA || i == PlanIntent::RushB ||
                i == PlanIntent::SplitA || i == PlanIntent::SplitB ||
                i == PlanIntent::FakeThenOther) {
                return !lowEco && !buyingEco;   // 穷局 / 存钱时不主动进攻（T10）
            }
        }
        return true;
    };

    if (isT) {
        // T13/T15/T10：打分制选点。候选 = Default(分兵) / RushA / RushB / SplitA / SplitB / FakeThenOther / MID 控中。
        struct Cand { PlanIntent intent; std::string target; float score; const char* reason; };
        std::vector<Cand> cands;

        // 1) Default 分兵：兜底，得分 0。
        cands.push_back({ PlanIntent::Default, "", 0.0f, "默认分兵找机会" });
        // 2) MID control (T11, P1-2): the standing default -- 2 push mid +
        // 1 anchor per site + 1 free lurk. It used to be offered only with
        // no intel, so once the first contact landed every Default plan fell
        // back to a target-less split (42% target '-'); offering it always
        // keeps "default = control mid for info" all round long. It still
        // loses to Rush/Split/Fake when their signals score higher, and to
        // the plain split when the repeat penalty hits after a lost MID round.
        cands.push_back({ PlanIntent::Default, "MID", 0.4f, "默认控中换信息" });
        // 3) RushA / RushB / SplitA / SplitB / Fake：按热差、敌人数、已控、道具、经济打分。
        // 注意：回合开始时敌人还没散开（集中在出生点），"敌少侧"不可作为速推依据，
        // 否则几乎每回合都会速推敌方出生点所在的对侧。速推只由「真实情报热差 / 记忆连败 /
        // 已控包点」等正向信号驱动。
        auto sideScore = [&](char side) {
            float s = 0.0f;
            const float heatMine = (side == 'A') ? heatA : heatB;
            const float heatOther = (side == 'A') ? heatB : heatA;
            // 热差：另一侧明显更热才主攻本侧（有情报时）。
            if (intel_.ObservationCount() >= 1) s += (heatOther - heatMine) * Tuning::ai::ScoreHeatDelta;
            return s;
        };
        const float economyBoost = (st.buyPlan == BuyPlan::Full || st.buyPlan == BuyPlan::Half)
                                       ? Tuning::ai::ScoreEconomyMul : 0.0f;
        // 历史连败换点信号（原有逻辑保留）：avoidA/avoidB 直接指定换侧。
        const bool haveIntel = intel_.ObservationCount() >= 1;
        for (char side : {'A', 'B'}) {
            float base = sideScore(side);
            const bool ctrl = (side == 'A') ? controlA : controlB;
            const bool forcedByMemory = (side == 'A' && avoidB) || (side == 'B' && avoidA);
            // Rush：已控包点（真实占点）或记忆连败换侧，或已有情报热差；不再用"敌少"。
            if (forcedByMemory || (ctrl && haveIntel)) {
                const float rushScore = base + economyBoost + (smokeCount >= 1 ? Tuning::ai::ScoreSmokeMul : 0.0f)
                                        + (forcedByMemory ? 1.0f : 0.0f);
                PlanIntent ri = (side == 'A') ? PlanIntent::RushA : PlanIntent::RushB;
                if (intentAllowed(ri)) cands.push_back({ ri, std::string(1, side), rushScore, "速推主攻" });
            }
            // Split：Mid 已控 + 满/半起 + 烟（真实中路控制 + 满经济）。
            // P1-3: Split needs any 2 of 3 signals (mid control, economy, smoke),
            // not all 3 at once; the old triple gate almost never fired in practice.
            const int splitSignals = (controlMid ? 1 : 0) + (economyBoost > 0.0f ? 1 : 0) + (smokeCount >= 1 ? 1 : 0);
            if (splitSignals >= 2) {
                PlanIntent si = (side == 'A') ? PlanIntent::SplitA : PlanIntent::SplitB;
                if (intentAllowed(si)) cands.push_back({ si, std::string(1, side), base + economyBoost + (smokeCount >= 1 ? Tuning::ai::ScoreSmokeMul : 0.0f) + 0.3f + Tuning::ai::ScoreSplitBase, "分路夹击" });
            }
        }
        // Fake：佯攻侧有接触 + 主攻侧 heat 低。
        {
            // P1-3: Fake fires when either side runs hotter by ScoreFakeHeatDelta.
            // plan.target is the cold side (the side the branch rush really attacks).
            const std::string fakeSide = (heatA >= heatB) ? "B" : "A";
            const float fakeGap = std::fabs(heatA - heatB);
            if (intentAllowed(PlanIntent::FakeThenOther) && fakeGap >= Tuning::ai::ScoreFakeHeatDelta) {
                cands.push_back({ PlanIntent::FakeThenOther, fakeSide, 0.8f, "佯攻后转点" });
            }
        }
        // T10：连续同 intent+target 失败 → 换点（近 3 回合）。
        if (!st.memory.empty()) {
            const RoundMemo& lm = st.memory.back();
            if (!lm.won) {
                for (auto& c : cands) {
                    if (c.intent == lm.plan && c.target == lm.target) {
                        c.score -= Tuning::ai::ScoreRepeatPenalty;
                    }
                }
            }
        }
        // T07：sameSideStreak ≥ SameSideStreakLimit → 强制换侧（给对侧加分）。
        if (st.sameSideStreak >= Tuning::ai::SameSideStreakLimit) {
            const std::string streakSide = st.lastAttackSide;
            const std::string other = (streakSide == "A") ? "B" : "A";
            for (auto& c : cands) {
                if (c.target == other) c.score += 2.0f;
                else if (c.target == streakSide) c.score -= 1.0f;
            }
        }
        // avoidA/avoidB（历史 0 胜）作为强惩罚叠加。
        for (auto& c : cands) {
            if (c.target == "A" && avoidA) c.score -= 1.5f;
            if (c.target == "B" && avoidB) c.score -= 1.5f;
        }

        const Cand* best = &cands[0];
        for (const auto& c : cands) if (c.score > best->score) best = &c;
        plan.intent = best->intent;
        plan.target = best->target;
        plan.reason = best->reason;

        // P2-4: branch diversification -- the 20s branch is no longer always a rush.
        // Deterministic pick from live signals only (no RNG), so replays stay
        // bit-identical. Rush stays a candidate and still wins on a clear intel gap.
        //   gap >= BranchRushHeatGap -> Rush the cold side (exploit; old behaviour)
        //   same-side streak / avoided side -> Rotate (other side; or the cold side
        //     via rotate routing when the plan is a Fake whose target IS the cold side)
        //   gap >= ScoreFakeHeatDelta -> Fake (feint hot, hit cold; not from a Fake plan)
        //   otherwise -> Default+MID (stay mid, gather info; a Fake plan keeps its rush payoff)
        struct BranchPick { PlanIntent intent; std::string target; Tempo tempo; };
        auto pickBranch = [&](const std::string& rotateSide, bool allowFake) -> BranchPick {
            const std::string coldSide = (heatA >= heatB) ? "B" : "A";
            const float heatGap = std::fabs(heatA - heatB);
            if (heatGap >= Tuning::ai::BranchRushHeatGap) {
                return { (coldSide == "B") ? PlanIntent::RushB : PlanIntent::RushA, coldSide, Tempo::Fast };
            }
            const std::string curSide = (plan.target == "A" || plan.target == "B")
                                            ? plan.target : st.lastAttackSide;
            if (!rotateSide.empty()) {
                const bool streakForcesSwitch =
                    (st.sameSideStreak >= Tuning::ai::SameSideStreakLimit);
                const bool avoidCur = (curSide == "A" && avoidA) || (curSide == "B" && avoidB);
                if (rotateSide != curSide) {
                    if (streakForcesSwitch || avoidCur) {
                        return { (rotateSide == "B") ? PlanIntent::RotateB : PlanIntent::RotateA,
                                 rotateSide, Tempo::Fast };
                    }
                } else if (streakForcesSwitch) {
                    // Same side, rotate routing: the Fake payoff attacks the cold side
                    // either way; a streak varies the routing instead of the side.
                    return { (rotateSide == "B") ? PlanIntent::RotateB : PlanIntent::RotateA,
                             rotateSide, Tempo::Fast };
                }
            }
            if (allowFake && heatGap >= Tuning::ai::ScoreFakeHeatDelta) {
                return { PlanIntent::FakeThenOther, coldSide, Tempo::Normal };
            }
            if (allowFake) {
                return { PlanIntent::Default, "MID", Tempo::Normal };
            }
            return { (coldSide == "B") ? PlanIntent::RushB : PlanIntent::RushA, coldSide, Tempo::Fast };
        };
        if (plan.intent == PlanIntent::Default && plan.target == "MID") {
            // T11: MID control -- 2 push mid + 1 anchor per site + 1 free lurk.
            plan.slots[0] = { CoachRole::Entry,   mid,   true, true  };
            plan.slots[1] = { CoachRole::Support, mid,   true, true  };
            plan.slots[2] = { CoachRole::Anchor,  aSite, true, false };
            plan.slots[3] = { CoachRole::Anchor,  bSite, true, false };
            plan.slots[4] = { CoachRole::Lurk,    mid,   true, true  };
            // P1-2: MID slots carry their own lookArea (mid pushers watch the
            // two site exits, site anchors watch the CT side, free lurk watches deep
            // CT). aSite/bSite/mid are AreaHint-resolved real area names, so
            // CoachApplyPlan can resolve them even without a template.
            plan.slots[0].lookArea = aSite;
            plan.slots[1].lookArea = bSite;
            plan.slots[2].lookArea = "CT_Spawn";
            plan.slots[3].lookArea = "CT_Spawn";
            plan.slots[4].lookArea = "CT_Spawn";
            plan.templateDefault = true;
            // T06：时间差打法——「按情报」才设分支（20s 后转 Rush 冷侧）。
            // 无情报不预支 rush：控中换信息就是本回合的完整计划，避免每回合开局都自动冲。
            if (intel_.ObservationCount() >= 2) {
                plan.hasBranch = true;
                plan.branchAfterSec = 20.0f;
                // P2-4: branch varies by signal (see pickBranch); rotate side is the
                // side opposite last round's attack.
                const std::string lastSide = st.lastAttackSide;
                const std::string rotateSide = (lastSide == "A") ? "B" : (lastSide == "B") ? "A" : std::string();
                const BranchPick bp = pickBranch(rotateSide, true);
                plan.branchIntent = bp.intent;
                plan.branchTarget = bp.target;
                plan.branchTempo = bp.tempo;
            }
        } else if (plan.intent == PlanIntent::RushA || plan.intent == PlanIntent::RushB) {
            const bool b = (plan.intent == PlanIntent::RushB);
            plan.tempo = Tempo::Fast;
            plan.reason = b ? "B 点条件有利，速推 B" : "A 点条件有利，速推 A";
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, b ? bLong : aLong, true, false };
            plan.slots[4] = { CoachRole::Anchor, b ? bSite : aSite, true, false };
            plan.templateDefault = true;
        } else if (plan.intent == PlanIntent::SplitA || plan.intent == PlanIntent::SplitB) {
            const bool b = (plan.intent == PlanIntent::SplitB);
            plan.tempo = Tempo::Fast;
            plan.reason = b ? "分路夹击 B" : "分路夹击 A";
            plan.slots[0] = { CoachRole::Entry,   b ? bLong : aLong, true, false };
            plan.slots[1] = { CoachRole::Entry,   b ? bSite : aSite, true, false };
            plan.slots[2] = { CoachRole::Support, b ? bSite : aSite, true, false };
            plan.slots[3] = { CoachRole::Lurk,    mid, true, true };
            plan.slots[4] = { CoachRole::Anchor,  b ? bSite : aSite, true, false };
            plan.templateDefault = true;
        } else if (plan.intent == PlanIntent::FakeThenOther) {
            plan.tempo = Tempo::Normal;
            // T06：Fake 也带分支——佯攻后按情报转另一侧。
            const bool attackB = (plan.target == "B");  // P2-4: kept -- the slot shape below still uses it.
            plan.hasBranch = true;
            plan.branchAfterSec = 20.0f;
            // P2-4: the fake's branch still attacks the real (cold) side, but the
            // routing varies -- a streak rotates in via mid instead of rushing
            // straight in. allowFake=false: a Fake plan never branches to another Fake.
            const BranchPick bp = pickBranch(plan.target, false);
            plan.branchIntent = bp.intent;
            plan.branchTarget = bp.target;
            plan.branchTempo = bp.tempo;
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, attackB ? aLong : bLong, true, false };
            plan.slots[4] = { CoachRole::Anchor, attackB ? bSite : aSite, true, false };
            plan.templateDefault = true;
        } else {
            // Default 分兵。
            plan.intent = PlanIntent::Default;
            plan.target.clear();
            plan.reason = "默认开局，A/B 分兵找机会";
            assign(aLong, aSite, bLong, bSite, mid);
        }
        // T06：Default 计划设时间差分支——「按情报」才预支 20s 后的 rush
        // （另一侧更热才转；无情报时保持 Default 分兵换信息，不预支）。
        if (plan.intent == PlanIntent::Default && plan.target.empty() && intel_.ObservationCount() >= 2) {
            plan.hasBranch = true;
            plan.branchAfterSec = 20.0f;
            // P2-4: branch varies by signal (see pickBranch); rotate side is the
            // side opposite last round's attack.
            const std::string lastSide = st.lastAttackSide;
            const std::string rotateSide = (lastSide == "A") ? "B" : (lastSide == "B") ? "A" : std::string();
            const BranchPick bp = pickBranch(rotateSide, true);
            plan.branchIntent = bp.intent;
            plan.branchTarget = bp.target;
            plan.branchTempo = bp.tempo;
        }
    } else {
        // ---- CT 回合开始：按敌方习惯/情报/敌枪变阵（T13/T14）----
        plan.intent = PlanIntent::Default;
        plan.reason = "常规守点，A/B 各两人压中路";
        plan.templateDefault = true;

        // T14：敌枪构成——敌狙多 → 慢打/避长廊（tempo=Slow，不冒进）。
        int sniper = 0;
        for (const auto& b : actors_) {
            if (!b.IsActive() || !b.alive || b.team == t) continue;
            WeaponId bestW = WeaponId::None;
            for (int slot = 0; slot <= 1; ++slot) {
                if (b.weapons[slot] != WeaponId::None) { bestW = b.weapons[slot]; break; }
            }
            if (bestW != WeaponId::None && WeaponDB::Get(bestW).cls == WeaponClass::Sniper) ++sniper;
        }

        // T13：敌方习惯——敌方最近主攻侧（记忆字段）。
        std::string enemySide;
        if (!st.memory.empty()) enemySide = st.memory.back().enemyAttackSide;

        // T13：敌方连续两回合主攻 A → 重防 A（3-1-1 / StackA）。
        bool enemyRepeatsA = false, enemyRepeatsB = false;
        if (st.memory.size() >= 2) {
            const auto& m1 = st.memory[st.memory.size() - 1];
            const auto& m2 = st.memory[st.memory.size() - 2];
            if (m1.enemyAttackSide == "A" && m2.enemyAttackSide == "A") enemyRepeatsA = true;
            if (m1.enemyAttackSide == "B" && m2.enemyAttackSide == "B") enemyRepeatsB = true;
        }
        // 情报热度也作为重防依据：某侧 heat 高 + 观察 ≥1 → 重防该侧（无情报不赌点）。
        const bool stackA = enemyRepeatsA || (intel_.ObservationCount() >= 1 && heatA - heatB >= 0.4f);
        const bool stackB = enemyRepeatsB || (intel_.ObservationCount() >= 1 && heatB - heatA >= 0.4f);

        if (stackA) {
            plan.intent = PlanIntent::StackA;
            plan.target = "A";
            plan.reason = "敌方主攻 A，重防 A";
            plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[1] = { CoachRole::Support, aShort, true, false };
            plan.slots[2] = { CoachRole::Support, aLong,  true, false };
            plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[4] = { CoachRole::Lurk,    mid,    true, true  };
            plan.templateDefault = true;
        } else if (stackB) {
            plan.intent = PlanIntent::StackB;
            plan.target = "B";
            plan.reason = "敌方主攻 B，重防 B";
            plan.slots[0] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[1] = { CoachRole::Support, bLong,  true, false };
            plan.slots[2] = { CoachRole::Support, mid,    true, false };
            plan.slots[3] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[4] = { CoachRole::Lurk,    aShort, true, true  };
            plan.templateDefault = true;
        } else {
            plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[1] = { CoachRole::Support, aShort, true, false };
            plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
            plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[4] = { CoachRole::Support, bLong,  true, false };
            if (sniper >= 2) {
                plan.tempo = Tempo::Slow;
                plan.reason = "敌狙多，慢打避长廊";
            }
        }
    }
    return plan;
}

// ---------------------------------------------------------------------------
// 计划整形：防止 LLM 连续压同一侧 / CT 默认全堆一点。
// 只改「默认分兵」和「无情报的 stack」，不碰已下包后的 retake/stack。
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 教练计划 trace：每下发一条计划写一行，用于 LLM vs 规则的 A/B 评估。
// 只读数据，不参与任何玩法逻辑。
// ---------------------------------------------------------------------------
void CS2DSimulation::EnablePlanTrace(const std::string& path) {
    ClosePlanTrace();
    planTrace_ = std::fopen(path.c_str(), "wb");
    if (!planTrace_) {
        std::cerr << "[PlanTrace] 无法写入: " << path << std::endl;
        return;
    }
    std::fprintf(planTrace_,
                 "round,elapsed_s,team,intent,target,tempo,source,latency_s,reshaped,reason\n");
    std::cout << "[PlanTrace] 计划 trace 已开启: " << path << std::endl;
}

void CS2DSimulation::ClosePlanTrace() {
    if (!planTrace_) return;
    std::fclose(planTrace_);
    planTrace_ = nullptr;
}

char CS2DSimulation::AreaSideOf(const std::string& area) const {
    if (!world_ || area.empty()) return '?';
    std::string low = area;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (low.find("mid") != std::string::npos) return 'M';
    float x = 0.0f, y = 0.0f;
    if (!AreaCenterByName(area, x, y)) return '?';
    const CS2DWorld::BombSiteDef* best = nullptr;
    float bestD2 = 1e18f;
    for (const auto& site : world_->GetBombSites()) {
        const float dx = site.cx - x, dy = site.cy - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = &site; }
    }
    return best ? best->name[0] : '?';
}

bool CS2DSimulation::PlanTargetConsistent(Team t, const TeamPlan& plan) const {
    (void)t;
    if (!world_ || plan.target.empty()) return true;
    const char want = plan.target[0];
    if (want != 'A' && want != 'B' && want != 'M') return true;

    int matched = 0;
    for (const auto& so : plan.slots) {
        if (AreaSideOf(so.area) == want) ++matched;
    }
    const int need = (want == 'M') ? 1 : 2;
    return matched >= need;
}

bool CS2DSimulation::ApplySlotTemplate(Team t, TeamPlan& plan) {
    const std::string teamKey = (t == Team::T) ? "T" : "CT";
    const std::string intentKey = TacticsIntentKey(plan.intent);
    // 先找 target 专属模板（T.Default@A），再退回通用模板（T.Default）
    const auto* tpl = plan.target.empty()
                          ? nullptr
                          : tactics_.Slots(teamKey, intentKey + "@" + plan.target);
    if (!tpl) tpl = tactics_.Slots(teamKey, intentKey);
    if (!tpl) return false;
    plan.slots = *tpl;
    return true;
}

void CS2DSimulation::TraceCoachAdvisory(Team t, const TeamPlan& plan) {
    if (!planTrace_) return;
    std::string slotSig;
    for (const auto& so : plan.slots) slotSig += so.area + ";";
    const std::string key = std::to_string(round_.RoundNumber()) + "|" + TeamName(t) +
                            "|" + IntentKey(plan.intent) + "|" + plan.target + "|" + slotSig;
    if (key == planTraceAdvKey_) return;
    planTraceAdvKey_ = key;

    std::string reason = plan.reason;
    for (char& c : reason) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') c = ';';
    }
    if (reason.size() > 80) {
        size_t cut = 80;
        while (cut > 0 && (static_cast<unsigned char>(reason[cut]) & 0xC0) == 0x80) --cut;
        reason.resize(cut);
    }
    std::fprintf(planTrace_, "%d,%.1f,%s,%s,%s,%s,%s,%.2f,%d,\"%s\"\n",
                 round_.RoundNumber(), GetRoundElapsed(), TeamName(t),
                 IntentKey(plan.intent),
                 plan.target.empty() ? "-" : plan.target.c_str(),
                 TempoKey(plan.tempo), "llm_advisory", plan.latencyWall,
                 plan.reshaped ? 1 : 0, reason.c_str());
}

// ---------------------------------------------------------------------------
// 决策对比日志：同一决策点，规则计划 / LLM 计划 / 最终执行 三者并排
// ---------------------------------------------------------------------------
static const char* CoachEventName(int type) {
    switch (static_cast<CoachEventType>(type)) {
        case CoachEventType::RoundStart:    return "round_start";
        case CoachEventType::EnemySpotted:  return "enemy_spotted";
        case CoachEventType::AllyDown:      return "ally_down";
        case CoachEventType::EnemyDown:     return "enemy_down";
        case CoachEventType::BombPlanted:   return "bomb_planted";
        case CoachEventType::DefuseStarted: return "defuse_started";
        case CoachEventType::DefuseAborted: return "defuse_aborted";
        case CoachEventType::TimeLow:       return "time_low";
        case CoachEventType::EconomyLow:    return "economy_low";
        case CoachEventType::HumanDied:     return "human_died";
        case CoachEventType::MatchPoint:    return "match_point";
        default:                            return "heartbeat";
    }
}

static std::string SlotsSignature(const TeamPlan& plan) {
    std::string out;
    for (size_t i = 0; i < plan.slots.size(); ++i) {
        const auto& so = plan.slots[i];
        if (i) out += ';';
        out += std::to_string(i) + ":" + so.area + "/" +
               std::to_string(static_cast<int>(so.role));
    }
    return out;
}

static std::string SanitizeField(std::string v, size_t cap = 60) {
    for (char& c : v) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') c = ' ';
    }
    if (v.size() > cap) {
        size_t cut = cap;
        while (cut > 0 && (static_cast<unsigned char>(v[cut]) & 0xC0) == 0x80) --cut;
        v.resize(cut);
    }
    return v;
}

void CS2DSimulation::EnableDecisionLog(const std::string& path) {
    CloseDecisionLog();
    decisionLog_ = std::fopen(path.c_str(), "wb");
    if (!decisionLog_) {
        std::cerr << "[DecisionLog] 无法写入: " << path << std::endl;
        return;
    }
    std::fprintf(decisionLog_,
                 "round,elapsed_s,event,team,"
                 "rule_intent,rule_target,rule_tempo,rule_reason,rule_slots,"
                 "llm_intent,llm_target,llm_tempo,llm_reason,llm_slots,"
                 "latency_s,status,applied_intent,applied_target,applied_source\n");
    std::cout << "[DecisionLog] 决策对比日志已开启: " << path << std::endl;
}

void CS2DSimulation::CloseDecisionLog() {
    if (!decisionLog_) return;
    std::fclose(decisionLog_);
    decisionLog_ = nullptr;
}

void CS2DSimulation::TraceCoachDecision(Team t, const char* status) {
    if (!decisionLog_) return;
    const CoachState& st = CoachFor(t);
    const TeamPlan& rule = st.hasRuleSnapshot ? st.ruleSnapshot : st.plan;
    const TeamPlan& llm  = st.llmSnapshot;
    const TeamPlan& app  = st.plan;
    const bool llmEmpty = llm.source != "llm" && llm.intent == PlanIntent::Default &&
                          llm.target.empty() && llm.reason.empty();

    std::fprintf(decisionLog_,
                 "%d,%.1f,%s,%s,"
                 "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\","
                 "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\","
                 "%.2f,%s,\"%s\",\"%s\",\"%s\"\n",
                 round_.RoundNumber(), GetRoundElapsed(),
                 CoachEventName(st.llmEventType), TeamName(t),
                 IntentKey(rule.intent), rule.target.c_str(), TempoKey(rule.tempo),
                 SanitizeField(rule.reason).c_str(), SlotsSignature(rule).c_str(),
                 llmEmpty ? "" : IntentKey(llm.intent),
                 llmEmpty ? "" : llm.target.c_str(),
                 llmEmpty ? "" : TempoKey(llm.tempo),
                 llmEmpty ? "" : SanitizeField(llm.reason).c_str(),
                 llmEmpty ? "" : SlotsSignature(llm).c_str(),
                 st.llmLatencyWall, status,
                 IntentKey(app.intent), app.target.c_str(), app.source.c_str());
}

void CS2DSimulation::TraceCoachPlan(Team t) {
    if (!planTrace_) return;
    const CoachState& st = CoachFor(t);
    if (!st.plan.valid) return;

    // 去重键：同一回合里"意图+目标+来源+槽位"不变就只写一行。
    // gen 每次 CoachThink 都会变，不能进 key，否则同一计划会被写十几次。
    std::string slotSig;
    for (const auto& so : st.plan.slots) slotSig += so.area + ";";
    const std::string key = std::to_string(round_.RoundNumber()) + "|" +
                            TeamName(t) + "|" +
                            std::to_string(static_cast<int>(st.plan.intent)) + "|" +
                            st.plan.target + "|" + st.plan.source + "|" + slotSig;
    if (key == planTraceKey_) return;
    planTraceKey_ = key;

    std::string reason = st.plan.reason;
    for (char& c : reason) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') c = ';';
    }
    // 截断时避免切断 UTF-8 多字节字符（否则 CSV 不是合法 UTF-8，脚本会崩）
    if (reason.size() > 80) {
        size_t cut = 80;
        while (cut > 0 && (static_cast<unsigned char>(reason[cut]) & 0xC0) == 0x80) --cut;
        reason.resize(cut);
    }

    const float latency = (st.plan.source == "llm") ? st.plan.latencyWall : 0.0f;

    std::fprintf(planTrace_, "%d,%.1f,%s,%s,%s,%s,%s,%.2f,%d,\"%s\"\n",
                 round_.RoundNumber(), GetRoundElapsed(),
                 TeamName(t), IntentKey(st.plan.intent),
                 st.plan.target.empty() ? "-" : st.plan.target.c_str(),
                 TempoKey(st.plan.tempo),
                 st.plan.source.c_str(), latency,
                 st.plan.reshaped ? 1 : 0, reason.c_str());
}

// ---------------------------------------------------------------------------
// T01 结构化教练决策日志（JSONL）：每次下发规则计划写一行。
// 与 plan trace（CSV）并存；未开启时零开销，不参与玩法逻辑。
// ---------------------------------------------------------------------------
static const char* CoachRoleKey(CoachRole r) {
    switch (r) {
        case CoachRole::Entry:   return "entry";
        case CoachRole::Support: return "support";
        case CoachRole::Lurk:    return "lurk";
        case CoachRole::Anchor:  return "anchor";
        case CoachRole::Awper:   return "awper";
        default:                 return "support";
    }
}
static const char* HoldStyleKey(HoldStyle h) {
    switch (h) {
        case HoldStyle::Anchor:   return "anchor";
        case HoldStyle::OffAngle: return "offangle";
        case HoldStyle::Passive:  return "passive";
        default:                  return "default";
    }
}
static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, 8, "\\u%04x", c); out += b; }
                else out += c;
        }
    }
    return out;
}

void CS2DSimulation::EnableWiringLog(const std::string& path) {
    CloseWiringLog();
    wiringLog_ = std::fopen(path.c_str(), "wb");
    if (!wiringLog_) {
        std::cerr << "[WiringLog] 无法写入: " << path << std::endl;
        return;
    }
    std::cout << "[WiringLog] 结构化教练决策日志已开启: " << path << std::endl;
}

void CS2DSimulation::CloseWiringLog() {
    if (!wiringLog_) return;
    std::fclose(wiringLog_);
    wiringLog_ = nullptr;
}

void CS2DSimulation::TraceWiring(Team t, const char* guard, const char* sanitized, const char* templateKey) {
    if (!wiringLog_) return;
    const CoachState& st = CoachFor(t);
    const TeamPlan& p = st.plan;

    // templateKey：命中 @ 模板优先，否则 plain 回退；都没有则为空。
    std::string tplKey;
    if (tactics_.loaded) {
        const char* teamKey = (t == Team::T) ? "T" : "CT";
        const std::string intentKey = TacticsIntentKey(p.intent);
        if (tactics_.Slots(teamKey, intentKey + "@" + p.target)) tplKey = std::string(teamKey) + "." + intentKey + "@" + p.target;
        else if (tactics_.Slots(teamKey, intentKey)) tplKey = std::string(teamKey) + "." + intentKey;
    }
    if (templateKey && *templateKey) tplKey = templateKey;

    std::string o;
    o.reserve(1024);
    o += "{\"round\":" + std::to_string(round_.RoundNumber());
    o += ",\"elapsed_s\":" + std::to_string(GetRoundElapsed());
    o += ",\"team\":\"" + std::string(TeamName(t)) + "\"";
    o += ",\"source\":\"" + p.source + "\"";
    o += ",\"intent\":\"" + std::string(IntentKey(p.intent)) + "\"";
    o += ",\"target\":\"" + JsonEscape(p.target) + "\"";
    o += ",\"tempo\":\"" + std::string(TempoKey(p.tempo)) + "\"";
    o += ",\"templateKey\":\"" + JsonEscape(tplKey) + "\"";
    o += ",\"slots\":[";
    for (size_t i = 0; i < p.slots.size(); ++i) {
        const SlotOrder& so = p.slots[i];
        if (i) o += ",";
        o += "{\"role\":\"" + std::string(CoachRoleKey(so.role)) + "\"";
        o += ",\"area\":\"" + JsonEscape(so.area) + "\"";
        o += ",\"canTrade\":" + std::string(so.canTrade ? "true" : "false");
        o += ",\"canRotate\":" + std::string(so.canRotate ? "true" : "false");
        o += ",\"lookArea\":\"" + JsonEscape(so.lookArea) + "\"";
        o += ",\"lookAngle\":" + std::to_string(so.lookAngle);
        o += ",\"hold\":\"" + std::string(HoldStyleKey(so.hold)) + "\"";
        o += ",\"util\":" + std::to_string(so.util);
        o += ",\"utilArea\":\"" + JsonEscape(so.utilArea) + "\"}";
    }
    o += "],\"signals\":{";
    o += "\"heatA\":" + std::to_string(intel_.HeatOfSide(world_.get(), "A"));
    o += ",\"heatB\":" + std::to_string(intel_.HeatOfSide(world_.get(), "B"));
    // 敌枪构成（五类计数）
    int sniper = 0, rifle = 0, smg = 0, shotgun = 0, pistol = 0;
    for (const auto& b : actors_) {
        if (!b.IsActive() || !b.alive || b.team == t) continue;
        WeaponId bestW = WeaponId::None;
        for (int w = 0; w <= 1; ++w) if (b.weapons[w] != WeaponId::None) { bestW = b.weapons[w]; break; }
        if (bestW == WeaponId::None) continue;
        switch (WeaponDB::Get(bestW).cls) {
            case WeaponClass::Sniper:  ++sniper;  break;
            case WeaponClass::Rifle:   ++rifle;   break;
            case WeaponClass::SMG:     ++smg;     break;
            case WeaponClass::Shotgun: ++shotgun; break;
            default:                   ++pistol;  break;
        }
    }
    o += ",\"enemyComp\":{\"sniper\":" + std::to_string(sniper)
         + ",\"rifle\":" + std::to_string(rifle)
         + ",\"smg\":" + std::to_string(smg)
         + ",\"shotgun\":" + std::to_string(shotgun)
         + ",\"pistol\":" + std::to_string(pistol) + "}";
    // 道具明细（烟/闪/火/雷）
    int smoke = 0, flash = 0, fire = 0, he = 0;
    for (const auto& b : actors_) {
        if (!b.IsActive() || !b.alive || b.team != t) continue;
        for (int gi = 0; gi < b.grenadeCount; ++gi) {
            switch (b.grenades[gi]) {
                case WeaponId::GrenadeSmoke: ++smoke; break;
                case WeaponId::GrenadeFlash: ++flash; break;
                case WeaponId::GrenadeMolotov:
                case WeaponId::GrenadeIncendiary: ++fire; break;
                case WeaponId::GrenadeHE: ++he; break;
                default: break;
            }
        }
    }
    o += ",\"utility\":{\"smoke\":" + std::to_string(smoke)
         + ",\"flash\":" + std::to_string(flash)
         + ",\"fire\":" + std::to_string(fire)
         + ",\"he\":" + std::to_string(he) + "}";
    o += ",\"buyPlan\":\"" + std::string(BuyPlanLabel(st.buyPlan)) + "\"";
    o += ",\"lossStreak\":" + std::to_string(GetLossStreak(t));
    o += ",\"changesThisRound\":" + std::to_string(st.changesThisRound);   // T05：变更预算计数
    // T20：钳子感知回防信号（有钳持有者 → mustRush 阈值 5s；无钳 → 11s）。
    {
        bool anyKit = false;
        for (const auto& b : actors_) {
            if (b.IsActive() && b.team == Team::CT && b.alive && b.hasDefuseKit) { anyKit = true; break; }
        }
        o += ",\"anyKit\":" + std::string(anyKit ? "true" : "false");
        o += ",\"mustRushAt\":" + std::to_string(anyKit ? 5.0f : 11.0f);
    }
    o += ",\"lastAttackSide\":\"" + JsonEscape(st.lastAttackSide) + "\"";
    o += ",\"sameSideStreak\":" + std::to_string(st.sameSideStreak);
    const std::string enemySide = st.memory.empty() ? "" : st.memory.back().enemyAttackSide;
    o += ",\"enemyLastAttackSide\":\"" + JsonEscape(enemySide) + "\"";
    int ecA = 0, ecB = 0;
    for (const auto& b : actors_) {
        if (!b.IsActive() || !b.alive || b.team == t) continue;
        const char cs = AreaSideOf(world_ ? world_->FindAreaAt(b.x, b.y) : "");
        if (cs == 'A') ++ecA; else if (cs == 'B') ++ecB;
    }
    for (const auto& c : ContactsOf(t)) {
        const char cs = AreaSideOf(world_ ? world_->FindAreaAt(c.x, c.y) : "");
        if (cs == 'A') ++ecA; else if (cs == 'B') ++ecB;
    }
    o += ",\"enemyCountA\":" + std::to_string(ecA) + ",\"enemyCountB\":" + std::to_string(ecB);
    // T31：区域控制信号（我方在该区 ≥1 人存活 = 已控）。
    {
        auto allyCount = [&](char side) -> int {
            int n = 0;
            for (const auto& b : actors_) {
                if (!b.IsActive() || !b.alive || b.team != t) continue;
                const char cs = AreaSideOf(world_ ? world_->FindAreaAt(b.x, b.y) : "");
                if (cs == side) ++n;
            }
            return n;
        };
        const int allyA = allyCount('A'), allyB = allyCount('B'), allyM = allyCount('M');
        o += ",\"control\":{\"A\":" + std::string(allyA >= 1 ? "true" : "false")
             + ",\"B\":" + std::string(allyB >= 1 ? "true" : "false")
             + ",\"Mid\":" + std::string(allyM >= 1 ? "true" : "false") + "}";
    }
    o += ",\"lastOrder\":\"" + JsonEscape(st.lastOrder) + "\"";
    o += "},\"guards\":\"" + std::string(guard ? guard : "") + "\"";
    o += ",\"sanitized\":\"" + std::string(sanitized ? sanitized : "") + "\"";
    o += ",\"hasBranch\":" + std::string(p.hasBranch ? "true" : "false");
    o += ",\"branchIntent\":\"" + std::string(IntentKey(p.branchIntent)) + "\"";
    o += ",\"branchAfterSec\":" + std::to_string(p.branchAfterSec);
    o += ",\"gen\":" + std::to_string(p.gen);
    o += "}\n";
    std::fwrite(o.data(), 1, o.size(), wiringLog_);
    std::fflush(wiringLog_);
}

// ---------------------------------------------------------------------------
// T26/T27：角色速度倍率 + 过渡插值
// ---------------------------------------------------------------------------
static float RoleSpeedMulRaw(BotRole role) {
    switch (role) {
        case BotRole::Entry:   return Tuning::SpeedMulEntry;
        case BotRole::Anchor:  return Tuning::SpeedMulAnchor;
        case BotRole::Lurk:    return Tuning::SpeedMulLurk;
        case BotRole::Awper:   return Tuning::SpeedMulAwper;
        default:               return Tuning::SpeedMulSupport;
    }
}

float CS2DSimulation::RoleSpeedMul(size_t botIdx, BotRole role) {
    const float target = RoleSpeedMulRaw(role);
    // T27：换角色后 3 秒内从旧倍率插值过渡到新倍率（RoleTransitionSec）。
    EnsureBotAi();
    if (botIdx >= actors_.size()) return target;

    const float since = clockSec_ - roleChangedAt_[botIdx];
    const float transition = Tuning::ai::RoleTransitionSec;
    if (since < 0.0f || since >= transition) return target;
    const float from = roleFromMul_[botIdx];
    const float k = std::clamp(since / transition, 0.0f, 1.0f);
    return from + (target - from) * k;
}

void CS2DSimulation::SetRoleWithTransition(size_t botIdx, BotRole role) {
    EnsureBotAi();
    if (botIdx >= actors_.size()) return;
    if (actorSquad_[botIdx].role != role) {
        roleFromMul_[botIdx] = RoleSpeedMul(botIdx, actorSquad_[botIdx].role);
        roleChangedAt_[botIdx] = clockSec_;
    }
    actorSquad_[botIdx].role = role;
}

// ---------------------------------------------------------------------------
// T36 coach_rounds.jsonl：一行一队一回合，写盘点不动，只加 gen/版本区分。
// 与 T01 的 coach_wiring.jsonl 并存；未开启时零开销。
// ---------------------------------------------------------------------------
void CS2DSimulation::EnableCoachRoundsLog(const std::string& path) {
    CloseCoachRoundsLog();
    coachRoundsLog_ = std::fopen(path.c_str(), "wb");
    if (!coachRoundsLog_) {
        std::cerr << "[CoachRounds] 无法写入: " << path << std::endl;
        return;
    }
    std::cout << "[CoachRounds] 老师数据日志已开启: " << path << std::endl;
}

void CS2DSimulation::CloseCoachRoundsLog() {
    if (!coachRoundsLog_) return;
    std::fclose(coachRoundsLog_);
    coachRoundsLog_ = nullptr;
}

void CS2DSimulation::TraceCoachRound(Team t) {
    if (!coachRoundsLog_) return;
    const CoachState& st = CoachFor(t);
    const TeamPlan& p = st.plan;
    std::string o;
    o.reserve(512);
    o += "{\"match_id\":\"" + mapId_ + "\"";
    o += ",\"round\":" + std::to_string(round_.RoundNumber());
    o += ",\"team\":\"" + std::string(TeamName(t)) + "\"";
    o += ",\"source\":\"" + p.source + "\"";
    o += ",\"gen\":" + std::to_string(p.gen);
    o += ",\"coach_version\":\"pix37-rules-v2\"";
    o += ",\"intent\":\"" + std::string(IntentKey(p.intent)) + "\"";
    o += ",\"target\":\"" + JsonEscape(p.target) + "\"";
    o += ",\"tempo\":\"" + std::string(TempoKey(p.tempo)) + "\"";
    o += ",\"won\":\"" + std::string(st.memory.empty() ? "?" : (st.memory.back().won ? "1" : "0")) + "\"";
    o += ",\"buyPlan\":\"" + std::string(BuyPlanLabel(st.buyPlan)) + "\"";
    o += ",\"reason\":\"" + JsonEscape(p.reason) + "\"";
    o += ",\"slots\":[";
    for (size_t i = 0; i < p.slots.size(); ++i) {
        if (i) o += ",";
        o += "{\"role\":\"" + std::string(CoachRoleKey(p.slots[i].role)) + "\",\"area\":\"" + JsonEscape(p.slots[i].area) + "\"}";
    }
    o += "]}\n";
    std::fwrite(o.data(), 1, o.size(), coachRoundsLog_);
    std::fflush(coachRoundsLog_);
}

void CS2DSimulation::SanitizeCoachPlan(Team t, TeamPlan& plan) {
    if (!world_ || bombPlanted_) return;

    auto lowerCopy = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    auto slotSide = [&](const SlotOrder& sl) -> char {
        if (sl.area.empty()) return 0;
        const std::string lower = lowerCopy(sl.area);
        if (lower.find("mid") != std::string::npos) return 0;   // 中路视为中立
        float x = 0.0f, y = 0.0f;
        if (!AreaCenterByName(sl.area, x, y)) return 0;
        const CS2DWorld::BombSiteDef* best = nullptr;
        float bestD2 = 1e18f;
        for (const auto& s : world_->GetBombSites()) {
            const float dx = s.cx - x, dy = s.cy - y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = &s; }
        }
        return best ? best->name[0] : 0;
    };

    int sideA = 0, sideB = 0;
    for (const auto& sl : plan.slots) {
        const char c = slotSide(sl);
        if (c == 'A') ++sideA;
        else if (c == 'B') ++sideB;
    }
    const bool allOneSide = (sideA > 0 && sideB == 0) || (sideB > 0 && sideA == 0);

    auto reshapeDefault = [&]() {
        // 记录 LLM 原意，便于区分"模型本来就没想法"与"被整形规则吃掉"
        plan.reshaped = true;
        std::cout << "[Sanitize] " << TeamName(t) << " 整形: "
                  << IntentName(plan.intent)
                  << (plan.target.empty() ? "" : ("->" + plan.target))
                  << " slots(A=" << sideA << ",B=" << sideB << ")"
                  << " → 默认分兵" << std::endl;
        const std::string aSite  = AreaHint("A_Site");
        const std::string bSite  = AreaHint("B_Site");
        const std::string mid    = AreaHint("Mid");
        auto areaOr = [](const std::string& prefer, const std::string& fallback) {
            return prefer.empty() ? fallback : prefer;
        };
        const std::string aLong  = areaOr(AreaHint("Long"),  aSite);
        const std::string aShort = areaOr(AreaHint("Short"), aSite);
        const std::string bLong  = areaOr(AreaHint("Tunnel"), bSite);

        if (t == Team::T) {
            plan.slots[0] = { CoachRole::Entry,   aLong,  true, false };
            plan.slots[1] = { CoachRole::Support, aSite,  true, false };
            plan.slots[2] = { CoachRole::Lurk,    mid,    true, true  };
            plan.slots[3] = { CoachRole::Anchor,  bLong,  true, false };
            plan.slots[4] = { CoachRole::Support, bSite,  true, false };
            // P1-2: sanitize runs after ApplyTacticsSlots, so a reshaped
            // default split must carry its own look (mirrors [slots.T.Default]).
            plan.slots[0].lookArea = aSite;
            plan.slots[1].lookArea = aShort;
            plan.slots[2].lookArea = bSite;
            plan.slots[3].lookArea = bSite;
            plan.slots[4].lookArea = bLong;
        } else {
            plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[1] = { CoachRole::Support, aShort, true, false };
            plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
            plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[4] = { CoachRole::Support, bLong,  true, false };
            // P1-2: same as above for CT (mirrors [slots.CT.Default]).
            plan.slots[0].lookArea = aLong;
            plan.slots[1].lookArea = mid;
            plan.slots[2].lookArea = aSite;
            plan.slots[3].lookArea = bLong;
            plan.slots[4].lookArea = mid;
        }
        plan.intent = PlanIntent::Default;
        plan.target.clear();
        plan.tempo = Tempo::Normal;
        plan.hasBranch = false;
    };

    CoachState& st = CoachFor(t);
    const bool isT = (t == Team::T);

    if (isT) {
        // 1) "default" 但 slots 全在同一侧：改成 A/B 分兵
        if (plan.intent == PlanIntent::Default && allOneSide) {
            reshapeDefault();
            plan.reason += "（默认分兵兜底）";
            std::cout << "[Coach] T 防单点：default 全在一侧 → A/B 分兵" << std::endl;
            return;
        }
        // 2) 连续两回合压同一侧：这一回合改默认分兵，强制另一侧有人
        std::string side = plan.target;
        if (side != "A" && side != "B") {
            side = (sideA > sideB) ? "A" : (sideB > sideA) ? "B" : "";
        }
        const bool aggressive =
            plan.intent == PlanIntent::RushA  || plan.intent == PlanIntent::RushB ||
            plan.intent == PlanIntent::SplitA || plan.intent == PlanIntent::SplitB ||
            plan.intent == PlanIntent::FakeThenOther;
        if (aggressive && !side.empty() && side == st.lastAttackSide) {
            reshapeDefault();
            plan.reason += "（同侧连续，改分兵）";
            std::cout << "[Coach] T 防重复：连续压 " << side
                      << " → 改 A/B 分兵" << std::endl;
        }
        return;
    }

    // CT：默认分兵全堆一侧 → 改 A/B/中路
    if (plan.intent == PlanIntent::Default && allOneSide) {
        reshapeDefault();
        plan.reason += "（默认分兵兜底）";
        std::cout << "[Coach] CT 防单点：default 全在一侧 → A/B/中路" << std::endl;
        return;
    }

    // CT：无情报的 stack 不成立，改成默认分兵，避免每回合全堆 A
    if (plan.intent == PlanIntent::StackA || plan.intent == PlanIntent::StackB) {
        const std::string side = (plan.intent == PlanIntent::StackA) ? "A" : "B";
        const std::string other = (side == "A") ? "B" : "A";
        const float heatSide  = intel_.HeatOfSide(world_.get(), side);
        const float heatOther = intel_.HeatOfSide(world_.get(), other);
        if (intel_.ObservationCount() < 1 || heatSide - heatOther < 0.4f) {
            reshapeDefault();
            plan.reason += "（无情报不赌点）";
            std::cout << "[Coach] CT 防单点：stack_" << side
                      << " 无足够情报 → 改默认分兵" << std::endl;
        }
    }
}

// ---------------------------------------------------------------------------
// 计划下发
// ---------------------------------------------------------------------------
void CS2DSimulation::CoachApplyPlan(Team t) {
    CoachState& st = CoachFor(t);
    if (!st.plan.valid) return;

    // 度量：意图真的变了才算一次变更（HUD/日志里的 reason 抖动不算）
    {
        const int nowIntent = static_cast<int>(st.plan.intent);
        if (st.appliedIntent >= 0 && nowIntent != st.appliedIntent) {
            ++counters_.intentChanges;
            ++st.changesThisRound;          // T05：变更预算接线（零写 → 真变自增）
            st.lastIntentChangeSec = clockSec_;
        }
        st.appliedIntent = nowIntent;
    }

    auto areaSiteIndex = [&](const std::string& area) -> int {
        if (area.empty() || !world_) return -1;
        std::string low = area;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low.find("mid") != std::string::npos) return -1;   // 中路是独立岗位
        float x = 0.0f, y = 0.0f;
        if (!AreaCenterByName(area, x, y)) return -1;
        int best = -1;
        float bestD2 = 1e18f;
        const auto& sites = world_->GetBombSites();
        for (size_t si = 0; si < sites.size(); ++si) {
            const float dx = sites[si].cx - x;
            const float dy = sites[si].cy - y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(si); }
        }
        return best;
    };

    int slot = 0;
    int holdOk = 0, holdFail = 0;
    std::ostringstream detail;

    // T22：按能力分槽——先把本队存活 BOT 收集起来，
    // 用确定性键（拿 AWP / 金钱 / 难度）排序，再按槽位角色匹配。
    EnsureBotAi();
    std::vector<size_t> alive;
    for (size_t i = 0; i < actors_.size(); ++i) {
        if (actors_[i].IsBot() && actors_[i].alive && actors_[i].team == t) alive.push_back(i);
    }
    auto hasSniper = [&](size_t i) {
        for (int w = 0; w <= 1; ++w) {
            const WeaponId wid = actors_[i].weapons[w];
            if (wid != WeaponId::None && WeaponDB::Get(wid).cls == WeaponClass::Sniper) return true;
        }
        return false;
    };
    // 保枪时优先低余额；其余计划按难度优先分配前置岗位。
    std::sort(alive.begin(), alive.end(), [&](size_t a, size_t b) {
        if (st.plan.intent == PlanIntent::Save && actors_[a].money != actors_[b].money)
            return actors_[a].money < actors_[b].money;
        if (AiAt(a).difficulty != AiAt(b).difficulty) return AiAt(a).difficulty > AiAt(b).difficulty;
        return a < b;
    });
    const size_t unassigned = actors_.size();
    std::array<size_t, 5> assigned;
    assigned.fill(unassigned);
    if (st.plan.intent != PlanIntent::Save) {
        for (size_t si = 0; si < assigned.size(); ++si) {
            if (st.plan.slots[si].role != CoachRole::Awper) continue;
            auto sniper = std::find_if(alive.begin(), alive.end(), [&](size_t i) {
                return actors_[i].weapons[0] == WeaponId::AWP;
            });
            if (sniper == alive.end()) sniper = std::find_if(alive.begin(), alive.end(), hasSniper);
            if (sniper == alive.end()) break;
            assigned[si] = *sniper;
            alive.erase(sniper);
        }
    }
    auto next = alive.begin();
    for (size_t& assignedBot : assigned) {
        if (assignedBot == unassigned && next != alive.end()) assignedBot = *next++;
    }

    std::vector<std::pair<float, float>> usedHoldPts;
    std::vector<char> holdReassigned(actors_.size(), 0);
    auto collectUsed = [&](size_t self) {
        std::vector<std::pair<float, float>> used = usedHoldPts;
        for (size_t j = 0; j < actors_.size(); ++j) {
            if (j == self || !actors_[j].IsBot() || actors_[j].team != t || !actors_[j].alive) continue;
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

    for (size_t si = 0; si < assigned.size(); ++si) {
        const size_t i = assigned[si];
        if (i == unassigned) continue;
        const SlotOrder& so = st.plan.slots[si];
        auto& bot = actors_[i];
        auto& ai = AiAt(i);

        switch (so.role) {
            case CoachRole::Entry:  SetRoleWithTransition(i, BotRole::Entry);   break;
            case CoachRole::Anchor: SetRoleWithTransition(i, BotRole::Anchor);  break;
            case CoachRole::Lurk:   SetRoleWithTransition(i, BotRole::Lurk);    break;
            case CoachRole::Awper:  SetRoleWithTransition(i, BotRole::Awper);   break;
            default:                SetRoleWithTransition(i, BotRole::Support); break;
        }

        float cx = 0.0f, cy = 0.0f;
        // 保枪：不跑回出生点，就地收缩（以当前位置为中心小范围站位）
        if (st.plan.intent == PlanIntent::Save) {
            std::uniform_real_distribution<float> jit(-36.0f, 36.0f);
            cx = bot.x + jit(rng_);
            cy = bot.y + jit(rng_);
            ai.goalArea = std::string();
            SetBotGoal(i, cx, cy, BotRuntime::GoalSource::Coach, /*force=*/true);
            actorSquad_[i].site = -1;
            ++holdOk;
            detail << " #" << si << "=(save)";
            actorSquad_[i].canTrade  = false;
            actorSquad_[i].canRotate = false;
            ++slot;
            continue;
        }
        if (!so.area.empty() && AreaCenterByName(so.area, cx, cy)) {
            const int areaSite = areaSiteIndex(so.area);
            bool toSiteCenter = false;
            if (t == Team::T && !bombPlanted_ && !st.plan.target.empty()) {
                const auto& sites = world_->GetBombSites();
                if (areaSite >= 0 && areaSite < static_cast<int>(sites.size()) &&
                    sites[static_cast<size_t>(areaSite)].name == st.plan.target) {
                    cx = sites[static_cast<size_t>(areaSite)].cx;
                    cy = sites[static_cast<size_t>(areaSite)].cy;
                    toSiteCenter = true;
                }
            }
            ai.goalArea = so.area;

            const bool useAnchor = (so.hold != HoldStyle::Default) ||
                                   !so.lookArea.empty() || so.lookAngle > -90.0f;
            if (useAnchor) {
                const std::string teamKey = (t == Team::T) ? "T." : "CT.";
                if (const auto* a = tactics_.Anchor(teamKey + so.area); a) {
                    cx = a->first;
                    cy = a->second;
                } else if (const auto* a2 = tactics_.Anchor(so.area); a2) {
                    cx = a2->first;
                    cy = a2->second;
                }
            }
            {
                std::vector<std::pair<float, float>> used = collectUsed(i);
                if (tooClose(used, cx, cy))
                    SpreadHoldPoint(cx, cy, static_cast<int>(used.size()), cx, cy, used);
                usedHoldPts.emplace_back(cx, cy);
                holdReassigned[i] = 1;
            }
            SetBotGoal(i, cx, cy, BotRuntime::GoalSource::Coach, /*force=*/true);

            if (so.lookAngle > -90.0f) {
                ai.lookAngle = so.lookAngle;
                st.plan.slots[si].lookAngle = so.lookAngle;
            } else if (!so.lookArea.empty()) {
                float lx = 0.0f, ly = 0.0f;
                if (AreaCenterByName(so.lookArea, lx, ly)) {
                    ai.lookAngle = std::atan2(ly - cy, lx - cx);
                    st.plan.slots[si].lookAngle = ai.lookAngle;
                }
            }
            ai.holdStyle = static_cast<int>(so.hold);
            ai.utilKind  = so.util;
            ai.utilDone  = (so.util <= 0);
            ai.utilThrowFailed = false;
            if (so.util > 0 && !so.utilArea.empty()) {
                float ux = 0.0f, uy = 0.0f;
                if (AreaCenterByName(so.utilArea, ux, uy)) { ai.utilX = ux; ai.utilY = uy; }
            }
            actorSquad_[i].site = areaSite;
            ++holdOk;
            detail << " #" << si << "=" << so.area;
        } else {
            ClearBotGoal(i);
            actorSquad_[i].site = -1;
            ++holdFail;
            detail << " #" << si << "=" << (so.area.empty() ? "(empty)" : so.area + "!");
        }
        actorSquad_[i].canTrade  = so.canTrade;
        actorSquad_[i].canRotate = so.canRotate;
        ++slot;
    }
    // C4 携带者若是 BOT，必须跟主攻侧走
    if (t == Team::T && !bombPlanted_ && !bombDropped_ &&
        st.plan.intent != PlanIntent::Save) {
        const Player* carrier = FindActor(bombCarrier_);
        if (carrier && carrier->IsBot() && carrier->team == Team::T && carrier->alive) {
            std::string side = st.plan.target;
            if (side != "A" && side != "B") {
                int a = 0, b = 0;
                const auto& sites = world_->GetBombSites();
                for (const auto& so : st.plan.slots) {
                    const int idx = areaSiteIndex(so.area);
                    if (idx < 0 || idx >= static_cast<int>(sites.size())) continue;
                    if (sites[static_cast<size_t>(idx)].name == "A") ++a;
                    else if (sites[static_cast<size_t>(idx)].name == "B") ++b;
                }
                const int ownSite = actorSquad_[static_cast<size_t>(carrier->id)].site;
                if (ownSite >= 0 && ownSite < static_cast<int>(sites.size())) {
                    side = sites[static_cast<size_t>(ownSite)].name;
                }
                if (side != "A" && side != "B") side = (a >= b) ? "A" : "B";
            }

            const std::string siteArea = AreaHint(side + "_Site");
            const int siteIdx = areaSiteIndex(siteArea);
            float cx = 0.0f, cy = 0.0f;
            bool havePoint = false;
            if (siteIdx >= 0 && siteIdx < static_cast<int>(world_->GetBombSites().size())) {
                cx = world_->GetBombSites()[static_cast<size_t>(siteIdx)].cx;
                cy = world_->GetBombSites()[static_cast<size_t>(siteIdx)].cy;
                havePoint = true;
            } else if (AreaCenterByName(siteArea, cx, cy)) {
                havePoint = true;
            }
            if (havePoint) {
                const size_t ci = static_cast<size_t>(carrier->id);
                auto& cai = AiAt(ci);
                auto& csq = actorSquad_[ci];
                cai.goalArea  = siteArea;
                {
                    const auto& cai0 = AiAt(ci);
                    const float sdx = cx - cai0.planHoldX, sdy = cy - cai0.planHoldY;
                    const bool selfSame = cai0.hasPlanHold &&
                                          (sdx * sdx + sdy * sdy < 40.0f * 40.0f);
                    if (!selfSame) {
                        std::vector<std::pair<float, float>> used = collectUsed(ci);
                        if (tooClose(used, cx, cy))
                            SpreadHoldPoint(cx, cy, static_cast<int>(used.size()), cx, cy, used);
                    }
                }
                SetBotGoal(ci, cx, cy, BotRuntime::GoalSource::Coach, /*force=*/true);
                csq.site      = siteIdx;
                csq.role      = BotRole::Entry;
                csq.canRotate = false;
                std::cout << "[Coach] T C4携带者 #" << bombCarrier_
                          << " 跟随主攻 " << side << std::endl;
            }
        }
    }

    std::cout << "[Coach] Apply " << TeamName(t)
              << " holdOk=" << holdOk << " holdFail=" << holdFail
              << detail.str() << std::endl;

    TraceCoachPlan(t);   // 评估用：一条计划一行（未开启 plan trace 时零开销）
    TraceWiring(t, "", st.plan.reshaped ? "sanitized" : "", nullptr);   // T01：规则计划结构化日志
}

// ---------------------------------------------------------------------------
// 回合记忆
// ---------------------------------------------------------------------------
void CS2DSimulation::CoachRecordRound(Team t, bool won) {
    CoachState& st = CoachFor(t);

    // T07：sameSideStreak 接线——按实际主攻侧更新（同侧 +1、换侧清零）。
    // 只对 T 有意义（T 才是主攻方）；CT 读的是敌方的 sameSideStreak 派生物。
    {
        const std::string side = (t == Team::T) ? st.plan.target : std::string();
        if (t == Team::T && (side == "A" || side == "B")) {
            if (side == st.lastAttackSide) ++st.sameSideStreak;
            else { st.sameSideStreak = 1; st.lastAttackSide = side; }
        }
    }
    // T18：跨回合经济状态机——记录本回合买档与胜负，供 DecideBuyPlan 做状态判断。
    st.lastBuyPlan = st.buyPlan;
    st.lastBuyWon  = won;

    RoundMemo m;
    m.roundNo          = round_.RoundNumber();
    m.plan             = st.plan.intent;
    m.target           = st.plan.target;
    m.won              = won;
    m.scoreT           = teams_.ScoreT();
    m.scoreCT          = teams_.ScoreCT();
    m.aliveEnd         = AliveCount(t);
    m.bombPlanted      = bombPlanted_;
    m.plantSite        = (world_ && bombX_ && bombY_) ? world_->FindAreaAt(bombX_, bombY_)
                                                     : std::string();
    m.firstContactArea = st.firstContactArea;
    // T13：敌方习惯——从对手记忆推导敌方最近主攻侧（本回合对手的主攻 target）。
    {
        const CoachState& opp = CoachFor(TeamManager::Opponent(t));
        const TeamPlan& oppPlan = opp.plan;
        if (oppPlan.target == "A" || oppPlan.target == "B") {
            m.enemyAttackSide = oppPlan.target;
        } else if (!opp.memory.empty()) {
            const RoundMemo& om = opp.memory.back();
            if (om.target == "A" || om.target == "B") m.enemyAttackSide = om.target;
        }
    }
    m.deaths           = st.deaths;

    st.memory.push_back(m);
    while (st.memory.size() > 10) st.memory.pop_front();

    // T36：写老师数据行（一行一队一回合；未开启零开销）。
    TraceCoachRound(t);

    // 回合结束额外调用一次复盘（异步，不阻塞）
    CoachTryDebrief(t);

    st.deaths.clear();
    st.firstContactArea.clear();
    st.timeLowFired    = false;
    st.matchPointFired = false;
}

void CS2DSimulation::CoachResetRound() {
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        st.pending.clear();
        st.mergeWindow     = 0.0f;
        st.callsThisRound  = 0;
        st.changesThisRound = 0;      // T05：变更预算每回合清零
        st.appliedIntent    = -1;     // T05：首条计划不算"变更"
        st.deaths.clear();
        st.firstContactArea.clear();
        st.timeLowFired    = false;
        st.matchPointFired = false;
        st.llmPending      = false;
        st.llmAnswered     = false;
    }
    for (size_t i = 0; i < actors_.size(); ++i) ClearBotGoal(i);
}


// ---------------------------------------------------------------------------
// 回合开始（买枪之前）：立刻用规则教练出计划，保证 save/eco 能拦住购买；
// 同时把 RoundStart 事件排进队列，稍后由 LLM 覆盖。
// ---------------------------------------------------------------------------
void CS2DSimulation::CoachBeginRound() {
    for (Team t : {Team::T, Team::CT}) {
        ++CoachFor(t).gen;
        CoachEvent ev;
        ev.type     = CoachEventType::RoundStart;
        ev.priority = 100.0f;
        ev.atSec    = clockSec_;

        CoachState& st = CoachFor(t);
        // 先定买档（纯经济快照，不依赖 intent），再出计划——这样 RuleCoach 的打分
        // 能读到本回合的 buyPlan（穷局/存钱不主动进攻的闭集判断才正确）。
        st.buyPlan = DecideBuyPlan(t);

        TeamPlan plan = RuleCoach(t, ev);
        plan.gen       = st.gen;
        plan.createdAt = clockSec_;
        plan.expiresAt = clockSec_ + 20.0f;
        plan.valid     = true;

        st.plan          = plan;
        st.reason        = plan.reason;
        st.planExpiresAt = plan.expiresAt;
        st.stickyLlm     = false; // BeginRound sticky clear
        // Save intent 强制 Eco（保枪不采购）。
        if (plan.intent == PlanIntent::Save) {
            st.buyPlan = BuyPlan::Eco;
        }
        coachPlanChangedAt_ = clockSec_;

        std::cout << "[Coach] " << TeamName(t) << " 回合计划 -> "
                  << IntentName(plan.intent)
                  << " 购买=" << BuyPlanLabel(st.buyPlan)
                  << " | " << plan.reason << std::endl;
        CoachApplyPlan(t);
    }
}


// 队伍平均余额（买枪前快照；没有快照时退回当前值）
int CS2DSimulation::GetRoundStartEconomy(Team t) const {
    if (roundEconomySnapshotValid_ && mode_ != MatchMode::DeathMatch) {
        return (t == Team::T) ? roundStartAvgMoneyT_ : roundStartAvgMoneyCT_;
    }
    int sum = 0, n = 0;
    for (const auto& b : actors_) {
        if (b.IsActive() && b.team == t) { sum += b.money; ++n; }
    }
    return (n > 0) ? sum / n : 0;
}

void CS2DSimulation::SnapshotRoundEconomy() {
    auto avg = [&](Team t) {
        int sum = 0, n = 0;
        for (const auto& b : actors_) {
            if (b.IsActive() && b.team == t) { sum += b.money; ++n; }
        }
        return (n > 0) ? sum / n : 0;
    };
    roundStartAvgMoneyT_  = avg(Team::T);
    roundStartAvgMoneyCT_ = avg(Team::CT);
    roundEconomySnapshotValid_ = true;
}

// 队伍平均余额（含玩家）
bool CS2DSimulation::LowEconomyAverage(Team t) const {
    // 手枪局有自己的节奏，不按 eco 处理；阈值 1800 更接近 CS 的经济线
    if (round_.RoundNumber() <= 1) return false;
    if (mode_ == MatchMode::DeathMatch) return false;
    return GetRoundStartEconomy(t) < 1800;
}

// 购买档：按买枪前经济、是否该保枪、敌方经济决定。
BuyPlan CS2DSimulation::DecideBuyPlan(Team t) const {
    if (mode_ != MatchMode::BombDefusal) return BuyPlan::Full;

    const int avg    = GetRoundStartEconomy(t);
    const int oppAvg = GetRoundStartEconomy(TeamManager::Opponent(t));

    // 手枪局：能买甲/雷就花，别按“穷局”直接空手。
    if (round_.RoundNumber() <= 1) return BuyPlan::Force;

    const CoachState& st = CoachFor(t);

    // T19：赛点 / 半场末 → 强起（除非经济实在允许满起）。
    const bool matchPoint = (teams_.ScoreT() == teams_.ScoreToWin() - 1 ||
                             teams_.ScoreCT() == teams_.ScoreToWin() - 1);
    if (matchPoint && avg < 3500 && avg >= 1800) return BuyPlan::Force;

    // 真正该保枪时优先 Eco；否则再按余额分 Half/Force/Full。
    if (ShouldSaveNow(t)) return BuyPlan::Eco;

    // T18：跨回合经济状态机（不删既有阈值逻辑，只在其上叠加状态判断）。
    // 1) 上回合强起且输了 → 本回合强制 Eco（禁连强）。
    if (st.lastBuyPlan == BuyPlan::Force && !st.lastBuyWon) return BuyPlan::Eco;
    // 2) 连败 ≥2 且败方补偿能让下一回合满起 → Eco 存钱（本回合已穷才存，富了照打）。
    // P2-1: widen the loss-streak save: the old avg<2600 almost never fired
    // mid-match (round-start money is bimodal: <1800 in R1 only, >=3500 after;
    // the 1800-2600 band holds ~0.3% of rounds). 4500 still leaves a team rich
    // enough to Full-buy the round after saving (post-Eco avg is ~4500 minus a
    // small Eco spend plus the loss bonus), so a save is one round, not a loop.
    if (GetLossStreak(t) >= 2 && avg < 4500) return BuyPlan::Eco;
    // 3) 我穷敌更穷 → 强起打经济（手册决策序第 3 条；阈值不动）。
    if (avg < 1800 && oppAvg > 0 && oppAvg <= avg && avg >= 1200) return BuyPlan::Force;

    if (avg >= 3500) {
        // 我方满钱、敌方穷：SMG + 甲的反 eco 打法。
        if (oppAvg > 0 && oppAvg < 2200) return BuyPlan::AntiEco;
        return BuyPlan::Full;
    }
    if (avg >= 2600) return BuyPlan::Half;
    if (avg >= 1800) return BuyPlan::Force;
    return BuyPlan::Eco;
}

BuyPlan CS2DSimulation::GetBuyPlan(Team t) const {
    return CoachFor(t).buyPlan;
}

const char* CS2DSimulation::BuyPlanLabel(BuyPlan p) {
    switch (p) {
        case BuyPlan::Eco:     return "Eco";
        case BuyPlan::Half:    return "Half";
        case BuyPlan::Force:   return "Force";
        case BuyPlan::AntiEco: return "AntiEco";
        case BuyPlan::Full:
        default:               return "Full";
    }
}

// 保枪只在「真的该保」时成立：
//   1) 还没下包（下了包就必须守包/回防）
//   2) 回合前 20 秒（中后期再保枪只会送掉回合）
//   3) 不是人数优势（多打少还保枪 = 主动让出主动权）
//   4) 经济确实崩（已有阈值）
bool CS2DSimulation::ShouldSaveNow(Team t) const {
    if (bombPlanted_) return false;
    // 手枪局必须买：800 经济下"保枪"等于白送一回合，还会把买枪档位倒退成 Eco。
    if (round_.RoundNumber() <= 1) return false;
    if (GetRoundElapsed() > 45.0f) return false;
    if (AliveCount(t) > AliveCount(TeamManager::Opponent(t))) return false;

    // T 已经站上包点 = 已经拿到主动权，再保枪等于把包点白送
    if (t == Team::T && world_) {
        for (const auto& b : actors_) {
            if (!b.IsActive() || !b.alive || b.team != Team::T) continue;
            if (world_->FindBombSiteAt(b.x, b.y)) return false;
        }
    }
    return LowEconomyAverage(t);
}

bool CS2DSimulation::CanRotateNow(Team t) const {
    if (mode_ != MatchMode::BombDefusal) return false;
    if (bombPlanted_) return false;
    if (bombDropped_) return false;
    if (t != Team::T) return false;
    if (!round_.IsLive()) return false;
    if (round_.PhaseRemaining() < 30.0f) return false;
    if (AliveCount(t) + 1 < AliveCount(TeamManager::Opponent(t))) return false;
    if (clockSec_ - lastRotateAt_ < 8.0f) return false;
    if (intel_.ObservationCount() < Tuning::ai::RotateMinIntel) return false;

    const CoachState& st = CoachFor(t);
    if (!st.plan.valid) return false;
    if (st.plan.intent != PlanIntent::Default) return false;
    if (clockSec_ - st.plan.createdAt < Tuning::ai::RotateMinPlanAge) return false;

    // 已经有人进包点 / C4 携带者接近包点：优先下包，不转点。
    if (world_) {
        for (const auto& b : actors_) {
            if (!b.IsActive() || !b.alive || b.team != Team::T) continue;
            if (world_->FindBombSiteAt(b.x, b.y)) return false;
            if (b.id == bombCarrier_) {
                for (const auto& s : world_->GetBombSites()) {
                    const float dx = s.cx - b.x, dy = s.cy - b.y;
                    if (dx * dx + dy * dy < 280.0f * 280.0f) return false;
                }
            }
        }
    }
    return true;
}

void CS2DSimulation::CoachTryRotate(Team t) {
    if (!CanRotateNow(t)) return;

    CoachState& st = CoachFor(t);
    std::string cur = st.plan.target.empty() ? st.lastAttackSide : st.plan.target;
    if (cur != "A" && cur != "B") cur = "A";   // 默认分兵时按 A 侧推进，可被转点改写
    if (cur != "A" && cur != "B") return;
    const std::string other = (cur == "A") ? "B" : "A";

    const float heatCur = intel_.HeatOfSide(world_.get(), cur);
    const float heatOth = intel_.HeatOfSide(world_.get(), other);
    const std::string curSite = AreaHint(cur == "A" ? "A_Site" : "B_Site");
    const std::string othSite = AreaHint(other == "A" ? "A_Site" : "B_Site");

    // 证据门槛：当前目标侧明显更重防才转
    // P1-3: rotate heat gap lives in Tuning (RotateHeatDelta).
    if (heatCur - heatOth < Tuning::ai::RotateHeatDelta) return;

    TeamPlan plan = st.plan;
    plan.intent    = (other == "A") ? PlanIntent::RotateA : PlanIntent::RotateB;
    plan.target    = other;
    plan.tempo     = Tempo::Fast;
    plan.reason    = cur + " 点重防，转 " + other;
    plan.source    = "rule";        // 转点是规则触发，不算 LLM 计划
    plan.latencyWall = 0.0f;
    plan.gen       = ++st.gen;
    plan.createdAt = clockSec_;
    plan.expiresAt = clockSec_ + 12.0f;
    plan.valid     = true;

    // 执行：两人先到中路重组，三人转推新点（避免全员拉一条长蛇）
    const std::string midArea = AreaHint("Mid");
    plan.slots[0] = { CoachRole::Entry,   othSite, true, false };
    plan.slots[1] = { CoachRole::Support, othSite, true, false };
    plan.slots[2] = { CoachRole::Anchor,  othSite, true, false };
    plan.slots[3] = { CoachRole::Lurk,    midArea, true, true };
    plan.slots[4] = { CoachRole::Support, midArea, true, false };
    ApplySlotTemplate(t, plan);

    st.plan        = plan;
    st.reason      = plan.reason;
    st.planExpiresAt = plan.expiresAt;
    st.rotateFrom  = cur;
    st.rotateTo    = other;
    st.rotateAt    = clockSec_;
    st.lastAttackSide = other;
    lastRotateAt_  = clockSec_;
    coachPlanChangedAt_ = clockSec_;

    std::cout << "[Rotate] " << TeamName(t) << " " << cur << "->" << other
              << " heat" << cur << "=" << heatCur << " heat" << other << "=" << heatOth
              << " 剩余=" << static_cast<int>(round_.PhaseRemaining()) << "s"
              << " 人数=" << AliveCount(t) << "v" << AliveCount(TeamManager::Opponent(t))
              << std::endl;

    CoachApplyPlan(t);
}


