#include "AudioManager.hpp"
#include "../Core/PathUtils.hpp"
#include <algorithm>
#include <iostream>

namespace {

// 武器 → 枪声素材。每把可射击武器一段「单发」录音：
// 每颗子弹都会触发一次，素材里若含连发会被听成多打了一枪。
struct WeaponFireAsset {
    WeaponId    id;
    const char* path;
};

const WeaponFireAsset kWeaponFireAssets[] = {
    // ---- 手枪 ----
    { WeaponId::Glock18,      "assets/audio/fire_glock18.wav" },
    { WeaponId::USP_S,        "assets/audio/fire_usp_s.wav" },
    { WeaponId::P2000,        "assets/audio/fire_p2000.wav" },
    { WeaponId::DualBerettas, "assets/audio/fire_dualberettas.wav" },
    { WeaponId::P250,         "assets/audio/fire_p250.wav" },
    { WeaponId::FiveSeven,    "assets/audio/fire_fiveseven.wav" },
    { WeaponId::Tec9,         "assets/audio/fire_tec9.wav" },
    { WeaponId::CZ75,         "assets/audio/fire_cz75.wav" },
    { WeaponId::Deagle,       "assets/audio/fire_deagle.wav" },
    { WeaponId::R8Revolver,   "assets/audio/fire_r8revolver.wav" },
    { WeaponId::Zeus,         "assets/audio/fire_zeus.wav" },

    // ---- 冲锋枪 ----
    { WeaponId::MAC10,        "assets/audio/fire_mac10.wav" },
    { WeaponId::MP9,          "assets/audio/fire_mp9.wav" },
    { WeaponId::MP7,          "assets/audio/fire_mp7.wav" },
    { WeaponId::MP5SD,        "assets/audio/fire_mp5sd.wav" },
    { WeaponId::UMP45,        "assets/audio/fire_ump45.wav" },
    { WeaponId::P90,          "assets/audio/fire_p90.wav" },

    // ---- 步枪 ----
    { WeaponId::GalilAR,      "assets/audio/fire_galilar.wav" },
    { WeaponId::FAMAS,        "assets/audio/fire_famas.wav" },
    { WeaponId::AK47,         "assets/audio/fire_ak47.wav" },
    { WeaponId::M4A4,         "assets/audio/fire_m4a4.wav" },
    { WeaponId::M4A1S,        "assets/audio/fire_m4a1s.wav" },
    { WeaponId::SG553,        "assets/audio/fire_sg553.wav" },
    { WeaponId::AUG,          "assets/audio/fire_aug.wav" },

    // ---- 狙击枪 ----
    { WeaponId::SSG08,        "assets/audio/fire_ssg08.wav" },
    { WeaponId::AWP,          "assets/audio/fire_awp.wav" },
    { WeaponId::SCAR20,       "assets/audio/fire_scar20.wav" },
    { WeaponId::G3SG1,        "assets/audio/fire_g3sg1.wav" },

    // ---- 霰弹枪 ----
    { WeaponId::Nova,         "assets/audio/fire_nova.wav" },
    { WeaponId::XM1014,       "assets/audio/fire_xm1014.wav" },
    { WeaponId::SawedOff,     "assets/audio/fire_sawedoff.wav" },
    { WeaponId::MAG7,         "assets/audio/fire_mag7.wav" },

    // ---- 机枪 ----
    { WeaponId::M249,         "assets/audio/fire_m249.wav" },
    { WeaponId::Negev,        "assets/audio/fire_negev.wav" },
};

} // namespace

