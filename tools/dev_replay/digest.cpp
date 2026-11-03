#include "digest.hpp"

namespace dev_replay {
namespace {

// 玩家：只哈希影响仿真的字段（表现层 animState/animTimer/deathTimer/spriteKey 排除）
void MixPlayer(TickHasher& h, const CS2DGame::Player& p) {
    h.Mix(static_cast<int>(p.team));
    h.MixFloat(p.x);
    h.MixFloat(p.y);
    h.MixFloat(p.vx);
    h.MixFloat(p.vy);
    h.MixFloat(p.aimAngle);
    h.Mix(p.health);
    h.Mix(static_cast<uint8_t>(p.alive ? 1 : 0));
    for (int i = 0; i < 5; ++i) h.Mix(static_cast<int>(p.weapons[i]));
    h.Mix(p.weaponSlot);
    h.Mix(p.ammo);
    h.Mix(p.maxAmmo);
    h.Mix(p.reserve);
    h.Mix(p.armor);
    h.Mix(static_cast<uint8_t>(p.helmet ? 1 : 0));
    h.Mix(p.grenadeCount);
    for (int i = 0; i < CS2DGame::Player::MAX_GRENADES; ++i) {
        h.Mix(static_cast<int>(p.grenades[i]));
    }
    h.Mix(p.kills);
    h.Mix(p.deaths);
    h.Mix(static_cast<uint8_t>(p.reloading ? 1 : 0));
    h.MixFloat(p.reloadTimer);
    h.MixFloat(p.shootCooldown);
}

// BOT 决策运行时：驱动朝向与移动，属于 gameplay
void MixBotRuntime(TickHasher& h, const CS2DGame::BotRuntime& ai) {
    h.Mix(static_cast<int>(ai.stance));
    h.Mix(static_cast<int>(ai.maneuver));
    h.MixFloat(ai.goalX);
    h.MixFloat(ai.goalY);
    h.Mix(ai.pathIdx);
    h.MixFloat(ai.retreatTimer);
    h.MixFloat(ai.threatX);
    h.MixFloat(ai.threatY);
    h.MixFloat(ai.lastHitTimer);
    h.MixFloat(ai.defuseCooldown);
    h.MixFloat(ai.blindTimer);
    h.Mix(static_cast<uint8_t>(ai.defusing ? 1 : 0));
    h.MixFloat(ai.alertTimer);
    h.MixFloat(ai.alertX);
    h.MixFloat(ai.alertY);
    h.Mix(static_cast<uint8_t>(ai.hadTarget ? 1 : 0));
    h.MixFloat(ai.scanPhase);
    h.MixFloat(ai.scanBase);
    h.MixFloat(ai.grenadeCooldown);
    h.MixFloat(ai.wanderX);
    h.MixFloat(ai.wanderY);
    h.MixFloat(ai.wanderTimer);
    h.Mix(static_cast<uint8_t>(ai.hasPlanHold ? 1 : 0));
    h.MixFloat(ai.planHoldX);
    h.MixFloat(ai.planHoldY);
    h.Mix(static_cast<uint32_t>(ai.heard.size()));   // 只哈希长度，避免变长膨胀
}

}  // namespace

uint64_t SnapshotTick(const CS2DGame& game, uint64_t tick) {
    TickHasher h;

    // ---- 时间 / 回合 ----
    h.Mix(tick);
    h.Mix(game.GetRoundNumber());
    h.Mix(static_cast<int>(game.GetRoundPhase()));
    h.MixFloat(game.GetRoundRemaining());
    h.Mix(game.GetScoreT());
    h.Mix(game.GetScoreCT());
    h.MixFloat(game.GetMatchOverTimer());

    // ---- 炸弹 ----
    h.MixFloat(game.GetBombTimer());
    h.MixFloat(game.GetBombX());
    h.MixFloat(game.GetBombY());
    h.MixFloat(game.GetDefuseProgress());
    h.MixFloat(game.GetPlantProgress());

    // ---- 玩家 ----
    MixPlayer(h, game.GetPlayer());

    // ---- BOT：玩家/运行时按 bots_ 顺序一一对应 ----
    const auto& bots = game.GetBOTs();
    h.Mix(static_cast<uint32_t>(bots.size()));
    for (size_t i = 0; i < bots.size(); ++i) {
        MixPlayer(h, bots[i]);
        MixBotRuntime(h, game.AiAt(i));
    }

    // ---- 子弹（只哈希存活段）----
    uint32_t bulletCount = 0;
    for (const auto& b : game.GetBullets()) if (b.active) ++bulletCount;
    h.Mix(bulletCount);
    for (const auto& b : game.GetBullets()) {
        if (!b.active) continue;
        h.MixFloat(b.x);
        h.MixFloat(b.y);
        h.MixFloat(b.vx);
        h.MixFloat(b.vy);
        h.MixFloat(b.traveled);
        h.Mix(b.damage);
        h.Mix(b.ownerIdx);
        h.Mix(static_cast<int>(b.ownerTeam));
        h.Mix(static_cast<int>(b.weapon));
    }

    // ---- 投掷物 ----
    h.Mix(static_cast<uint32_t>(game.GetGrenades().size()));
    for (const auto& g : game.GetGrenades()) {
        h.MixFloat(g.x);
        h.MixFloat(g.y);
        h.MixFloat(g.vx);
        h.MixFloat(g.vy);
        h.MixFloat(g.fuse);
        h.Mix(static_cast<int>(g.type));
        h.Mix(static_cast<int>(g.team));
        h.Mix(static_cast<uint8_t>(g.active ? 1 : 0));
    }

    // ---- 烟 / 火（seed 属渲染随机，排除）----
    h.Mix(static_cast<uint32_t>(game.GetSmokes().size()));
    for (const auto& s : game.GetSmokes()) {
        h.MixFloat(s.x);
        h.MixFloat(s.y);
        h.MixFloat(s.radius);
        h.MixFloat(s.life);
        h.MixFloat(s.age);
    }
    h.Mix(static_cast<uint32_t>(game.GetFires().size()));
    for (const auto& f : game.GetFires()) {
        h.MixFloat(f.x);
        h.MixFloat(f.y);
        h.MixFloat(f.radius);
        h.MixFloat(f.life);
        h.MixFloat(f.age);
    }

    // ---- 小队指令 ----
    const auto& squad = game.GetSquadOrders();
    h.Mix(static_cast<uint32_t>(squad.size()));
    for (const auto& o : squad) {
        h.Mix(static_cast<int>(o.role));
        h.Mix(o.focus);
        h.Mix(o.site);
        h.Mix(static_cast<uint8_t>(o.hasHold ? 1 : 0));
        h.MixFloat(o.holdX);
        h.MixFloat(o.holdY);
        h.Mix(static_cast<uint8_t>(o.canTrade ? 1 : 0));
        h.Mix(static_cast<uint8_t>(o.canRotate ? 1 : 0));
    }

    // ---- 教练计划（reazon 是自然语言，排除；expiresAt 是 clockSec_ 派生，纳入）----
    for (Team t : {Team::T, Team::CT}) {
        const TeamPlan& plan = game.GetCoachPlan(t);
        h.Mix(static_cast<int>(plan.intent));
        h.MixString(plan.target);
        h.Mix(static_cast<int>(plan.tempo));
        h.Mix(plan.gen);
        h.Mix(static_cast<uint8_t>(plan.valid ? 1 : 0));
        h.MixFloat(plan.expiresAt);
        for (const auto& slot : plan.slots) {
            h.Mix(static_cast<int>(slot.role));
            h.MixString(slot.area);
            h.Mix(static_cast<uint8_t>(slot.canTrade ? 1 : 0));
        }
    }

    return h.Value();
}

}  // namespace dev_replay
