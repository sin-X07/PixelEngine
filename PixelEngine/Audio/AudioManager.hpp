#pragma once
#include <SDL2/SDL.h>
#include <SDL_mixer.h>
#include <string>

enum class SoundEffect {
    PICKUP,
    BUTTON_HOVER,
    CLICK,
    ATTACK_SWING,
    OBSTACLE_BREAK,
    PLAYER_DEATH,
    SHOOT,       // 射击
    HIT,         // 命中
    KILL,        // 击杀
    PLAYER_HURT  // 玩家受击
};

class AudioManager {
private:
    bool audioInitialized = false;
    Mix_Music* backgroundMusic = nullptr;
    Mix_Chunk* pickupSound = nullptr;
    Mix_Chunk* buttonHoverSound = nullptr;
    Mix_Chunk* clickSound = nullptr;
    Mix_Chunk* attackSound = nullptr;
    Mix_Chunk* breakSound = nullptr;
    Mix_Chunk* deathSound = nullptr;
    Mix_Chunk* shootSound = nullptr;
    Mix_Chunk* hitSound = nullptr;
    Mix_Chunk* killSound = nullptr;
    Mix_Chunk* playerHurtSound = nullptr;

    int masterVolume = 100;
    int musicVolume = 25;
    int effectsVolume = 100;
    bool masterMuted = false;

public:
    AudioManager() = default;
    ~AudioManager() { Clean(); }

    bool Initialize();
    void Clean();

    void PlayMusic(const std::string& filename);
    void PauseMusic();
    void ResumeMusic();
    void StopMusic();

    void PlaySound(SoundEffect effect);
    void UpdateVolumes();

    void SetMasterVolume(int vol);
    void SetMusicVolume(int vol);
    void SetEffectsVolume(int vol);
    void ToggleMute();
    void SetMuted(bool mute);

    int GetMasterVolume() const { return masterVolume; }
    int GetMusicVolume() const { return musicVolume; }
    int GetEffectsVolume() const { return effectsVolume; }
    bool IsMuted() const { return masterMuted; }
    bool IsInitialized() const { return audioInitialized; }
};
