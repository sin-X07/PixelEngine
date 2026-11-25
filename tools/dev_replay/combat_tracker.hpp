#pragma once
// ============================================================================
// CombatTracker (P2-5): firefight-level combat log for dev_replay.
//
// One row per combat event (not per tick): shot, hit taken/dealt, reload
// start/end, empty magazine, empty magazine reloaded. Reaction time is the
// ticks from first seeing a hostile (LOS within 700px) to the first shot at
// a target that was not fired at in the previous 120 ticks, so sustained
// fire does not pollute the distribution.
//
// Hit attribution is a same-tick heuristic: the opposing actor whose shot
// counter rose this tick, with LOS to the victim and within 800px, nearest
// wins; unattributed hits are logged as such. Deterministic: no RNG, no
// game-state writes -- read-only observation of CS2DGame public state.
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace dev_replay {

class CombatTracker {
public:
    static void WriteHeader(std::FILE* f) {
        if (!f) return;
        std::fprintf(f, "tick,round,actor,team,event,ammo,reserve,reaction_ticks,dist_nearest_enemy,detail\n");
    }

    void Begin(const CS2DGame& game, uint64_t tick) {
        (void)tick;
        Snapshot(game);
        shots_ = hitsDealt_ = hitsTaken_ = reloads_ = empties_ = emptyReloaded_ = 0;
    }

    void Sample(const CS2DGame& game, uint64_t tick, std::FILE* f) {
        if (!f) return;
        const int round = game.GetRoundNumber();
        // First pass: per-actor snapshots (shots, health) so hit attribution
        // does not depend on the order actors are processed in.
        unsigned shotsNow[kActors];
        unsigned prevShots[kActors];
        int healthNow[kActors];
        for (int i = 0; i < kActors; ++i) {
            const CS2DGame::Player& p = Actor(game, i);
            shotsNow[i] = ShotsOf(p);
            prevShots[i] = st_[i].shots;
            healthNow[i] = p.health;
        }
        // First-seen tracking runs every 3 ticks (LOS is the expensive part).
        if (tick % 3 == 0) UpdateFirstSeen(game, tick);
        for (int i = 0; i < kActors; ++i) {
            const CS2DGame::Player& p = Actor(game, i);
            ActorState& s = st_[i];
            const int distFoe = NearestFoeDist(game, i);
            // Expire a stale first-seen (120 ticks without LOS).
            if (s.firstSeenTick >= 0 && static_cast<int>(tick) - s.lastSeenTick > 120) s.firstSeenTick = -1;
            if (shotsNow[i] > s.shots) {
                // Reaction is measured only against a target not fired at in the
                // previous 120 ticks; sustained fire gets reaction -1 (excluded).
                int reaction = -1;
                if (s.firstSeenTick >= 0 &&
                    static_cast<int>(tick) - s.lastShotTick > 120) {
                    reaction = static_cast<int>(tick) - s.firstSeenTick;
                }
                Row(f, tick, round, i, p, "shot", reaction, distFoe, "");
                s.firstSeenTick = -1;  // reaction is measured to the first shot only
                s.lastShotTick = static_cast<int>(tick);
                s.shots = shotsNow[i];
                ++shots_;
            } else if (shotsNow[i] < s.shots) {
                // Shot counters reset on weapon switch; resync without logging.
                s.shots = shotsNow[i];
            }
            if (healthNow[i] < s.health && p.alive) {
                const int atk = Attribute(game, i, shotsNow, prevShots);
                ++hitsTaken_;
                if (atk >= 0) {
                    char atkLabel[8];
                    char vicLabel[8];
                    Label(atk, atkLabel, sizeof(atkLabel));
                    Label(i, vicLabel, sizeof(vicLabel));
                    Row(f, tick, round, i, p, "hit_taken", -1, distFoe, atkLabel);
                    const CS2DGame::Player& ap = Actor(game, atk);
                    Row(f, tick, round, atk, ap, "hit_dealt", -1, NearestFoeDist(game, atk), vicLabel);
                    ++hitsDealt_;
                } else {
                    Row(f, tick, round, i, p, "hit_taken", -1, distFoe, "unattributed");
                }
            }
            if (p.reloading && !s.reloading) {
                Row(f, tick, round, i, p, "reload_start", -1, distFoe, "");
                ++reloads_;
                if (s.wasEmpty) s.reloadedDuringEmpty = true;
            }
            if (!p.reloading && s.reloading) {
                char detail[32];
                std::snprintf(detail, sizeof(detail), "ammo_after=%d", p.ammo);
                Row(f, tick, round, i, p, "reload_end", -1, distFoe, detail);
            }
            if (p.ammo == 0 && p.reserve > 0 && !s.wasEmpty && p.alive) {
                s.wasEmpty = true;
                s.emptyStartTick = static_cast<int>(tick);
                s.reloadedDuringEmpty = false;
                Row(f, tick, round, i, p, "empty", -1, distFoe, "");
                ++empties_;
            }
            if (s.wasEmpty && p.ammo > 0) {
                s.wasEmpty = false;
                if (s.reloadedDuringEmpty) {
                    Row(f, tick, round, i, p, "empty_reloaded", -1, distFoe, "");
                    ++emptyReloaded_;
                }
            }
            s.health = healthNow[i];
            s.ammo = p.ammo;
            s.reserve = p.reserve;
            s.alive = p.alive;
            s.reloading = p.reloading;
        }
    }

    void Finish(const CS2DGame& game, uint64_t tick, std::FILE* f) {
        (void)game; (void)tick;
        std::printf("[combat] shots=%d hits_dealt=%d hits_taken=%d reloads=%d empty=%d empty_reloaded=%d%s\n",
                    shots_, hitsDealt_, hitsTaken_, reloads_, empties_, emptyReloaded_,
                    f ? "" : " (no --combat file: sampling was off)");
    }

private:
    static constexpr int kActors = 10;  // 0 = scripted player, 1..9 = bots in GetBOTs() order

    struct ActorState {
        int health = 100;
        int ammo = 0, reserve = 0;
        bool alive = false;
        bool reloading = false;
        unsigned shots = 0;
        int firstSeenTick = -1;  // -1 = no hostile currently seen
        int lastSeenTick = -1;
        int lastShotTick = -1000;
        bool wasEmpty = false;   // ammo==0 with reserve>0, episode open
        int emptyStartTick = -1;
        bool reloadedDuringEmpty = false;
    };

    ActorState st_[kActors];
    int shots_ = 0, hitsDealt_ = 0, hitsTaken_ = 0;
    int reloads_ = 0, empties_ = 0, emptyReloaded_ = 0;

    static const CS2DGame::Player& Actor(const CS2DGame& game, int i) {
        if (i == 0) return game.GetPlayer();
        const auto& bots = game.GetBOTs();
        const size_t idx = static_cast<size_t>(i - 1);
        return idx < bots.size() ? bots[idx] : game.GetPlayer();
    }

    static unsigned ShotsOf(const CS2DGame::Player& p) {
        unsigned n = 0;
        for (const auto& fs : p.fireStates) n += fs.shot;
        return n;
    }

    static void Label(int i, char* buf, size_t n) {
        if (i == 0) std::snprintf(buf, n, "player");
        else std::snprintf(buf, n, "bot%d", i - 1);
    }

    static const char* TeamOf(const CS2DGame::Player& p) {
        return p.team == Team::T ? "T" : (p.team == Team::CT ? "CT" : "None");
    }

    static void Row(std::FILE* f, uint64_t tick, int round, int actor,
                    const CS2DGame::Player& p, const char* event,
                    int reaction, int distFoe, const char* detail) {
        char label[8];
        Label(actor, label, sizeof(label));
        std::fprintf(f, "%llu,%d,%s,%s,%s,%d,%d,%d,%d,%s\n",
                     static_cast<unsigned long long>(tick), round, label, TeamOf(p), event,
                     p.ammo, p.reserve, reaction, distFoe, detail ? detail : "");
    }

    void Snapshot(const CS2DGame& game) {
        for (int i = 0; i < kActors; ++i) {
            const CS2DGame::Player& p = Actor(game, i);
            ActorState& s = st_[i];
            s.health = p.health;
            s.ammo = p.ammo;
            s.reserve = p.reserve;
            s.alive = p.alive;
            s.reloading = p.reloading;
            s.shots = ShotsOf(p);
            s.firstSeenTick = -1;
            s.lastSeenTick = -1;
            s.lastShotTick = -1000;
            s.wasEmpty = false;
            s.emptyStartTick = -1;
            s.reloadedDuringEmpty = false;
        }
    }

    void UpdateFirstSeen(const CS2DGame& game, uint64_t tick) {
        for (int i = 0; i < kActors; ++i) {
            const CS2DGame::Player& p = Actor(game, i);
            if (!p.alive) continue;
            ActorState& s = st_[i];
            bool seen = false;
            for (int j = 0; j < kActors; ++j) {
                if (i == j) continue;
                const CS2DGame::Player& e = Actor(game, j);
                if (!e.alive || !game.IsHostile(p, e)) continue;
                const float dx = e.x - p.x;
                const float dy = e.y - p.y;
                if (dx * dx + dy * dy > 700.0f * 700.0f) continue;
                if (!game.Visibility().HasLineOfSight(p.x, p.y, e.x, e.y)) continue;
                seen = true;
                break;
            }
            if (seen) {
                if (s.firstSeenTick < 0) s.firstSeenTick = static_cast<int>(tick);
                s.lastSeenTick = static_cast<int>(tick);
            }
        }
    }

    int NearestFoeDist(const CS2DGame& game, int i) {
        const CS2DGame::Player& p = Actor(game, i);
        float bestD2 = 1e18f;
        for (int j = 0; j < kActors; ++j) {
            if (i == j) continue;
            const CS2DGame::Player& e = Actor(game, j);
            if (!e.alive || !game.IsHostile(p, e)) continue;
            const float dx = e.x - p.x;
            const float dy = e.y - p.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) bestD2 = d2;
        }
        return bestD2 > 1e17f ? -1 : static_cast<int>(std::sqrt(bestD2));
    }

    // Same-tick hit attribution: the opposing actor whose shot counter rose
    // this tick, with LOS to the victim and within 800px, nearest wins.
    int Attribute(const CS2DGame& game, int victim,
                  const unsigned* shotsNow, const unsigned* prevShots) {
        const CS2DGame::Player& v = Actor(game, victim);
        int best = -1;
        float bestD2 = 800.0f * 800.0f;
        for (int j = 0; j < kActors; ++j) {
            if (j == victim) continue;
            const CS2DGame::Player& a = Actor(game, j);
            if (!a.alive || !game.IsHostile(a, v)) continue;
            if (shotsNow[j] <= prevShots[j]) continue;  // did not fire this tick
            const float dx = v.x - a.x;
            const float dy = v.y - a.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= bestD2) continue;
            if (!game.Visibility().HasLineOfSight(a.x, a.y, v.x, v.y)) continue;
            bestD2 = d2;
            best = j;
        }
        return best;
    }
};

}  // namespace dev_replay
