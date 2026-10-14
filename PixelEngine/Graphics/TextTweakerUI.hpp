#pragma once

class TextConfig;

// 实时文字调参面板（F2 开关，须在 ImGui 帧内调用 Draw）。
class TextTweakerUI {
public:
    // 唯一公开方法：绘制调参面板（直接操作 config 的样式）
    void Draw(TextConfig& config);
};
