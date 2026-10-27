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
#include <unordered_set>
#include <unordered_map>

// ============================================================================
// 教练层的 LLM 接入（结构化输出 + 短摘要 + TTL + 复盘笔记）
//
//   - system prompt 按地图固定（角色 / 输出契约 / 区域表 / _common + 地图手册）
//   - user 只放「会变的短摘要」，控制在几十 token
//   - 战术库按 playbookId 缓存；缺地图文件时只用 _common.txt
//   - 加载时用 world->GetAreas() 校验手册里的区域名，尽早暴露拼写错误
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
    case PlanIntent::RotateA: return "rotate_a";
    case PlanIntent::RotateB: return "rotate_b";
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
    if (v == "rotate_a") { out = PlanIntent::RotateA; return true; }
    if (v == "rotate_b") { out = PlanIntent::RotateB; return true; }
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


// 去掉 # 注释行，并按行截断到 kMaxBookBytes
std::string CleanPlaybookText(std::string raw) {
    constexpr size_t kMaxBookBytes = 12000;
    std::string cleaned;
    cleaned.reserve(raw.size());
    std::istringstream lines(raw);
    std::string line;
    while (std::getline(lines, line)) {
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed.erase(trimmed.begin());
        if (!trimmed.empty() && trimmed.front() == '#') continue;
        cleaned.append(line);
        cleaned.push_back('\n');
    }
    if (cleaned.size() > kMaxBookBytes) {
        size_t cut = cleaned.rfind('\n', kMaxBookBytes);
        if (cut == std::string::npos) cut = kMaxBookBytes;
        cleaned.resize(cut);
        std::cout << "[Coach] 战术库超过 " << kMaxBookBytes
                  << " 字节，已按行截断" << std::endl;
    }
    return cleaned;
}

std::string ReadPlaybookFile(const std::string& relPath) {
    const std::string path = PathUtils::ResolveAppPath(relPath);
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string cleaned = CleanPlaybookText(buf.str());
    if (!cleaned.empty()) {
        std::cout << "[Coach] 已加载战术库 (" << cleaned.size()
                  << " bytes): " << path << std::endl;
    }
    return cleaned;
}

