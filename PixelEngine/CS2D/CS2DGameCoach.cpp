#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>

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
    std::string want = key;
    std::transform(want.begin(), want.end(), want.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);

        if (st.mergeWindow > 0.0f) {
            st.mergeWindow -= dt;
            if (st.mergeWindow > 0.0f) continue;
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

    ++st.gen;
    TeamPlan plan = RuleCoach(t, ev);
    plan.gen       = st.gen;
    plan.createdAt = clockSec_;
    plan.valid     = true;

    st.plan   = plan;
    st.reason = plan.reason;
    coachPlanChangedAt_ = clockSec_;

    std::cout << "[Coach] " << TeamName(t) << " 第" << st.callsThisRound << "次"
              << " 事件#" << static_cast<int>(ev.type)
              << " -> " << IntentName(plan.intent)
              << " 目标=" << (plan.target.empty() ? "-" : plan.target)
              << " | " << plan.reason << std::endl;

    CoachApplyPlan(t);

    // 规则计划先落地（保证零延迟），再异步向 LLM 要一份更好的
    CoachTryLlm(t, ev);
}

// ---------------------------------------------------------------------------
// 规则教练
// ---------------------------------------------------------------------------
TeamPlan CS2DGame::RuleCoach(Team t, const CoachEvent& ev) {
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
    };

    const std::string aSite  = AreaHint("A_Site");
    const std::string aLong  = AreaHint("A_Long");
    const std::string aShort = AreaHint("A_Short");
    const std::string bSite  = AreaHint("B_Site");
    const std::string bLong  = AreaHint("B_Long");
    const std::string mid    = AreaHint("Mid");

    // 当前包点区域（未下包就用 A 点占位，避免空字符串）
    const std::string bombArea =
        bombPlanted_ && world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
    const std::string siteArea = bombArea.empty() ? aSite : bombArea;

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
    if (ev.type == CoachEventType::DefuseStarted && isT) {
        plan.intent = PlanIntent::StackA;
        plan.tempo  = Tempo::Fast;
        plan.reason = "有人在拆包，回防包点";
        for (auto& sl : plan.slots) sl = { CoachRole::Entry, siteArea, true, false };
        return plan;
    }
    if (ev.type == CoachEventType::EnemyDown) {
        plan.tempo  = Tempo::Fast;
        plan.reason = "对面减员，前压抓人";
        assign(aLong, aSite, bLong, bSite, mid);
        return plan;
    }
    if (ev.type == CoachEventType::AllyDown) {
        plan.tempo  = Tempo::Slow;
        plan.reason = "我方减员，收缩等机会";
        assign(aSite, aLong, bSite, bLong, mid);
        return plan;
    }
    if (ev.type == CoachEventType::TimeLow) {
        plan.tempo  = Tempo::Fast;
        plan.intent = isT ? PlanIntent::RushA : PlanIntent::Default;
        plan.reason = "时间不足，抓紧进攻/占点";
        assign(aLong, aSite, bLong, bSite, mid);
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

    if (isT) {
        if (avoidA && !avoidB) {
            plan.intent = PlanIntent::RushB;
            plan.target = "B";
            plan.tempo  = Tempo::Fast;
            plan.reason = "A 点连续失手，改速推 B";
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, bLong, true, false };
            plan.slots[4] = { CoachRole::Anchor, bSite, true, false };
        } else if (avoidB && !avoidA) {
            plan.intent = PlanIntent::RushA;
            plan.target = "A";
            plan.tempo  = Tempo::Fast;
            plan.reason = "B 点连续失手，改速推 A";
            for (auto& sl : plan.slots) sl = { CoachRole::Entry, aLong, true, false };
            plan.slots[4] = { CoachRole::Anchor, aSite, true, false };
        } else {
            plan.intent = PlanIntent::Default;
            plan.reason = "默认开局，A/B 分兵找机会";
            assign(aLong, aSite, bLong, bSite, mid);
        }
    } else {
        plan.intent = PlanIntent::Default;
        plan.reason = "常规守点，A/B 各两人压中路";
        plan.slots[0] = { CoachRole::Anchor,  aSite,  true, false };
        plan.slots[1] = { CoachRole::Support, aShort, true, false };
        plan.slots[2] = { CoachRole::Entry,   mid,    true, true  };
        plan.slots[3] = { CoachRole::Anchor,  bSite,  true, false };
        plan.slots[4] = { CoachRole::Support, bLong,  true, false };
    }
    return plan;
}

// ---------------------------------------------------------------------------
// 计划下发
// ---------------------------------------------------------------------------
void CS2DGame::CoachApplyPlan(Team t) {
    CoachState& st = CoachFor(t);
    if (!st.plan.valid) return;

    int slot = 0;
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
        if (AreaCenterByName(so.area, cx, cy)) {
            ai.hasPlanHold = true;
            ai.planHoldX   = cx;
            ai.planHoldY   = cy;
            ai.goalArea    = so.area;
        }
        ++slot;
    }
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
    }
    for (size_t i = 0; i < bots_.size(); ++i) AiAt(i).hasPlanHold = false;
}