bool AudioManager::Initialize() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL 音频子系统初始化失败，游戏将静音运行: " << SDL_GetError() << std::endl;
        return false;
    }

    // 默认只有 8 个混音通道，多人混战（10 人 + 脚步 + 命中音）容易抢不到而丢音
    Mix_AllocateChannels(32);

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

    // ---- 界面 / 音乐 ----
    buttonHoverSound = loadChunk("assets/audio/ui_hover.ogg", "按钮悬停");
    clickSound = loadChunk("assets/audio/ui_click.ogg", "按钮点击");

    // ---- 通用反馈 ----
    hitSound = loadChunk("assets/audio/sfx_hit.wav", "命中");
    killSound = loadChunk("assets/audio/sfx_kill.mp3", "击杀");
    armorHitSound = loadChunk("assets/audio/sfx_armor_hit.wav", "护甲受击");
    footstepSound = loadChunk("assets/audio/sfx_footstep.mp3", "脚步");
    equipSound = loadChunk("assets/audio/sfx_equip.mp3", "购买装备");

    // ---- 战局事件 ----
    grenadeExplodeSound = loadChunk("assets/audio/fx_explosion.wav", "手雷爆炸");
    grenadeThrowSound   = loadChunk("assets/audio/throw_generic.mp3", "投掷出手");
    // 出手音效：优先用专用素材，缺失时回落到通用出手声
    throwHeSound    = loadChunk("assets/audio/throw_he.mp3", "高爆雷出手");
    if (!throwHeSound)    throwHeSound    = grenadeThrowSound;
    throwFlashSound = loadChunk("assets/audio/throw_flash.mp3", "闪光弹出手");
    if (!throwFlashSound) throwFlashSound = grenadeThrowSound;
    throwSmokeSound = loadChunk("assets/audio/throw_smoke.mp3", "烟雾弹出手");
    if (!throwSmokeSound) throwSmokeSound = grenadeThrowSound;
    flashSound          = loadChunk("assets/audio/fx_flash.mp3", "闪光弹");
    smokeSound          = loadChunk("assets/audio/fx_smoke.mp3", "烟雾弹");
    bombPlantSound      = loadChunk("assets/audio/bomb_plant.wav", "下包");
    bombDefuseSound     = loadChunk("assets/audio/bomb_defuse.wav", "拆包");
    bombBeepSound       = loadChunk("assets/audio/bomb_beep.wav", "C4 滴答");
    roundStartSound     = loadChunk("assets/audio/round_start.wav", "回合开始");
    roundWinSound       = loadChunk("assets/audio/round_win.wav", "回合胜利");
    roundLoseSound      = loadChunk("assets/audio/round_lose.wav", "回合失败");
    daggerSound         = loadChunk("assets/audio/melee_knife.wav", "匕首攻击");

    // ---- 换弹音效 ----
    reloadGlockSound   = loadChunk("assets/audio/reload_glock.mp3",   "Glock 换弹");
    reloadUspSound     = loadChunk("assets/audio/reload_usp.mp3",     "USP 换弹");
    reloadDeagleSound  = loadChunk("assets/audio/reload_deagle.mp3",  "Deagle 换弹");
    reloadGalilSound   = loadChunk("assets/audio/reload_galil.mp3",   "Galil 换弹");
    reloadAk47Sound    = loadChunk("assets/audio/reload_ak47.mp3",    "AK47 换弹");
    reloadM4a1Sound    = loadChunk("assets/audio/reload_m4a1.mp3",    "M4A1 换弹");
    reloadShotgunSound = loadChunk("assets/audio/reload_shotgun.mp3", "霰弹枪换弹");
    reloadAwpSound     = loadChunk("assets/audio/reload_awp.mp3",     "AWP 换弹");

    // ---- 分武器枪声（单发）----
    for (const auto& asset : kWeaponFireAssets) {
        weaponFire_[static_cast<size_t>(asset.id)] = loadChunk(asset.path, asset.path);
    }

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
    if (buttonHoverSound) { Mix_FreeChunk(buttonHoverSound); buttonHoverSound = nullptr; }
    if (clickSound) { Mix_FreeChunk(clickSound); clickSound = nullptr; }
    if (daggerSound) { Mix_FreeChunk(daggerSound); daggerSound = nullptr; }
    if (hitSound) { Mix_FreeChunk(hitSound); hitSound = nullptr; }
    if (killSound) { Mix_FreeChunk(killSound); killSound = nullptr; }

    // 战局事件
    if (grenadeExplodeSound) { Mix_FreeChunk(grenadeExplodeSound); grenadeExplodeSound = nullptr; }
    if (equipSound) { Mix_FreeChunk(equipSound); equipSound = nullptr; }
    if (grenadeThrowSound)   { Mix_FreeChunk(grenadeThrowSound);   grenadeThrowSound = nullptr; }
    if (throwHeSound && throwHeSound != grenadeThrowSound) { Mix_FreeChunk(throwHeSound); throwHeSound = nullptr; }
    if (throwFlashSound && throwFlashSound != grenadeThrowSound) { Mix_FreeChunk(throwFlashSound); throwFlashSound = nullptr; }
    if (throwSmokeSound && throwSmokeSound != grenadeThrowSound) { Mix_FreeChunk(throwSmokeSound); throwSmokeSound = nullptr; }
    if (flashSound)          { Mix_FreeChunk(flashSound);          flashSound = nullptr; }
    if (smokeSound)          { Mix_FreeChunk(smokeSound);          smokeSound = nullptr; }
    if (bombPlantSound)      { Mix_FreeChunk(bombPlantSound);      bombPlantSound = nullptr; }
    if (bombDefuseSound)     { Mix_FreeChunk(bombDefuseSound);     bombDefuseSound = nullptr; }
    if (bombBeepSound)       { Mix_FreeChunk(bombBeepSound);       bombBeepSound = nullptr; }
    if (roundStartSound)     { Mix_FreeChunk(roundStartSound);     roundStartSound = nullptr; }
    if (roundWinSound)       { Mix_FreeChunk(roundWinSound);       roundWinSound = nullptr; }
    if (roundLoseSound)      { Mix_FreeChunk(roundLoseSound);      roundLoseSound = nullptr; }
    if (footstepSound)       { Mix_FreeChunk(footstepSound);       footstepSound = nullptr; }
    if (armorHitSound)       { Mix_FreeChunk(armorHitSound);       armorHitSound = nullptr; }

    // 换弹音效
    if (reloadGlockSound)   { Mix_FreeChunk(reloadGlockSound);   reloadGlockSound = nullptr; }
    if (reloadUspSound)     { Mix_FreeChunk(reloadUspSound);     reloadUspSound = nullptr; }
    if (reloadDeagleSound)  { Mix_FreeChunk(reloadDeagleSound);  reloadDeagleSound = nullptr; }
    if (reloadGalilSound)   { Mix_FreeChunk(reloadGalilSound);   reloadGalilSound = nullptr; }
    if (reloadAk47Sound)    { Mix_FreeChunk(reloadAk47Sound);    reloadAk47Sound = nullptr; }
    if (reloadM4a1Sound)    { Mix_FreeChunk(reloadM4a1Sound);    reloadM4a1Sound = nullptr; }
    if (reloadShotgunSound) { Mix_FreeChunk(reloadShotgunSound); reloadShotgunSound = nullptr; }
    if (reloadAwpSound)     { Mix_FreeChunk(reloadAwpSound);     reloadAwpSound = nullptr; }

    // 分武器枪声
    for (auto& chunk : weaponFire_) {
        if (chunk) { Mix_FreeChunk(chunk); chunk = nullptr; }
    }

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
        case SoundEffect::BUTTON_HOVER:     return buttonHoverSound;
        case SoundEffect::CLICK:            return clickSound;
        case SoundEffect::DAGGER_FIRE:      return daggerSound;
        case SoundEffect::HIT:              return hitSound;
        case SoundEffect::KILL:             return killSound;

        case SoundEffect::GRENADE_THROW:   return grenadeThrowSound;
        case SoundEffect::THROW_HE:        return throwHeSound;
        case SoundEffect::THROW_FLASH:     return throwFlashSound;
        case SoundEffect::THROW_SMOKE:     return throwSmokeSound;
        case SoundEffect::EQUIP:            return equipSound;
        case SoundEffect::GRENADE_EXPLODE:  return grenadeExplodeSound;
        case SoundEffect::FLASH:        return flashSound;
        case SoundEffect::SMOKE:            return smokeSound;
        case SoundEffect::BOMB_PLANT:       return bombPlantSound;
        case SoundEffect::BOMB_DEFUSE:      return bombDefuseSound;
        case SoundEffect::BOMB_BEEP:        return bombBeepSound;
        case SoundEffect::ROUND_START:      return roundStartSound;
        case SoundEffect::ROUND_WIN:        return roundWinSound;
        case SoundEffect::ROUND_LOSE:       return roundLoseSound;
        case SoundEffect::FOOTSTEP:         return footstepSound;
        case SoundEffect::ARMOR_HIT:        return armorHitSound;

        case SoundEffect::RELOAD_GLOCK:      return reloadGlockSound;
        case SoundEffect::RELOAD_USP:        return reloadUspSound;
        case SoundEffect::RELOAD_DEAGLE:     return reloadDeagleSound;
        case SoundEffect::RELOAD_GALIL:      return reloadGalilSound;
        case SoundEffect::RELOAD_AK47:       return reloadAk47Sound;
        case SoundEffect::RELOAD_M4A1:       return reloadM4a1Sound;
        case SoundEffect::RELOAD_SHOTGUN:    return reloadShotgunSound;
        case SoundEffect::RELOAD_AWP:        return reloadAwpSound;

        default:                             return nullptr;
    }
}

