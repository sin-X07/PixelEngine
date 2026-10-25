#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include "../CS2D/MiniJSON.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include "../Core/PathUtils.hpp"

// ============================================================================
// 教练层的 LLM 接入（结构化输出 + 短摘要 + TTL + 复盘笔记）
//
//   - system prompt 固定（角色 / 词表 / 区域表 / 输出契约），利于前缀缓存
//   - user 只放「会变的短摘要」，控制在几十 token
//   - response_format:json_schema 强制结构化输出，游戏侧再校验一次
//   - 每条计划带 ttl_s；回包时回合号或炸弹状态变了就丢弃
//   - 回合结束额外请求一次「一句话复盘」，滚动保留最近 5 条笔记
// ============================================================================

namespace {

// llama-server 结构化输出 schema（与游戏侧枚举一一对应）
const char* kCoachSchema = R"JSON({"type":"object","additionalProperties":false,
"required":["intent","target","tempo","ttl_s","reason"],
"properties":{
 "intent":{"enum":["default","rush_a","rush_b","split_a","split_b","stack_a","stack_b","retake_a","retake_b","save","fake"]},
 "target":{"enum":["A","B","MID",""]},
 "tempo":{"enum":["slow","normal","fast"]},
 "ttl_s":{"type":"integer","minimum":3,"maximum":30},
 "reason":{"type":"string","maxLength":60},
 "slots":{"type":"array","maxItems":5,"items":{"type":"object","additionalProperties":false,
   "required":["slot","role"],
   "properties":{"slot":{"type":"integer","minimum":0,"maximum":4},
                 "role":{"enum":["entry","support","lurk","anchor","awper"]},
                 "area":{"type":"string"}}}}}})JSON";

const char* IntentLabel(PlanIntent i) {
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

std::string Lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

struct CoachAllowSet {
    std::vector<std::string> intents;   // 小写标签，与 IntentLabel 一致
    std::vector<std::string> targets;   // "A","B","MID" 的子集（可含空串）
    std::vector<std::string> areas;     // 地图区域名
};

bool ContainsCI(const std::vector<std::string>& v, const std::string& s) {
    const std::string t = Lower(s);
    for (const auto& x : v) {
        if (Lower(x) == t) return true;
    }
    return false;
}

// 区域名 → "A" / "B" / ""（与规则教练同样的大小写/子串判断）
std::string SiteSideFromArea(const std::string& area) {
    if (area.empty()) return std::string();
    std::string lower = Lower(area);
    if (lower.find("b_") != std::string::npos ||
        lower.find("b site") != std::string::npos) {
        return "B";
    }
    return "A";
}

// 根据阵营 / 炸弹 / 经济，算出「当前允许的战术闭集」
CoachAllowSet BuildAllowSet(Team team, bool bombPlanted, bool lowEconomy,
                            const CS2DWorld* world) {
    CoachAllowSet a;

    if (team == Team::CT) {
        a.intents = { "default", "stack_a", "stack_b" };
        if (bombPlanted) {
            a.intents.push_back("retake_a");
            a.intents.push_back("retake_b");
        } else if (lowEconomy) {
            a.intents.push_back("save");
        }
    } else {   // T
        if (bombPlanted) {
            // 已下包：叠点守包（交叉火力）或默认守包
            a.intents = { "default", "stack_a", "stack_b" };
        } else {
            a.intents = { "default", "rush_a", "rush_b",
                          "split_a", "split_b", "fake" };
            if (lowEconomy) a.intents.push_back("save");
        }
    }

    a.targets = { "A", "B", "MID", "" };

    if (world) {
        for (const auto& ar : world->GetAreas()) a.areas.push_back(ar.name);
    }
    return a;
}

// 固定前缀：角色 + 词表 + 区域表 + 输出契约
// 按允许集合动态生成 json_schema 的 enum
std::string BuildCoachSchema(const CoachAllowSet& allow) {
    std::ostringstream o;
    o << "{\"type\":\"object\",\"additionalProperties\":false,"
         "\"required\":[\"intent\",\"target\",\"tempo\",\"ttl_s\",\"reason\"],"
         "\"properties\":{"
         "\"intent\":{\"enum\":[";
    for (size_t i = 0; i < allow.intents.size(); ++i) {
        if (i) o << ',';
        o << '"' << allow.intents[i] << '"';
    }
    o << "]},\"target\":{\"enum\":[";
    for (size_t i = 0; i < allow.targets.size(); ++i) {
        if (i) o << ',';
        o << '"' << allow.targets[i] << '"';
    }
    o << "]},"
         "\"tempo\":{\"enum\":[\"slow\",\"normal\",\"fast\"]},"
         "\"ttl_s\":{\"type\":\"integer\",\"minimum\":3,\"maximum\":30},"
         "\"reason\":{\"type\":\"string\",\"maxLength\":60},"
         "\"slots\":{\"type\":\"array\",\"maxItems\":5,"
         "\"items\":{\"type\":\"object\",\"additionalProperties\":false,"
         "\"required\":[\"slot\",\"role\"],"
         "\"properties\":{"
         "\"slot\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":4},"
         "\"role\":{\"enum\":[\"entry\",\"support\",\"lurk\",\"anchor\",\"awper\"]},"
         "\"area\":{\"type\":\"string\"}}}}}}";
    return o.str();
}


