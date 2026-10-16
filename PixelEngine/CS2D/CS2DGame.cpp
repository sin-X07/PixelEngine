#include "CS2DGame.hpp"
#include "TMJParser.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>

CS2DGame::CS2DGame()
    : rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())) {
    world_ = std::make_unique<CS2DWorld>();
}

bool CS2DGame::LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer) {
    renderer_ = renderer;

    // 1. 解析 TMJ
    CS2DMapData mapData;
    if (!TMJParser::Parse(resolvedTMJPath, mapData)) {
        return false;
    }

    // 2. 构建世界
    if (!world_->Build(mapData)) {
        return false;
    }

    // 3. 设置渲染器
    if (renderer_) {
        // 提取 TMJ 所在目录，用于加载 tileset 图片
        std::string mapDir;
        const auto slashPos = resolvedTMJPath.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            mapDir = resolvedTMJPath.substr(0, slashPos);
        }
        renderer_->SetWorld(world_.get());
    }

    // 终止任何残留 BOT
    bots_.clear();
    bullets_.clear();
    playerKills_ = 0;
    playerDeaths_ = 0;

    SpawnPlayer();
    // 预生成初始 BOT
    for (int i = 0; i < ACTIVE_BOTS; ++i) {
        Player bot;
        SpawnBOT(bot, i % 2 != 0);
        bots_.push_back(bot);
    }

    std::cout << "[CS2DGame] 地图加载完成，玩家 + " << bots_.size() << " 个 BOT 就绪" << std::endl;
    return true;
}

void CS2DGame::Reset() {
    bullets_.clear();
    bots_.clear();
    playerKills_  = 0;
    playerDeaths_ = 0;
    botAITimer_   = 0.0f;
    botRespawnTimer_ = 0.0f;
    SpawnPlayer();
    for (int i = 0; i < ACTIVE_BOTS; ++i) {
        Player bot;
        SpawnBOT(bot, i % 2 != 0);
        bots_.push_back(bot);
    }
}

// ========= 出生点分配 =========

void CS2DGame::SpawnPlayer() {
    player_.alive   = true;
    player_.health  = 100;
    player_.maxHealth = 100;
    player_.vx = player_.vy = 0.0f;
    player_.aimAngle = 0.0f;
    player_.shootCooldown = 0.0f;
    player_.isCT = true; // 玩家固定为反恐方

    const auto& spawns = world_->GetSpawnPoints();
    // 优先选一个 CT 出生点
    bool found = false;
    for (const auto& sp : spawns) {
        if (sp.isCT) {
            player_.x = sp.x;
            player_.y = sp.y;
            found = true;
            break;
        }
    }
    if (!found && !spawns.empty()) {
        player_.x = spawns[0].x;
        player_.y = spawns[0].y;
    }
    if (!found) {
        // 默认地图中心
        player_.x = world_->GetMapPixelW() * 0.5f;
        player_.y = world_->GetMapPixelH() * 0.5f;
    }
}

void CS2DGame::SpawnBOT(Player& bot, bool isCT) {
    bot.alive   = true;
    bot.health  = 100;
    bot.maxHealth = 100;
    bot.vx = bot.vy = 0.0f;
    bot.shootCooldown = 0.0f;
    bot.isCT = isCT;

    const auto& spawns = world_->GetSpawnPoints();
    std::vector<const CS2DWorld::SpawnPoint*> candidates;
    for (const auto& sp : spawns) {
        if (sp.isCT == isCT) candidates.push_back(&sp);
    }
    if (!candidates.empty()) {
        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
        const auto* sp = candidates[pick(rng_)];
        bot.x = sp->x;
        bot.y = sp->y;
        bot.aimAngle = angleDist_(rng_);
    } else if (!spawns.empty()) {
        std::uniform_int_distribution<size_t> pick(0, spawns.size() - 1);
        const auto& sp = spawns[pick(rng_)];
        bot.x = sp.x;
        bot.y = sp.y;
        bot.aimAngle = angleDist_(rng_);
    } else {
        bot.x = world_->GetMapPixelW() * 0.5f;
        bot.y = world_->GetMapPixelH() * 0.5f;
        bot.aimAngle = angleDist_(rng_);
    }
}

// ========= 每帧更新 =========

void CS2DGame::Update(float dt, bool mouseLeftDown, float mouseWorldX, float mouseWorldY,
                       bool wPressed, bool aPressed, bool sPressed, bool dPressed) {
    if (!world_) return;

    // 玩家移动
    if (player_.alive) {
        UpdatePlayerMovement(dt, wPressed, aPressed, sPressed, dPressed);
        UpdatePlayerShooting(dt, mouseLeftDown, mouseWorldX, mouseWorldY);
    } else {
        // 玩家死亡后等待重生
        botRespawnTimer_ -= dt;
        if (botRespawnTimer_ <= 0.0f) {
            SpawnPlayer();
        }
    }

    // BOT AI
    UpdateBOTs(dt);

    // 子弹
    UpdateBullets(dt);
}

