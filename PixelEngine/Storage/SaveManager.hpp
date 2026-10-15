#pragma once
#include "../Core/Types.hpp"
#include <vector>
#include <string>

class SaveManager {
private:
    std::vector<ScoreRecord> scoreHistory;
    std::vector<float> timeHistory;
    int selectedRole = 0; // 当前选中角色索引（0..ROLE_COUNT-1）
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

    // 角色选择持久化（role_saves.txt 单行整数；缺失/损坏/越界回退 0）
    void LoadRoleIndex();
    void SaveRoleIndex();
    int GetSelectedRole() const { return selectedRole; }
    void SetSelectedRole(int index);
};
