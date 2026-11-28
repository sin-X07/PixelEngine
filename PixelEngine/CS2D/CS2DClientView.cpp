#include "CS2DClientView.hpp"
#include "Tuning.hpp"
#include <algorithm>
#include <cmath>

CS2DClientView::CS2DClientView() {
    plantingChannels_.fill(-1);
    defuseChannels_.fill(-1);
}

void CS2DClientView::OnNewMatchReset(const CS2DSimulation& sim, IAudioSink* audio) {
    StopObjectiveAudio(audio);
    damageDirections_ = {};
    damageFlashTimer_ = hitMarkerTimer_ = 0.0f;
    dryFireHintTimer_ = 0.0f;
    hitMarkerKind_ = HitMarkerKind::Normal;
    announcement_.clear();
    announcementTimer_ = 0.0f;
    deathPresentation_ = {};
    deathCam_ = false;
    specMode_ = SpectateMode::None;
    specTarget_ = kInvalidActorId;
    ResetScope(sim, Presentation::ScopeResetReason::RoundRestart);
}

void CS2DClientView::OnLocalActorSpawned(const CS2DSimulation& sim) {
    ++playerSpawnVersion_;
    recoilShake_ = recoilShakeAge_ = 0.0f;
    aimFollowsPlayer_ = false;
    viewHistoryValid_ = false;
    edgeZoom_ = previousEdgeZoom_ = edgeZoomFrom_ = edgeZoomTo_ = 0.0f;
    edgeZoomTime_ = 1.0f;
    edgeZoomRequested_ = false;
    cameraDistanceTier_ = 0;
    cameraPrepared_ = cameraScopeSettled_ = false;
    SpectatorEnd();
    deathPresentation_ = {};
    deathCam_ = false;
    crosshairBloom_ = 0.0f;
    aimMotionBlur_ = 0.0f;
    (void)sim;
}

void CS2DClientView::BeginTick(float dt, CS2DSimulation& sim, const ActorInput* localInput) {
    deathPresentation_.remaining = std::max(0.0f, deathPresentation_.remaining - dt);
    dryFireHintTimer_ = std::max(0.0f, dryFireHintTimer_ - dt);

    if (IsSpectating(sim)) {
        if (spectateTogglePending_) SpectatorToggle(sim);
        if (specMode_ == SpectateMode::Free && localInput) {
            const float fx = (localInput->moveRight ? 1.0f : 0.0f) - (localInput->moveLeft ? 1.0f : 0.0f);
            const float fy = (localInput->moveDown  ? 1.0f : 0.0f) - (localInput->moveUp   ? 1.0f : 0.0f);
            SpectatorMoveFree(sim, fx, fy, dt);
        }
    }
    spectateTogglePending_ = false;

    previousEdgeZoom_ = edgeZoom_;
    previousScopeZoom_ = scopeZoom_;
    previousScopeEngagement_ = scopeEngagement_;
    previousActorViews_.clear();
    const auto& actors = sim.GetActors();
    previousActorViews_.reserve(actors.size());
    for (const auto& actor : actors) {
        previousActorViews_.push_back({actor.x, actor.y, actor.alive, actor.EffectiveAim(), actor.animTimer, actor.animState});
    }
    viewHistoryValid_ = true;

    if (localInput && localInput->hasAimWorld) {
        SetAimTarget(sim, localInput->aimWorldX, localInput->aimWorldY, aimFollowsPlayer_);
    }

    recoilShakeAge_ += dt;
    recoilShake_ *= std::exp(-Tuning::RecoilShakeDecay * dt);

    TickScope(dt, sim);

    const float edgeTarget = !IsSpectating(sim) ? static_cast<float>(cameraDistanceTier_) : 0.0f;
    if (edgeTarget != edgeZoomTo_) {
        edgeZoomFrom_ = edgeZoom_;
        edgeZoomTo_ = edgeTarget;
        edgeZoomTime_ = 0.0f;
    }
    edgeZoomTime_ = std::min(1.0f, edgeZoomTime_ + dt / Tuning::EdgeZoomSeconds);
    const float edgeEase = edgeZoomTime_ * edgeZoomTime_ * (3.0f - 2.0f * edgeZoomTime_);
    edgeZoom_ = edgeZoomFrom_ + (edgeZoomTo_ - edgeZoomFrom_) * edgeEase;

    if (scopeCloseOnRelease_) {
        if (!localInput || !localInput->fire) {
            ResetScope(sim, Presentation::ScopeResetReason::ShotFired);
        }
    }

    sim.SetActorScopeView(sim.GetLocalActorId(), scopeLevel_, scopeEngagement_);
}

