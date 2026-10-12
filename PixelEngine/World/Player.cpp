#include "Player.hpp"
#include <algorithm>

void Player::Reset() {
    vx = 0.0f;
    vy = 0.0f;
    health = MAX_HEALTH;
    invulnTimer = 0.0f;
    noHitTimer = 0.0f;
    hpRegenTimer = 0.0f;
    diedByTrap = false;
    diedByMonster = false;
}

int Player::TakeDamage(int amount) {
    if (invulnTimer > 0.0f) return 0; // 无敌帧，不受伤

    health -= amount;
    invulnTimer = 1.0f;
    noHitTimer = 0.0f;
    hpRegenTimer = 0.0f;

    if (health <= 0) {
        health = 0;
        diedByMonster = true;
        return 2; // 致死
    }
    return 1; // 受伤未死亡
}

void Player::UpdateTimers(float dt) {
    if (invulnTimer > 0.0f) invulnTimer = std::max(0.0f, invulnTimer - dt);

    noHitTimer += dt;
    if (noHitTimer >= 3.0f) {
        hpRegenTimer += dt;
        if (hpRegenTimer >= 4.0f) {
            hpRegenTimer = 0.0f;
            if (health < MAX_HEALTH) ++health;
        }
    } else {
        hpRegenTimer = 0.0f;
    }
}