// 战术库：启动后读一次，追加进固定 system 前缀（利于 cache_prompt）
std::string LoadCoachPlaybook() {
    static std::string cached;
    static bool tried = false;
    if (tried) return cached;
    tried = true;

    const std::string path =
        PathUtils::ResolveAppPath("assets/config/coach_playbook_dust2.txt");
    std::ifstream in(path);
    if (!in) {
        std::cout << "[Coach] 未找到战术库: " << path << std::endl;
        return cached;
    }
    // 上限 6000 字节（约 2000 汉字），超出部分按行截断，避免撑爆上下文
    constexpr size_t kMaxBookBytes = 12000;

    std::ostringstream buf;
    buf << in.rdbuf();
    cached = buf.str();
    std::string cleaned;
    cleaned.reserve(cached.size());
    std::istringstream lines(cached);
    std::string line;
    while (std::getline(lines, line)) {
        std::string t = line;
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
        if (!t.empty() && t.front() == '#') continue;
        cleaned.append(line);
        cleaned.push_back('\n');
    }
    // 超限按行截断（保留完整的行，避免把一句话切成两半）
    if (cleaned.size() > kMaxBookBytes) {
        size_t cut = cleaned.rfind('\n', kMaxBookBytes);
        if (cut == std::string::npos) cut = kMaxBookBytes;
        cleaned.resize(cut);
        std::cout << "[Coach] 战术库超过 " << kMaxBookBytes
                  << " 字节，已按行截断" << std::endl;
    }

    cached.swap(cleaned);
    std::cout << "[Coach] 已加载战术库 (" << cached.size() << " bytes): " << path
              << std::endl;
    return cached;
}