void CS2DClientView::EndTick(float dt, const CS2DSimulation& sim, bool canMove) {
    UpdateSpectatorState(sim);

    if (!canMove || !sim.LocalActor().alive) {
        const float recover = 1.0f - std::exp(-12.0f * dt);
        aimMotionBlur_ += (0.0f - aimMotionBlur_) * recover;
        crosshairBloom_ += (0.0f - crosshairBloom_) * recover;
        if (aimMotionBlur_ < 0.002f) aimMotionBlur_ = 0.0f;
        if (crosshairBloom_ < 0.002f) crosshairBloom_ = 0.0f;
    }

    for (auto& direction : damageDirections_) direction.remaining = std::max(0.0f, direction.remaining - dt);
    if (hitMarkerTimer_ > 0.0f)   hitMarkerTimer_   = std::max(0.0f, hitMarkerTimer_ - dt);
    if (damageFlashTimer_ > 0.0f) damageFlashTimer_ = std::max(0.0f, damageFlashTimer_ - dt);
    if (announcementTimer_ > 0.0f) {
        announcementTimer_ = std::max(0.0f, announcementTimer_ - dt);
        if (announcementTimer_ <= 0.0f) announcement_.clear();
    }
}

void CS2DClientView::UpdateMotionFeedback(float dt, const CS2DSimulation& sim) {
    const auto& local = sim.LocalActor();
    const float runMaxSpeed = Tuning::PlayerBaseSpeed * local.CurrentWeapon().moveSpeedMul;
    const float finalSpeed = std::sqrt(local.vx * local.vx + local.vy * local.vy);
    const float target = (runMaxSpeed > 0.001f)
        ? std::clamp(finalSpeed / runMaxSpeed, 0.0f, 1.0f)
        : 0.0f;
    const float targetBloom = (target < 0.02f) ? 0.0f : target;
    const float blurTarget = 5.0f * std::clamp(target, 0.0f, 1.0f);
    const float blurSeconds = blurTarget > aimMotionBlur_ ? 0.08f : 0.15f;
    aimMotionBlur_ += (blurTarget - aimMotionBlur_) * (1.0f - std::exp(-3.0f * dt / blurSeconds));

    const float rate = (targetBloom > crosshairBloom_)
        ? Tuning::CrosshairBloomExpandRate
        : (local.CurrentWeapon().cls == WeaponClass::Sniper ? Tuning::BloomSniper
           : local.CurrentWeapon().cls == WeaponClass::SMG ? Tuning::BloomSmg
           : Tuning::CrosshairBloomContractRate);
    const float k = 1.0f - std::exp(-rate * dt);
    crosshairBloom_ += (targetBloom - crosshairBloom_) * k;
    if (std::abs(targetBloom - crosshairBloom_) < 0.002f) {
        crosshairBloom_ = targetBloom;
    }
}

