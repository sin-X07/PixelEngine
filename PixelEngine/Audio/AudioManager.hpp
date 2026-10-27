#pragma once
#include <SDL2/SDL.h>
#include <SDL_mixer.h>
#include <string>

enum class SoundEffect {
    PICKUP,
    BUTTON_HOVER,
    CLICK,
    DAGGER_FIRE,
    OBSTACLE_BREAK,
    SHOOT,        // 射击（通用，沙漠之鹰用）
    HIT,          // 命中
    KILL,         // 击杀
    AK47_FIRE,    // AK47 连发枪声
    DEAGLE_FIRE,  // 沙漠之鹰单发枪声（备用）
    KATANA_SWING, // 武士刀挥砍

    // ---- 战局事件 ----
    GRENADE_EXPLODE,
    EQUIP,           // 购买装备
    GRENADE_THROW,   // 投掷出手（通用兜底）
    THROW_HE,        // 高爆雷出手
    THROW_FLASH,     // 闪光弹出手
    THROW_SMOKE,     // 烟雾弹出手
    FLASH,
    SMOKE,
    BOMB_PLANT,
    BOMB_DEFUSE,
    BOMB_BEEP,
    ROUND_START,
    ROUND_WIN,
    ROUND_LOSE,
    FOOTSTEP,
    ARMOR_HIT,

    // ---- 换弹音效（时长与各武器 reloadTime 对齐）----
    RELOAD_GLOCK,
    RELOAD_USP,
    RELOAD_DEAGLE,
    RELOAD_GALIL,
    RELOAD_AK47,
    RELOAD_M4A1,
    RELOAD_SHOTGUN,
    RELOAD_AWP,

    // ---- 分武器枪声 ----
    GLOCK_FIRE,
    USP_FIRE,
    M4A1_FIRE,
    AWP_FIRE,
    SHOTGUN_FIRE,
    SMG_FIRE,
    GALIL_FIRE
};

// 专用音频通道（避免连发射击音效相互抢占导致无声）
constexpr int CHANNEL_AK47_FIRE = 0;   // AK47 连发专用

class AudioManager {
private:
    bool audioInitialized = false;
    Mix_Music* backgroundMusic = nullptr;
    Mix_Chunk* buttonHoverSound = nullptr;
    Mix_Chunk* clickSound = nullptr;
    Mix_Chunk* daggerSound = nullptr;
    Mix_Chunk* shootSound = nullptr;
    Mix_Chunk* hitSound = nullptr;
    Mix_Chunk* killSound = nullptr;
    Mix_Chunk* ak47Sound = nullptr;
    Mix_Chunk* deagleSound = nullptr;

    // 战局事件音效
    Mix_Chunk* grenadeExplodeSound = nullptr;
    Mix_Chunk* equipSound         = nullptr;
    Mix_Chunk* grenadeThrowSound  = nullptr;
    Mix_Chunk* throwHeSound        = nullptr;
    Mix_Chunk* throwFlashSound     = nullptr;
    Mix_Chunk* throwSmokeSound     = nullptr;
    Mix_Chunk* flashSound = nullptr;
    Mix_Chunk* smokeSound = nullptr;
    Mix_Chunk* bombPlantSound = nullptr;
    Mix_Chunk* bombDefuseSound = nullptr;
    Mix_Chunk* bombBeepSound = nullptr;
    Mix_Chunk* roundStartSound = nullptr;
    Mix_Chunk* roundWinSound = nullptr;
    Mix_Chunk* roundLoseSound = nullptr;
    Mix_Chunk* footstepSound = nullptr;
    Mix_Chunk* armorHitSound = nullptr;

    // 换弹音效
    Mix_Chunk* reloadGlockSound   = nullptr;
    Mix_Chunk* reloadUspSound     = nullptr;
    Mix_Chunk* reloadDeagleSound  = nullptr;
    Mix_Chunk* reloadGalilSound   = nullptr;
    Mix_Chunk* reloadAk47Sound    = nullptr;
    Mix_Chunk* reloadM4a1Sound    = nullptr;
    Mix_Chunk* reloadShotgunSound = nullptr;
    Mix_Chunk* reloadAwpSound     = nullptr;

    // 分武器枪声
    Mix_Chunk* glockFireSound   = nullptr;
    Mix_Chunk* uspFireSound     = nullptr;
    Mix_Chunk* m4a1FireSound    = nullptr;
    Mix_Chunk* awpFireSound     = nullptr;
    Mix_Chunk* shotgunFireSound = nullptr;
    Mix_Chunk* galilFireSound   = nullptr;

    // 效果 → 音频块（两个播放入口共用，避免 switch 重复）
    Mix_Chunk* ChunkFor(SoundEffect effect) const;

    // 当前生效的通道音量（0..MIX_MAX_VOLUME），供 3D 音效与单通道重置使用
    int effectsLevel_ = MIX_MAX_VOLUME;

    int masterVolume = 100;
    int musicVolume = 40;
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
    // 3D 音效：声像由声源的世界位置相对听者决定（与听者朝向无关）
    void PlaySoundAt(SoundEffect effect, float sourceX, float sourceY,
                     float listenerX, float listenerY);
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
