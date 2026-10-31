#include "CS2DGame.hpp"
#include "Tuning.hpp"
#include "NadePhysics.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ============================================================================
// 玩家移动 / 射击
// ============================================================================

float CS2DGame::ComputeFlashBlind(float dist) {
    if (dist >= Tuning::FlashRadius) return 0.0f;
    const float t01 = 1.0f - dist / Tuning::FlashRadius;
    const float k = t01 * t01;
    return Tuning::FlashMinBlind + (Tuning::FlashMaxBlind - Tuning::FlashMinBlind) * k;
}

float CS2DGame::ComputeFlashIntensity(float dist) {
    if (dist >= Tuning::FlashRadius) return 0.0f;
    const float t01 = 1.0f - dist / Tuning::FlashRadius;
    return 0.35f + 0.65f * (t01 * t01);
}

void CS2DGame::UpdatePlayerMovement(float dt, bool w, bool a, bool s, bool d, bool stealth) {
    constexpr float ACCEL    = 600.0f;
    constexpr float FRICTION = 8.0f;
    // 持械移速惩罚：步枪最慢、手枪次之、近战最快（倍率来自武器表）
    const float weaponMul = player_.CurrentWeapon().moveSpeedMul;
    const float baseSpeed = stealth ? Tuning::PlayerStealthSpeed : Tuning::PlayerBaseSpeed;
    const float maxSpeed  = baseSpeed * weaponMul;

    const int dx = (d ? 1 : 0) - (a ? 1 : 0);
    const int dy = (s ? 1 : 0) - (w ? 1 : 0);

    const float len = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    if (len > 0.001f) {
        player_.vx += (dx / len) * ACCEL * dt;
        player_.vy += (dy / len) * ACCEL * dt;
    } else {
        player_.vx -= player_.vx * FRICTION * dt;
        player_.vy -= player_.vy * FRICTION * dt;
        if (std::abs(player_.vx) < 1.0f) player_.vx = 0.0f;
        if (std::abs(player_.vy) < 1.0f) player_.vy = 0.0f;
    }

    const float speed = std::sqrt(player_.vx * player_.vx + player_.vy * player_.vy);
    if (speed > maxSpeed) {
        player_.vx = player_.vx / speed * maxSpeed;
        player_.vy = player_.vy / speed * maxSpeed;
    }

    MoveWithCollision(player_, dt);

    // 移动扩散目标值 = 当前真实速度 / 该武器「跑动」满速。
    // 注意不能用本帧的 maxSpeed：按住静步时 maxSpeed 本身会降到 25，
    // 那样静步速度/静步上限仍然等于 1，准星不会聚合。
    const float runMaxSpeed = Tuning::PlayerBaseSpeed * player_.CurrentWeapon().moveSpeedMul;
    const float finalSpeed = std::sqrt(player_.vx * player_.vx + player_.vy * player_.vy);
    const float target = (runMaxSpeed > 0.001f)
        ? std::clamp(finalSpeed / runMaxSpeed, 0.0f, 1.0f)
        : 0.0f;
    const float targetBloom = (target < 0.02f) ? 0.0f : target;

    // 指数缓动追赶目标：按下静步平滑聚合，松开静步平滑散开。
    const float rate = (targetBloom > crosshairBloom_)
        ? Tuning::CrosshairBloomExpandRate
        : Tuning::CrosshairBloomContractRate;
    const float k = 1.0f - std::exp(-rate * dt);
    crosshairBloom_ += (targetBloom - crosshairBloom_) * k;
    if (std::abs(targetBloom - crosshairBloom_) < 0.002f) {
        crosshairBloom_ = targetBloom;
    }
}

// 平滑后的移动扩散：按下/松开静步不会瞬间跳变。
// 0=静止，~0.42=静步，1=全速跑动。
float CS2DGame::GetCrosshairBloom() const {
    return player_.alive ? crosshairBloom_ : 0.0f;
}

void CS2DGame::MoveWithCollision(Player& entity, float dt) {
    const float half = entity.size * 0.5f;

    const float nx = entity.x + entity.vx * dt;
    if (!world_->CheckRectCollision(nx - half, entity.y - half, entity.size, entity.size)) {
        entity.x = nx;
    } else {
        entity.vx = 0.0f;
    }

    const float ny = entity.y + entity.vy * dt;
    if (!world_->CheckRectCollision(entity.x - half, ny - half, entity.size, entity.size)) {
        entity.y = ny;
    } else {
        entity.vy = 0.0f;
    }

    entity.x = std::clamp(entity.x, 0.0f, world_->GetMapPixelW() - 1.0f);
    entity.y = std::clamp(entity.y, 0.0f, world_->GetMapPixelH() - 1.0f);
}

