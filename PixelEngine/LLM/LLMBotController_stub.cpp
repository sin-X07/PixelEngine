// ============================================================================
// LLMBotController_stub.cpp — 非 Windows 平台的桩实现（macOS / Linux）
//
// 原实现依赖 WinHTTP 与 CreateProcess，只能在 Windows 上使用。
// 在其它平台上：
//   - LLMConfig 返回默认配置；
//   - LLMBotController::Initialize() 返回 false，
//     Engine 会打印「[LLM] 不可用, 教练回退规则引擎」；
//   - AskRaw() 立即回调空字符串，调用方（教练层）走规则回退。
//
// 之后如需在 macOS 上启用 LLM，可把这里的 HTTP 层替换为 libcurl
// （macOS 自带 /usr/lib/libcurl.dylib），接口保持不变。
// ============================================================================

#include "LLMBotController.hpp"

#if !defined(_WIN32)

LLMConfig LLMConfig::FromFile(const std::string& iniPath) {
    (void)iniPath;
    LLMConfig cfg;          // 保留头文件里的默认值
    cfg.autoLaunch = false; // 非 Windows 不自动拉起 llama-server
    return cfg;
}

std::string PromptBuilder::BuildRequestBodyEx(const std::string& systemPrompt,
                                              const std::string& userMessage,
                                              int maxTokens,
                                              float temperature,
                                              const std::string& jsonSchema,
                                              bool cachePrompt,
                                              int idSlot) {
    (void)maxTokens; (void)temperature; (void)jsonSchema;
    (void)cachePrompt; (void)idSlot;
    // 最小可用的 OpenAI 兼容请求体；当前平台不会真正发出去
    std::string body = "{\"messages\":[{\"role\":\"system\",\"content\":\"";
    body += systemPrompt;
    body += "\"},{\"role\":\"user\",\"content\":\"";
    body += userMessage;
    body += "\"}]}";
    return body;
}

LLMBotController::LLMBotController() = default;
LLMBotController::~LLMBotController() = default;

bool LLMBotController::Initialize(const LLMConfig& config) {
    (void)config;
    ready_.store(false);
    return false;
}

void LLMBotController::Shutdown() {
    ready_.store(false);
    running_.store(false);
    busy_.store(false);
}

void LLMBotController::AskRaw(const std::string& systemPrompt,
                              const std::string& userMessage,
                              int maxTokens,
                              RawCallback callback,
                              const std::string& tag,
                              const std::string& jsonSchema) {
    (void)systemPrompt; (void)userMessage; (void)maxTokens;
    (void)tag; (void)jsonSchema;
    if (callback) callback(std::string());
}

#endif // !_WIN32
