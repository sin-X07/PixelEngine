#pragma once
// ============================================================================
// ScriptedPlayer —— 无头基准用的"脚本玩家"（第 10 个单位）
//
// 目的：让 5v5 在人数与信息上对称——不把玩家藏起来做 4v5，也不用特判旁路
//       让 BOT 逻辑失真。脚本玩家只通过 CS2DGame 的公开输入接口驱动：
//       每 tick 产出 (mouseDown, aimX, aimY, W/A/S/D, shift, R)，与真人输入等价。
//
// 策略（固定、可复现，属于基准的一部分）：
//   1. 有视线内敌人 → 瞄准它；进入反应延迟后开火；按距离拉近/拉开/侧移。
//   2. No enemy: follow a mate (P2-5: the mate in combat / most advanced, re-evaluated every 5s).
//   3. 弹药不足 → 按 R 换弹；购买期按固定顺序补装备（主武器 → 护甲）。
//   4. 不使用任何随机数（不污染游戏 RNG）。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"

#include <cmath>

namespace dev_replay {

class ScriptedPlayer {
public:
    // 脚本策略参数（属于基准的一部分：改动策略等于改基准，需记录在 baseline 说明里）
    struct Params {
        float turnRate = 1.5f;      // 转身速率 rad/s，≈86°/s
        float aimErrorDeg = 5.0f;   // 瞄准误差（±度），避免"像素级锁头"
        int   burstOn = 12;         // 连发窗口（tick），与 burstOff 组成点射节奏
        int   burstOff = 24;
        int   reactionTicks = 15;   // 看见目标后多少 tick 才开火（0.25s @60Hz）
        float engageRange = 600.0f; // 开火距离上限
    };

    ScriptedPlayer() = default;
    explicit ScriptedPlayer(const Params& p) : params_(p) {}

    // T20 验收场景：脚本玩家（CT）每回合购买拆包钳。默认关闭，保持基准可比。
    void SetBuyKit(bool on) { buyKit_ = on; }

    // P2-5: contrast strategies for playtest A/B. Default Follow keeps the
    // baseline behaviour (same mate pick rule, same ranges) -- changing the
    // strategy changes the benchmark, so record it in the summary header.
    enum class Strategy { Follow, Hold, Rush, Stealth };
    void SetStrategy(Strategy s) { strategy_ = s; }
    Strategy GetStrategy() const { return strategy_; }

    struct Inputs {
        bool  mouseDown = false;
        float aimX = 0.0f;
        float aimY = 0.0f;
        bool  w = false, a = false, s = false, d = false;
        bool  shift = false;
        bool  reload = false;
        // 本 tick 的"动作"：购买。同样是输入的一部分，必须进日志才能复现。
        int   buyWeapon = 0;      // WeaponId 数值，0 = 不购买
        int   buyArmorTier = 0;   // 1/2，0 = 不购买
        bool  buyKit = false;     // 购买拆包钳（CT）
    };

    // 每 tick 调用一次：读取只读状态 → 产出本帧输入
    Inputs Think(CS2DGame& game, float dt) {
        Inputs in{};
        const CS2DGame::Player& me = game.GetPlayer();

        ++tick_;
        // P2-5: effective engage range depends on the contrast strategy.
        const float engageRange = (strategy_ == Strategy::Rush)    ? 800.0f
                                : (strategy_ == Strategy::Stealth) ? 300.0f
                                                                   : params_.engageRange;
        BuyIfNeeded(game, in);

        // 死亡/观战：不产生输入（等回合重生）
        if (!me.alive) {
            lastTarget_ = nullptr;
            seenTicks_ = 0;
            return in;
        }

        const CS2DGame::Player* target = PickTarget(game, engageRange);
        if (target) {
            if (target != lastTarget_) { seenTicks_ = 0; lastTarget_ = target; }
            ++seenTicks_;

            // 转身速率限制：不是瞬时锁头，而是按 340°/s 转向目标（可复现的"人手感"）
            // 确定性瞄准误差：低频摆动，模拟人手微抖（不用随机数，保证可复现）
            const float err = params_.aimErrorDeg * 0.0174533f *
                              std::sin(static_cast<float>(tick_) * 0.13f);
            const float wantAngle = std::atan2(target->y - me.y, target->x - me.x) + err;
            TurnToward(wantAngle, dt);
            in.aimX = me.x + std::cos(virtualAim_) * 400.0f;
            in.aimY = me.y + std::sin(virtualAim_) * 400.0f;

            // 反应延迟 + 对准：看见 15 tick 且方向误差 < 3° 才开火
            float diff = wantAngle - virtualAim_;
            while (diff > 3.14159265f)  diff -= 6.28318531f;
            while (diff < -3.14159265f) diff += 6.28318531f;
            const bool aimed = std::abs(diff) < 0.05f;
            const float tdist = std::sqrt((target->x - me.x) * (target->x - me.x) +
                                         (target->y - me.y) * (target->y - me.y));
            const bool ready = seenTicks_ >= params_.reactionTicks &&
                               tdist <= engageRange;
            const int period = params_.burstOn + params_.burstOff;
            const bool inBurst = period > 0 && (tick_ % period) < params_.burstOn;
            in.mouseDown = ready && aimed && inBurst && me.ammo > 0 && !me.reloading;

            // 距离控制：太远拉近、太近拉开、中间侧移
            const float dx = target->x - me.x;
            const float dy = target->y - me.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 640.0f)      MoveToward(in, dx, dy);
            else if (dist < 220.0f) MoveToward(in, -dx, -dy);
            else                    Strafe(in, dx, dy);

            // 低弹换弹
            if (!me.reloading && me.ammo <= 3 && me.reserve > 0) in.reload = true;
            return in;
        }