void CS2DGame::UpdatePlayerShooting(float dt, bool mouseDown, float worldMX, float worldMY,
                                    AudioManager* audio) {
    if (!player_.alive) return;

    player_.aimAngle = std::atan2(worldMY - player_.y, worldMX - player_.x);

    // 射击冷却每帧都要推进（持投掷物 / 换弹期间也一样），
    // 否则切到投掷物后冷却会「冻住」，表现为怎么点都扔不出去
    if (player_.shootCooldown > 0.0f) player_.shootCooldown -= dt;

    // 拆包过程中双手占用：只允许转向，不能开枪/投掷
    if (mode_ == MatchMode::BombDefusal && bombPlanted_) {
        const float dxb = player_.x - bombX_;
        const float dyb = player_.y - bombY_;
        if (dxb * dxb + dyb * dyb <= Tuning::DefuseRadius * Tuning::DefuseRadius &&
            defuseProgress_ > 0.0f) {
            return;
        }
    }

    const WeaponDef& def = player_.CurrentWeapon();

    // 投掷物：按下即投（音效在出手瞬间播放）
    // C4 槽 / 手持 C4：不能开枪也不能投掷
    if (player_.weaponSlot == 4 || playerHoldingC4_ ||
        def.cls == WeaponClass::Bomb) {
        return;
    }

    if (def.cls == WeaponClass::Grenade) {
        if (!mouseDown || player_.shootCooldown > 0.0f) return;
        if (player_.grenadeCount <= 0) return;
        ThrowGrenade(player_, player_.aimAngle, audio);
        player_.shootCooldown = def.cooldown;
        player_.shootAnimTimer = 0.12f;
        return;
    }

    if (player_.reloading) return;

    const bool mouseEdge = mouseDown && !prevMouseDown_;
    if (!def.automatic) {
        if (!mouseEdge || player_.shootCooldown > 0.0f) return;
    } else {
        if (!mouseDown || player_.shootCooldown > 0.0f) return;
    }

    // ---- 近战 ----
    if (def.cls == WeaponClass::Melee) {
        for (auto& bot : bots_) {
            if (!bot.alive || !IsHostile(player_, bot)) continue;   // 死斗 FFA
            const float bdx = bot.x - player_.x;
            const float bdy = bot.y - player_.y;
            if (std::sqrt(bdx * bdx + bdy * bdy) < def.range) {
                if (ApplyDamage(bot, static_cast<int>(def.damage), WeaponDB::ArmorPen(def.id))) OnCombatantKilled(bot, audio, &player_);
            }
        }
        player_.shootCooldown = def.cooldown;
        player_.shootAnimTimer = 0.12f;
        EmitSound(SoundEventType::KnifeSwing, player_.x, player_.y, player_.team, -1);
        if (audio) audio->PlaySound(SoundEffect::KATANA_SWING);
        return;
    }

    // ---- 枪械 ----
    if (player_.ammo <= 0) {
        // 弹匣打空：先给一声空仓咔哒（被动反馈），再开始换弹
        // 注意 StartReload 内部还会播换弹音，两者叠加就是"咔哒 + 拉栓"
        if (!player_.reloading && player_.reserve > 0 && player_.maxAmmo > 0 && audio) {
            audio->PlaySound(SoundEffect::CLICK);
        }
        StartReload(player_, audio);
        return;
    }

    // 散布 + 多弹丸（霰弹）
    // 静止 = 基础散布；静步/跑动按速度放大，跑动时明显打不准。
    const float bloom = GetCrosshairBloom();
    const float moveSpreadMul =
        1.0f + bloom * (Tuning::MoveSpreadMaxMul - 1.0f);
    const float shotSpread = def.spreadBase * moveSpreadMul;
    std::uniform_real_distribution<float> spreadDist(-shotSpread, shotSpread);
    for (int p = 0; p < std::max(1, def.pellets); ++p) {
        const float angle = player_.aimAngle + spreadDist(rng_);
        FireBullet(player_.x, player_.y, angle, def, -1, player_.team);
    }

    player_.ammo--;
    player_.shootCooldown = def.cooldown;
    player_.shootAnimTimer = 0.12f;

    // 开枪与开镜的关系（需求方口径）：
    //   - 可连发的开镜武器（AUG / SG 553 / SCAR-20 / G3SG1）：**保持开镜连射**，不自动退镜；
    //   - 单发狙（AWP / SSG 08）：开枪后退镜；开了「延迟关镜」且按住扳机时改为松手再关。
    if (scopeLevel_ > 0 && !def.automatic) {
        if (delayedScopeClose_ && mouseDown) {
            scopeCloseOnRelease_ = true;
        } else {
            ResetScope(Presentation::ScopeResetReason::ShotFired);
        }
    }

    // 枪声传播：附近敌人按武器类别/消音器得到不同半径
    EmitSound(SoundEventType::Gunshot, player_.x, player_.y, player_.team, -1,
              GunshotRadius(def));
    if (audio) audio->PlayWeaponFire(def.id);
}

// 由 WeaponDef 直接发射子弹
void CS2DGame::FireBullet(float x, float y, float angle, const WeaponDef& def,
                          int ownerIdx, Team ownerTeam) {
    Bullet b;
    b.x = x;
    b.y = y;
    b.vx = std::cos(angle) * def.bulletSpeed;
    b.vy = std::sin(angle) * def.bulletSpeed;
    b.traveled = 0.0f;
    b.maxDist  = def.range;
    b.damage   = static_cast<int>(def.damage);
    b.ownerIdx = ownerIdx;
    b.ownerTeam = ownerTeam;
    b.weapon   = def.id;      // 记录武器：击杀奖励按武器类型发放
    b.active   = true;
    bullets_.push_back(b);
}


// ============================================================================
// 子弹
// ============================================================================

// 视觉段：只记录真实模拟位移（世界坐标），由表现层绘制；不改碰撞/伤害判定。
void CS2DGame::AddBulletVisual(const Bullet& b, float fromX, float fromY,
                               float toX, float toY, bool impact) {
    Presentation::BulletVisualSegment seg;
    seg.id               = nextBulletVisualId_++;
    seg.weapon           = b.weapon;
    seg.fromX            = fromX;
    seg.fromY            = fromY;
    seg.toX              = toX;
    seg.toY              = toY;
    seg.durationSeconds  = Presentation::kBulletVisualSeconds;
    seg.remainingSeconds = Presentation::kBulletVisualSeconds;
    seg.impact           = impact;   // 仅实际碰撞终止为 true；射程到期不算命中
    bulletVisuals_.push_back(seg);

    // 硬上限：超过 512 段丢最旧的
    if (bulletVisuals_.size() > Presentation::kBulletVisualMaxSegments) {
        const std::size_t drop =
            bulletVisuals_.size() - Presentation::kBulletVisualMaxSegments;
        bulletVisuals_.erase(bulletVisuals_.begin(), bulletVisuals_.begin() + drop);
    }
}

