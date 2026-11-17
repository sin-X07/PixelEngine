#include "CS2DGame.hpp"
#include "../Audio/AudioManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>

// ============================================================================
// 目标物：爆破（下包/拆包）
// ============================================================================

namespace {
constexpr float BOMB_FUSE        = 40.0f;   // 下包后倒计时
constexpr float PLANT_TIME       = 3.0f;    // T 在包点停留多久完成下包
constexpr float DEFUSE_TIME      = 10.0f;    // CT 拆包耗时
} // namespace

// 记分板数据（玩家 + 全部 BOT，按阵营排序）
std::vector<CS2DGame::ScoreRow> CS2DGame::BuildScoreboard() const {
    std::vector<ScoreRow> rows;

    ScoreRow me;
    me.name      = "你";
    me.team      = player_.team;
    me.kills     = player_.kills;
    me.deaths    = player_.deaths;
    me.money     = player_.money;
    me.alive     = player_.alive;
    me.isPlayer  = true;
    rows.push_back(me);

    for (size_t i = 0; i < bots_.size(); ++i) {
        const auto& b = bots_[i];
        ScoreRow r;
        r.name   = b.name.empty() ? ("BOT#" + std::to_string(i)) : b.name;
        r.team   = b.team;
        r.kills  = b.kills;
        r.deaths = b.deaths;
        r.money  = b.money;
        r.alive  = b.alive;
        rows.push_back(r);
    }

    std::stable_sort(rows.begin(), rows.end(), [](const ScoreRow& a, const ScoreRow& b) {
        if (a.team != b.team) return static_cast<int>(a.team) > static_cast<int>(b.team);
        return a.kills > b.kills;
    });
    return rows;
}

// 按模式初始化目标物
void CS2DGame::SetupObjective() {
    c4EquipmentId_=mode_==MatchMode::BombDefusal ? nextEquipmentId_++ : 0;
    AssignBombCarrier();
    bombPlanted_   = false;
    bombTimer_     = 0.0f;
    StopObjectiveAudio(nullptr);
    defuseProgress_ = 0.0f;

    // 每回合必须清掉上一局遗留的 C4 掉落 / 情报 / 拾取冷却，
    // 否则上一局的掉落点会「继承」到新回合
    bombDropped_    = false;
    bombDropDelay_  = 0.0f;
    bombDropX_ = bombDropY_ = 0.0f;
    bombIntel_      = false;
    bombIntelAge_   = 0.0f;
    bombIntelX_ = bombIntelY_ = 0.0f;
    playerHoldingC4_ = false;
    player_.weapons[4] = WeaponId::None;
    player_.equipment[4]={};
    if (player_.weaponSlot == 4) player_.weaponSlot = 1;
    bombX_ = bombY_ = 0.0f;
}

