#include "LLMBotController.hpp"
#include "../Core/IniFile.hpp"
#include "../Core/PathUtils.hpp"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>

#pragma comment(lib, "winhttp.lib")

// ============================================================================
// JSON 字符串转义
// ============================================================================
namespace {

std::string EscapeJSON(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 32);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;
        }
    }
    return out;
}

} // anonymous namespace

// ---- 公开接口 ----
LLMConfig LLMConfig::FromFile(const std::string& iniPath) {
    LLMConfig cfg;
    IniFile ini = IniFile::Load(iniPath);
    if (!ini.IsLoaded()) return cfg;   // 文件缺失 → 全默认

    cfg.serverUrl = ini.GetString("server", "url", cfg.serverUrl);

    cfg.autoLaunch     = ini.GetBool  ("launcher", "auto_launch", cfg.autoLaunch);
    cfg.serverExePath  = ini.GetString("launcher", "exe",         cfg.serverExePath);
    cfg.modelPath      = ini.GetString("launcher", "model",       cfg.modelPath);
    cfg.ngl            = ini.GetInt   ("launcher", "ngl",         cfg.ngl);
    cfg.startupWaitSec = ini.GetInt   ("launcher", "startup_wait", cfg.startupWaitSec);
    cfg.killOnExit     = ini.GetBool  ("launcher", "kill_on_exit", cfg.killOnExit);

    return cfg;
}

// ============================================================================
// LLMDecision
// ============================================================================

LLMDecision LLMDecision::FromJSON(const std::string& json) {
    LLMDecision d;
    MiniJSON::Value root = MiniJSON::parse(json);
    if (!root.is_object()) return d;
    d.action = root.value<std::string>("action", "");
    d.target = root.value<std::string>("target", "");
    d.shoot  = root.value<std::string>("shoot",  "");
    d.reason = root.value<std::string>("reason", "");
    d.valid  = !d.action.empty();
    return d;
}

std::string LLMDecision::ToJSON() const {
    return "{\"action\":\"" + action + "\",\"target\":\"" + target +
           "\",\"shoot\":\"" + shoot + "\",\"reason\":\"" + reason + "\"}";
}

// ============================================================================
// PromptBuilder
// ============================================================================

std::string PromptBuilder::BuildSystemPrompt() {
    return
        "你是一个CS2D俯视角射击游戏的BOT，扮演一支5人小队中的一员。"
        "你必须直接输出战术决策，不要分析，不要解释。\n"
        "严格输出JSON格式，不要输出其他任何文字：\n"
        "{\"action\":\"行动\",\"target\":\"目标\",\"shoot\":\"射击模式\",\"reason\":\"简短理由\"}\n"
        "\n"
        "=== 你的角色（按【自身】里的角色执行）===\n"
        "- 突破(Entry)：第一个进点，负责打开空间。血>60%时主动推进、抢角度；"
        "即使要死也要让队友能补枪，不要站在开阔地犹豫。\n"
        "- 支援(Support)：跟在突破身后，负责补枪(trade)和道具。"
        "突破交火后立刻跟上换人；同时面对两人以上时优先\"掩体移动\"保存自己。\n"
        "- 断后(Anchor)：守点/殿后。不主动远距离追击，站在掩体后等敌人进射程；"
        "存活本身就是价值，血量低时优先撤退而不是换命。\n"
        "\n"
        "=== 基础战术原则 ===\n"
        "- 补枪(trade)：队友刚在附近阵亡且敌人应该还在那，优先冲过去换人，不要白白放走。\n"
        "- 卡位/架枪：有掩体的位置优先\"防守\"；没有掩体不要长时间站桩。\n"
        "- 交火优势：己方人数多于敌方时才\"进攻\"；人数劣势优先\"撤退\"保存有生力量。\n"
        "- 交叉火力：队友在另一侧时不要和队友走同一条路线，从另一角度压制。\n"
        "- 残局：只剩你一人且时间不多时，优先完成任务目标（下包/拆包），不要绕路找人。\n"
        "\n"
        "=== 核心规则（必须遵守）===\n"
        "- 血量<30% → 必须选\"防守\"或\"撤退\"，绝不进攻\n"
        "- 血量30%-70% → 可\"掩体移动\"或\"防守\"\n"
        "- 血量>70%且敌人数≤1 → 可\"进攻\"\n"
        "- 敌人数≥2且无队友 → 必须\"撤退\"\n"
        "- 使用狙击枪 → shoot必须是\"点射\"，action不能是\"进攻\"\n"
        "- 使用手枪/近战 → 优先\"掩体移动\"找掩护，不正面交火\n"
        "- 敌人正对且距离<10米 → 可\"连射\"\n"
        "- 敌人背对/侧对 → 优先\"点射\"\n"
        "- 换弹中 → 必须选\"掩体移动\"或\"防守\"，不能\"进攻\"\n"
        "- 队友在附近 → 可考虑协同\"进攻\"\n"
        "- 据守有利位置（高地/掩体） → 优先\"防守\"\n"
        "\n"
        "可用行动: 进攻, 防守, 撤退, 掩体移动\n"
        "可用目标(target): 敌人, 前方, 左前方, 右前方, 后退, 掩体, 队友\n"
        "射击模式: 连射, 点射, 停止射击";
}