void CS2DClientView::ConsumeEvents(const std::vector<SimEvent>& events,
                                   const CS2DSimulation& sim, IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    IAudioSink* sink = audio ? audio : (audioSink_ ? audioSink_ : IAudioSink::Active());
    const ActorId localId = sim.GetLocalActorId();
    const float lx = ListenerX(sim);
    const float ly = ListenerY(sim);

    for (const auto& ev : events) {
        const bool isLocalActor = (ev.actorId == localId);
        switch (ev.type) {
            case SimEventType::ShotFired: {
                if (ev.flagB) {
                    // 空弹干响
                    if (isLocalActor && ev.flagC) dryFireHintTimer_ = 1.0f;
                    if (audio) {
                        if (isLocalActor) audio->PlaySound(SoundEffect::DRYFIRE);
                        else audio->PlaySoundAt(SoundEffect::DRYFIRE, ev.x, ev.y, lx, ly);
                    }
                } else if (ev.flagA) {
                    // 近战挥刀
                    if (audio) {
                        if (isLocalActor) audio->PlaySound(SoundEffect::KATANA_SWING);
                        else audio->PlaySoundAt(SoundEffect::KATANA_SWING, ev.x, ev.y, lx, ly);
                    }
                } else {
                    // 枪声 / 诱饵弹假枪声
                    if (audio) {
                        if (!ev.flagD && isLocalActor) audio->PlayWeaponFire(ev.weapon);
                        else audio->PlayWeaponFireAt(ev.weapon, ev.x, ev.y, lx, ly);
                    }
                }
                break;
            }
            case SimEventType::WeaponSwitched: {
                if (audio && ev.weapon != WeaponId::None) {
                    if (isLocalActor) audio->PlayWeaponSwitch(ev.weapon);
                    else audio->PlayWeaponSwitchAt(ev.weapon, ev.x, ev.y, lx, ly);
                }
                break;
            }
            case SimEventType::ReloadStarted: {
                if (isLocalActor) ResetScope(sim, Presentation::ScopeResetReason::ReloadStarted);
                if (audio) {
                    const SoundEffect se = ReloadSoundFor(ev.weapon);
                    if (isLocalActor) audio->PlaySound(se);
                    else audio->PlaySoundAt(se, ev.x, ev.y, lx, ly);
                }
                break;
            }
            case SimEventType::RecoilImpulse: {
                if (isLocalActor) {
                    recoilShake_ = std::min(Tuning::RecoilShakeMax, recoilShake_ + ev.amount);
                    recoilShakeAge_ = 0.0f;
                }
                break;
            }
            case SimEventType::HitConfirm: {
                const bool attackerIsLocal = (ev.actorId == localId);
                const bool victimIsLocal = (ev.targetId == localId);
                const bool armored = ev.flagA;
                const bool killed  = ev.flagB;
                const bool suppressMarker = ev.flagC;

                if (attackerIsLocal && !suppressMarker) {
                    if (killed || hitMarkerKind_ != HitMarkerKind::Kill || hitMarkerTimer_ <= 0.0f) {
                        hitMarkerKind_ = killed ? HitMarkerKind::Kill
                                                : (armored ? HitMarkerKind::Armor : HitMarkerKind::Normal);
                        hitMarkerTimer_ = killed ? 0.30f : 0.18f;
                    }
                }

                if (killed) {
                    if (audio) {
                        if (!victimIsLocal) {
                            audio->PlaySoundAt(SoundEffect::KILL, ev.x, ev.y, lx, ly);
                        }
                        if (attackerIsLocal) {
                            audio->PlaySound(SoundEffect::KILL, 0.65f);
                        }
                    }
                } else if (audio) {
                    if (victimIsLocal) {
                        audio->PlaySound(armored ? SoundEffect::ARMOR_HIT : SoundEffect::HIT);
                    } else {
                        audio->PlaySoundAt(
                            (attackerIsLocal && armored) ? SoundEffect::ARMOR_HIT : SoundEffect::HIT,
                            ev.x, ev.y, lx, ly);
                    }
                }
                break;
            }
            case SimEventType::DamageTaken: {
                if (ev.targetId == localId && ev.flagB) {
                    auto direction = std::min_element(
                        damageDirections_.begin(), damageDirections_.end(),
                        [](const Presentation::DamageDirection& a,
                           const Presentation::DamageDirection& b) {
                            return a.remaining < b.remaining;
                        });
                    *direction = {std::atan2(ev.dirY, ev.dirX), 0.5f};
                    damageFlashTimer_ = 0.30f;
                }
                break;
            }
            case SimEventType::KillFeed: {
                if (ev.targetId == localId) {
                    CS2DSimulation::KillFeedEntry entry;
                    entry.equipmentId = ev.equipmentId;
                    entry.sourcePosition = Vec2{ev.x, ev.y};
                    entry.id = ev.killEventId;
                    entry.timestamp = ev.amount;
                    entry.text = ev.text;
                    entry.killer = ev.killerName;
                    entry.victim = ev.victimName;
                    entry.killerTeam = ev.team;
                    entry.victimTeam = ev.victimTeam;
                    entry.weapon = ev.weapon;
                    entry.personal = true;
                    deathPresentation_.event = std::move(entry);
                    deathPresentation_.remaining = DeathFreezeSeconds;
                    hitMarkerTimer_ = 0.0f;
                    if (audio) {
                        audio->PlaySound(SoundEffect::DEATH);
                        audio->PlaySound(SoundEffect::KILL);
                    }
                    ResetScope(sim, Presentation::ScopeResetReason::PlayerDied);
                    SpectatorEnd();
                    if (sim.GetMatchMode() == MatchMode::DeathMatch) {
                        deathCam_ = true;
                    }
                }
                break;
            }
            case SimEventType::BuyResult: {
                if (audio && ev.buyResult.code == Presentation::BuyCode::Ok) {
                    if (isLocalActor) audio->PlaySound(SoundEffect::EQUIP);
                    else audio->PlaySoundAt(SoundEffect::EQUIP, ev.x, ev.y, lx, ly);
                }
                break;
            }
            case SimEventType::ItemPickedUp: {
                if (audio) {
                    if (isLocalActor) audio->PlaySound(SoundEffect::PICKUP);
                    else audio->PlaySoundAt(SoundEffect::PICKUP, ev.x, ev.y, lx, ly);
                }
                break;
            }
            case SimEventType::GrenadeEvent: {
                if (!audio) break;
                switch (ev.grenadeKind) {
                    case GrenadeEventKind::PullPin:
                        if (isLocalActor) audio->PlaySound(SoundEffect::PULL_PIN);
                        else audio->PlaySoundAt(SoundEffect::PULL_PIN, ev.x, ev.y, lx, ly);
                        break;
                    case GrenadeEventKind::Throw: {
                        SoundEffect se = SoundEffect::GRENADE_THROW;
                        if (ev.weapon == WeaponId::GrenadeHE) se = SoundEffect::THROW_HE;
                        else if (ev.weapon == WeaponId::GrenadeFlash) se = SoundEffect::THROW_FLASH;
                        else if (ev.weapon == WeaponId::GrenadeSmoke) se = SoundEffect::THROW_SMOKE;
                        if (isLocalActor) audio->PlaySound(se);
                        else audio->PlaySoundAt(se, ev.x, ev.y, lx, ly);
                        break;
                    }
                    case GrenadeEventKind::ExplodeHE:
                        audio->PlaySound(SoundEffect::GRENADE_EXPLODE);
                        break;
                    case GrenadeEventKind::ExplodeFlash:
                        audio->PlaySound(SoundEffect::FLASH);
                        audio->PlaySound(SoundEffect::HIT);
                        break;
                    case GrenadeEventKind::ExplodeSmoke:
                        audio->PlaySound(SoundEffect::SMOKE);
                        break;
                    case GrenadeEventKind::IgniteFire:
                        audio->PlaySound(SoundEffect::FIRE_IGNITE);
                        break;
                    case GrenadeEventKind::ExplodeDecoy:
                        audio->PlaySoundAt(SoundEffect::GRENADE_EXPLODE, ev.x, ev.y, lx, ly);
                        break;
                }
                break;
            }
            case SimEventType::BombEvent: {
                switch (ev.bombKind) {
                    case BombEventKind::PlantActive: {
                        if (ev.actorId < 0 || ev.actorId >= kMaxActors) break;
                        int& chan = plantingChannels_[static_cast<size_t>(ev.actorId)];
                        if (chan < 0 && audio) {
                            chan = isLocalActor
                                ? audio->PlaySoundTracked(SoundEffect::BOMB_PLANTING, 1)
                                : audio->PlaySoundTrackedAt(SoundEffect::BOMB_PLANTING,
                                                            ev.x, ev.y, lx, ly, 1);
                        }
                        break;
                    }
                    case BombEventKind::PlantStopped: {
                        if (ev.actorId < 0 || ev.actorId >= kMaxActors) break;
                        int& chan = plantingChannels_[static_cast<size_t>(ev.actorId)];
                        if (sink && chan >= 0) sink->StopChannel(chan);
                        chan = -1;
                        break;
                    }
                    case BombEventKind::Planted:
                        if (audio) audio->PlaySound(SoundEffect::BOMB_PLANT);
                        break;
                    case BombEventKind::Beep:
                        if (audio) audio->PlaySoundAt(SoundEffect::BOMB_BEEP, ev.x, ev.y, lx, ly);
                        break;
                    case BombEventKind::DefuseActive: {
                        if (ev.actorId < 0 || ev.actorId >= kMaxActors) break;
                        int& chan = defuseChannels_[static_cast<size_t>(ev.actorId)];
                        const SoundEffect sound = ev.flagA
                            ? SoundEffect::BOMB_DEFUSING_KIT
                            : SoundEffect::BOMB_DEFUSING;
                        const int reservedChan = std::min(2 + ev.actorId, kReservedChannels - 1);
                        if (chan < 0 && audio) {
                            chan = isLocalActor
                                ? audio->PlaySoundTracked(sound, reservedChan)
                                : audio->PlaySoundTrackedAt(sound, ev.x, ev.y, lx, ly, reservedChan);
                        }
                        if (audio && !isLocalActor && chan >= 0) {
                            audio->UpdateTrackedSoundAt(chan, sound, ev.x, ev.y, lx, ly);
                        }
                        break;
                    }
                    case BombEventKind::DefuseStopped: {
                        if (ev.actorId < 0 || ev.actorId >= kMaxActors) break;
                        int& chan = defuseChannels_[static_cast<size_t>(ev.actorId)];
                        if (sink && chan >= 0) sink->StopChannel(chan);
                        chan = -1;
                        break;
                    }
                    case BombEventKind::Defused:
                        if (audio) audio->PlaySound(SoundEffect::BOMB_DEFUSE);
                        break;
                    case BombEventKind::Exploded:
                        if (audio) audio->PlaySound(SoundEffect::BOMB_EXPLODE);
                        break;
                    case BombEventKind::StopAllObjectiveAudio:
                        StopObjectiveAudio(audio);
                        break;
                }
                break;
            }
            case SimEventType::FootstepEvent: {
                if (!audio) break;
                const int steps = std::max(0, ev.intParam);
                for (int i = 0; i < steps; ++i) {
                    if (ev.flagA) audio->PlaySoundAt(SoundEffect::FOOTSTEP, ev.x, ev.y, lx, ly);
                    else audio->PlaySound(SoundEffect::FOOTSTEP);
                }
                break;
            }
            case SimEventType::RoundEvent: {
                switch (ev.roundKind) {
                    case RoundEventKind::RoundStart:
                        announcement_ = "第 " + std::to_string(ev.intParam) + " 回合";
                        announcementTimer_ = 2.2f;
                        ResetScope(sim, Presentation::ScopeResetReason::RoundRestart);
                        SpectatorEnd();
                        deathPresentation_ = {};
                        deathCam_ = false;
                        break;
                    case RoundEventKind::RoundWin: {
                        const Team viewerTeam = sim.LocalActor().team;
                        if (ev.team != Team::None) {
                            if (audio && ev.team == viewerTeam) {
                                audio->PlaySound(SoundEffect::ROUND_WIN);
                            }
                            announcement_ = (ev.team == viewerTeam) ? "回合胜利" : "回合失败";
                            announcementTimer_ = 2.4f;
                        }
                        break;
                    }
                    case RoundEventKind::MatchReset:
                        OnNewMatchReset(sim, audio);
                        break;
                    case RoundEventKind::MatchOver:
                        break;
                }
                break;
            }
            case SimEventType::ActorSpawned: {
                if (isLocalActor) OnLocalActorSpawned(sim);
                break;
            }
            case SimEventType::ScopeReset: {
                if (!isLocalActor) break;
                if (ev.scopeReason == Presentation::ScopeResetReason::ShotFired && ev.flagA) {
                    const WeaponDef& def = sim.LocalActor().CurrentWeapon();
                    if (scopeLevel_ > 0 && !def.automatic) {
                        if (delayedScopeClose_ && ev.flagB) {
                            scopeCloseOnRelease_ = true;
                        } else {
                            ResetScope(sim, Presentation::ScopeResetReason::ShotFired);
                        }
                    }
                } else {
                    ResetScope(sim, ev.scopeReason);
                }
                break;
            }
        }
    }
}

