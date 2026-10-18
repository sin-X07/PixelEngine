#include "CombatSystem.hpp"
#include "Player.hpp"
#include "Monster.hpp"
#include "WorldMap.hpp"
#include "PhysicsSystem.hpp"
#include "../Graphics/ParticleSystem.hpp"
#include "../Graphics/RenderSystem.hpp"
#include "../Audio/AudioManager.hpp"
#include "../Input/InputManager.hpp"
#include <cmath>
#include <algorithm>

CombatSystem::CombatSystem(Player& player,
                           std::vector<Monster>& monsters,
                           std::vector<Bullet>& bullets,
                           std::vector<WeaponDrop>& weaponDrops,
                           WorldMap& worldMap,
                           PhysicsSystem& physics,
                           ParticleSystem& particles,
                           AudioManager* audio,
                           std::mt19937& rng,
                           InputManager* input,
                           RenderSystem* render)
    : player_(player), monsters_(monsters), bullets_(bullets), weaponDrops_(weaponDrops),
      worldMap_(worldMap), physics_(physics), particles_(particles), audio_(audio),
      rng_(rng), input_(input), render_(render) {
    meleeWeapon = Weapon::MakeDagger();
    pistolWeapon = Weapon::MakePistol();
    rifleWeapon = Weapon::MakeRifle();
}

void CombatSystem::Reset() {
    weaponCooldown = 0.0f;
    attackAnimActive = false;
    attackAnimTimer = 0.0f;
    meleeHitRegistered = false;
    noWeaponHintTimer = 0.0f;
    noWeaponHintText.clear();
    meleeWeapon = Weapon::MakeDagger();
    hasPistol = false;
    hasRifle = false;
    pistolWeapon = Weapon::MakePistol();
    rifleWeapon = Weapon::MakeRifle();
    activeSlot = SLOT_MELEE;
    rangedUnlocked = false;
}

void CombatSystem::Update(float dt) {
    if (weaponCooldown > 0.0f) weaponCooldown = std::max(0.0f, weaponCooldown - dt);
    if (attackAnimActive) {
        attackAnimTimer -= dt;
        if (attackAnimTimer <= 0.0f) {
            attackAnimTimer = 0.0f;
            attackAnimActive = false;
        }
    }
    if (noWeaponHintTimer > 0.0f) noWeaponHintTimer = std::max(0.0f, noWeaponHintTimer - dt);
}

const Weapon* CombatSystem::GetActiveWeapon() const {
    if (activeSlot == SLOT_PISTOL) return &pistolWeapon;
    if (activeSlot == SLOT_RIFLE) return &rifleWeapon;
    return &meleeWeapon;
}

Weapon* CombatSystem::GetActiveWeapon() {
    if (activeSlot == SLOT_PISTOL) return &pistolWeapon;
    if (activeSlot == SLOT_RIFLE) return &rifleWeapon;
    return &meleeWeapon;
}

float CombatSystem::ComputeTotalWalkPenalty() const {
    return (hasPistol ? pistolWeapon.walkPenalty : 0.0f) +
           (hasRifle ? rifleWeapon.walkPenalty : 0.0f);
}

float CombatSystem::ComputeTotalSprintPenalty() const {
    return (hasPistol ? pistolWeapon.sprintPenalty : 0.0f) +
           (hasRifle ? rifleWeapon.sprintPenalty : 0.0f);
}

float CombatSystem::NormalizeAngle(float angle) const {
    const float twoPi = 2.0f * 3.14159265358979323846f;
    while (angle > 3.14159265358979323846f) angle -= twoPi;
    while (angle < -3.14159265358979323846f) angle += twoPi;
    return angle;
}

Direction CombatSystem::DirFromAngle(float angle) const {
    const float ax = std::cos(angle);
    const float ay = std::sin(angle);
    if (std::fabs(ax) >= std::fabs(ay)) {
        return (ax >= 0.0f) ? DIR_RIGHT : DIR_LEFT;
    }
    return (ay >= 0.0f) ? DIR_DOWN : DIR_UP;
}

