#pragma once
#include "Tuning.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

struct AimLineStyle {
    SDL_Color solid{Tuning::AimLineSolidR, Tuning::AimLineSolidG, Tuning::AimLineSolidB, Tuning::AimLineSolidAlpha};
    SDL_Color hidden{Tuning::AimLineHiddenR, Tuning::AimLineHiddenG, Tuning::AimLineHiddenB, Tuning::AimLineHiddenAlpha};
    float solidThickness = 1.0f;
    float hiddenThickness = 1.0f;
    float dashLength = Tuning::AimLineDashPixels;
    float gapLength = Tuning::AimLineGapPixels;

    void Normalize() {
        solidThickness = std::isfinite(solidThickness) ? std::clamp(solidThickness, 0.5f, 6.0f) : 1.0f;
        hiddenThickness = std::isfinite(hiddenThickness) ? std::clamp(hiddenThickness, 0.5f, 6.0f) : 1.0f;
        dashLength = std::isfinite(dashLength) ? std::clamp(dashLength, 1.0f, 32.0f) : Tuning::AimLineDashPixels;
        gapLength = std::isfinite(gapLength) ? std::clamp(gapLength, 1.0f, 32.0f) : Tuning::AimLineGapPixels;
    }
};
