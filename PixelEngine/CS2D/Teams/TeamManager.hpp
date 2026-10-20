#pragma once
#include "../../Core/Types.hpp"

// ============================================================================
// TeamManager — 阵营比分与胜负判定
//
// 只负责比分与"比赛是否结束"；阵营成员与存活统计由 CS2DGame 持有。
// ============================================================================
class TeamManager {
public:
    void Reset() {
        scoreT_  = 0;
        scoreCT_ = 0;
    }

    void AddScore(Team t) {
        if (t == Team::T)       ++scoreT_;
        else if (t == Team::CT) ++scoreCT_;
    }

    int Score(Team t) const {
        if (t == Team::T)  return scoreT_;
        if (t == Team::CT) return scoreCT_;
        return 0;
    }

    int ScoreT()  const { return scoreT_; }
    int ScoreCT() const { return scoreCT_; }

    void SetScoreToWin(int n) { scoreToWin_ = (n > 0) ? n : 1; }
    int  ScoreToWin() const { return scoreToWin_; }

    bool MatchOver() const {
        return scoreT_ >= scoreToWin_ || scoreCT_ >= scoreToWin_;
    }

    Team MatchWinner() const {
        if (scoreT_  >= scoreToWin_) return Team::T;
        if (scoreCT_ >= scoreToWin_) return Team::CT;
        return Team::None;
    }

    // 对方阵营（Team::None 时返回 None）
    static Team Opponent(Team t) {
        if (t == Team::T)  return Team::CT;
        if (t == Team::CT) return Team::T;
        return Team::None;
    }

private:
    int scoreT_     = 0;
    int scoreCT_    = 0;
    int scoreToWin_ = 8;
};