void CS2DGame::UpdateBullets(float dt, AudioManager* audio) {
    // 先让旧段随时间淡出，再加入本帧新段（寿命只在模拟里推进，Render 不消费）
    for (auto& seg : bulletVisuals_) seg.remainingSeconds -= dt;
    bulletVisuals_.erase(
        std::remove_if(bulletVisuals_.begin(), bulletVisuals_.end(),
                       [](const Presentation::BulletVisualSegment& s) {
                           return s.remainingSeconds <= 0.0f;
                       }),
        bulletVisuals_.end());

    for (auto& bullet : bullets_) {
        if (!bullet.active) continue;

        // 本步起点：段的两端都取实际模拟位置，不从速度反推
        const float prevX = bullet.x;
        const float prevY = bullet.y;
        bool segImpact = false;

        const float stepX = bullet.vx * dt;
        const float stepY = bullet.vy * dt;
        bullet.x += stepX;
        bullet.y += stepY;
        bullet.traveled += std::sqrt(stepX * stepX + stepY * stepY);

        if (bullet.traveled >= bullet.maxDist) {
            bullet.active = false;
            AddBulletVisual(bullet, prevX, prevY, bullet.x, bullet.y, false);
            continue;
        }

        int row = 0, col = 0;
        world_->WorldToTile(bullet.x, bullet.y, row, col);
        if (world_->IsBlockingBullet(row, col)) {
            bullet.active = false;
            AddBulletVisual(bullet, prevX, prevY, bullet.x, bullet.y, true);
            continue;
        }

        // ---- 命中 BOT ----
        for (size_t i = 0; i < bots_.size(); ++i) {
            auto& bot = bots_[i];
            if (!bot.alive) continue;
            // 死斗 FFA：任何他人的子弹都能命中；同时排除"自己打自己"的退化情况
            if (bullet.ownerIdx == static_cast<int>(i)) continue;
            if (!IsHostileTeam(bullet.ownerTeam, bot.team)) continue;

            const float dx = bullet.x - bot.x;
            const float dy = bullet.y - bot.y;
            const float hitR = bot.size * 0.5f + 2.0f;
            if (dx * dx + dy * dy >= hitR * hitR) continue;

            bullet.active = false;
            segImpact = true;   // 打中角色 = 实际碰撞终止
            const std::string victimName =
                bot.name.empty() ? ("BOT#" + std::to_string(i)) : bot.name;
            {
                auto& hitAi = AiAt(i);
                hitAi.lastHitTimer = 2.0f;   // 拆包安全判定：最近受击
                if (bullet.ownerIdx == -1) {
                    hitAi.threatX = player_.x;
                    hitAi.threatY = player_.y;
                } else if (bullet.ownerIdx >= 0 &&
                           bullet.ownerIdx < static_cast<int>(bots_.size())) {
                    hitAi.threatX = bots_[static_cast<size_t>(bullet.ownerIdx)].x;
                    hitAi.threatY = bots_[static_cast<size_t>(bullet.ownerIdx)].y;
                }
            }

            // 玩家命中反馈（无论是否致死）
            if (bullet.ownerIdx == -1) hitMarkerTimer_ = 0.18f;

            if (ApplyDamage(bot, bullet.damage, WeaponDB::ArmorPen(bullet.weapon))) {
                Player* bulletKiller = (bullet.ownerIdx == -1)
                    ? &player_
                    : ((bullet.ownerIdx >= 0 &&
                        bullet.ownerIdx < static_cast<int>(bots_.size()))
                       ? &bots_[static_cast<size_t>(bullet.ownerIdx)] : nullptr);
                OnCombatantKilled(bot, audio, bulletKiller);

                // 玩家击杀：计分 + 经济奖励 + 播报
                if (bullet.ownerIdx == -1) {
                    ++playerKills_;
                    ++player_.kills;
                    // 击杀奖励按武器类型发放（照抄 CS2）
                    player_.money += economy_.GrantKill(WeaponDB::KillReward(bullet.weapon));
                    economy_.Clamp(player_.money);
                    PushKillFeed("你  ▸  击杀 " + victimName);
                } else {
                    const std::string killerName =
                        (bullet.ownerIdx >= 0 &&
                         bullet.ownerIdx < static_cast<int>(bots_.size()))
                            ? (bots_[static_cast<size_t>(bullet.ownerIdx)].name.empty()
                                   ? ("BOT#" + std::to_string(bullet.ownerIdx))
                                   : bots_[static_cast<size_t>(bullet.ownerIdx)].name)
                            : std::string("BOT");
                    PushKillFeed(killerName + "  ▸  击杀 " + victimName);
                    // BOT 击杀奖励（同样按武器类型）
                    if (bullet.ownerIdx >= 0 &&
                        bullet.ownerIdx < static_cast<int>(bots_.size())) {
                        bots_[bullet.ownerIdx].money +=
                            economy_.GrantKill(WeaponDB::KillReward(bullet.weapon));
                        economy_.Clamp(bots_[bullet.ownerIdx].money);
                    }
                }

                if (audio) audio->PlaySoundAt(SoundEffect::KILL, bot.x, bot.y,
                                              ListenerX(), ListenerY());
            } else if (audio) {
                audio->PlaySoundAt(SoundEffect::HIT, bot.x, bot.y,
                                   ListenerX(), ListenerY());
            }

            // 受击反应：血量低且当前未在撤退 → 记录威胁来源并撤退一次
            if (AiAt(i).retreatTimer <= 0.0f && bot.health > 0 &&
                bot.health <= Tuning::RetreatHealth) {
                AiAt(i).retreatTimer = Tuning::RetreatDuration;
                if (bullet.ownerIdx == -1) {
                    AiAt(i).threatX = player_.x;
                    AiAt(i).threatY = player_.y;
                } else if (bullet.ownerIdx >= 0 &&
                           bullet.ownerIdx < static_cast<int>(bots_.size())) {
                    AiAt(i).threatX = bots_[bullet.ownerIdx].x;
                    AiAt(i).threatY = bots_[bullet.ownerIdx].y;
                }
                // 受击立刻转向威胁：否则扇形视野下会「背后挨枪还在往前走」
                if (Tuning::BotTurnOnHit > 0.0f) {
                    const float tdx = AiAt(i).threatX - bot.x;
                    const float tdy = AiAt(i).threatY - bot.y;
                    if (tdx * tdx + tdy * tdy > 1.0f) {
                        bot.aimAngle = std::atan2(tdy, tdx);
                    }
                }
            }
            break;
        }

        // ---- 命中玩家 ----
        // 死斗 FFA：BOT 的子弹一律能命中玩家；排除玩家自己的子弹
        if (bullet.active && player_.alive && bullet.ownerIdx != -1 &&
            IsHostileTeam(bullet.ownerTeam, player_.team)) {
            const float dx = bullet.x - player_.x;
            const float dy = bullet.y - player_.y;
            const float hitR = player_.size * 0.5f + 2.0f;
            if (dx * dx + dy * dy < hitR * hitR) {
                bullet.active = false;
                segImpact = true;   // 打中玩家 = 实际碰撞终止
                damageFlashTimer_ = 0.30f;   // 玩家受伤反馈
                if (audio) audio->PlaySound(player_.armor > 0 ? SoundEffect::ARMOR_HIT
                                                              : SoundEffect::HIT);
                if (ApplyDamage(player_, bullet.damage, WeaponDB::ArmorPen(bullet.weapon))) {
                    const std::string killerName =
                        (bullet.ownerIdx >= 0 &&
                         bullet.ownerIdx < static_cast<int>(bots_.size()) &&
                         !bots_[static_cast<size_t>(bullet.ownerIdx)].name.empty())
                            ? bots_[static_cast<size_t>(bullet.ownerIdx)].name
                            : std::string("BOT#") + std::to_string(bullet.ownerIdx);
                    PushKillFeed(killerName + "  ▸  击杀 你");
                    OnCombatantKilled(player_, audio);
                    // BOT 击杀玩家奖励（按武器类型）
                    if (bullet.ownerIdx >= 0 &&
                        bullet.ownerIdx < static_cast<int>(bots_.size())) {
                        bots_[bullet.ownerIdx].money +=
                            economy_.GrantKill(WeaponDB::KillReward(bullet.weapon));
                        economy_.Clamp(bots_[bullet.ownerIdx].money);
                    }
                }
            }
        }

        // 本步的视觉段：命中角色 / 命中玩家都在这里补上，
        // 高速弹当帧失活也能留下可见的一段。
        AddBulletVisual(bullet, prevX, prevY, bullet.x, bullet.y, segImpact);
    }

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [](const Bullet& b) { return !b.active; }),
                   bullets_.end());
}


// ============================================================================
// 手雷
// ============================================================================

