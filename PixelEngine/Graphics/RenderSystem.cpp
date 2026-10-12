#include "RenderSystem.hpp"
#include "../Core/PathUtils.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

bool RenderSystem::Initialize(SDL_Renderer* ren, int screenWidth, int screenHeight) {
    renderer = ren;

    const int imageInitFlags = IMG_Init(IMG_INIT_PNG);
    imageInitialized = (imageInitFlags & IMG_INIT_PNG) != 0;
    if (!imageInitialized) {
        std::cerr << "SDL2_image PNG 初始化失败: " << IMG_GetError() << std::endl;
    } else {
        auto loadTexture = [this](const std::string& filename) {
            const std::string path = PathUtils::ResolveProjectFile("../assets/images/dungeon/" + filename);
            SDL_Texture* texture = IMG_LoadTexture(renderer, path.c_str());
            if (!texture) {
                std::cerr << "图片加载失败 " << filename << ": " << IMG_GetError() << std::endl;
            }
            return texture;
        };
        dungeonFloorTexture = loadTexture("floor.png");
        dungeonWallTexture = loadTexture("wall.png");
        playerTexture = loadTexture("player.png");
        treasureTexture = loadTexture("treasure.png");
        slashTexture = loadTexture("slash_effect.png");

        auto loadAsset = [this](const std::string& relPath) {
            const std::string path = PathUtils::ResolveProjectFile(relPath);
            SDL_Texture* texture = IMG_LoadTexture(renderer, path.c_str());
            if (!texture) {
                std::cerr << "图片加载失败 " << relPath << ": " << IMG_GetError() << std::endl;
            }
            return texture;
        };
        gruntTexture = loadAsset("../assets/images/monsters/grunt.png");
        bruteTexture = loadAsset("../assets/images/monsters/brute.png");
        shooterTexture = loadAsset("../assets/images/monsters/shooter.png");
        daggerTexture = loadAsset("../assets/images/Knife.png");
        katanaTexture = loadAsset("../assets/images/weapons/bushido.png");
        pistolTexture = loadAsset("../assets/images/weapons/Deagle.png");
        rifleTexture = loadAsset("../assets/images/weapons/AK47.png");
        spikesTexture = loadAsset("../assets/images/traps/spikes.png");
        bearTrapTexture = loadAsset("../assets/images/traps/barricade.png");
        bulletTexture = loadAsset("../assets/images/effects/bullet.png");
        bloodTexture = loadAsset("../assets/images/effects/blood.png");
    }

    visionTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        screenWidth / VISION_MASK_SCALE,
        screenHeight / VISION_MASK_SCALE
    );
    if (visionTexture) {
        SDL_SetTextureBlendMode(visionTexture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(visionTexture, SDL_ScaleModeLinear);
        visionPixels.resize(static_cast<size_t>(screenWidth / VISION_MASK_SCALE) *
                            (screenHeight / VISION_MASK_SCALE) * 4);
    } else {
        std::cerr << "视野遮罩纹理创建失败: " << SDL_GetError() << std::endl;
    }

    return true;
}

void RenderSystem::Clean() {
    if (dungeonFloorTexture) { SDL_DestroyTexture(dungeonFloorTexture); dungeonFloorTexture = nullptr; }
    if (dungeonWallTexture) { SDL_DestroyTexture(dungeonWallTexture); dungeonWallTexture = nullptr; }
    if (playerTexture) { SDL_DestroyTexture(playerTexture); playerTexture = nullptr; }
    if (treasureTexture) { SDL_DestroyTexture(treasureTexture); treasureTexture = nullptr; }
    if (slashTexture) { SDL_DestroyTexture(slashTexture); slashTexture = nullptr; }

    if (gruntTexture) { SDL_DestroyTexture(gruntTexture); gruntTexture = nullptr; }
    if (bruteTexture) { SDL_DestroyTexture(bruteTexture); bruteTexture = nullptr; }
    if (shooterTexture) { SDL_DestroyTexture(shooterTexture); shooterTexture = nullptr; }
    if (daggerTexture) { SDL_DestroyTexture(daggerTexture); daggerTexture = nullptr; }
    if (katanaTexture) { SDL_DestroyTexture(katanaTexture); katanaTexture = nullptr; }
    if (pistolTexture) { SDL_DestroyTexture(pistolTexture); pistolTexture = nullptr; }
    if (rifleTexture) { SDL_DestroyTexture(rifleTexture); rifleTexture = nullptr; }
    if (spikesTexture) { SDL_DestroyTexture(spikesTexture); spikesTexture = nullptr; }
    if (bearTrapTexture) { SDL_DestroyTexture(bearTrapTexture); bearTrapTexture = nullptr; }
    if (bulletTexture) { SDL_DestroyTexture(bulletTexture); bulletTexture = nullptr; }
    if (bloodTexture) { SDL_DestroyTexture(bloodTexture); bloodTexture = nullptr; }

    if (imageInitialized) {
        IMG_Quit();
        imageInitialized = false;
    }

    if (visionTexture) {
        SDL_DestroyTexture(visionTexture);
        visionTexture = nullptr;
    }
}

