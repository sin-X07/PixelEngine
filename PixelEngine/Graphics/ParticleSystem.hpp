#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <random>

class ParticleSystem {
private:
    static constexpr size_t MAX_PARTICLES = 200;
    static constexpr size_t MAX_DEBRIS = 160;
    static constexpr size_t MAX_BLOOD = 120;

    // 血液粒子
    struct BloodParticle {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float lifetime = 0.0f;
        float maxLifetime = 0.0f;
        bool active = false;
    };

    std::vector<Particle> particlePool;
    std::vector<DebrisParticle> debrisPool;
    std::vector<BloodParticle> bloodPool;

    SDL_Texture* bloodTexture = nullptr;

    float screenShakeTimer = 0.0f;
    float screenShakeIntensity = 0.0f;

    std::mt19937 gen;

public:
    ParticleSystem(std::mt19937& rng);

    void Reset();
    void Update(float dt);
    void Render(SDL_Renderer* renderer, float cameraX, float cameraY, float cameraZoom);

    void EmitExplosion(float spawnX, float spawnY, int count);
    void EmitDebrisBurst(float centerX, float centerY, int count, SDL_Color color);
    void EmitBloodBurst(float centerX, float centerY, int count);
    // 按被破坏障碍类型发射对应碎屑（矮墙青灰 / 木箱棕色）
    void EmitObstacleDebris(const SDL_Rect& rect, bool isLowWall);
    void SetBloodTexture(SDL_Texture* texture) { bloodTexture = texture; }
    void TriggerScreenShake(float duration, float intensity);

    SDL_FPoint GetShakeOffset();
    bool IsShaking() const { return screenShakeTimer > 0.0f; }
};