std::string PromptBuilder::BuildUserMessage(
    float hp, float maxHp, float armor,
    const std::string& weapon, int ammo, int maxAmmo,
    float x, float y, const std::string& area,
    bool enemyVisible, float enemyDist, const std::string& enemyDir,
    bool enemyFiring, bool teammateAlive, float teammateDist,
    const std::string& teammateDir, bool isReloading)
{
    std::ostringstream oss;
    int hpPct = static_cast<int>(hp / maxHp * 100.0f);
    oss << "【自身】血量" << hpPct << "/100，护甲" << static_cast<int>(armor)
        << "。武器" << weapon << "(弹药" << ammo << "/" << maxAmmo << ")";
    if (isReloading) oss << "，正在换弹中";
    oss << "。位置(" << static_cast<int>(x) << "," << static_cast<int>(y) << "," << area << ")。";

    if (enemyVisible) {
        oss << "【敌人】" << enemyDir << static_cast<int>(enemyDist) << "米有敌人";
        if (enemyFiring) oss << "正在开火";
        oss << "。";
    } else {
        oss << "【敌人】视野内无敌人。";
    }

    if (teammateAlive) {
        oss << "【队友】" << teammateDir << static_cast<int>(teammateDist) << "米有队友存活。";
    } else {
        oss << "【队友】队友已阵亡。";
    }
    return oss.str();
}

std::string PromptBuilder::BuildRequestBodyEx(const std::string& systemPrompt,
                                              const std::string& userMessage,
                                              int maxTokens, float temperature)
{
    std::ostringstream oss;
    oss << "{"
        << "\"model\":\"gpt-3.5-turbo\","
        << "\"messages\":["
        << "{\"role\":\"system\",\"content\":\""
        << EscapeJSON(systemPrompt)
        << "\"},"
        << "{\"role\":\"user\",\"content\":\""
        << EscapeJSON(userMessage)
        << "\"}"
        << "],"
        << "\"max_tokens\":" << maxTokens << ","
        << "\"temperature\":" << temperature
        << "}";
    return oss.str();
}

std::string PromptBuilder::BuildRequestBody(
    const std::string& userMessage, int maxTokens, float temperature)
{
    std::ostringstream oss;
    oss << "{"
        << "\"model\":\"gpt-3.5-turbo\","
        << "\"messages\":["
        << "{\"role\":\"system\",\"content\":\""
        << EscapeJSON(BuildSystemPrompt())
        << "\"},"
        << "{\"role\":\"user\",\"content\":\""
        << EscapeJSON(userMessage)
        << "\"}"
        << "],"
        << "\"max_tokens\":" << maxTokens << ","
        << "\"temperature\":" << temperature
        << "}";
    return oss.str();
}

// ============================================================================
// LLMBotController
// ============================================================================

LLMBotController::LLMBotController() = default;