std::string BuildSystemPrompt(const CS2DWorld* world) {
    std::ostringstream o;
    o << "你是 CS2D 战队主教练。只做战术决策，不做瞄准与走位。\n"
      << "严格只输出一个 JSON 对象：不要 YAML、不要解释、不要代码块。\n"
      << "示例：{\"intent\":\"rush_b\",\"target\":\"B\",\"tempo\":\"fast\","
         "\"ttl_s\":12,\"reason\":\"敌方减员速推B\","
         "\"slots\":[{\"slot\":0,\"role\":\"entry\",\"area\":\"Tunnel\"}]}\n"
      << "字段含义：\n"
      << " intent: 只能从 user 行「允许:intent=...」中选，不得使用其它值\n"
      << "   retake_* 仅在 C4 已被安装时使用；save 仅在经济崩溃时使用\n"
      << "   rush_*/split_*/fake 是 T 战术；CT 用 default/stack_*/retake_*\n"
      << " target: A / B / MID / 空\n"
      << " tempo: slow/normal/fast\n"
      << " ttl_s: 指令有效秒数（3~30），场面易变给小值\n"
      << " slots: 最多 5 个，slot 0~4，area 必须是下列区域之一\n"
      << " reason: 不超过 20 个汉字，只用于日志\n"
      << "C4 规则：若 C4 掉落在地上，T 方应派最近的队友去拾取后再进攻；"
         "CT 方若已知掉落位置，应收缩到该区域卡点。\n"
      << "优先级：当前炸弹状态与经济高于笔记。已下包时必须按包点选择 "
         "retake_*（CT）或 stack_*（T），禁止因笔记改打另一侧。\n"
      << "save 的使用条件很窄：仅「回合开始 20 秒内 + 经济崩 + 不占人数优势」。"
         "已占领包点、已下包、或人多打人少时一律不许 save；"
         "保枪也不代表跑回出生点，而是就地收缩不交火。\n"
      << "ttl_s 建议：开局 18~25；已下包 12~20；不要给低于 10 的开局 ttl。\n"
      << "下包后战术（T）：\n"
      << "  - 不要全员贴包：按用途分散——1 人看拆包角度、1 人控回防路线、"
         "1 人延迟 peek，形成交叉火力\n"
      << "  - 已下包用 stack_<包点侧>（叠点守包）或 default；不要 rush/split\n"
      << "  - 残局少打多不要硬拼：守包、等 CT 上手拆包再抓；时间站在你这边\n"
      << "回防战术（CT）：\n"
      << "  - 先集结再进，别一个个送；两人同角度互换，utility 先清点\n"
      << "  - 拆包必须有队友掩护；时间不够（无钳 10s，有钳 5s）就强拆\n"
      << "  - 已下包用 retake_<包点侧>，target 必须是同一侧；2v4 时可打人数差/武器\n";
    if (world) {
        o << "可选区域:";
        for (const auto& a : world->GetAreas()) o << ' ' << a.name;
        o << "\n";
    }
    const std::string& book = LoadCoachPlaybook();
    if (!book.empty()) {
        o << "\n" << book;
    }
    return o.str();
}