void CS2DClientView::SetObjectiveAudioPaused(bool paused, IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    IAudioSink* sink = audio ? audio : (audioSink_ ? audioSink_ : IAudioSink::Active());
    if (!sink) return;
    for (int ch : plantingChannels_) {
        if (ch >= 0) sink->SetChannelPaused(ch, paused);
    }
    for (int ch : defuseChannels_) {
        if (ch >= 0) sink->SetChannelPaused(ch, paused);
    }
}

void CS2DClientView::StopObjectiveAudio(IAudioSink* audio) {
    if (audio) audioSink_ = audio;
    IAudioSink* sink = audio ? audio : (audioSink_ ? audioSink_ : IAudioSink::Active());
    for (int& ch : plantingChannels_) {
        if (sink && ch >= 0) sink->StopChannel(ch);
        ch = -1;
    }
    for (int& ch : defuseChannels_) {
        if (sink && ch >= 0) sink->StopChannel(ch);
        ch = -1;
    }
}

SoundEffect CS2DClientView::ReloadSoundFor(WeaponId id) {
    switch (id) {
        case WeaponId::Glock18: return SoundEffect::RELOAD_GLOCK;
        case WeaponId::USP_S: return SoundEffect::RELOAD_USP;
        case WeaponId::Deagle: return SoundEffect::RELOAD_DEAGLE;
        case WeaponId::GalilAR: return SoundEffect::RELOAD_GALIL;
        case WeaponId::AK47: return SoundEffect::RELOAD_AK47;
        case WeaponId::M4A1S: return SoundEffect::RELOAD_M4A1;
        case WeaponId::AWP: return SoundEffect::RELOAD_AWP;
        case WeaponId::Nova: return SoundEffect::RELOAD_SHOTGUN;
        case WeaponId::P2000: return SoundEffect::RELOAD_P2000;
        case WeaponId::DualBerettas: return SoundEffect::RELOAD_DUALBERETTAS;
        case WeaponId::P250: return SoundEffect::RELOAD_P250;
        case WeaponId::FiveSeven: return SoundEffect::RELOAD_FN57;
        case WeaponId::Tec9: return SoundEffect::RELOAD_TEC9;
        case WeaponId::CZ75: return SoundEffect::RELOAD_CZ75;
        case WeaponId::R8Revolver: return SoundEffect::RELOAD_R8;
        case WeaponId::MAC10: return SoundEffect::RELOAD_MAC10;
        case WeaponId::MP9: return SoundEffect::RELOAD_MP9;
        case WeaponId::MP7: return SoundEffect::RELOAD_MP7;
        case WeaponId::MP5SD: return SoundEffect::RELOAD_MP5SD;
        case WeaponId::UMP45: return SoundEffect::RELOAD_UMP45;
        case WeaponId::P90: return SoundEffect::RELOAD_P90;
        case WeaponId::FAMAS: return SoundEffect::RELOAD_FAMAS;
        case WeaponId::M4A4: return SoundEffect::RELOAD_M4A4;
        case WeaponId::SG553: return SoundEffect::RELOAD_SG553;
        case WeaponId::AUG: return SoundEffect::RELOAD_AUG;
        case WeaponId::SSG08: return SoundEffect::RELOAD_SSG08;
        case WeaponId::SCAR20: return SoundEffect::RELOAD_SCAR20;
        case WeaponId::G3SG1: return SoundEffect::RELOAD_G3SG1;
        case WeaponId::XM1014: return SoundEffect::RELOAD_XM1014;
        case WeaponId::SawedOff: return SoundEffect::RELOAD_SAWEDOFF;
        case WeaponId::MAG7: return SoundEffect::RELOAD_MAG7;
        case WeaponId::M249: return SoundEffect::RELOAD_M249;
        case WeaponId::Negev: return SoundEffect::RELOAD_NEGEV;
        default: return SoundEffect::CLICK;
    }
}

