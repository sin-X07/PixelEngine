#include "CS2DGame.hpp"
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
        case PlanIntent::Save:          return "保枪";
        case PlanIntent::FakeThenOther: return "假打转点";
        default:                        return "默认分兵";
    }
}

} // namespace

CS2DGame::CoachState& CS2DGame::CoachFor(Team t) {
    return (t == Team::T) ? coachT_ : coachCT_;
}
const CS2DGame::CoachState& CS2DGame::CoachFor(Team t) const {
    return (t == Team::T) ? coachT_ : coachCT_;
}

const TeamPlan& CS2DGame::GetCoachPlan(Team t) const { return CoachFor(t).plan; }
const std::string& CS2DGame::GetCoachReason(Team t) const { return CoachFor(t).reason; }

// ---------------------------------------------------------------------------
// 区域名 → 中心点（按面积加权）
// ---------------------------------------------------------------------------
bool CS2DGame::AreaCenterByName(const std::string& name, float& ox, float& oy) const {
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
std::string CS2DGame::AreaHint(const std::string& key) const {
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
void CS2DGame::CoachOnEvent(Team t, CoachEventType type, const std::string& area) {
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
void CS2DGame::CoachUpdate(float dt) {
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

void CS2DGame::CoachThink(Team t, const CoachEvent& ev) {
    CoachState& st = CoachFor(t);

    const bool criticalEv =
        ev.type == CoachEventType::RoundStart ||
        ev.type == CoachEventType::BombPlanted ||
        ev.type == CoachEventType::DefuseStarted ||
        ev.type == CoachEventType::TimeLow ||
        ev.type == CoachEventType::EconomyLow ||
        ev.type == CoachEventType::MatchPoint ||
        ev.type == CoachEventType::HumanDied;

    // Soft events: keep a still-valid plan (skip full replan (sticky soft)).
    // Kill spam was flipping reason every death while intent stayed Default split.
    const bool softEv =
        ev.type == CoachEventType::Heartbeat ||
        ev.type == CoachEventType::EnemyDown ||
        ev.type == CoachEventType::AllyDown ||
        ev.type == CoachEventType::EnemySpotted ||
        ev.type == CoachEventType::DefuseAborted;

    if (st.plan.valid && clockSec_ < st.planExpiresAt && !bombPlanted_ && softEv) {
        if (ev.type == CoachEventType::EnemyDown) {
            st.plan.tempo = Tempo::Fast;
        } else if (ev.type == CoachEventType::AllyDown) {
            st.plan.tempo = Tempo::Slow;
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
        ev.type != CoachEventType::BombPlanted) {
        if (st.callsThisRound > 0) --st.callsThisRound;
        std::cout << "[Coach] T 执行锁定 " << st.plan.target
                  << " (ev#" << static_cast<int>(ev.type) << " no-replan)" << std::endl;
        return;
    }

    // ---- 变更预算：意图至少保持 IntentMinHold，每回合最多 IntentMaxChanges 次 ----
    // 治"无头苍蝇"的核心：没有新信息就不换指令（新信息由 criticalEv 放行）。
    if (!criticalEv && st.plan.valid && !bombPlanted_) {
        const float held = clockSec_ - st.plan.createdAt;
        const bool tooSoon = held < Tuning::ai::IntentMinHold ||
                             (clockSec_ - st.lastIntentChangeSec) < Tuning::ai::IntentChangeCooldown;
        const bool budgetOut = st.changesThisRound >= Tuning::ai::IntentMaxChanges;
        if (tooSoon || budgetOut) {
            st.planExpiresAt  = clockSec_ + Tuning::ai::IntentRenewSec;
            st.plan.expiresAt = st.planExpiresAt;
            ++counters_.renewals;
            if (st.callsThisRound > 0) --st.callsThisRound;
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
                // CT：已知 C4 掉落位置 → 去卡那个区域
                const std::string ia = world_ ? world_->FindAreaAt(bombIntelX_, bombIntelY_)
                                              : std::string();
                plan.intent = PlanIntent::StackA;   // 收缩防守到该区
                plan.target = ia;
                plan.tempo  = Tempo::Normal;
                plan.reason = "发现掉落C4，收缩卡点";
            }
        }
    }
    plan.gen       = st.gen;
    plan.createdAt = clockSec_;
    plan.valid     = true;

    plan.expiresAt = clockSec_ + 20.0f;   // 20 秒后重新评估
    st.planExpiresAt = plan.expiresAt;
    st.plan   = plan;
    st.reason = plan.reason;
    st.stickyLlm = false;          // rules own this plan
    // 中段规则重算时同步购买档（Save→Eco；否则按当前金钱再判）
    st.buyPlan = (plan.intent == PlanIntent::Save) ? BuyPlan::Eco : DecideBuyPlan(t);
    st.lastThinkAt = clockSec_;
    coachPlanChangedAt_ = clockSec_;
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
        case PlanIntent::FakeThenOther: return "Fake";
        default:                  return "Default";
    }
}

// 战术表覆盖：表里存在该 (队伍, 意图) 的槽位配置时，用它替换代码里的默认分兵。
// 表里没有就保持代码默认 → 战术表是可选增强，不会让旧图行为突变。
void CS2DGame::ApplyTacticsSlots(Team t, TeamPlan& plan) {
    if (!tactics_.loaded) return;
    const char* teamKey = (t == Team::T) ? "T" : "CT";
    const auto* slots = tactics_.Slots(teamKey, TacticsIntentKey(plan.intent));
    if (!slots) return;
    if (!plan.templateDefault) return;   // 只覆盖通用分兵模板，事件整形计划保持原样

    for (size_t i = 0; i < plan.slots.size(); ++i) {
        plan.slots[i] = (*slots)[i];
        plan.slots[i].area = tactics_.ResolveArea(plan.slots[i].area);
    }
}

TeamPlan CS2DGame::RuleCoach(Team t, const CoachEvent& ev) {
    TeamPlan plan = RuleCoachCore(t, ev);
    plan.source = "rule";     // 评估用：区分规则计划与 LLM 计划
    ApplyTacticsSlots(t, plan);
    return plan;
}

TeamPlan CS2DGame::RuleCoachCore(Team t, const CoachEvent& ev) {
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
                plan.intent = PlanIntent::Default;
                plan.target = onB ? "B" : "A";
                plan.tempo  = Tempo::Slow;
                plan.reason = "包已下：残局守包，等拆包再抓";
            } else {
                plan.intent = onB ? PlanIntent::StackB : PlanIntent::StackA;
                plan.target = onB ? "B" : "A";
                plan.tempo  = (bombLeft < 15.0f) ? Tempo::Fast : Tempo::Normal;
                plan.reason = "包已下：交叉火力守包";
            }
            plan.slots[0] = { CoachRole::Anchor,  siteArea, true, false };
            plan.slots[1] = { CoachRole::Support, siteArea, true, false };
            plan.slots[2] = { CoachRole::Lurk,    onB ? bLong : aLong, true, true };
            plan.slots[3] = { CoachRole::Entry,   onB ? bSite : aSite, true, false };
            plan.slots[4] = { CoachRole::Support, mid, true, false };
            return plan;
        }

        const bool mustRush = (bombLeft <= 11.0f);   // 无钳需 10s，留 1s 余量
        plan.intent = onB ? PlanIntent::RetakeB : PlanIntent::RetakeA;
        plan.target = onB ? "B" : "A";
        plan.tempo  = mustRush ? Tempo::Fast : Tempo::Normal;
        plan.reason = mustRush ? "时间不足，强拆包点" : "集结回防，先清点再拆";
        for (auto& sl : plan.slots) {
            sl = { CoachRole::Entry, siteArea, true, false };
        }
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
            for (auto& sl : plan.slots) sl = { CoachRole::Anchor, siteArea, true, false };
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
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, siteArea, true, false };
        } else {
            plan.intent = onB ? PlanIntent::RetakeB : PlanIntent::RetakeA;
            plan.target = onB ? "B" : "A";
            plan.reason = "正在拆包，掩护拆弹";
            for (auto& sl : plan.slots) sl = { CoachRole::Anchor, siteArea, true, false };
        }
        return plan;
    }
    if (ev.type == CoachEventType::EnemyDown) {
        plan.intent = PlanIntent::Default;
        plan.tempo  = Tempo::Fast;
        plan.reason = "对面减员，前压抓人";
        assign(aLong, aSite, bLong, bSite, mid);
        return plan;
    }
    if (ev.type == CoachEventType::AllyDown) {
        plan.intent = PlanIntent::Default;
        plan.tempo  = Tempo::Slow;
        plan.reason = "我方减员，收缩等机会";
        assign(aSite, aLong, bSite, bLong, mid);
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
    if (ev.type == CoachEventType::TimeLow) {
        plan.tempo = Tempo::Fast;
        if (isT) {
            // 跟随当前主攻侧，不要每次 TimeLow 都硬切 A。
            std::string side = st.plan.target;
            if (side != "A" && side != "B") side = st.lastAttackSide;
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
        }
        return plan;
    }

    // ---- 回合开始：结合记忆选点 ----
    int aTries = 0, aWins = 0, bTries = 0, bWins = 0;
    for (const auto& m : st.memory) {
        if (m.target == "A") { ++aTries; if (m.won) ++aWins; }
        if (m.target == "B") { ++bTries; if (m.won) ++bWins; }
    }
    const bool avoidA = (aTries >= 2 && aWins == 0);
    const bool avoidB = (bTries >= 2 && bWins == 0);

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

    if (isT) {
        if (avoidA && !avoidB) {
            plan.intent = PlanIntent::RushB;
            plan.target = "B";
            plan.tempo  = Tempo::Fast;
            plan.reason = "A 点连续失手，改速推 B";
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, bLong, true, false };
            plan.slots[4] = { CoachRole::Anchor, bSite, true, false };
            plan.templateDefault = true;
        } else if (avoidB && !avoidA) {
            plan.intent = PlanIntent::RushA;
            plan.target = "A";
            plan.tempo  = Tempo::Fast;
            plan.reason = "B 点连续失手，改速推 A";
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, aLong, true, false };
            plan.slots[4] = { CoachRole::Anchor, aSite, true, false };
            plan.templateDefault = true;
        } else {
            plan.intent = PlanIntent::Default;
            plan.reason = "默认开局，A/B 分兵找机会";
            assign(aLong, aSite, bLong, bSite, mid);
        }
    } else {
        plan.intent = PlanIntent::Default;
        plan.reason = "常规守点，A/B 各两人压中路";
        plan.templateDefault = true;
        plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
        plan.slots[1] = { CoachRole::Support, aShort, true, false };
        plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
        plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
        plan.slots[4] = { CoachRole::Support, bLong,  true, false };
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
void CS2DGame::EnablePlanTrace(const std::string& path) {
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

void CS2DGame::ClosePlanTrace() {
    if (!planTrace_) return;
    std::fclose(planTrace_);
    planTrace_ = nullptr;
}

bool CS2DGame::PlanTargetConsistent(Team t, const TeamPlan& plan) const {
    (void)t;
    if (!world_ || plan.target.empty()) return true;
    const char want = plan.target[0];
    if (want != 'A' && want != 'B' && want != 'M') return true;

    auto sideOf = [this](const std::string& area) -> char {
        if (area.empty()) return '?';
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
    };

    int matched = 0;
    for (const auto& so : plan.slots) {
        if (sideOf(so.area) == want) ++matched;
    }
    const int need = (want == 'M') ? 1 : 2;
    return matched >= need;
}

bool CS2DGame::ApplySlotTemplate(Team t, TeamPlan& plan) {
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

void CS2DGame::TraceCoachAdvisory(Team t, const TeamPlan& plan) {
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

void CS2DGame::EnableDecisionLog(const std::string& path) {
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

void CS2DGame::CloseDecisionLog() {
    if (!decisionLog_) return;
    std::fclose(decisionLog_);
    decisionLog_ = nullptr;
}

void CS2DGame::TraceCoachDecision(Team t, const char* status) {
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

void CS2DGame::TraceCoachPlan(Team t) {
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
void CS2DGame::SanitizeCoachPlan(Team t, TeamPlan& plan) {
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
        } else {
            plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
            plan.slots[1] = { CoachRole::Support, aShort, true, false };
            plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
            plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
            plan.slots[4] = { CoachRole::Support, bLong,  true, false };
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
void CS2DGame::CoachApplyPlan(Team t) {
    CoachState& st = CoachFor(t);
    if (!st.plan.valid) return;

    // 度量：意图真的变了才算一次变更（HUD/日志里的 reason 抖动不算）
    {
        const int nowIntent = static_cast<int>(st.plan.intent);
        if (st.appliedIntent >= 0 && nowIntent != st.appliedIntent) {
            ++counters_.intentChanges;
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
    for (size_t i = 0; i < bots_.size(); ++i) {
        auto& bot = bots_[i];
        if (!bot.alive || bot.team != t) continue;
        if (slot >= 5) break;

        const SlotOrder& so = st.plan.slots[static_cast<size_t>(slot)];
        auto& ai = AiAt(i);

        switch (so.role) {
            case CoachRole::Entry:  squad_[i].role = BotRole::Entry;   break;
            case CoachRole::Anchor: squad_[i].role = BotRole::Anchor;  break;
            case CoachRole::Lurk:   squad_[i].role = BotRole::Entry;   break;
            default:                squad_[i].role = BotRole::Support; break;
        }

        float cx = 0.0f, cy = 0.0f;
        // 保枪：不跑回出生点，就地收缩（以当前位置为中心小范围站位）
        if (st.plan.intent == PlanIntent::Save) {
            std::uniform_real_distribution<float> jit(-18.0f, 18.0f);
            cx = bot.x + jit(rng_);
            cy = bot.y + jit(rng_);
            // goalArea 必须在 SetBotGoal 之前写好：SetBotGoal 用它查战术表 [look] 的守点朝向
            ai.goalArea = std::string();
            SetBotGoal(i, cx, cy, BotRuntime::GoalSource::Coach, /*force=*/true);
            // 计划点到达后不要退回默认包点：把小队岗位固定在当前点。
            squad_[i].site = -1;
            ++holdOk;
            detail << " #" << slot << "=(save)";
            squad_[i].canTrade  = false;
            squad_[i].canRotate = false;
            ++slot;
            continue;
        }
        if (!so.area.empty() && AreaCenterByName(so.area, cx, cy)) {
            const int areaSite = areaSiteIndex(so.area);
            // T 进攻期：真正的推进目标是「目标包点中心」，区域中心只当集结/架枪点。
            // 否则 T 会停在区域边缘（例如 A_Long 中心）磨时间，进攻节奏变慢。
            if (t == Team::T && !bombPlanted_ && !st.plan.target.empty()) {
                const auto& sites = world_->GetBombSites();
                if (areaSite >= 0 && areaSite < static_cast<int>(sites.size()) &&
                    sites[static_cast<size_t>(areaSite)].name == st.plan.target) {
                    cx = sites[static_cast<size_t>(areaSite)].cx;
                    cy = sites[static_cast<size_t>(areaSite)].cy;
                }
            }
            ai.goalArea = so.area;   // 先写区域名，SetBotGoal 才能查到 [look] 朝向

            // 锚点优先：战术表 [anchor.<area>]（按阵营）给出"该站的具体点"，
            // 比区域中心更贴近真实架点；没有锚点才用中心。
            // 只有显式声明站姿（来自战术卡）才用锚点；规则模板保持区域中心，
            // 避免自动生成的锚点把整队推到不可走/过于靠前的位置。
            if (so.hold != HoldStyle::Default) {
                const std::string teamKey = (t == Team::T) ? "T." : "CT.";
                if (const auto* a = tactics_.Anchor(teamKey + so.area); a) {
                    cx = a->first;
                    cy = a->second;
                } else if (const auto* a2 = tactics_.Anchor(so.area); a2) {
                    cx = a2->first;
                    cy = a2->second;
                }
            }
            SetBotGoal(i, cx, cy, BotRuntime::GoalSource::Coach, /*force=*/true);

            // ---- 可执行字段：朝向 / 站姿 / 投掷 ----
            if (so.lookAngle > -90.0f) {
                ai.lookAngle = so.lookAngle;
            } else if (!so.lookArea.empty()) {
                float lx = 0.0f, ly = 0.0f;
                if (AreaCenterByName(so.lookArea, lx, ly)) {
                    ai.lookAngle = std::atan2(ly - cy, lx - cx);
                }
            }
            ai.holdStyle = static_cast<int>(so.hold);
            ai.utilKind  = so.util;
            ai.utilDone  = (so.util <= 0);
            if (so.util > 0 && !so.utilArea.empty()) {
                float ux = 0.0f, uy = 0.0f;
                if (AreaCenterByName(so.utilArea, ux, uy)) { ai.utilX = ux; ai.utilY = uy; }
            }
            // 小队岗位与教练计划保持一致；计划点到达后继续守这个区域，
            // 而不是被 AssignSquadRoles 的轮转岗位拉回另一侧。
            squad_[i].site = areaSite;
            ++holdOk;
            detail << " #" << slot << "=" << so.area;
        } else {
            ClearBotGoal(i);
            squad_[i].site = -1;
            ++holdFail;
            detail << " #" << slot << "=" << (so.area.empty() ? "(empty)" : so.area + "!");
        }
        squad_[i].canTrade  = so.canTrade;
        squad_[i].canRotate = so.canRotate;
        ++slot;
    }
    // C4 携带者必须跟主攻侧走：否则他可能被分到另一侧/中路，
    // 队友在 A 打，他一个人在中路或 B 转，永远没人下包。
    if (t == Team::T && !bombPlanted_ && !bombDropped_ &&
        st.plan.intent != PlanIntent::Save &&
        bombCarrier_ >= 0 && bombCarrier_ < static_cast<int>(bots_.size()) &&
        bots_[static_cast<size_t>(bombCarrier_)].team == Team::T &&
        bots_[static_cast<size_t>(bombCarrier_)].alive) {
        std::string side = st.plan.target;
        if (side != "A" && side != "B") {
            // default 分兵没有 target：让携带者跟随人数最多/自己槽位所在的一侧。
            int a = 0, b = 0;
            const auto& sites = world_->GetBombSites();
            for (const auto& so : st.plan.slots) {
                const int idx = areaSiteIndex(so.area);
                if (idx < 0 || idx >= static_cast<int>(sites.size())) continue;
                if (sites[static_cast<size_t>(idx)].name == "A") ++a;
                else if (sites[static_cast<size_t>(idx)].name == "B") ++b;
            }
            const int ownSite = squad_[static_cast<size_t>(bombCarrier_)].site;
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
            const size_t ci = static_cast<size_t>(bombCarrier_);
            auto& cai = AiAt(ci);
            auto& csq = squad_[ci];
            cai.goalArea  = siteArea;   // 先写区域名，SetBotGoal 才能查到 [look] 朝向
            SetBotGoal(ci, cx, cy, BotRuntime::GoalSource::Coach, /*force=*/true);
            csq.site      = siteIdx;
            csq.role      = BotRole::Entry;   // 跟突破组进点，而不是当断后
            csq.canRotate = false;
            std::cout << "[Coach] T C4携带者 #" << bombCarrier_
                      << " 跟随主攻 " << side << std::endl;
        }
    }

    std::cout << "[Coach] Apply " << TeamName(t)
              << " holdOk=" << holdOk << " holdFail=" << holdFail
              << detail.str() << std::endl;

    TraceCoachPlan(t);   // 评估用：一条计划一行（未开启 plan trace 时零开销）
}

// ---------------------------------------------------------------------------
// 回合记忆
// ---------------------------------------------------------------------------
void CS2DGame::CoachRecordRound(Team t, bool won) {
    CoachState& st = CoachFor(t);

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
    m.deaths           = st.deaths;

    st.memory.push_back(m);
    while (st.memory.size() > 10) st.memory.pop_front();

    // 回合结束额外调用一次复盘（异步，不阻塞）
    CoachTryDebrief(t);

    st.deaths.clear();
    st.firstContactArea.clear();
    st.timeLowFired    = false;
    st.matchPointFired = false;
}

void CS2DGame::CoachResetRound() {
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        st.pending.clear();
        st.mergeWindow     = 0.0f;
        st.callsThisRound  = 0;
        st.deaths.clear();
        st.firstContactArea.clear();
        st.timeLowFired    = false;
        st.matchPointFired = false;
        st.llmPending      = false;
        st.llmAnswered     = false;
    }
    for (size_t i = 0; i < bots_.size(); ++i) ClearBotGoal(i);
}


// ---------------------------------------------------------------------------
// 回合开始（买枪之前）：立刻用规则教练出计划，保证 save/eco 能拦住购买；
// 同时把 RoundStart 事件排进队列，稍后由 LLM 覆盖。
// ---------------------------------------------------------------------------
void CS2DGame::CoachBeginRound() {
    for (Team t : {Team::T, Team::CT}) {
        ++CoachFor(t).gen;
        CoachEvent ev;
        ev.type     = CoachEventType::RoundStart;
        ev.priority = 100.0f;
        ev.atSec    = clockSec_;

        TeamPlan plan = RuleCoach(t, ev);
        plan.gen       = CoachFor(t).gen;
        plan.createdAt = clockSec_;
        plan.expiresAt = clockSec_ + 20.0f;
        plan.valid     = true;

        CoachState& st = CoachFor(t);
        st.plan          = plan;
        st.reason        = plan.reason;
        st.planExpiresAt = plan.expiresAt;
        st.stickyLlm     = false; // BeginRound sticky clear
        // 购买档：在 RuleCoach 定 intent 之后立刻决定，供 BotBuyLoadout / digest 使用
        st.buyPlan = DecideBuyPlan(t);
        if (plan.intent == PlanIntent::Save) {
            st.buyPlan = BuyPlan::Eco;
        }
        // Eco + ShouldSaveNow：由 RuleCoach 决定是否 Save；此处不覆盖其 intent（不打架）
        coachPlanChangedAt_ = clockSec_;

        std::cout << "[Coach] " << TeamName(t) << " 回合计划 -> "
                  << IntentName(plan.intent)
                  << " 购买=" << BuyPlanLabel(st.buyPlan)
                  << " | " << plan.reason << std::endl;
        CoachApplyPlan(t);
    }
}


// 队伍平均余额（买枪前快照；没有快照时退回当前值）
int CS2DGame::GetRoundStartEconomy(Team t) const {
    if (roundEconomySnapshotValid_ && mode_ != MatchMode::DeathMatch) {
        return (t == Team::T) ? roundStartAvgMoneyT_ : roundStartAvgMoneyCT_;
    }
    int sum = 0, n = 0;
    if (player_.team == t) { sum += player_.money; ++n; }
    for (const auto& b : bots_) {
        if (b.team == t) { sum += b.money; ++n; }
    }
    return (n > 0) ? sum / n : 0;
}

void CS2DGame::SnapshotRoundEconomy() {
    auto avg = [&](Team t) {
        int sum = 0, n = 0;
        if (player_.team == t) { sum += player_.money; ++n; }
        for (const auto& b : bots_) {
            if (b.team == t) { sum += b.money; ++n; }
        }
        return (n > 0) ? sum / n : 0;
    };
    roundStartAvgMoneyT_  = avg(Team::T);
    roundStartAvgMoneyCT_ = avg(Team::CT);
    roundEconomySnapshotValid_ = true;
}

// 队伍平均余额（含玩家）
bool CS2DGame::LowEconomyAverage(Team t) const {
    // 手枪局有自己的节奏，不按 eco 处理；阈值 1800 更接近 CS 的经济线
    if (round_.RoundNumber() <= 1) return false;
    if (mode_ == MatchMode::DeathMatch) return false;
    return GetRoundStartEconomy(t) < 1800;
}

// 购买档：按买枪前经济、是否该保枪、敌方经济决定。
BuyPlan CS2DGame::DecideBuyPlan(Team t) const {
    if (mode_ != MatchMode::BombDefusal) return BuyPlan::Full;

    const int avg    = GetRoundStartEconomy(t);
    const int oppAvg = GetRoundStartEconomy(TeamManager::Opponent(t));

    // 手枪局：能买甲/雷就花，别按“穷局”直接空手。
    if (round_.RoundNumber() <= 1) return BuyPlan::Force;

    // 真正该保枪时优先 Eco；否则再按余额分 Half/Force/Full。
    if (ShouldSaveNow(t)) return BuyPlan::Eco;

    if (avg >= 3500) {
        // 我方满钱、敌方穷：SMG + 甲的反 eco 打法。
        if (oppAvg > 0 && oppAvg < 2200) return BuyPlan::AntiEco;
        return BuyPlan::Full;
    }
    if (avg >= 2600) return BuyPlan::Half;
    if (avg >= 1800) return BuyPlan::Force;
    return BuyPlan::Eco;
}

BuyPlan CS2DGame::GetBuyPlan(Team t) const {
    return CoachFor(t).buyPlan;
}

const char* CS2DGame::BuyPlanLabel(BuyPlan p) {
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
bool CS2DGame::ShouldSaveNow(Team t) const {
    if (bombPlanted_) return false;
    // 手枪局必须买：800 经济下"保枪"等于白送一回合，还会把买枪档位倒退成 Eco。
    if (round_.RoundNumber() <= 1) return false;
    if (GetRoundElapsed() > 45.0f) return false;
    if (AliveCount(t) > AliveCount(TeamManager::Opponent(t))) return false;

    // T 已经站上包点 = 已经拿到主动权，再保枪等于把包点白送
    if (t == Team::T && world_) {
        for (const auto& b : bots_) {
            if (!b.alive || b.team != Team::T) continue;
            if (world_->FindBombSiteAt(b.x, b.y)) return false;
        }
    }
    return LowEconomyAverage(t);
}

// ============================================================================
// 转点（信息驱动）
//
//   判定顺序：时间 -> 人数 -> 冷却 -> 证据
//   证据 = 两个包点的威胁度差；未知的包点不会被当成「空」
// ============================================================================

bool CS2DGame::CanRotateNow(Team t) const {
    if (mode_ != MatchMode::BombDefusal) return false;
    if (bombPlanted_) return false;                       // 下包后不转
    if (bombDropped_) return false;                       // C4 掉了先回收
    if (t != Team::T) return false;                       // 目前只做进攻方转点
    if (!round_.IsLive()) return false;
    if (round_.PhaseRemaining() < 30.0f) return false;    // 留足执行 + 下包时间
    if (AliveCount(t) + 1 < AliveCount(TeamManager::Opponent(t))) return false;  // 少两个不转
    if (clockSec_ - lastRotateAt_ < 8.0f) return false;   // 8 秒冷却，防来回抖
    if (intel_.ObservationCount() < 2) return false;      // 至少两条情报

    // 只有「默认分兵」阶段才允许转点；rush/split/stack/fake/save 都是已承诺的
    // 执行，不能被情报轻易拉回另一侧，否则会在两点之间来回走。
    const CoachState& st = CoachFor(t);
    if (!st.plan.valid) return false;
    if (st.plan.intent != PlanIntent::Default) return false;
    if (clockSec_ - st.plan.createdAt < 12.0f) return false;   // 新计划先执行一段

    // 已经有人进包点 / C4 携带者接近包点：优先下包，不转点。
    if (world_) {
        for (size_t i = 0; i < bots_.size(); ++i) {
            const auto& b = bots_[i];
            if (!b.alive || b.team != Team::T) continue;
            if (world_->FindBombSiteAt(b.x, b.y)) return false;
            if (static_cast<int>(i) == bombCarrier_) {
                for (const auto& s : world_->GetBombSites()) {
                    const float dx = s.cx - b.x, dy = s.cy - b.y;
                    if (dx * dx + dy * dy < 140.0f * 140.0f) return false;
                }
            }
        }
    }
    return true;
}

void CS2DGame::CoachTryRotate(Team t) {
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
    constexpr float kDelta = 1.2f;
    if (heatCur - heatOth < kDelta) return;

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