// 会变化的短摘要
std::string BuildStateDigest(const CS2DGame& game, Team team,
                             const CoachEvent& ev,
                             const std::deque<std::string>& notes,
                             const std::string& lastOrder,
                             const CS2DWorld* world,
                             const std::deque<RoundMemo>& memory,
                             const CoachAllowSet& allow) {
    std::ostringstream o;

    o << "优先级:炸弹>经济>减员>笔记\n";

    const int total = static_cast<int>(game.GetRoundElapsed());
    o << "R" << game.GetRoundNumber() << (team == Team::T ? " T" : " CT")
      << " " << game.GetScoreT() << ":" << game.GetScoreCT()
      << " " << (total / 60) << ":" << (total % 60)
      << " 我" << game.AliveCount(team) << " 敌"
      << game.AliveCount(TeamManager::Opponent(team));
    // ---- 炸弹状态：放在最前面，优先级最高 ----
    o << "\n炸弹:";
    if (game.IsBombDropped()) {
        const std::string da = world ? world->FindAreaAt(game.BombDropX(), game.BombDropY())
                                     : std::string();
        o << "掉落@" << (da.empty() ? "未知" : da);
        if (game.HasBombIntel() && team == Team::CT) o << "(已知位置)";
    } else if (game.IsBombPlanted()) {
        const std::string site = world ? world->FindAreaAt(game.GetBombX(), game.GetBombY())
                                       : std::string();
        o << "已下@" << (site.empty() ? "未知" : site)
          << " 倒计时" << static_cast<int>(game.GetBombTimer()) << "s"
          << " 拆包需10s";
    } else if (game.PlayerHasC4()) {
        o << "未下(玩家携带)";
    } else {
        o << "未下";
    }

    int sum = 0, n = 0;
    if (game.GetPlayer().team == team) { sum += game.GetPlayer().money; ++n; }
    for (const auto& b : game.GetBOTs()) {
        if (b.team == team) { sum += b.money; ++n; }
    }
    const int avg = (n > 0) ? sum / n : 0;
    o << " 经济" << (avg < 1800 ? "穷" : (avg < 3500 ? "中" : "富"));


    o << "\n敌:";
    bool any = false;
    for (const auto& c : game.GetContactsOfTeam(team)) {
        const std::string area = world ? world->FindAreaAt(c.x, c.y) : std::string();
        if (area.empty()) continue;
        o << " " << area << "x1(" << static_cast<int>(c.age) << "s)";
        any = true;
    }
    if (!any) o << " 未知";

    if (!lastOrder.empty()) o << "\n上条:" << lastOrder;

    // 笔记只参考最新 1 条，且明确标注优先级低于炸弹/经济
    if (!notes.empty()) {
        o << "\n参考笔记(勿覆盖炸弹/经济): " << notes.back();
    }

    o << "\n事件:";
    switch (ev.type) {
        case CoachEventType::RoundStart:    o << "回合开始"; break;
        case CoachEventType::EnemySpotted:  o << "首次接火@" << ev.area; break;
        case CoachEventType::AllyDown:      o << "我方减员@" << ev.area; break;
        case CoachEventType::EnemyDown:     o << "敌方减员@" << ev.area; break;
        case CoachEventType::BombPlanted:   o << "C4已安装@" << ev.area; break;
        case CoachEventType::DefuseStarted: o << "有人在拆包"; break;
        case CoachEventType::DefuseAborted: o << "拆包中断"; break;
        case CoachEventType::TimeLow:       o << "时间不足"; break;
        case CoachEventType::EconomyLow:    o << "经济不足"; break;
        case CoachEventType::HumanDied:     o << "玩家阵亡"; break;
        case CoachEventType::MatchPoint:    o << "赛点"; break;
        case CoachEventType::Heartbeat:     o << "例行检查"; break;
        default: break;
    }
    // ---- 合法闭集（重点卡 intent）----
    o << "\n允许:intent=";
    for (size_t ai = 0; ai < allow.intents.size(); ++ai) {
        if (ai) o << ',';
        o << allow.intents[ai];
    }
    o << " target=";
    for (size_t ai = 0; ai < allow.targets.size(); ++ai) {
        if (ai) o << ',';
        o << (allow.targets[ai].empty() ? "-" : allow.targets[ai]);
    }
    o << " area_n=" << allow.areas.size();

    // ---- 最近 3 回合（结构化一行，不塞死亡坐标）----
    if (!memory.empty()) {
        o << "\n近局:";
        int mk = 0;
        for (auto it = memory.rbegin(); it != memory.rend() && mk < 3; ++it, ++mk) {
            const RoundMemo& m = *it;
            if (mk) o << "; ";
            o << "R" << m.roundNo << ' ' << IntentLabel(m.plan)
              << "->" << (m.target.empty() ? "-" : m.target)
              << (m.won ? " 胜" : " 负");
            if (!m.firstContactArea.empty()) o << " 首触@" << m.firstContactArea;
            if (m.bombPlanted) o << " 已下包";
        }
    }

    return o.str();
}

