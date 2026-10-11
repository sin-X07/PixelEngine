#pragma once
#include "../Core/Types.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <random>

class ParticleSystem {
private:
    static constexpr size_t MAX_PARTICLES = 200;
    static constexpr size_t MAX_DEBRIS = 160;

    std::vector<Particle> particlePool;
    std::vector<DebrisParticle> debrisPool;

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
    void TriggerScreenShake(float duration, float intensity);

    SDL_FPoint GetShakeOffset();
    bool IsShaking() const { return screenShakeTimer > 0.0f; }
};