void CS2DClientView::SetAimTarget(CS2DSimulation& sim, float x, float y, bool followsPlayer) {
    auto& local = sim.LocalActor();
    local.aimWorldX = x;
    local.aimWorldY = y;
    aimFollowsPlayer_ = followsPlayer;
    aimOffsetX_ = x - local.x;
    aimOffsetY_ = y - local.y;
    if (local.alive) {
        local.aimAngle = std::atan2(y - local.y, x - local.x);
        local.aimDistance = std::max(std::hypot(x - local.x, y - local.y), 0.001f);
    }
}

CS2DClientView::ActorView CS2DClientView::GetActorView(
    const CS2DSimulation& sim, const CS2DSimulation::Player& actor) const {
    const ActorView current{actor.x, actor.y, actor.alive, actor.EffectiveAim(), actor.animTimer, actor.animState};
    if (!viewHistoryValid_) return current;
    const ActorView* previous = nullptr;
    const ActorId id = sim.ActorIdOf(actor);
    if (id >= 0 && static_cast<size_t>(id) < previousActorViews_.size()) {
        previous = &previousActorViews_[static_cast<size_t>(id)];
    }
    if (!previous || previous->alive != current.alive ||
        std::hypot(current.x - previous->x, current.y - previous->y) > actor.size * 4.0f) {
        return current;
    }
    return {previous->x + (current.x - previous->x) * renderInterpolation_,
            previous->y + (current.y - previous->y) * renderInterpolation_,
            current.alive,
            previous->aimAngle + std::remainder(current.aimAngle - previous->aimAngle, Tuning::Pi * 2.0f) * renderInterpolation_,
            previous->animState == current.animState
                ? previous->animTimer + (current.animTimer - previous->animTimer) * renderInterpolation_
                : current.animTimer,
            current.animState};
}