void CS2DGame::ThrowGrenade(Player& thrower, float aimAngle, AudioManager* audio) {
    if (thrower.grenadeCount <= 0) return;

    const WeaponId type = thrower.ActiveGrenade();
    const WeaponDef& def = WeaponDB::Get(type);
    if (def.cls != WeaponClass::Grenade) return;

    Grenade g;
    g.x = thrower.x;
    g.y = thrower.y;
    // 摩擦 2.0/s → 投掷距离 ≈ 速度/2；420 约 210px
    g.vx = std::cos(aimAngle) * 420.0f;
    g.vy = std::sin(aimAngle) * 420.0f;
    g.fuse = 1.4f;
    g.type = type;
    g.team = thrower.team;
    g.active = true;
    grenades_.push_back(g);

    // 每个投掷物一局只能用一次：用掉就从背包移除
    thrower.ConsumeActiveGrenade();

    // 最后一颗投掷物用完后，自动回到切投掷物之前的武器槽
    if (&thrower == &player_ && thrower.weaponSlot == 3 && thrower.grenadeCount <= 0) {
        int back = thrower.prevWeaponSlot;
        if (back < 0 || back > 2 || thrower.weapons[back] == WeaponId::None) {
            back = -1;
            for (int s = 0; s <= 2; ++s) {
                if (thrower.weapons[s] != WeaponId::None) { back = s; break; }
            }
        }
        if (back >= 0) SwitchWeaponSlot(back);
    }

    // 投掷动作本身会出声（附近敌人能听到，见 Tuning::HearThrowRadius）
    {
        const int idx = (&thrower == &player_)
            ? -1
            : static_cast<int>(&thrower - bots_.data());
        EmitSound(SoundEventType::GrenadeThrow, thrower.x, thrower.y, thrower.team, idx);
    }

    // 出手音效：按投掷物类型选择，且与投掷动作在同一帧
    if (audio) {
        SoundEffect se = SoundEffect::GRENADE_THROW;
        if (type == WeaponId::GrenadeHE)         se = SoundEffect::THROW_HE;
        else if (type == WeaponId::GrenadeFlash)  se = SoundEffect::THROW_FLASH;
        else if (type == WeaponId::GrenadeSmoke)  se = SoundEffect::THROW_SMOKE;

        // 玩家自己的投掷用 2D 播放，保证一定能听到（不受距离衰减影响）
        if (&thrower == &player_) audio->PlaySound(se);
        else audio->PlaySoundAt(se, thrower.x, thrower.y, ListenerX(), ListenerY());
    }
}

void CS2DGame::UpdateGrenades(float dt, AudioManager* audio) {
    // 投掷物飞行 + 撞墙反弹 + 引信
    for (auto& g : grenades_) {
        if (!g.active) continue;

        g.fuse -= dt;
        if (g.bounceHearCooldown > 0.0f) g.bounceHearCooldown -= dt;

        // 子步进：每步不超过半个 tile，避免高速穿过薄墙
        const float speed = std::sqrt(g.vx * g.vx + g.vy * g.vy);
        const float halfTile = static_cast<float>(world_->GetTileWidth()) * 0.5f;
        int steps = 1;
        if (speed * dt > halfTile) {
            steps = static_cast<int>(std::ceil(speed * dt / halfTile));
        }
        steps = std::min(steps, 16);
        const float sub = dt / static_cast<float>(steps);

        for (int st = 0; st < steps; ++st) {
            if (g.vx == 0.0f && g.vy == 0.0f) break;

            const float oldX = g.x, oldY = g.y;
            const float nx = oldX + g.vx * sub;
            const float ny = oldY + g.vy * sub;

            // 分轴判定：只把「被墙挡住」的分量反弹，切向速度保留（贴着墙滑行）。
            // 例：斜着撞到竖直墙面 → vx 反转并按恢复系数衰减，vy 不变。
            bool hitX = false, hitY = false;
            int row = 0, col = 0;
            if (g.vx != 0.0f) {
                world_->WorldToTile(nx, oldY, row, col);
                hitX = world_->IsBlockingBullet(row, col);
                if (!hitX) g.x = nx;
            }
            if (g.vy != 0.0f) {
                world_->WorldToTile(g.x, ny, row, col);
                hitY = world_->IsBlockingBullet(row, col);
                if (!hitY) g.y = ny;
            }
            // 斜向穿角：两个单轴都可行，但对角格是墙 → 回退并双轴反弹
            if (!hitX && !hitY && g.vx != 0.0f && g.vy != 0.0f) {
                world_->WorldToTile(nx, ny, row, col);
                if (world_->IsBlockingBullet(row, col)) {
                    g.x = oldX;
                    g.y = oldY;
                    hitX = hitY = true;
                }
            }

            if (hitX || hitY) {
                const float before = std::sqrt(g.vx * g.vx + g.vy * g.vy);
                ApplyNadeBounce(g.vx, g.vy, hitX, hitY,
                                Tuning::NadeBounceRestitution,
                                Tuning::NadeMinBounceSpeed);
                const float after = std::sqrt(g.vx * g.vx + g.vy * g.vy);
                ++g.wallBounces;

                // 撞墙同样会发出听觉信息（附近敌人能据此判断投掷物位置）
                if (g.bounceHearCooldown <= 0.0f) {
                    g.bounceHearCooldown = Tuning::NadeBounceHearCooldown;
                    EmitSound(SoundEventType::GrenadeBounce, g.x, g.y, g.team);
                    std::cout << "[Nade] 撞墙反弹 (" << static_cast<int>(g.x) << ","
                              << static_cast<int>(g.y) << ") 速度 "
                              << static_cast<int>(before) << " → "
                              << static_cast<int>(after) << std::endl;
                }
            }
        }

        // 地面摩擦（指数衰减；v0/friction ≈ 总投掷距离）
        // 飞行时缓慢自转（停下后保持角度）
        const float spinSpeed = std::sqrt(g.vx * g.vx + g.vy * g.vy);
        if (spinSpeed > 8.0f) g.spin = std::fmod(g.spin + spinSpeed * dt * 1.4f, 360.0f);

        g.vx -= g.vx * 2.0f * dt;
        g.vy -= g.vy * 2.0f * dt;
        g.x = std::clamp(g.x, 0.0f, world_->GetMapPixelW() - 1.0f);
        g.y = std::clamp(g.y, 0.0f, world_->GetMapPixelH() - 1.0f);

        // 落点预警：速度降下来（即将停住）时广播一次位置，附近敌人会躲开落点。
        // 阈值取 200 而不是几十，是为了给反应留出约 1 秒的窗口。
        if (!g.landingWarned) {
            const float spd2 = g.vx * g.vx + g.vy * g.vy;
            if (spd2 < 200.0f * 200.0f) {
                g.landingWarned = true;
                EmitSound(SoundEventType::GrenadeBounce, g.x, g.y, g.team);
                std::cout << "[Nade] 落点 (" << static_cast<int>(g.x) << ","
                          << static_cast<int>(g.y) << ") → 落点信息已广播"
                          << "（反弹 " << g.wallBounces << " 次）" << std::endl;
            }
        }

        if (g.fuse <= 0.0f) {
            ExplodeGrenade(g, audio);
            g.active = false;
        }
    }
    grenades_.erase(std::remove_if(grenades_.begin(), grenades_.end(),
                                   [](const Grenade& g) { return !g.active; }),
                    grenades_.end());

    // 烟雾云寿命
    for (auto& s : smokes_) { s.life -= dt; s.age += dt; }
    smokes_.erase(std::remove_if(smokes_.begin(), smokes_.end(),
                                 [](const SmokeCloud& s) { return s.life <= 0.0f; }),
                  smokes_.end());

    // 火区寿命 + 持续伤害（燃烧弹 / 燃烧瓶）
    for (auto& f : fires_) { f.life -= dt; f.age += dt; }
    fires_.erase(std::remove_if(fires_.begin(), fires_.end(),
                              [](const FireZone& f) { return f.life <= 0.0f; }),
                 fires_.end());

    fireTickTimer_ -= dt;
    if (fireTickTimer_ <= 0.0f) {
        fireTickTimer_ = Tuning::FireTickInterval;
        auto burn = [&](Player& p) {
            if (!p.alive) return;
            for (const auto& f : fires_) {
                const float dx = p.x - f.x;
                const float dy = p.y - f.y;
                if (dx * dx + dy * dy <= f.radius * f.radius) {
                    ApplyDamage(p, Tuning::FireTickDamage, 1.0f);   // 火区无视护甲（CS2 同）
                    break;
                }
            }
        };
        burn(player_);
        for (auto& b : bots_) burn(b);
    }

    // 闪光致盲衰减
    if (flashTimer_ > 0.0f) {
        flashTimer_ -= dt;
        if (flashTimer_ <= 0.0f) {
            flashTimer_ = 0.0f;
            flashPeak_  = 0.0f;
        }
    }
}