void CombatSystem::UpdateAimAngle() {
    const int mouseX = input_->GetMouseX();
    const int mouseY = input_->GetMouseY();
    const float zoom = render_->CurrentCameraZoom();
    const float worldX = render_->GetCameraX() + static_cast<float>(mouseX) / zoom;
    const float worldY = render_->GetCameraY() + static_cast<float>(mouseY) / zoom;
    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;
    aimAngle = std::atan2(worldY - centerY, worldX - centerX);
}

void CombatSystem::TryAttack() {
    if (weaponCooldown > 0.0f) return;
    Weapon* weapon = GetActiveWeapon();

    if (weapon->ranged) {
        if (!weapon->HasAmmo()) return;
        weapon->ammo--;
        weaponCooldown = weapon->cooldown;
        FireWeapon(*weapon);
    } else {
        weaponCooldown = weapon->cooldown;
        attackAnimActive = true;
        attackAnimTimer = ATTACK_DURATION;
        meleeHitRegistered = false;

        // 近战音效：武士刀用专属挥砍音，匕首用通用挥剑音
        if (weapon->type == WeaponType::KATANA) {
            audio_->PlaySound(SoundEffect::KATANA_SWING);
        } else {
            audio_->PlaySound(SoundEffect::ATTACK_SWING);
        }

        // 破坏前方障碍
        const Direction dir = DirFromAngle(aimAngle);
        const auto broken = physics_.PerformAttackHitCheck(player_.x, player_.y, player_.size, dir, worldMap_);
        if (!broken.empty()) {
            audio_->PlaySound(SoundEffect::OBSTACLE_BREAK);
            particles_.TriggerScreenShake(0.12f, 3.0f);
            for (const auto& obs : broken) particles_.EmitObstacleDebris(obs.rect, obs.isLowWall);
        }

        // 扇形命中怪物
        ApplyMeleeHit(*weapon);
    }
}

void CombatSystem::FireWeapon(const Weapon& weapon) {
    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;

    Bullet bullet;
    bullet.x = centerX;
    bullet.y = centerY;
    bullet.vx = std::cos(aimAngle) * BULLET_SPEED;
    bullet.vy = std::sin(aimAngle) * BULLET_SPEED;
    bullet.maxDist = weapon.range;
    bullet.damage = weapon.damage;
    bullet.instaKill = weapon.instaKill;
    bullet.hostile = false;
    bullet.active = true;
    bullets_.push_back(bullet);

    if (weapon.type == WeaponType::RIFLE) {
        audio_->PlaySound(SoundEffect::AK47_FIRE);
    } else {
        audio_->PlaySound(SoundEffect::DEAGLE_FIRE);
    }
}

void CombatSystem::ApplyMeleeHit(const Weapon& weapon) {
    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;

    for (auto& monster : monsters_) {
        if (!monster.alive) continue;
        const float monsterCX = monster.x + monster.size * 0.5f;
        const float monsterCY = monster.y + monster.size * 0.5f;
        const float dx = monsterCX - centerX;
        const float dy = monsterCY - centerY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float reach = player_.size * 0.5f + monster.size * 0.5f + weapon.range;
        if (dist > reach) continue;

        const float diff = std::fabs(NormalizeAngle(std::atan2(dy, dx) - aimAngle));
        if (diff <= MELEE_ARC_HALF) {
            monster.health -= weapon.damage;
        }
    }
}

void CombatSystem::TrySwitchWeapon(ActiveSlot slot) {
    if (slot == SLOT_RIFLE && !hasRifle) {
        noWeaponHintText = "未装配步枪";
        noWeaponHintTimer = 1.5f;
        return;
    }
    if (slot == SLOT_PISTOL && !hasPistol) {
        noWeaponHintText = "未装配手枪";
        noWeaponHintTimer = 1.5f;
        return;
    }
    activeSlot = slot;
}

void CombatSystem::HandlePickupOrDrop() {
    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;

    // F：仅手动拾取近战掉落（匕首/武士刀互换）；枪械已改为经过时自动拾取
    for (size_t i = 0; i < weaponDrops_.size(); ++i) {
        const WeaponDrop& drop = weaponDrops_[i];
        if (drop.type != WeaponType::DAGGER && drop.type != WeaponType::KATANA) continue;
        const float dx = drop.x - centerX;
        const float dy = drop.y - centerY;
        if (dx * dx + dy * dy <= PICKUP_RADIUS * PICKUP_RADIUS) {
            PickUpMeleeDrop(drop);
            weaponDrops_.erase(weaponDrops_.begin() + i);
            return;
        }
    }

    DropActiveWeapon();
}

