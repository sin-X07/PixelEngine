#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include "../CS2D/MiniJSON.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

// ============================================================================
// 教练层的 LLM 接入
//
// 流程：CoachThink 先用 RuleCoach 落地一份计划（零延迟），
//       再异步向 LLM 要一份更好的；回包后校验，通过就替换并重新下发。
// 约束：回包在 worker 线程，只写进加锁队列，主线程 CoachDrainLlm 消费。
// ============================================================================

namespace {

const char* kCoachSystemPrompt =
    "你是 CS2D 战队的主教练（IGL）。你只负责战术选择，不负责瞄准和走位。\n"
    "根据比分、人数、包点状态和过去几回合的比赛记忆，为全队制定本回合计划。\n"
    "严格只输出一个 JSON 对象，不要解释、不要 Markdown、不要代码块。\n"
    "字段要求：\n"
    "  intent: default|rush_a|rush_b|split_a|split_b|stack_a|stack_b|retake_a|retake_b|save|fake\n"
    "  target: A|B|MID 或空字符串\n"
    "  tempo: slow|normal|fast\n"
    "  slots: 恰好 5 个元素，slot 从 0 到 4\n"
    "     每个元素 {role, area}；role 取 entry|support|lurk|anchor|awper；\n"
    "     area 必须是【区域】里出现过的名字，不知道就填空字符串\n"
    "  reason: 一句中文，不超过 20 个字，说明你为什么这么打\n"
    "\n"
    "intent 语义约束（必须遵守）：\n"
    "  retake_a / retake_b 只能用于 C4 已被敌方安装、我方需要夺回包点的情况\n"
    "  stack_a / stack_b 用于全员集中到一个点（进攻或守包）\n"
    "  save 只能用于经济崩溃、主动放弃本回合\n"
    "  没下包、正常开局时，只能用 default / rush_* / split_* / fake\n";

const char* IntentToStr(PlanIntent i) {
    switch (i) {
        case PlanIntent::RushA:   return "rush_a";
        case PlanIntent::RushB:   return "rush_b";
        case PlanIntent::SplitA:  return "split_a";
        case PlanIntent::SplitB:  return "split_b";
        case PlanIntent::StackA:  return "stack_a";
        case PlanIntent::StackB:  return "stack_b";
        case PlanIntent::RetakeA: return "retake_a";
        case PlanIntent::RetakeB: return "retake_b";
        case PlanIntent::Save:    return "save";
        case PlanIntent::FakeThenOther: return "fake";
        default:                  return "default";
    }
}

bool ParseIntent(const std::string& v, PlanIntent& out) {
    if (v == "default")  { out = PlanIntent::Default; return true; }
    if (v == "rush_a")   { out = PlanIntent::RushA;   return true; }
    if (v == "rush_b")   { out = PlanIntent::RushB;   return true; }
    if (v == "split_a")  { out = PlanIntent::SplitA;  return true; }
    if (v == "split_b")  { out = PlanIntent::SplitB;  return true; }
    if (v == "stack_a")  { out = PlanIntent::StackA;  return true; }
    if (v == "stack_b")  { out = PlanIntent::StackB;  return true; }
    if (v == "retake_a") { out = PlanIntent::RetakeA; return true; }
    if (v == "retake_b") { out = PlanIntent::RetakeB; return true; }
    if (v == "save")     { out = PlanIntent::Save;    return true; }
    if (v == "fake")     { out = PlanIntent::FakeThenOther; return true; }
    return false;
}

bool ParseTempo(const std::string& v, Tempo& out) {
    if (v == "slow")   { out = Tempo::Slow;   return true; }
    if (v == "normal") { out = Tempo::Normal; return true; }
    if (v == "fast")   { out = Tempo::Fast;   return true; }
    return false;
}

bool ParseRole(const std::string& v, CoachRole& out) {
    if (v == "entry")   { out = CoachRole::Entry;   return true; }
    if (v == "support") { out = CoachRole::Support; return true; }
    if (v == "lurk")    { out = CoachRole::Lurk;    return true; }
    if (v == "anchor")  { out = CoachRole::Anchor;  return true; }
    if (v == "awper")   { out = CoachRole::Awper;   return true; }
    return false;
}

std::string LowerCopy(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// 组 prompt
// ---------------------------------------------------------------------------
static std::string BuildCoachUserPrompt(const CS2DGame& game, Team t,
                                        const CoachEvent& ev,
                                        const std::deque<RoundMemo>& memory,
                                        const CS2DWorld* world) {
    std::ostringstream o;

    const Team enemy = TeamManager::Opponent(t);
    o << "【队伍】" << (t == Team::T ? "T（进攻方）" : "CT（防守方）")
      << "  比分 T:" << game.GetScoreT() << " CT:" << game.GetScoreCT()
      << "  第" << game.GetRoundNumber() << "回合\n";

    o << "【人数】我方存活 " << game.AliveCount(t)
      << "  敌方存活 " << game.AliveCount(enemy) << "\n";

    o << "【时间】回合已进行 " << static_cast<int>(game.GetRoundElapsed()) << " 秒\n";

    o << "【包点】";
    if (game.IsBombPlanted()) {
        o << "C4 已安装，剩余 " << static_cast<int>(game.GetBombTimer()) << " 秒，位置在 "
          << (world ? world->FindAreaAt(game.GetBombX(), game.GetBombY()) : "") << "\n";
    } else {
        o << "尚未下包\n";
    }

    o << "【区域】";
    if (world) {
        const auto& areas = world->GetAreas();
        for (size_t i = 0; i < areas.size(); ++i) {
            o << areas[i].name << (i + 1 < areas.size() ? " " : "");
        }
    }
    o << "\n";

    // 比赛记忆（最近 10 回合）
    o << "【记忆】\n";
    if (memory.empty()) {
        o << "（这是第一回合，没有历史）\n";
    } else {
        for (const auto& m : memory) {
            o << "R" << m.roundNo << " " << IntentToStr(m.plan);
            if (!m.target.empty()) o << "->" << m.target;
            o << (m.won ? " 胜 " : " 负 ");
            o << m.scoreT << ":" << m.scoreCT;
            if (m.bombPlanted) o << " 已下包";
            if (!m.firstContactArea.empty()) o << " 首次接触@" << m.firstContactArea;
            o << " 我方存活" << m.aliveEnd;
            // 双方阵亡点（最多列 4 条，控制 token）
            int listed = 0;
            for (const auto& d : m.deaths) {
                if (listed++ >= 4) break;
                o << " | " << (d.team == t ? "我方" : "敌方") << "亡@"
                  << (d.area.empty() ? "未知" : d.area);
            }
            o << "\n";
        }
    }

    o << "【当前事件】";
    switch (ev.type) {
        case CoachEventType::RoundStart:    o << "回合刚开始，制定整回合计划"; break;
        case CoachEventType::EnemySpotted:  o << "发现敌人，首次接触@" << ev.area; break;
        case CoachEventType::AllyDown:      o << "我方减员@" << ev.area; break;
        case CoachEventType::EnemyDown:     o << "敌方减员@" << ev.area; break;
        case CoachEventType::BombPlanted:   o << "C4 已安装@" << ev.area; break;
        case CoachEventType::DefuseStarted: o << "有人在拆包@" << ev.area; break;
        case CoachEventType::DefuseAborted: o << "拆包被中断"; break;
        case CoachEventType::TimeLow:       o << "时间不足"; break;
        case CoachEventType::EconomyLow:    o << "经济不足"; break;
        case CoachEventType::HumanDied:     o << "玩家阵亡"; break;
        case CoachEventType::MatchPoint:    o << "赛点"; break;
        default: break;
    }
    o << "\n【输出】只输出 JSON。";
    return o.str();
}

// ---------------------------------------------------------------------------
// 解析并校验 LLM 计划
// ---------------------------------------------------------------------------
static bool ParseCoachPlan(const std::string& raw, TeamPlan& out, const CS2DWorld* world,
                           bool game_bombPlanted, bool lowEconomy) {
    const auto bo = raw.find('{');
    const auto bc = raw.rfind('}');
    if (bo == std::string::npos || bc == std::string::npos || bc <= bo) return false;

    MiniJSON::Value root;
    try {
        root = MiniJSON::parse(raw.substr(bo, bc - bo + 1));
    } catch (...) {
        return false;
    }
    if (!root.is_object()) return false;

    TeamPlan plan;
    bool ok = false;

    const std::string intent = LowerCopy(root.value<std::string>("intent", ""));
    if (ParseIntent(intent, plan.intent)) ok = true;

    plan.target = root.value<std::string>("target", "");
    const std::string tempo = LowerCopy(root.value<std::string>("tempo", "normal"));
    ParseTempo(tempo, plan.tempo);
    plan.reason = root.value<std::string>("reason", "");

    // slots
    if (root.contains("slots") && root["slots"].is_array()) {
        const auto& arr = root["slots"];
        const size_t n = std::min<size_t>(arr.size(), 5);
        for (size_t i = 0; i < n; ++i) {
            const auto& sl = arr[i];
            CoachRole role = CoachRole::Support;
            ParseRole(LowerCopy(sl.value<std::string>("role", "")), role);
            std::string area = sl.value<std::string>("area", "");

            // 区域必须在世界区域表里存在，否则丢弃（避免模型编造）
            bool areaOk = area.empty();
            if (!areaOk && world) {
                for (const auto& a : world->GetAreas()) {
                    if (LowerCopy(a.name) == LowerCopy(area)) { areaOk = true; break; }
                }
            }
            if (!areaOk) area.clear();

            plan.slots[i].role = role;
            plan.slots[i].area = area;
            plan.slots[i].canTrade = true;
            plan.slots[i].canRotate = (role == CoachRole::Lurk);
        }
    }

    if (!ok) return false;

    // 语义校验：纠正模型最常见的两个误用
    const bool bombDown = game_bombPlanted;
    if (!bombDown &&
        (plan.intent == PlanIntent::RetakeA || plan.intent == PlanIntent::RetakeB)) {
        plan.intent = PlanIntent::Default;          // 没下包不可能是回防
    }
    if (plan.intent == PlanIntent::Save && !lowEconomy) {
        plan.intent = PlanIntent::Default;          // 有钱就别保枪
    }

    out = plan;
    return true;
}

// ---------------------------------------------------------------------------
// 发起 LLM 请求
// ---------------------------------------------------------------------------
void CS2DGame::CoachTryLlm(Team t, const CoachEvent& ev) {
    if (!coachLlmEnabled_ || !coachLlm_) return;
    if (!world_) return;

    CoachState& st = CoachFor(t);
    if (st.llmPending) return;      // 一次只允许一个在途请求

    const std::string user = BuildCoachUserPrompt(*this, t, ev, st.memory, world_.get());

    st.llmPending   = true;
    st.llmGen       = st.gen;
    st.llmAt        = clockSec_;
    st.llmEventType = static_cast<int>(ev.type);
    const int gen = st.gen;

    // 回调在 worker 线程：只写加锁队列，绝不碰游戏状态
    coachLlm_(kCoachSystemPrompt, user, 200,
              [this, t, gen](const std::string& raw) {
                  std::lock_guard<std::mutex> lock(coachLlmMutex_);
                  coachLlmResults_.push_back(CoachLlmResult{t, gen, raw});
              });
}

// ---------------------------------------------------------------------------
// 主线程：消费回包
// ---------------------------------------------------------------------------
void CS2DGame::CoachDrainLlm() {
    std::vector<CoachLlmResult> got;
    {
        std::lock_guard<std::mutex> lock(coachLlmMutex_);
        got.swap(coachLlmResults_);
    }

    for (const auto& r : got) {
        CoachState& st = CoachFor(r.team);
        st.llmPending = false;

        // 代际号过期 → 丢弃（防止旧计划覆盖新计划）
        if (r.gen < st.gen) {
            std::cout << "[Coach] " << TeamName(r.team)
                      << " LLM 回包过期(" << r.gen << "<" << st.gen << ")，丢弃" << std::endl;
            continue;
        }
        if (r.raw.empty()) {
            std::cout << "[Coach] " << TeamName(r.team) << " LLM 无回复，沿用规则计划" << std::endl;
            continue;
        }

        TeamPlan plan;
        if (!ParseCoachPlan(r.raw, plan, world_.get(), bombPlanted_, false)) {
            std::cout << "[Coach] " << TeamName(r.team) << " LLM 计划非法，沿用规则计划 | "
                      << r.raw.substr(0, 120) << std::endl;
            continue;
        }

        plan.gen       = st.gen;
        plan.createdAt = clockSec_;
        plan.valid     = true;
        st.plan   = plan;
        st.reason = plan.reason;

        std::cout << "[Coach] " << TeamName(r.team) << " LLM("
                  << (clockSec_ - st.llmAt) << "s) -> "
                  << IntentToStr(plan.intent)
                  << " 目标=" << (plan.target.empty() ? "-" : plan.target)
                  << " | " << plan.reason << std::endl;

        CoachApplyPlan(r.team);
    }

    // 超时兜底：3.5 秒没回包就认为失败，规则计划继续生效
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        const float deadline = (st.llmEventType == static_cast<int>(CoachEventType::RoundStart))
                                   ? 7.0f : 4.0f;
        if (st.llmPending && clockSec_ - st.llmAt > deadline) {
            st.llmPending = false;
            std::cout << "[Coach] " << TeamName(t) << " LLM 超时，沿用规则计划" << std::endl;
        }
    }
}
