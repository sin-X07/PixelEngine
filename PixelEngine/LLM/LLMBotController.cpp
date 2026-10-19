#include "LLMBotController.hpp"
#include "../Core/IniFile.hpp"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cctype>

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
        "你是一个CS2D俯视角射击游戏的BOT。你必须直接输出战术决策，不要分析，不要解释。\n"
        "严格输出JSON格式，不要输出其他任何文字：\n"
        "{\"action\":\"行动\",\"target\":\"目标\",\"shoot\":\"射击模式\",\"reason\":\"简短理由\"}\n"
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
            std::cout << "[LLM] 发现已在运行的 llama-server, 复用" << std::endl;
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
        workerThread_.join();
    }

    KillServer();
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
    ownProcess_ = true;

    std::cout << "[LLM] llama-server 已启动 (PID: " << pi.dwProcessId << ")" << std::endl;
    return true;
}

// ---- 终止子进程 ----
void LLMBotController::KillServer() {
    if (!ownProcess_ || !hServerProcess_) return;

    // 发送关闭信号 (CTRL_BREAK → llama-server 优雅退出)
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetProcessId(hServerProcess_))) {
        // 失败则直接终止
        TerminateProcess(hServerProcess_, 0);
    }

    // 等待最多 3 秒
    WaitForSingleObject(hServerProcess_, 3000);

    CloseHandle(hServerProcess_);
    if (hServerThread_) CloseHandle(hServerThread_);
    hServerProcess_ = nullptr;
    hServerThread_  = nullptr;
    ownProcess_ = false;

    std::cout << "[LLM] llama-server 已关闭" << std::endl;
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

void LLMBotController::WorkerLoop() {
    while (running_.load()) {
        std::string body;
        std::string tag;
        DecisionCallback cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingBody_.empty()) { running_ = false; break; }
            body = std::move(pendingBody_);
            tag  = std::move(pendingTag_);
            cb   = std::move(pendingCallback_);
        }

        busy_ = true;
        LLMDecision result = DoHttpRequest(body, tag);
        busy_ = false;

        if (cb) cb(result);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingBody_.empty()) running_ = false;
        }
    }
}

// ---- DoHttpRequest (WinHTTP POST) ----
LLMDecision LLMBotController::DoHttpRequest(const std::string& requestBody,
                                            const std::string& tag) {
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