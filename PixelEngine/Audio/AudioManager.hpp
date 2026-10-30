#pragma once
#include <SDL2/SDL.h>
#include <SDL_mixer.h>
#include <array>
#include <string>
#include "../CS2D/Weapons/WeaponDef.hpp"

// ============================================================================
// AudioManager —— 音效播放
//
// 命名约定（assets/audio/）：
//   ui_*     按钮等界面音        music_*  背景音乐
//   fire_*   枪声（每把枪一发）   reload_* 换弹
//   melee_*  近战                throw_*  投掷出手
//   fx_*     爆炸/闪光/烟雾       bomb_*   下包拆包
//   round_*  回合事件            sfx_*    命中/击杀/脚步等通用反馈
//
// 枪声一律是「一次开火对应一段单发素材」，不使用连发录音：每发射出一颗
// 子弹都会调用一次 PlayWeaponFire，素材里的第二声会变成凭空多出的一枪。
// ============================================================================

enum class SoundEffect {
    PICKUP,
    BUTTON_HOVER,
    CLICK,
    DAGGER_FIRE,
    OBSTACLE_BREAK,
    HIT,          // 命中
    KILL,         // 击杀
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
    RELOAD_AWP
};

// 自动武器专用通道：新枪声覆盖上一发，避免通道耗尽后失声
constexpr int CHANNEL_AUTO_FIRE = 0;

class AudioManager {
private:
    bool audioInitialized = false;
    Mix_Music* backgroundMusic = nullptr;
    Mix_Chunk* buttonHoverSound = nullptr;
    Mix_Chunk* clickSound = nullptr;
    Mix_Chunk* daggerSound = nullptr;
    Mix_Chunk* hitSound = nullptr;
    Mix_Chunk* killSound = nullptr;

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

    // 分武器枪声：按 WeaponId 索引，每把枪对应一段单发素材
    std::array<Mix_Chunk*, static_cast<size_t>(WeaponId::Count)> weaponFire_{};

    // 切枪音效：按 WeaponClass 索引（刀/手枪/冲锋枪/步枪/狙击/霰弹/机枪/投掷物/C4）
    std::array<Mix_Chunk*, 10> weaponSwitch_{};

    // 效果 → 音频块（两个播放入口共用，避免 switch 重复）
    Mix_Chunk* ChunkFor(SoundEffect effect) const;
    Mix_Chunk* WeaponFireChunk(WeaponId id) const;
    Mix_Chunk* WeaponSwitchChunk(WeaponId id) const;
    void PlayChunkAt(Mix_Chunk* chunk, float sourceX, float sourceY,
                     float listenerX, float listenerY);

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
    bool IsMusicPlaying() const {
        return audioInitialized && Mix_PlayingMusic() != 0;
    }

    void PlaySound(SoundEffect effect);
    // 3D 音效：声像由声源的世界位置相对听者决定（与听者朝向无关）
    void PlaySoundAt(SoundEffect effect, float sourceX, float sourceY,
                     float listenerX, float listenerY);

    // 枪声：每开一枪调用一次，素材必须是单发
    void PlayWeaponFire(WeaponId id);
    void PlayWeaponFireAt(WeaponId id, float sourceX, float sourceY,
                          float listenerX, float listenerY);

    // 切枪：切到某把武器时播放该武器类别的「掏出/上膛」声
    void PlayWeaponSwitch(WeaponId id);
    void PlayWeaponSwitchAt(WeaponId id, float sourceX, float sourceY,
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