        // No target: behaviour depends on the contrast strategy (P2-5).
        lastTarget_ = nullptr;
        seenTicks_ = 0;
        if (strategy_ == Strategy::Hold) {
            // Anchor at the nearest bomb site; stay within ~120px of its centre.
            float cx = 0.0f, cy = 0.0f;
            if (HoldSite(game, cx, cy)) {
                const float dx = cx - me.x;
                const float dy = cy - me.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 120.0f) MoveToward(in, dx, dy);
                in.aimX = me.x + std::cos(me.aimAngle) * 200.0f;
                in.aimY = me.y + std::sin(me.aimAngle) * 200.0f;
            }
        } else if (strategy_ == Strategy::Rush) {
            // Push toward the nearest hostile even without line of sight.
            const CS2DGame::Player* foe = NearestHostile(game);
            if (foe) {
                MoveToward(in, foe->x - me.x, foe->y - me.y);
                in.aimX = foe->x;
                in.aimY = foe->y;
            }
        } else {
            // Follow (default) / Stealth: follow the mate in combat or most advanced.
            const CS2DGame::Player* mate = PickFollowMate(game);
            if (mate) {
                const float dx = mate->x - me.x;
                const float dy = mate->y - me.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 160.0f) MoveToward(in, dx, dy);
                in.aimX = mate->x;
                in.aimY = mate->y;
            } else {
                in.aimX = me.x + std::cos(me.aimAngle) * 200.0f;
                in.aimY = me.y + std::sin(me.aimAngle) * 200.0f;
            }
        }
        if (strategy_ == Strategy::Stealth) in.shift = true;   // walk, never run
        if (!me.reloading && me.ammo < me.maxAmmo && me.reserve > 0) in.reload = true;
        return in;
    }