void RenderSystem::UpdateMovementBlends(float dt, bool sprintHeld, bool focusHeld) {
    sprintBlend = std::clamp(sprintBlend + (sprintHeld ? dt : -dt / 0.4f), 0.0f, 1.0f);
    focusBlend = std::clamp(focusBlend + (focusHeld ? dt / 0.3f : -dt / 0.3f), 0.0f, 1.0f);
}

void RenderSystem::ResetMovementBlends() {
    sprintBlend = 0.0f;
    focusBlend = 0.0f;
}

float RenderSystem::CurrentCameraZoom() const {
    return CAMERA_ZOOM - focusBlend * (CAMERA_ZOOM - 1.0f);
}

float RenderSystem::CurrentVisionRadius() const {
    return VISION_RADIUS * (1.0f + focusBlend);
}

float RenderSystem::CurrentVisionSoftEdge() const {
    return VISION_SOFT_EDGE * (1.0f + focusBlend);
}

void RenderSystem::UpdateCamera(float playerX, float playerY, float playerSize,
                                const WorldMap& worldMap, bool isGameplayVisible,
                                float shakeOffsetX, float shakeOffsetY) {
    if (!isGameplayVisible) {
        cameraX = 0.0f;
        cameraY = 0.0f;
        return;
    }

    const float cameraZoom = CurrentCameraZoom();
    const float viewportWorldWidth = 800.0f / cameraZoom;
    const float viewportWorldHeight = 600.0f / cameraZoom;
    const float mapPixelWidth = static_cast<float>(worldMap.GetMapWidthTiles() * worldMap.GetTileSize());
    const float mapPixelHeight = static_cast<float>(worldMap.GetMapHeightTiles() * worldMap.GetTileSize());
    const float maxCameraX = std::max(0.0f, mapPixelWidth - viewportWorldWidth);
    const float maxCameraY = std::max(0.0f, mapPixelHeight - viewportWorldHeight);

    cameraX = std::clamp(playerX + playerSize * 0.5f - viewportWorldWidth * 0.5f, 0.0f, maxCameraX);
    cameraY = std::clamp(playerY + playerSize * 0.5f - viewportWorldHeight * 0.5f, 0.0f, maxCameraY);

    cameraX += shakeOffsetX / cameraZoom;
    cameraY += shakeOffsetY / cameraZoom;
}

void RenderSystem::DrawDungeonBackground(const WorldMap& worldMap, Uint8 overlayAlpha) {
    SDL_Rect background = {0, 0, 800, 600};
    SDL_SetRenderDrawColor(renderer, 18, 36, 43, 255);
    SDL_RenderFillRect(renderer, &background);

    if (dungeonFloorTexture) {
        const float cameraZoom = CurrentCameraZoom();
        const int tileSize = worldMap.GetTileSize();
        const float screenTileSize = tileSize * cameraZoom;
        const float startX = -std::fmod(cameraX, static_cast<float>(tileSize)) * cameraZoom;
        const float startY = -std::fmod(cameraY, static_cast<float>(tileSize)) * cameraZoom;
        for (float tileY = startY; tileY < background.h; tileY += screenTileSize) {
            for (float tileX = startX; tileX < background.w; tileX += screenTileSize) {
                SDL_Rect tile = {
                    static_cast<int>(std::lround(tileX)),
                    static_cast<int>(std::lround(tileY)),
                    static_cast<int>(std::lround(screenTileSize)),
                    static_cast<int>(std::lround(screenTileSize))
                };
                SDL_RenderCopy(renderer, dungeonFloorTexture, nullptr, &tile);
            }
        }
    }

    if (overlayAlpha > 0) {
        SDL_SetRenderDrawColor(renderer, 12, 23, 31, overlayAlpha);
        SDL_RenderFillRect(renderer, &background);
    }
}

