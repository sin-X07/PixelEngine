#include "MazeGenerator.hpp"
#include "Core/PathUtils.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <stack>
#include <string>
#include <vector>

namespace MazeGenerator {

namespace {
    struct GridPos {
        int r;
        int c;
    };
}

void GenerateRandomMazeFile(const std::string& filename, std::mt19937& gen) {
    constexpr int ROWS = 29;
    constexpr int COLS = 39;
    constexpr GridPos START = {1, 1};
    constexpr std::array<GridPos, 4> DIRECTIONS = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};

    const auto inBounds = [](int row, int col) {
        return row > 0 && row < ROWS - 1 && col > 0 && col < COLS - 1;
    };
    const auto isBlocked = [](char tile, bool obstaclesBlockMovement) {
        return tile == '#' || (obstaclesBlockMovement && (tile == 'H' || tile == 'o'));
    };
    const auto findPath = [&](const std::vector<std::vector<char>>& grid, GridPos start,
                              GridPos goal, bool obstaclesBlockMovement) {
        std::vector<std::vector<GridPos>> parent(
            ROWS, std::vector<GridPos>(COLS, {-1, -1}));
        std::queue<GridPos> frontier;
        parent[start.r][start.c] = start;
        frontier.push(start);

        while (!frontier.empty()) {
            const GridPos current = frontier.front();
            frontier.pop();
            if (current.r == goal.r && current.c == goal.c) break;

            for (const GridPos direction : DIRECTIONS) {
                const int nextRow = current.r + direction.r;
                const int nextCol = current.c + direction.c;
                if (!inBounds(nextRow, nextCol) ||
                    parent[nextRow][nextCol].r != -1 ||
                    isBlocked(grid[nextRow][nextCol], obstaclesBlockMovement)) {
                    continue;
                }
                parent[nextRow][nextCol] = current;
                frontier.push({nextRow, nextCol});
            }
        }

        std::vector<GridPos> path;
        if (parent[goal.r][goal.c].r == -1) return path;
        for (GridPos current = goal;; current = parent[current.r][current.c]) {
            path.push_back(current);
            if (current.r == start.r && current.c == start.c) break;
        }
        std::reverse(path.begin(), path.end());
        return path;
    };

    std::vector<std::vector<char>> completedGrid;
    bool generated = false;