void CS2DGame::ExplodeGrenade(const Grenade& g, AudioManager* audio) {
    switch (g.type) {
        case WeaponId::GrenadeHE: {
            const float RADIUS  = Tuning::NadeBlastRadius;
            const float MAX_DMG = Tuning::NadeMaxDamage;

            auto damageAt = [&](Player& p) {
                if (!p.alive) return;   // 道具对所有人（含己方与投掷者）生效
                const float dx = p.x - g.x;
                const float dy = p.y - g.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > RADIUS) return;
                // 平方衰减：贴近中心才有高伤害，边缘伤害很低
                const float t = 1.0f - d / RADIUS;
                const float falloff = t * t;
                if (ApplyDamage(p, static_cast<int>(MAX_DMG * falloff), 0.5f)) {   // 爆炸按 50% 穿透
                    OnCombatantKilled(p, audio);
                }
            };

            damageAt(player_);
            for (auto& b : bots_) damageAt(b);

            // 爆炸特效（供 Render 画扩散圆环）
            blasts_.push_back(BlastFx{ g.x, g.y, Tuning::NadeBlastFxTime,
                                       Tuning::NadeBlastFxTime });

            if (audio) audio->PlaySound(SoundEffect::GRENADE_EXPLODE);
            EmitSound(SoundEventType::Explosion, g.x, g.y, g.team);
            break;
        }
        case WeaponId::GrenadeFlash: {
            if (audio) audio->PlaySound(SoundEffect::FLASH);
            EmitSound(SoundEventType::Flashbang, g.x, g.y, g.team);
            // 致盲：距离越近越久（平方衰减）+ 墙体遮挡
            // 返回 {致盲秒数, 峰值强度}，强度用于控制白屏有多白
            auto flashAt = [&](const Player& t) -> std::pair<float, float> {
                if (!t.alive) return {0.0f, 0.0f};

                const float dx = t.x - g.x;
                const float dy = t.y - g.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d >= Tuning::FlashRadius) return {0.0f, 0.0f};

                // 墙后看不到闪光：不闪（避免隔墙致盲）
                if (renderer_ && !renderer_->HasLineOfSight(g.x, g.y, t.x, t.y)) {
                    return {0.0f, 0.0f};
                }

                return { ComputeFlashBlind(d), ComputeFlashIntensity(d) };
            };

            if (player_.alive) {
                const auto [blind, inten] = flashAt(player_);
                if (blind > 0.0f) {
                    flashTimer_ = std::max(flashTimer_, blind);
                    flashPeak_  = std::max(flashPeak_, inten);
                }
            }
            for (size_t bi = 0; bi < bots_.size(); ++bi) {
                auto& b = bots_[bi];
                if (!b.alive) continue;
                const auto [blind, inten] = flashAt(b);
                (void)inten;
                if (blind > 0.0f) {
                    AiAt(bi).blindTimer = std::max(AiAt(bi).blindTimer, blind);
                }
            }
            if (audio) audio->PlaySound(SoundEffect::HIT);
            break;
        }
        case WeaponId::GrenadeSmoke: {
            SmokeCloud s;
            s.x = g.x;
            s.y = g.y;
            s.radius = 70.0f;
            s.maxLife = 12.0f;
            s.age = 0.0f;
            s.expandTime = 1.6f;
            s.seed = std::fmod(g.x * 0.013f + g.y * 0.017f, 1.0f);
            s.life = 12.0f;
            smokes_.push_back(s);
            if (audio) audio->PlaySound(SoundEffect::SMOKE);
            EmitSound(SoundEventType::SmokePop, g.x, g.y, g.team);
            break;
        }
        case WeaponId::GrenadeMolotov:
        case WeaponId::GrenadeIncendiary: {
            FireZone f;
            f.x = g.x;
            f.y = g.y;
            f.radius = Tuning::FireRadius;
            f.life = Tuning::FireLife;
            f.maxLife = Tuning::FireLife;
            f.age = 0.0f;
            f.seed = std::fmod(g.x * 0.011f + g.y * 0.019f, 1.0f);
            fires_.push_back(f);
            if (audio) audio->PlaySound(SoundEffect::FIRE_IGNITE);
            EmitSound(SoundEventType::Explosion, g.x, g.y, g.team);
            break;
        }
        case WeaponId::GrenadeDecoy: {
            if (audio) audio->PlaySound(SoundEffect::DECOY_EXPLODE);
            EmitSound(SoundEventType::Explosion, g.x, g.y, g.team);
            break;
        }
        default:
            break;
    }
}

// 线段是否被任一烟雾云遮挡（用于视野判定）
bool CS2DGame::SegmentBlockedBySmoke(float x1, float y1, float x2, float y2) const {
    if (smokes_.empty()) return false;

    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float len2 = dx * dx + dy * dy;
    if (len2 < 0.0001f) return false;

    for (const auto& s : smokes_) {
        // 点到线段最短距离，与烟雾半径比较
        float t = ((s.x - x1) * dx + (s.y - y1) * dy) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
        const float px = x1 + dx * t;
        const float py = y1 + dy * t;
        const float ddx = s.x - px;
        const float ddy = s.y - py;
        if (ddx * ddx + ddy * ddy <= s.radius * s.radius) return true;
    }
    return false;
}

