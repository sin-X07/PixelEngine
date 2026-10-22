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
#include <deque>
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
    bool   killOnExit      = true;   // 退出时终止本程序拉起的 llama-server (释放显存)

    // 从 INI 文件解析 (失败时保留默认值, 不抛出)
    static LLMConfig FromFile(const std::string& iniPath);
};


// ---- System Prompt 构建器 ----
class PromptBuilder {
public:
    // 用 system + user 构造 OpenAI 兼容请求体（教练层使用）
    static std::string BuildRequestBodyEx(const std::string& systemPrompt,
                                          const std::string& userMessage,
                                          int maxTokens = 260,
                                          float temperature = 0.6f);
};

// ---- 异步 LLM 控制器 ----
class LLMBotController {
public:
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


    // 是否正在等待决策中
    bool IsBusy() const { return busy_.load(); }

    // 通用请求：自定义 system prompt，回调返回模型原文（教练层用）
    using RawCallback = std::function<void(const std::string&)>;
    void AskRaw(const std::string& systemPrompt, const std::string& userMessage,
                int maxTokens, RawCallback callback, const std::string& tag = std::string());

private:
    // HTTP 核心
    // 发送请求，成功时把模型原文写入 rawOut
    bool DoHttpRequest(const std::string& requestBody, const std::string& tag,
                       std::string& rawOut);

    // 健康检测: 通过 GET /health 探测 server 是否就绪
    bool HealthCheck(int timeoutMs = 15000);

    // 启动 llama-server 子进程
    bool LaunchServer(const std::string& exePath, const std::string& modelPath,
                      const std::string& host, int port, int ngl);

    // 终止子进程（强制，且校验确实退出）
    void KillServer();

    // PID 文件：跨会话记录"本程序拉起的 llama-server"，保证重启后仍能回收显存
    static std::string PidFilePath();
    void WritePidFile(unsigned long pid);
    unsigned long ReadPidFile() const;   // 0 = 无 / 已失效
    void RemovePidFile();

    void WorkerLoop();

    std::string serverUrl_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    bool ownProcess_ = false;  // 是否由本对象（含跨会话接管）拥有的子进程
    bool killOnExit_ = true;   // 退出时是否终止子进程

    // 子进程句柄 (void* 避免引入 windows.h 污染 Engine)
    void* hServerProcess_ = nullptr;
    void* hServerThread_  = nullptr;
    unsigned long ownedPid_ = 0;   // 拥有的 llama-server PID（无句柄时的回退路径）

    // 待处理请求
    // 请求队列：两队可能在同一帧都要计划，单槽会被覆盖 → 排队执行
    struct PendingRequest {
        std::string body;          // 非 raw 模式=用户消息原文；raw 模式=user message
        std::string tag;
        bool        raw = false;
        std::string system;
        int         maxTokens = 260;
        RawCallback      rawCb;
    };
    std::deque<PendingRequest> queue_;
    mutable std::mutex mutex_;

    // 两个工作线程：两队可以真正并发请求（服务端已开 --parallel 2）
    static constexpr int kWorkers = 2;
    std::thread workers_[kWorkers];
    std::atomic<int> activeWorkers_{0};
};