float CS2DClientView::GetRenderAimAngle(const CS2DSimulation& sim) const {
    const auto view = GetActorView(sim, sim.LocalActor());
    return std::atan2(GetRenderAimY(sim) - view.y, GetRenderAimX(sim) - view.x);
}

float CS2DClientView::GetRenderAimX(const CS2DSimulation& sim) const {
    const auto& local = sim.LocalActor();
    return aimFollowsPlayer_
        ? GetActorView(sim, local).x + std::cos(local.aimAngle) * local.aimDistance + local.RecoilOffsetX()
        : local.aimWorldX + local.RecoilOffsetX();
}

float CS2DClientView::GetRenderAimY(const CS2DSimulation& sim) const {
    const auto& local = sim.LocalActor();
    return aimFollowsPlayer_
        ? GetActorView(sim, local).y + std::sin(local.aimAngle) * local.aimDistance + local.RecoilOffsetY()
        : local.aimWorldY + local.RecoilOffsetY();
}

// ============================================================================
// 开镜
// ============================================================================

bool CS2DClientView::IsScopeSupported(WeaponId id) {
    if (id == WeaponId::None) return false;
    return WeaponDB::Get(id).scoped;
}

void CS2DClientView::ResetScope(const CS2DSimulation& sim, Presentation::ScopeResetReason reason) {
    (void)reason;
    const WeaponId cur = sim.LocalActor().CurrentWeapon().id;
    scopeCloseOnRelease_ = false;
    scopeLevel_      = 0;
    scopeEngagement_ = 0.0f;
    scopeEngFrom_    = scopeEngTo_ = 0.0f;
    scopeEngT_       = 1.0f;
    scopeZoom_       = 1.0f;
    previousScopeZoom_ = 1.0f;
    previousScopeEngagement_ = 0.0f;
    scopeZoomFrom_   = scopeZoomTo_ = 1.0f;
    scopeZoomT_      = 1.0f;

    scope_.weapon       = cur;
    scope_.supported    = IsScopeSupported(cur);
    scope_.active       = false;
    scope_.level        = 0;
    scope_.transition   = 0.0f;
    scope_.zoomMultiplier       = 1.0f;
    scope_.targetZoomMultiplier = 1.0f;
}

Presentation::ScopeView CS2DClientView::GetScopeView(const CS2DSimulation& sim) const {
    Presentation::ScopeView view = scope_;
    const auto& local = sim.LocalActor();
    const WeaponId cur = local.CurrentWeapon().id;
    const int level = std::clamp(scopeLevel_, 0, Presentation::kScopeLevelCount - 1);

    view.weapon    = cur;
    view.supported = IsScopeSupported(cur);
    view.level     = level;
    view.active    = (level > 0);
    view.transition = scopeEngagement_;
    view.zoomMultiplier = scopeZoom_;
    view.targetZoomMultiplier = Presentation::kScopeZoomLevels[level];

    if (!view.supported || !local.alive) {
        view.active         = false;
        view.level          = 0;
        view.transition     = 0.0f;
        view.zoomMultiplier = 1.0f;
        view.targetZoomMultiplier = 1.0f;
    }
    return view;
}

Presentation::ScopeView CS2DClientView::GetRenderScopeView(const CS2DSimulation& sim) const {
    auto view = GetScopeView(sim);
    if (viewHistoryValid_ && view.supported && sim.LocalActor().alive) {
        view.zoomMultiplier = previousScopeZoom_ + (view.zoomMultiplier - previousScopeZoom_) * renderInterpolation_;
        view.transition = previousScopeEngagement_ + (view.transition - previousScopeEngagement_) * renderInterpolation_;
    }
    return view;
}