void RenderSystem::DrawTrap(const Trap& trap) {
    const float cameraZoom = CurrentCameraZoom();
    SDL_Rect screenRect = {
        static_cast<int>(std::lround((trap.x - cameraX) * cameraZoom)),
        static_cast<int>(std::lround((trap.y - cameraY) * cameraZoom)),
        static_cast<int>(std::lround(trap.size * cameraZoom)),
        static_cast<int>(std::lround(trap.size * cameraZoom))
    };

    SDL_Texture* trapTexture = (trap.type == TRAP_SPIKES) ? spikesTexture : bearTrapTexture;
    if (trapTexture) {
        SDL_RenderCopy(renderer, trapTexture, nullptr, &screenRect);
    }
}

void RenderSystem::DrawWorld(const WorldMap& worldMap, float playerX, float playerY, float playerSize,
                             bool isAttacking, float attackAngle, float attackTimer, float attackDuration,
                             bool drawCoins) {
    const float cameraZoom = CurrentCameraZoom();

    // 1. 矮墙
    for (const SDL_Rect& lowWall : worldMap.GetLowWalls()) {
        const float visibleHeight = std::min(14.0f, static_cast<float>(lowWall.h));
        SDL_Rect screenWall = {
            static_cast<int>(std::lround((lowWall.x - cameraX) * cameraZoom)),
            static_cast<int>(std::lround((lowWall.y + lowWall.h - visibleHeight - cameraY) * cameraZoom)),
            static_cast<int>(std::lround(lowWall.w * cameraZoom)),
            static_cast<int>(std::lround(visibleHeight * cameraZoom))
        };
        if (dungeonWallTexture) {
            SDL_RenderCopy(renderer, dungeonWallTexture, nullptr, &screenWall);
        } else {
            SDL_SetRenderDrawColor(renderer, 98, 129, 126, 255);
            SDL_RenderFillRect(renderer, &screenWall);
        }
    }

    // 2. 木质箱子小障碍物
    for (const SDL_Rect& obstacle : worldMap.GetSmallObstacles()) {
        SDL_Rect screenObstacle = {
            static_cast<int>(std::lround((obstacle.x - cameraX) * cameraZoom)),
            static_cast<int>(std::lround((obstacle.y - cameraY) * cameraZoom)),
            static_cast<int>(std::lround(obstacle.w * cameraZoom)),
            static_cast<int>(std::lround(obstacle.h * cameraZoom))
        };
        SDL_SetRenderDrawColor(renderer, 130, 72, 38, 255);
        SDL_RenderFillRect(renderer, &screenObstacle);
        if (screenObstacle.w > 4 && screenObstacle.h > 4) {
            SDL_Rect inner = {screenObstacle.x + 2, screenObstacle.y + 2, screenObstacle.w - 4, screenObstacle.h - 4};
            SDL_SetRenderDrawColor(renderer, 194, 111, 58, 255);
            SDL_RenderFillRect(renderer, &inner);
        }
    }

    // 3. 陷阱
    for (const auto& trap : worldMap.GetTraps()) {
        DrawTrap(trap);
    }

    // 4. 玩家（视觉适当放大，命中盒仍为 playerSize）
    const float playerDrawSize = playerSize * 1.5f;
    const int playerCenterX = static_cast<int>(std::lround((playerX + playerSize * 0.5f - cameraX) * cameraZoom));
    const int playerCenterY = static_cast<int>(std::lround((playerY + playerSize * 0.5f - cameraY) * cameraZoom));
    const int playerDrawPx = static_cast<int>(std::lround(playerDrawSize * cameraZoom));
    SDL_Rect pRect = {
        playerCenterX - playerDrawPx / 2,
        playerCenterY - playerDrawPx / 2,
        playerDrawPx,
        playerDrawPx
    };
    if (playerTexture) {
        SDL_RenderCopy(renderer, playerTexture, nullptr, &pRect);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
        SDL_RenderFillRect(renderer, &pRect);
    }

    // 5. 挥剑攻击刀光（沿瞄准方向旋转）
    if (isAttacking && slashTexture) {
        float progress = 1.0f - (attackTimer / attackDuration);
        int frameIndex = std::clamp(static_cast<int>(progress * 3.0f), 0, 2);
        SDL_Rect srcRect = { frameIndex * 64, 0, 64, 47 };

        const float forwardDist = 12.0f;
        const float slashWorldX = playerX + playerSize * 0.5f + std::cos(attackAngle) * forwardDist;
        const float slashWorldY = playerY + playerSize * 0.5f + std::sin(attackAngle) * forwardDist;
        const double angle = attackAngle * 180.0 / 3.14159265358979323846;

        float displayW = 28.0f * cameraZoom;
        float displayH = 22.0f * cameraZoom;

        SDL_Rect destRect = {
            static_cast<int>(std::lround((slashWorldX - cameraX) * cameraZoom - displayW * 0.5f)),
            static_cast<int>(std::lround((slashWorldY - cameraY) * cameraZoom - displayH * 0.5f)),
            static_cast<int>(std::lround(displayW)),
            static_cast<int>(std::lround(displayH))
        };

        SDL_RenderCopyEx(renderer, slashTexture, &srcRect, &destRect, angle, nullptr, SDL_FLIP_NONE);
    }

    // 6. 墙壁
    for (const auto& wall : worldMap.GetWalls()) {
        SDL_Rect screenWall = {
            static_cast<int>(std::lround((wall.x - cameraX) * cameraZoom)),
            static_cast<int>(std::lround((wall.y - cameraY) * cameraZoom)),
            static_cast<int>(std::lround(wall.w * cameraZoom)),
            static_cast<int>(std::lround(wall.h * cameraZoom))
        };
        if (dungeonWallTexture) {
            SDL_RenderCopy(renderer, dungeonWallTexture, nullptr, &screenWall);
        } else {
            SDL_SetRenderDrawColor(renderer, 60, 255, 60, 255);
            SDL_RenderFillRect(renderer, &screenWall);
        }
    }

    // 7. 宝藏
    if (drawCoins) {
        const float coinSize = worldMap.GetCoinSize();
        for (const SDL_FPoint& coin : worldMap.GetCoins()) {
            if (treasureTexture) {
                SDL_Rect treasure = {
                    static_cast<int>(std::lround((coin.x - 2.0f - cameraX) * cameraZoom)),
                    static_cast<int>(std::lround((coin.y - 2.0f - cameraY) * cameraZoom)),
                    static_cast<int>(std::lround((coinSize + 4.0f) * cameraZoom)),
                    static_cast<int>(std::lround((coinSize + 4.0f) * cameraZoom))
                };
                SDL_RenderCopy(renderer, treasureTexture, nullptr, &treasure);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                int radius = static_cast<int>(std::lround(coinSize * cameraZoom * 0.5f));
                int cx = static_cast<int>(std::lround((coin.x + coinSize * 0.5f - cameraX) * cameraZoom));
                int cy = static_cast<int>(std::lround((coin.y + coinSize * 0.5f - cameraY) * cameraZoom));
                // 简单圆绘制
                for (int w = 0; w < radius * 2; w++) {
                    for (int h = 0; h < radius * 2; h++) {
                        int dx = radius - w;
                        int dy = radius - h;
                        if ((dx * dx + dy * dy) <= (radius * radius)) {
                            SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                        }
                    }
                }
            }
        }
    }
}