// ============================================================================
// 死亡处理
// ============================================================================

void CS2DGame::OnCombatantKilled(Player& victim, AudioManager* audio, Player* killer) {
    if (!victim.alive) return;

    victim.alive = false;
    victim.health = 0;
    victim.deathTimer = 0.0f;
    victim.vx = victim.vy = 0.0f;
    victim.reloading = false;
    ++victim.deaths;

    // 玩家阵亡 → 开镜立即归零（不依赖 game.Update 的 dt）
    if (&victim == &player_) {
        ResetScope(Presentation::ScopeResetReason::PlayerDied);
    }

    // 倒地也会出声：附近单位听到后转向警戒（队友还会据此补枪）
    EmitSound(SoundEventType::Death, victim.x, victim.y, victim.team,
              (&victim == &player_) ? -1 : static_cast<int>(&victim - bots_.data()));

    // 击杀归属（自己炸自己 / 环境伤害不计入）
    // 玩家阵亡 → 进入观察者模式
    if (&victim == &player_) SpectatorBegin();

    // 敌方阵亡位置也是一种情报（会衰减）
    if (victim.team != player_.team) {
        intel_.OnEnemySeen(victim.x, victim.y, clockSec_);
    }

    if (killer && killer != &victim) {
        ++killer->kills;
        if (killer == &player_) ++playerKills_;
    }

    // C4 携带者阵亡 → C4 掉在原地
    if (mode_ == MatchMode::BombDefusal && bombCarrier_ >= -1) {
        if ((&victim == &player_ && bombCarrier_ == -1) ||
            (bombCarrier_ >= 0 && bombCarrier_ < static_cast<int>(bots_.size()) &&
             &victim == &bots_[static_cast<size_t>(bombCarrier_)])) {
            DropC4(victim.x, victim.y);
        }
    }

    // 阵亡掉落：主武器 → 副武器（仅非标配） → 随机一个投掷物
    DropEquipmentOnDeath(victim);

    // 教练：记录阵亡点 + 双方减员事件
    {
        const std::string area = world_ ? world_->FindAreaAt(victim.x, victim.y) : std::string();
        for (Team t : {Team::T, Team::CT}) {
            RoundMemo::Death d;
            d.team  = victim.team;
            d.area  = area;
            d.x     = victim.x;
            d.y     = victim.y;
            d.atSec = clockSec_;
            CoachFor(t).deaths.push_back(d);
            CoachOnEvent(t, victim.team == t ? CoachEventType::AllyDown
                                             : CoachEventType::EnemyDown, area);
        }
        if (&victim == &player_) {
            CoachOnEvent(player_.team, CoachEventType::HumanDied, area);
            CoachOnEvent(TeamManager::Opponent(player_.team), CoachEventType::HumanDied, area);
        }
    }

    // 记录阵亡点：队友会尝试「补枪」（trade）
    lastDeathX_      = victim.x;
    lastDeathY_      = victim.y;
    lastDeathTeam_   = victim.team;
    lastDeathTimer_  = 3.5f;

    if (&victim == &player_) {
        ++playerDeaths_;
        if (audio) audio->PlaySound(SoundEffect::KILL);
        if (mode_ == MatchMode::DeathMatch) botRespawnTimer_ = BOT_RESPAWN_DELAY;
        std::cout << "[CS2D] 玩家阵亡 (累计 " << playerDeaths_ << ")" << std::endl;
    } else {
        // BOT 阵亡由击杀方计分；此处只需统计自身
        std::cout << "[CS2D] BOT 阵亡 (" << TeamName(victim.team) << ")" << std::endl;
    }
}

// ============================================================================
// 统一伤害入口
// ============================================================================

// ============================================================================
// 伤害结算（契约 v1.2）
// CS2 机制：护甲按武器的穿透率放行一部分伤害，剩余被护甲吸收，
// 并按吸收量的一半消耗护甲点数；护甲不够时没吸掉的部分转成伤害。
// ============================================================================
CS2DGame::ArmorDamageResult CS2DGame::ResolveArmorDamage(int amount, float armorPen,
                                                         int armorPoints) {
    ArmorDamageResult r;
    if (amount <= 0) return r;

    r.healthDamage = amount;
    if (armorPoints <= 0) return r;                 // 没甲：全额进血

    const float pen = std::clamp(armorPen, 0.0f, 1.0f);
    int through = static_cast<int>(std::lround(static_cast<float>(amount) * pen));
    through = std::clamp(through, 0, amount);
    const int absorbed = amount - through;
    int armorLoss = (absorbed + 1) / 2;             // CS2：护甲值按被吸收量的一半掉

    // 护甲不够：没吸收掉的部分按剩余护甲比例转成伤害
    if (armorLoss > armorPoints) {
        const float keep = static_cast<float>(armorPoints) / static_cast<float>(armorLoss);
        armorLoss = armorPoints;
        through = amount - static_cast<int>(std::lround(static_cast<float>(absorbed) * keep));
        through = std::clamp(through, 0, amount);
    }

    r.healthDamage = through;
    r.armorLoss    = armorLoss;
    return r;
}

bool CS2DGame::ApplyDamage(Player& victim, int amount, float armorPen) {
    if (!victim.alive || amount <= 0) return false;

    const ArmorDamageResult r = ResolveArmorDamage(amount, armorPen, victim.armor);
    victim.armor = std::max(0, victim.armor - r.armorLoss);
    if (victim.armor <= 0) victim.helmet = false;   // 护甲耗尽，头盔失效

    victim.health -= r.healthDamage;
    if (victim.health <= 0) {
        victim.health = 0;
        return true;
    }
    return false;
}

// ============================================================================
// 换弹 / 视野 / 安全距离
// ============================================================================

void CS2DGame::StartReload(Player& p, AudioManager* audio) {
    if (p.reloading) return;

    const WeaponDef& def = p.CurrentWeapon();
    if (def.magazine <= 0) return;          // 刀/投掷物不换弹
    if (p.ammo >= p.maxAmmo) return;        // 弹匣已满
    if (p.reserve <= 0) return;             // 没有备弹，无法换弹

    p.reloading   = true;
    p.reloadTimer = def.reloadTime;         // 每把枪不同，取自素材时长
    // 换弹开始 → 开镜归零（玩家本体，BOT 无开镜状态）
    if (&p == &player_) ResetScope(Presentation::ScopeResetReason::ReloadStarted);

    // 换弹声会暴露位置：附近敌人能听见（约 220px），可趁机前压
    {
        const int idx = (&p == &player_)
            ? -1
            : static_cast<int>(&p - bots_.data());
        EmitSound(SoundEventType::Reload, p.x, p.y, p.team, idx);
    }
    if (audio) {
        // 玩家自己的换弹用非位置音：走 3D 衰减/声像容易出现"听不见"
        // （被动换弹紧跟连射之后、声源与听者几乎重合时声像会被拉偏）。
        if (&p == &player_) {
            audio->PlaySound(ReloadSoundFor(def.id));
        } else {
            audio->PlaySoundAt(ReloadSoundFor(def.id), p.x, p.y, ListenerX(), ListenerY());
        }
    }
}

