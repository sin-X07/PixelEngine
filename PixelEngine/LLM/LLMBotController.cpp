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
    cfg.jsonSchema     = ini.GetBool  ("launcher", "json_schema", cfg.jsonSchema);

    return cfg;
}

// ============================================================================
// PromptBuilder
// ============================================================================





// JSON 字符串转义（请求体用）
static std::string EscapeJSON(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 32);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                else out += c;
        }
    }
    return out;
}

std::string PromptBuilder::BuildRequestBodyEx(const std::string& systemPrompt,
                                              const std::string& userMessage,
                                              int maxTokens, float temperature,
                                              const std::string& jsonSchema,
                                              bool cachePrompt, int idSlot)
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
        << "\"n_predict\":" << maxTokens << ","
        << "\"temperature\":" << temperature << ","
        << "\"cache_prompt\":" << (cachePrompt ? "true" : "false") << ","
        << "\"id_slot\":" << idSlot;

    if (!jsonSchema.empty()) {
        // llama-server 的结构化输出：强制模型按 schema 产出 JSON
        oss << ",\"response_format\":{\"type\":\"json_schema\","
            << "\"json_schema\":{\"name\":\"coach_plan\",\"strict\":true,"
            << "\"schema\":" << jsonSchema << "}}";
    }

    oss << "}";
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
    schemaSupported_ = config.jsonSchema;

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

    // 等待所有工作线程退出（已设置 WinHTTP 超时，不会无限阻塞）
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    activeWorkers_ = 0;

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
    // 教练层要两队并发请求，单槽会互相挤掉：开 2 个并行槽。
    // 上下文总和 4096，拆成 2×2048，足够装下 ~1000 token 的教练 prompt + 200 输出。
    cmd << "\"" << exePath << "\""
        << " -m \"" << modelPath << "\""
        << " -ngl " << ngl
        << " -c 8192"
        << " --parallel 2"
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


void LLMBotController::AskRaw(const std::string& systemPrompt, const std::string& userMessage,
                              int maxTokens, RawCallback callback, const std::string& tag,
                              const std::string& jsonSchema) {
    if (!ready_) {
        if (callback) callback(std::string());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        PendingRequest req;
        req.body      = userMessage;
        req.tag       = tag;
        req.raw       = true;
        req.system    = systemPrompt;
        req.maxTokens = maxTokens;
        req.schema    = jsonSchema;
        req.rawCb     = std::move(callback);
        queue_.push_back(std::move(req));
    }
    if (activeWorkers_.load() == 0) {
        running_ = true;
        for (int w = 0; w < kWorkers; ++w) {
            if (workers_[w].joinable()) workers_[w].join();
        }
        activeWorkers_ = kWorkers;
        for (int w = 0; w < kWorkers; ++w) {
            workers_[w] = std::thread(&LLMBotController::WorkerLoop, this);
        }
    }
}

void LLMBotController::WorkerLoop() {
    while (running_.load()) {
        std::string body, tag, system, schema;
        int maxTokens = 260;
        RawCallback rawCb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) {
                if (--activeWorkers_ <= 0) { activeWorkers_ = 0; running_ = false; }
                break;
            }
            PendingRequest req = std::move(queue_.front());
            queue_.pop_front();
            body      = std::move(req.body);
            tag       = std::move(req.tag);
            system    = std::move(req.system);
            schema    = std::move(req.schema);
            maxTokens = req.maxTokens;
            rawCb     = std::move(req.rawCb);
        }

        busy_ = true;
        std::string raw;
        const bool useSchema = !schema.empty() && schemaSupported_.load();
        std::string req = PromptBuilder::BuildRequestBodyEx(
            system, body, maxTokens, 0.3f, useSchema ? schema : std::string(), true, 0);

        int status = 0;
        bool ok = DoHttpRequest(req, tag, raw, &status);

        // 只有「4xx 客户端错误」才说明真的不支持 schema；
        // 5xx（模型还在加载）或超时不应该永久关掉结构化输出。
        if (!ok && useSchema && status >= 400 && status < 500) {
            if (schemaSupported_.exchange(false)) {
                std::cout << "[LLM] 服务端拒绝 json_schema (HTTP " << status
                          << ")，回退为普通 JSON 输出" << std::endl;
            }
            raw.clear();
            const std::string req2 = PromptBuilder::BuildRequestBodyEx(
                system, body, maxTokens, 0.3f, std::string(), true, 0);
            DoHttpRequest(req2, tag, raw);
        } else if (ok && useSchema && !schemaLogged_.exchange(true)) {
            std::cout << "[LLM] json_schema 结构化输出已生效" << std::endl;
        }
        busy_ = false;

        if (rawCb) rawCb(raw);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) {
                if (--activeWorkers_ <= 0) { activeWorkers_ = 0; running_ = false; }
                break;
            }
        }
    }
}

// ---- DoHttpRequest (WinHTTP POST) ----
bool LLMBotController::DoHttpRequest(const std::string& requestBody,
                                    const std::string& tag,
                                    std::string& rawOut, int* outStatus) {

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
    if (!hSession) return false;

    // 设置超时：避免服务端异常时工作线程无限阻塞，从而让 Shutdown() 的 join() 卡死
    WinHttpSetTimeouts(hSession, 3000, 3000, 5000, 10000);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring wh(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wh[0], wlen);

    HINTERNET hConnect = WinHttpConnect(hSession, wh.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", L"/v1/chat/completions", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, useSSL ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    WinHttpAddRequestHeaders(hReq, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)requestBody.c_str(), (DWORD)requestBody.size(), (DWORD)requestBody.size(), 0)) {
        std::cerr << "[LLM] HTTP 发送失败: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        std::cerr << "[LLM] HTTP 接收失败" << std::endl;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
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

    if (outStatus) {
        DWORD statusCode = 0;
        DWORD sz = sizeof(statusCode);
        if (WinHttpQueryHeaders(hReq,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz,
                WINHTTP_NO_HEADER_INDEX)) {
            *outStatus = static_cast<int>(statusCode);
        }
    }

    MiniJSON::Value root = MiniJSON::parse(resp);
    if (!root.is_object() || !root.contains("choices")) {
        std::cerr << "[LLM] 响应异常: " << resp.substr(0, 250) << std::endl;
        return false;
    }

    const MiniJSON::Value& choices = root["choices"];
    if (!choices.is_array() || choices.size() == 0) return false;

    std::string content = choices[0]["message"].value<std::string>("content", "");
    if (content.empty()) {
        std::cerr << "[LLM] 空回复 (" << (tag.empty() ? "-" : tag.c_str()) << ")" << std::endl;
        return false;
    }

    rawOut = content;      // 教练层自己解析 JSON
    return true;
}