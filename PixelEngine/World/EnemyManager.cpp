#include "EnemyManager.hpp"
#include "Player.hpp"
#include "WorldMap.hpp"
#include "PhysicsSystem.hpp"
#include "CombatSystem.hpp"
#include "../Graphics/ParticleSystem.hpp"
#include "../Graphics/RenderSystem.hpp"
#include "../Audio/AudioManager.hpp"
#include <cmath>
#include <algorithm>
#include <random>
#include <functional>

EnemyManager::EnemyManager(Player& player, WorldMap& worldMap, PhysicsSystem& physics,
                           ParticleSystem& particles, AudioManager* audio, RenderSystem* render,
                           std::mt19937& rng, std::function<void(int)> onPlayerDamage)
    : player_(player), worldMap_(worldMap), physics_(physics), particles_(particles),
      audio_(audio), render_(render), rng_(rng), onPlayerDamage_(std::move(onPlayerDamage)) {}

void EnemyManager::Reset() {
    monsters.clear();
    bullets.clear();
    weaponDrops.clear();
    pressure = 0.0f;
    recentKills = 0;
    recentKillTimer = 0.0f;
    spawnTimer = 2.0f;
    weaponSpawnTimer = WEAPON_SPAWN_INTERVAL;
}

void EnemyManager::Update(float dt, bool timeAttackMode) {
    // 武器掉落冷却推进（刚丢弃的武器短暂不可自动拾取）
    for (auto& d : weaponDrops) {
        if (d.pickupCooldown > 0.0f) d.pickupCooldown = std::max(0.0f, d.pickupCooldown - dt);
    }

    // 压力值衰减与击杀频率计时
    if (pressure > 0.0f) pressure = std::max(0.0f, pressure - PRESSURE_DECAY * dt);
    if (recentKillTimer > 0.0f) {
        recentKillTimer -= dt;
        if (recentKillTimer <= 0.0f) recentKills = 0;
    }

    // 模式一：刷怪与武器定时掉落 + 经过时自动拾取
    if (timeAttackMode) {
        spawnTimer -= dt;
        if (spawnTimer <= 0.0f) {
            SpawnMonster();
            float interval = BASE_SPAWN_INTERVAL * (1.0f + pressure * PRESSURE_SPAWN_SCALE);
            spawnTimer = std::min(interval, MAX_SPAWN_INTERVAL);
        }

        weaponSpawnTimer -= dt;
        if (weaponSpawnTimer <= 0.0f) {
            SpawnWeaponDrop(false, 0.0f, 0.0f);
            weaponSpawnTimer = WEAPON_SPAWN_INTERVAL;
        }

        combat_->AutoPickupWeapons();
    }

    // 子弹与怪物更新
    UpdateBullets(dt);
    UpdateMonsters(dt);

    // 清理死亡怪物
    for (size_t i = 0; i < monsters.size(); ) {
        if (monsters[i].alive && monsters[i].health <= 0.0f) {
            KillMonster(i);
            monsters.erase(monsters.begin() + i);
        } else {
            ++i;
        }
    }
}

int EnemyManager::CurrentMonsterCap() const {
    const Weapon* weapon = combat_->GetActiveWeapon();
    return (weapon->IsMelee()) ? MELEE_MONSTER_CAP : RANGED_MONSTER_CAP;
}

MonsterType EnemyManager::PickRandomMonsterType() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const float roll = dist(rng_);
    if (combat_->HasRangedUnlocked()) {
        if (roll < 0.5f) return MonsterType::GRUNT;
        if (roll < 0.7f) return MonsterType::BRUTE;
        return MonsterType::SHOOTER;
    }
    if (roll < 0.7f) return MonsterType::GRUNT;
    return MonsterType::BRUTE;
}

void EnemyManager::SpawnWeaponDropAt(WeaponType type, float x, float y, int ammo, float pickupCooldown) {
    weaponDrops.push_back({type, x, y, ammo, pickupCooldown});
}

void EnemyManager::SpawnWeaponDrop(bool fromKill, float x, float y) {
    const WeaponType type = combat_->PickRandomWeaponType();
    // 击杀/定时掉落的枪械为满弹夹；近战为 -1
    const int ammo = (type == WeaponType::PISTOL) ? Weapon::MakePistol().magazineSize
                   : (type == WeaponType::RIFLE)  ? Weapon::MakeRifle().magazineSize
                   : -1;

    if (fromKill) {
        SpawnWeaponDropAt(type, x, y, ammo);
        return;
    }

    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    if (!worldMap_.FindRandomSpawnPoint(centerX, centerY, render_->MaxVisionRadius(), rng_, spawnX, spawnY)) {
        return;
    }
    SpawnWeaponDropAt(type, spawnX, spawnY, ammo);
}

void EnemyManager::SpawnMonster() {
    int aliveCount = 0;
    for (const auto& m : monsters) {
        if (m.alive) ++aliveCount;
    }
    if (aliveCount >= CurrentMonsterCap()) return;

    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    if (!worldMap_.FindRandomSpawnPoint(centerX, centerY, render_->MaxVisionRadius(), rng_, spawnX, spawnY)) {
        return;
    }

    monsters.push_back(Monster::Make(PickRandomMonsterType(), spawnX, spawnY));
}