// 每帧推进目标物（仅交战期）
void CS2DGame::UpdateObjective(float dt, bool interactHeld, AudioManager* audio) {
    if (mode_ != MatchMode::BombDefusal || !round_.IsLive()) {
        StopObjectiveAudio(audio);
        return;
    }
    const bool planting = player_.alive && !bombPlanted_ && PlayerHasC4()
        && interactHeld && world_ && world_->FindBombSiteAt(player_.x, player_.y);
    if (planting) {
        if (plantProgress_ == 0.0f && audio)
            plantingChannel_ = audio->PlaySoundTracked(SoundEffect::BOMB_PLANTING, 1);
        plantProgress_ += dt;
        if (plantProgress_ >= PLANT_TIME) {
            PlantBomb(player_.x, player_.y, audio);
            plantProgress_ = 0.0f;
        }
    } else {
        plantProgress_ = 0.0f;
        if (audio) audio->StopChannel(plantingChannel_);
    }

    if (mode_ == MatchMode::BombDefusal) {
        if (!bombPlanted_) {
            // 未下包：清掉所有 BOT 的拆包状态
            for (size_t bi = 0; bi < bots_.size(); ++bi) AiAt(bi).defusing = false;

            // T 站在包点内累积下包进度
            // T 只有「C4 携带者」本人能在包点安装
            bool carrierInSite = false;

            if (!bombDropped_ && world_ &&
                bombCarrier_ >= 0 && bombCarrier_ < static_cast<int>(bots_.size())) {
                const Player& carrier = bots_[static_cast<size_t>(bombCarrier_)];
                if (carrier.alive && carrier.team == Team::T) {
                    if (world_->FindBombSiteAt(carrier.x, carrier.y)) {
                        carrierInSite = true;
                        bombX_ = carrier.x;
                        bombY_ = carrier.y;
                    }
                }
            }

            if (carrierInSite) {
                botPlantProgress_ += dt;
                if (botPlantProgress_ >= PLANT_TIME) {
                    PlantBomb(bombX_, bombY_, audio);
                    botPlantProgress_ = 0.0f;
                    CoachOnEvent(Team::T,  CoachEventType::DefuseAborted);
                    CoachOnEvent(Team::CT, CoachEventType::DefuseAborted);
                }
            } else {
                botPlantProgress_ = 0.0f;
            }
        } else {
            // 倒计时
            const float stepTime = std::min(std::max(0.0f,dt),std::max(0.0f,bombTimer_));

            // C4 滴答：越接近爆炸越急促（2s → 0.25s）
            // 滴答声是 CT 定位炸弹的听觉线索，因此即使没有音频也要发事件。
            {
                bombBeepTimer_ -= dt;
                const float interval = (bombTimer_ > 10.0f) ? 1.0f
                                     : (bombTimer_ > 4.0f)  ? 0.5f
                                     : 0.25f;
                if (bombBeepTimer_ <= 0.0f) {
                    bombBeepTimer_ = interval;
                    EmitSound(SoundEventType::BombBeep, bombX_, bombY_, Team::T);
                    if (audio) {
                        audio->PlaySoundAt(SoundEffect::BOMB_BEEP, bombX_, bombY_,
                                           ListenerX(), ListenerY());
                    }
                }
            }

            const int previousLeader = activeDefuser_;
            activeDefuser_ = -2;
            defuseProgress_ = defuseRemaining_ = 0;
            int winner = -2;
            float firstFinish = std::numeric_limits<float>::infinity();
            float bestRemaining = std::numeric_limits<float>::infinity();
            auto updateDefuser = [&](Player& actor, int id, bool eligible) {
                if (!eligible) {
                    if (audio) audio->StopChannel(actor.defuseChannel);
                    else if (actor.defuseChannel >= 0 && Mix_QuerySpec(nullptr,nullptr,nullptr)) Mix_HaltChannel(actor.defuseChannel);
                    actor.defuseChannel = -1;
                    actor.defuseProgress = 0;
                    return;
                }
                if (actor.hasDefuseKit && dt>0) actor.kitEquipment.used = true;
                const float duration = actor.hasDefuseKit ? 4.0f : DEFUSE_TIME;
                const float finishIn = (1-actor.defuseProgress)*duration;
                const bool starting = actor.defuseProgress <= 0;
                if (starting) {
                    const std::string area = world_ ? world_->FindAreaAt(bombX_,bombY_) : std::string();
                    CoachOnEvent(Team::T,CoachEventType::DefuseStarted,area);
                    CoachOnEvent(Team::CT,CoachEventType::DefuseStarted,area);
                }
                if (actor.defuseChannel < 0 && audio) {
                    const auto sound = actor.hasDefuseKit ? SoundEffect::BOMB_DEFUSING_KIT : SoundEffect::BOMB_DEFUSING;
                    actor.defuseChannel = id == -1 ? audio->PlaySoundTracked(sound, 2) :
                        audio->PlaySoundTrackedAt(sound,actor.x,actor.y,ListenerX(),ListenerY(),id+3);
                }
                actor.defuseProgress = std::min(1.0f,actor.defuseProgress+stepTime/duration);
                if (finishIn <= stepTime && finishIn < firstFinish) {
                    winner = id;
                    firstFinish = finishIn;
                }
                const float remaining = (1-actor.defuseProgress)*duration;
                const bool sameTime = std::abs(remaining-bestRemaining) < .0001f;
                if (remaining < bestRemaining-.0001f || (sameTime && id == previousLeader)) {
                    bestRemaining = remaining;
                    activeDefuser_ = id;
                    defuseProgress_ = actor.defuseProgress;
                    defuseRemaining_ = remaining;
                }
            };
            const float pdx=player_.x-bombX_, pdy=player_.y-bombY_;
            const bool playerDefusing = player_.alive && player_.team == Team::CT && interactHeld &&
                pdx*pdx+pdy*pdy <= Tuning::DefuseRadius*Tuning::DefuseRadius;
            updateDefuser(player_,-1,playerDefusing);
            for (size_t i=0;i<bots_.size();++i) {
                const bool eligible = BotCanDefuse(i,bots_[i]);
                AiAt(i).defusing = eligible;
                if (eligible) bots_[i].vx=bots_[i].vy=0;
                updateDefuser(bots_[i],static_cast<int>(i),eligible);
            }
            if (winner != -2) {
                StopObjectiveAudio(audio);
                bombPlanted_ = false;
                bombTimer_ = 0;
                EmitSound(SoundEventType::BombDefused,bombX_,bombY_,Team::CT);
                if (audio) audio->PlaySound(SoundEffect::BOMB_DEFUSE);
                Player& successful = winner == -1 ? player_ : bots_[winner];
                successful.money += economy_.GrantDefuse();
                economy_.Clamp(successful.money);
                std::cout << "[Defuse] #" << winner << " success" << std::endl;
                ApplyRoundResult(Team::CT,audio);
                round_.EndRound(Team::CT);
            } else {
                bombTimer_ -= dt;
                if (bombTimer_ <= 0) DetonateBomb(audio);
            }
        }
        return;
    }
}