// ========= 玩家移动 =========

void CS2DGame::UpdatePlayerMovement(float dt, bool w, bool a, bool s, bool d) {
    constexpr float ACCEL   = 1200.0f;
    constexpr float FRICTION = 8.0f;
    constexpr float MAX_SPEED = 200.0f;

    // 加速度
    int dx = (d ? 1 : 0) - (a ? 1 : 0);
    int dy = (s ? 1 : 0) - (w ? 1 : 0);

    // 归一化对角线移动
    float len = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    if (len > 0.001f) {
        player_.vx += (dx / len) * ACCEL * dt;
        player_.vy += (dy / len) * ACCEL * dt;
    } else {
        player_.vx -= player_.vx * FRICTION * dt;
        player_.vy -= player_.vy * FRICTION * dt;
        if (std::abs(player_.vx) < 1.0f) player_.vx = 0.0f;
        if (std::abs(player_.vy) < 1.0f) player_.vy = 0.0f;
    }

    // 限速
    float speed = std::sqrt(player_.vx * player_.vx + player_.vy * player_.vy);
    if (speed > MAX_SPEED) {
        player_.vx = player_.vx / speed * MAX_SPEED;
        player_.vy = player_.vy / speed * MAX_SPEED;
    }

    MoveWithCollision(player_, dt);
}

void CS2DGame::MoveWithCollision(Player& entity, float dt) {
    const float halfSize = entity.size * 0.5f;

    // X 轴
    float newX = entity.x + entity.vx * dt;
    if (!world_->CheckRectCollision(newX - halfSize, entity.y - halfSize,
                                     entity.size, entity.size)) {
        entity.x = newX;
    } else {
        entity.vx = 0.0f;
    }

    // Y 轴
    float newY = entity.y + entity.vy * dt;
    if (!world_->CheckRectCollision(entity.x - halfSize, newY - halfSize,
                                     entity.size, entity.size)) {
        entity.y = newY;
    } else {
        entity.vy = 0.0f;
    }

    // 夹在边界内
    entity.x = std::clamp(entity.x, 0.0f, world_->GetMapPixelW() - 1.0f);
    entity.y = std::clamp(entity.y, 0.0f, world_->GetMapPixelH() - 1.0f);
}

// ========= 玩家射击 =========

void CS2DGame::UpdatePlayerShooting(float dt, bool mouseDown, float worldMX, float worldMY) {
    if (!player_.alive) return;

    // 更新瞄准角
    const float dx = worldMX - player_.x;
    const float dy = worldMY - player_.y;
    player_.aimAngle = std::atan2(dy, dx);

    // 冷却
    if (player_.shootCooldown > 0.0f) {
        player_.shootCooldown -= dt;
    }

    if (mouseDown && player_.shootCooldown <= 0.0f) {
        // 从玩家中心发射
        FireBullet(player_.x, player_.y, player_.aimAngle, 25, -1 /* 玩家 */);
        player_.shootCooldown = 0.15f; // ~6.67 rps
    }
}

void CS2DGame::FireBullet(float x, float y, float angle, int damage, int ownerIdx) {
    constexpr float BULLET_SPEED = 600.0f;
    Bullet b;
    b.x = x;
    b.y = y;
    b.vx = std::cos(angle) * BULLET_SPEED;
    b.vy = std::sin(angle) * BULLET_SPEED;
    b.traveled = 0.0f;
    b.maxDist  = 800.0f;
    b.damage   = damage;
    b.ownerIdx = ownerIdx;
    b.active   = true;
    bullets_.push_back(b);
}

// ========= BOT AI =========

void CS2DGame::UpdateBOTs(float dt) {
    for (auto& bot : bots_) {
        if (!bot.alive) continue;

        // 简单 AI：随机游走 + 随机射击
        botAITimer_ -= dt;
        if (botAITimer_ <= 0.0f) {
            std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);
            bot.vx = dirDist(rng_) * 150.0f;
            bot.vy = dirDist(rng_) * 150.0f;
            bot.aimAngle = std::atan2(bot.vy, bot.vx);
            if (std::abs(bot.vx) < 5.0f && std::abs(bot.vy) < 5.0f) {
                bot.aimAngle = angleDist_(rng_);
            }
            botAITimer_ = 1.0f + static_cast<float>(rng_() % 100) / 100.0f;
        }

        MoveWithCollision(bot, dt);

        // 向玩家方向随机射击
        if (bot.shootCooldown > 0.0f) bot.shootCooldown -= dt;
        if (bot.shootCooldown <= 0.0f) {
            const float toPlayerDx = player_.x - bot.x;
            const float toPlayerDy = player_.y - bot.y;
            const float toPlayerDist = std::sqrt(toPlayerDx * toPlayerDx + toPlayerDy * toPlayerDy);
            if (toPlayerDist < 500.0f) {
                // 瞄准玩家方向（带随机散布）
                std::uniform_real_distribution<float> spread(-0.15f, 0.15f);
                const float aimAngle = std::atan2(toPlayerDy, toPlayerDx) + spread(rng_);
                const int botIdx = static_cast<int>(&bot - &bots_[0]);
                FireBullet(bot.x, bot.y, aimAngle, 20, botIdx);
                bot.shootCooldown = 0.4f + static_cast<float>(rng_() % 100) / 200.0f;
            }
        }
    }

    // BOT 重生
    for (auto& bot : bots_) {
        if (!bot.alive) {
            // 简化：立刻重生
            RespawnBOT(bot);
        }
    }

    // 保持 BOT 数量
    while (static_cast<int>(bots_.size()) < ACTIVE_BOTS) {
        Player bot;
        SpawnBOT(bot, bots_.size() % 2 != 0);
        bots_.push_back(bot);
    }
}