// 解析 + 游戏侧二次校验
bool ParseCoachPlan(const std::string& raw, TeamPlan& out, const CS2DWorld* world,
                    bool bombPlanted, bool teamIsCT, bool lowEconomy,
                    const std::string& plantSiteArea,
                    const CoachAllowSet& allow, float& ttlOut) {
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
    if (!ParseIntent(Lower(root.value<std::string>("intent", "")), plan.intent)) {
        return false;
    }

    // ---- 闭集硬过滤：越界整份拒绝（沿用规则计划）----
    if (!ContainsCI(allow.intents, IntentLabel(plan.intent))) {
        return false;
    }

    plan.target = root.value<std::string>("target", "");
    if (!plan.target.empty() && !ContainsCI(allow.targets, plan.target)) {
        return false;
    }
    ParseTempo(Lower(root.value<std::string>("tempo", "normal")), plan.tempo);
    plan.reason = root.value<std::string>("reason", "");

    ttlOut = static_cast<float>(root.value<double>("ttl_s", 12.0));
    ttlOut = std::clamp(ttlOut, 3.0f, 30.0f);

    if (root.contains("slots") && root["slots"].is_array()) {
        const auto& arr = root["slots"];
        const size_t n = std::min<size_t>(arr.size(), 5);
        std::array<bool, 5> filled{};
        for (size_t i = 0; i < n; ++i) {
            const auto& sl = arr[i];
            int slot = static_cast<int>(sl.value<double>("slot", static_cast<double>(i)));
            if (slot < 0 || slot > 4 || filled[static_cast<size_t>(slot)]) continue;

            CoachRole role = CoachRole::Support;
            ParseRole(Lower(sl.value<std::string>("role", "")), role);

            std::string area = sl.value<std::string>("area", "");
            if (!area.empty() && !allow.areas.empty() &&
                !ContainsCI(allow.areas, area)) {
                continue;      // 区域不在合法闭集内：跳过这个 slot
            }
            if (!area.empty() && world) {
                bool found = false;
                for (const auto& a : world->GetAreas()) {
                    if (Lower(a.name) == Lower(area)) { found = true; break; }
                }
                if (!found) area.clear();
            }
            if (area.empty()) continue;      // 没有区域就没有意义

            auto& dst = plan.slots[static_cast<size_t>(slot)];
            dst.role      = role;
            dst.area      = area;
            dst.canTrade  = true;
            dst.canRotate = (role == CoachRole::Lurk);
            filled[static_cast<size_t>(slot)] = true;
        }
    }

    // 语义校验
    if (!bombPlanted && (plan.intent == PlanIntent::RetakeA ||
                         plan.intent == PlanIntent::RetakeB)) {
        plan.intent = PlanIntent::Default;
    }
    // save 只允许「经济确实穷」且「还没下包」时使用；否则会变成消极送分
    if (plan.intent == PlanIntent::Save && (!lowEconomy || bombPlanted)) {
        plan.intent = bombPlanted ? PlanIntent::RetakeA : PlanIntent::Default;
    }
    if (teamIsCT) {
        if (plan.intent == PlanIntent::RushA || plan.intent == PlanIntent::SplitA) {
            plan.intent = PlanIntent::StackA;
        } else if (plan.intent == PlanIntent::RushB || plan.intent == PlanIntent::SplitB) {
            plan.intent = PlanIntent::StackB;
        } else if (plan.intent == PlanIntent::FakeThenOther) {
            plan.intent = PlanIntent::Default;
        }
    } else if (plan.intent == PlanIntent::RetakeA || plan.intent == PlanIntent::RetakeB) {
        // T 侧不能"重夺"（那是 CT 的战术），也别用 stack 这种防守站位
        plan.intent = PlanIntent::Default;
    } else if (plan.intent == PlanIntent::StackA || plan.intent == PlanIntent::StackB) {
        plan.intent = PlanIntent::Default;   // stack 仅 CT 有效
    }

    // ---- 硬规则 1：已下包必须锁在包点一侧 ----
    if (bombPlanted) {
        const std::string side = SiteSideFromArea(plantSiteArea);
        if (!side.empty()) {
            const bool onB = (side == "B");
            if (teamIsCT) {
                const bool okIntent = onB ? (plan.intent == PlanIntent::RetakeB)
                                          : (plan.intent == PlanIntent::RetakeA);
                const bool okTarget = onB ? (plan.target == "B") : (plan.target == "A");
                if (!okIntent || !okTarget) return false;
            } else {
                const bool okIntent = onB
                    ? (plan.intent == PlanIntent::StackB || plan.intent == PlanIntent::Default)
                    : (plan.intent == PlanIntent::StackA || plan.intent == PlanIntent::Default);
                const bool okTarget = onB ? (plan.target == "B") : (plan.target == "A");
                if (!okIntent || !okTarget) return false;
            }
        }
    }

    // ---- 硬规则 2：穷局不许主动进攻 ----
    if (lowEconomy && !bombPlanted) {
        if (teamIsCT) {
            if (plan.intent != PlanIntent::Save &&
                plan.intent != PlanIntent::Default &&
                plan.intent != PlanIntent::StackA &&
                plan.intent != PlanIntent::StackB) {
                return false;
            }
        } else if (plan.intent != PlanIntent::Save &&
                   plan.intent != PlanIntent::Default) {
            return false;
        }
    }

    out = plan;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 发起计划请求
// ---------------------------------------------------------------------------
void CS2DGame::CoachTryLlm(Team t, const CoachEvent& ev) {
    if (!coachLlmEnabled_ || !coachLlm_) return;
    if (!world_) return;

    CoachState& st = CoachFor(t);
    if (st.llmPending) return;      // 同时只允许一个在途请求

    switch (ev.type) {
        case CoachEventType::RoundStart:
        case CoachEventType::BombPlanted:
        case CoachEventType::DefuseStarted:
        case CoachEventType::TimeLow:
        case CoachEventType::MatchPoint:
            break;
        case CoachEventType::Heartbeat:
            return;    // 心跳只刷新规则（规则层在 CoachThink 里已跑），不再烧 LLM
        default:
            return;                 // 战术级事件交给规则层
    }

    const bool lowEco = LowEconomyAverage(t);
    const CoachAllowSet allow = BuildAllowSet(t, bombPlanted_, lowEco, world_.get());
    const std::string user = BuildStateDigest(*this, t, ev, st.notes,
                                              st.lastOrder, world_.get(),
                                              st.memory, allow);

    st.llmPending     = true;
    st.llmGen         = st.gen;
    st.llmAt          = clockSec_;
    st.llmEventType   = static_cast<int>(ev.type);
    st.llmRoundNo     = round_.RoundNumber();
    st.llmBombPlanted = bombPlanted_;

    const int gen = st.gen;

    coachLlm_(BuildSystemPrompt(world_.get()), user, 150, BuildCoachSchema(allow),
              [this, t, gen](const std::string& raw) {
                  std::lock_guard<std::mutex> lock(coachLlmMutex_);
                  coachLlmResults_.push_back(CoachLlmResult{t, gen, raw, false});
              });
}

// ---------------------------------------------------------------------------
// 回合复盘：请求一句话教训
// ---------------------------------------------------------------------------
void CS2DGame::CoachTryDebrief(Team t) {
    if (!coachLlmEnabled_ || !coachLlm_ || !world_) return;

    CoachState& st = CoachFor(t);
    if (st.memory.empty()) return;

    const RoundMemo& m = st.memory.back();

    std::ostringstream o;
    o << "回合R" << m.roundNo << " 阵营" << (t == Team::T ? "T" : "CT")
      << " 战术" << IntentLabel(m.plan)
      << " 目标" << (m.target.empty() ? "-" : m.target)
      << " 结果" << (m.won ? "胜" : "负")
      << " 比分" << m.scoreT << ":" << m.scoreCT
      << " 收尾存活" << m.aliveEnd
      << (m.bombPlanted ? " 已下包" : " 未下包");
    if (!m.firstContactArea.empty()) o << " 首接触@" << m.firstContactArea;

    const std::string sys =
        "只根据给定事实写一句不超过 18 个汉字的教训。"
        "只允许写下包点、首接触区域、是否超时、攻防结果；"
        "禁止猜测「习惯/性格/耐心」，不要写未出现的信息。只输出一句话，不要引号。";

    const int gen = st.gen;
    coachLlm_(sys, o.str(), 48, std::string(),
              [this, t, gen](const std::string& raw) {
                  std::lock_guard<std::mutex> lock(coachLlmMutex_);
                  coachLlmResults_.push_back(CoachLlmResult{t, gen, raw, true});
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

        // ---------- 复盘笔记 ----------
        if (r.debrief) {
            std::string note = r.raw;
            note.erase(std::remove_if(note.begin(), note.end(),
                                      [](unsigned char c) { return c == '\n' || c == '\r'; }),
                       note.end());
            while (!note.empty() && (note.front() == '"' || note.front() == ' ')) {
                note.erase(note.begin());
            }
            while (!note.empty() && (note.back() == '"' || note.back() == ' ')) {
                note.pop_back();
            }
            if (!note.empty()) {
                if (note.size() > 60) note.resize(60);
                st.notes.push_back(note);
                while (st.notes.size() > 5) st.notes.pop_front();
                std::cout << "[Coach] " << TeamName(r.team) << " 笔记: " << note << std::endl;
            }
            continue;
        }

        st.llmPending = false;

        // 回包过期：代际号 / 回合号 / 炸弹状态任一变化都丢弃
        if (r.gen < st.gen) {
            std::cout << "[Coach] " << TeamName(r.team) << " LLM 回包过期(gen)，丢弃" << std::endl;
            continue;
        }
        if (st.llmRoundNo != round_.RoundNumber() || st.llmBombPlanted != bombPlanted_) {
            std::cout << "[Coach] " << TeamName(r.team) << " LLM 回包状态已变，丢弃" << std::endl;
            continue;
        }
        if (r.raw.empty()) {
            std::cout << "[Coach] " << TeamName(r.team) << " LLM 无回复，沿用规则计划" << std::endl;
            continue;
        }

        // 回包时用「当前」局面重建闭集（发请求后局面可能已变）
        const CoachAllowSet allow = BuildAllowSet(r.team, bombPlanted_,
                                                  LowEconomyAverage(r.team), world_.get());

        std::string plantArea;
        if (bombPlanted_ && world_) plantArea = world_->FindAreaAt(bombX_, bombY_);

        const bool lowEco = LowEconomyAverage(r.team);

        TeamPlan plan;
        float ttl = 12.0f;
        if (!ParseCoachPlan(r.raw, plan, world_.get(), bombPlanted_,
                            r.team == Team::CT, lowEco, plantArea, allow, ttl)) {
            std::cout << "[Coach] " << TeamName(r.team)
                      << " LLM 计划非法(硬规则:plant/save)，沿用规则 | "
                      << r.raw.substr(0, 100) << std::endl;
            continue;
        }

        // 双保险：穷局且未下包，只接受 save（其余一律丢弃）
        if (lowEco && !bombPlanted_ && plan.intent != PlanIntent::Save) {
            std::cout << "[Coach] " << TeamName(r.team)
                      << " 穷局拒绝进攻性 LLM 计划，沿用规则计划" << std::endl;
            continue;
        }

        // TTL 下限：开局 18s / 下包·拆包 12s / 其它 10s
        {
            float floorTtl = 10.0f;
            if (st.llmEventType == static_cast<int>(CoachEventType::RoundStart))      floorTtl = 18.0f;
            else if (st.llmEventType == static_cast<int>(CoachEventType::BombPlanted) ||
                     st.llmEventType == static_cast<int>(CoachEventType::DefuseStarted)) floorTtl = 12.0f;
            ttl = std::min(30.0f, std::max(ttl, floorTtl));
        }

        plan.gen       = st.gen;
        plan.createdAt = clockSec_;
        plan.expiresAt = clockSec_ + ttl;      // 由模型给的 ttl_s 决定
        plan.valid     = true;
        st.planExpiresAt = plan.expiresAt;
        st.plan   = plan;
        st.reason = plan.reason;
        st.stickyLlm = true;       // LLM owns plan until expiry/critical

        {   // 记录上一条指令，供下一次 prompt 使用
            std::ostringstream o;
            o << IntentLabel(plan.intent);
            if (!plan.target.empty()) o << "->" << plan.target;
            st.lastOrder = o.str();
        }

        coachPlanChangedAt_ = clockSec_;
        std::cout << "[Coach] " << TeamName(r.team) << " LLM("
                  << (clockSec_ - st.llmAt) << "s, ttl=" << static_cast<int>(ttl) << "s) -> "
                  << IntentLabel(plan.intent)
                  << " 目标=" << (plan.target.empty() ? "-" : plan.target)
                  << " | " << plan.reason << " [sticky]" << std::endl;

        CoachApplyPlan(r.team);
    }

    // 超时兜底
    for (Team t : {Team::T, Team::CT}) {
        CoachState& st = CoachFor(t);
        const float deadline =
            (st.llmEventType == static_cast<int>(CoachEventType::RoundStart)) ? 7.0f : 4.0f;
        if (st.llmPending && clockSec_ - st.llmAt > deadline) {
            st.llmPending = false;
            std::cout << "[Coach] " << TeamName(t) << " LLM 超时，沿用规则计划" << std::endl;
        }
    }
}
