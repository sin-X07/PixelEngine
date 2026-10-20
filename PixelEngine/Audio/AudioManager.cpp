#include "AudioManager.hpp"
#include "../Core/PathUtils.hpp"
#include <algorithm>
#include <iostream>

bool AudioManager::Initialize() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL 音频子系统初始化失败，游戏将静音运行: " << SDL_GetError() << std::endl;
        return false;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "SDL_mixer 音频设备打开失败，游戏将静音运行: " << Mix_GetError() << std::endl;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    audioInitialized = true;

    int failedAudio = 0;
    auto loadChunk = [&failedAudio](const std::string& relPath, const char* name) -> Mix_Chunk* {
        const std::string fullPath = PathUtils::ResolveProjectFile(relPath);
        Mix_Chunk* chunk = Mix_LoadWAV(fullPath.c_str());
        if (!chunk) {
            ++failedAudio;
            std::cerr << "  [Audio] 缺失: " << relPath << " (" << name << ")" << std::endl;
        }
        return chunk;
    };

    pickupSound = loadChunk("assets/audio/pickup.wav", "金币");
    buttonHoverSound = loadChunk("assets/audio/button.ogg", "按钮悬停");
    clickSound = loadChunk("assets/audio/click.ogg", "按钮点击");
    daggerSound = loadChunk("assets/audio/dagger_attack.wav", "匕首攻击");
    breakSound = loadChunk("assets/audio/obstacle_break.wav", "障碍物击碎");
    shootSound = loadChunk("assets/audio/deagle.mp3", "射击");
    hitSound = loadChunk("assets/audio/hit.wav", "命中");
    killSound = loadChunk("assets/audio/kill.wav", "击杀");
    ak47Sound = loadChunk("assets/audio/ak47.wav", "AK47 射击");
    deagleSound = loadChunk("assets/audio/deagle.mp3", "沙漠之鹰射击");
    katanaSound = loadChunk("assets/audio/katana.mp3", "武士刀挥砍");

    // 战局事件
    grenadeExplodeSound = loadChunk("assets/audio/grenade_explode.wav", "手雷爆炸");
    flashbangSound      = loadChunk("assets/audio/flashbang.wav", "闪光弹");
    smokeSound          = loadChunk("assets/audio/smoke.wav", "烟雾弹");
    bombPlantSound      = loadChunk("assets/audio/bomb_plant.wav", "下包");
    bombDefuseSound     = loadChunk("assets/audio/bomb_defuse.wav", "拆包");
    bombBeepSound       = loadChunk("assets/audio/bomb_beep.wav", "C4 滴答");
    roundStartSound     = loadChunk("assets/audio/round_start.wav", "回合开始");
    roundWinSound       = loadChunk("assets/audio/round_win.wav", "回合胜利");
    roundLoseSound      = loadChunk("assets/audio/round_lose.wav", "回合失败");
    footstepSound       = loadChunk("assets/audio/footstep.wav", "脚步");
    armorHitSound       = loadChunk("assets/audio/armor_hit.wav", "护甲受击");

    // 分武器枪声
    glockFireSound   = loadChunk("assets/audio/glock.wav", "Glock 射击");
    m4a1FireSound    = loadChunk("assets/audio/m4a1.mp3", "M4A1 射击");
    awpFireSound     = loadChunk("assets/audio/awp.wav", "AWP 射击");
    shotgunFireSound = loadChunk("assets/audio/shotgun.wav", "霰弹枪射击");
    smgFireSound     = loadChunk("assets/audio/smg.wav", "SMG 射击");
    galilFireSound   = loadChunk("assets/audio/galir.wav", "Galil 射击");

    if (failedAudio > 0) {
        std::cerr << "[Asset] 音效缺失 " << failedAudio
                  << " 项，请参照 tools/ASSET_SPEC.md 补齐" << std::endl;
    }

    UpdateVolumes();
    return true;
}

