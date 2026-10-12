#include "ParticleSystem.hpp"
#include <cmath>
#include <algorithm>

ParticleSystem::ParticleSystem(std::mt19937& rng) : gen(rng) {
    particlePool.resize(MAX_PARTICLES);
    debrisPool.resize(MAX_DEBRIS);
    bloodPool.resize(MAX_BLOOD);
}

void ParticleSystem::Reset() {
    for (auto& p : particlePool) p.active = false;
    for (auto& p : debrisPool) p.active = false;
    for (auto& p : bloodPool) p.active = false;
    screenShakeTimer = 0.0f;
    screenShakeIntensity = 0.0f;
}

void ParticleSystem::Update(float dt) {
    if (screenShakeTimer > 0.0f) {
        screenShakeTimer -= dt;
        if (screenShakeTimer <= 0.0f) {
            screenShakeTimer = 0.0f;
            screenShakeIntensity = 0.0f;
        }
    }

    // 更新金币火花粒子
    for (auto& p : particlePool) {
        if (p.active) {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += 200.0f * dt;
            p.lifetime -= dt;
            if (p.lifetime <= 0.0f) {
                p.active = false;
            }
        }
    }

    // 更新障碍物碎屑粒子
    for (auto& p : debrisPool) {
        if (p.active) {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += 320.0f * dt; // 重力下坠
            p.lifetime -= dt;
            if (p.lifetime <= 0.0f) {
                p.active = false;
            }
        }
    }

    // 更新血液粒子
    for (auto& p : bloodPool) {
        if (p.active) {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += 260.0f * dt; // 血液下落
            p.lifetime -= dt;
            if (p.lifetime <= 0.0f) {
                p.active = false;
            }
        }
    }
}

void ParticleSystem::Render(SDL_Renderer* renderer, float cameraX, float cameraY, float cameraZoom) {
    // 渲染金币火花粒子
    for (const auto& p : particlePool) {
        if (p.active) {
            float lifeRatio = p.lifetime / p.maxLifetime;
            int alpha = static_cast<int>(lifeRatio * 255.0f);
            if (alpha < 0) alpha = 0;

            int red = 255;
            int green = static_cast<int>(140 + 115 * lifeRatio);
            int blue = static_cast<int>(30 * lifeRatio);

            SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
            SDL_Rect pRect = {
                static_cast<int>(std::lround((p.x - cameraX) * cameraZoom)),
                static_cast<int>(std::lround((p.y - cameraY) * cameraZoom)),
                static_cast<int>(std::lround(2.0f * cameraZoom)),
                static_cast<int>(std::lround(2.0f * cameraZoom))
            };
            SDL_RenderFillRect(renderer, &pRect);
        }
    }

    // 渲染障碍物碎屑粒子
    for (const auto& p : debrisPool) {
        if (p.active) {
            float lifeRatio = p.lifetime / p.maxLifetime;
            int alpha = static_cast<int>(std::clamp(lifeRatio * 255.0f, 0.0f, 255.0f));
            SDL_SetRenderDrawColor(renderer, p.color.r, p.color.g, p.color.b, alpha);

            int pSize = static_cast<int>(std::lround(p.size * cameraZoom * (0.6f + 0.4f * lifeRatio)));
            if (pSize < 1) pSize = 1;

            SDL_Rect pRect = {
                static_cast<int>(std::lround((p.x - cameraX) * cameraZoom - pSize * 0.5f)),
                static_cast<int>(std::lround((p.y - cameraY) * cameraZoom - pSize * 0.5f)),
                pSize,
                pSize
            };
            SDL_RenderFillRect(renderer, &pRect);
        }
    }

    // 渲染血液粒子
    for (const auto& p : bloodPool) {
        if (p.active) {
            const float lifeRatio = p.lifetime / p.maxLifetime;
            const int alpha = static_cast<int>(std::clamp(lifeRatio * 255.0f, 0.0f, 255.0f));
            const int pSize = std::max(2, static_cast<int>(std::lround(4.0f * cameraZoom * (0.6f + 0.4f * lifeRatio))));

            if (bloodTexture) {
                SDL_SetTextureColorMod(bloodTexture, 220, 30, 30);
                SDL_SetTextureAlphaMod(bloodTexture, alpha);
                SDL_Rect dst = {
                    static_cast<int>(std::lround((p.x - cameraX) * cameraZoom - pSize * 0.5f)),
                    static_cast<int>(std::lround((p.y - cameraY) * cameraZoom - pSize * 0.5f)),
                    pSize,
                    pSize
                };
                SDL_RenderCopy(renderer, bloodTexture, nullptr, &dst);
            } else {
                SDL_SetRenderDrawColor(renderer, 190, 25, 25, alpha);
                SDL_Rect dst = {
                    static_cast<int>(std::lround((p.x - cameraX) * cameraZoom - pSize * 0.5f)),
                    static_cast<int>(std::lround((p.y - cameraY) * cameraZoom - pSize * 0.5f)),
                    pSize,
                    pSize
                };
                SDL_RenderFillRect(renderer, &dst);
            }
        }
    }
}

