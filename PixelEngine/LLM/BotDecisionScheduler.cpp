#include "BotDecisionScheduler.hpp"
#include "../CS2D/CS2DGame.hpp"
#include "../Core/PathUtils.hpp"
#include <iostream>
#include <cmath>
#include <cstdio>
#include <algorithm>

// ============================================================================
// 生命周期
// ============================================================================

bool BotDecisionScheduler::Initialize(const std::string& iniPath) {
    LLMConfig cfg = LLMConfig::FromFile(iniPath);

    // 相对路径 → 绝对路径（CreateProcess 需要；基准是 exe 所在目录）
    if (!cfg.serverExePath.empty()) {
        const std::string resolved = PathUtils::ResolveAppPath(cfg.serverExePath);
        if (!resolved.empty()) cfg.serverExePath = resolved;
    }
    if (!cfg.modelPath.empty()) {
        const std::string resolved = PathUtils::ResolveAppPath(cfg.modelPath);
        if (!resolved.empty()) cfg.modelPath = resolved;
    }

    controller_ = std::make_unique<LLMBotController>();
    const bool ok = controller_->Initialize(cfg);

    if (ok) {
        std::cout << "[LLM] 就绪 (server: " << cfg.serverUrl
                  << (cfg.autoLaunch ? ", 自动拉起" : "") << ")" << std::endl;
    } else {
        std::cout << "[LLM] 不可用, BOT 使用传统行为树" << std::endl;
    }
    return ok;
}

void BotDecisionScheduler::Shutdown() {
    if (controller_) {
        controller_->Shutdown();
        controller_.reset();
    }
}

bool BotDecisionScheduler::IsReady() const {
    return controller_ && controller_->IsReady();
}

// ============================================================================
// 调度
// ============================================================================

void BotDecisionScheduler::Tick(float dt, CS2DGame& game) {
    if (!IsReady()) return;

    cooldownSec_ -= dt;
    if (cooldownSec_ > 0.0f) return;

    cooldownSec_ = static_cast<float>(intervalMs_) / 1000.0f;
    DispatchOne(game);
}