std::string TrimText(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// 解析地图文件头部的 [areas] 清单。
// 清单是「引擎区域名」的唯一权威声明，用于加载时校验；格式为空格/逗号分隔。
std::vector<std::string> ParseAreaManifest(const std::string& book) {
    std::vector<std::string> out;
    std::istringstream in(book);
    std::string line;
    bool inAreas = false;
    while (std::getline(in, line)) {
        const std::string t = TrimText(line);
        if (t.empty()) continue;
        if (t.front() == '[') {
            inAreas = (t == "[areas]" || t == "[area]");
            continue;
        }
        if (!inAreas) continue;
        if (t.front() == '#' || t.rfind("【", 0) == 0) { inAreas = false; continue; }

        std::string clean = t;
        const size_t hash = clean.find('#');
        if (hash != std::string::npos) clean.resize(hash);

        std::string cur;
        for (char ch : clean) {
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == ';') {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

// [areas] 只用于加载校验；送进模型前去掉，避免和「可选区域」重复占 token。
std::string StripAreaManifest(const std::string& book) {
    std::istringstream in(book);
    std::ostringstream out;
    std::string line;
    bool inAreas = false;
    while (std::getline(in, line)) {
        const std::string t = TrimText(line);
        if (t == "[areas]" || t == "[area]") { inAreas = true; continue; }
        if (inAreas) {
            if (t.empty()) continue;
            if (t.front() != '[' && t.rfind("【", 0) != 0 && t.front() != '#') {
                continue;   // [areas] 段内的区域 token，不送模型
            }
            inAreas = false;   // 遇到下一个 section / 标题，继续正常输出
        }
        out << line << '\n';
    }
    return out.str();
}

// 收集手册里 "area":"X" / area="X" 两种写法引用的区域名（few-shot 与模板）。
void CollectAreaReferences(const std::string& book, const std::string& key,
                           char sep, std::vector<std::string>& out) {
    size_t pos = 0;
    while ((pos = book.find(key, pos)) != std::string::npos) {
        size_t p = pos + key.size();
        while (p < book.size() && std::isspace(static_cast<unsigned char>(book[p]))) ++p;
        if (p >= book.size() || book[p] != sep) { pos = p; continue; }
        ++p;
        while (p < book.size() && std::isspace(static_cast<unsigned char>(book[p]))) ++p;
        if (p >= book.size() || (book[p] != '"' && book[p] != '\'')) { pos = p; continue; }
        const char quote = book[p++];
        const size_t end = book.find(quote, p);
        if (end == std::string::npos) break;
        out.push_back(book.substr(p, end - p));
        pos = end + 1;
    }
}

// 收集正文里的 snake_case token（A_Site / B_Apts ...），用于抓模板/剧本中的拼写错误。
// 纯英文单词（Mid/Long/Site）不在此列，避免把社区别名/普通英文误报。
void CollectSnakeCaseTokens(const std::string& book, std::vector<std::string>& out) {
    std::string cur;
    auto flush = [&]() {
        if (cur.find('_') != std::string::npos) out.push_back(cur);
        cur.clear();
    };
    for (unsigned char c : book) {
        if (std::isalnum(c) || c == '_') {
            cur.push_back(static_cast<char>(c));
        } else {
            flush();
        }
    }
    flush();
}

// 加载时校验：手册声明的区域名 / slots 示例引用，必须存在于当前地图。
// 只警告不阻断；大小写不一致会给出正确写法，方便直接改手册。
void ValidatePlaybookAreasOnce(const std::string& mapId, const std::string& book,
                               const CS2DWorld* world) {
    if (!world || book.empty() || mapId.empty()) return;
    static std::unordered_set<std::string> validated;
    if (!validated.insert(mapId).second) return;

    std::unordered_set<std::string> exact;
    std::unordered_map<std::string, std::string> byLower;
    for (const auto& a : world->GetAreas()) {
        exact.insert(a.name);
        byLower.emplace(Lower(a.name), a.name);
    }

    const std::vector<std::string> declared = ParseAreaManifest(book);
    std::vector<std::string> referenced;
    CollectAreaReferences(book, "\"area\"", ':', referenced);
    CollectAreaReferences(book, "area=", '=', referenced);
    std::vector<std::string> proseTokens;
    CollectSnakeCaseTokens(book, proseTokens);

    if (declared.empty()) {
        std::cout << "[Coach] 警告: 战术库缺少 [areas] 区域清单，跳过区域名校验 ("
                  << mapId << ")" << std::endl;
    }

    std::unordered_set<std::string> reported;
    int unknown = 0;
    auto check = [&](const std::string& name, const char* source) {
        if (name.empty()) return;
        if (exact.find(name) != exact.end()) return;

        const auto lowerIt = byLower.find(Lower(name));
        if (lowerIt != byLower.end()) {
            if (reported.insert("case:" + Lower(name)).second) {
                std::cout << "[Coach] 警告: 战术库 " << source << " 区域 \""
                          << name << "\" 大小写不匹配，应为 \""
                          << lowerIt->second << "\" (" << mapId << ")" << std::endl;
            }
            return;
        }
        if (!reported.insert(name).second) return;
        ++unknown;
        std::cout << "[Coach] 警告: 战术库 " << source << " 区域 \"" << name
                  << "\" 不在 " << mapId << " 的区域表中" << std::endl;
    };

    for (const auto& n : declared) check(n, "[areas]");
    for (const auto& n : referenced) check(n, "slots.area");

    // 模板/剧本正文里的 snake_case token：跳过 intent/字段名，其余按区域名校验。
    static const std::unordered_set<std::string> kNonAreaTokens = {
        "ttl_s", "retake_", "retake_a", "retake_b",
        "rush_", "rush_a", "rush_b", "split_", "split_a", "split_b",
        "stack_", "stack_a", "stack_b", "rotate_", "rotate_a", "rotate_b",
        "force_buy", "half_buy", "full_buy", "eco_buy"
    };
    for (const auto& n : proseTokens) {
        if (kNonAreaTokens.find(n) == kNonAreaTokens.end()) check(n, "正文");
    }

    if (!declared.empty()) {
        std::cout << "[Coach] 战术库区域校验(" << mapId << "): 清单 "
                  << declared.size() << " 个, slots 示例 " << referenced.size()
                  << " 个, 未知 " << unknown << std::endl;
    }
}

bool IsSafePlaybookId(const std::string& id) {
    if (id.empty()) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

// 战术库：按 playbookId 缓存，最终内容 = _common.txt + <playbookId>.txt。
// 路径 assets/config/coach/<playbookId>.txt；某图缺专属文件时只返回通用规则。
// 缓存只依赖文件内容，不依赖世界状态；一局内 system 前缀因此保持字节稳定。
const std::string& LoadCoachPlaybook(const std::string& playbookId) {
    static std::unordered_map<std::string, std::string> cache;
    static const std::string kEmpty;

    if (!IsSafePlaybookId(playbookId)) {
        if (!playbookId.empty()) {
            std::cout << "[Coach] 非法战术库 id，忽略: " << playbookId << std::endl;
        }
        return kEmpty;
    }

    auto it = cache.find(playbookId);
    if (it != cache.end()) return it->second;

    // 约 10KB 手册 ≈ 3.8k tokens；llama-server 单槽 4096，留出 user 摘要与输出。
    constexpr size_t kMaxTotalBookBytes = 10000;
    std::string body;

    const std::string common =
        ReadPlaybookFile("assets/config/coach/_common.txt");
    if (common.empty()) {
        std::cout << "[Coach] 警告: 缺少通用战术库 assets/config/coach/_common.txt"
                  << std::endl;
    } else {
        body += common;
    }

    const std::string mapRel = "assets/config/coach/" + playbookId + ".txt";
    const std::string mapBody = ReadPlaybookFile(mapRel);
    const bool hasDedicated = !mapBody.empty();
    if (!hasDedicated) {
        std::cout << "[Coach] 地图 " << playbookId
                  << " 无专属战术库，仅使用通用规则: "
                  << PathUtils::ResolveAppPath(mapRel) << std::endl;
    } else {
        if (!body.empty() && body.back() != '\n') body.push_back('\n');
        body += mapBody;
    }

    if (body.size() > kMaxTotalBookBytes) {
        size_t cut = body.rfind('\n', kMaxTotalBookBytes);
        if (cut == std::string::npos) cut = kMaxTotalBookBytes;
        body.resize(cut);
        std::cout << "[Coach] 战术库超过 " << kMaxTotalBookBytes
                  << " 字节，已按行截断 (" << playbookId << ")" << std::endl;
    }

    auto inserted = cache.emplace(playbookId, std::move(body));
    std::cout << "[Coach] 战术库就绪(" << playbookId << "): "
              << inserted.first->second.size() << " bytes"
              << (hasDedicated ? "" : " [common only]") << std::endl;
    return inserted.first->second;
}

// system prompt：角色 / 输出契约 / 当前区域表 + 战术手册。
// 内容只由 mapId + playbook 决定，一局内字节稳定，cache_prompt 前缀缓存有效。
std::string BuildSystemPrompt(const CS2DWorld* world,
                              const std::string& mapId,
                              const std::string& playbookId) {
    std::ostringstream o;
    o << "你是 CS2D 战队主教练。只做战术决策，不做瞄准与走位。\n"
      << "严格只输出一个 JSON 对象：不要 YAML、不要解释、不要代码块。\n"
      << "示例：{\"intent\":\"rush_b\",\"target\":\"B\",\"tempo\":\"fast\","
         "\"ttl_s\":12,\"reason\":\"敌方减员速推B\","
         "\"slots\":[{\"slot\":0,\"role\":\"entry\",\"area\":\"A_Site\"}]}\n"
      << "字段含义：\n"
      << " intent: 只能从 user 行「允许:intent=...」中选，不得使用其它值\n"
      << "   retake_* 仅在 C4 已被安装时使用；save 仅在经济崩溃时使用\n"
      << "   rush_*/split_*/fake 是 T 战术；CT 用 default/stack_*/retake_*\n"
      << " target: A / B / MID / 空；已下包时必须是包点侧\n"
      << " tempo: slow/normal/fast\n"
      << " ttl_s: 指令有效秒数（3~30），场面易变给小值\n"
      << " slots: 最多 5 个，slot 0~4，area 必须是下面「可选区域」之一\n"
      << " reason: 不超过 20 个汉字，只用于日志\n"
      << "当前地图: " << mapId << "\n";
    if (world) {
        o << "可选区域:";
        for (const auto& a : world->GetAreas()) o << ' ' << a.name;
        o << "\n";
    }

    const std::string& book = LoadCoachPlaybook(playbookId);
    ValidatePlaybookAreasOnce(mapId, book, world);
    if (!book.empty()) {
        o << "\n【战术手册】\n" << StripAreaManifest(book);
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

    // 经济摘要：精确人均 $ + 穷/中/富 + 购买档(半起/强起拆开中档) + 连败 + 有枪 + 敌经济
    // 「中」通过 购买=半起|强起 区分；勿再写「强起/半起按穷局打」笼统合并。
    {
        const Team enemy = TeamManager::Opponent(team);
        int sum = 0, n = 0, eSum = 0, eN = 0;
        int rifleish = 0;
        auto tally = [&](const Player& p) {
            if (p.team == team) {
                sum += p.money; ++n;
                if (p.weapons[0] != WeaponId::None) {
                    const WeaponClass cls = WeaponDB::Get(p.weapons[0]).cls;
                    if (cls == WeaponClass::Rifle || cls == WeaponClass::Sniper) ++rifleish;
                }
            } else if (p.team == enemy) {
                eSum += p.money; ++eN;
            }
        };
        tally(game.GetPlayer());
        for (const auto& b : game.GetBOTs()) tally(b);
        const int avg  = (n  > 0) ? sum  / n  : 0;
        const int eAvg = (eN > 0) ? eSum / eN : 0;
        const char* band  = (avg  < 1800) ? "穷" : (avg  < 3500 ? "中" : "富");
        const char* eBand = (eAvg < 1800) ? "敌穷" : (eAvg < 3500 ? "敌中" : "敌富");
        // 优先用本回合已存的 buyPlan（CoachBeginRound 写入）；否则现算
        const BuyPlan bp = game.GetBuyPlan(team);
        const int streak = game.GetLossStreak(team);
        o << " 经济$" << avg << " " << band
          << " 购买=" << CS2DGame::BuyPlanLabel(bp)
          << " 连败" << streak
          << " 有枪" << rifleish << "/" << n
          << " 敌$" << eAvg << " " << eBand;
    }


    // ---- 敌方分布（按包点威胁度；未知 != 空）----
    {
        const TacticalIntel& in = game.Intel();
        if (in.SiteCount() > 0) {
            o << "\n敌分布:";
            for (int si = 0; si < in.SiteCount(); ++si) {
                const std::string an = in.AreaName(si);
                if (an.empty()) continue;
                o << (si ? " " : " ") << an << "=" << in.Heat(si);
            }
            if (in.ObservationCount() == 0) o << " (暂无情报)";
        }
    }

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
                    bool bombPlanted, bool teamIsCT, bool saveAllowed,
                    bool rotateAllowed,
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
                std::string canonical;
                for (const auto& a : world->GetAreas()) {
                    if (Lower(a.name) == Lower(area)) { canonical = a.name; break; }
                }
                area = std::move(canonical);   // 统一成引擎里的精确大小写
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
    if (plan.intent == PlanIntent::Save && (!saveAllowed || bombPlanted)) {
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

    // ---- 硬规则 1.5：转点必须有证据 + 时间允许 ----
    if (plan.intent == PlanIntent::RotateA || plan.intent == PlanIntent::RotateB) {
        if (!rotateAllowed) return false;
        const std::string side = (plan.intent == PlanIntent::RotateA) ? "A" : "B";
        if (plan.target != side) return false;
    }

    // ---- 硬规则 2：穷局不许主动进攻 ----
    if (saveAllowed && !bombPlanted) {
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
// 换图预热：加载战术库并按当前地图校验区域名
// ---------------------------------------------------------------------------
void CS2DGame::CoachWarmupPlaybook() {
    if (!world_) return;
    const std::string& book = LoadCoachPlaybook(playbookId_);
    ValidatePlaybookAreasOnce(mapId_, book, world_.get());
}

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

    coachLlm_(BuildSystemPrompt(world_.get(), mapId_, playbookId_), user, 150,
              BuildCoachSchema(allow),
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
        const bool saveAllowed = ShouldSaveNow(r.team);
        const bool rotateAllowed = CanRotateNow(r.team);

        TeamPlan plan;
        float ttl = 12.0f;
        if (!ParseCoachPlan(r.raw, plan, world_.get(), bombPlanted_,
                            r.team == Team::CT, saveAllowed, rotateAllowed, plantArea, allow, ttl)) {
            std::cout << "[Coach] " << TeamName(r.team)
                      << " LLM 计划非法(硬规则:plant/save)，沿用规则 | "
                      << r.raw.substr(0, 100) << std::endl;
            continue;
        }

        // 双保险：穷局且未下包，只接受 save（其余一律丢弃）
        if (saveAllowed && !bombPlanted_ && plan.intent != PlanIntent::Save) {
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
        if (plan.intent == PlanIntent::Save) st.buyPlan = BuyPlan::Eco;

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