void AudioManager::Clean() {
    if (!audioInitialized) return;

    Mix_HaltMusic();
    Mix_HaltChannel(-1);

    if (backgroundMusic) { Mix_FreeMusic(backgroundMusic); backgroundMusic = nullptr; }
    if (pickupSound) { Mix_FreeChunk(pickupSound); pickupSound = nullptr; }
    if (buttonHoverSound) { Mix_FreeChunk(buttonHoverSound); buttonHoverSound = nullptr; }
    if (clickSound) { Mix_FreeChunk(clickSound); clickSound = nullptr; }
    if (daggerSound) { Mix_FreeChunk(daggerSound); daggerSound = nullptr; }
    if (breakSound) { Mix_FreeChunk(breakSound); breakSound = nullptr; }
    if (shootSound) { Mix_FreeChunk(shootSound); shootSound = nullptr; }
    if (hitSound) { Mix_FreeChunk(hitSound); hitSound = nullptr; }
    if (killSound) { Mix_FreeChunk(killSound); killSound = nullptr; }
    if (ak47Sound) { Mix_FreeChunk(ak47Sound); ak47Sound = nullptr; }
    if (deagleSound) { Mix_FreeChunk(deagleSound); deagleSound = nullptr; }
    if (katanaSound) { Mix_FreeChunk(katanaSound); katanaSound = nullptr; }

    // 战局事件
    if (grenadeExplodeSound) { Mix_FreeChunk(grenadeExplodeSound); grenadeExplodeSound = nullptr; }
    if (flashbangSound)      { Mix_FreeChunk(flashbangSound);      flashbangSound = nullptr; }
    if (smokeSound)          { Mix_FreeChunk(smokeSound);          smokeSound = nullptr; }
    if (bombPlantSound)      { Mix_FreeChunk(bombPlantSound);      bombPlantSound = nullptr; }
    if (bombDefuseSound)     { Mix_FreeChunk(bombDefuseSound);     bombDefuseSound = nullptr; }
    if (bombBeepSound)       { Mix_FreeChunk(bombBeepSound);       bombBeepSound = nullptr; }
    if (roundStartSound)     { Mix_FreeChunk(roundStartSound);     roundStartSound = nullptr; }
    if (roundWinSound)       { Mix_FreeChunk(roundWinSound);       roundWinSound = nullptr; }
    if (roundLoseSound)      { Mix_FreeChunk(roundLoseSound);      roundLoseSound = nullptr; }
    if (footstepSound)       { Mix_FreeChunk(footstepSound);       footstepSound = nullptr; }
    if (armorHitSound)       { Mix_FreeChunk(armorHitSound);       armorHitSound = nullptr; }

    // 分武器枪声
    if (glockFireSound)   { Mix_FreeChunk(glockFireSound);   glockFireSound = nullptr; }
    if (m4a1FireSound)    { Mix_FreeChunk(m4a1FireSound);    m4a1FireSound = nullptr; }
    if (awpFireSound)     { Mix_FreeChunk(awpFireSound);     awpFireSound = nullptr; }
    if (shotgunFireSound) { Mix_FreeChunk(shotgunFireSound); shotgunFireSound = nullptr; }
    if (smgFireSound)     { Mix_FreeChunk(smgFireSound);     smgFireSound = nullptr; }
    if (galilFireSound)   { Mix_FreeChunk(galilFireSound);   galilFireSound = nullptr; }

    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audioInitialized = false;
}

void AudioManager::PlayMusic(const std::string& filename) {
    if (!audioInitialized) return;

    if (backgroundMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(backgroundMusic);
        backgroundMusic = nullptr;
    }

    const std::string path = PathUtils::ResolveProjectFile(filename);
    backgroundMusic = Mix_LoadMUS(path.c_str());
    if (backgroundMusic) {
        UpdateVolumes();
        Mix_PlayMusic(backgroundMusic, -1);
    } else {
        std::cerr << "背景音乐加载失败: " << Mix_GetError() << std::endl;
    }
}

void AudioManager::PauseMusic() {
    if (audioInitialized) Mix_PauseMusic();
}

void AudioManager::ResumeMusic() {
    if (audioInitialized) Mix_ResumeMusic();
}

void AudioManager::StopMusic() {
    if (audioInitialized) Mix_HaltMusic();
}

// 效果 → 音频块（播放入口共用，避免 switch 重复）
Mix_Chunk* AudioManager::ChunkFor(SoundEffect effect) const {
    switch (effect) {
        case SoundEffect::PICKUP:           return pickupSound;
        case SoundEffect::BUTTON_HOVER:     return buttonHoverSound;
        case SoundEffect::CLICK:            return clickSound;
        case SoundEffect::DAGGER_FIRE:      return daggerSound;
        case SoundEffect::OBSTACLE_BREAK:   return breakSound;
        case SoundEffect::SHOOT:            return shootSound;
        case SoundEffect::HIT:              return hitSound;
        case SoundEffect::KILL:             return killSound;
        case SoundEffect::AK47_FIRE:        return ak47Sound;
        case SoundEffect::DEAGLE_FIRE:      return deagleSound;
        case SoundEffect::KATANA_SWING:     return katanaSound;

        case SoundEffect::GRENADE_EXPLODE:  return grenadeExplodeSound;
        case SoundEffect::FLASHBANG:        return flashbangSound;
        case SoundEffect::SMOKE:            return smokeSound;
        case SoundEffect::BOMB_PLANT:       return bombPlantSound;
        case SoundEffect::BOMB_DEFUSE:      return bombDefuseSound;
        case SoundEffect::BOMB_BEEP:        return bombBeepSound;
        case SoundEffect::ROUND_START:      return roundStartSound;
        case SoundEffect::ROUND_WIN:        return roundWinSound;
        case SoundEffect::ROUND_LOSE:       return roundLoseSound;
        case SoundEffect::FOOTSTEP:         return footstepSound;
        case SoundEffect::ARMOR_HIT:        return armorHitSound;

        case SoundEffect::GLOCK_FIRE:       return glockFireSound;
        case SoundEffect::M4A1_FIRE:        return m4a1FireSound;
        case SoundEffect::AWP_FIRE:         return awpFireSound;
        case SoundEffect::SHOTGUN_FIRE:     return shotgunFireSound;
        case SoundEffect::SMG_FIRE:         return smgFireSound;
        case SoundEffect::GALIL_FIRE:       return galilFireSound;
    }
    return nullptr;
}

