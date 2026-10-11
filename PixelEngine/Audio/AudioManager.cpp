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

    pickupSound = loadChunk("../assets/audio/pickup.wav", "金币");
    buttonHoverSound = loadChunk("../assets/audio/button.ogg", "按钮悬停");
    clickSound = loadChunk("../assets/audio/click.ogg", "按钮点击");
    attackSound = loadChunk("../assets/audio/attack_swing.wav", "挥剑攻击");
    breakSound = loadChunk("../assets/audio/obstacle_break.wav", "障碍物击碎");
    deathSound = loadChunk("../assets/audio/player_death.wav", "惨死");

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