void CombatSystem::AutoPickupWeapons() {
    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;

    for (size_t i = 0; i < weaponDrops_.size(); ) {
        const WeaponDrop& drop = weaponDrops_[i];
        // 仅枪械自动拾取，且当前未持有才拾取
        if (drop.type != WeaponType::PISTOL && drop.type != WeaponType::RIFLE) { ++i; continue; }
        if (drop.pickupCooldown > 0.0f) { ++i; continue; } // 刚丢弃的枪暂不收回

        const float dx = drop.x - centerX;
        const float dy = drop.y - centerY;
        if (dx * dx + dy * dy <= PICKUP_RADIUS * PICKUP_RADIUS) {
            bool picked = false;
            if (drop.type == WeaponType::PISTOL && !hasPistol) {
                hasPistol = true;
                pistolWeapon = Weapon::MakePistol();
                pistolWeapon.ammo = (drop.ammo >= 0) ? drop.ammo : pistolWeapon.magazineSize;
                rangedUnlocked = true;
                picked = true;
            } else if (drop.type == WeaponType::RIFLE && !hasRifle) {
                hasRifle = true;
                rifleWeapon = Weapon::MakeRifle();
                rifleWeapon.ammo = (drop.ammo >= 0) ? drop.ammo : rifleWeapon.magazineSize;
                rangedUnlocked = true;
                picked = true;
            }

            if (picked) {
                audio_->PlaySound(SoundEffect::PICKUP);
                weaponDrops_.erase(weaponDrops_.begin() + i);
                continue; // 不改变 activeSlot
            }
        }
        ++i;
    }
}

void CombatSystem::PickUpMeleeDrop(const WeaponDrop& drop) {
    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;

    if (drop.type == WeaponType::DAGGER) {
        if (meleeWeapon.type == WeaponType::KATANA) {
            weaponDrops_.push_back({WeaponType::KATANA, centerX, centerY, -1, 1.5f});
            meleeWeapon = Weapon::MakeDagger();
        }
        activeSlot = SLOT_MELEE;
    } else if (drop.type == WeaponType::KATANA) {
        if (meleeWeapon.type == WeaponType::DAGGER) {
            weaponDrops_.push_back({WeaponType::DAGGER, centerX, centerY, -1, 1.5f});
            meleeWeapon = Weapon::MakeKatana();
        }
        activeSlot = SLOT_MELEE;
    }

    audio_->PlaySound(SoundEffect::PICKUP);
}

void CombatSystem::DropActiveWeapon() {
    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;

    if (activeSlot == SLOT_PISTOL && hasPistol) {
        hasPistol = false;
        weaponDrops_.push_back({WeaponType::PISTOL, centerX, centerY, pistolWeapon.ammo, 1.5f}); // 保留剩余子弹
        activeSlot = SLOT_MELEE;
        audio_->PlaySound(SoundEffect::CLICK);
    } else if (activeSlot == SLOT_RIFLE && hasRifle) {
        hasRifle = false;
        weaponDrops_.push_back({WeaponType::RIFLE, centerX, centerY, rifleWeapon.ammo, 1.5f}); // 保留剩余子弹
        activeSlot = SLOT_MELEE;
        audio_->PlaySound(SoundEffect::CLICK);
    } else if (activeSlot == SLOT_MELEE && meleeWeapon.type == WeaponType::KATANA) {
        weaponDrops_.push_back({WeaponType::KATANA, centerX, centerY, -1, 1.5f});
        meleeWeapon = Weapon::MakeDagger();
        audio_->PlaySound(SoundEffect::CLICK);
    }
    // 匕首为默认武器，不可丢弃
}

WeaponType CombatSystem::PickRandomWeaponType() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const float roll = dist(rng_);
    if (roll < 0.5f) return WeaponType::KATANA;
    if (roll < 0.8f) return WeaponType::PISTOL;
    return WeaponType::RIFLE;
}
