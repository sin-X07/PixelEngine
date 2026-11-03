// ============================================================================
// dev_replay_runner —— 开发期确定性 / 回放 / 基准工具
//
// 不启动渲染器与音频，用固定步长跑仿真，提供：
//   * 逐 tick 状态哈希（确定性校验）
//   * 回合级指标（基准对比）
//   * 批量基准（多地图 × 多种子）
//
// 本工具是**外挂**：只用 CS2DGame 的 public API 与公开输入接口，不修改游戏逻辑。
// 删除方式见 tools/dev_replay/README.md。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"
#include "../../PixelEngine/Core/PathUtils.hpp"

#include "conflicts.hpp"
#include "digest.hpp"
#include "metrics.hpp"
#include "replay.hpp"
#include "scripted_player.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct Options {
    int   ticks = 60 * 30;
    int   seed = 1;
    std::string map = "dust_2";
    int   viewportW = 1728;
    int   viewportH = 1080;

    float aimTurnRate = 3.0f;
    float aimErrorDeg = 5.0f;
    int   reactionTicks = 15;

    std::string hashPath;   // 单局逐 tick 哈希
    std::string outDir;     // 非空 → 批量模式
    std::vector<std::string> maps;
    std::vector<int> seeds;
    std::string tag = "unknown";   // baseline 版本号（建议填 commit SHA）

    std::string journalPath;                // 非空 → 录制输入日志
    std::string replayPath;                 // 非空 → 回放模式
    std::string dumpPath;                   // 调试：逐 tick 状态 dump
    std::string conflictsPath;             // 非空 → 写 AI 冲突 CSV
    std::vector<std::string> conflictIgnore;   // 按 kind 忽略
    bool enableNoisyConflicts = false;         // 开启 duplicate_hold 等高噪声规则
};