void CS2DGame::RespawnBOT(Player& bot) {
    SpawnBOT(bot, bot.isCT);
}

// ========= 子弹更新 =========

void CS2DGame::UpdateBullets(float dt) {
    for (auto& bullet : bullets_) {
        if (!bullet.active) continue;

        const float stepX = bullet.vx * dt;
        const float stepY = bullet.vy * dt;
        bullet.x += stepX;
        bullet.y += stepY;
        bullet.traveled += std::sqrt(stepX * stepX + stepY * stepY);

        // 射程耗尽
        if (bullet.traveled >= bullet.maxDist) {
            bullet.active = false;
            continue;
        }

        // 碰撞固体（使用 tile 坐标查 IsBlockingBullet）
        int tileCol = 0, tileRow = 0;
        world_->WorldToTile(bullet.x, bullet.y, tileRow, tileCol);
        if (world_->IsBlockingBullet(tileRow, tileCol)) {
            bullet.active = false;
            continue;
        }

        // 命中实体
        // 玩家子弹 vs BOT
        if (bullet.ownerIdx == -1) {
            for (auto& bot : bots_) {
                if (!bot.alive) continue;
                const float bdx = bullet.x - bot.x;
                const float bdy = bullet.y - bot.y;
                const float bdist2 = bdx * bdx + bdy * bdy;
                const float hitR = bot.size * 0.5f + 2.0f;
                if (bdist2 < hitR * hitR) {
                    bot.health -= bullet.damage;
                    bullet.active = false;
                    if (bot.health <= 0) {
                        bot.alive  = false;
                        bot.health = 0;
                        ++playerKills_;
                        std::cout << "[CS2D] 击杀 BOT! 总计: " << playerKills_ << " kills" << std::endl;
                    }
                    break;
                }
            }
        }
        // BOT 子弹 vs 玩家
        if (bullet.active && bullet.ownerIdx >= 0 && player_.alive) {
            const float pdx = bullet.x - player_.x;
            const float pdy = bullet.y - player_.y;
            const float pdist2 = pdx * pdx + pdy * pdy;
            const float hitR = player_.size * 0.5f + 2.0f;
            if (pdist2 < hitR * hitR) {
                player_.health -= bullet.damage;
                bullet.active = false;
                if (player_.health <= 0) {
                    player_.alive  = false;
                    player_.health = 0;
                    ++playerDeaths_;
                    botRespawnTimer_ = BOT_RESPAWN_DELAY;
                    std::cout << "[CS2D] 玩家阵亡! 总计: " << playerDeaths_ << " deaths" << std::endl;
                }
            }
        }
    }

    // 清理非活跃子弹
    bullets_.erase(
        std::remove_if(bullets_.begin(), bullets_.end(),
                       [](const Bullet& b) { return !b.active; }),
        bullets_.end());
}

// ========= 渲染 =========

void CS2DGame::Render() {
    if (!renderer_ || !world_) return;

    const int viewW = 800;
    const int viewH = 600;

    // 1. 相机跟随玩家
    renderer_->UpdateCamera(player_.x, player_.y, viewW, viewH);

    // 2. 世界图层（ground → walls → decorations）
    renderer_->DrawWorld();

    // 3. 绘制 BOT
    for (const auto& bot : bots_) {
        if (!bot.alive) continue;
        renderer_->DrawBOT(bot.x, bot.y, bot.size, bot.aimAngle, bot.isCT, 255);
    }

    // 4. 绘制玩家
    if (player_.alive) {
        renderer_->DrawPlayer(player_.x, player_.y, player_.size,
                              player_.aimAngle, true, 255);
    }

    // 5. 绘制子弹
    for (const auto& b : bullets_) {
        if (!b.active) continue;
        renderer_->DrawBullet(b.x, b.y, b.vx, b.vy);
    }

    // 6. overlay 层
    renderer_->DrawOverlay();
}