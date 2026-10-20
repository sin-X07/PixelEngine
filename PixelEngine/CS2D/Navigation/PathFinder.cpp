#include "PathFinder.hpp"
#include "../CS2DWorld.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <limits>

namespace {

// 八方向
const int kDirX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
const int kDirY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

// 八方向代价
const float kCost[8] = { 1.0f, 1.0f, 1.0f, 1.0f,
                         1.41421f, 1.41421f, 1.41421f, 1.41421f };

inline float Octile(int dx, int dy) {
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    const int mn = std::min(ax, ay);
    const int mx = std::max(ax, ay);
    return static_cast<float>(mx - mn) + 1.41421f * static_cast<float>(mn);
}

} // namespace

// ============================================================================
// 构建
// ============================================================================

void PathFinder::Build(const CS2DWorld& world, float agentRadius) {
    w_ = world.GetMapWidth();
    h_ = world.GetMapHeight();
    tileW_ = static_cast<float>(world.GetTileWidth());
    tileH_ = static_cast<float>(world.GetTileHeight());
    originX_ = 0.0f;
    originY_ = 0.0f;

    walkable_.assign(static_cast<size_t>(w_) * h_, 0);
    if (w_ <= 0 || h_ <= 0) return;

    const float d = agentRadius * 2.0f;
    for (int row = 0; row < h_; ++row) {
        for (int col = 0; col < w_; ++col) {
            const float cx = CenterX(col);
            const float cy = CenterY(row);
            // 角色碰撞体完全落在可通行区域才算可走
            const bool blocked = world.CheckRectCollision(cx - agentRadius, cy - agentRadius, d, d);
            walkable_[static_cast<size_t>(row) * w_ + col] = blocked ? 0 : 1;
        }
    }
}

// ============================================================================
// 坐标换算
// ============================================================================

int PathFinder::ColOf(float x) const {
    return static_cast<int>(std::floor((x - originX_) / tileW_));
}

int PathFinder::RowOf(float y) const {
    return static_cast<int>(std::floor((y - originY_) / tileH_));
}

float PathFinder::CenterX(int col) const {
    return originX_ + (static_cast<float>(col) + 0.5f) * tileW_;
}

float PathFinder::CenterY(int row) const {
    return originY_ + (static_cast<float>(row) + 0.5f) * tileH_;
}

bool PathFinder::InBounds(int col, int row) const {
    return col >= 0 && col < w_ && row >= 0 && row < h_;
}

bool PathFinder::WalkableAt(int col, int row) const {
    if (!InBounds(col, row)) return false;
    return walkable_[static_cast<size_t>(row) * w_ + col] != 0;
}

bool PathFinder::IsWalkableWorld(float x, float y) const {
    return WalkableAt(ColOf(x), RowOf(y));
}

// 以 (col,row) 为中心做环形搜索，找最近可行走格
bool PathFinder::SnapToWalkable(int& col, int& row, int maxRadius) const {
    if (WalkableAt(col, row)) return true;

    for (int r = 1; r <= maxRadius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                // 只检查环上
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int c = col + dx;
                const int rr = row + dy;
                if (WalkableAt(c, rr)) { col = c; row = rr; return true; }
            }
        }
    }
    return false;
}

// ============================================================================
// 视线（网格 DDA）
// ============================================================================

bool PathFinder::HasLineOfSight(float x1, float y1, float x2, float y2) const {
    if (!IsBuilt()) return false;

    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.001f) return WalkableAt(ColOf(x1), RowOf(y1));

    // 步长取半格，保证不漏格
    const float step = std::min(tileW_, tileH_) * 0.5f;
    const int steps = std::max(1, static_cast<int>(dist / step));

    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float sx = x1 + dx * t;
        const float sy = y1 + dy * t;
        if (!WalkableAt(ColOf(sx), RowOf(sy))) return false;
    }
    return true;
}

// ============================================================================
// A*
// ============================================================================