// 武器 → 换弹音效（新增武器回退到最接近的已有音效，后续替换真实素材）
SoundEffect CS2DGame::ReloadSoundFor(WeaponId id) {
    switch (id) {
        // 手枪
        case WeaponId::Glock18:      return SoundEffect::RELOAD_GLOCK;
        case WeaponId::USP_S:
        case WeaponId::P2000:        return SoundEffect::RELOAD_USP;
        case WeaponId::Deagle:
        case WeaponId::R8Revolver:   return SoundEffect::RELOAD_DEAGLE;
        case WeaponId::P250:
        case WeaponId::FiveSeven:
        case WeaponId::Tec9:
        case WeaponId::CZ75:
        case WeaponId::DualBerettas: return SoundEffect::RELOAD_GLOCK;

        // 冲锋枪
        case WeaponId::MAC10:
        case WeaponId::MP9:
        case WeaponId::MP7:
        case WeaponId::MP5SD:
        case WeaponId::UMP45:
        case WeaponId::P90:          return SoundEffect::RELOAD_GALIL;

        // 步枪
        case WeaponId::AK47:
        case WeaponId::SG553:        return SoundEffect::RELOAD_AK47;
        case WeaponId::M4A4:
        case WeaponId::M4A1S:
        case WeaponId::AUG:          return SoundEffect::RELOAD_M4A1;
        case WeaponId::GalilAR:
        case WeaponId::FAMAS:        return SoundEffect::RELOAD_GALIL;

        // 狙击枪
        case WeaponId::AWP:
        case WeaponId::SSG08:
        case WeaponId::SCAR20:
        case WeaponId::G3SG1:        return SoundEffect::RELOAD_AWP;

        // 霰弹枪
        case WeaponId::Nova:
        case WeaponId::XM1014:
        case WeaponId::SawedOff:
        case WeaponId::MAG7:         return SoundEffect::RELOAD_SHOTGUN;

        // 机枪
        case WeaponId::M249:
        case WeaponId::Negev:        return SoundEffect::RELOAD_AK47;

        default:                     return SoundEffect::CLICK;
    }
}

void CS2DGame::UpdateReload(Player& p, float dt) {
    if (!p.reloading) return;
    p.reloadTimer -= dt;
    if (p.reloadTimer <= 0.0f) {
        p.reloading = false;
        p.reloadTimer = 0.0f;
        // 从备弹里取出子弹补满弹匣（备弹不够就装多少算多少）
        const int need = std::max(0, p.maxAmmo - p.ammo);
        const int take = std::min(need, p.reserve);
        p.ammo    += take;
        p.reserve -= take;
        p.SaveAmmoToSlot();
    }
}

bool CS2DGame::IsEnemyNearby(float x, float y, float safeRadius, Team myTeam) const {
    auto near = [&](const Player& u) {
        if (!u.alive || u.team == myTeam) return false;
        const float dx = u.x - x;
        const float dy = u.y - y;
        return dx * dx + dy * dy < safeRadius * safeRadius;
    };

    if (near(player_)) return true;
    for (const auto& bot : bots_) if (near(bot)) return true;
    return false;
}



// ============================================================================
// 掉落武器 / 拾取
// ============================================================================

void CS2DGame::DropEquipmentOnDeath(const Player& victim) {
    WeaponId dropId = WeaponId::None;
    bool isGrenade = false;
    bool fromPrimary = false;
    int  dropMag = 0, dropReserve = 0;

    // 优先级 1：主武器
    if (victim.weapons[0] != WeaponId::None) {
        dropId = victim.weapons[0];
        fromPrimary = true;
    } else {
        // 优先级 2：副武器（起始手枪不掉落）
        const bool isT = (victim.team == Team::T);
        if (victim.weapons[1] != WeaponId::None &&
            !WeaponDB::IsDefaultPistol(victim.weapons[1], isT)) {
            dropId = victim.weapons[1];
        } else if (victim.grenadeCount > 0) {
            // 优先级 3：随机掉一个投掷物
            const int idx = static_cast<int>(rng_() % static_cast<unsigned>(victim.grenadeCount));
            dropId = victim.grenades[idx];
            isGrenade = true;
        }
    }

    if (dropId == WeaponId::None) return;

    // 记录丢弃瞬间该武器的弹药：主武器可能正被手持（实时值），否则读它自己的槽位
    if (!isGrenade) {
        const int slot = fromPrimary ? 0 : 1;
        if (victim.weaponSlot == slot) {
            dropMag     = victim.ammo;
            dropReserve = victim.reserve;
        } else {
            dropMag     = victim.ammoSlot[slot];
            dropReserve = victim.reserveSlot[slot];
        }
    }

    DroppedItem it;
    it.id = dropId;
    it.isGrenade = isGrenade;
    it.life = 30.0f;
    it.hasAmmoState = !isGrenade;
    it.magAmmo      = dropMag;
    it.reserveAmmo  = dropReserve;
    // 随机平铺朝向 + 少量抖动，避免多件物品完全重叠
    std::uniform_real_distribution<float> ang(0.0f, 360.0f);
    std::uniform_real_distribution<float> jitter(-5.0f, 5.0f);
    it.x   = victim.x + jitter(rng_);
    it.y   = victim.y + jitter(rng_);
    it.rot = ang(rng_);
    it.pickupDelay = 0.8f;   // 丢下后 0.8 秒内不能捡回
    drops_.push_back(it);

    std::cout << "[Drop] " << TeamName(victim.team) << " 掉落 "
              << WeaponDB::Get(dropId).name
              << (isGrenade ? "" : (" 弹匣 " + std::to_string(dropMag)
                                    + " 备弹 " + std::to_string(dropReserve)))
              << " @(" << static_cast<int>(victim.x) << ","
              << static_cast<int>(victim.y) << ")" << std::endl;
}

