#pragma once

#include "ActorTypes.hpp"
#include <cstdint>

enum class InputButton : std::uint16_t {
    None        = 0,
    MoveUp      = 1u << 0,
    MoveLeft    = 1u << 1,
    MoveDown    = 1u << 2,
    MoveRight   = 1u << 3,
    Stealth     = 1u << 4,
    Fire        = 1u << 5,
    Reload      = 1u << 6,
    Interact    = 1u << 7,
    ScopeToggle = 1u << 8,
    EdgeZoom    = 1u << 9,
    RelativeAim = 1u << 10,
    QuickSwitch = 1u << 11,
    Drop        = 1u << 12,
    ToggleC4    = 1u << 13,
};

struct InputCmd {
    std::uint32_t tick         = 0;
    std::uint32_t seq          = 0;
    ActorId       actorId      = kInvalidActorId;
    std::uint16_t buttons      = 0;
    float         aimX         = 0.0f;
    float         aimY         = 0.0f;
    bool          hasAimWorld  = true;
    int           switchSlot   = -1;  // -1 = 无切枪请求，0..4 = 切换到指定槽
    int           grenadeIndex = -1;  // -1 = 默认循环/当前，>=0 = 指定投掷物下标
    std::uint8_t  buyWeapon    = 0;   // WeaponId 数值，0 = 无
    std::uint8_t  buyArmorTier = 0;   // 0 = 无，1 = 护甲，2 = 护甲+头盔
    bool          buyKit       = false;

    bool Has(InputButton btn) const {
        return (buttons & static_cast<std::uint16_t>(btn)) != 0;
    }

    void Set(InputButton btn, bool pressed) {
        const std::uint16_t mask = static_cast<std::uint16_t>(btn);
        if (pressed) buttons |= mask;
        else buttons &= static_cast<std::uint16_t>(~mask);
    }

    ActorInput ToActorInput() const {
        ActorInput out;
        out.moveUp      = Has(InputButton::MoveUp);
        out.moveLeft    = Has(InputButton::MoveLeft);
        out.moveDown    = Has(InputButton::MoveDown);
        out.moveRight   = Has(InputButton::MoveRight);
        out.stealth     = Has(InputButton::Stealth);
        out.fire        = Has(InputButton::Fire);
        out.reload      = Has(InputButton::Reload);
        out.interact    = Has(InputButton::Interact);
        out.rightClick  = Has(InputButton::ScopeToggle);
        out.aimWorldX     = aimX;
        out.aimWorldY     = aimY;
        out.hasAimWorld   = hasAimWorld;
        out.aimIsRelative = Has(InputButton::RelativeAim);
        return out;
    }

    static InputCmd FromActorInput(ActorId id, const ActorInput& in,
                                   std::uint32_t tickValue = 0, std::uint32_t seqValue = 0) {
        InputCmd cmd;
        cmd.tick        = tickValue;
        cmd.seq         = seqValue;
        cmd.actorId     = id;
        cmd.aimX        = in.aimWorldX;
        cmd.aimY        = in.aimWorldY;
        cmd.hasAimWorld = in.hasAimWorld;
        cmd.Set(InputButton::MoveUp,      in.moveUp);
        cmd.Set(InputButton::MoveLeft,    in.moveLeft);
        cmd.Set(InputButton::MoveDown,    in.moveDown);
        cmd.Set(InputButton::MoveRight,   in.moveRight);
        cmd.Set(InputButton::Stealth,     in.stealth);
        cmd.Set(InputButton::Fire,        in.fire);
        cmd.Set(InputButton::Reload,      in.reload);
        cmd.Set(InputButton::Interact,    in.interact);
        cmd.Set(InputButton::ScopeToggle, in.rightClick);
        cmd.Set(InputButton::RelativeAim, in.aimIsRelative);
        return cmd;
    }
};