bool PathFinder::AStar(int sc, int sr, int tc, int tr, std::vector<int>& outCells) const {
    outCells.clear();
    if (!IsBuilt()) return false;
    if (!WalkableAt(sc, sr) || !WalkableAt(tc, tr)) return false;
    if (sc == tc && sr == tr) return true;

    const int total = w_ * h_;
    const int startIdx = sr * w_ + sc;
    const int goalIdx  = tr * w_ + tc;

    std::vector<float> g(static_cast<size_t>(total), std::numeric_limits<float>::infinity());
    std::vector<int>   came(static_cast<size_t>(total), -1);
    std::vector<uint8_t> closed(static_cast<size_t>(total), 0);

    struct Node {
        float f;
        int   idx;
        bool operator>(const Node& o) const { return f > o.f; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    g[startIdx] = 0.0f;
    open.push({ Octile(tc - sc, tr - sr), startIdx });

    // 迭代上限：防止病态地图拖慢帧率
    int budget = total * 4;

    while (!open.empty() && budget > 0) {
        const Node cur = open.top();
        open.pop();
        if (closed[cur.idx]) continue;
        closed[cur.idx] = 1;

        if (cur.idx == goalIdx) {
            // 回溯
            std::vector<int> reversed;
            for (int i = goalIdx; i != -1; i = came[i]) {
                reversed.push_back(i);
                if (i == startIdx) break;
            }
            outCells.assign(reversed.rbegin(), reversed.rend());
            return true;
        }

        const int cx = cur.idx % w_;
        const int cy = cur.idx / w_;
        --budget;

        for (int d = 0; d < 8; ++d) {
            const int nx = cx + kDirX[d];
            const int ny = cy + kDirY[d];
            if (!WalkableAt(nx, ny)) continue;

            // 斜向禁止穿墙角：两侧正交格都必须可走
            if (d >= 4) {
                if (!WalkableAt(cx + kDirX[d], cy) || !WalkableAt(cx, cy + kDirY[d])) continue;
            }

            const int nIdx = ny * w_ + nx;
            if (closed[nIdx]) continue;

            const float ng = g[cur.idx] + kCost[d];
            if (ng >= g[nIdx]) continue;

            g[nIdx] = ng;
            came[nIdx] = cur.idx;
            open.push({ ng + Octile(tc - nx, tr - ny), nIdx });
        }
    }
    return false;
}

// ============================================================================
// 路径拉直
// ============================================================================

void PathFinder::SmoothPath(const std::vector<int>& cells, std::vector<Point>& outPath) const {
    outPath.clear();
    if (cells.empty()) return;

    // 至少 3 个点才需要平滑
    if (cells.size() <= 2) {
        for (int idx : cells) {
            outPath.emplace_back(CenterX(idx % w_), CenterY(idx / w_));
        }
        return;
    }

    size_t anchor = 0;
    while (anchor + 1 < cells.size()) {
        // 从最远点往回找第一个可直达的
        size_t best = anchor + 1;
        for (size_t probe = cells.size() - 1; probe > anchor + 1; --probe) {
            const float ax = CenterX(cells[anchor] % w_);
            const float ay = CenterY(cells[anchor] / w_);
            const float bx = CenterX(cells[probe] % w_);
            const float by = CenterY(cells[probe] / w_);
            if (HasLineOfSight(ax, ay, bx, by)) { best = probe; break; }
        }
        outPath.emplace_back(CenterX(cells[best] % w_), CenterY(cells[best] / w_));
        anchor = best;
    }
}

// ============================================================================
// 对外寻路
// ============================================================================

bool PathFinder::FindPath(float sx, float sy, float tx, float ty,
                          std::vector<Point>& outPath) const {
    outPath.clear();
    if (!IsBuilt()) return false;

    int sc = ColOf(sx), sr = RowOf(sy);
    int tc = ColOf(tx), tr = RowOf(ty);

    if (!SnapToWalkable(sc, sr) || !SnapToWalkable(tc, tr)) return false;

    // 直线可达则不绕路（避免为近处目标生成多余路点）
    if (HasLineOfSight(sx, sy, tx, ty)) {
        outPath.emplace_back(tx, ty);
        return true;
    }

    std::vector<int> cells;
    if (!AStar(sc, sr, tc, tr, cells)) return false;

    SmoothPath(cells, outPath);

    // 末点替换为精确目标（平滑后可能停在格中心）
    if (!outPath.empty()) outPath.back() = { tx, ty };
    return true;
}
