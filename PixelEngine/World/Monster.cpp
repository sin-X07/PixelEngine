#include "Monster.hpp"
#include <cmath>
#include <queue>
#include <algorithm>
#include <limits>
#include <utility>

namespace {
    struct Node {
        int r;
        int c;
        float g;
        float f;
    };
    struct NodeCmp {
        bool operator()(const Node& a, const Node& b) const { return a.f > b.f; }
    };

    float Manhattan(int r, int c, int gr, int gc) {
        return static_cast<float>(std::abs(r - gr) + std::abs(c - gc));
    }

    bool IsTileBlocked(const WorldMap& wm, int r, int c) {
        const int rows = wm.GetMapHeightTiles();
        const int cols = wm.GetMapWidthTiles();
        if (r < 0 || r >= rows || c < 0 || c >= cols) return true;

        const char ch = wm.GetLevelGrid()[r][c];
        if (ch == '#' || ch == 'H' || ch == 'o') return true;

        const int ts = wm.GetTileSize();
        for (const auto& t : wm.GetTraps()) {
            if (static_cast<int>(t.y / ts) == r && static_cast<int>(t.x / ts) == c) return true;
        }
        const float cs = wm.GetCoinSize();
        for (const auto& coin : wm.GetCoins()) {
            int cr = static_cast<int>((coin.y + cs * 0.5f) / ts);
            int cc = static_cast<int>((coin.x + cs * 0.5f) / ts);
            if (cr == r && cc == c) return true;
        }
        return false;
    }
}

Monster Monster::Make(MonsterType type, float centerX, float centerY) {
    Monster m;
    m.type = type;
    switch (type) {
        case MonsterType::GRUNT:
            m.size = 12.0f;
            m.speed = 120.0f;
            m.attackRange = 12.0f;
            m.attackCooldownMax = 1.0f;
            m.health = 2.0f;
            m.maxHealth = 2.0f;
            m.damage = 1;
            m.ranged = false;
            m.bulletSpeed = 0.0f;
            break;
        case MonsterType::BRUTE:
            m.size = 16.0f;
            m.speed = 70.0f;
            m.attackRange = 14.0f;
            m.attackCooldownMax = 1.4f;
            m.health = 6.0f;
            m.maxHealth = 6.0f;
            m.damage = 2;
            m.ranged = false;
            m.bulletSpeed = 0.0f;
            break;
        case MonsterType::SHOOTER:
            m.size = 12.0f;
            m.speed = 95.0f;
            m.attackRange = 200.0f;
            m.attackCooldownMax = 1.5f;
            m.health = 3.0f;
            m.maxHealth = 3.0f;
            m.damage = 1;
            m.ranged = true;
            m.bulletSpeed = 240.0f;
            break;
    }
    m.x = centerX - m.size * 0.5f;
    m.y = centerY - m.size * 0.5f;
    m.attackCooldown = 0.0f;
    m.repathTimer = 0.0f;
    m.alive = true;
    return m;
}

bool Monster::IsNextWaypointBlocked(const WorldMap& worldMap) const {
    if (pathIndex >= path.size()) return true;
    const int ts = worldMap.GetTileSize();
    const SDL_FPoint wp = path[pathIndex];
    const int r = static_cast<int>(wp.y / ts);
    const int c = static_cast<int>(wp.x / ts);
    return IsTileBlocked(worldMap, r, c);
}

bool Monster::RecomputePath(float targetCenterX, float targetCenterY, const WorldMap& worldMap) {
    path.clear();
    pathIndex = 0;

    const int rows = worldMap.GetMapHeightTiles();
    const int cols = worldMap.GetMapWidthTiles();
    const int ts = worldMap.GetTileSize();
    if (rows <= 0 || cols <= 0) return false;

    int startR = std::clamp(static_cast<int>((y + size * 0.5f) / ts), 0, rows - 1);
    int startC = std::clamp(static_cast<int>((x + size * 0.5f) / ts), 0, cols - 1);
    int goalR = std::clamp(static_cast<int>(targetCenterY / ts), 0, rows - 1);
    int goalC = std::clamp(static_cast<int>(targetCenterX / ts), 0, cols - 1);

    // 目标不可达时回退到最近的可通行瓦片
    if (IsTileBlocked(worldMap, goalR, goalC)) {
        int bestR = goalR, bestC = goalC;
        int bestDist = std::numeric_limits<int>::max();
        for (int r = std::max(0, goalR - 3); r <= std::min(rows - 1, goalR + 3); ++r) {
            for (int c = std::max(0, goalC - 3); c <= std::min(cols - 1, goalC + 3); ++c) {
                if (IsTileBlocked(worldMap, r, c)) continue;
                int d = std::abs(r - goalR) + std::abs(c - goalC);
                if (d < bestDist) { bestDist = d; bestR = r; bestC = c; }
            }
        }
        if (bestDist == std::numeric_limits<int>::max()) return false;
        goalR = bestR;
        goalC = bestC;
    }

    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};

    std::vector<std::vector<float>> g(rows, std::vector<float>(cols, std::numeric_limits<float>::infinity()));
    std::vector<std::vector<bool>> visited(rows, std::vector<bool>(cols, false));
    std::vector<std::vector<std::pair<int, int>>> parent(rows, std::vector<std::pair<int, int>>(cols, {-1, -1}));

    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;
    g[startR][startC] = 0.0f;
    open.push({startR, startC, 0.0f, Manhattan(startR, startC, goalR, goalC)});

    bool found = false;
    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        if (visited[cur.r][cur.c]) continue;
        visited[cur.r][cur.c] = true;
        if (cur.r == goalR && cur.c == goalC) { found = true; break; }

        for (int i = 0; i < 4; ++i) {
            const int nr = cur.r + dr[i];
            const int nc = cur.c + dc[i];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            if (visited[nr][nc] || IsTileBlocked(worldMap, nr, nc)) continue;
            const float ng = cur.g + 1.0f;
            if (ng < g[nr][nc]) {
                g[nr][nc] = ng;
                parent[nr][nc] = {cur.r, cur.c};
                open.push({nr, nc, ng, ng + Manhattan(nr, nc, goalR, goalC)});
            }
        }
    }

    if (!found) return false;

    std::vector<std::pair<int, int>> tiles;
    int r = goalR, c = goalC;
    while (!(r == startR && c == startC)) {
        tiles.push_back({r, c});
        const std::pair<int, int> p = parent[r][c];
        if (p.first < 0) break;
        r = p.first;
        c = p.second;
    }
    std::reverse(tiles.begin(), tiles.end());

    for (const auto& t : tiles) {
        path.push_back({t.second * ts + ts * 0.5f, t.first * ts + ts * 0.5f});
    }
    pathIndex = 0;
    return true;
}

void Monster::Update(float dt, float targetCenterX, float targetCenterY, const WorldMap& worldMap) {
    if (!alive) return;

    if (attackCooldown > 0.0f) {
        attackCooldown = std::max(0.0f, attackCooldown - dt);
    }

    repathTimer -= dt;
    if (repathTimer <= 0.0f || IsNextWaypointBlocked(worldMap)) {
        RecomputePath(targetCenterX, targetCenterY, worldMap);
        repathTimer = 0.25f;
    }

    if (pathIndex >= path.size()) return;

    const SDL_FPoint wp = path[pathIndex];
    const float cx = x + size * 0.5f;
    const float cy = y + size * 0.5f;
    const float dx = wp.x - cx;
    const float dy = wp.y - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 2.0f) {
        ++pathIndex;
        return;
    }

    float step = speed * dt;
    if (step > dist) step = dist;
    x += dx / dist * step;
    y += dy / dist * step;
}