Options ParseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i] ? argv[i] : "";
        if (a.rfind("--ticks=", 0) == 0)         o.ticks = std::atoi(a.c_str() + 8);
        else if (a.rfind("--seed=", 0) == 0)     o.seed = std::atoi(a.c_str() + 7);
        else if (a.rfind("--map=", 0) == 0)      o.map = a.substr(6);
        else if (a.rfind("--hash=", 0) == 0)     o.hashPath = a.substr(7);
        else if (a.rfind("--out=", 0) == 0)      o.outDir = a.substr(6);
        else if (a.rfind("--tag=", 0) == 0)      o.tag = a.substr(6);
        else if (a.rfind("--conflicts=", 0) == 0) o.conflictsPath = a.substr(12);
        else if (a.rfind("--journal=", 0) == 0) o.journalPath = a.substr(10);
        else if (a.rfind("--replay=", 0) == 0) o.replayPath = a.substr(9);
        else if (a.rfind("--dump=", 0) == 0) o.dumpPath = a.substr(7);
        else if (a == "--noisy-conflicts") o.enableNoisyConflicts = true;
        else if (a.rfind("--conflict-ignore=", 0) == 0) {
            const std::string rest = a.substr(18);
            size_t pos = 0;
            while (pos <= rest.size()) {
                const size_t comma = rest.find(',', pos);
                const std::string item = rest.substr(pos, comma == std::string::npos
                                                            ? std::string::npos : comma - pos);
                if (!item.empty()) o.conflictIgnore.push_back(item);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
        else if (a.rfind("--aim-turn=", 0) == 0) o.aimTurnRate = static_cast<float>(std::atof(a.c_str() + 11));
        else if (a.rfind("--aim-error=", 0) == 0) o.aimErrorDeg = static_cast<float>(std::atof(a.c_str() + 12));
        else if (a.rfind("--reaction=", 0) == 0) o.reactionTicks = std::atoi(a.c_str() + 11);
        else if (a.rfind("--maps=", 0) == 0) {
            const std::string rest = a.substr(7);
            size_t pos = 0;
            while (pos <= rest.size()) {
                const size_t comma = rest.find(',', pos);
                const std::string item = rest.substr(pos, comma == std::string::npos
                                                            ? std::string::npos : comma - pos);
                if (!item.empty()) o.maps.push_back(item);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (a.rfind("--seeds=", 0) == 0) {
            const std::string rest = a.substr(8);
            const size_t dash = rest.find('-');
            if (dash != std::string::npos) {
                const int from = std::atoi(rest.substr(0, dash).c_str());
                const int to   = std::atoi(rest.substr(dash + 1).c_str());
                for (int v = from; v <= to; ++v) o.seeds.push_back(v);
            } else {
                size_t pos = 0;
                while (pos <= rest.size()) {
                    const size_t comma = rest.find(',', pos);
                    const std::string item = rest.substr(pos, comma == std::string::npos
                                                                ? std::string::npos : comma - pos);
                    if (!item.empty()) o.seeds.push_back(std::atoi(item.c_str()));
                    if (comma == std::string::npos) break;
                    pos = comma + 1;
                }
            }
        } else if (a.rfind("--viewport=", 0) == 0) {
            const std::string v = a.substr(11);
            const size_t x = v.find('x');
            if (x != std::string::npos) {
                o.viewportW = std::atoi(v.substr(0, x).c_str());
                o.viewportH = std::atoi(v.substr(x + 1).c_str());
            }
        }
    }
    if (o.maps.empty())  o.maps.push_back(o.map);
    if (o.seeds.empty()) o.seeds.push_back(o.seed);
    return o;
}

struct Summary {
    bool ok = false;
    std::string map;
    int   seed = 0;
    int   scoreT = 0, scoreCT = 0;
    Team  winner = Team::None;
    int   rounds = 0;
    int   ticks = 0;
    double avgRoundTicks = 0.0;
    int   plants = 0, defuses = 0;
    int   playerKills = 0, playerDeaths = 0;
    uint64_t finalHash = 0;
    int   conflicts = 0;      // AI 决策冲突条数（0 表示没触发任何规则）
    int lastTick_ = 0;
};

// 调试用：把每 tick 的关键状态写成一行，便于对比两个运行的首个差异字段
void DumpTick(std::FILE* f, const CS2DGame& game, uint64_t tick) {
    if (!f) return;
    const auto& p = game.GetPlayer();
    const auto& bots = game.GetBOTs();
    const auto& planT = game.GetCoachPlan(Team::T);
    const auto& planCT = game.GetCoachPlan(Team::CT);
    const auto& ai0 = game.AiAt(0);
    int bullets = 0;
    for (const auto& b : game.GetBullets()) if (b.active) ++bullets;
    std::fprintf(f, "%llu r%d ph%d px%.2f py%.2f ammo%d money%d alive%d "
                    "b0%.2f,%.2f,b0hp%d\n",
                 static_cast<unsigned long long>(tick),
                 game.GetRoundNumber(), static_cast<int>(game.GetRoundPhase()),
                 p.x, p.y, p.ammo, p.money, p.alive ? 1 : 0,
                 bots.empty() ? 0.0f : bots[0].x,
                 bots.empty() ? 0.0f : bots[0].y,
                 bots.empty() ? 0 : bots[0].health);
    std::fprintf(f, " planT=%d,%d,%s,%d planCT=%d,%d,%s,%d ai0=%d,%d %.1f,%.1f hold%d " 
                 "bul%d gre%zu smoke%zu fire%zu aim%.9g\n",
                 static_cast<int>(planT.intent), planT.gen, planT.target.c_str(),
                 static_cast<int>(planT.tempo),
                 static_cast<int>(planCT.intent), planCT.gen, planCT.target.c_str(),
                 static_cast<int>(planCT.tempo),
                 static_cast<int>(ai0.stance), static_cast<int>(ai0.maneuver),
                 ai0.planHoldX, ai0.planHoldY, ai0.hasPlanHold ? 1 : 0,
                 bullets, game.GetGrenades().size(), game.GetSmokes().size(),
                 game.GetFires().size(), p.aimAngle);
}
// 执行"购买动作"：录制与回放共用，保证两边时序完全一致
void ApplyBuy(CS2DGame& game, int weapon, int armorTier) {
    if (weapon > 0) {
        Presentation::BuyRequest r;
        r.kind = Presentation::BuyKind::Weapon;
        r.weapon = static_cast<WeaponId>(weapon);
        game.ExecutePurchase(r, nullptr);
    } else if (armorTier > 0) {
        Presentation::BuyRequest r;
        r.kind = Presentation::BuyKind::Armor;
        r.armorTier = armorTier;
        game.ExecutePurchase(r, nullptr);
    }
}

// 跑一局：固定 1/60 步长、renderer=nullptr、audio=nullptr
Summary RunOne(const Options& opt, const std::string& map, int seed,
               const std::string& hashPath, const std::string& roundsCsv) {
    Summary s;
    s.map = map;
    s.seed = seed;

    CS2DGame game;
    game.SetMatchMode(MatchMode::BombDefusal);
    game.SetRngSeed(static_cast<uint32_t>(seed));

    const std::string mapPath =
        PathUtils::ResolveAppPath("assets/cs2d/maps/" + map + ".tmj");
    if (!game.LoadMap(mapPath, nullptr, true)) {
        std::fprintf(stderr, "[dev_replay] 地图加载失败: %s\n", mapPath.c_str());
        return s;
    }
    game.Visibility().SetVisionRadius(
        VisibilityQuery::RadiusForViewport(opt.viewportW, opt.viewportH));

    dev_replay::ScriptedPlayer::Params sp;
    sp.turnRate = opt.aimTurnRate;
    sp.reactionTicks = opt.reactionTicks;
    sp.aimErrorDeg = opt.aimErrorDeg;
    dev_replay::ScriptedPlayer script(sp);

    dev_replay::RoundTracker tracker;
    tracker.Begin(game, 0);

    std::FILE* hashFile = nullptr;
    if (!hashPath.empty()) {
        hashFile = std::fopen(hashPath.c_str(), "wb");
        if (!hashFile) {
            std::fprintf(stderr, "[dev_replay] 无法写入哈希日志: %s\n", hashPath.c_str());
            return s;
        }
        std::fprintf(hashFile,
                     "# magic=PXHASH1 map=%s seed=%d viewport=%dx%d "
                     "aimTurn=%.2f aimErr=%.1f reaction=%d tag=%s\n",
                     map.c_str(), seed, opt.viewportW, opt.viewportH,
                     opt.aimTurnRate, opt.aimErrorDeg, opt.reactionTicks, opt.tag.c_str());
    }

    // ---- AI 决策冲突检查（只读；每 tick 跑一次）----
    dev_replay::ConflictChecker conflicts(opt.conflictIgnore, opt.enableNoisyConflicts);
    std::FILE* conflictFile = nullptr;
    if (!opt.conflictsPath.empty()) {
        conflictFile = std::fopen(opt.conflictsPath.c_str(), "wb");
        if (conflictFile) dev_replay::ConflictChecker::WriteHeader(conflictFile);
    }

    std::FILE* dumpFile = opt.dumpPath.empty() ? nullptr : std::fopen(opt.dumpPath.c_str(), "wb");
    const float dt = 1.0f / 60.0f;
    uint64_t finalHash = 0;
    int lastTick_ = 0;
    std::vector<dev_replay::InputFrame> journalFrames;
    const bool recordJournal = !opt.journalPath.empty();
    for (int t = 0; t < opt.ticks; ++t) {
        const auto in = script.Think(game, dt);
        if (recordJournal) {
            dev_replay::InputFrame fr;
            fr.tick = static_cast<uint32_t>(t);
            fr.mouseDown = in.mouseDown;
            fr.aimX = in.aimX;
            fr.aimY = in.aimY;
            fr.w = in.w; fr.a = in.a; fr.s = in.s; fr.d = in.d;
            fr.shift = in.shift; fr.reload = in.reload;
            fr.buyWeapon = in.buyWeapon;
            fr.buyArmorTier = in.buyArmorTier;
            journalFrames.push_back(fr);
        }
        ApplyBuy(game, in.buyWeapon, in.buyArmorTier);
        game.Update(dt, in.mouseDown, in.aimX, in.aimY,
                    in.w, in.a, in.s, in.d,
                    in.shift, in.reload, false,
                    nullptr);
        tracker.Sample(game, static_cast<uint64_t>(t));
        conflicts.Check(game, static_cast<uint64_t>(t), conflictFile);
        DumpTick(dumpFile, game, static_cast<uint64_t>(t));
        finalHash = dev_replay::SnapshotTick(game, static_cast<uint64_t>(t));
        if (hashFile) std::fprintf(hashFile, "%d %016" PRIx64 "\n", t, finalHash);
        // 比赛已决出：停止仿真（否则赛后空闲会被算进最后一回合时长）
        if (game.GetMatchWinner() != Team::None) { lastTick_ = t; break; }
        lastTick_ = t;
    }
    if (hashFile) std::fclose(hashFile);
    if (conflictFile) std::fclose(conflictFile);
    if (dumpFile) std::fclose(dumpFile);
    s.conflicts = conflicts.Total();
    tracker.Finish(game, static_cast<uint64_t>(lastTick_ + 1));

    // 输入日志：与 final_hash 一起写入，供回放自校验
    if (recordJournal) {
        dev_replay::JournalHeader jh;
        jh.map = map;
        jh.seed = seed;
        jh.viewportW = opt.viewportW;
        jh.viewportH = opt.viewportH;
        jh.aimTurnRate = opt.aimTurnRate;
        jh.aimErrorDeg = opt.aimErrorDeg;
        jh.reactionTicks = opt.reactionTicks;
        jh.tag = opt.tag;
        jh.finalHash = finalHash;
        jh.hasFinalHash = true;
        if (!dev_replay::WriteJournal(opt.journalPath, jh, journalFrames)) {
            std::fprintf(stderr, "[dev_replay] 无法写入输入日志: %s\n",
                         opt.journalPath.c_str());
        }
    }

    const auto& rows = tracker.Rows();
    double sumTicks = 0.0;
    for (const auto& r : rows) {
        sumTicks += static_cast<double>(r.ticks);
        if (r.plant)  ++s.plants;
        if (r.defuse) ++s.defuses;
    }
    s.rounds = static_cast<int>(rows.size());
    s.avgRoundTicks = rows.empty() ? 0.0 : sumTicks / static_cast<double>(rows.size());
    s.scoreT = game.GetScoreT();
    s.scoreCT = game.GetScoreCT();
    s.winner = game.GetMatchWinner();
    s.ticks = lastTick_ + 1;
    s.playerKills = game.GetPlayerKills();
    s.playerDeaths = game.GetPlayerDeaths();
    s.finalHash = finalHash;
    s.ok = true;

    if (!hashPath.empty() && conflicts.Total() > 0) conflicts.PrintSummary();
    if (!roundsCsv.empty()) dev_replay::WriteRoundsCsv(roundsCsv, rows);
    return s;
}

// 回放：按输入日志重建同一局，逐 tick 重算哈希，最后与日志里的 final_hash 比对
int RunReplay(const Options& opt) {
    dev_replay::JournalHeader jh;
    std::vector<dev_replay::InputFrame> frames;
    if (!dev_replay::ReadJournal(opt.replayPath, jh, frames)) {
        std::fprintf(stderr, "[replay] 读取输入日志失败: %s\n", opt.replayPath.c_str());
        return 2;
    }

    CS2DGame game;
    game.SetMatchMode(MatchMode::BombDefusal);
    game.SetRngSeed(static_cast<uint32_t>(jh.seed));

    const std::string mapPath =
        PathUtils::ResolveAppPath("assets/cs2d/maps/" + jh.map + ".tmj");
    if (!game.LoadMap(mapPath, nullptr, true)) {
        std::fprintf(stderr, "[replay] 地图加载失败: %s\n", mapPath.c_str());
        return 2;
    }
    game.Visibility().SetVisionRadius(
        VisibilityQuery::RadiusForViewport(jh.viewportW, jh.viewportH));

    std::FILE* hashFile = nullptr;
    if (!opt.hashPath.empty()) {
        hashFile = std::fopen(opt.hashPath.c_str(), "wb");
        if (hashFile) {
            std::fprintf(hashFile,
                         "# magic=PXHASH1 map=%s seed=%d viewport=%dx%d "
                         "aimTurn=%.2f aimErr=%.1f reaction=%d tag=%s\n",
                         jh.map.c_str(), jh.seed, jh.viewportW, jh.viewportH,
                         jh.aimTurnRate, jh.aimErrorDeg, jh.reactionTicks, jh.tag.c_str());
        }
    }

    std::FILE* dumpFile = opt.dumpPath.empty() ? nullptr : std::fopen(opt.dumpPath.c_str(), "wb");
    const float dt = 1.0f / 60.0f;
    uint64_t finalHash = 0;
    for (const auto& fr : frames) {
        ApplyBuy(game, fr.buyWeapon, fr.buyArmorTier);
        game.Update(dt, fr.mouseDown, fr.aimX, fr.aimY,
                    fr.w, fr.a, fr.s, fr.d,
                    fr.shift, fr.reload, fr.interact,
                    nullptr);
        DumpTick(dumpFile, game, static_cast<uint64_t>(fr.tick));
        finalHash = dev_replay::SnapshotTick(game, static_cast<uint64_t>(fr.tick));
        if (hashFile) std::fprintf(hashFile, "%u %016" PRIx64 "\n", fr.tick, finalHash);
    }
    if (hashFile) std::fclose(hashFile);
    if (dumpFile) std::fclose(dumpFile);
    std::printf("[replay] map=%s seed=%d frames=%zu\n", jh.map.c_str(), jh.seed, frames.size());
    std::printf("[replay] final_hash=%016" PRIx64 "  expected=%016" PRIx64 "\n",
                finalHash, jh.finalHash);
    if (jh.hasFinalHash && finalHash != jh.finalHash) {
        std::printf("[replay] MISMATCH：输入 + 种子没能复现该局（先用 diff_hash_logs 定位）\n");
        return 1;
    }
    std::printf("[replay] MATCH：输入 + 种子完整复现该局\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = ParseArgs(argc, argv);
    if (!opt.replayPath.empty()) return RunReplay(opt);
    const bool batch = !opt.outDir.empty();

    if (!batch) {
        const std::string roundsCsv = opt.hashPath.empty() ? std::string()
                                                          : opt.hashPath + ".rounds.csv";
        const Summary s = RunOne(opt, opt.map, opt.seed, opt.hashPath, roundsCsv);
        if (!s.ok) return 1;
        std::printf("[dev_replay] map=%s seed=%d ticks=%d aimTurn=%.2f aimErr=%.1f reaction=%d\n",
                    s.map.c_str(), s.seed, s.ticks, opt.aimTurnRate, opt.aimErrorDeg,
                    opt.reactionTicks);
        std::printf("[dev_replay] final_hash=%016" PRIx64 "\n", s.finalHash);
        std::printf("[dev_replay] player kills=%d deaths=%d  ai_conflicts=%d\n",
                    s.playerKills, s.playerDeaths, s.conflicts);
        std::printf("[dev_replay] score T=%d CT=%d winner=%s rounds=%d avgRoundTicks=%.0f "
                    "plants=%d defuses=%d\n",
                    s.scoreT, s.scoreCT, dev_replay::TeamTag(s.winner),
                    s.rounds, s.avgRoundTicks, s.plants, s.defuses);
        return 0;
    }

    // ---- 批量基准：maps × seeds ----
    std::error_code ec;
    std::filesystem::create_directories(opt.outDir, ec);
    const std::string summaryPath = opt.outDir + "/summary.csv";
    std::FILE* summary = std::fopen(summaryPath.c_str(), "wb");
    if (!summary) {
        std::fprintf(stderr, "[dev_replay] 无法写入 %s\n", summaryPath.c_str());
        return 1;
    }
    std::fprintf(summary,
                 "# PXBENCH1 tag=%s ticks=%d viewport=%dx%d aimTurn=%.2f aimErr=%.1f reaction=%d\n",
                 opt.tag.c_str(), opt.ticks, opt.viewportW, opt.viewportH,
                 opt.aimTurnRate, opt.aimErrorDeg, opt.reactionTicks);
    std::fprintf(summary,
                 "map,seed,winner,score_t,score_ct,rounds,ticks,avg_round_ticks,"
                 "plants,defuses,player_kills,player_deaths,ai_conflicts,final_hash\n");

    int games = 0, tWins = 0, ctWins = 0, failed = 0;
    double sumAvgRoundTicks = 0.0;
    int sumRounds = 0, sumPlants = 0, sumDefuses = 0;

    for (const auto& map : opt.maps) {
        for (int seed : opt.seeds) {
            const std::string base = opt.outDir + "/" + map + "-s" + std::to_string(seed);
            const Summary s = RunOne(opt, map, seed, base + ".hash.txt", base + ".rounds.csv");
            if (!s.ok) { ++failed; continue; }
            ++games;
            if (s.winner == Team::CT) ++ctWins;
            if (s.winner == Team::T)  ++tWins;
            sumAvgRoundTicks += s.avgRoundTicks;
            sumRounds += s.rounds;
            sumPlants += s.plants;
            sumDefuses += s.defuses;
            std::fprintf(summary,
                         "%s,%d,%s,%d,%d,%d,%d,%.0f,%d,%d,%d,%d,%d,%016" PRIx64 "\n",
                         s.map.c_str(), s.seed, dev_replay::TeamTag(s.winner),
                         s.scoreT, s.scoreCT, s.rounds, s.ticks, s.avgRoundTicks,
                         s.plants, s.defuses, s.playerKills, s.playerDeaths, s.conflicts,
                         s.finalHash);
        }
    }
    std::fclose(summary);

    if (games == 0) {
        std::fprintf(stderr, "[dev_replay] 没有成功跑完任何一局\n");
        return 1;
    }
    std::printf("[dev_replay] batch games=%d failed=%d T=%d CT=%d draw=%d\n",
                games, failed, tWins, ctWins, games - tWins - ctWins);
    std::printf("[dev_replay] avg rounds=%.1f  avg round ticks=%.0f  plants/game=%.1f  "
                "defuses/game=%.1f\n",
                static_cast<double>(sumRounds) / games,
                sumAvgRoundTicks / games,
                static_cast<double>(sumPlants) / games,
                static_cast<double>(sumDefuses) / games);
    std::printf("[dev_replay] summary → %s\n", summaryPath.c_str());
    return failed == 0 ? 0 : 1;
}