    for (int generationAttempt = 0; generationAttempt < 12 && !generated; ++generationAttempt) {
        std::vector<std::vector<char>> grid(ROWS, std::vector<char>(COLS, '#'));
        std::vector<std::vector<bool>> visited(ROWS, std::vector<bool>(COLS, false));
        std::stack<GridPos> cellStack;
        grid[START.r][START.c] = '.';
        visited[START.r][START.c] = true;
        cellStack.push(START);

        while (!cellStack.empty()) {
            const GridPos current = cellStack.top();
            std::vector<GridPos> validDirections;
            for (const GridPos direction : DIRECTIONS) {
                const int nextRow = current.r + direction.r * 2;
                const int nextCol = current.c + direction.c * 2;
                if (inBounds(nextRow, nextCol) && !visited[nextRow][nextCol]) {
                    validDirections.push_back(direction);
                }
            }

            if (validDirections.empty()) {
                cellStack.pop();
                continue;
            }

            std::uniform_int_distribution<size_t> directionIndex(0, validDirections.size() - 1);
            const GridPos direction = validDirections[directionIndex(gen)];
            const int nextRow = current.r + direction.r * 2;
            const int nextCol = current.c + direction.c * 2;
            grid[current.r + direction.r][current.c + direction.c] = '.';
            grid[nextRow][nextCol] = '.';
            visited[nextRow][nextCol] = true;
            cellStack.push({nextRow, nextCol});
        }

        std::vector<GridPos> braidCandidates;
        for (int row = 1; row < ROWS - 1; ++row) {
            for (int col = 1; col < COLS - 1; ++col) {
                if (grid[row][col] != '#') continue;
                const bool verticalConnection = grid[row - 1][col] != '#' && grid[row + 1][col] != '#';
                const bool horizontalConnection = grid[row][col - 1] != '#' && grid[row][col + 1] != '#';
                if (verticalConnection || horizontalConnection) braidCandidates.push_back({row, col});
            }
        }
        std::shuffle(braidCandidates.begin(), braidCandidates.end(), gen);
        std::uniform_int_distribution<int> braidCount(3, 5);
        const int braidsToOpen = std::min(static_cast<int>(braidCandidates.size()), braidCount(gen));
        for (int index = 0; index < braidsToOpen; ++index) {
            grid[braidCandidates[index].r][braidCandidates[index].c] = '.';
        }

        const auto findDeadEnds = [&]() {
            std::vector<GridPos> deadEnds;
            for (int row = 1; row < ROWS - 1; row += 2) {
                for (int col = 1; col < COLS - 1; col += 2) {
                    if (grid[row][col] == '#') continue;
                    int exits = 0;
                    for (const GridPos direction : DIRECTIONS) {
                        if (grid[row + direction.r][col + direction.c] != '#') ++exits;
                    }
                    if (exits == 1 && !(row == START.r && col == START.c)) {
                        deadEnds.push_back({row, col});
                    }
                }
            }
            return deadEnds;
        };
        const auto extendDeadEnd = [&](GridPos deadEnd) {
            std::vector<GridPos> extensionDirections;
            for (const GridPos direction : DIRECTIONS) {
                const int nextRow = deadEnd.r + direction.r * 2;
                const int nextCol = deadEnd.c + direction.c * 2;
                if (inBounds(nextRow, nextCol) &&
                    grid[deadEnd.r + direction.r][deadEnd.c + direction.c] == '#' &&
                    grid[nextRow][nextCol] == '#') {
                    extensionDirections.push_back(direction);
                }
            }
            if (extensionDirections.empty()) return false;

            std::shuffle(extensionDirections.begin(), extensionDirections.end(), gen);
            const GridPos direction = extensionDirections.front();
            int length = 1;
            const int secondRow = deadEnd.r + direction.r * 4;
            const int secondCol = deadEnd.c + direction.c * 4;
            if (inBounds(secondRow, secondCol) &&
                grid[deadEnd.r + direction.r * 3][deadEnd.c + direction.c * 3] == '#' &&
                grid[secondRow][secondCol] == '#') {
                std::uniform_int_distribution<int> extensionLength(1, 2);
                length = extensionLength(gen);
            }

            for (int step = 1; step <= length; ++step) {
                grid[deadEnd.r + direction.r * (step * 2 - 1)]
                    [deadEnd.c + direction.c * (step * 2 - 1)] = '.';
                grid[deadEnd.r + direction.r * step * 2]
                    [deadEnd.c + direction.c * step * 2] = '.';
            }
            return true;
        };
        const auto expandDeadEndIntoRoom = [&](GridPos deadEnd) {
            const int minRow = deadEnd.r - 1;
            const int maxRow = deadEnd.r + 1;
            const int minCol = deadEnd.c - 1;
            const int maxCol = deadEnd.c + 1;

            if (!inBounds(minRow, minCol) || !inBounds(maxRow, maxCol)) return false;

            for (int row = minRow; row <= maxRow; ++row) {
                for (int col = minCol; col <= maxCol; ++col) {
                    if (row == START.r && col == START.c) return false;
                }
            }

            for (int row = minRow; row <= maxRow; ++row) {
                for (int col = minCol; col <= maxCol; ++col) {
                    grid[row][col] = '.';
                }
            }
            return true;
        };

        std::uniform_int_distribution<int> expandedDeadEndCount(4, 6);
        const int expansionsTarget = expandedDeadEndCount(gen);
        int expansionsMade = 0;
        while (expansionsMade < expansionsTarget) {
            std::vector<GridPos> deadEnds = findDeadEnds();
            std::shuffle(deadEnds.begin(), deadEnds.end(), gen);
            bool expanded = false;
            for (const GridPos deadEnd : deadEnds) {
                std::bernoulli_distribution makeRoom(0.5);
                const bool preferRoom = makeRoom(gen);
                if ((preferRoom && (expandDeadEndIntoRoom(deadEnd) || extendDeadEnd(deadEnd))) ||
                    (!preferRoom && (extendDeadEnd(deadEnd) || expandDeadEndIntoRoom(deadEnd)))) {
                    expanded = true;
                    ++expansionsMade;
                    break;
                }
            }
            if (!expanded) break;
        }

        std::vector<std::vector<int>> distance(ROWS, std::vector<int>(COLS, -1));
        std::queue<GridPos> frontier;
        frontier.push(START);
        distance[START.r][START.c] = 0;
        GridPos treasure = START;
        while (!frontier.empty()) {
            const GridPos current = frontier.front();
            frontier.pop();
            if (distance[current.r][current.c] > distance[treasure.r][treasure.c]) treasure = current;
            for (const GridPos direction : DIRECTIONS) {
                const int nextRow = current.r + direction.r;
                const int nextCol = current.c + direction.c;
                if (!inBounds(nextRow, nextCol) || distance[nextRow][nextCol] != -1 ||
                    grid[nextRow][nextCol] == '#') {
                    continue;
                }
                distance[nextRow][nextCol] = distance[current.r][current.c] + 1;
                frontier.push({nextRow, nextCol});
            }
        }

        grid[START.r][START.c] = 'P';
        grid[treasure.r][treasure.c] = 'C';
        const std::vector<GridPos> criticalPath = findPath(grid, START, treasure, false);

        const auto isNearCriticalPath = [&](GridPos candidate) {
            for (const GridPos pathTile : criticalPath) {
                if (std::abs(candidate.r - pathTile.r) + std::abs(candidate.c - pathTile.c) <= 1) {
                    return true;
                }
            }
            return false;
        };
        std::vector<GridPos> obstacleCandidates;
        for (int row = 1; row < ROWS - 1; ++row) {
            for (int col = 1; col < COLS - 1; ++col) {
                if (grid[row][col] == '.' && !isNearCriticalPath({row, col})) {
                    obstacleCandidates.push_back({row, col});
                }
            }
        }
        std::shuffle(obstacleCandidates.begin(), obstacleCandidates.end(), gen);
        std::uniform_int_distribution<int> obstacleCount(6, 10);
        const int obstaclesTarget = obstacleCount(gen);
        const int lowWallTarget = std::min(3, std::max(2, obstaclesTarget / 3));
        int lowWallsPlaced = 0;
        int obstaclesPlaced = 0;
        std::vector<GridPos> placedObstacles;
        for (const GridPos candidate : obstacleCandidates) {
            bool tooClose = false;
            for (const GridPos placed : placedObstacles) {
                if (std::abs(candidate.r - placed.r) + std::abs(candidate.c - placed.c) < 3) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;

            grid[candidate.r][candidate.c] = lowWallsPlaced < lowWallTarget ? 'H' : 'o';
            if (lowWallsPlaced < lowWallTarget) ++lowWallsPlaced;
            placedObstacles.push_back(candidate);
            ++obstaclesPlaced;
            if (obstaclesPlaced == obstaclesTarget) break;
        }

        if (!criticalPath.empty() && !findPath(grid, START, treasure, true).empty()) {
            completedGrid = std::move(grid);
            generated = true;
        }
    }

    if (!generated) {
        std::cerr << "迷宫生成失败：无法构建可步行通关的地图。" << std::endl;
        return;
    }

    std::ofstream outFile(PathUtils::ResolveWritablePath(filename));
    if (!outFile.is_open()) {
        std::cerr << "无法写入迷宫文件：" << filename << std::endl;
        return;
    }
    for (const auto& row : completedGrid) {
        for (const char tile : row) outFile << tile;
        outFile << '\n';
    }
    std::cout << "全新随机编织迷宫已写入 " << filename << std::endl;
}

} // namespace MazeGenerator