void CS2DClientView::UpdateScopeInput(CS2DSimulation& sim, bool rightClicked, bool gameplayInputAllowed) {
    const auto& local = sim.LocalActor();
    const WeaponId cur = local.CurrentWeapon().id;

    if (!gameplayInputAllowed) {
        ResetScope(sim, Presentation::ScopeResetReason::InputBlocked);
        sim.SetActorScopeView(sim.GetLocalActorId(), scopeLevel_, scopeEngagement_);
        return;
    }
    if (!local.alive) {
        ResetScope(sim, Presentation::ScopeResetReason::PlayerDied);
        sim.SetActorScopeView(sim.GetLocalActorId(), scopeLevel_, scopeEngagement_);
        return;
    }
    if (cur != scope_.weapon) {
        ResetScope(sim, Presentation::ScopeResetReason::WeaponChanged);
    }
    if (local.reloading) {
        if (scopeLevel_ > 0) ResetScope(sim, Presentation::ScopeResetReason::ReloadStarted);
        sim.SetActorScopeView(sim.GetLocalActorId(), scopeLevel_, scopeEngagement_);
        return;
    }
    {
        const WeaponDef& def = local.CurrentWeapon();
        if (!def.automatic && local.shootCooldown > 0.0f) return;
    }
    if (!rightClicked) return;
    if (!IsScopeSupported(cur)) return;

    const int next = (scopeLevel_ + 1) % Presentation::kScopeLevelCount;
    scope_.weapon    = cur;
    scope_.supported = true;
    scopeLevel_      = next;

    scopeZoomFrom_ = scopeZoom_;
    scopeZoomTo_   = Presentation::kScopeZoomLevels[next];
    scopeZoomT_    = 0.0f;

    scopeEngFrom_  = scopeEngagement_;
    scopeEngTo_    = (next > 0) ? 1.0f : 0.0f;
    scopeEngT_     = 0.0f;

    sim.SetActorScopeView(sim.GetLocalActorId(), scopeLevel_, scopeEngagement_);
}

void CS2DClientView::TickScope(float dt, CS2DSimulation& sim) {
    const auto& local = sim.LocalActor();
    const WeaponId cur = local.CurrentWeapon().id;
    scope_.weapon    = cur;
    scope_.supported = IsScopeSupported(cur);
    if (!local.alive) {
        ResetScope(sim, Presentation::ScopeResetReason::PlayerDied);
        return;
    }
    if (!scope_.supported) {
        if (scopeLevel_ > 0 || scopeZoom_ > 1.0f) {
            ResetScope(sim, Presentation::ScopeResetReason::WeaponChanged);
        }
        return;
    }
    if (local.reloading) {
        if (scopeLevel_ > 0) ResetScope(sim, Presentation::ScopeResetReason::ReloadStarted);
        return;
    }

    const float step = (Presentation::kScopeTransitionSeconds > 0.0f)
                           ? dt / Presentation::kScopeTransitionSeconds : 1.0f;
    if (scopeZoomT_ < 1.0f) scopeZoomT_ = std::min(1.0f, scopeZoomT_ + step);
    const float zoomEase = scopeZoomT_ * scopeZoomT_ * (3.0f - 2.0f * scopeZoomT_);
    scopeZoom_ = scopeZoomFrom_ + (scopeZoomTo_ - scopeZoomFrom_) * zoomEase;

    if (scopeEngT_ < 1.0f) scopeEngT_ = std::min(1.0f, scopeEngT_ + step);
    const float engagementEase = scopeEngT_ * scopeEngT_ * (3.0f - 2.0f * scopeEngT_);
    scopeEngagement_ = scopeEngFrom_ + (scopeEngTo_ - scopeEngFrom_) * engagementEase;
}

float CS2DClientView::ScopeCameraShiftForZoom(float zoom) {
    const float low = Presentation::kScopeZoomLevels[1];
    const float high = Presentation::kScopeZoomLevels[2];
    const float z = std::clamp(zoom, 1.0f, high);
    if (z <= low) {
        return Tuning::CameraLookShift + (z - 1.0f) / (low - 1.0f) *
            (Tuning::ScopeCameraShiftLow - Tuning::CameraLookShift);
    }
    return Tuning::ScopeCameraShiftLow + (z - low) / (high - low) *
        (Presentation::kScopeCameraShiftMax - Tuning::ScopeCameraShiftLow);
}

// ============================================================================
// 观察者子系统
// ============================================================================

ActorId CS2DClientView::NextTeammateBot(const CS2DSimulation& sim, ActorId from, int dir) const {
    const auto& actors = sim.GetActors();
    const int n = static_cast<int>(actors.size());
    if (n <= 0) return kInvalidActorId;
    const Team myTeam = sim.LocalActor().team;
    const ActorId localId = sim.GetLocalActorId();
    const int start = (from >= 0 && from < n) ? from : -1;
    for (int step = 1; step <= n; ++step) {
        const ActorId idx = ((start + dir * step) % n + n) % n;
        const auto& a = actors[static_cast<size_t>(idx)];
        if (idx != localId && a.IsActive() && a.alive && a.team == myTeam) {
            return idx;
        }
    }
    return kInvalidActorId;
}

const CS2DSimulation::Player* CS2DClientView::GetSpectatedPlayer(const CS2DSimulation& sim) const {
    if (specMode_ == SpectateMode::Follow && !IsDeathFreeze(sim)) {
        const auto* p = sim.FindActor(specTarget_);
        if (p && p->IsActive() && p->alive && p->id != sim.GetLocalActorId()) return p;
    }
    return nullptr;
}