bool RenderSystem::HasLineOfSight(float targetWorldX, float targetWorldY,
                                  float playerCenterX, float playerCenterY,
                                  const WorldMap& worldMap) const {
    const float deltaX = targetWorldX - playerCenterX;
    const float deltaY = targetWorldY - playerCenterY;
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (distance <= 0.001f) return true;

    const int tileSize = worldMap.GetTileSize();
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / (tileSize * 0.25f))));
    const auto& levelGrid = worldMap.GetLevelGrid();
    const int mapHeight = worldMap.GetMapHeightTiles();
    const int mapWidth = worldMap.GetMapWidthTiles();

    for (int step = 1; step < steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const float sampleX = playerCenterX + deltaX * t;
        const float sampleY = playerCenterY + deltaY * t;
        const int tileX = static_cast<int>(std::floor(sampleX / tileSize));
        const int tileY = static_cast<int>(std::floor(sampleY / tileSize));

        if (tileX >= 0 && tileX < mapWidth && tileY >= 0 && tileY < mapHeight) {
            const char tile = levelGrid[tileY][tileX];
            if (tile == '#' || tile == 'H' || tile == 'o') {
                return false;
            }
        }
    }
    return true;
}

void RenderSystem::DrawVisionFog(float playerX, float playerY, float playerSize, const WorldMap& worldMap) {
    if (!visionTexture || visionPixels.empty()) return;

    const int maskWidth = 800 / VISION_MASK_SCALE;
    const int maskHeight = 600 / VISION_MASK_SCALE;
    const float playerCenterX = playerX + playerSize * 0.5f;
    const float playerCenterY = playerY + playerSize * 0.5f;
    const float visionRadius = CurrentVisionRadius();
    const float visionSoftEdge = CurrentVisionSoftEdge();
    const float fullyLitRadius = visionRadius - visionSoftEdge;
    const float cameraZoom = CurrentCameraZoom();

    for (int y = 0; y < maskHeight; ++y) {
        for (int x = 0; x < maskWidth; ++x) {
            const float worldX = cameraX + (x + 0.5f) * VISION_MASK_SCALE / cameraZoom;
            const float worldY = cameraY + (y + 0.5f) * VISION_MASK_SCALE / cameraZoom;
            const float deltaX = worldX - playerCenterX;
            const float deltaY = worldY - playerCenterY;
            const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            Uint8 alpha = 255;

            if (distance < visionRadius && HasLineOfSight(worldX, worldY, playerCenterX, playerCenterY, worldMap)) {
                if (distance <= fullyLitRadius) {
                    alpha = 0;
                } else {
                    float blend = (distance - fullyLitRadius) / visionSoftEdge;
                    blend = blend * blend * (3.0f - 2.0f * blend);
                    alpha = static_cast<Uint8>(blend * 255.0f);
                }
            }

            const size_t pixelIndex = (static_cast<size_t>(y) * maskWidth + x) * 4;
            visionPixels[pixelIndex] = 0;
            visionPixels[pixelIndex + 1] = 0;
            visionPixels[pixelIndex + 2] = 0;
            visionPixels[pixelIndex + 3] = alpha;
        }
    }

    SDL_UpdateTexture(visionTexture, nullptr, visionPixels.data(), maskWidth * 4);
    SDL_Rect viewport = {0, 0, 800, 600};
    SDL_RenderCopy(renderer, visionTexture, nullptr, &viewport);
}