LLMBotController::~LLMBotController() {
    Shutdown();
}

// ---- Initialize → 读取配置 → 可能自动拉起 → 健康检测 ----
bool LLMBotController::Initialize(const LLMConfig& config) {
    serverUrl_ = config.serverUrl;
    killOnExit_ = config.killOnExit;

    // 0. 若存在上次运行遗留的 PID 文件，先校验是否仍有效
    const unsigned long stalePid = ReadPidFile();
    if (stalePid == 0) RemovePidFile();

    // 1. 如果配置了自动拉起
    if (config.autoLaunch && !config.serverExePath.empty() && !config.modelPath.empty()) {

        // 解析 host:port
        std::string host = "127.0.0.1";
        int port = 9931;
        std::string url = serverUrl_;
        if (url.compare(0, 7, "http://") == 0) url = url.substr(7);
        else if (url.compare(0, 8, "https://") == 0) url = url.substr(8);
        auto colonPos = url.find(':');
        if (colonPos != std::string::npos) {
            host = url.substr(0, colonPos);
            try { port = std::stoi(url.substr(colonPos + 1)); } catch (...) {}
        }

        // 2. 先健康检查 — server 可能已经在运行
        std::cout << "[LLM] 检测现有 llama-server..." << std::endl;
        if (HealthCheck(2000)) {
            if (stalePid != 0) {
                // 这是本程序上次拉起但未回收的实例 → 接管所有权，退出时终止以释放显存
                ownedPid_    = stalePid;
                ownProcess_  = true;
                std::cout << "[LLM] 发现本程序遗留的 llama-server (PID: " << stalePid
                          << "), 已接管, 退出时将关闭" << std::endl;
            } else {
                std::cout << "[LLM] 发现外部已运行的 llama-server, 复用 (退出时不会关闭它)"
                          << std::endl;
            }
            ready_ = true;
            return true;
        }

        // 3. 没有 → 自动拉起
        std::cout << "[LLM] 自动启动 llama-server..." << std::endl;
        if (LaunchServer(config.serverExePath, config.modelPath, host, port, config.ngl)) {

            // 4. 等待加载完成
            std::cout << "[LLM] 等待模型加载 (最多 " << config.startupWaitSec << " 秒)..." << std::endl;
            int waitMs = config.startupWaitSec * 1000;
            if (HealthCheck(waitMs)) {
                std::cout << "[LLM] llama-server 就绪!" << std::endl;
                ready_ = true;
                return true;
            }

            std::cerr << "[LLM] 启动超时, 进程可能仍在加载中; 后续请求将重试连接" << std::endl;
            ready_ = true;  // 不阻塞游戏; 请求失败会退回到传统行为树
            return true;    // 进程已拉起, 只是连接未就绪
        }

        std::cerr << "[LLM] 自动拉起失败" << std::endl;
    }

    // 5. 不自动拉起 → 直接尝试连接
    std::cout << "[LLM] 尝试连接 llama-server: " << serverUrl_ << std::endl;
    if (HealthCheck(2000)) {
        std::cout << "[LLM] 连接成功!" << std::endl;
        ready_ = true;
        return true;
    }

    ready_ = false;
    std::cout << "[LLM] 未检测到 llama-server, LLM BOT 功能已禁用" << std::endl;
    return false;  // 连接失败 → game 应继续但 BOT 回退行为树
}

// ---- Shutdown → 线程 + 子进程 ----
void LLMBotController::Shutdown() {
    running_ = false;
    ready_ = false;

    if (workerThread_.joinable()) {
        workerThread_.join();   // 工作线程已设置 WinHTTP 超时，不会无限阻塞
    }

    if (killOnExit_) {
        KillServer();
    }
}

// ---- PID 文件（跨会话回收显存）----
std::string LLMBotController::PidFilePath() {
    return PathUtils::ResolveWritablePath("llm_server.pid");
}

void LLMBotController::WritePidFile(unsigned long pid) {
    std::ofstream f(PidFilePath());
    if (f) f << pid << "\n";
}

