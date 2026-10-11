#pragma once
#include "../Core/Types.hpp"
#include <vector>
#include <string>

class SaveManager {
private:
    std::vector<ScoreRecord> scoreHistory;
    std::vector<float> timeHistory;
    bool historyLoaded = false;
    static constexpr size_t MAX_HISTORY_COUNT = 10;

public:
    SaveManager() = default;

    void LoadAll();
    void SaveAll();

    void LoadScoreHistory();
    void SaveScoreHistory();

    void LoadTimeHistory();
    void SaveTimeHistory();

    void AddScoreRecord(int score, bool success);
    void AddTimeRecord(float time);

    const std::vector<ScoreRecord>& GetScoreHistory() const { return scoreHistory; }
    const std::vector<float>& GetTimeHistory() const { return timeHistory; }
    bool IsLoaded() const { return historyLoaded; }
};