void AudioManager::PlaySound(SoundEffect effect) {
    if (!audioInitialized) return;

    Mix_Chunk* targetChunk = ChunkFor(effect);
    if (targetChunk) {
        // 自动武器共用专用通道：新枪声覆盖旧枪声，避免通道耗尽后失声
        const bool autoWeapon = (effect == SoundEffect::AK47_FIRE ||
                                 effect == SoundEffect::M4A1_FIRE ||
                                 effect == SoundEffect::GALIL_FIRE ||
                                 effect == SoundEffect::SMG_FIRE);
        const int chan = autoWeapon ? CHANNEL_AK47_FIRE : -1;
        Mix_PlayChannel(chan, targetChunk, 0);
    }
}

void AudioManager::PlaySoundAt(SoundEffect effect, float sourceX, float sourceY,
                                float listenerX, float listenerY) {
    if (!audioInitialized) return;

    Mix_Chunk* targetChunk = ChunkFor(effect);
    if (!targetChunk) return;

    const int chan = Mix_PlayChannel(-1, targetChunk, 0);
    if (chan < 0) return;

    // ---- 声像：由「声源相对玩家的世界位置」决定 ----
    // 俯视视角镜头不做旋转，因此屏幕水平方向恒等于世界 +x。
    // 注意：这里刻意不使用玩家朝向，避免转身时左右声道跟着翻转。
    const float dx = sourceX - listenerX;
    const float dy = sourceY - listenerY;
    const float dist = std::sqrt(dx * dx + dy * dy);

    float pan = 0.0f;
    if (dist > 0.001f) {
        pan = dx / dist;                       // -1=左侧, 0=正上/正下, +1=右侧
        // 贴脸时淡化声像，避免极近距离突然完全单边
        const float nearFade = std::min(1.0f, dist / 40.0f);
        pan *= nearFade;
        pan *= 0.85f;                          // 保留少量另一声道，听感更自然
    }

    const int leftVol  = static_cast<int>((1.0f - pan) * 0.5f * 255.0f);
    const int rightVol = static_cast<int>((1.0f + pan) * 0.5f * 255.0f);
    Mix_SetPanning(chan, leftVol, rightVol);

    // 距离衰减：200 单位内正常，超出按线性衰减到 0
    constexpr float FULL_RANGE = 200.0f;
    constexpr float MAX_RANGE  = 400.0f;
    float distFactor = 1.0f;
    if (dist > FULL_RANGE) {
        distFactor = 1.0f - std::min(1.0f, (dist - FULL_RANGE) / (MAX_RANGE - FULL_RANGE));
    }
    const int vol = static_cast<int>(distFactor * MIX_MAX_VOLUME);
    Mix_Volume(chan, vol);
}

void AudioManager::UpdateVolumes() {
    if (!audioInitialized) return;

    const int effectiveMaster = masterMuted ? 0 : masterVolume;
    const int musicLevel = effectiveMaster * musicVolume * MIX_MAX_VOLUME / 10000;
    const int effectsLevel = effectiveMaster * effectsVolume * MIX_MAX_VOLUME / 10000;

    Mix_VolumeMusic(musicLevel);
    Mix_Volume(-1, effectsLevel);
}

void AudioManager::SetMasterVolume(int vol) {
    masterVolume = std::clamp(vol, 0, 100);
    masterMuted = false;
    UpdateVolumes();
}

void AudioManager::SetMusicVolume(int vol) {
    musicVolume = std::clamp(vol, 0, 100);
    UpdateVolumes();
}

void AudioManager::SetEffectsVolume(int vol) {
    effectsVolume = std::clamp(vol, 0, 100);
    UpdateVolumes();
}

void AudioManager::ToggleMute() {
    masterMuted = !masterMuted;
    UpdateVolumes();
}

void AudioManager::SetMuted(bool mute) {
    masterMuted = mute;
    UpdateVolumes();
}