unsigned long LLMBotController::ReadPidFile() const {
    std::ifstream f(PidFilePath());
    if (!f) return 0;
    unsigned long pid = 0;
    if (!(f >> pid) || pid == 0) return 0;

    // 校验该进程是否仍是 llama-server（避免 PID 复用误杀）
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    char path[MAX_PATH] = {0};
    DWORD sz = MAX_PATH;
    bool ok = false;
    if (QueryFullProcessImageNameA(h, 0, path, &sz)) {
        std::string p(path);
        std::string lower;
        for (char c : p) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        ok = (lower.find("llama-server") != std::string::npos);
    }
    CloseHandle(h);
    return ok ? pid : 0;
}

void LLMBotController::RemovePidFile() {
    std::error_code ec;
    std::filesystem::remove(PidFilePath(), ec);
}

// ---- 健康检测: GET /health ----
bool LLMBotController::HealthCheck(int timeoutMs) {
    // 解析 URL
    std::string host = "127.0.0.1";
    int port = 9931;
    bool useSSL = false;
    std::string url = serverUrl_;
    if (url.compare(0, 7, "http://") == 0)      { useSSL = false; url = url.substr(7); }
    else if (url.compare(0, 8, "https://") == 0) { useSSL = true;  url = url.substr(8); }
    auto cp = url.find(':');
    if (cp != std::string::npos) {
        host = url.substr(0, cp);
        try { port = std::stoi(url.substr(cp + 1)); } catch (...) {}
    }

    HINTERNET hSession = WinHttpOpen(L"LLM-HealthCheck/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // 超时
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring wh(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wh[0], wlen);

    HINTERNET hConnect = WinHttpConnect(hSession, wh.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", L"/health", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        useSSL ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    BOOL sent = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    BOOL recv = WinHttpReceiveResponse(hReq, nullptr);
    DWORD status = 0;
    DWORD sz = sizeof(status);
    if (recv) WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return (status >= 200 && status < 400);
}

// ---- 启动 llama-server 子进程 ----
bool LLMBotController::LaunchServer(const std::string& exePath, const std::string& modelPath,
                                     const std::string& host, int port, int ngl) {
    // 构造命令行: "llama-server.exe" -m model.gguf -ngl 99 --host 127.0.0.1 --port 9931
    std::ostringstream cmd;
    cmd << "\"" << exePath << "\""
        << " -m \"" << modelPath << "\""
        << " -ngl " << ngl
        << " --host " << host
        << " --port " << port;

    std::string cmdStr = cmd.str();

    // 宽字符转换
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdStr.c_str(), -1, nullptr, 0);
    std::wstring wcmd(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmdStr.c_str(), -1, &wcmd[0], wlen);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // 不弹控制台窗口

    PROCESS_INFORMATION pi = {};

    // 获取 exe 所在目录作为工作目录
    std::string workDir;
    {
        auto slash = exePath.find_last_of("/\\");
        if (slash != std::string::npos) workDir = exePath.substr(0, slash);
    }
    std::wstring wWorkDir;
    if (!workDir.empty()) {
        int dlen = MultiByteToWideChar(CP_UTF8, 0, workDir.c_str(), -1, nullptr, 0);
        wWorkDir.resize(dlen);
        MultiByteToWideChar(CP_UTF8, 0, workDir.c_str(), -1, &wWorkDir[0], dlen);
    }

    BOOL ok = CreateProcessW(
        nullptr,                // lpApplicationName (用命令行)
        wcmd.data(),            // lpCommandLine
        nullptr, nullptr,       // 不继承安全属性
        FALSE,                  // 不继承句柄
        CREATE_NO_WINDOW,       // 无窗口
        nullptr,                // 继承环境变量
        wWorkDir.empty() ? nullptr : wWorkDir.c_str(),
        &si, &pi);

    if (!ok) {
        std::cerr << "[LLM] CreateProcess 失败, 错误码: " << GetLastError() << std::endl;
        return false;
    }

    hServerProcess_ = pi.hProcess;
    hServerThread_  = pi.hThread;
    ownedPid_       = pi.dwProcessId;
    ownProcess_     = true;
    WritePidFile(ownedPid_);   // 记录 PID，保证下次启动可回收（即便本次异常退出）

    std::cout << "[LLM] llama-server 已启动 (PID: " << pi.dwProcessId << ")" << std::endl;
    return true;
}

// ---- 终止子进程（强制 + 校验）----
void LLMBotController::KillServer() {
    if (!ownProcess_) return;

    HANDLE hProc = static_cast<HANDLE>(hServerProcess_);

    // 无句柄但记录了 PID（跨会话接管）→ 用 PID 打开
    bool openedHere = false;
    if (!hProc && ownedPid_ != 0) {
        hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, ownedPid_);
        openedHere = true;
    }
    if (!hProc) {
        std::cerr << "[LLM] 无法获取 llama-server 句柄, 跳过终止" << std::endl;
        ownProcess_ = false;
        ownedPid_ = 0;
        return;
    }

    // 直接强制终止：CTRL_BREAK 需要子进程为独立进程组，
    // 而本程序用 CREATE_NO_WINDOW 启动（无新组），该路径不可靠。
    if (!TerminateProcess(hProc, 0)) {
        const DWORD err = GetLastError();
        if (err != ERROR_ACCESS_DENIED) {
            std::cerr << "[LLM] TerminateProcess 失败, 错误码: " << err << std::endl;
        }
    }

    // 等待并校验确实退出（释放显存需要进程真正结束）
    const DWORD waitRes = WaitForSingleObject(hProc, 5000);
    if (waitRes == WAIT_TIMEOUT) {
        std::cerr << "[LLM] 警告: llama-server 未在 5 秒内退出 (PID "
                  << ownedPid_ << ")，显存可能仍被占用" << std::endl;
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
        std::cerr << "[LLM] 警告: llama-server 仍在运行 (PID " << ownedPid_ << ")" << std::endl;
    } else {
        std::cout << "[LLM] llama-server 已关闭 (PID " << ownedPid_ << ")" << std::endl;
    }

    CloseHandle(hProc);
    if (openedHere) {
        // openedHere 的句柄已在上面关闭
    }
    if (hServerThread_) { CloseHandle(static_cast<HANDLE>(hServerThread_)); }

    hServerProcess_ = nullptr;
    hServerThread_  = nullptr;
    ownedPid_       = 0;
    ownProcess_     = false;
    RemovePidFile();
}

