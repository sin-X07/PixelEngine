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
//   2. 没有敌人 → 跟随最近的存活队友（队友由 BOT 负责寻路），保持 80px 距离。
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
        float engageRange = 300.0f; // 开火距离上限
    };

    ScriptedPlayer() = default;
    explicit ScriptedPlayer(const Params& p) : params_(p) {}

    struct Inputs {
        bool  mouseDown = false;
        float aimX = 0.0f;
        float aimY = 0.0f;
        bool  w = false, a = false, s = false, d = false;
        bool  shift = false;
        bool  reload = false;
    };

    // 每 tick 调用一次：读取只读状态 → 产出本帧输入
    Inputs Think(CS2DGame& game, float dt) {
        Inputs in{};
        const CS2DGame::Player& me = game.GetPlayer();

        ++tick_;
        BuyIfNeeded(game);

        // 死亡/观战：不产生输入（等回合重生）
        if (!me.alive) {
            lastTarget_ = nullptr;
            seenTicks_ = 0;
            return in;
        }

        const CS2DGame::Player* target = PickTarget(game);
        if (target) {
            if (target != lastTarget_) { seenTicks_ = 0; lastTarget_ = target; }
            ++seenTicks_;

            // 转身速率限制：不是瞬时锁头，而是按 340°/s 转向目标（可复现的"人手感"）
            // 确定性瞄准误差：低频摆动，模拟人手微抖（不用随机数，保证可复现）
            const float err = params_.aimErrorDeg * 0.0174533f *
                              std::sin(static_cast<float>(tick_) * 0.13f);
            const float wantAngle = std::atan2(target->y - me.y, target->x - me.x) + err;
            TurnToward(wantAngle, dt);
            in.aimX = me.x + std::cos(virtualAim_) * 200.0f;
            in.aimY = me.y + std::sin(virtualAim_) * 200.0f;

            // 反应延迟 + 对准：看见 15 tick 且方向误差 < 3° 才开火
            float diff = wantAngle - virtualAim_;
            while (diff > 3.14159265f)  diff -= 6.28318531f;
            while (diff < -3.14159265f) diff += 6.28318531f;
            const bool aimed = std::abs(diff) < 0.05f;
            const float tdist = std::sqrt((target->x - me.x) * (target->x - me.x) +
                                         (target->y - me.y) * (target->y - me.y));
            const bool ready = seenTicks_ >= params_.reactionTicks &&
                               tdist <= params_.engageRange;
            const int period = params_.burstOn + params_.burstOff;
            const bool inBurst = period > 0 && (tick_ % period) < params_.burstOn;
            in.mouseDown = ready && aimed && inBurst && me.ammo > 0 && !me.reloading;

            // 距离控制：太远拉近、太近拉开、中间侧移
            const float dx = target->x - me.x;
            const float dy = target->y - me.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 320.0f)      MoveToward(in, dx, dy);
            else if (dist < 110.0f) MoveToward(in, -dx, -dy);
            else                    Strafe(in, dx, dy);

            // 低弹换弹
            if (!me.reloading && me.ammo <= 3 && me.reserve > 0) in.reload = true;
            return in;
        }

        // 没有目标：跟随最近的存活队友（让脚本玩家也会推进/回防）
        lastTarget_ = nullptr;
        seenTicks_ = 0;
        const CS2DGame::Player* mate = NearestMate(game);
        if (mate) {
            const float dx = mate->x - me.x;
            const float dy = mate->y - me.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 80.0f) MoveToward(in, dx, dy);
            in.aimX = mate->x;
            in.aimY = mate->y;
        } else {
            in.aimX = me.x + std::cos(me.aimAngle) * 100.0f;
            in.aimY = me.y + std::sin(me.aimAngle) * 100.0f;
        }
        if (!me.reloading && me.ammo < me.maxAmmo && me.reserve > 0) in.reload = true;
        return in;
    }

private:
    static constexpr int kReactionTicks = 15;   // 0.25s @60Hz

    // ---- 目标：视线内最近的敌人（不要求落在朝向扇区内，避免"背对就不会转身"）----
    const CS2DGame::Player* PickTarget(CS2DGame& game) {
        const CS2DGame::Player& me = game.GetPlayer();
        const CS2DGame::Player* best = nullptr;
        float bestD2 = params_.engageRange * params_.engageRange;
        for (const auto& b : game.GetBOTs()) {
            if (!b.alive || !game.IsHostile(me, b)) continue;
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

    const CS2DGame::Player* NearestMate(CS2DGame& game) {
        const CS2DGame::Player& me = game.GetPlayer();
        const CS2DGame::Player* best = nullptr;
        float bestD2 = 1e18f;
        for (const auto& b : game.GetBOTs()) {
            if (!b.alive || game.IsHostile(me, b)) continue;
            const float dx = b.x - me.x;
            const float dy = b.y - me.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = &b; }
        }
        return best;
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
        in.d = dx > 8.0f;
        in.a = dx < -8.0f;
        in.s = dy > 8.0f;
        in.w = dy < -8.0f;
    }

    void Strafe(Inputs& in, float dx, float dy) {
        // 垂直于目标方向侧移，每 0.8s 换向（纯计时，不用随机数）
        strafeTimer_ += 1.0f / 60.0f;
        if (strafeTimer_ > 0.8f) { strafeTimer_ = 0.0f; strafeDir_ = -strafeDir_; }
        const float px = -dy * static_cast<float>(strafeDir_);
        const float py = dx * static_cast<float>(strafeDir_);
        MoveToward(in, px, py);
    }

    // ---- 购买：固定顺序，购买期内每 tick 尝试一次（失败无副作用）----
    void BuyIfNeeded(CS2DGame& game) {
        if (!game.IsBuyPhase()) return;
        const CS2DGame::Player& me = game.GetPlayer();
        if (!me.alive) return;

        const Team team = game.GetPlayerTeam();
        const int money = game.GetPlayerMoney();

        if (me.weapons[0] == WeaponId::None) {
            // 主武器：优先步枪，买不起就冲锋枪
            const WeaponId want = (team == Team::CT) ? WeaponId::M4A4 : WeaponId::AK47;
            const WeaponId fallback = (team == Team::CT) ? WeaponId::MP9 : WeaponId::GalilAR;
            Presentation::BuyRequest r;
            r.kind = Presentation::BuyKind::Weapon;
            r.weapon = want;
            if (game.ExecutePurchase(r, nullptr).code != Presentation::BuyCode::Ok) {
                r.weapon = fallback;
                game.ExecutePurchase(r, nullptr);
            }
            return;
        }
        if (me.armor < 50 && money >= 650) {
            Presentation::BuyRequest r;
            r.kind = Presentation::BuyKind::Armor;
            r.armorTier = 1;
            game.ExecutePurchase(r, nullptr);
        }
    }

    static constexpr float kEngageRange = 520.0f;   // 视野内可交战距离上限

    const CS2DGame::Player* lastTarget_ = nullptr;
    int   seenTicks_ = 0;
    Params params_;
    int   strafeDir_ = 1;
    float virtualAim_ = 0.0f;   // 脚本玩家的"视线方向"（受转身速率限制）
    int   tick_ = 0;            // 内部计时（确定性抖动用）
    float strafeTimer_ = 0.0f;
};

}  // namespace dev_replay
