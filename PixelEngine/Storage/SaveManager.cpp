#include "SaveManager.hpp"
#include "../Core/PathUtils.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

void SaveManager::LoadAll() {
    LoadScoreHistory();
    LoadTimeHistory();
    historyLoaded = true;
}

void SaveManager::SaveAll() {
    if (historyLoaded) {
        SaveScoreHistory();
        SaveTimeHistory();
    }
}

void SaveManager::SaveScoreHistory() {
    const std::string savePath = PathUtils::ResolveProjectFile("score_saves.txt");
    std::ofstream outFile(savePath);

    if (!outFile) {
        std::cerr << "分数存档文件打开失败!" << std::endl;
        return;
    }

    for (const auto& record : scoreHistory) {
        outFile << record.score << " " << (record.success ? 1 : 0) << "\n";
    }

    outFile.close();
    std::cout << "分数存档文件保存成功!" << std::endl;
}

void SaveManager::SaveTimeHistory() {
    const std::string savePath = PathUtils::ResolveProjectFile("time_saves.txt");
    std::ofstream outFile(savePath);

    if (!outFile) {
        std::cerr << "时间存档文件打开失败!" << std::endl;
        return;
    }

    for (float hTime : timeHistory) {
        outFile << hTime << "\n";
    }

    outFile.close();
    std::cout << "时间存档文件保存成功!" << std::endl;
}

void SaveManager::LoadScoreHistory() {
    const std::string savePath = PathUtils::ResolveProjectFile("score_saves.txt");
    std::ifstream inFile(savePath);

    if (!inFile) {
        std::cout << "创建新分数档案。" << std::endl;
    }

    scoreHistory.clear();
    std::string line;
    while (std::getline(inFile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::istringstream iss(line);
        int hScore = 0;
        int flag = 1; // 默认标记为成功（向下兼容旧存档）
        if (iss >> hScore) {
            if (!(iss >> flag)) {
                flag = 1;
            }
            scoreHistory.push_back({hScore, flag != 0});
        }
    }

    inFile.close();

    if (scoreHistory.size() > MAX_HISTORY_COUNT) {
        scoreHistory.resize(MAX_HISTORY_COUNT);
    }

    std::cout << "成功读取 " << scoreHistory.size() << " 局历史分数!" << std::endl;
}

void SaveManager::LoadTimeHistory() {
    const std::string savePath = PathUtils::ResolveProjectFile("time_saves.txt");
    std::ifstream inFile(savePath);

    if (!inFile) {
        std::cout << "创建新时间档案。" << std::endl;
    }

    timeHistory.clear();
    float hTime = 0.0f;

    while (inFile >> hTime) {
        timeHistory.push_back(hTime);
    }

    inFile.close();

    if (timeHistory.size() > MAX_HISTORY_COUNT) {
        timeHistory.resize(MAX_HISTORY_COUNT);
    }

    std::cout << "成功读取 " << timeHistory.size() << " 局历史时间!" << std::endl;
}

void SaveManager::AddScoreRecord(int score, bool success) {
    scoreHistory.insert(scoreHistory.begin(), {score, success});
    if (scoreHistory.size() > MAX_HISTORY_COUNT) {
        scoreHistory.pop_back();
    }
}

void SaveManager::AddTimeRecord(float time) {
    timeHistory.insert(timeHistory.begin(), time);
    if (timeHistory.size() > MAX_HISTORY_COUNT) {
        timeHistory.pop_back();
    }
}
