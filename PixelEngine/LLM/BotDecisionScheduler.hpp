#pragma once
// ============================================================================
// BotDecisionScheduler — LLM 决策调度器
//
// 职责（单一）:
//   把 CS2D 战场状态翻译成 LLM 请求，按轮询节奏分发给各 BOT，
//   再把 LLM 的回答翻译回 BotIntent 写入对应 BOT。
//
// 分层位置:
//   Engine::Update  →  BotDecisionScheduler::Tick(dt, game)
//                        └─ 采样状态 → PromptBuilder → LLMBotController
//                                                     └─ 回调写 bot.llmIntent
//
// 设计要点:
//   - Engine 不再持有轮询索引/冷却计时/请求构建逻辑
//   - 与 CS2DGame 仅通过构造函数/ Tick 参数耦合，便于单独测试
//   - LLM 不可用时 Tick 直接返回，BOT 自动退回传统行为树
// ============================================================================

#include "LLMBotController.hpp"
#include <memory>
#include <string>

class CS2DGame;

class BotDecisionScheduler {
public:
    // 轮询间隔（毫秒）：每 N ms 向一个存活 BOT 发一次决策请求。
    // 8 个 BOT 全量刷新一轮 = N * 8。
    static constexpr int DEFAULT_INTERVAL_MS = 800;

    BotDecisionScheduler() = default;

    // 从配置文件初始化（内部创建并启动 LLMBotController）
    // 返回 true 表示 LLM 就绪（服务器可用或已自动拉起）
    bool Initialize(const std::string& iniPath);

    // 关闭：终止工作线程；若自行拉起了 llama-server 也一并终止
    void Shutdown();

    // LLM 是否就绪（未就绪时 Tick 为空操作）
    bool IsReady() const;

    // 推进调度：内部按 interval 节流，累加到点才发一次请求。
    // 每帧调用即可，不需要调用方做计时。
    void Tick(float dt, CS2DGame& game);

    // 供教练层直接复用同一个 LLM 客户端
    LLMBotController* Controller() { return controller_.get(); }

    // 调整轮询间隔（毫秒），运行时可变
    void SetIntervalMs(int ms) { intervalMs_ = (ms > 0) ? ms : DEFAULT_INTERVAL_MS; }
    int  GetIntervalMs() const { return intervalMs_; }

private:
    // 为一个存活 BOT 构建状态并发出异步请求
    void DispatchOne(CS2DGame& game);

    std::unique_ptr<LLMBotController> controller_;
    int   roundRobinIdx_ = 0;      // 下一个待请求的 BOT 索引
    float cooldownSec_   = 0.0f;   // 距离下次请求的剩余秒数
    int   intervalMs_    = DEFAULT_INTERVAL_MS;
};