void CS2DGame::PlantBomb(float x, float y, AudioManager* audio) {
    StopObjectiveAudio(audio);
    // C4 已安装：从玩家手里/槽位里移除
    bombPlanter_ = bombCarrier_;
    bombCarrier_ = -2;
    playerHoldingC4_ = false;
    player_.weapons[4] = WeaponId::None;
    player_.equipment[4]={};
    if (player_.weaponSlot == 4) EquipFallbackWeapon();
    bombPlanted_ = true;
    // Set coords before events so FindAreaAt is not fed stale/zero bomb position.
    bombX_ = x;
    bombY_ = y;

    // ---- 下包奖励：T 全员每人 +$300（照抄 CS2）----
    {
        const int bonus = economy_.GrantPlant();
        if (player_.team == Team::T) {
            player_.money += bonus;
            economy_.Clamp(player_.money);
        }
        for (auto& bot : bots_) {
            if (bot.team != Team::T) continue;
            bot.money += bonus;
            economy_.Clamp(bot.money);
        }
        std::cout << "[Economy] C4 下包 → T 全员 +$" << bonus << std::endl;
    }

    {
        const std::string plantArea = world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
        CoachOnEvent(Team::T,  CoachEventType::BombPlanted, plantArea);
        CoachOnEvent(Team::CT, CoachEventType::BombPlanted, plantArea);
        std::cout << "[Coach] BombPlanted area="
                  << (plantArea.empty() ? "(empty)" : plantArea) << std::endl;
    }
    bombTimer_ = BOMB_FUSE;
    bombBeepTimer_ = 0.0f;
    defuseProgress_ = 0.0f;
    // 下包完成是全图可闻的事件：所有单位收到「炸弹在这里」
    EmitSound(SoundEventType::BombPlanted, bombX_, bombY_, Team::T);
    std::cout << "[CS2D] C4 已安装! " << static_cast<int>(BOMB_FUSE) << " 秒后爆炸" << std::endl;
    if (audio) audio->PlaySound(SoundEffect::BOMB_PLANT);
}

void CS2DGame::DetonateBomb(AudioManager* audio) {
    StopObjectiveAudio(audio);
    bombPlanted_ = false;
    bombTimer_ = 0.0f;
    std::cout << "[CS2D] C4 爆炸! T 获胜本回合" << std::endl;
    EmitSound(SoundEventType::Explosion, bombX_, bombY_, Team::T, -2, 3200.0f);

    // 爆炸范围伤害（不分阵营）
    constexpr float RADIUS = 320.0f;
    auto damageAt = [&](Player& p) {
        if (!p.alive) return;
        const float dx = p.x - bombX_;
        const float dy = p.y - bombY_;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > RADIUS) return;
        const float f = 1.0f - d / RADIUS;
        if (ApplyDamage(p, static_cast<int>(200.0f * f), 1.0f)) {   // C4 爆炸无视护甲
            OnCombatantKilled(p, audio, DamageOwner(bombPlanter_), WeaponId::C4,c4EquipmentId_,SDL_FPoint{bombX_,bombY_});
        }
    };
    damageAt(player_);
    for (auto& b : bots_) damageAt(b);

    if (audio) audio->PlaySound(SoundEffect::BOMB_EXPLODE);

    ApplyRoundResult(Team::T, audio);
    round_.EndRound(Team::T);
}


// C4 携带者：每回合重新分配
//   玩家在 T 侧：60% 玩家，40% 随机 T BOT
//   玩家在 CT 侧：随机 T BOT（否则 T 方无人能下包）
void CS2DGame::AssignBombCarrier() {
    bombCarrier_ = -2;
    if (mode_ != MatchMode::BombDefusal) return;

    std::vector<int> tBots;
    for (size_t i = 0; i < bots_.size(); ++i) {
        if (bots_[i].team == Team::T) tBots.push_back(static_cast<int>(i));
    }

    if (player_.team == Team::T) {
        if (tBots.empty() || (rng_() % 100) < 60) {
            bombCarrier_ = -1;
        } else {
            bombCarrier_ = tBots[rng_() % tBots.size()];
        }
    } else if (!tBots.empty()) {
        bombCarrier_ = tBots[rng_() % tBots.size()];
    }
}

void CS2DGame::StopObjectiveAudio(AudioManager* audio) {
    auto stop = [&](int& channel) {
        if (audio) audio->StopChannel(channel);
        else if (channel >= 0 && Mix_QuerySpec(nullptr,nullptr,nullptr)) Mix_HaltChannel(channel);
        channel = -1;
    };
    stop(plantingChannel_);
    stop(player_.defuseChannel);
    player_.defuseProgress = 0;
    for (size_t i=0;i<bots_.size();++i) {
        stop(bots_[i].defuseChannel);
        bots_[i].defuseProgress = 0;
        AiAt(i).defusing = false;
    }
    activeDefuser_ = -2;
    defuseProgress_ = defuseRemaining_ = 0;
    plantProgress_ = botPlantProgress_ = 0;
}
