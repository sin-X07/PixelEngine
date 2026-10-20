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
    daggerSound = loadChunk("assets/audio/dagger_attack.wav", "匕首攻击");
    breakSound = loadChunk("assets/audio/obstacle_break.wav", "障碍物击碎");
    shootSound = loadChunk("assets/audio/deagle.mp3", "射击");
    hitSound = loadChunk("assets/audio/hit.wav", "命中");
    killSound = loadChunk("assets/audio/kill.wav", "击杀");
    ak47Sound = loadChunk("assets/audio/ak47.wav", "AK47 射击");
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
    if (daggerSound) { Mix_FreeChunk(daggerSound); daggerSound = nullptr; }
    if (breakSound) { Mix_FreeChunk(breakSound); breakSound = nullptr; }
    if (shootSound) { Mix_FreeChunk(shootSound); shootSound = nullptr; }
    if (hitSound) { Mix_FreeChunk(hitSound); hitSound = nullptr; }
    if (killSound) { Mix_FreeChunk(killSound); killSound = nullptr; }
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
        case SoundEffect::DAGGER_FIRE: targetChunk = daggerSound; break;
        case SoundEffect::OBSTACLE_BREAK: targetChunk = breakSound; break;
        case SoundEffect::SHOOT: targetChunk = shootSound; break;
        case SoundEffect::HIT: targetChunk = hitSound; break;
        case SoundEffect::KILL: targetChunk = killSound; break;
        case SoundEffect::AK47_FIRE: targetChunk = ak47Sound; break;
        case SoundEffect::DEAGLE_FIRE: targetChunk = deagleSound; break;
        case SoundEffect::KATANA_SWING: targetChunk = katanaSound; break;
    }

    if (targetChunk) {
        // AK47 连发使用专用通道，新枪声直接覆盖旧枪声，避免通道耗尽后失声
        const int chan = (effect == SoundEffect::AK47_FIRE) ? CHANNEL_AK47_FIRE : -1;
        Mix_PlayChannel(chan, targetChunk, 0);
    }
}

void AudioManager::PlaySoundAt(SoundEffect effect, float sourceX, float sourceY,
                                float listenerX, float listenerY, float listenerAngle) {
    if (!audioInitialized) return;

    Mix_Chunk* targetChunk = nullptr;
    int reservedChan = -1;
    switch (effect) {
        case SoundEffect::PICKUP:       targetChunk = pickupSound;      break;
        case SoundEffect::BUTTON_HOVER: targetChunk = buttonHoverSound; break;
        case SoundEffect::CLICK:        targetChunk = clickSound;       break;
        case SoundEffect::DAGGER_FIRE: targetChunk = daggerSound;      break;
        case SoundEffect::OBSTACLE_BREAK: targetChunk = breakSound;     break;
        case SoundEffect::SHOOT:        targetChunk = shootSound;       break;
        case SoundEffect::HIT:          targetChunk = hitSound;         break;
        case SoundEffect::KILL:         targetChunk = killSound;        break;
        case SoundEffect::AK47_FIRE:    targetChunk = ak47Sound;        break;
        case SoundEffect::DEAGLE_FIRE:  targetChunk = deagleSound;      break;
        case SoundEffect::KATANA_SWING: targetChunk = katanaSound;      break;
    }

    if (!targetChunk) return;

    const int chan = Mix_PlayChannel(reservedChan, targetChunk, 0);
    if (chan < 0) return;

    // ---- 计算方位：声源相对于听者的方向 ----
    const float dx = sourceX - listenerX;
    const float dy = sourceY - listenerY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float sourceAngle = std::atan2(dy, dx);

    // 相对角度（声源在听者正前方 = 0，右侧 = +π/2，左侧 = -π/2）
    float relAngle = sourceAngle - listenerAngle;
    while (relAngle >  3.14159265f) relAngle -= 6.28318531f;
    while (relAngle < -3.14159265f) relAngle += 6.28318531f;

    // 全景定位：sin 映射到左右声道
    const float pan = std::sin(relAngle);  // -1=全左, 0=居中, +1=全右
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