// 为一个存活 BOT 采集状态并发出异步请求
void BotDecisionScheduler::DispatchOne(CS2DGame& game) {
    auto& bots = game.GetBOTs();
    const auto& playerData = game.GetPlayer();
    if (bots.empty()) return;

    // ---- 轮询：从当前索引起找下一个存活 BOT ----
    const int count = static_cast<int>(bots.size());
    const int start = roundRobinIdx_ % count;
    int foundIdx = -1;
    for (int attempt = 0; attempt < count; ++attempt) {
        const int idx = (start + attempt) % count;
        if (bots[idx].alive) {
            foundIdx = idx;
            roundRobinIdx_ = (idx + 1) % count;
            break;
        }
    }
    if (foundIdx < 0) return;   // 没有存活 BOT

    const auto& targetBot = bots[foundIdx];
    const int botIdx = foundIdx;

    // ---- 自身状态 ----
    const float hp     = static_cast<float>(targetBot.health);
    const float maxHp  = static_cast<float>(targetBot.maxHealth);
    const float armor  = static_cast<float>(targetBot.armor);

    const std::string weaponName = WeaponDB::Get(targetBot.CurrentWeapon().id).name;

    const int ammo    = targetBot.ammo;
    const int maxAmmo = targetBot.maxAmmo;
    const std::string area = "CS2D地图";

    // ---- 敌人：最近的异阵营存活单位（玩家或 BOT）----
    bool enemyVisible = false;
    float enemyDist   = 0.0f;
    std::string enemyDir = "前方";
    const bool enemyFiring = false;
    {
        float bestD2 = 1e18f;
        float ex = 0.0f, ey = 0.0f;
        bool found = false;

        auto consider = [&](const CS2DGame::Player& other) {
            if (!other.alive || other.team == targetBot.team) return;
            const float dx = other.x - targetBot.x;
            const float dy = other.y - targetBot.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) {
                bestD2 = d2;
                ex = other.x;
                ey = other.y;
                found = true;
            }
        };

        consider(playerData);
        for (const auto& b : bots) {
            if (&b == &targetBot) continue;
            consider(b);
        }

        if (found) {
            enemyDist = std::sqrt(bestD2);
            if (enemyDist < CS2DGame::BOT_VIEW_RADIUS) {
                enemyVisible = true;
                const float dx = ex - targetBot.x;
                const float dy = ey - targetBot.y;
                if (std::abs(dx) > std::abs(dy))
                    enemyDir = (dx > 0) ? "右前方" : "左前方";
                else
                    enemyDir = (dy > 0) ? "前方" : "后退";
            }
        }
    }

    // ---- 队友：最近的存活 BOT（排除自己）----
    bool  teammateAlive = false;
    float teammateDist  = 0.0f;
    std::string teammateDir = "后方";
    int   aliveCount = 0;
    float nearestMateX = targetBot.x;   // 无队友时退化为自身位置
    float nearestMateY = targetBot.y;

    for (const auto& b : bots) {
        if (&b == &targetBot || !b.alive) continue;
        if (b.team != targetBot.team) continue;   // 只统计同阵营队友
        ++aliveCount;

        const float dx = b.x - targetBot.x;
        const float dy = b.y - targetBot.y;
        const float d  = std::sqrt(dx * dx + dy * dy);
        if (d < CS2DGame::BOT_VIEW_RADIUS * 1.5f) {
            teammateAlive = true;
            teammateDist  = d;
            teammateDir   = (dx > 0) ? "右" : "左";
            teammateDir  += (dy > 0) ? "前方" : "后方";
            nearestMateX  = b.x;
            nearestMateY  = b.y;
        }
    }

    // ---- 组装请求 ----
    std::string userMsg = PromptBuilder::BuildUserMessage(
        hp, maxHp, armor, weaponName, ammo, maxAmmo,
        targetBot.x, targetBot.y, area,
        enemyVisible, enemyDist, enemyDir, enemyFiring,
        teammateAlive, teammateDist, teammateDir,
        targetBot.reloading);

    // ---- 战局上下文（阵营 / 回合 / 比分）----
    {
        const char* phaseName = "交战";
        switch (game.GetRoundPhase()) {
            case RoundPhase::Freeze:   phaseName = "冻结"; break;
            case RoundPhase::Buy:      phaseName = "购买"; break;
            case RoundPhase::Live:     phaseName = "交战"; break;
            case RoundPhase::RoundEnd: phaseName = "结算"; break;
            default:                   phaseName = "-";    break;
        }
        char ctx[384];
        std::snprintf(ctx, sizeof(ctx),
                      "\n【战局】你是%s阵营，第%d回合，阶段:%s，剩余%d秒，比分 T%d:%d CT，本阵营存活%d人，你的金钱$%d。",
                      TeamName(targetBot.team),
                      game.GetRoundNumber(),
                      phaseName,
                      static_cast<int>(game.GetRoundRemaining()),
                      game.GetScoreT(), game.GetScoreCT(),
                      game.AliveCount(targetBot.team),
                      targetBot.money);
        userMsg += ctx;

        // 目标提示
        char obj[192] = {0};
        switch (game.GetMatchMode()) {
            case MatchMode::BombDefusal:
                if (game.IsBombPlanted())
                    std::snprintf(obj, sizeof(obj),
                                  "\n【任务】C4 已安装，剩余%d秒。CT 需拆包，T 需守住。",
                                  static_cast<int>(game.GetBombTimer()));
                else
                    std::snprintf(obj, sizeof(obj), "\n【任务】T 需前往包点下包，CT 需阻止。");
                break;
            case MatchMode::HostageRescue:
                std::snprintf(obj, sizeof(obj), "\n【任务】CT 需营救人质(%d/%d)，T 需阻止。",
                              game.GetHostagesRescued(), game.GetHostagesTotal());
                break;
            default:
                break;
        }
        if (obj[0] != '\0') userMsg += obj;
    }

    // ---- 详细战术态势：把能拿到的状态尽量喂给 LLM ----
    {
        std::ostringstream oss;
        auto roleName = [](BotRole r) {
            return (r == BotRole::Entry) ? "突破" : (r == BotRole::Anchor ? "断后" : "支援");
        };

        const auto& orders = game.GetSquadOrders();
        BotRole myRole = BotRole::Support;
        int mySite = -1;
        if (botIdx >= 0 && botIdx < static_cast<int>(orders.size())) {
            myRole = orders[botIdx].role;
            mySite = orders[botIdx].site;
        }

        oss << "\n【战术态势】角色:" << roleName(myRole)
            << " 血量:" << targetBot.health << "/" << targetBot.maxHealth
            << " 护甲:" << targetBot.armor << (targetBot.helmet ? "(盔)" : "")
            << " 武器:" << weaponName
            << " 弹药:" << targetBot.ammo << "/" << targetBot.maxAmmo
            << " 金钱:$" << targetBot.money
            << " 换弹:" << (targetBot.reloading ? "是" : "否")
            << " 坐标:(" << static_cast<int>(targetBot.x) << ","
            << static_cast<int>(targetBot.y) << ")";

        // 队友
        oss << "\n【队友】存活" << game.AliveCount(targetBot.team) << "人";
        {
            int shown = 0;
            for (size_t i = 0; i < bots.size() && shown < 3; ++i) {
                if (static_cast<int>(i) == botIdx) continue;
                const auto& m = bots[i];
                if (!m.alive || m.team != targetBot.team) continue;
                const float dx = m.x - targetBot.x;
                const float dy = m.y - targetBot.y;
                const BotRole r = (i < orders.size()) ? orders[i].role : BotRole::Support;
                oss << " | BOT#" << i << "(" << roleName(r) << ") 距"
                    << static_cast<int>(std::sqrt(dx * dx + dy * dy))
                    << " " << ((dx > 0) ? "右" : "左") << ((dy > 0) ? "下" : "上");
                ++shown;
            }
        }

        // 敌情（团队情报）
        {
            const auto& contacts = game.GetContactsOfTeam(targetBot.team);
            oss << "\n【敌情】已知" << contacts.size() << "个";
            if (contacts.empty()) {
                oss << "（无情报，需要靠视野或队友寻找）";
            } else {
                int shown = 0;
                for (const auto& c : contacts) {
                    if (shown++ >= 3) break;
                    const float dx = c.x - targetBot.x;
                    const float dy = c.y - targetBot.y;
                    oss << " | 距" << static_cast<int>(std::sqrt(dx * dx + dy * dy))
                        << " " << ((dx > 0) ? "右" : "左") << ((dy > 0) ? "下" : "上")
                        << "(情报" << static_cast<int>(c.age) << "s前)";
                }
            }
        }

        // 目标点
        {
            const int siteCount = game.GetBombSiteCount();
            if (siteCount > 0) {
                oss << "\n【目标】";
                for (int i = 0; i < siteCount; ++i) {
                    float cx = 0.0f, cy = 0.0f;
                    if (game.GetBombSiteCenter(i, cx, cy)) {
                        oss << "包点" << static_cast<char>('A' + i)
                            << "(" << static_cast<int>(cx) << "," << static_cast<int>(cy) << ") ";
                    }
                }
                if (mySite >= 0) oss << "| 你负责:" << static_cast<char>('A' + mySite);
                else             oss << "| 你负责:中路";
            }
        }

        // 局势
        oss << "\n【局势】";
        if (game.GetLastDeathTimer() > 0.0f && game.GetLastDeathTeam() == targetBot.team) {
            oss << "队友" << static_cast<int>(CS2DGame::CONTACT_MEMORY > 0 ? 3.5f - game.GetLastDeathTimer() : 0)
                << "秒前阵亡于(" << static_cast<int>(game.GetLastDeathX()) << ","
                << static_cast<int>(game.GetLastDeathY()) << ") → 可考虑补枪 ";
        }
        if (game.AliveCount(targetBot.team) == 1) oss << "你是本队最后一人 ";
        if (game.AliveCount(targetBot.team) < game.AliveCount(targetBot.team == Team::T ? Team::CT : Team::T))
            oss << "人数劣势，谨慎接敌 ";

        userMsg += oss.str();
    }

    const std::string requestBody = PromptBuilder::BuildRequestBody(userMsg);

    // 地图尺寸（用于 target 字符串 → 世界坐标映射）
    const float mapW = game.GetWorld().GetMapPixelW();
    const float mapH = game.GetWorld().GetMapPixelH();

    // 回调需要的快照（异步执行时游戏状态可能已变，必须按值捕获）
    const float botX = targetBot.x, botY = targetBot.y;
    const float playerX = playerData.x, playerY = playerData.y;
    const bool  hasMate = teammateAlive;
    const float mateX = nearestMateX, mateY = nearestMateY;

    char tagBuf[32];
    std::snprintf(tagBuf, sizeof(tagBuf), "BOT#%d", botIdx);
    const std::string tag = tagBuf;

    // 回调只持 game 指针（Engine 生命周期长于调度器，安全）
    CS2DGame* gamePtr = &game;
    controller_->Decide(requestBody,
        [gamePtr, botIdx, botX, botY, playerX, playerY,
         mateX, mateY, hasMate, mapW, mapH](const LLMDecision& d) {
            if (!d.valid) return;
            auto& all = gamePtr->GetBOTs();
            if (botIdx < 0 || static_cast<size_t>(botIdx) >= all.size()) return;

            // LLMDecision → BotIntent（战略目标）
            gamePtr->AiAt(static_cast<size_t>(botIdx)).llmIntent.FromLLMDecision(
                d.action, d.target, d.shoot,
                botX, botY, playerX, playerY,
                mateX, mateY, hasMate,
                mapW, mapH);
        },
        tag);

    // 每次请求打印一行，便于观察轮询节奏
    std::cout << "[LLM] 请求 " << tag
              << " (" << TeamName(targetBot.team)
              << ", HP " << static_cast<int>(hp)
              << ", " << weaponName
              << ", 阵营存活 " << game.AliveCount(targetBot.team) << ")" << std::endl;
}