private:
    static constexpr int kReactionTicks = 15;   // 0.25s @60Hz

    // ---- 目标：视线内最近的敌人（不要求落在朝向扇区内，避免"背对就不会转身"）----
    const CS2DGame::Player* PickTarget(CS2DGame& game, float range) {
        const CS2DGame::Player& me = game.GetPlayer();
        const CS2DGame::Player* best = nullptr;
        float bestD2 = range * range;
        for (const auto& b : game.GetActors()) {
            if (&b == &me || !b.IsActive() || !b.alive || !game.IsHostile(me, b)) continue;
            const float dx = b.x - me.x;
            const float dy = b.y - me.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= bestD2) continue;
            if (!game.Visibility().HasLineOfSight(me.x, me.y, b.x, b.y)) continue;
            bestD2 = d2;
            best = &b;
        }
        return best;
    }

    // P2-5: follow the mate who is in combat or most advanced, not the nearest one.
    const CS2DGame::Player* PickFollowMate(CS2DGame& game) {
        const CS2DGame::Player& me = game.GetPlayer();
        if (followMate_ && followMate_->alive && tick_ - followEvalTick_ < 300) return followMate_;
        followEvalTick_ = tick_;
        const CS2DGame::Player* best = nullptr;
        double bestScore = 1e18;
        for (const auto& b : game.GetActors()) {
            if (&b == &me || !b.IsActive() || !b.alive || game.IsHostile(me, b)) continue;
            double nearestFoeD2 = 1e18;
            bool inCombat = false;
            for (const auto& e : game.GetActors()) {
                if (&e == &b || !e.IsActive() || !e.alive || !game.IsHostile(b, e)) continue;
                const double dx = static_cast<double>(e.x - b.x);
                const double dy = static_cast<double>(e.y - b.y);
                const double d2 = dx * dx + dy * dy;
                if (d2 < nearestFoeD2) nearestFoeD2 = d2;
                if (d2 <= 600.0 * 600.0 &&
                    game.Visibility().HasLineOfSight(b.x, b.y, e.x, e.y)) inCombat = true;
            }
            const double dMe = std::sqrt(static_cast<double>(b.x - me.x) * (b.x - me.x) +
                                         static_cast<double>(b.y - me.y) * (b.y - me.y));
            const double score = (inCombat ? nearestFoeD2 : 1e12 + nearestFoeD2) + dMe * 0.001;
            if (score < bestScore) { bestScore = score; best = &b; }
        }
        followMate_ = best;
        return best;
    }

    // Nearest hostile regardless of line of sight (Rush strategy pushes blind).
    const CS2DGame::Player* NearestHostile(CS2DGame& game) {
        const CS2DGame::Player& me = game.GetPlayer();
        const CS2DGame::Player* best = nullptr;
        float bestD2 = 1e18f;
        for (const auto& b : game.GetActors()) {
            if (&b == &me || !b.IsActive() || !b.alive || !game.IsHostile(me, b)) continue;
            const float dx = b.x - me.x;
            const float dy = b.y - me.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = &b; }
        }
        return best;
    }

    // Nearest bomb-site centre (Hold strategy anchors there).
    bool HoldSite(CS2DGame& game, float& cx, float& cy) {
        const CS2DGame::Player& me = game.GetPlayer();
        float bestD2 = 1e18f;
        bool found = false;
        for (int i = 0; i < 2; ++i) {
            float sx = 0.0f, sy = 0.0f;
            if (!game.GetBombSiteCenter(i, sx, sy)) continue;
            const float dx = sx - me.x;
            const float dy = sy - me.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; cx = sx; cy = sy; found = true; }
        }
        return found;
    }

    // 转身速率限制（340°/s），无随机数
    void TurnToward(float wantAngle, float dt) {
        const float kMaxTurn = params_.turnRate;
        float diff = wantAngle - virtualAim_;
        while (diff > 3.14159265f)  diff -= 6.28318531f;
        while (diff < -3.14159265f) diff += 6.28318531f;
        const float step = kMaxTurn * dt;
        if (diff >  step) diff =  step;
        if (diff < -step) diff = -step;
        virtualAim_ += diff;
        while (virtualAim_ > 3.14159265f)  virtualAim_ -= 6.28318531f;
        while (virtualAim_ < -3.14159265f) virtualAim_ += 6.28318531f;
    }

    static void MoveToward(Inputs& in, float dx, float dy) {
        in.d = dx > 16.0f;
        in.a = dx < -16.0f;
        in.s = dy > 16.0f;
        in.w = dy < -16.0f;
    }

    void Strafe(Inputs& in, float dx, float dy) {
        // 垂直于目标方向侧移，每 0.8s 换向（纯计时，不用随机数）
        strafeTimer_ += 1.0f / 60.0f;
        if (strafeTimer_ > 0.8f) { strafeTimer_ = 0.0f; strafeDir_ = -strafeDir_; }
        const float px = -dy * static_cast<float>(strafeDir_);
        const float py = dx * static_cast<float>(strafeDir_);
        MoveToward(in, px, py);
    }

    // ---- 购买决策：只产出"想买什么"，由调用方执行并记入输入日志 ----
    // 购买属于"动作"，不是移动输入；不记进日志的话回放会在第一个购买期就分叉。
    void BuyIfNeeded(CS2DGame& game, Inputs& in) {
        if (!game.IsBuyPhase()) return;
        const CS2DGame::Player& me = game.GetPlayer();
        if (!me.alive) return;

        const Team team = game.GetPlayerTeam();
        const int money = game.GetPlayerMoney();

        if (me.weapons[0] == WeaponId::None) {
            const WeaponId want = (team == Team::CT) ? WeaponId::M4A4 : WeaponId::AK47;
            const WeaponId fallback = (team == Team::CT) ? WeaponId::MP9 : WeaponId::GalilAR;
            if (money >= WeaponDB::Get(want).price) in.buyWeapon = static_cast<int>(want);
            else if (money >= WeaponDB::Get(fallback).price) in.buyWeapon = static_cast<int>(fallback);
            return;
        }
        if (me.armor < 50 && money >= 650) in.buyArmorTier = 1;
        // CT 购买拆包钳（T20 验收场景：runner --buy-kit 打开，默认关闭以保持基准可比）。
        if (buyKit_ && team == Team::CT && !me.hasDefuseKit && money >= 400) in.buyKit = true;
    }
    static constexpr float kEngageRange = 520.0f;   // 视野内可交战距离上限

    bool buyKit_ = false;

    const CS2DGame::Player* lastTarget_ = nullptr;
    int   seenTicks_ = 0;
    Strategy strategy_ = Strategy::Follow;   // P2-5: contrast strategy (default = baseline)
    const CS2DGame::Player* followMate_ = nullptr;  // P2-5: cached follow target
    int followEvalTick_ = 0;                 // P2-5: last follow re-evaluation tick
    Params params_;
    int   strafeDir_ = 1;
    float virtualAim_ = 0.0f;   // 脚本玩家的"视线方向"（受转身速率限制）
    int   tick_ = 0;            // 内部计时（确定性抖动用）
    float strafeTimer_ = 0.0f;
};

}  // namespace dev_replay