void RenderSystem::DrawMonsters(const std::vector<Monster>& monsters) {
    const float z = CurrentCameraZoom();
    for (const auto& m : monsters) {
        if (!m.alive) continue;

        SDL_Rect rect = {
            static_cast<int>(std::lround((m.x - cameraX) * z)),
            static_cast<int>(std::lround((m.y - cameraY) * z)),
            static_cast<int>(std::lround(m.size * z)),
            static_cast<int>(std::lround(m.size * z))
        };

        SDL_Texture* tex = nullptr;
        if (m.type == MonsterType::GRUNT) tex = gruntTexture;
        else if (m.type == MonsterType::BRUTE) tex = bruteTexture;
        else tex = shooterTexture;

        if (tex) {
            int tw = 0;
            int th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);

            const int centerX = static_cast<int>(std::lround((m.x + m.size * 0.5f - cameraX) * z));
            const int centerY = static_cast<int>(std::lround((m.y + m.size * 0.5f - cameraY) * z));
            const int dw = std::max(8, static_cast<int>(std::lround(m.size * 1.6f * z)));
            int dh = dw;
            if (tw > 0 && th > 0) {
                dh = static_cast<int>(std::lround(static_cast<float>(dw) * th / tw));
            }
            SDL_Rect dst = {centerX - dw / 2, centerY - dh / 2, dw, dh};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            continue;
        }

        if (m.type == MonsterType::GRUNT) {
            SDL_SetRenderDrawColor(renderer, 200, 60, 60, 255);
        } else if (m.type == MonsterType::BRUTE) {
            SDL_SetRenderDrawColor(renderer, 140, 60, 160, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 50, 160, 170, 255);
        }
        SDL_RenderFillRect(renderer, &rect);

        // 眼睛：朝向玩家方向的小白点
        const int eyeSize = std::max(2, static_cast<int>(2.0f * z));
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Rect eye = {rect.x + rect.w / 2 - eyeSize / 2, rect.y + rect.h / 2 - eyeSize / 2, eyeSize, eyeSize};
        SDL_RenderFillRect(renderer, &eye);
    }
}

