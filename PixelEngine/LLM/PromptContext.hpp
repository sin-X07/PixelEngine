#pragma once
#include <string>
#include <vector>

#include "../CS2D/CS2DGame.hpp"
namespace PromptContext {

// 生成【战术态势】文本块（角色/队友/敌情/目标/局势）
std::string BuildTacticalBlock(const CS2DGame& game,
                               const CS2DGame::Player& targetBot,
                               const std::vector<CS2DGame::Player>& bots,
                               int botIdx);

} // namespace PromptContext