// ---- Decide ----
void LLMBotController::Decide(const std::string& stateJSON, DecisionCallback callback,
                              const std::string& tag) {
    if (!ready_) return;
    if (busy_.load()) {
        std::cerr << "[LLM] 上一次决策仍在进行中, 丢弃本次请求 "
                  << (tag.empty() ? "" : tag.c_str()) << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingBody_ = stateJSON;
        pendingTag_  = tag;
        pendingCallback_ = std::move(callback);
    }

    if (!running_.load()) {
        running_ = true;
        if (workerThread_.joinable()) workerThread_.join();
        workerThread_ = std::thread(&LLMBotController::WorkerLoop, this);
    }
}

void LLMBotController::AskRaw(const std::string& systemPrompt, const std::string& userMessage,
                              int maxTokens, RawCallback callback, const std::string& tag) {
    if (!ready_) {
        if (callback) callback(std::string());
        return;
    }
    if (busy_.load()) {
        std::cerr << "[LLM] 通道占用, 丢弃 " << tag << std::endl;
        if (callback) callback(std::string());   // 立即回报失败，不等超时
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingBody_        = userMessage;      // 原始模式下存 user 消息
        pendingTag_         = tag;
        pendingRaw_         = true;
        pendingSystem_      = systemPrompt;
        pendingMaxTokens_   = maxTokens;
        pendingRawCallback_ = std::move(callback);
        pendingCallback_    = nullptr;
    }
    if (!running_.load()) {
        running_ = true;
        if (workerThread_.joinable()) workerThread_.join();
        workerThread_ = std::thread(&LLMBotController::WorkerLoop, this);
    }
}

