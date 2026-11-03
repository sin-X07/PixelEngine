#pragma once
// ============================================================================
// replay —— 输入日志（journal）录制 / 回放
//
// 只记录"对局内 CS2DGame::Update 边界的输入"，不记录菜单/设置：
//   tick, mouseDown, aimX, aimY, W, A, S, D, Shift, Reload, Interact
//
// 头部（第一行）记录复现所需的全部上下文：map / seed / viewport / 脚本策略 /
// final_hash。回放时按同一 seed 与同一视口重建对局，再用录下来的输入驱动，
// 最后比对 final_hash —— 一致就说明"输入 + 种子"完整决定了这一局。
//
// 这份格式同时用于：脚本玩家（基准）与真人录制（Engine 侧 --record）。
// ============================================================================

#include "../../PixelEngine/CS2D/CS2DGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace dev_replay {

struct InputFrame {
    uint32_t tick = 0;
    bool mouseDown = false;
    float aimX = 0.0f;
    float aimY = 0.0f;
    bool w = false, a = false, s = false, d = false;
    bool shift = false, reload = false, interact = false;
    bool scopeToggle = false;   // 本 tick 是否切换开镜档位
    int  switchSlot = 0;        // 切枪槽位（0..4），0 表示无动作
    int  buyWeapon = 0;      // 购买动作（WeaponId 数值），0 = 无
    int  buyArmorTier = 0;
};

struct JournalHeader {
    std::string map = "dust_2";
    int seed = 1;
    int viewportW = 1728;
    int viewportH = 1080;
    float aimTurnRate = 3.0f;
    float aimErrorDeg = 5.0f;
    int reactionTicks = 15;
    std::string tag;
    uint64_t finalHash = 0;
    bool hasFinalHash = false;
};

inline bool WriteJournal(const std::string& path, const JournalHeader& h,
                         const std::vector<InputFrame>& frames) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f,
                 "# PXINPUT2 map=%s seed=%d viewport=%dx%d aimTurn=%.2f aimErr=%.1f "
                 "reaction=%d tag=%s final_hash=%016llx ticks=%zu\n",
                 h.map.c_str(), h.seed, h.viewportW, h.viewportH,
                 h.aimTurnRate, h.aimErrorDeg, h.reactionTicks, h.tag.c_str(),
                 static_cast<unsigned long long>(h.finalHash), frames.size());
    for (const auto& fr : frames) {
        std::fprintf(f, "%u %d %.9g %.9g %d %d %d %d %d %d %d %d %d %d %d\n",
                     fr.tick, fr.mouseDown ? 1 : 0, fr.aimX, fr.aimY,
                     fr.w ? 1 : 0, fr.a ? 1 : 0, fr.s ? 1 : 0, fr.d ? 1 : 0,
                     fr.shift ? 1 : 0, fr.reload ? 1 : 0, fr.interact ? 1 : 0,
                     fr.scopeToggle ? 1 : 0, fr.switchSlot, fr.buyWeapon, fr.buyArmorTier);
    }
    std::fclose(f);
    return true;
}

inline bool ReadJournal(const std::string& path, JournalHeader& out,
                        std::vector<InputFrame>& frames) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    char line[512];
    if (!std::fgets(line, sizeof(line), f)) { std::fclose(f); return false; }
    // 解析头部 key=value
    {
        std::string s(line);
        size_t pos = 0;
        while (pos < s.size()) {
            const size_t sp = s.find(' ', pos);
            const std::string tok = s.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
            const size_t eq = tok.find('=');
            if (eq != std::string::npos) {
                const std::string k = tok.substr(0, eq);
                const std::string v = tok.substr(eq + 1);
                if (k == "map") out.map = v;
                else if (k == "seed") out.seed = std::atoi(v.c_str());
                else if (k == "aimTurn") out.aimTurnRate = static_cast<float>(std::atof(v.c_str()));
                else if (k == "aimErr") out.aimErrorDeg = static_cast<float>(std::atof(v.c_str()));
                else if (k == "reaction") out.reactionTicks = std::atoi(v.c_str());
                else if (k == "tag") out.tag = v;
                else if (k == "final_hash") {
                    out.finalHash = std::strtoull(v.c_str(), nullptr, 16);
                    out.hasFinalHash = true;
                } else if (k == "viewport") {
                    const size_t x = v.find('x');
                    if (x != std::string::npos) {
                        out.viewportW = std::atoi(v.substr(0, x).c_str());
                        out.viewportH = std::atoi(v.substr(x + 1).c_str());
                    }
                }
            }
            if (sp == std::string::npos) break;
            pos = sp + 1;
        }
    }

    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        InputFrame fr;
        int md = 0, w = 0, a = 0, s = 0, d = 0, sh = 0, rl = 0, it = 0, sc = 0;
        const int n = std::sscanf(line, "%u %d %f %f %d %d %d %d %d %d %d %d %d %d %d",
                                  &fr.tick, &md, &fr.aimX, &fr.aimY,
                                  &w, &a, &s, &d, &sh, &rl, &it,
                                  &sc, &fr.switchSlot, &fr.buyWeapon, &fr.buyArmorTier);
        if (n < 15) continue;
        fr.mouseDown = md != 0;
        fr.w = w != 0; fr.a = a != 0; fr.s = s != 0; fr.d = d != 0;
        fr.shift = sh != 0; fr.reload = rl != 0; fr.interact = it != 0;
        fr.scopeToggle = sc != 0;
        frames.push_back(fr);
    }
    std::fclose(f);
    return !frames.empty();
}

}  // namespace dev_replay
