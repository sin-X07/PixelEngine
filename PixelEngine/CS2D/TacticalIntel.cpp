#include "TacticalIntel.hpp"
#include "CS2DWorld.hpp"
#include <algorithm>
#include <cmath>

void TacticalIntel::Build(const CS2DWorld* world, float now) {
    sites_.clear();
    observations_ = 0;
    if (!world) return;

    for (const auto& s : world->GetBombSites()) {
        SiteHeat h;
        h.x = s.x;
        h.y = s.y;
        h.w = s.width;
        h.h = s.height;
        h.area = world->FindAreaAt(s.x + s.width * 0.5f, s.y + s.height * 0.5f);
        h.lastSeen = now - 999.0f;
        sites_.push_back(h);
    }
}

void TacticalIntel::Reset() {
    for (auto& s : sites_) {
        s.heat = 0.0f;
        s.lastCount = 0;
        s.lastSeen = -999.0f;
    }
    observations_ = 0;
}

void TacticalIntel::OnEnemySeen(float x, float y, float now) {
    const int idx = SiteIndexAt(x, y);
    if (idx < 0) return;
    SiteHeat& s = sites_[static_cast<size_t>(idx)];
    s.heat = std::min(kMaxHeat, s.heat + kSeenGain);
    s.lastSeen = now;
    s.lastCount = std::max(1, s.lastCount + 1);
    ++observations_;
}

void TacticalIntel::Update(float dt) {
    const float k = std::exp(-dt / kDecayTau);
    for (auto& s : sites_) {
        s.heat *= k;
        if (s.heat < 0.01f) {
            s.heat = 0.0f;
            s.lastCount = 0;
        }
    }
}

float TacticalIntel::Heat(int i) const {
    if (i < 0 || i >= static_cast<int>(sites_.size())) return 0.0f;
    return sites_[static_cast<size_t>(i)].heat;
}

float TacticalIntel::HeatOfArea(const std::string& area) const {
    if (area.empty()) return 0.0f;
    for (const auto& s : sites_) {
        if (s.area == area) return s.heat;
    }
    return 0.0f;
}

std::string TacticalIntel::AreaName(int i) const {
    if (i < 0 || i >= static_cast<int>(sites_.size())) return std::string();
    return sites_[static_cast<size_t>(i)].area;
}

int TacticalIntel::SiteIndexAt(float x, float y) const {
    // 先判断落在哪个包点矩形内
    for (size_t i = 0; i < sites_.size(); ++i) {
        const auto& s = sites_[i];
        if (x >= s.x && x <= s.x + s.w && y >= s.y && y <= s.y + s.h) {
            return static_cast<int>(i);
        }
    }
    // 否则归到最近的包点（阈值 220px，避免把中路接触算成包点）
    int best = -1;
    float bestD2 = 220.0f * 220.0f;
    for (size_t i = 0; i < sites_.size(); ++i) {
        const auto& s = sites_[i];
        const float dx = (s.x + s.w * 0.5f) - x;
        const float dy = (s.y + s.h * 0.5f) - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(i); }
    }
    return best;
}

float TacticalIntel::HeatOfSide(const CS2DWorld* world, const std::string& side) const {
    if (!world || side.empty()) return 0.0f;

    // 找该侧包点区域（A_Site / B_Site）的中心
    const std::string want = side + "_Site";
    float ax = 0.0f, ay = 0.0f;
    bool found = false;
    for (const auto& area : world->GetAreas()) {
        std::string lower = area.name;
        for (auto& ch : lower) ch = static_cast<char>(std::tolower(ch));
        std::string w = want;
        for (auto& ch : w) ch = static_cast<char>(std::tolower(ch));
        if (lower.find(w) == std::string::npos) continue;
        if (area.rects.empty()) continue;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (const auto& r : area.rects) {
            minX = std::min(minX, r.x);               minY = std::min(minY, r.y);
            maxX = std::max(maxX, r.x + r.width);     maxY = std::max(maxY, r.y + r.height);
        }
        ax = (minX + maxX) * 0.5f;
        ay = (minY + maxY) * 0.5f;
        found = true;
        break;
    }
    if (!found) return 0.0f;

    // 最近的包点
    int best = -1;
    float bestD2 = 1e18f;
    for (size_t i = 0; i < sites_.size(); ++i) {
        const float dx = (sites_[i].x + sites_[i].w * 0.5f) - ax;
        const float dy = (sites_[i].y + sites_[i].h * 0.5f) - ay;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(i); }
    }
    return (best >= 0) ? sites_[static_cast<size_t>(best)].heat : 0.0f;
}