void ParticleSystem::EmitExplosion(float spawnX, float spawnY, int count) {
    int activated = 0;
    std::uniform_real_distribution<float> randAngle(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> randSpeed(50.0f, 150.0f);
    std::uniform_real_distribution<float> randLife(0.3f, 0.7f);

    for (auto& p : particlePool) {
        if (!p.active) {
            p.active = true;
            p.x = spawnX;
            p.y = spawnY;

            float angle = randAngle(gen);
            float speed = randSpeed(gen);
            p.vx = std::cos(angle) * speed;
            p.vy = std::sin(angle) * speed;
            p.lifetime = randLife(gen);
            p.maxLifetime = p.lifetime;

            activated++;
            if (activated >= count) break;
        }
    }
}

void ParticleSystem::EmitDebrisBurst(float centerX, float centerY, int count, SDL_Color color) {
    int activated = 0;
    std::uniform_real_distribution<float> randAngle(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> randSpeed(50.0f, 190.0f);
    std::uniform_real_distribution<float> randLife(0.25f, 0.55f);
    std::uniform_real_distribution<float> randSize(2.0f, 4.5f);

    for (auto& p : debrisPool) {
        if (!p.active) {
            p.active = true;
            p.x = centerX;
            p.y = centerY;

            float angle = randAngle(gen);
            float speed = randSpeed(gen);
            p.vx = std::cos(angle) * speed;
            p.vy = std::sin(angle) * speed - 35.0f;
            p.size = randSize(gen);
            p.lifetime = randLife(gen);
            p.maxLifetime = p.lifetime;
            p.color = color;

            activated++;
            if (activated >= count) break;
        }
    }
}

void ParticleSystem::EmitBloodBurst(float centerX, float centerY, int count) {
    int activated = 0;
    std::uniform_real_distribution<float> randAngle(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> randSpeed(40.0f, 150.0f);
    std::uniform_real_distribution<float> randLife(0.2f, 0.5f);

    for (auto& p : bloodPool) {
        if (!p.active) {
            p.active = true;
            p.x = centerX;
            p.y = centerY;

            const float angle = randAngle(gen);
            const float speed = randSpeed(gen);
            p.vx = std::cos(angle) * speed;
            p.vy = std::sin(angle) * speed - 40.0f;
            p.lifetime = randLife(gen);
            p.maxLifetime = p.lifetime;

            activated++;
            if (activated >= count) break;
        }
    }
}

void ParticleSystem::EmitObstacleDebris(const SDL_Rect& rect, bool isLowWall) {
    const float centerX = rect.x + rect.w * 0.5f;
    const float centerY = rect.y + rect.h * 0.5f;
    if (isLowWall) {
        EmitDebrisBurst(centerX, centerY, 32, {140, 150, 155, 255}); // 砖石青灰石屑
    } else {
        EmitDebrisBurst(centerX, centerY, 26, {194, 111, 58, 255}); // 棕色木屑
    }
}

void ParticleSystem::TriggerScreenShake(float duration, float intensity) {
    screenShakeTimer = duration;
    screenShakeIntensity = intensity;
}

SDL_FPoint ParticleSystem::GetShakeOffset() {
    if (screenShakeTimer <= 0.0f) return {0.0f, 0.0f};

    std::uniform_real_distribution<float> shakeDist(-screenShakeIntensity, screenShakeIntensity);
    return { shakeDist(gen), shakeDist(gen) };
}
