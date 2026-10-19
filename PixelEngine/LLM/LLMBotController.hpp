#pragma once
// ============================================================================
// LLMBotController — llama-server (OpenAI 兼容 API) 异步客户端
//
// 设计原则:
//   - 对游戏主循环零阻塞 — HTTP 请求跑在独立线程上
//   - 线程安全 — 共享状态通过互斥锁保护
//   - 接口简洁 — 一个 Decide() 调用, 一个回调通知
//   - 故障安全 — LLM 不可用时自动回退到传统行为树
//   - 自动拉起  — 可选择自动 CreateProcess 启动 llama-server
//   - 可配置    — 从 llm.ini 读取端口/模型路径/自动拉起开关
//
// 依赖:
//   - Windows 自带的 WinHTTP (不需要外部库)
//   - 项目内置的 MiniJSON (CS2D/MiniJSON.hpp)
// ============================================================================

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include "../CS2D/MiniJSON.hpp"

// ---- 配置结构体 (从 llm.ini 解析) ----
struct LLMConfig {
    std::string serverUrl   = "http://127.0.0.1:9931";
    bool   autoLaunch       = false;
    std::string serverExePath;       // llama-server.exe 路径 (可相对)
    std::string modelPath;           // 模型 .gguf 路径 (可相对)
    int    ngl             = 99;     // GPU 全卸载层数
    int    startupWaitSec  = 20;     // 自动拉起后等待模型加载最长时间

    // 从 INI 文件解析 (失败时保留默认值, 不抛出)
    static LLMConfig FromFile(const std::string& iniPath);
};

// ---- LLM 决策结果 ----
struct LLMDecision {
    std::string action;   // "进攻" / "防守" / "撤退" / "掩体移动"
    std::string target;   // 目标描述 (如 "敌人" / "掩体" / "B点")
    std::string shoot;    // "连射" / "点射" / "停止射击"
    std::string reason;   // 简短理由

    bool valid = false;   // false 表示 LLM 调用失败, 调用方应回退

    static LLMDecision FromJSON(const std::string& json);
    std::string ToJSON() const;
};

// ---- System Prompt 构建器 ----
class PromptBuilder {
public:
    static std::string BuildSystemPrompt();
    static std::string BuildUserMessage(
        float hp, float maxHp, float armor,
        const std::string& weapon, int ammo, int maxAmmo,
        float x, float y, const std::string& area,
        bool enemyVisible, float enemyDist, const std::string& enemyDir,
        bool enemyFiring, bool teammateAlive, float teammateDist,
        const std::string& teammateDir, bool isReloading);
    static std::string BuildRequestBody(
        const std::string& userMessage,
        int maxTokens = 80,
        float temperature = 0.7f);
};

// ---- 异步 LLM 控制器 ----
class LLMBotController {
public:
    using DecisionCallback = std::function<void(const LLMDecision&)>;

    LLMBotController();
    ~LLMBotController();

    // 从配置文件初始化；返回 true 表示就绪 (自行拉起或连接已存在的 server)
    // config 里 autoLaunch=true 时会自动 CreateProcess
    bool Initialize(const LLMConfig& config);

    // 关闭：终止工作线程，若自行拉起了 server 则终止进程
    void Shutdown();

    // 是否已初始化且健康
    bool IsReady() const { return ready_; }

    // 是否自行拉起了 llama-server 子进程
    bool IsOwnProcess() const { return ownProcess_; }

    // 异步请求决策 (立即返回, 在工作线程上调用 callback)
    void Decide(const std::string& stateJSON, DecisionCallback callback);

    // 是否正在等待决策中
    bool IsBusy() const { return busy_.load(); }

private:
    // HTTP 核心
    LLMDecision DoHttpRequest(const std::string& requestBody);

    // 健康检测: 通过 GET /health 探测 server 是否就绪
    bool HealthCheck(int timeoutMs = 15000);

    // 启动 llama-server 子进程
    bool LaunchServer(const std::string& exePath, const std::string& modelPath,
                      const std::string& host, int port, int ngl);

    // 终止子进程
    void KillServer();

    void WorkerLoop();

    std::string serverUrl_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    bool ownProcess_ = false;  // 是否由本对象拉起的子进程

    // 子进程句柄 (void* 避免引入 windows.h 污染 Engine)
    void* hServerProcess_ = nullptr;
    void* hServerThread_  = nullptr;

    // 待处理请求
    std::string pendingBody_;
    DecisionCallback pendingCallback_;
    mutable std::mutex mutex_;

    std::thread workerThread_;
};