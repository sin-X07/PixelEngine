#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include <cmath>
#include <iostream>

// ============================================================================
// C4 物品逻辑（与「下包/拆包」分开，这里只负责：手持、丢弃、掉落、拾取、CT 情报）
// ============================================================================

void CS2DGame::ToggleHoldC4() {
    if (!PlayerHasC4()) {
        playerHoldingC4_ = false;
        return;
    }
    playerHoldingC4_ = !playerHoldingC4_;
    if (playerHoldingC4_) {
        // 拿在手上时收起武器动作
        player_.reloading = false;
        std::cout << "[C4] 玩家拿起了 C4" << std::endl;
    } else {
        std::cout << "[C4] 玩家收起了 C4" << std::endl;
    }
}

void CS2DGame::UpdateBombItem(float dt) {
    if (mode_ != MatchMode::BombDefusal || bombPlanted_) {
        bombIntel_ = false;
        return;
    }

    // ---- 掉落在地上的 C4：最近的 T 靠近即拾取 ----
    if (bombDropped_) {
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
            const float dx = bombDropX_ - p.x;
            const float dy = bombDropY_ - p.y;
            if (dx * dx + dy * dy > Tuning::VisionRadius * Tuning::VisionRadius) return false;
            if (!InViewCone(p.x, p.y, p.aimAngle, bombDropX_, bombDropY_, Tuning::VisionRadius))
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
    bombDropX_ = x;
    bombDropY_ = y;
    bombCarrier_ = -2;
    playerHoldingC4_ = false;
    std::cout << "[C4] C4 掉落在 (" << static_cast<int>(x) << ","
              << static_cast<int>(y) << ")" << std::endl;
}
