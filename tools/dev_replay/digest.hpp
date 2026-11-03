#pragma once
// ============================================================================
// digest —— 逐 tick 状态哈希（开发期确定性校验用，纯外挂）
//
// 设计原则：
//   * 只通过 CS2DGame 的公开只读 API 取状态，不修改游戏、不新增快照接口；
//   * 字段顺序固定（顺序即哈希输入顺序），字段集合见 SnapshotTick；
//   * 浮点一律 bit-cast（memcpy 到 uint32），禁止 std::to_string / 流式格式化；
//   * 表现层字段（动画计时、贴图、特效种子、日志节流、LLM 文本）一律排除。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace dev_replay {

// FNV-1a 64：零依赖、足够快，用于"发现分歧"而非防碰撞攻击
class TickHasher {
public:
    void Reset() { h_ = kOffset; }

    void MixBytes(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) {
            h_ ^= static_cast<uint64_t>(p[i]);
            h_ *= kPrime;
        }
    }

    template <class T>
    void Mix(const T& v) {
        static_assert(std::is_trivially_copyable<T>::value, "只接受平凡可拷贝类型");
        MixBytes(&v, sizeof(T));
    }

    // 浮点：禁止字符串化；NaN 位模式不稳定，先断言
    void MixFloat(float f) {
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        Mix(bits);
    }

    void MixString(const std::string& s) {
        Mix(static_cast<uint32_t>(s.size()));
        MixBytes(s.data(), s.size());
    }

    uint64_t Value() const { return h_; }

private:
    static constexpr uint64_t kOffset = 1469598103934665603ULL;
    static constexpr uint64_t kPrime  = 1099511628211ULL;
    uint64_t h_ = kOffset;
};

// 计算当前 tick 结束时的状态哈希（字段顺序固定，改动即视为基准变更）
uint64_t SnapshotTick(const CS2DGame& game, uint64_t tick);

}  // namespace dev_replay