std::string CS2DClientView::GetSpectatedName(const CS2DSimulation& sim) const {
    const auto* p = GetSpectatedPlayer(sim);
    if (p) {
        if (!p->name.empty()) return p->name;
        return (p->IsHuman() ? "P" : "BOT#") + std::to_string(p->id);
    }
    if (specTarget_ != kInvalidActorId) return "BOT#" + std::to_string(specTarget_);
    return "-";
}

void CS2DClientView::SpectatorBegin(const CS2DSimulation& sim) {
    if (sim.GetMatchMode() == MatchMode::DeathMatch) return;
    specMode_ = SpectateMode::Follow;
    const auto& local = sim.LocalActor();
    specFreeX_ = local.x;
    specFreeY_ = local.y;
    specTarget_ = NextTeammateBot(sim, kInvalidActorId, +1);
    UpdateSpectatorState(sim);
}

void CS2DClientView::UpdateSpectatorState(const CS2DSimulation& sim) {
    const auto& local = sim.LocalActor();
    if (local.alive || IsDeathFreeze(sim) || sim.GetMatchMode() == MatchMode::DeathMatch) return;
    if (sim.IsBombPlanted() && sim.AliveCount(local.team) == 0) {
        specMode_ = SpectateMode::Bomb;
        specTarget_ = kInvalidActorId;
        return;
    }
    if (specMode_ == SpectateMode::Bomb) return;
    if (specMode_ == SpectateMode::None) {
        specFreeX_ = local.x;
        specFreeY_ = local.y;
        specMode_ = SpectateMode::Follow;
    }
    if (specMode_ == SpectateMode::Follow && !GetSpectatedPlayer(sim)) {
        specTarget_ = NextTeammateBot(sim, specTarget_, 1);
        if (specTarget_ == kInvalidActorId) specMode_ = SpectateMode::Free;
    }
}

void CS2DClientView::SpectatorEnd() {
    specMode_ = SpectateMode::None;
    specTarget_ = kInvalidActorId;
}

void CS2DClientView::SpectatorToggle(const CS2DSimulation& sim) {
    if (IsDeathFreeze(sim) || specMode_ == SpectateMode::Bomb) return;
    if (sim.GetMatchMode() == MatchMode::DeathMatch) return;
    if (specMode_ == SpectateMode::Follow) {
        if (const auto* target = sim.FindActor(specTarget_)) {
            specFreeX_ = target->x;
            specFreeY_ = target->y;
        }
        specMode_ = SpectateMode::Free;
    } else {
        specMode_ = SpectateMode::Follow;
        const auto* target = sim.FindActor(specTarget_);
        if (!target || !target->IsActive() || !target->alive || target->id == sim.GetLocalActorId()) {
            specTarget_ = NextTeammateBot(sim, kInvalidActorId, +1);
        }
    }
}

void CS2DClientView::SpectatorCycle(const CS2DSimulation& sim, int dir) {
    if (sim.GetMatchMode() == MatchMode::DeathMatch) return;
    if (specMode_ != SpectateMode::Follow) return;
    const ActorId found = NextTeammateBot(sim, specTarget_, dir);
    if (found != kInvalidActorId) specTarget_ = found;
}

void CS2DClientView::SpectatorMoveFree(const CS2DSimulation& sim, float dx, float dy, float dt) {
    if (sim.GetMatchMode() == MatchMode::DeathMatch) return;
    if (specMode_ != SpectateMode::Free) return;

    const float speed = 840.0f;
    specFreeX_ += dx * speed * dt;
    specFreeY_ += dy * speed * dt;
    specFreeX_ = std::clamp(specFreeX_, 0.0f, sim.GetWorld().GetMapPixelW());
    specFreeY_ = std::clamp(specFreeY_, 0.0f, sim.GetWorld().GetMapPixelH());
}

std::size_t CS2DClientView::CollectFreeSpectateObservers(const CS2DSimulation& sim,
                                                         VisibilityQuery::Observer* out,
                                                         std::size_t capacity) const {
    if (!out || capacity == 0) return 0;
    std::size_t count = 0;
    const Team myTeam = sim.LocalActor().team;
    const ActorId localId = sim.GetLocalActorId();
    for (const auto& actor : sim.GetActors()) {
        if (count >= capacity) break;
        if (!actor.IsActive() || !actor.alive || actor.id == localId || actor.team != myTeam) continue;
        const auto view = GetActorView(sim, actor);
        out[count++] = {view.x, view.y, view.aimAngle};
    }
    return count;
}

float CS2DClientView::ListenerX(const CS2DSimulation& sim) const {
    if (specMode_ == SpectateMode::Bomb) return sim.GetBombX();
    if (specMode_ == SpectateMode::Follow) {
        if (const auto* target = sim.FindActor(specTarget_)) return target->x;
    }
    if (specMode_ == SpectateMode::Free) return specFreeX_;
    return sim.LocalActor().x;
}

float CS2DClientView::ListenerY(const CS2DSimulation& sim) const {
    if (specMode_ == SpectateMode::Bomb) return sim.GetBombY();
    if (specMode_ == SpectateMode::Follow) {
        if (const auto* target = sim.FindActor(specTarget_)) return target->y;
    }
    if (specMode_ == SpectateMode::Free) return specFreeY_;
    return sim.LocalActor().y;
}
