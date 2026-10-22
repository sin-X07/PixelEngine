#include "BotDecisionScheduler.hpp"
#include "../CS2D/CS2DGame.hpp"
#include "../Core/PathUtils.hpp"
#include <iostream>
#include <cmath>
#include <cstdio>
#include <algorithm>

// ============================================================================
// 生命周期
// ============================================================================

bool BotDecisionScheduler::Initialize(const std::string& iniPath) {
    LLMConfig cfg = LLMConfig::FromFile(iniPath);

    // 相对路径 → 绝对路径（CreateProcess 需要；基准是 exe 所在目录）
    if (!cfg.serverExePath.empty()) {
        const std::string resolved = PathUtils::ResolveAppPath(cfg.serverExePath);
        if (!resolved.empty()) cfg.serverExePath = resolved;
    }
    if (!cfg.modelPath.empty()) {
        const std::string resolved = PathUtils::ResolveAppPath(cfg.modelPath);
        if (!resolved.empty()) cfg.modelPath = resolved;
    }

    controller_ = std::make_unique<LLMBotController>();
    const bool ok = controller_->Initialize(cfg);

    if (ok) {
        std::cout << "[LLM] 就绪 (server: " << cfg.serverUrl
                  << (cfg.autoLaunch ? ", 自动拉起" : "") << ")" << std::endl;
    } else {
        std::cout << "[LLM] 不可用, BOT 使用传统行为树" << std::endl;
    }
    return ok;
}

void BotDecisionScheduler::Shutdown() {
    if (controller_) {
        controller_->Shutdown();
        controller_.reset();
    }
}

bool BotDecisionScheduler::IsReady() const {
    return controller_ && controller_->IsReady();
}

// ============================================================================
// 调度
// ============================================================================



// 为一个存活 BOT 采集状态并发出异步请求

