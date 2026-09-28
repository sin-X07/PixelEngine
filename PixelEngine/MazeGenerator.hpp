#pragma once
#include <vector>
#include <stack>
#include <fstream>
#include <random>
#include <iostream>

class MazeGenerator {
private:
    struct GridPos {
        int r, c;
    };

public:
    static void GenerateRandomMazeFile(const std::string& filename, std::mt19937& gen) {
        const int ROWS = 15;
        const int COLS = 20;

        std::vector<std::vector<char>> grid(ROWS, std::vector<char>(COLS, '#'));
        std::vector<std::vector<bool>> visited(ROWS, std::vector<bool>(COLS, false));

        std::stack<GridPos> cellStack;
        grid[1][1] = '.';
        visited[1][1] = true;
        cellStack.push({1, 1});

        while (!cellStack.empty()) {
            GridPos current = cellStack.top();

            int dr[] = {-2, 2, 0, 0};
            int dc[] = {0, 0, -2, 2};

            std::vector<int> validDirections;
            for (int i = 0; i < 4; i++) {
                int nr = current.r + dr[i];
                int nc = current.c + dc[i];

                if (nr > 0 && nr < ROWS - 1 && nc > 0 && nc < COLS - 1) {
                    if (!visited[nr][nc]) validDirections.push_back(i);
                }
            }

            if (!validDirections.empty()) {
                std::uniform_int_distribution<int> randDir(0, validDirections.size() - 1);
                int dir = validDirections[randDir(gen)];

                int nextR = current.r + dr[dir];
                int nextC = current.c + dc[dir];

                int wallR = current.r + dr[dir] / 2;
                int wallC = current.c + dc[dir] / 2;

                grid[wallR][wallC] = '.';
                grid[nextR][nextC] = '.';
                visited[nextR][nextC] = true;

                cellStack.push({nextR, nextC});
            } else {
                cellStack.pop();
            }
        }

        grid[1][1] = 'P';

        bool coinPlaced = false;
        for (int r = ROWS - 2; r > 0 && !coinPlaced; r--) {
            for (int c = COLS - 2; c > 0 && !coinPlaced; c--) {
                if (grid[r][c] == '.') {
                    grid[r][c] = 'C';
                    coinPlaced = true;
                }
            }
        }

        std::ofstream outFile(filename);
        if (outFile.is_open()) {
            for (int r = 0; r < ROWS; r++) {
                for (int c = 0; c < COLS; c++) {
                    outFile << grid[r][c];
                }
                outFile << "\n";
            }
            outFile.close();
            std::cout << "全新随即迷宫已写入 " << filename << std::endl;
        }
    }
};