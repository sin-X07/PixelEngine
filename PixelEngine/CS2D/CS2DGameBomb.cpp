#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include <cmath>
#include <iostream>

// ============================================================================
// C4 物品逻辑（与「下包/拆包」分开，这里只负责：手持、丢弃、掉落、拾取、CT 情报）
// ============================================================================

void CS2DGame::ToggleHoldC4() {
    // 按 5 等价于切到 C4 槽；已经在 C4 槽则切回副武器
    if (!PlayerHasC4()) {
        playerHoldingC4_ = false;
        return;
    }
    if (player_.weaponSlot == 4) {
        player_.weapons[4] = WeaponId::None;
        playerHoldingC4_ = false;
        SwitchWeaponSlot(1);
    } else {
        SwitchWeaponSlot(4);
    }
}

void CS2DGame::UpdateBombItem(float dt) {
    if (mode_ != MatchMode::BombDefusal || bombPlanted_) {
        bombIntel_ = false;
        return;
    }

    if (bombDropDelay_ > 0.0f) bombDropDelay_ -= dt;

    // ---- 掉落在地上的 C4：最近的 T 靠近即拾取（丢下 1 秒内不捡）----
    if (bombDropped_ && bombDropDelay_ <= 0.0f) {
        Player* best = nullptr;
        float bestD2 = 24.0f * 24.0f;

        auto consider = [&](Player& p) {
            if (!p.alive || p.team != Team::T) return;
            const float dx = p.x - bombDropX_;
            const float dy = p.y - bombDropY_;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = &p; }
        };
        consider(player_);
        for (auto& b : bots_) consider(b);

        if (best) {
            bombCarrier_ = (best == &player_) ? -1 : static_cast<int>(best - bots_.data());
            bombDropped_ = false;
            if (best == &player_) player_.weapons[4] = WeaponId::C4;
            if (best == &player_) {
                std::cout << "[C4] 玩家拾取了掉落的 C4" << std::endl;
            } else {
                std::cout << "[C4] " << TeamName(Team::T) << " BOT 拾取了掉落的 C4" << std::endl;
            }
        }
    }

    // ---- CT 情报：任何活着的 CT 看到掉落 C4 → 记 8 秒 ----
    if (bombIntel_) {
        bombIntelAge_ -= dt;
        if (bombIntelAge_ <= 0.0f) bombIntel_ = false;
    }
    if (bombDropped_ && renderer_) {
        auto ctSees = [&](const Player& p) {
            if (!p.alive || p.team != Team::CT) return false;
            if (SegmentBlockedBySmoke(p.x, p.y, bombDropX_, bombDropY_)) return false;
            // 玩家用动态屏幕视野（可到屏幕边缘）；BOT 仍用自身固定视野。
            if (&p == &player_) {
                return renderer_->IsVisibleToPlayer(p.x, p.y, p.aimAngle,
                                                    bombDropX_, bombDropY_);
            }
            const float dx = bombDropX_ - p.x;
            const float dy = bombDropY_ - p.y;
            if (dx * dx + dy * dy > BOT_VIEW_RADIUS * BOT_VIEW_RADIUS) return false;
            if (!InViewCone(p.x, p.y, p.aimAngle, bombDropX_, bombDropY_, BOT_VIEW_RADIUS))
                return false;
            return renderer_->HasLineOfSight(p.x, p.y, bombDropX_, bombDropY_);
        };
        if (ctSees(player_)) {
            bombIntel_ = true; bombIntelX_ = bombDropX_; bombIntelY_ = bombDropY_;
            bombIntelAge_ = 8.0f;
        } else {
            for (const auto& b : bots_) {
                if (ctSees(b)) {
                    bombIntel_ = true; bombIntelX_ = bombDropX_; bombIntelY_ = bombDropY_;
                    bombIntelAge_ = 8.0f;
                    break;
                }
            }
        }
    }
}

void CS2DGame::DropC4(float x, float y) {
    if (mode_ != MatchMode::BombDefusal || bombPlanted_) return;
    if (bombCarrier_ == -2) return;

    bombDropped_ = true;
    bombDropDelay_ = 1.0f;
    bombDropX_ = x;
    bombDropY_ = y;
    bombCarrier_ = -2;
    playerHoldingC4_ = false;
    player_.weapons[4] = WeaponId::None;
    if (player_.weaponSlot == 4) EquipFallbackWeapon();
    std::cout << "[C4] C4 掉落在 (" << static_cast<int>(x) << ","
              << static_cast<int>(y) << ")" << std::endl;
}

// 当前槽位不可用（C4 丢了 / 枪丢了）时，切到有武器的槽位并读它自己的弹药
void CS2DGame::EquipFallbackWeapon() {
    if (player_.weapons[player_.weaponSlot] != WeaponId::None &&
        player_.weaponSlot != 4) {
        player_.LoadAmmoFromSlot();
        return;
    }
    int target = 2;   // 近战永远有
    if (player_.weapons[0] != WeaponId::None)      target = 0;
    else if (player_.weapons[1] != WeaponId::None) target = 1;
    player_.weaponSlot = target;
    player_.reloading = false;
    player_.reloadTimer = 0.0f;
    playerHoldingC4_ = false;
    player_.LoadAmmoFromSlot();
}
