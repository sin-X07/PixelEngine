#include "CS2DSimulation.hpp"
#include "Tuning.hpp"
#include <cmath>
#include <iostream>

// ============================================================================
// C4 物品逻辑（与「下包/拆包」分开，这里只负责：手持、丢弃、掉落、拾取、CT 情报）
// ============================================================================

void CS2DSimulation::ToggleHoldC4For(ActorId actorId) {
    Player* actor = FindActor(actorId);
    if (!actor) return;
    // 按 5 等价于切到 C4 槽；已经在 C4 槽则切回副武器
    if (!ActorCarriesC4(*actor)) {
        actor->holdingC4 = false;
        return;
    }
    if (actor->weaponSlot == 4) {
        SwitchWeaponSlotFor(actorId, 1);
    } else {
        SwitchWeaponSlotFor(actorId, 4);
    }
}

void CS2DSimulation::UpdateBombItem(float dt) {
    if (mode_ != MatchMode::BombDefusal || bombPlanted_) {
        bombIntel_ = false;
        return;
    }

    if (bombDropDelay_ > 0.0f) bombDropDelay_ -= dt;

    // ---- 掉落在地上的 C4：最近的存活 T 靠近即拾取（丢下 1 秒内不捡）----
    if (bombDropped_ && bombDropDelay_ <= 0.0f) {
        Player* best = nullptr;
        float bestD2 = Tuning::BombPickupRadius * Tuning::BombPickupRadius;

        for (auto& p : actors_) {
            if (!p.IsActive() || !p.alive || p.team != Team::T) continue;
            const float dx = p.x - bombDropX_;
            const float dy = p.y - bombDropY_;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = &p; }
        }

        if (best) {
            bombCarrier_ = best->id;
            bombDropped_ = false;
            best->weapons[4] = WeaponId::C4;
            best->equipment[4] = {};
            best->equipment[4].id = c4EquipmentId_;
            best->equipment[4].epoch = purchaseEpoch_;
            if (best->id == localActorId_) {
                std::cout << "[C4] 玩家拾取了掉落的 C4" << std::endl;
            } else {
                std::cout << "[C4] " << TeamName(Team::T) << " #" << best->id
                          << " 拾取了掉落的 C4" << std::endl;
            }
        }
    }

    // ---- CT 情报：任何活着的 CT 看到掉落 C4 → 记 8 秒 ----
    if (bombIntel_) {
        bombIntelAge_ -= dt;
        if (bombIntelAge_ <= 0.0f) bombIntel_ = false;
    }
    if (bombDropped_) {
        for (const auto& p : actors_) {
            if (!p.IsActive() || !p.alive || p.team != Team::CT) continue;
            if (CanSeePoint(p, bombDropX_, bombDropY_)) {
                bombIntel_ = true;
                bombIntelX_ = bombDropX_;
                bombIntelY_ = bombDropY_;
                bombIntelAge_ = 8.0f;
                break;
            }
        }
    }
}

void CS2DSimulation::DropC4(float x, float y) {
    if (mode_ != MatchMode::BombDefusal || bombPlanted_) return;
    if (bombCarrier_ == kInvalidActorId) return;

    Player* prevCarrier = FindActor(bombCarrier_);
    bombDropped_ = true;
    bombDropDelay_ = 1.0f;
    bombDropX_ = x;
    bombDropY_ = y;
    bombCarrier_ = kInvalidActorId;

    if (prevCarrier) {
        prevCarrier->holdingC4 = false;
        prevCarrier->plantProgress = 0.0f;
        prevCarrier->weapons[4] = WeaponId::None;
        prevCarrier->equipment[4] = {};
        if (prevCarrier->weaponSlot == 4) EquipFallbackWeaponFor(*prevCarrier);
    }
    for (auto& a : actors_) {
        a.holdingC4 = false;
        a.weapons[4] = WeaponId::None;
        a.equipment[4] = {};
        if (a.weaponSlot == 4) EquipFallbackWeaponFor(a);
    }
    std::cout << "[C4] C4 掉落在 (" << static_cast<int>(x) << ","
              << static_cast<int>(y) << ")" << std::endl;
}

// 当前槽位不可用（C4 丢了 / 枪丢了）时，切到有武器的槽位并读它自己的弹药
void CS2DSimulation::EquipFallbackWeaponFor(Player& actor) {
    if (actor.weapons[actor.weaponSlot] != WeaponId::None &&
        actor.weaponSlot != 4) {
        actor.LoadAmmoFromSlot();
        return;
    }
    int target = 2;   // 近战永远有
    if (actor.weapons[0] != WeaponId::None)      target = 0;
    else if (actor.weapons[1] != WeaponId::None) target = 1;
    SwitchWeaponSlotFor(actor.id, target);
    actor.reloading = false;
    actor.reloadTimer = 0.0f;
    actor.holdingC4 = false;
    actor.LoadAmmoFromSlot();
}