void LLMBotController::WorkerLoop() {
    while (running_.load()) {
        std::string body, tag, system;
        int maxTokens = 260;
        bool rawMode = false;
        DecisionCallback cb;
        RawCallback rawCb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingBody_.empty()) { running_ = false; break; }
            body      = std::move(pendingBody_);
            tag       = std::move(pendingTag_);
            rawMode   = pendingRaw_;
            system    = std::move(pendingSystem_);
            maxTokens = pendingMaxTokens_;
            cb        = std::move(pendingCallback_);
            rawCb     = std::move(pendingRawCallback_);
            pendingRaw_ = false;
        }

        busy_ = true;
        std::string raw;
        LLMDecision result;
        if (rawMode) {
            const std::string req =
                PromptBuilder::BuildRequestBodyEx(system, body, maxTokens, 0.6f);
            DoHttpRequest(req, tag, &raw);
        } else {
            result = DoHttpRequest(body, tag);
        }
        busy_ = false;

        if (rawMode) { if (rawCb) rawCb(raw); }
        else         { if (cb) cb(result); }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingBody_.empty()) running_ = false;
        }
    }
}

// ---- DoHttpRequest (WinHTTP POST) ----
LLMDecision LLMBotController::DoHttpRequest(const std::string& requestBody,
                                            const std::string& tag,
                                            std::string* rawOut) {
    LLMDecision result;
    result.valid = false;

    std::string host = "127.0.0.1";
    int port = 9931;
    bool useSSL = false;
    std::string url = serverUrl_;
    if (url.compare(0, 7, "http://") == 0)      { useSSL = false; url = url.substr(7); }
    else if (url.compare(0, 8, "https://") == 0) { useSSL = true;  url = url.substr(8); }
    auto cp = url.find(':');
    if (cp != std::string::npos) { host = url.substr(0, cp); try { port = std::stoi(url.substr(cp + 1)); } catch (...) {} }

    HINTERNET hSession = WinHttpOpen(L"LLMBotController/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    // 设置超时：避免服务端异常时工作线程无限阻塞，从而让 Shutdown() 的 join() 卡死
    WinHttpSetTimeouts(hSession, 3000, 3000, 5000, 10000);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring wh(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wh[0], wlen);

    HINTERNET hConnect = WinHttpConnect(hSession, wh.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", L"/v1/chat/completions", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, useSSL ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    WinHttpAddRequestHeaders(hReq, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)requestBody.c_str(), (DWORD)requestBody.size(), (DWORD)requestBody.size(), 0)) {
        std::cerr << "[LLM] HTTP 发送失败: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return result;
    }

    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        std::cerr << "[LLM] HTTP 接收失败" << std::endl;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return result;
    }

    std::string resp;
    DWORD avail = 0;
    char buf[4096];
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        DWORD toRead = (avail < sizeof(buf)) ? avail : sizeof(buf);
        DWORD rd = 0;
        if (WinHttpReadData(hReq, buf, toRead, &rd)) resp.append(buf, rd);
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    MiniJSON::Value root = MiniJSON::parse(resp);
    if (!root.is_object() || !root.contains("choices")) {
        std::cerr << "[LLM] 响应异常: " << resp.substr(0, 250) << std::endl;
        return result;
    }

    const MiniJSON::Value& choices = root["choices"];
    if (!choices.is_array() || choices.size() == 0) return result;

    std::string content = choices[0]["message"].value<std::string>("content", "");
    if (content.empty()) return result;

    // 教练模式：直接把原文交回调用方
    if (rawOut) {
        *rawOut = content;
        result.valid = true;
        return result;
    }

    auto bo = content.find('{');
    auto bc = content.rfind('}');
    if (bo != std::string::npos && bc != std::string::npos && bc > bo) {
        result = LLMDecision::FromJSON(content.substr(bo, bc - bo + 1));
        std::cout << "[LLM] 决策 " << (tag.empty() ? "-" : tag.c_str())
                  << ": " << result.ToJSON() << std::endl;
    } else {
        std::cerr << "[LLM] content 无有效 JSON: " << content << std::endl;
    }
    return result;
}