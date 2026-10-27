#pragma once
// ============================================================================
// TacticalIntel —— 按包点聚合的敌方威胁度（信息层）
//
// 只做统计，不做决策：
//   - 每次「看到敌人」累加热度（有上限，避免刷屏）
//   - 热度随时间指数衰减：10 秒前看到 2 人 ≠ 现在还有 2 人
//   - 提供按包点 / 按区域名的查询，供规则层与 LLM digest 使用
// ============================================================================

#include <string>
#include <vector>
#include "CS2DTypes.hpp"

class CS2DWorld;

struct SiteHeat {
    std::string area;        // 包点所在区域名
    float cx = 0.0f, cy = 0.0f;   // 包点中心
    std::vector<ObjectDef> rects;  // 包点矩形（一个包点可由多块组成）
    float heat = 0.0f;       // 衰减后的威胁度
    float lastSeen = -999.0f;// 最后一次观察到敌人的时间
    int   lastCount = 0;     // 最近一次累计到的敌人数（粗估）
};

class TacticalIntel {
public:
    // 衰减时间常数（秒）：越大记忆越久
    static constexpr float kDecayTau = 14.0f;
    // 单点热度上限，避免长时间架枪把热度刷爆
    static constexpr float kMaxHeat  = 6.0f;
    // 一次观察增加多少热度
    static constexpr float kSeenGain = 1.0f;

    void Build(const CS2DWorld* world, float now);

    // 观察到一个敌人（同一敌人短时间内重复上报会被调用方过滤）
    void OnEnemySeen(float x, float y, float now);

    // 每帧衰减
    void Update(float dt);

    int    SiteCount() const { return static_cast<int>(sites_.size()); }
    const SiteHeat& Site(int i) const { return sites_[static_cast<size_t>(i)]; }
    float  Heat(int i) const;
    float  HeatOfArea(const std::string& area) const;
    // 按侧取热度："A"/"B" —— 用该侧包点区域中心找最近的包点
    float  HeatOfSide(const CS2DWorld* world, const std::string& side) const;
    std::string AreaName(int i) const;

    // 点所在（或最近）的包点索引；-1 = 不在任何包点附近
    int SiteIndexAt(float x, float y) const;

    // 总观察次数（可用于判断「有没有任何情报」）
    int  ObservationCount() const { return observations_; }

    void Reset();

private:
    std::vector<SiteHeat> sites_;
    int observations_ = 0;
};