void RenderSystem::DrawBullets(const std::vector<Bullet>& bullets) {
    const float z = CurrentCameraZoom();
    for (const auto& b : bullets) {
        if (!b.active) continue;
        const int cx = static_cast<int>(std::lround((b.x - cameraX) * z));
        const int cy = static_cast<int>(std::lround((b.y - cameraY) * z));

        if (bulletTexture) {
            const int s = std::max(6, static_cast<int>(std::lround(10.0f * z)));
            SDL_Rect dst = {cx - s / 2, cy - s / 2, s, s};
            // 子弹贴图为竖直朝向（朝上），+90° 使其沿速度方向对齐
            const double angle = std::atan2(b.vy, b.vx) * 180.0 / 3.14159265358979323846 + 90.0;
            SDL_RenderCopyEx(renderer, bulletTexture, nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
            continue;
        }

        const int s = std::max(2, static_cast<int>(std::lround(2.0f * z)));
        SDL_Rect rect = {cx - s / 2, cy - s / 2, s, s};
        if (b.hostile) {
            SDL_SetRenderDrawColor(renderer, 255, 90, 90, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 225, 70, 255);
        }
        SDL_RenderFillRect(renderer, &rect);
    }
}

void RenderSystem::DrawWeaponDrops(const std::vector<WeaponDrop>& drops) {
    const float z = CurrentCameraZoom();
    for (const auto& d : drops) {
        SDL_Texture* tex = GetWeaponIconTexture(d.type);
        if (!tex) continue;

        const int cx = static_cast<int>(std::lround((d.x - cameraX) * z));
        const int cy = static_cast<int>(std::lround((d.y - cameraY) * z));
        const int box = std::max(10, static_cast<int>(std::lround(16.0f * z)));

        int tw = 0;
        int th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        if (tw <= 0 || th <= 0) continue;

        // 按比例适配，横枪宽、竖刀高都保持原比例
        int dw = box;
        int dh = box;
        if (tw >= th) {
            dh = static_cast<int>(std::lround(static_cast<float>(box) * th / tw));
        } else {
            dw = static_cast<int>(std::lround(static_cast<float>(box) * tw / th));
        }
        SDL_Rect rect = {cx - dw / 2, cy - dh / 2, dw, dh};
        SDL_RenderCopy(renderer, tex, nullptr, &rect);
    }
}

SDL_Texture* RenderSystem::GetWeaponIconTexture(WeaponType type) const {
    switch (type) {
        case WeaponType::DAGGER: return daggerTexture;
        case WeaponType::KATANA: return katanaTexture;
        case WeaponType::PISTOL: return pistolTexture;
        case WeaponType::RIFLE:  return rifleTexture;
    }
    return nullptr;
}