void EnemyManager::UpdateMonsters(float dt) {
    const float playerCX = player_.x + player_.size * 0.5f;
    const float playerCY = player_.y + player_.size * 0.5f;

    for (auto& m : monsters) {
        if (!m.alive) continue;

        const float monsterCX = m.x + m.size * 0.5f;
        const float monsterCY = m.y + m.size * 0.5f;
        const float dx = playerCX - monsterCX;
        const float dy = playerCY - monsterCY;
        const float dist = std::sqrt(dx * dx + dy * dy);

        if (m.ranged) {
            // 远程：保持距离并朝玩家射击
            if (dist > m.attackRange) {
                m.Update(dt, playerCX, playerCY, worldMap_);
            }
            if (dist <= m.attackRange && m.attackCooldown <= 0.0f) {
                m.attackCooldown = m.attackCooldownMax;
                Bullet bullet;
                bullet.x = monsterCX;
                bullet.y = monsterCY;
                const float len = std::max(dist, 0.001f);
                bullet.vx = dx / len * m.bulletSpeed;
                bullet.vy = dy / len * m.bulletSpeed;
                bullet.maxDist = m.attackRange;
                bullet.damage = m.damage;
                bullet.instaKill = false;
                bullet.hostile = true;
                bullet.active = true;
                bullets.push_back(bullet);
            }
        } else {
            // 近战：贴近后攻击并破坏前方障碍
            if (dist > m.attackRange) {
                m.Update(dt, playerCX, playerCY, worldMap_);
            }
            if (dist <= m.attackRange && m.attackCooldown <= 0.0f) {
                m.attackCooldown = m.attackCooldownMax;
                const float len = std::max(dist, 0.001f);
                const float dirX = dx / len;
                const float dirY = dy / len;
                const float hitX = monsterCX + dirX * (m.size * 0.5f + 4.0f) - 6.0f;
                const float hitY = monsterCY + dirY * (m.size * 0.5f + 4.0f) - 6.0f;
                const auto broken = physics_.BreakObstaclesInRect(hitX, hitY, 12.0f, 12.0f, worldMap_);
                for (const auto& obs : broken) particles_.EmitObstacleDebris(obs.rect, obs.isLowWall);
                onPlayerDamage_(m.damage);
            }
        }
    }
}

void EnemyManager::UpdateBullets(float dt) {
    for (size_t i = 0; i < bullets.size(); ) {
        Bullet& b = bullets[i];
        if (!b.active) {
            bullets.erase(bullets.begin() + i);
            continue;
        }

        b.x += b.vx * dt;
        b.y += b.vy * dt;
        b.traveled += std::sqrt(b.vx * b.vx + b.vy * b.vy) * dt;

        if (b.traveled >= b.maxDist) {
            bullets.erase(bullets.begin() + i);
            continue;
        }

        // 破坏可破坏障碍；命中 # 墙则停止
        std::vector<BrokenObstacle> broken;
        if (physics_.BulletPassesAndBreaks(b.x, b.y, worldMap_, broken)) {
            bullets.erase(bullets.begin() + i);
            continue;
        }
        for (const auto& obs : broken) particles_.EmitObstacleDebris(obs.rect, obs.isLowWall);

        if (b.hostile) {
            if (PhysicsSystem::CheckCollision(player_.x, player_.y, player_.size, b.x - 2.0f, b.y - 2.0f, 4.0f, 4.0f)) {
                onPlayerDamage_(b.damage);
                bullets.erase(bullets.begin() + i);
                continue;
            }
        } else {
            bool hit = false;
            for (auto& m : monsters) {
                if (!m.alive) continue;
                const float monsterCX = m.x + m.size * 0.5f;
                const float monsterCY = m.y + m.size * 0.5f;
                const float dx = b.x - monsterCX;
                const float dy = b.y - monsterCY;
                const float radius = m.size * 0.5f + 2.0f;
                if (dx * dx + dy * dy <= radius * radius) {
                    if (b.instaKill) m.health = 0.0f;
                    else m.health -= b.damage;
                    audio_->PlaySound(SoundEffect::HIT);
                    particles_.EmitBloodBurst(monsterCX, monsterCY, 8);
                    hit = true;
                    break;
                }
            }
            if (hit) {
                bullets.erase(bullets.begin() + i);
                continue;
            }
        }

        ++i;
    }
}

void EnemyManager::KillMonster(size_t index) {
    const Monster& m = monsters[index];

    const float centerX = player_.x + player_.size * 0.5f;
    const float centerY = player_.y + player_.size * 0.5f;
    const float monsterCX = m.x + m.size * 0.5f;
    const float monsterCY = m.y + m.size * 0.5f;
    const float dx = monsterCX - centerX;
    const float dy = monsterCY - centerY;
    const float dist = std::sqrt(dx * dx + dy * dy);

    // 压力值：越近、击杀频率越高，压力越大
    const float distRatio = std::clamp(dist / MAX_KILL_DISTANCE, 0.0f, 1.0f);
    pressure += KILL_PRESSURE_BASE * (1.0f - distRatio) + FREQ_PRESSURE_GAIN * recentKills;
    recentKills++;
    recentKillTimer = KILL_FREQ_WINDOW;

    audio_->PlaySound(SoundEffect::KILL);
    particles_.EmitBloodBurst(monsterCX, monsterCY, 14);

    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    if (dist01(rng_) < MONSTER_DROP_CHANCE) {
        SpawnWeaponDrop(true, monsterCX, monsterCY);
    }
}
