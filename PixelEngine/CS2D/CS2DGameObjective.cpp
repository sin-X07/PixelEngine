#include "CS2DGame.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>

// ============================================================================
// 目标物：爆破（下包/拆包）
// ============================================================================

namespace {
constexpr float BOMB_FUSE   = 40.0f;   // 下包后倒计时
constexpr float PLANT_TIME  = 3.0f;    // T 在包点停留多久完成下包
constexpr float DEFUSE_TIME = 10.0f;   // CT 拆包耗时
} // namespace

// 记分板数据（全部活跃单位，按阵营与击杀排序）
std::vector<CS2DGame::ScoreRow> CS2DGame::BuildScoreboard() const {
    std::vector<ScoreRow> rows;
    rows.reserve(actors_.size());

    for (const auto& actor : actors_) {
        if (!actor.IsActive()) continue;
        ScoreRow r;
        r.id        = actor.id;
        r.isHuman   = actor.IsHuman();
        r.isPlayer  = (actor.id == localActorId_);
        if (!actor.name.empty()) {
            r.name = actor.name;
        } else if (r.isPlayer) {
            r.name = "你";
        } else {
            r.name = (actor.IsHuman() ? "P" : "BOT#") + std::to_string(actor.id);
        }
        r.team      = actor.team;
        r.kills     = actor.kills;
        r.teamKills = actor.teamKills;
        r.deaths    = actor.deaths;
        r.money     = actor.money;
        r.alive     = actor.alive;
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
    c4EquipmentId_ = mode_ == MatchMode::BombDefusal ? nextEquipmentId_++ : 0;
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
    for (auto& actor : actors_) {
        actor.holdingC4 = false;
        actor.plantProgress = 0.0f;
        actor.weapons[4] = WeaponId::None;
        actor.equipment[4] = {};
        if (actor.weaponSlot == 4) actor.weaponSlot = 1;
    }
    bombX_ = bombY_ = 0.0f;
}

// 每帧推进目标物（仅交战期）
void CS2DGame::UpdateObjective(float dt, IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    IAudioSink* sink = audio ? audio : (audioSink_ ? audioSink_ : IAudioSink::Active());
    if (mode_ != MatchMode::BombDefusal || !round_.IsLive()) {
        StopObjectiveAudio(audio);
        return;
    }

    if (!bombPlanted_) {
        // 未下包：清掉所有 BOT 的拆包状态
        for (size_t i = 0; i < actorAi_.size(); ++i) actorAi_[i].defusing = false;

        Player* carrier = (!bombDropped_ && world_) ? FindActor(bombCarrier_) : nullptr;
        const bool canCarrierPlant = carrier && carrier->IsActive() && carrier->alive &&
                                     carrier->team == Team::T &&
                                     world_->FindBombSiteAt(carrier->x, carrier->y) != nullptr;
        const bool wantPlant = canCarrierPlant &&
                               (carrier->IsHuman() ? carrier->interactHeld : true);

        for (auto& actor : actors_) {
            if (&actor != carrier || !wantPlant) {
                actor.plantProgress = 0.0f;
                if (actor.plantingChannel >= 0) {
                    if (sink) sink->StopChannel(actor.plantingChannel);
                    actor.plantingChannel = -1;
                }
            }
        }

        if (wantPlant && carrier) {
            if (carrier->IsHuman()) {
                if (carrier->plantProgress == 0.0f && audio) {
                    if (carrier->id == localActorId_)
                        carrier->plantingChannel = audio->PlaySoundTracked(SoundEffect::BOMB_PLANTING, 1);
                    else
                        carrier->plantingChannel = audio->PlaySoundTrackedAt(
                            SoundEffect::BOMB_PLANTING, carrier->x, carrier->y, ListenerX(), ListenerY(), 1);
                }
            } else {
                bombX_ = carrier->x;
                bombY_ = carrier->y;
            }
            const bool wasBot = carrier->IsBot();
            carrier->plantProgress += dt;
            if (carrier->plantProgress >= PLANT_TIME) {
                const float px = carrier->x;
                const float py = carrier->y;
                carrier->plantProgress = 0.0f;
                PlantBomb(px, py, audio);
                if (wasBot) {
                    CoachOnEvent(Team::T,  CoachEventType::DefuseAborted);
                    CoachOnEvent(Team::CT, CoachEventType::DefuseAborted);
                }
            }
        }
        return;
    }

    // ---- 已下包：倒计时 + 拆包 ----
    for (auto& actor : actors_) {
        actor.plantProgress = 0.0f;
        if (actor.plantingChannel >= 0) {
            if (sink) sink->StopChannel(actor.plantingChannel);
            actor.plantingChannel = -1;
        }
    }

    const float stepTime = std::min(std::max(0.0f, dt), std::max(0.0f, bombTimer_));

    // C4 滴答：越接近爆炸越急促（1s → 0.25s）
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

    const ActorId previousLeader = activeDefuser_;
    activeDefuser_ = kInvalidActorId;
    defuseProgress_ = defuseRemaining_ = 0.0f;
    ActorId winner = kInvalidActorId;
    float firstFinish = std::numeric_limits<float>::infinity();
    float bestRemaining = std::numeric_limits<float>::infinity();

    auto updateDefuser = [&](Player& actor, bool eligible) {
        if (!eligible) {
            if (sink && actor.defuseChannel >= 0) sink->StopChannel(actor.defuseChannel);
            actor.defuseChannel = -1;
            actor.defuseProgress = 0.0f;
            return;
        }
        if (actor.hasDefuseKit && dt > 0.0f) actor.kitEquipment.used = true;
        const float duration = actor.hasDefuseKit ? 4.0f : DEFUSE_TIME;
        const float finishIn = (1.0f - actor.defuseProgress) * duration;
        const bool starting = actor.defuseProgress <= 0.0f;
        if (starting) {
            const std::string area = world_ ? world_->FindAreaAt(bombX_, bombY_) : std::string();
            CoachOnEvent(Team::T,  CoachEventType::DefuseStarted, area);
            CoachOnEvent(Team::CT, CoachEventType::DefuseStarted, area);
        }
        const auto sound = actor.hasDefuseKit ? SoundEffect::BOMB_DEFUSING_KIT : SoundEffect::BOMB_DEFUSING;
        const int reservedChan = std::min(2 + actor.id, kReservedChannels - 1);
        const bool isLocal = (actor.id == localActorId_);
        if (actor.defuseChannel < 0 && audio) {
            actor.defuseChannel = isLocal
                ? audio->PlaySoundTracked(sound, reservedChan)
                : audio->PlaySoundTrackedAt(sound, actor.x, actor.y, ListenerX(), ListenerY(), reservedChan);
        }
        if (audio && !isLocal)
            audio->UpdateTrackedSoundAt(actor.defuseChannel, sound, actor.x, actor.y, ListenerX(), ListenerY());
        actor.defuseProgress = std::min(1.0f, actor.defuseProgress + stepTime / duration);
        if (finishIn <= stepTime && finishIn < firstFinish) {
            winner = actor.id;
            firstFinish = finishIn;
        }
        const float remaining = (1.0f - actor.defuseProgress) * duration;
        const bool sameTime = std::abs(remaining - bestRemaining) < .0001f;
        if (remaining < bestRemaining - .0001f || (sameTime && actor.id == previousLeader)) {
            bestRemaining = remaining;
            activeDefuser_ = actor.id;
            defuseProgress_ = actor.defuseProgress;
            defuseRemaining_ = remaining;
        }
    };

    EnsureBotAi();
    for (size_t i = 0; i < actors_.size(); ++i) {
        auto& actor = actors_[i];
        if (!actor.IsActive()) continue;
        bool eligible = false;
        if (actor.IsHuman()) {
            eligible = actor.alive && actor.team == Team::CT && actor.interactHeld &&
                       IsInDefuseRange(actor.x, actor.y);
            actorAi_[i].defusing = false;
        } else {
            eligible = BotCanDefuse(i, actor);
            actorAi_[i].defusing = eligible;
            if (eligible) actor.vx = actor.vy = 0.0f;
        }
        updateDefuser(actor, eligible);
    }

    if (winner != kInvalidActorId) {
        StopObjectiveAudio(audio);
        bombPlanted_ = false;
        bombTimer_ = 0.0f;
        EmitSound(SoundEventType::BombDefused, bombX_, bombY_, Team::CT);
        if (audio) audio->PlaySound(SoundEffect::BOMB_DEFUSE);
        if (Player* successful = FindActor(winner)) {
            successful->money += economy_.GrantDefuse();
            economy_.Clamp(successful->money);
            const bool kitWin = successful->hasDefuseKit;
            std::cout << "[Defuse] #" << winner << " success kit=" << (kitWin ? 1 : 0)
                      << " dur=" << (kitWin ? 4 : 10) << std::endl;
        }
        ApplyRoundResult(Team::CT, audio);
        round_.EndRound(Team::CT);
    } else {
        bombTimer_ -= dt;
        if (bombTimer_ <= 0.0f) DetonateBomb(audio);
    }
}

void CS2DGame::PlantBomb(float x, float y, IAudioSink* audio) {
    StopObjectiveAudio(audio);
    // C4 已安装：从携带者手里/槽位里移除
    bombPlanter_ = bombCarrier_;
    bombCarrier_ = kInvalidActorId;
    for (auto& actor : actors_) {
        actor.holdingC4 = false;
        actor.plantProgress = 0.0f;
        actor.weapons[4] = WeaponId::None;
        actor.equipment[4] = {};
        if (actor.weaponSlot == 4) EquipFallbackWeaponFor(actor);
    }
    bombPlanted_ = true;
    bombX_ = x;
    bombY_ = y;

    // ---- 下包奖励：T 全员每人 +$300（照抄 CS2）----
    {
        const int bonus = economy_.GrantPlant();
        for (auto& actor : actors_) {
            if (!actor.IsActive() || actor.team != Team::T) continue;
            actor.money += bonus;
            economy_.Clamp(actor.money);
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

void CS2DGame::DetonateBomb(IAudioSink* audio) {
    StopObjectiveAudio(audio);
    bombPlanted_ = false;
    bombTimer_ = 0.0f;
    std::cout << "[CS2D] C4 爆炸! T 获胜本回合" << std::endl;
    EmitSound(SoundEventType::Explosion, bombX_, bombY_, Team::T, kEnvironmentActorId, 3200.0f);

    // 爆炸范围伤害（不分阵营）
    constexpr float RADIUS = Tuning::BombBlastRadius;
    for (auto& p : actors_) {
        if (!p.IsActive() || !p.alive) continue;
        const float dx = p.x - bombX_;
        const float dy = p.y - bombY_;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > RADIUS) continue;
        const float f = 1.0f - d / RADIUS;
        if (ApplyDamage(p, static_cast<int>(200.0f * f), 1.0f)) {   // C4 爆炸无视护甲
            OnCombatantKilled(p, audio, DamageOwner(bombPlanter_), WeaponId::C4, c4EquipmentId_, Vec2{bombX_, bombY_});
        }
    }

    if (audio) audio->PlaySound(SoundEffect::BOMB_EXPLODE);

    ApplyRoundResult(Team::T, audio);
    round_.EndRound(Team::T);
}

// C4 携带者：每回合重新分配
//   T 侧若仅 1 名真人且有 T BOT：保持单机 60% 真人 / 40% T BOT；
//   多个 T 真人或无 T 真人时在所有 T 侧候选者间随机抽取。
void CS2DGame::AssignBombCarrier() {
    bombCarrier_ = kInvalidActorId;
    if (mode_ != MatchMode::BombDefusal) return;

    std::vector<ActorId> tHumans;
    std::vector<ActorId> tBots;
    for (const auto& a : actors_) {
        if (!a.IsActive() || a.team != Team::T) continue;
        if (a.IsHuman()) tHumans.push_back(a.id);
        else tBots.push_back(a.id);
    }

    if (tHumans.size() == 1) {
        if (tBots.empty() || (rng_() % 100) < 60) {
            bombCarrier_ = tHumans[0];
        } else {
            bombCarrier_ = tBots[rng_() % tBots.size()];
        }
    } else if (!tHumans.empty()) {
        std::vector<ActorId> allT = tHumans;
        allT.insert(allT.end(), tBots.begin(), tBots.end());
        bombCarrier_ = allT[rng_() % allT.size()];
    } else if (!tBots.empty()) {
        bombCarrier_ = tBots[rng_() % tBots.size()];
    }

    if (Player* carrier = FindActor(bombCarrier_)) {
        if (carrier->IsHuman()) {
            carrier->weapons[4] = WeaponId::C4;
            carrier->equipment[4] = {};
            carrier->equipment[4].id = c4EquipmentId_;
            carrier->equipment[4].epoch = purchaseEpoch_;
        }
    }
}

void CS2DGame::SetObjectiveAudioPaused(bool paused, IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    IAudioSink* sink = audio ? audio : (audioSink_ ? audioSink_ : IAudioSink::Active());
    if (!sink) return;
    for (const auto& actor : actors_) {
        if (actor.plantingChannel >= 0) sink->SetChannelPaused(actor.plantingChannel, paused);
        if (actor.defuseChannel >= 0) sink->SetChannelPaused(actor.defuseChannel, paused);
    }
}

void CS2DGame::StopObjectiveAudio(IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    IAudioSink* sink = audio ? audio : (audioSink_ ? audioSink_ : IAudioSink::Active());
    auto stop = [&](int& channel) {
        if (sink && channel >= 0) sink->StopChannel(channel);
        channel = -1;
    };
    for (size_t i = 0; i < actors_.size(); ++i) {
        stop(actors_[i].plantingChannel);
        stop(actors_[i].defuseChannel);
        actors_[i].defuseProgress = 0.0f;
        actors_[i].plantProgress = 0.0f;
        if (i < actorAi_.size()) actorAi_[i].defusing = false;
    }
    activeDefuser_ = kInvalidActorId;
    defuseProgress_ = defuseRemaining_ = 0.0f;
}
