#pragma once
#include "../CS2D/Weapons/WeaponDef.hpp"

// ============================================================================
// 音频抽象契约（纯 C++17，无 SDL / SDL_mixer 依赖）
// ============================================================================

enum class SoundEffect {
    PICKUP,
    BUTTON_HOVER,
    CLICK,
    DAGGER_FIRE,
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
    FIRE_IGNITE,     // 燃烧弹点燃
    DECOY_EXPLODE,   // 诱饵弹爆炸
    BOMB_PLANT,
    BOMB_DEFUSE,
    BOMB_BEEP,
    ROUND_WIN,
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
    DEATH,
    PULL_PIN,
    BOMB_EXPLODE,
    BOMB_PLANTING,
    BOMB_DEFUSING_KIT,
    BOMB_DEFUSING,
    RELOAD_P2000,
    RELOAD_DUALBERETTAS,
    RELOAD_P250,
    RELOAD_FN57,
    RELOAD_TEC9,
    RELOAD_CZ75,
    RELOAD_R8,
    RELOAD_MAC10,
    RELOAD_MP9,
    RELOAD_MP7,
    RELOAD_MP5SD,
    RELOAD_UMP45,
    RELOAD_P90,
    RELOAD_FAMAS,
    RELOAD_M4A4,
    RELOAD_SG553,
    RELOAD_AUG,
    RELOAD_SSG08,
    RELOAD_SCAR20,
    RELOAD_G3SG1,
    RELOAD_XM1014,
    RELOAD_SAWEDOFF,
    RELOAD_MAG7,
    RELOAD_M249,
    RELOAD_NEGEV,
    DRYFIRE,
};

// 自动武器专用通道：新枪声覆盖上一发，避免通道耗尽后失声
constexpr int CHANNEL_AUTO_FIRE = 0;
// 保留通道数：0 自动开火，1 玩家下包，2 玩家拆包，3–11 九名 BOT 拆包
constexpr int kReservedChannels = 12;

class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    virtual void PlaySound(SoundEffect effect, float volume = 1.0f) = 0;
    virtual int  PlaySoundTracked(SoundEffect effect, int channel = -1) = 0;
    virtual void StopChannel(int& channel) = 0;
    virtual void SetChannelPaused(int channel, bool paused) = 0;
    virtual int  PlaySoundTrackedAt(SoundEffect effect, float x, float y,
                                    float listenerX, float listenerY, int channel = -1) = 0;
    virtual void UpdateTrackedSoundAt(int channel, SoundEffect effect, float x, float y,
                                      float listenerX, float listenerY) = 0;
    virtual void PlaySoundAt(SoundEffect effect, float sourceX, float sourceY,
                             float listenerX, float listenerY) = 0;
    virtual void PlayWeaponFire(WeaponId id) = 0;
    virtual void PlayWeaponFireAt(WeaponId id, float sourceX, float sourceY,
                                  float listenerX, float listenerY) = 0;
    virtual void PlayWeaponSwitch(WeaponId id) = 0;
    virtual void PlayWeaponSwitchAt(WeaponId id, float sourceX, float sourceY,
                                    float listenerX, float listenerY) = 0;

    static IAudioSink* Active() { return ActiveSlot(); }
    static void SetActive(IAudioSink* sink) { ActiveSlot() = sink; }

private:
    static IAudioSink*& ActiveSlot() {
        static IAudioSink* instance = nullptr;
        return instance;
    }
};
