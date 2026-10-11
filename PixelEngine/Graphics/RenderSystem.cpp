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

    if (trap.type == TRAP_SPIKES) {
        SDL_SetRenderDrawColor(renderer, 55, 60, 65, 255);
        SDL_RenderFillRect(renderer, &screenRect);
        
        SDL_SetRenderDrawColor(renderer, 95, 105, 110, 255);
        SDL_RenderDrawRect(renderer, &screenRect);

        const int w = screenRect.w;
        const int h = screenRect.h;
        const int spikeOffsets[4][2] = {
            { w / 4, h / 4 },
            { w * 3 / 4, h / 4 },
            { w / 4, h * 3 / 4 },
            { w * 3 / 4, h * 3 / 4 }
        };

        for (int i = 0; i < 4; ++i) {
            int cx = screenRect.x + spikeOffsets[i][0];
            int cy = screenRect.y + spikeOffsets[i][1];
            int spikeR = std::max(2, static_cast<int>(3.0f * cameraZoom));

            SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
            SDL_Rect baseShadow = { cx - spikeR, cy - spikeR, spikeR * 2, spikeR * 2 };
            SDL_RenderFillRect(renderer, &baseShadow);

            SDL_SetRenderDrawColor(renderer, 220, 230, 240, 255);
            SDL_Rect spikeCore = { cx - 1, cy - spikeR, 2, spikeR * 2 };
            SDL_RenderFillRect(renderer, &spikeCore);
            SDL_Rect spikeH = { cx - spikeR, cy - 1, spikeR * 2, 2 };
            SDL_RenderFillRect(renderer, &spikeH);

            SDL_SetRenderDrawColor(renderer, 195, 30, 30, 255);
            SDL_Rect bloodDot = { cx, cy - spikeR + 1, 2, 2 };
            SDL_RenderFillRect(renderer, &bloodDot);
        }
    } else {
        SDL_SetRenderDrawColor(renderer, 65, 62, 60, 255);
        SDL_RenderFillRect(renderer, &screenRect);

        if (screenRect.w > 4 && screenRect.h > 4) {
            SDL_Rect slot = { screenRect.x + 2, screenRect.y + 2, screenRect.w - 4, screenRect.h - 4 };
            SDL_SetRenderDrawColor(renderer, 28, 26, 25, 255);
            SDL_RenderFillRect(renderer, &slot);
        }

        SDL_SetRenderDrawColor(renderer, 215, 215, 220, 255);
        int halfH = screenRect.h;
        int step = std::max(3, static_cast<int>(4.0f * cameraZoom));
        for (int yOff = 2; yOff < halfH - 2; yOff += step) {
            SDL_Rect leftTooth = { screenRect.x + 2, screenRect.y + yOff, std::max(2, static_cast<int>(3.0f * cameraZoom)), 2 };
            SDL_RenderFillRect(renderer, &leftTooth);
            SDL_Rect rightTooth = { screenRect.x + screenRect.w - 2 - std::max(2, static_cast<int>(3.0f * cameraZoom)), screenRect.y + yOff + step / 2, std::max(2, static_cast<int>(3.0f * cameraZoom)), 2 };
            SDL_RenderFillRect(renderer, &rightTooth);
        }

        int plateW = std::max(4, screenRect.w / 3);
        int plateH = std::max(4, screenRect.h / 3);
        SDL_Rect pressurePlate = {
            screenRect.x + (screenRect.w - plateW) / 2,
            screenRect.y + (screenRect.h - plateH) / 2,
            plateW,
            plateH
        };
        SDL_SetRenderDrawColor(renderer, 205, 150, 45, 255);
        SDL_RenderFillRect(renderer, &pressurePlate);

        SDL_SetRenderDrawColor(renderer, 220, 40, 40, 255);
        SDL_Rect warningDot = { pressurePlate.x + plateW / 2 - 1, pressurePlate.y + plateH / 2 - 1, 2, 2 };
        SDL_RenderFillRect(renderer, &warningDot);
    }
}

void RenderSystem::DrawWorld(const WorldMap& worldMap, float playerX, float playerY, float playerSize,
                             bool isAttacking, Direction attackDir, float attackTimer, float attackDuration,
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

    // 4. 玩家
    SDL_Rect pRect = {
        static_cast<int>(std::lround((playerX - cameraX) * cameraZoom)),
        static_cast<int>(std::lround((playerY - cameraY) * cameraZoom)),
        static_cast<int>(std::lround(playerSize * cameraZoom)),
        static_cast<int>(std::lround(playerSize * cameraZoom))
    };
    if (playerTexture) {
        const int spriteInset = static_cast<int>(std::lround(2.0f * cameraZoom));
        SDL_Rect playerSprite = {
            pRect.x + spriteInset,
            pRect.y + spriteInset,
            pRect.w - spriteInset * 2,
            pRect.h - spriteInset * 2
        };
        SDL_RenderCopy(renderer, playerTexture, nullptr, &playerSprite);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
        SDL_RenderFillRect(renderer, &pRect);
    }

    // 5. 挥剑攻击刀光
    if (isAttacking && slashTexture) {
        float progress = 1.0f - (attackTimer / attackDuration);
        int frameIndex = std::clamp(static_cast<int>(progress * 3.0f), 0, 2);
        SDL_Rect srcRect = { frameIndex * 64, 0, 64, 47 };

        double angle = 0.0;
        float slashWorldX = playerX + playerSize * 0.5f;
        float slashWorldY = playerY + playerSize * 0.5f;

        const float forwardDist = 12.0f;
        if (attackDir == DIR_RIGHT) {
            angle = 0.0;
            slashWorldX += forwardDist;
        } else if (attackDir == DIR_DOWN) {
            angle = 90.0;
            slashWorldY += forwardDist;
        } else if (attackDir == DIR_LEFT) {
            angle = 180.0;
            slashWorldX -= forwardDist;
        } else if (attackDir == DIR_UP) {
            angle = 270.0;
            slashWorldY -= forwardDist;
        }

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