Mix_Chunk* AudioManager::WeaponFireChunk(WeaponId id) const {
    const int idx = static_cast<int>(id);
    if (idx < 0 || idx >= static_cast<int>(weaponFire_.size())) return nullptr;
    return weaponFire_[static_cast<size_t>(idx)];
}

void AudioManager::PlaySound(SoundEffect effect) {
    if (!audioInitialized) return;

    Mix_Chunk* targetChunk = ChunkFor(effect);
    if (!targetChunk) return;

    const int chan = Mix_PlayChannel(-1, targetChunk, 0);
    if (chan < 0) return;
    // 关键修复：通道是复用的，上一个音可能是远处 3D 音效（音量 0、声像偏一侧）。
    // 不重置的话，玩家自己的枪声会继承那个 0 音量，表现为「时有时无」。
    Mix_Volume(chan, effectsLevel_);
    Mix_SetPanning(chan, 255, 255);
}

void AudioManager::PlayWeaponFire(WeaponId id) {
    if (!audioInitialized) return;

    Mix_Chunk* targetChunk = WeaponFireChunk(id);
    if (!targetChunk) return;

    // 自动武器共用专用通道：新枪声覆盖上一发，避免通道耗尽后失声
    const bool autoWeapon = WeaponDB::Get(id).automatic;
    const int chan = Mix_PlayChannel(autoWeapon ? CHANNEL_AUTO_FIRE : -1,
                                     targetChunk, 0);
    if (chan < 0) return;
    Mix_Volume(chan, effectsLevel_);
    Mix_SetPanning(chan, 255, 255);
}

