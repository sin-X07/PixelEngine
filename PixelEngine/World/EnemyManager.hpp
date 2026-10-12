#pragma once
#include "../Core/Types.hpp"
#include "Monster.hpp"
#include "Weapon.hpp"
#include <vector>
#include <random>
#include <functional>

class Player;
class WorldMap;
class PhysicsSystem;
class ParticleSystem;
class AudioManager;
class RenderSystem;
class CombatSystem;

// 敌人管理器：怪物/子弹/掉落容器、压力系统、刷怪与掉落、怪物/子弹更新与击杀
class EnemyManager {
public:
    static constexpr int MELEE_MONSTER_CAP = 5;
    static constexpr int RANGED_MONSTER_CAP = 15;
    static constexpr float BASE_SPAWN_INTERVAL = 3.0f;
    static constexpr float MAX_SPAWN_INTERVAL = 10.0f;
    static constexpr float WEAPON_SPAWN_INTERVAL = 15.0f;
    static constexpr float MONSTER_DROP_CHANCE = 0.4f;
    static constexpr float KILL_PRESSURE_BASE = 1.0f;
    static constexpr float MAX_KILL_DISTANCE = 240.0f;
    static constexpr float FREQ_PRESSURE_GAIN = 0.5f;
    static constexpr float KILL_FREQ_WINDOW = 5.0f;
    static constexpr float PRESSURE_DECAY = 0.5f;
    static constexpr float PRESSURE_SPAWN_SCALE = 0.2f;

    std::vector<Monster> monsters;
    std::vector<Bullet> bullets;
    std::vector<WeaponDrop> weaponDrops;

    float pressure = 0.0f;
    int recentKills = 0;
    float recentKillTimer = 0.0f;
    float spawnTimer = 2.0f;
    float weaponSpawnTimer = WEAPON_SPAWN_INTERVAL;

    EnemyManager(Player& player, WorldMap& worldMap, PhysicsSystem& physics,
                 ParticleSystem& particles, AudioManager* audio, RenderSystem* render,
                 std::mt19937& rng, std::function<void(int)> onPlayerDamage);

    void SetCombatSystem(CombatSystem* combat) { combat_ = combat; }

    void Reset();
    void Update(float dt, bool timeAttackMode);

    int CurrentMonsterCap() const;
    MonsterType PickRandomMonsterType();
    void SpawnMonster();
    void SpawnWeaponDrop(bool fromKill, float x, float y);
    void SpawnWeaponDropAt(WeaponType type, float x, float y, int ammo, float pickupCooldown = 0.0f);
    void UpdateMonsters(float dt);
    void UpdateBullets(float dt);
    void KillMonster(size_t index);

private:
    Player& player_;
    WorldMap& worldMap_;
    PhysicsSystem& physics_;
    ParticleSystem& particles_;
    AudioManager* audio_;
    RenderSystem* render_;
    CombatSystem* combat_ = nullptr;
    std::mt19937& rng_;
    std::function<void(int)> onPlayerDamage_;
};
