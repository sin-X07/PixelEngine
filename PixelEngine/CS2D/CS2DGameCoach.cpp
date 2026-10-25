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

    // 计划过期：20 秒没有新事件就重新评估，避免守着过时计划
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        if (st.plan.valid && st.planExpiresAt > 0.0f && clockSec_ > st.planExpiresAt) {
            st.planExpiresAt = clockSec_ + 20.0f;
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
            st.reason = p.reason + "（计划过期，重新评估）";
            st.stickyLlm = false; // plan expiry sticky clear
            coachPlanChangedAt_ = clockSec_;
            std::cout << "[Coach] " << TeamName(t) << " 计划过期 -> "
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

    // dust_2 site layer real names: Long/Short/Cat/Tunnel (no A_Long/A_Short/B_Long)
    const std::string aSite  = AreaHint("A_Site");
    const std::string aLong  = AreaHint("Long");
    const std::string aShort = AreaHint("Short");
    const std::string bSite  = AreaHint("B_Site");
    const std::string bLong  = AreaHint("Tunnel");
    const std::string mid    = AreaHint("Mid");

    // 当前包点区域（未下包就用 A 点占位，避免空字符串）
    const std::string bombArea =
        bombPlanted_ && world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
    const std::string siteArea = bombArea.empty() ? aSite : bombArea;

    // ================= 下包后的战术姿态（任何事件都按当前局势重算）=================
    // 参考公开攻略的通用原则：
    //   T ：按用途分散守包（拆包角度 / 回防路线 / 延迟 peek），交叉火力；
    //       残局少打多不硬拼，守包等 CT 上手拆包再抓；时间是 T 的盟友。
    //   CT：先集结再进，utility 先清点，两人互换；拆包需队友掩护；
    //       时间不够（无钳 10s）就强拆。
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
            ai.hasPlanHold = true;
            ai.planHoldX   = cx;
            ai.planHoldY   = cy;
            ai.goalArea    = std::string();
            ++holdOk;
            detail << " #" << slot << "=(save)";
            squad_[i].canTrade  = false;
            squad_[i].canRotate = false;
            ++slot;
            continue;
        }
        if (!so.area.empty() && AreaCenterByName(so.area, cx, cy)) {
            ai.hasPlanHold = true;
            ai.planHoldX   = cx;
            ai.planHoldY   = cy;
            ai.goalArea    = so.area;
            ++holdOk;
            detail << " #" << slot << "=" << so.area;
        } else {
            ai.hasPlanHold = false;
            ++holdFail;
            detail << " #" << slot << "=" << (so.area.empty() ? "(empty)" : so.area + "!");
        }
        squad_[i].canTrade  = so.canTrade;
        squad_[i].canRotate = so.canRotate;
        ++slot;
    }
    std::cout << "[Coach] Apply " << TeamName(t)
              << " holdOk=" << holdOk << " holdFail=" << holdFail
              << detail.str() << std::endl;
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
    }
    for (size_t i = 0; i < bots_.size(); ++i) AiAt(i).hasPlanHold = false;
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
        coachPlanChangedAt_ = clockSec_;

        std::cout << "[Coach] " << TeamName(t) << " 回合计划 -> "
                  << IntentName(plan.intent)
                  << " | " << plan.reason << std::endl;
        CoachApplyPlan(t);
    }
}


// 队伍平均余额（含玩家）
bool CS2DGame::LowEconomyAverage(Team t) const {
    int sum = 0, n = 0;
    if (player_.team == t) { sum += player_.money; ++n; }
    for (const auto& b : bots_) {
        if (b.team == t) { sum += b.money; ++n; }
    }
    // 手枪局有自己的节奏，不按 eco 处理；阈值 1800 更接近 CS 的经济线
    if (round_.RoundNumber() <= 1) return false;
    return (n > 0 && sum / n < 1800);
}

// 保枪只在「真的该保」时成立：
//   1) 还没下包（下了包就必须守包/回防）
//   2) 回合前 20 秒（中后期再保枪只会送掉回合）
//   3) 不是人数优势（多打少还保枪 = 主动让出主动权）
//   4) 经济确实崩（已有阈值）
bool CS2DGame::ShouldSaveNow(Team t) const {
    if (bombPlanted_) return false;
    if (GetRoundElapsed() > 45.0f) return false;
    if (AliveCount(t) > AliveCount(TeamManager::Opponent(t))) return false;

    // T 已经站上包点 = 已经拿到主动权，再保枪等于把包点白送
    if (t == Team::T && world_) {
        for (const auto& b : bots_) {
            if (!b.alive || b.team != Team::T) continue;
            for (const auto& site : world_->GetBombSites()) {
                if (b.x >= site.x && b.x <= site.x + site.width &&
                    b.y >= site.y && b.y <= site.y + site.height) {
                    return false;
                }
            }
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
    if (t != Team::T) return false;                       // 目前只做进攻方转点
    if (!round_.IsLive()) return false;
    if (round_.PhaseRemaining() < 30.0f) return false;    // 留足执行 + 下包时间
    if (AliveCount(t) + 1 < AliveCount(TeamManager::Opponent(t))) return false;  // 少两个不转
    if (clockSec_ - lastRotateAt_ < 8.0f) return false;   // 8 秒冷却，防来回抖
    if (intel_.ObservationCount() < 2) return false;      // 至少两条情报
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