void CS2DGame::TryPickupDrops(bool manual) {
    if (!player_.alive) return;

    // 找最近的掉落物
    int   best = -1;
    float bestD2 = (manual ? 30.0f * 30.0f : 16.0f * 16.0f);
    for (size_t i = 0; i < drops_.size(); ++i) {
        if (drops_[i].id == WeaponId::None) continue;
        if (drops_[i].pickupDelay > 0.0f) continue;   // 冷却中
        const float dx = drops_[i].x - player_.x;
        const float dy = drops_[i].y - player_.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(i); }
    }
    if (best < 0) return;

    DroppedItem& d = drops_[static_cast<size_t>(best)];
    const WeaponDef& def = WeaponDB::Get(d.id);
    bool taken = false;

    if (d.isGrenade) {
        const int limit = (d.id == WeaponId::GrenadeFlash) ? 2 : 1;
        if (player_.grenadeCount < Player::MAX_GRENADES &&
            player_.CountGrenade(d.id) < limit) {
            player_.grenades[player_.grenadeCount++] = d.id;
            player_.SyncActiveGrenade();
            taken = true;
        }
    } else if (def.slot == WeaponSlotKind::Primary) {
        // 自动拾取只在空槽时发生，按住 E 可以替换
        if (manual || player_.weapons[0] == WeaponId::None) {
            // 手上有旧主武器且不是同一把 → 先丢到地上
            if (player_.weapons[0] != WeaponId::None && player_.weapons[0] != d.id) {
                DropWeaponToGround(0, player_.x, player_.y);
            }
            // 捡起时沿用原持有者丢弃瞬间的弹药状态
            if (d.hasAmmoState) {
                player_.SetSlotWeaponWithAmmo(0, d.id, d.magAmmo, d.reserveAmmo);
            } else {
                player_.SetSlotWeapon(0, d.id);
            }
            if (player_.weaponSlot == 0) {
                player_.LoadAmmoFromSlot();
            }
            player_.reloading   = false;
            player_.reloadTimer = 0.0f;
            taken = true;
        }
    } else if (def.slot == WeaponSlotKind::Secondary) {
        if (manual || player_.weapons[1] == WeaponId::None) {
            if (player_.weapons[1] != WeaponId::None && player_.weapons[1] != d.id) {
                DropWeaponToGround(1, player_.x, player_.y);
            }
            if (d.hasAmmoState) {
                player_.SetSlotWeaponWithAmmo(1, d.id, d.magAmmo, d.reserveAmmo);
            } else {
                player_.SetSlotWeapon(1, d.id);
            }
            if (player_.weaponSlot == 1) {
                player_.LoadAmmoFromSlot();
            }
            player_.reloading   = false;
            player_.reloadTimer = 0.0f;
            taken = true;
        }
    }

    if (taken) d.id = WeaponId::None;
}

// G：丢弃当前武器（近战不可丢弃）
// 把某个槽位的枪丢到地面（换枪/被替换时调用，避免旧枪凭空消失）
void CS2DGame::DropWeaponToGround(int slot, float x, float y) {
    if (slot < 0 || slot > 4) return;
    const WeaponId id = player_.weapons[slot];
    if (id == WeaponId::None) return;

    // 丢弃瞬间的弹药：正被手持的槽用实时值，否则用它自己的缓存
    int magAmmo = 0, reserveAmmo = 0;
    if (player_.weaponSlot == slot) {
        magAmmo     = player_.ammo;
        reserveAmmo = player_.reserve;
    } else {
        magAmmo     = player_.ammoSlot[slot];
        reserveAmmo = player_.reserveSlot[slot];
    }

    DroppedItem it;
    it.id = id;
    it.isGrenade = false;
    it.hasAmmoState = true;
    it.magAmmo      = magAmmo;
    it.reserveAmmo  = reserveAmmo;
    std::uniform_real_distribution<float> ang(0.0f, 360.0f);
    std::uniform_real_distribution<float> jitter(-6.0f, 6.0f);
    it.x = x + jitter(rng_);
    it.y = y + jitter(rng_);
    it.rot = ang(rng_);
    it.pickupDelay = 0.8f;
    drops_.push_back(it);

    std::cout << "[Drop] 换下 " << WeaponDB::Get(id).name
              << " 弹匣 " << magAmmo << " 备弹 " << reserveAmmo << std::endl;
}

void CS2DGame::DropCurrentWeapon() {
    // 槽位 4 = C4：G 键丢的就是 C4 本身
    if (player_.weaponSlot == 4) {
        if (mode_ == MatchMode::BombDefusal && bombCarrier_ == -1 && !bombPlanted_) {
            DropC4(player_.x, player_.y);
        } else {
            // 已经下包 / C4 已丢失：只清空槽位，避免卡在空槽
            player_.weapons[4] = WeaponId::None;
            playerHoldingC4_ = false;
            EquipFallbackWeapon();
        }
        return;
    }
    // 手持 C4（旧路径保留）
    if (playerHoldingC4_ && PlayerHasC4()) {
        DropC4(player_.x, player_.y);
        return;
    }
    if (!player_.alive) return;

    const int slot = player_.weaponSlot;
    if (slot == 2) return;                     // 近战不能丢

    WeaponId dropId = WeaponId::None;
    bool isGrenade = false;
    int  dropMag = 0, dropReserve = 0;

    if (slot == 3) {
        dropId = player_.ActiveGrenade();
        if (dropId == WeaponId::None) return;
        isGrenade = true;
        player_.ConsumeActiveGrenade();
        // 没有投掷物了：回到有枪的槽位
        if (player_.grenadeCount <= 0) {
            if (player_.weapons[0] != WeaponId::None)      player_.weaponSlot = 0;
            else if (player_.weapons[1] != WeaponId::None) player_.weaponSlot = 1;
            else                                           player_.weaponSlot = 2;
            player_.LoadAmmoFromSlot();
        }
    } else {
        dropId = player_.weapons[slot];
        if (dropId == WeaponId::None) return;
        // 丢弃瞬间的弹药（必须在 ClearSlotWeapon 之前取）
        if (player_.weaponSlot == slot) {
            dropMag     = player_.ammo;
            dropReserve = player_.reserve;
        } else {
            dropMag     = player_.ammoSlot[slot];
            dropReserve = player_.reserveSlot[slot];
        }
        player_.ClearSlotWeapon(slot);

        // 主武器丢了 → 自动换成副武器，再不行换近战
        if (slot == 0) {
            if (player_.weapons[1] != WeaponId::None) { player_.weaponSlot = 1; }
            else                                       { player_.weaponSlot = 2; }
            player_.LoadAmmoFromSlot();
        } else if (slot == 1) {
            player_.weaponSlot = 2;
            player_.LoadAmmoFromSlot();
        }
    }

    DroppedItem it;
    it.id = dropId;
    it.isGrenade = isGrenade;
    it.hasAmmoState = !isGrenade;
    it.magAmmo      = dropMag;
    it.reserveAmmo  = dropReserve;
    std::uniform_real_distribution<float> ang(0.0f, 360.0f);
    std::uniform_real_distribution<float> jitter(-5.0f, 5.0f);
    it.x   = player_.x + jitter(rng_);
    it.y   = player_.y + jitter(rng_);
    it.rot = ang(rng_);
    it.pickupDelay = 0.8f;   // 丢下后 0.8 秒内不能捡回
    drops_.push_back(it);

    std::cout << "[Drop] 玩家丢弃 " << WeaponDB::Get(dropId).name;
    if (!isGrenade) {
        std::cout << " 弹匣 " << dropMag << " 备弹 " << dropReserve;
    }
    std::cout << std::endl;
    // 丢枪 → 开镜归零
    ResetScope(Presentation::ScopeResetReason::WeaponChanged);
}

