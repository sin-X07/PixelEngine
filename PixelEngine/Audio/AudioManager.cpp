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

    auto loadChunk = [](const std::string& relPath, const char* name) -> Mix_Chunk* {
        const std::string fullPath = PathUtils::ResolveProjectFile(relPath);
        Mix_Chunk* chunk = Mix_LoadWAV(fullPath.c_str());
        if (!chunk) {
            std::cerr << name << " 音效加载失败: " << Mix_GetError() << std::endl;
        }
        return chunk;
    };

    pickupSound = loadChunk("assets/audio/pickup.wav", "金币");
    buttonHoverSound = loadChunk("assets/audio/button.ogg", "按钮悬停");
    clickSound = loadChunk("assets/audio/click.ogg", "按钮点击");
    attackSound = loadChunk("assets/audio/attack_swing.wav", "挥剑攻击");
    breakSound = loadChunk("assets/audio/obstacle_break.wav", "障碍物击碎");
    deathSound = loadChunk("assets/audio/player_death.wav", "惨死");
    shootSound = loadChunk("assets/audio/shoot.ogg", "射击");
    hitSound = loadChunk("assets/audio/hit.ogg", "命中");
    killSound = loadChunk("assets/audio/kill.ogg", "击杀");
    playerHurtSound = loadChunk("assets/audio/player_hurt.ogg", "玩家受击");
    ak47Sound = loadChunk("assets/audio/ak47.mp3", "AK47 射击");
    deagleSound = loadChunk("assets/audio/deagle.mp3", "沙漠之鹰射击");
    katanaSound = loadChunk("assets/audio/katana.mp3", "武士刀挥砍");

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
    if (attackSound) { Mix_FreeChunk(attackSound); attackSound = nullptr; }
    if (breakSound) { Mix_FreeChunk(breakSound); breakSound = nullptr; }
    if (deathSound) { Mix_FreeChunk(deathSound); deathSound = nullptr; }
    if (shootSound) { Mix_FreeChunk(shootSound); shootSound = nullptr; }
    if (hitSound) { Mix_FreeChunk(hitSound); hitSound = nullptr; }
    if (killSound) { Mix_FreeChunk(killSound); killSound = nullptr; }
    if (playerHurtSound) { Mix_FreeChunk(playerHurtSound); playerHurtSound = nullptr; }
    if (ak47Sound) { Mix_FreeChunk(ak47Sound); ak47Sound = nullptr; }
    if (deagleSound) { Mix_FreeChunk(deagleSound); deagleSound = nullptr; }
    if (katanaSound) { Mix_FreeChunk(katanaSound); katanaSound = nullptr; }

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

void AudioManager::PlaySound(SoundEffect effect) {
    if (!audioInitialized) return;

    Mix_Chunk* targetChunk = nullptr;
    switch (effect) {
        case SoundEffect::PICKUP: targetChunk = pickupSound; break;
        case SoundEffect::BUTTON_HOVER: targetChunk = buttonHoverSound; break;
        case SoundEffect::CLICK: targetChunk = clickSound; break;
        case SoundEffect::ATTACK_SWING: targetChunk = attackSound; break;
        case SoundEffect::OBSTACLE_BREAK: targetChunk = breakSound; break;
        case SoundEffect::PLAYER_DEATH: targetChunk = deathSound; break;
        case SoundEffect::SHOOT: targetChunk = shootSound; break;
        case SoundEffect::HIT: targetChunk = hitSound; break;
        case SoundEffect::KILL: targetChunk = killSound; break;
        case SoundEffect::PLAYER_HURT: targetChunk = playerHurtSound; break;
        case SoundEffect::AK47_FIRE: targetChunk = ak47Sound; break;
        case SoundEffect::DEAGLE_FIRE: targetChunk = deagleSound; break;
        case SoundEffect::KATANA_SWING: targetChunk = katanaSound; break;
    }

    if (targetChunk) {
        Mix_PlayChannel(-1, targetChunk, 0);
    }
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
