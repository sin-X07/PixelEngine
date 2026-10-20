#pragma once
#include <vector>
#include <utility>
#include <cstdint>

class CS2DWorld;

// ============================================================================
// PathFinder — 网格 A* 寻路
//
//  - 载入地图时按「角色碰撞体可通过」预烘焙可行走网格
//  - A* 8 方向搜索，禁止斜穿墙角
//  - 路径做视线拉直（string pulling），避免锯齿走位
//  - 提供网格级视线判定，供平滑与「能否直达」判断复用
// ============================================================================

class PathFinder {
public:
    using Point = std::pair<float, float>;

    // 按地图重建可行走网格（agentRadius: 角色半径，单位世界像素）
    void Build(const CS2DWorld& world, float agentRadius = 3.5f);

    bool IsBuilt() const { return !walkable_.empty(); }

    // 寻路：从 (sx,sy) 到 (tx,ty)，输出世界坐标路点（不含起点）
    // 起点/终点会吸附到最近可行走格；不可达返回 false
    bool FindPath(float sx, float sy, float tx, float ty,
                  std::vector<Point>& outPath) const;

    // 两点之间是否被墙体阻挡（网格级 DDA）
    bool HasLineOfSight(float x1, float y1, float x2, float y2) const;

    // 世界坐标是否可行走
    bool IsWalkableWorld(float x, float y) const;

    int  Width()  const { return w_; }
    int  Height() const { return h_; }
    float TileW() const { return tileW_; }
    float TileH() const { return tileH_; }

private:
    int   w_ = 0, h_ = 0;
    float tileW_ = 8.0f, tileH_ = 8.0f;
    float originX_ = 0.0f, originY_ = 0.0f;
    std::vector<uint8_t> walkable_;    // 1 = 可走

    // 世界坐标 → 格坐标
    int ColOf(float x) const;
    int RowOf(float y) const;
    float CenterX(int col) const;
    float CenterY(int row) const;
    bool InBounds(int col, int row) const;
    bool WalkableAt(int col, int row) const;

    // 在给定点附近搜索最近可行走格（用于吸附）
    bool SnapToWalkable(int& col, int& row, int maxRadius = 8) const;

    // A* 核心
    bool AStar(int sc, int sr, int tc, int tr, std::vector<int>& outCells) const;

    // 把格路径拉直为世界路点
    void SmoothPath(const std::vector<int>& cells,
                    std::vector<Point>& outPath) const;
};