void AudioManager::PlayChunkAt(Mix_Chunk* chunk, float sourceX, float sourceY,
                               float listenerX, float listenerY) {
    if (!audioInitialized || !chunk) return;

    const int chan = Mix_PlayChannel(-1, chunk, 0);
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

    // 距离衰减：FULL_RANGE 内全音量，之后线性衰减，但保留一个可听见的底噪，
    // 避免「一过 MAX_RANGE 就彻底静音」——那会让远处的枪战听着像时有时无
    constexpr float FULL_RANGE = 0.0f;
    constexpr float MAX_RANGE  = 100.0f;
    constexpr float MIN_FACTOR = 0.0f;   // 最远处仍保留 12% 音量
    float distFactor = 1.0f;
    if (dist > FULL_RANGE) {
        const float t = std::min(1.0f, (dist - FULL_RANGE) / (MAX_RANGE - FULL_RANGE));
        distFactor = 1.0f - (1.0f - MIN_FACTOR) * t;
    }
    const int vol = static_cast<int>(distFactor * effectsLevel_);
    Mix_Volume(chan, vol);
}

void AudioManager::PlaySoundAt(SoundEffect effect, float sourceX, float sourceY,
                               float listenerX, float listenerY) {
    PlayChunkAt(ChunkFor(effect), sourceX, sourceY, listenerX, listenerY);
}

void AudioManager::PlayWeaponFireAt(WeaponId id, float sourceX, float sourceY,
                                    float listenerX, float listenerY) {
    PlayChunkAt(WeaponFireChunk(id), sourceX, sourceY, listenerX, listenerY);
}

void AudioManager::UpdateVolumes() {
    if (!audioInitialized) return;

    const int effectiveMaster = masterMuted ? 0 : masterVolume;
    const int musicLevel = effectiveMaster * musicVolume * MIX_MAX_VOLUME / 10000;
    const int effectsLevel = effectiveMaster * effectsVolume * MIX_MAX_VOLUME / 10000;

    effectsLevel_ = effectsLevel;
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
