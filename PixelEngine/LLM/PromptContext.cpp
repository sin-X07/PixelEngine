#include "PromptContext.hpp"
#include "../CS2D/CS2DGame.hpp"
#include <sstream>
#include <cmath>

namespace PromptContext {

// 把当前 BOT 能感知到的战术态势拼成一段文本，附在用户消息后面。
// 单独成文件是因为这段逻辑会持续变长（角色/队友/情报/目标/局势），
// 塞在决策调度里会把「发请求」和「组织状态」两件事混在一起。
std::string BuildTacticalBlock(const CS2DGame& game,
                               const CS2DGame::Player& targetBot,
                               const std::vector<CS2DGame::Player>& bots,
                               int botIdx) {
    const std::string weaponName = WeaponDB::Get(targetBot.CurrentWeapon().id).name;
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

        return oss.str();
    }

}

} // namespace PromptContext
