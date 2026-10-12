#pragma once
#include "Weapon.hpp"
#include "../Core/Types.hpp"
#include <string>
#include <vector>
#include <random>

class Player;
class WorldMap;
class PhysicsSystem;
class ParticleSystem;
class AudioManager;
class InputManager;
class RenderSystem;
class Monster;

// 战斗系统：武器槽、攻击/射击、近战判定、拾取与丢弃
class CombatSystem {
public:
    static constexpr float ATTACK_DURATION = 0.18f;
    static constexpr float MELEE_ARC_HALF  = 1.0f;
    static constexpr float PICKUP_RADIUS   = 16.0f;
    static constexpr float BULLET_SPEED    = 900.0f;

    float weaponCooldown = 0.0f;
    bool  attackAnimActive = false;
    float attackAnimTimer = 0.0f;
    bool  meleeHitRegistered = false;
    float aimAngle = 0.0f;

    Weapon meleeWeapon;
    bool hasPistol = false;
    bool hasRifle = false;
    Weapon pistolWeapon;
    Weapon rifleWeapon;
    enum ActiveSlot { SLOT_MELEE, SLOT_PISTOL, SLOT_RIFLE };
    ActiveSlot activeSlot = SLOT_MELEE;

    float noWeaponHintTimer = 0.0f;
    std::string noWeaponHintText;

    // 本局是否已解锁过远程武器（粘性，供刷怪判断远程怪是否可生成）
    bool rangedUnlocked = false;

    CombatSystem(Player& player,
                 std::vector<Monster>& monsters,
                 std::vector<Bullet>& bullets,
                 std::vector<WeaponDrop>& weaponDrops,
                 WorldMap& worldMap,
                 PhysicsSystem& physics,
                 ParticleSystem& particles,
                 AudioManager* audio,
                 std::mt19937& rng,
                 InputManager* input,
                 RenderSystem* render);

    void Reset();
    void Update(float dt);                       // 冷却 / 攻击动画 / 未装配提示计时

    void UpdateAimAngle();
    void TryAttack();
    void FireWeapon(const Weapon& weapon);
    void ApplyMeleeHit(const Weapon& weapon);
    void TrySwitchWeapon(ActiveSlot slot);
    void HandlePickupOrDrop();
    void AutoPickupWeapons();
    void PickUpMeleeDrop(const WeaponDrop& drop);
    void DropActiveWeapon();

    const Weapon* GetActiveWeapon() const;
    Weapon* GetActiveWeapon();
    float ComputeTotalWalkPenalty() const;
    float ComputeTotalSprintPenalty() const;
    bool HasActiveNoWeaponHint() const { return noWeaponHintTimer > 0.0f; }
    const std::string& GetNoWeaponHintText() const { return noWeaponHintText; }
    bool HasRangedUnlocked() const { return rangedUnlocked; }

    WeaponType PickRandomWeaponType();

private:
    Direction DirFromAngle(float angle) const;
    float NormalizeAngle(float angle) const;

    Player& player_;
    std::vector<Monster>& monsters_;
    std::vector<Bullet>& bullets_;
    std::vector<WeaponDrop>& weaponDrops_;
    WorldMap& worldMap_;
    PhysicsSystem& physics_;
    ParticleSystem& particles_;
    AudioManager* audio_;
    std::mt19937& rng_;
    InputManager* input_;
    RenderSystem* render_;
};
