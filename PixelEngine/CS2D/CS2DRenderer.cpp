#include "CS2DRenderer.hpp"
#include "../Graphics/UiScale.hpp"
#include "../Core/PathUtils.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <filesystem>

namespace {
// ---- 程序化烟雾噪声（value noise + fBm），用于生成柔和的烟雾贴图 ----
float SmokeHash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / 16777215.0f;
}

float SmokeSmooth(float t) { return t * t * (3.0f - 2.0f * t); }

float SmokeNoise(float x, float y) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi);
    const float fy = y - static_cast<float>(yi);
    const float a = SmokeHash(xi, yi);
    const float b = SmokeHash(xi + 1, yi);
    const float c = SmokeHash(xi, yi + 1);
    const float d = SmokeHash(xi + 1, yi + 1);
    const float u = SmokeSmooth(fx);
    const float v = SmokeSmooth(fy);
    const float ab = a + (b - a) * u;
    const float cd = c + (d - c) * u;
    return ab + (cd - ab) * v;
}

float SmokeFbm(float x, float y) {
    float total = 0.0f, amp = 0.55f, freq = 1.0f;
    for (int i = 0; i < 4; ++i) {
        total += SmokeNoise(x * freq, y * freq) * amp;
        freq *= 2.07f;
        amp *= 0.5f;
    }
    return total;
}
} // namespace

// ============================================================================
// 初始化 / 清理
// ============================================================================

bool CS2DRenderer::Initialize(SDL_Renderer* ren, const std::string& mapDir, int viewW, int viewH) {
    renderer_ = ren;
    mapDir_   = mapDir;
    viewW_    = viewW;
    viewH_    = viewH;
    zoom_     = 6.0f;   // 镜头缩放（6px 命中盒 → 屏上约 36px）
    const std::string absDir = PathUtils::ResolveAppPath(mapDir);
    if (std::filesystem::exists(absDir)) {
        mapDir_ = absDir;
    }
    std::cout << "[CS2DRenderer] 地图目录: " << mapDir_ << std::endl;

    CreateVisionTexture();
    return true;
}

void CS2DRenderer::CreateVisionTexture() {
    // 先销毁旧纹理，避免重复创建泄漏
    if (visionTex_) {
        SDL_DestroyTexture(visionTex_);
        visionTex_ = nullptr;
    }
    visionPixels_.clear();

    if (!renderer_ || viewW_ <= 0 || viewH_ <= 0) return;

    maskW_ = viewW_ / VISION_MASK_SCALE;
    maskH_ = viewH_ / VISION_MASK_SCALE;
    visionTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STREAMING, maskW_, maskH_);
    if (visionTex_) {
        SDL_SetTextureBlendMode(visionTex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(visionTex_, SDL_ScaleModeLinear);
        visionPixels_.resize(static_cast<size_t>(maskW_) * maskH_ * 4);
    } else {
        std::cerr << "[CS2DRenderer] 视野纹理创建失败: " << SDL_GetError() << std::endl;
    }
}

void CS2DRenderer::SetViewportSize(int w, int h) {
    if (w == viewW_ && h == viewH_) return;
    viewW_ = w;
    viewH_ = h;
    CreateVisionTexture();
}

void CS2DRenderer::Clean() {
    if (backgroundTex_) { SDL_DestroyTexture(backgroundTex_); backgroundTex_ = nullptr; }
    for (auto& tt : tilesetTextures_) {
        if (tt.texture) { SDL_DestroyTexture(tt.texture); tt.texture = nullptr; }
    }
    tilesetTextures_.clear();

    if (ctSheet_.tex)   { SDL_DestroyTexture(ctSheet_.tex);   ctSheet_.tex   = nullptr; }
    if (tSheet_.tex)    { SDL_DestroyTexture(tSheet_.tex);    tSheet_.tex    = nullptr; }
    if (tAltSheet_.tex) { SDL_DestroyTexture(tAltSheet_.tex); tAltSheet_.tex = nullptr; }

    for (auto*& t : weaponSprites_) { if (t) { SDL_DestroyTexture(t); t = nullptr; } }
    for (auto*& t : weaponIcons_)   { if (t) { SDL_DestroyTexture(t); t = nullptr; } }
    weaponSprites_.clear();
    weaponIcons_.clear();

    if (bulletTex_) { SDL_DestroyTexture(bulletTex_); bulletTex_ = nullptr; }
    if (smokeTex_)  { SDL_DestroyTexture(smokeTex_);  smokeTex_  = nullptr; }

    if (visionTex_) { SDL_DestroyTexture(visionTex_); visionTex_ = nullptr; }
    visionPixels_.clear();

    world_ = nullptr;
}

// ============================================================================
// 资源加载
// ============================================================================

void CS2DRenderer::SetWorld(const CS2DWorld* world) {
    Clean();
    world_ = world;
    failedLoads_ = 0;
    if (!world_) return;

    // ---- 加载背景图片 ----
    if (world_->HasBackgroundImage()) {
        const std::string& imgRel = world_->GetBackgroundImagePath();
        std::string imgPath;
        if (imgRel.find(":/") != std::string::npos || (!imgRel.empty() && imgRel[0] == '/')) {
            imgPath = imgRel;
        } else {
            imgPath = mapDir_ + "/" + imgRel;
        }

        // 地图可能引用工程外的图片（例如 ../../prev_assets/x.jpg）：
        // 主路径不存在时直接走 tilesets 回退，避免误报缺失
        if (!std::filesystem::exists(PathUtils::ResolveAppPath(imgPath))) {
            const size_t slash = imgRel.find_last_of("/\\");
            const std::string base = (slash == std::string::npos) ? imgRel
                                                                 : imgRel.substr(slash + 1);
            const std::string alt = mapDir_ + "/../tilesets/" + base;
            if (std::filesystem::exists(PathUtils::ResolveAppPath(alt))) {
                imgPath = alt;
                std::cout << "[CS2DRenderer] 背景图回退到 tilesets/" << base << std::endl;
            }
        }

        backgroundTex_ = LoadTexture(imgPath);
        if (backgroundTex_) {
            std::cout << "[CS2DRenderer] 背景图加载成功: " << imgRel << std::endl;
        }
    }

    // ---- 加载 tileset 贴图 ----
    if (world_->GetMapData()) {
        const auto& data = *world_->GetMapData();
        for (const auto& ts : data.tilesets) {
            if (ts.imagePath.empty()) continue;
            std::string imgPath;
            if (ts.imagePath.find(":/") != std::string::npos ||
                (ts.imagePath.size() >= 2 && ts.imagePath[0] == '/')) {
                imgPath = ts.imagePath;
            } else {
                imgPath = mapDir_ + "/" + ts.imagePath;
            }
            SDL_Texture* tex = LoadTexture(imgPath);
            if (tex) {
                TilesetTexture tt;
                tt.texture  = tex;
                tt.tileW    = ts.tileWidth;
                tt.tileH    = ts.tileHeight;
                tt.columns  = ts.columns > 0 ? ts.columns : 1;
                tt.firstGid = ts.firstGid;
                tilesetTextures_.push_back(tt);
                std::cout << "[CS2DRenderer] 加载 tileset: " << ts.name
                          << " (" << ts.tileWidth << "x" << ts.tileHeight
                          << ", col=" << tt.columns << ")" << std::endl;
            }
        }
    }

    // ---- 加载角色精灵（2列×3行；切片帧按武器槽固定，不循环播放） ----
    ctSheet_.tex     = LoadTexture(mapDir_ + "/../sprites/ct_player.png");
    ctSheet_.frameW  = 32; ctSheet_.frameH = 32; ctSheet_.cols = 2; ctSheet_.rows = 3;

    tSheet_.tex      = LoadTexture(mapDir_ + "/../sprites/t_player.png");
    tSheet_.frameW   = 32; tSheet_.frameH  = 32; tSheet_.cols  = 2; tSheet_.rows  = 3;

    tAltSheet_.tex   = LoadTexture(mapDir_ + "/../sprites/t_player_alt.png");
    tAltSheet_.frameW = 32; tAltSheet_.frameH = 31; tAltSheet_.cols = 2; tAltSheet_.rows = 3;

    // ---- 武器贴图 / 图标（来自 WeaponDB，索引 = WeaponId）----
    const int wcount = static_cast<int>(WeaponId::Count);
    weaponSprites_.assign(wcount, nullptr);
    weaponIcons_.assign(wcount, nullptr);
    for (int i = 1; i < wcount; ++i) {
        const WeaponDef& def = WeaponDB::Get(static_cast<WeaponId>(i));
        if (def.spritePath) {
            weaponSprites_[i] = LoadTexture(std::string("assets/") + def.spritePath);
        }
        if (def.iconPath) {
            weaponIcons_[i] = LoadTexture(std::string("assets/") + def.iconPath);
        }
    }

    bulletTex_ = LoadTexture("assets/cs2d/sprites/bullet.png");
    CreateSmokeTexture();

    BuildRadarTexture();   // 用地图内容烘焙雷达底图

    // 武器素材加载汇总
    int wOk = 0, wFail = 0;
    for (int i = 1; i < wcount; ++i) {
        if (weaponSprites_[i]) ++wOk; else ++wFail;
    }
    std::cout << "[CS2DRenderer] 角色精灵: CT=" << (ctSheet_.tex ? "OK" : "FAIL")
              << " T=" << (tSheet_.tex ? "OK" : "FAIL")
              << " T_alt=" << (tAltSheet_.tex ? "OK" : "FAIL")
              << " | 武器贴图 " << wOk << "/" << (wOk + wFail)
              << " | Bullet=" << (bulletTex_ ? "OK" : "FAIL") << std::endl;

    if (failedLoads_ > 0) {
        std::cerr << "[Asset] 贴图缺失 " << failedLoads_
                  << " 项，请参照 tools/ASSET_SPEC.md 补齐" << std::endl;
    }

    // Clean() 已销毁视野纹理，此处重建（修复视野雾失效 bug）
    CreateVisionTexture();
}

SDL_Texture* CS2DRenderer::LoadTexture(const std::string& filename) {
    const std::string resolved = PathUtils::ResolveAppPath(filename);
    SDL_Texture* tex = IMG_LoadTexture(renderer_, resolved.c_str());
    if (!tex) {
        ++failedLoads_;
        std::cerr << "  [Sprite] 缺失: " << filename << std::endl;
    } else {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    return tex;
}

bool CS2DRenderer::FindTileSrc(int gid, SDL_Texture*& outTex, SDL_Rect& outSrc) const {
    if (gid <= 0 || tilesetTextures_.empty()) return false;
    const TilesetTexture* found = nullptr;
    for (size_t i = tilesetTextures_.size(); i > 0; --i) {
        if (tilesetTextures_[i - 1].firstGid <= gid) {
            found = &tilesetTextures_[i - 1];
            break;
        }
    }
    if (!found || !found->texture) return false;
    const int localId = gid - found->firstGid;
    const int col = localId % found->columns;
    const int row = localId / found->columns;
    outSrc.x = col * found->tileW;
    outSrc.y = row * found->tileH;
    outSrc.w = found->tileW;
    outSrc.h = found->tileH;
    outTex = found->texture;
    return true;
}

// ============================================================================
// 相机
// ============================================================================

void CS2DRenderer::UpdateCamera(float playerCenterX, float playerCenterY,
                                int viewportW, int viewportH, float zoomMultiplier) {
    if (!world_) return;

    // 视野是半径 VISION_RADIUS 的圆：把镜头缩放到「整圆刚好装进较窄的那一屏边」
    const float minView = static_cast<float>(std::min(viewportW, viewportH));
    const float fitZoom = minView / (2.0f * VISION_RADIUS);
    const float baseZoom = std::clamp(fitZoom, 1.2f, 6.0f);

    // 开镜只乘一次倍率：zoom_ 就是实际倍率，GetZoom()/UpdateCursor 不得再乘
    zoomMultiplier = std::clamp(zoomMultiplier, 0.25f, 4.0f);
    zoom_ = baseZoom * zoomMultiplier;

    const float viewW = static_cast<float>(viewportW) / zoom_;
    const float viewH = static_cast<float>(viewportH) / zoom_;

    // 玩家视野：正前方要能看到屏幕边缘，但必须有上限，避免 BOT 在屏幕外开火。
    // 加一个软边宽度，让屏幕四角落在"完全点亮"区内，而不是刚好卡在雾边。
    // 关键：按 baseZoom 计算（不是开镜后的 zoom_），否则开镜会缩小敌人知情半径。
    const float baseViewW = static_cast<float>(viewportW) / baseZoom;
    const float baseViewH = static_cast<float>(viewportH) / baseZoom;
    const float halfW = baseViewW * 0.5f;
    const float halfH = baseViewH * 0.5f;
    const float screenHalfDiag = std::sqrt(halfW * halfW + halfH * halfH);
    visionRadius_ = std::clamp(screenHalfDiag + VISION_SOFT_EDGE, VISION_RADIUS,
                               MAX_PLAYER_VISION_RADIUS);

    float totalW = world_->GetMapPixelW();
    float totalH = world_->GetMapPixelH();
    if (world_->HasBackgroundImage()) {
        totalW = std::max(totalW, world_->GetImageW());
        totalH = std::max(totalH, world_->GetImageH());
    }

    // 允许镜头越界至多一个视野半径：这样即便贴着地图边缘，
    // 玩家仍在屏幕中心附近，整圆也不会被屏幕裁掉
    const float loX = -VISION_RADIUS;
    const float hiX = std::max(loX, totalW - viewW + VISION_RADIUS);
    const float loY = -VISION_RADIUS;
    const float hiY = std::max(loY, totalH - viewH + VISION_RADIUS);

    cameraX_ = std::clamp(playerCenterX - viewW * 0.5f, loX, hiX);
    cameraY_ = std::clamp(playerCenterY - viewH * 0.5f, loY, hiY);
}

// ============================================================================
// 背景图 & tile 层
// ============================================================================

void CS2DRenderer::SetPreviewCamera(float centerX, float centerY, float zoom,
                                    bool clampToMap) {
    if (!world_) return;
    zoom_ = std::clamp(zoom, 0.05f, 10.0f);
    const float viewW = static_cast<float>(viewW_) / zoom_;
    const float viewH = static_cast<float>(viewH_) / zoom_;

    float totalW = world_->GetMapPixelW();
    float totalH = world_->GetMapPixelH();
    if (world_->HasBackgroundImage()) {
        totalW = std::max(totalW, world_->GetImageW());
        totalH = std::max(totalH, world_->GetImageH());
    }

    cameraX_ = centerX - viewW * 0.5f;
    cameraY_ = centerY - viewH * 0.5f;
    if (clampToMap) {
        if (viewW < totalW) cameraX_ = std::clamp(cameraX_, 0.0f, totalW - viewW);
        else cameraX_ = (totalW - viewW) * 0.5f;
        if (viewH < totalH) cameraY_ = std::clamp(cameraY_, 0.0f, totalH - viewH);
        else cameraY_ = (totalH - viewH) * 0.5f;
    }
}

void CS2DRenderer::DrawImageBackground() {
    if (!backgroundTex_ || !world_) return;
    const float viewW = world_->GetImageW();
    const float viewH = world_->GetImageH();
    SDL_Rect dst;
    dst.x = static_cast<int>(std::lround(-cameraX_ * zoom_));
    dst.y = static_cast<int>(std::lround(-cameraY_ * zoom_));
    dst.w = static_cast<int>(std::lround(viewW * zoom_));
    dst.h = static_cast<int>(std::lround(viewH * zoom_));
    SDL_RenderCopy(renderer_, backgroundTex_, nullptr, &dst);
}

void CS2DRenderer::DrawTileLayer(const std::vector<int>& data,
                                 int layerWidth, int layerHeight) {
    if (!world_ || data.empty()) return;
    const int tileW = world_->GetTileWidth();
    const int tileH = world_->GetTileHeight();
    const int viewW = static_cast<int>(std::ceil(static_cast<float>(viewW_) / zoom_));
    const int viewH = static_cast<int>(std::ceil(static_cast<float>(viewH_) / zoom_));
    const int colMin = std::max(0, static_cast<int>(std::floor(cameraX_ / tileW)) - 1);
    const int colMax = std::min(layerWidth  - 1, static_cast<int>(std::ceil((cameraX_ + viewW) / tileW)) + 1);
    const int rowMin = std::max(0, static_cast<int>(std::floor(cameraY_ / tileH)) - 1);
    const int rowMax = std::min(layerHeight - 1, static_cast<int>(std::ceil((cameraY_ + viewH) / tileH)) + 1);

    for (int row = rowMin; row <= rowMax; ++row) {
        for (int col = colMin; col <= colMax; ++col) {
            const int idx = row * layerWidth + col;
            if (idx < 0 || idx >= static_cast<int>(data.size())) continue;
            const int gid = data[idx];
            if (gid <= 0) continue;

            const float screenX = (col * tileW - cameraX_) * zoom_;
            const float screenY = (row * tileH - cameraY_) * zoom_;
            const float drawSizeW = tileW * zoom_;
            const float drawSizeH = tileH * zoom_;

            SDL_Texture* tex = nullptr;
            SDL_Rect src;
            if (!FindTileSrc(gid, tex, src)) {
                SDL_Rect fallbackDst = {
                    static_cast<int>(std::lround(screenX)),
                    static_cast<int>(std::lround(screenY)),
                    static_cast<int>(std::lround(drawSizeW)),
                    static_cast<int>(std::lround(drawSizeH))
                };
                DrawTileFallback(gid, fallbackDst);
                continue;
            }
            SDL_Rect dst = {
                static_cast<int>(std::lround(screenX)),
                static_cast<int>(std::lround(screenY)),
                static_cast<int>(std::lround(drawSizeW)),
                static_cast<int>(std::lround(drawSizeH))
            };
            SDL_RenderCopy(renderer_, tex, &src, &dst);
        }
    }
}

void CS2DRenderer::DrawWorld() {
    if (!world_) return;
    DrawImageBackground();
    const auto& names = world_->GetVisibleLayerNames();
    const auto& datas = world_->GetVisibleLayerData();
    for (size_t i = 0; i < names.size(); ++i) {
        std::string lower;
        lower.reserve(names[i].size());
        for (char c : names[i]) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "overlay") continue;
        DrawTileLayer(datas[i], world_->GetMapWidth(), world_->GetMapHeight());
    }
}

void CS2DRenderer::DrawOverlay() {
    if (!world_) return;
    const auto& names = world_->GetVisibleLayerNames();
    const auto& datas = world_->GetVisibleLayerData();
    for (size_t i = 0; i < names.size(); ++i) {
        std::string lower;
        lower.reserve(names[i].size());
        for (char c : names[i]) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower != "overlay") continue;
        DrawTileLayer(datas[i], world_->GetMapWidth(), world_->GetMapHeight());
        break;
    }
}

// ============================================================================
// 角色精灵绘制（固定切片：按武器槽选取固定帧，不循环播放）
//   主武器 → 第1行第1列；副武器 → 第2行第2列；近战 → 第2行第1列
// ============================================================================

static constexpr float WALK_BOB_AMPLITUDE   = 0.6f;   // 世界单位（×zoom 后约 2.4px）

void CS2DRenderer::DrawCharacter(const SpriteDrawParams& p) {
    // 选序列帧 sheet
    const SpriteSheetDef* sheet = nullptr;
    if (p.spriteKey == 0)      sheet = &ctSheet_;
    else if (p.spriteKey == 1) sheet = &tSheet_;
    else                       sheet = &tAltSheet_;

    if (!sheet || !sheet->tex) return;

    // ---- 按武器槽固定切片（死亡用主武器帧 + 倾斜淡出） ----
    int frameCol = 0, frameRow = 0;   // 默认：第1行第1列（主武器）
    if (p.animState != 3) {           // 非死亡状态才按槽位切帧
        switch (p.weaponSlot) {
            case 1:  frameCol = 1; frameRow = 1; break;  // 副武器：第2行第2列
            case 2:  frameCol = 0; frameRow = 1; break;  // 近战：  第2行第1列
            default: frameCol = 0; frameRow = 0; break;  // 主武器：第1行第1列
        }
    }

    // ---- srcRect：从整张图中"取景框"切出固定帧 ----
    SDL_Rect src;
    src.x = frameCol * sheet->frameW;
    src.y = frameRow * sheet->frameH;
    src.w = sheet->frameW;
    src.h = sheet->frameH;

    // ---- 屏幕坐标：视觉盒严格等于命中盒 p.size ----
    const float cx = (p.x - cameraX_) * zoom_;
    const float cy = (p.y - cameraY_) * zoom_;
    const float sz = p.size * zoom_;
    Uint8 alpha = p.alpha;

    // 死亡动画：倾斜 + 淡出
    float tiltDeg = 0.0f;
    if (p.animState == 3) {
        tiltDeg = -90.0f;
        const float fadeFactor = std::max(0.0f, 1.0f - p.deathTimer / 2.0f);
        alpha = static_cast<Uint8>(alpha * fadeFactor);
        if (alpha < 5) return;
    }

    // 行走 bobbing（世界单位，随缩放）
    float yOff = 0.0f;
    if (p.animState == 1) {
        yOff = std::sin(p.animTimer * 12.0f) * WALK_BOB_AMPLITUDE * zoom_;
    }

    // 射击后坐力（世界单位）
    float xOff = 0.0f;
    if (p.animState == 2) {
        xOff = -std::max(0.0f, 1.0f - p.animTimer * 6.0f) * 0.8f * zoom_;
    }

    SDL_FRect dst;
    dst.x = cx - sz * 0.5f + xOff;
    dst.y = cy - sz * 0.5f + yOff;
    dst.w = sz;
    dst.h = sz;

    SDL_SetTextureAlphaMod(sheet->tex, alpha);
    SDL_FPoint center = { sz * 0.5f, sz * 0.5f };
    // 精灵默认面朝上方，需 +90° 使人物朝向与武器朝向一致
    const float charAngle = p.aimAngle * 180.0f / 3.14159265f + tiltDeg
                            + (p.animState == 3 ? 0.0f : 90.0f);
    SDL_RenderCopyExF(renderer_, sheet->tex, &src, &dst,
                      charAngle, &center, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(sheet->tex, 255);

    // 武器
    if (p.animState != 3) {
        DrawWeaponForCharacter(cx + xOff, cy + yOff, p.size, p.aimAngle,
                               p.weaponSlot, p.weaponId, p.animState == 2);
    }
}

// 枪口火焰：cx/cy 为武器中心的屏幕坐标
void CS2DRenderer::DrawMuzzleFlash(float cx, float cy, float size, float aimAngle,
                                   int weaponSlot) {
    const float weaponSz = (weaponSlot == 1) ? 3.0f
                         : (weaponSlot == 2) ? 4.5f
                         : 4.0f;
    // 枪口位置 = 角色中心 + 瞄准方向 * (武器偏移 + 武器长度*/
    const float tip = size * 0.35f + weaponSz * 0.55f;
    const float mx = cx + std::cos(aimAngle) * tip * zoom_;
    const float my = cy + std::sin(aimAngle) * tip * zoom_;

    const float r = 2.6f * zoom_;

    // 外层：橙色柔光
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 255, 170, 60, 130);
    for (int i = 3; i >= 1; --i) {
        const float rr = r * static_cast<float>(i) / 3.0f;
        const int span = static_cast<int>(rr);
        for (int dy = -span; dy <= span; ++dy) {
            const float half = std::sqrt(std::max(0.0f, rr * rr - static_cast<float>(dy * dy)));
            SDL_RenderDrawLineF(renderer_, mx - half, my + dy, mx + half, my + dy);
        }
    }

    // 内层：白热点
    SDL_SetRenderDrawColor(renderer_, 255, 245, 210, 235);
    const int span = static_cast<int>(r * 0.5f);
    for (int dy = -span; dy <= span; ++dy) {
        const float half = std::sqrt(std::max(0.0f, (r * 0.5f) * (r * 0.5f) - static_cast<float>(dy * dy)));
        SDL_RenderDrawLineF(renderer_, mx - half, my + dy, mx + half, my + dy);
    }
}

void CS2DRenderer::DrawWeaponForCharacter(float cx, float cy, float size, float aimAngle,
                                          int weaponSlot, int weaponId, bool muzzleFlash) {
    if (weaponId <= 0 || weaponId >= static_cast<int>(weaponSprites_.size())) return;
    SDL_Texture* tex = weaponSprites_[weaponId];
    if (!tex) return;

    const WeaponDef& def = WeaponDB::Get(static_cast<WeaponId>(weaponId));
    const float degOffset = def.degOffset;
    const float weaponSz  = (weaponSlot == 1) ? 3.0f
                          : (weaponSlot == 2) ? 4.5f
                          : (weaponSlot == 3) ? 2.2f   // 投掷物手持：缩小
                          : 4.0f;

    // 武器绘制在精灵前方偏移
    const float offset = size * 0.35f * zoom_;
    const float wx = cx + std::cos(aimAngle) * offset;
    const float wy = cy + std::sin(aimAngle) * offset;
    const float drawSz = weaponSz * zoom_;

    SDL_FRect wdst;
    wdst.x = wx - drawSz * 0.5f;
    wdst.y = wy - drawSz * 0.5f;
    wdst.w = drawSz;
    wdst.h = drawSz;

    SDL_FPoint wcenter = { drawSz * 0.5f, drawSz * 0.5f };
    SDL_RenderCopyExF(renderer_, tex, nullptr, &wdst,
                      aimAngle * 180.0f / 3.14159265f + degOffset,
                      &wcenter, SDL_FLIP_NONE);

    // 开枪瞬间的枪口火焰（近战/投掷物不画）
    if (muzzleFlash && def.cls != WeaponClass::Melee && def.cls != WeaponClass::Grenade) {
        DrawMuzzleFlash(wx, wy, size, aimAngle, weaponSlot);   // wx/wy 已是屏幕坐标
    }
}

// ============================================================================
// 雷达底图：一次性把地图背景（无背景则用碰撞网格）烘焙成纹理
// ============================================================================

void CS2DRenderer::BuildRadarTexture() {
    if (!renderer_ || !world_) return;

    if (radarMapTex_) { SDL_DestroyTexture(radarMapTex_); radarMapTex_ = nullptr; }

    // 底图取 512x512，按地图宽高比内接（letterbox）
    const int tex = 512;
    radarMapW_ = tex;
    radarMapH_ = tex;

    SDL_Texture* target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, tex, tex);
    if (!target) return;

    SDL_SetRenderTarget(renderer_, target);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 14, 18, 22, 255);
    SDL_RenderClear(renderer_);

    const float mapW = world_->GetMapPixelW();
    const float mapH = world_->GetMapPixelH();
    float iw = static_cast<float>(tex), ih = static_cast<float>(tex);
    if (mapW > 0.0f && mapH > 0.0f) {
        const float sFit = std::min(static_cast<float>(tex) / mapW,
                                    static_cast<float>(tex) / mapH);
        iw = mapW * sFit;
        ih = mapH * sFit;
    }
    radarImgX_ = (tex - iw) * 0.5f;
    radarImgY_ = (tex - ih) * 0.5f;
    radarImgW_ = iw;
    radarImgH_ = ih;

    if (backgroundTex_) {
        // 有背景图：直接缩放填进内接矩形
        SDL_SetTextureBlendMode(backgroundTex_, SDL_BLENDMODE_BLEND);
        SDL_Rect dst = { static_cast<int>(radarImgX_), static_cast<int>(radarImgY_),
                         static_cast<int>(iw), static_cast<int>(ih) };
        SDL_RenderCopy(renderer_, backgroundTex_, nullptr, &dst);
    } else {
        // 无背景图：用碰撞网格烘一层地形（只做一次，不再每帧采样）
        const auto& grid = world_->GetCollisionGrid();
        const int gw = world_->GetMapWidth();
        const int gh = world_->GetMapHeight();
        if (!grid.empty() && gw > 0 && gh > 0) {
            const float pxPerCellX = iw / static_cast<float>(gw);
            const float pxPerCellY = ih / static_cast<float>(gh);
            for (int gy = 0; gy < gh; ++gy) {
                for (int gx = 0; gx < gw; ++gx) {
                    const auto cell = grid[gy * gw + gx];
                    Uint8 cr = 34, cg = 40, cb = 34;
                    if (cell == CS2DWorld::CellType::Solid)           { cr = 78; cg = 68; cb = 60; }
                    else if (cell == CS2DWorld::CellType::Penetrable) { cr = 56; cg = 62; cb = 56; }
                    else if (cell == CS2DWorld::CellType::Damage)     { cr = 96; cg = 38; cb = 26; }
                    SDL_SetRenderDrawColor(renderer_, cr, cg, cb, 255);
                    SDL_Rect r;
                    r.x = static_cast<int>(radarImgX_ + gx * pxPerCellX);
                    r.y = static_cast<int>(radarImgY_ + gy * pxPerCellY);
                    r.w = std::max(1, static_cast<int>(std::ceil(pxPerCellX)));
                    r.h = std::max(1, static_cast<int>(std::ceil(pxPerCellY)));
                    SDL_RenderFillRect(renderer_, &r);
                }
            }
        }
    }

    // 统一压暗，保证上面的圆点清晰
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 90);
    SDL_Rect all = { 0, 0, tex, tex };
    SDL_RenderFillRect(renderer_, &all);

    SDL_SetRenderTarget(renderer_, nullptr);
    radarMapTex_ = target;

    std::cout << "[CS2DRenderer] 雷达底图已烘焙 " << tex << "x" << tex
              << " 图像区 " << static_cast<int>(iw) << "x" << static_cast<int>(ih)
              << (backgroundTex_ ? "（来自地图背景）" : "（来自碰撞网格）") << std::endl;
}

// ============================================================================
// 世界坐标圆
// ============================================================================

void CS2DRenderer::DrawCircleOutline(float worldX, float worldY, float radius,
                                     Uint8 r, Uint8 g, Uint8 b, Uint8 a, int segments) {
    if (!renderer_ || segments < 3) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);

    const float cx = (worldX - cameraX_) * zoom_;
    const float cy = (worldY - cameraY_) * zoom_;
    const float rr = radius * zoom_;

    float px = cx + rr, py = cy;
    for (int i = 1; i <= segments; ++i) {
        const float ang = 6.28318531f * static_cast<float>(i) / static_cast<float>(segments);
        const float nx = cx + std::cos(ang) * rr;
        const float ny = cy + std::sin(ang) * rr;
        SDL_RenderDrawLineF(renderer_, px, py, nx, ny);
        px = nx; py = ny;
    }
}

void CS2DRenderer::DrawCircleFilled(float worldX, float worldY, float radius,
                                    Uint8 r, Uint8 g, Uint8 b, Uint8 a, int segments) {
    if (!renderer_ || segments < 3) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);

    const float cx = (worldX - cameraX_) * zoom_;
    const float cy = (worldY - cameraY_) * zoom_;
    const float rr = radius * zoom_;

    // 三角形扇
    float px = cx + rr, py = cy;
    for (int i = 1; i <= segments; ++i) {
        const float ang = 6.28318531f * static_cast<float>(i) / static_cast<float>(segments);
        const float nx = cx + std::cos(ang) * rr;
        const float ny = cy + std::sin(ang) * rr;
        SDL_Vertex v[3];
        v[0].position = { cx, cy };
        v[0].color = { r, g, b, a };
        v[0].tex_coord = { 0, 0 };
        v[1].position = { px, py };
        v[1].color = { r, g, b, a };
        v[1].tex_coord = { 0, 0 };
        v[2].position = { nx, ny };
        v[2].color = { r, g, b, a };
        v[2].tex_coord = { 0, 0 };
        SDL_RenderGeometry(renderer_, nullptr, v, 3, nullptr, 0);
        px = nx; py = ny;
    }
}

// ============================================================================
// 掉落武器（用 _d 图标，与 HUD 状态栏同一套素材）
// ============================================================================

void CS2DRenderer::DrawDroppedWeapon(int weaponId, float worldX, float worldY,
                                     float angleDeg) {
    if (!renderer_ || weaponId <= 0) return;

    // 掉落物属于「状态栏那一类素材」：优先 _d 图标
    SDL_Texture* tex = nullptr;
    if (weaponId < static_cast<int>(weaponIcons_.size())) tex = weaponIcons_[weaponId];
    if (!tex && weaponId < static_cast<int>(weaponSprites_.size())) {
        tex = weaponSprites_[weaponId];
    }
    if (!tex) return;

    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    // 统一放进约 9 个世界单位的方框，保持宽高比
    const float box = 5.5f * zoom_;
    float dw = box, dh = box;
    if (tw >= th) dh = box * static_cast<float>(th) / static_cast<float>(tw);
    else          dw = box * static_cast<float>(tw) / static_cast<float>(th);

    // 平铺在地面：不做上下浮动
    SDL_FRect dst;
    dst.x = (worldX - cameraX_) * zoom_ - dw * 0.5f;
    dst.y = (worldY - cameraY_) * zoom_ - dh * 0.5f;
    dst.w = dw;
    dst.h = dh;

    // 地面投影，避免看着像浮在空中
    const SDL_FPoint rotCenter = { dst.w * 0.5f, dst.h * 0.5f };
    SDL_RenderCopyExF(renderer_, tex, nullptr, &dst,
                      static_cast<double>(angleDeg), &rotCenter, SDL_FLIP_NONE);

}
// ============================================================================
// 目标物标记（包点 / C4）
// ============================================================================

void CS2DRenderer::DrawObjectiveMarkers(float timeSec) {
    if (!world_ || !renderer_) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // 包点：琥珀色区域 + 呼吸边线（一个包点可能由多个矩形组成）
    const auto& sites = world_->GetBombSites();
    const float pulse = 0.5f + 0.5f * std::sin(timeSec * 2.2f);
    for (const auto& site : sites) {
        for (const auto& s : site.rects) {
            SDL_FRect r;
            r.x = (s.x - cameraX_) * zoom_;
            r.y = (s.y - cameraY_) * zoom_;
            r.w = s.width * zoom_;
            r.h = s.height * zoom_;

            SDL_SetRenderDrawColor(renderer_, 240, 190, 60, 34);
            SDL_RenderFillRectF(renderer_, &r);

            SDL_SetRenderDrawColor(renderer_, 250, 205, 80,
                                   static_cast<Uint8>(110 + 70 * pulse));
            SDL_RenderDrawRectF(renderer_, &r);

            // 四角加粗，强调边界
            const float arm = std::min(r.w, r.h) * 0.22f;
            SDL_SetRenderDrawColor(renderer_, 255, 225, 120, 200);
            for (int i = 0; i < 4; ++i) {
                const float cx = (i % 2 == 0) ? r.x : r.x + r.w;
                const float cy = (i / 2 == 0) ? r.y : r.y + r.h;
                const float dx = (i % 2 == 0) ? arm : -arm;
                const float dy = (i / 2 == 0) ? arm : -arm;
                SDL_RenderDrawLineF(renderer_, cx, cy, cx + dx, cy);
                SDL_RenderDrawLineF(renderer_, cx, cy, cx, cy + dy);
            }
        }
    }
}



void CS2DRenderer::DrawBullet(float x, float y, float vx, float vy) {
    if (!bulletTex_) return;

    const float cx = (x - cameraX_) * zoom_;
    const float cy = (y - cameraY_) * zoom_;
    const float s = 1.0f * zoom_;   

    SDL_Rect dst = {
        static_cast<int>(std::lround(cx - s * 0.5f)),
        static_cast<int>(std::lround(cy - s * 0.5f)),
        static_cast<int>(std::lround(s)),
        static_cast<int>(std::lround(s))
    };
    // bullet.png 竖直朝上，+90° 使其沿速度方向对齐
    const double angle = std::atan2(vy, vx) * 180.0 / 3.14159265358979323846 + 90.0;
    SDL_RenderCopyEx(renderer_, bulletTex_, nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
}

// ---------------------------------------------------------------------------
// 枪弹视觉段（契约 v1 第 6 节）
// 最小占位实现：按 remaining/duration 淡出、逻辑像素宽度 1.5~3、长度上限 24px，
// 并按地图阻挡瓦片裁剪。表现细节归 UI 方（PIX-17）替换，这里保证可运行且不丢弹道。
// ---------------------------------------------------------------------------
void CS2DRenderer::DrawBulletVisuals(
    const std::vector<Presentation::BulletVisualSegment>& segments) {
    if (!renderer_ || segments.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const auto& s : segments) {
        const float life = (s.durationSeconds > 0.0f)
                               ? std::clamp(s.remainingSeconds / s.durationSeconds, 0.0f, 1.0f)
                               : 0.0f;
        if (life <= 0.0f) continue;

        // 从起点向终点推进，遇到阻挡瓦片就截断（不宣称是精确命中点）
        float toX = s.toX, toY = s.toY;
        if (world_) {
            const float dx = s.toX - s.fromX;
            const float dy = s.toY - s.fromY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const int steps = std::max(1, static_cast<int>(dist / 2.0f));
            for (int i = 1; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const float px = s.fromX + dx * t;
                const float py = s.fromY + dy * t;
                int row = 0, col = 0;
                world_->WorldToTile(px, py, row, col);
                if (world_->IsBlockingBullet(row, col)) {
                    const float tb = static_cast<float>(i - 1) / static_cast<float>(steps);
                    toX = s.fromX + dx * tb;
                    toY = s.fromY + dy * tb;
                    break;
                }
            }
        }

        // 屏幕坐标 + 长度上限（逻辑像素 24）
        float x0 = (s.fromX - cameraX_) * zoom_;
        float y0 = (s.fromY - cameraY_) * zoom_;
        float x1 = (toX - cameraX_) * zoom_;
        float y1 = (toY - cameraY_) * zoom_;
        const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        constexpr float kMaxLen = 24.0f;
        if (len > kMaxLen && len > 0.0f) {
            const float k = kMaxLen / len;
            x0 = x1 - (x1 - x0) * k;
            y0 = y1 - (y1 - y0) * k;
        }

        const Uint8 alpha = static_cast<Uint8>(235.0f * life);
        // 枪弹用暖白；命中段略亮，射程到期段略暗
        SDL_SetRenderDrawColor(renderer_, 255,
                               s.impact ? 232 : 220,
                               s.impact ? 180 : 160,
                               alpha);
        // SDL 没有画线粗细：按 1.5~3 逻辑像素画 1~3 条平行线
        const int w = 1 + static_cast<int>(life * 2.0f);
        const float dx = x1 - x0, dy = y1 - y0;
        const float len2 = std::sqrt(dx * dx + dy * dy);
        const float nx = (len2 > 0.001f) ? -dy / len2 : 0.0f;
        const float ny = (len2 > 0.001f) ? dx / len2 : 1.0f;
        for (int i = 0; i < w; ++i) {
            const float off = static_cast<float>(i) - (static_cast<float>(w) - 1.0f) * 0.5f;
            SDL_RenderDrawLineF(renderer_,
                                x0 + nx * off, y0 + ny * off,
                                x1 + nx * off, y1 + ny * off);
        }
    }
}

// ---------------------------------------------------------------------------
// 开镜覆盖层（契约 v1 第 4 节）
// 最小占位实现：按 transition 收拢的暗角 + 镜筒圆环 + 中心准星。
// 只使用已有 logicalW/H 坐标，不做命中或相机计算；表现归 UI 方（PIX-10）替换。
// ---------------------------------------------------------------------------
void CS2DRenderer::DrawScopeOverlay(const Presentation::ScopeView& scope,
                                    int viewW, int viewH) {
    if (!renderer_) return;
    const float t = std::clamp(scope.transition, 0.0f, 1.0f);
    if (t <= 0.001f || viewW <= 0 || viewH <= 0) return;

    const float cx = static_cast<float>(viewW) * 0.5f;
    const float cy = static_cast<float>(viewH) * 0.5f;
    const float minDim = static_cast<float>(std::min(viewW, viewH));
    // 窗口从整屏收拢到 0.84 * 短边，过渡完成即是"镜筒"
    const float half = minDim * (0.5f - 0.08f * t);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 6, 8, static_cast<Uint8>(215.0f * t));
    const SDL_FRect top    = { 0.0f, 0.0f, static_cast<float>(viewW), cy - half };
    const SDL_FRect bottom = { 0.0f, cy + half, static_cast<float>(viewW), cy - half };
    const SDL_FRect left   = { 0.0f, cy - half, cx - half, half * 2.0f };
    const SDL_FRect right  = { cx + half, cy - half, cx - half, half * 2.0f };
    SDL_RenderFillRectF(renderer_, &top);
    SDL_RenderFillRectF(renderer_, &bottom);
    SDL_RenderFillRectF(renderer_, &left);
    SDL_RenderFillRectF(renderer_, &right);

    // 镜筒边缘：一圈深色 + 一圈细亮线
    SDL_SetRenderDrawColor(renderer_, 12, 16, 20, static_cast<Uint8>(200.0f * t));
    for (int i = 0; i < 6; ++i) {
        const float rr = half - static_cast<float>(i);
        SDL_FRect ring = { cx - rr, cy - rr, rr * 2.0f, rr * 2.0f };
        SDL_RenderDrawRectF(renderer_, &ring);
    }
    SDL_SetRenderDrawColor(renderer_, 210, 225, 235, static_cast<Uint8>(110.0f * t));
    SDL_FRect ring = { cx - half, cy - half, half * 2.0f, half * 2.0f };
    SDL_RenderDrawRectF(renderer_, &ring);

    // 中心准星：小圆 + 十字，幅度随 t 收敛
    const float dot = 2.0f;
    SDL_SetRenderDrawColor(renderer_, 235, 245, 250, static_cast<Uint8>(220.0f * t));
    SDL_FRect center = { cx - dot, cy - dot, dot * 2.0f, dot * 2.0f };
    SDL_RenderFillRectF(renderer_, &center);
    const float tick = 10.0f + 6.0f * t;
    SDL_RenderDrawLineF(renderer_, cx - tick, cy, cx - dot * 2.0f, cy);
    SDL_RenderDrawLineF(renderer_, cx + dot * 2.0f, cy, cx + tick, cy);
    SDL_RenderDrawLineF(renderer_, cx, cy - tick, cx, cy - dot * 2.0f);
    SDL_RenderDrawLineF(renderer_, cx, cy + dot * 2.0f, cx, cy + tick);
}

void CS2DRenderer::DrawThrownGrenade(int weaponId, float worldX, float worldY,
                                  float spinDeg) {
    if (!renderer_) return;
    SDL_Texture* tex = nullptr;
    if (weaponId > 0 && weaponId < static_cast<int>(weaponSprites_.size())) {
        tex = weaponSprites_[weaponId];
    }
    if (!tex) tex = bulletTex_;
    if (!tex) return;

    // 地面投影，避免看起来像浮在空中
    DrawCircleFilled(worldX + 0.6f, worldY + 1.1f, 3.2f, 0, 0, 0, 70);

    const float cx = (worldX - cameraX_) * zoom_;
    const float cy = (worldY - cameraY_) * zoom_;
    const float box = 3.0f * zoom_;   // 投出状态：缩小到原来的 3/7
    SDL_FRect dst = { cx - box * 0.5f, cy - box * 0.5f, box, box };
    const SDL_FPoint center = { box * 0.5f, box * 0.5f };
    SDL_RenderCopyExF(renderer_, tex, nullptr, &dst, spinDeg, &center, SDL_FLIP_NONE);
}

void CS2DRenderer::DrawCrosshair(float cx, float cy,
                                  Uint8 r, Uint8 g, Uint8 b, Uint8 a,
                                  float thickness, bool centerDot,
                                  float length, float gap) {
    // 水平线：左段 + 右段，中间 gap
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    for (float i = 0.0f; i < thickness; i += 1.0f) {
        const float off = i - thickness * 0.5f;
        const float lx = static_cast<float>(static_cast<int>(cx - gap - length));
        const float rx = static_cast<float>(static_cast<int>(cx + gap));
        const float lx2 = static_cast<float>(static_cast<int>(cx - gap));
        const float rx2 = static_cast<float>(static_cast<int>(cx + gap + length));
        const float sy = static_cast<float>(static_cast<int>(cy + off));
        SDL_RenderDrawLineF(renderer_, lx, sy, lx2, sy);
        SDL_RenderDrawLineF(renderer_, rx, sy, rx2, sy);
    }
    // 竖直线：上段 + 下段，中间 gap
    for (float i = 0.0f; i < thickness; i += 1.0f) {
        const float off = i - thickness * 0.5f;
        const float ty = static_cast<float>(static_cast<int>(cy - gap - length));
        const float by = static_cast<float>(static_cast<int>(cy + gap));
        const float ty2 = static_cast<float>(static_cast<int>(cy - gap));
        const float by2 = static_cast<float>(static_cast<int>(cy + gap + length));
        const float sx = static_cast<float>(static_cast<int>(cx + off));
        SDL_RenderDrawLineF(renderer_, sx, ty, sx, ty2);
        SDL_RenderDrawLineF(renderer_, sx, by, sx, by2);
    }
    // 中心点
    if (centerDot) {
        for (float i = -1.0f; i <= 1.0f; i += 1.0f)
            for (float j = -1.0f; j <= 1.0f; j += 1.0f)
                SDL_RenderDrawPointF(renderer_, cx + i, cy + j);
    }
}

void CS2DRenderer::DrawWeaponStatus(int weaponId, int ammo, int magazine,
                                    int viewW, int viewH) {
    // 右下角武器栏视觉 — 相对视口右下角定位（尺寸随 UI 缩放）
    const float us = g_uiScale;
    const int panelW = static_cast<int>(std::lround(160.0f * us));
    const int panelH = static_cast<int>(std::lround(56.0f * us));
    const int margin = static_cast<int>(std::lround(10.0f * us));
    const int panelX = viewW - panelW - margin;
    const int panelY = viewH - panelH - margin;
    SDL_Rect panel = {panelX, panelY, panelW, panelH};
    SDL_SetRenderDrawColor(renderer_, 18, 28, 38, 240);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 200, 155, 60, 200);
    SDL_RenderDrawRect(renderer_, &panel);
    // 底沿金色高亮线
    SDL_SetRenderDrawColor(renderer_, 230, 185, 80, 140);
    SDL_RenderDrawLine(renderer_, panel.x + 2, panel.y + panel.h - 1,
                       panel.x + panel.w - 2, panel.y + panel.h - 1);

    // 图标：按 WeaponId 查表；缺失时回退到武器世界贴图
    SDL_Texture* icon = nullptr;
    if (weaponId > 0 && weaponId < static_cast<int>(weaponIcons_.size())) {
        icon = weaponIcons_[weaponId];
        if (!icon && weaponId < static_cast<int>(weaponSprites_.size())) {
            icon = weaponSprites_[weaponId];
        }
    }
    if (!icon) return;

    int tw = 0, th = 0;
    SDL_QueryTexture(icon, nullptr, nullptr, &tw, &th);
    const int box = static_cast<int>(std::lround(44.0f * us));
    int dw = box;
    int dh = box;
    if (tw > 0 && th > 0) {
        if (tw >= th) {
            dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * th / tw)));
        } else {
            dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * tw / th)));
        }
    }
    const int iconX = panel.x + static_cast<int>(std::lround(10.0f * us)) + box / 2;
    const int iconY = panel.y + panel.h / 2;
    SDL_Rect iconRect = {iconX - dw / 2, iconY - dh / 2, dw, dh};
    SDL_RenderCopy(renderer_, icon, nullptr, &iconRect);

    (void)ammo;
    (void)magazine;
}

// ============================================================================
// 视野遮罩
// ============================================================================

bool CS2DRenderer::HasLineOfSight(float fromX, float fromY, float toX, float toY) const {
    if (!world_) return false;

    const int tileW = world_->GetTileWidth();
    const int tileH = world_->GetTileHeight();
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) return true;

    const int steps = std::max(2, static_cast<int>(dist / (std::min(tileW, tileH) * 0.5f)));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const float sx = fromX + dx * t;
        const float sy = fromY + dy * t;
        int row, col;
        world_->WorldToTile(sx, sy, row, col);
        if (world_->IsBlockingBullet(row, col)) return false;
    }
    return true;
}

// ---- 玩家视野可见度：距离 × 120° 扇形 × 墙体遮挡 ----
float CS2DRenderer::ComputeVisibility(float playerX, float playerY, float aimAngle,
                                     float targetX, float targetY) const {
    if (!world_) return 0.0f;

    const float deltaX = targetX - playerX;
    const float deltaY = targetY - playerY;
    const float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);

    // 1) 距离因子：半径内 1.0，边缘 smoothstep → 0.0
    const float radius = visionRadius_;
    const float fullyLitRadius = radius - VISION_SOFT_EDGE;
    float distFactor = 0.0f;
    if (dist <= fullyLitRadius) {
        distFactor = 1.0f;
    } else if (dist < radius) {
        float blend = (dist - fullyLitRadius) / VISION_SOFT_EDGE;
        blend = blend * blend * (3.0f - 2.0f * blend);  // smoothstep
        distFactor = 1.0f - blend;
    }
    if (distFactor <= 0.0f) return 0.0f;   // 超出半径，无需再算

    // 2) 角度因子：以玩家朝向为中心的 120° 扇形，扇形外完全不可见
    const float halfConeRad  = (VISION_CONE_DEG * 0.5f - VISION_ANGLE_SOFT * 0.5f)
                               * 3.14159265f / 180.0f;
    const float angleSoftRad = VISION_ANGLE_SOFT * 3.14159265f / 180.0f;

    const float targetAngle = std::atan2(deltaY, deltaX);
    float diff = targetAngle - aimAngle;
    while (diff > 3.14159265f)  diff -= 6.28318531f;
    while (diff < -3.14159265f) diff += 6.28318531f;
    const float absDiff = std::abs(diff);

    float coneFactor = 0.0f;
    if (absDiff <= halfConeRad) {
        coneFactor = 1.0f;
    } else if (absDiff < halfConeRad + angleSoftRad) {
        float blend = (absDiff - halfConeRad) / angleSoftRad;
        blend = blend * blend * (3.0f - 2.0f * blend);  // smoothstep
        coneFactor = 1.0f - blend;
    }
    if (coneFactor <= 0.0f) return 0.0f;   // 扇形外，跳过射线检测

    // 3) 视线遮挡：被墙挡则完全不可见
    if (!HasLineOfSight(playerX, playerY, targetX, targetY)) return 0.0f;

    return std::min(distFactor, coneFactor);
}

// 烟雾云：用同心圆近似柔和边缘
// 生成 128×128 的柔和烟雾贴图：径向衰减 × fBm 噪声，边缘完全透明
void CS2DRenderer::CreateSmokeTexture() {
    if (smokeTex_) { SDL_DestroyTexture(smokeTex_); smokeTex_ = nullptr; }
    if (!renderer_) return;

    constexpr int S = 128;
    std::vector<Uint8> px(static_cast<size_t>(S) * S * 4);
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / S * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / S * 2.0f - 1.0f;
            const float d = std::sqrt(u * u + v * v);
            const float edge = std::clamp(1.0f - d, 0.0f, 1.0f);
            const float n = SmokeFbm(u * 2.4f + 13.0f, v * 2.4f + 7.0f);
            const float a = std::pow(edge, 1.35f) * (0.45f + 0.9f * n);
            const Uint8 alpha = static_cast<Uint8>(std::clamp(a * 255.0f, 0.0f, 255.0f));
            const Uint8 c = static_cast<Uint8>(std::clamp(182.0f + 30.0f * n, 0.0f, 255.0f));
            const size_t idx = (static_cast<size_t>(y) * S + x) * 4;
            px[idx]     = c;
            px[idx + 1] = c;
            px[idx + 2] = static_cast<Uint8>(std::min(255.0f, c + 4.0f));
            px[idx + 3] = alpha;
        }
    }

    smokeTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STATIC, S, S);
    if (!smokeTex_) return;
    SDL_UpdateTexture(smokeTex_, nullptr, px.data(), S * 4);
    SDL_SetTextureBlendMode(smokeTex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(smokeTex_, SDL_ScaleModeLinear);
}

void CS2DRenderer::DrawSmokeCloud(float worldX, float worldY, float worldRadius,
                                  float age, float expandTime, float life,
                                  float maxLife, float seed) {
    if (!renderer_ || !smokeTex_) return;

    const float cx = (worldX - cameraX_) * zoom_;
    const float cy = (worldY - cameraY_) * zoom_;
    const float p = std::clamp(age / std::max(0.08f, expandTime), 0.0f, 1.0f);
    const float ease = 1.0f - (1.0f - p) * (1.0f - p);          // 先快后慢地扩散
    const float r = worldRadius * (0.12f + 0.88f * ease) * zoom_;
    if (r < 1.5f) return;

    // 出现时快速淡入，最后 2 秒淡出
    const float fadeIn  = std::clamp(age / 0.28f, 0.0f, 1.0f);
    const float fadeOut = std::clamp(life / std::min(2.0f, std::max(0.1f, maxLife)), 0.0f, 1.0f);
    const float k = fadeIn * fadeOut;
    if (k <= 0.01f) return;

    auto drawPuff = [&](float ox, float oy, float scale, float rot, float alphaMul) {
        const float size = r * 2.0f * scale;
        SDL_FRect dst = { cx + ox - size * 0.5f, cy + oy - size * 0.5f, size, size };
        const Uint8 a = static_cast<Uint8>(std::clamp(alphaMul * k * 255.0f, 0.0f, 255.0f));
        if (a < 3) return;
        SDL_SetTextureAlphaMod(smokeTex_, a);
        const SDL_FPoint center = { size * 0.5f, size * 0.5f };
        SDL_RenderCopyExF(renderer_, smokeTex_, nullptr, &dst, rot, &center, SDL_FLIP_NONE);
    };

    // 底层：中心最大的一团
    drawPuff(0.0f, 0.0f, 0.95f, seed * 360.0f, 0.85f);

    // 体积层：围绕中心缓慢漂移的几团，制造层次和轮廓
    constexpr int kPuffs = 12;
    for (int i = 0; i < kPuffs; ++i) {
        const float ang = seed * 6.2831853f + static_cast<float>(i) * 2.39996f;
        const float wob = 0.18f + 0.22f * std::fmod(static_cast<float>(i) * 0.61f + seed * 3.0f, 1.0f);
        const float ox = std::cos(ang) * r * wob;
        const float oy = std::sin(ang) * r * wob * 0.82f;
        const float scale = 0.40f + 0.22f * std::fmod(static_cast<float>(i) * 0.37f + seed, 1.0f);
        const float rot = (seed + static_cast<float>(i) * 0.23f) * 360.0f;
        drawPuff(ox, oy, scale, rot, 0.52f);
    }

    // 爆炸初期：从中心向外散开的一圈烟（很快消失）
    if (age < 0.6f) {
        const float bp = age / 0.6f;
        const float ba = 1.0f - bp;
        for (int i = 0; i < 7; ++i) {
            const float ang = seed * 6.2831853f + static_cast<float>(i) * 0.8976f;
            const float dist = r * (0.10f + 0.85f * bp);
            const float ox = std::cos(ang) * dist;
            const float oy = std::sin(ang) * dist * 0.85f;
            drawPuff(ox, oy, 0.25f + 0.45f * bp, ang * 57.2958f, 0.5f * ba);
        }
    }

    SDL_SetTextureAlphaMod(smokeTex_, 255);
}

void CS2DRenderer::DrawFireZone(float worldX, float worldY, float worldRadius,
                                float age, float life, float maxLife, float seed) {
    if (!renderer_) return;
    const float r = worldRadius * zoom_;
    if (r < 2.0f) return;

    const float fadeIn  = std::clamp(age / 0.30f, 0.0f, 1.0f);
    const float fadeOut = std::clamp(life / std::min(1.0f, std::max(0.1f, maxLife)), 0.0f, 1.0f);
    const float k = fadeIn * fadeOut;
    if (k <= 0.01f) return;

    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;

    auto blob = [&](float ox, float oy, float rad, int rr, int gg, int bb, float a) {
        DrawCircleFilled(worldX + ox / zoom_, worldY + oy / zoom_, rad / zoom_,
                         rr, gg, bb,
                         static_cast<Uint8>(std::clamp(a * k * 255.0f, 0.0f, 255.0f)));
    };

    // 外层：橙红色火焰团（大量交叠的圆，形成不规则的火焰轮廓）
    for (int i = 0; i < 24; ++i) {
        const float ang = seed * 6.2831853f + static_cast<float>(i) * 2.39996f;
        const float d   = r * 0.42f * std::fmod(static_cast<float>(i) * 0.37f + seed, 1.0f);
        const float ph  = t * (3.0f + 0.25f * (i % 5)) + static_cast<float>(i) * 1.7f;
        const float fl  = 0.5f + 0.5f * std::sin(ph);
        const float rad = r * (0.10f + 0.07f * fl);
        const float ox  = std::cos(ang) * d;
        const float oy  = std::sin(ang) * d * 0.6f - r * 0.05f * fl;
        blob(ox, oy, rad, 255, 85 + static_cast<int>(55.0f * fl), 18, 0.55f + 0.25f * fl);
    }

    // 中层：橙黄色
    for (int i = 0; i < 14; ++i) {
        const float ang = seed * 6.2831853f + static_cast<float>(i) * 1.7f;
        const float d   = r * 0.24f * std::fmod(static_cast<float>(i) * 0.53f + seed, 1.0f);
        const float ph  = t * (4.0f + 0.3f * (i % 4)) + static_cast<float>(i) * 2.1f;
        const float fl  = 0.5f + 0.5f * std::sin(ph);
        const float rad = r * (0.07f + 0.05f * fl);
        blob(std::cos(ang) * d, std::sin(ang) * d * 0.6f, rad,
             255, 175 + static_cast<int>(45.0f * fl), 55, 0.60f + 0.25f * fl);
    }

    // 内层：黄白色火心
    for (int i = 0; i < 8; ++i) {
        const float ang = seed * 6.2831853f + static_cast<float>(i) * 1.1f;
        const float d   = r * 0.12f * std::fmod(static_cast<float>(i) * 0.71f + seed, 1.0f);
        const float fl  = 0.5f + 0.5f * std::sin(t * 5.0f + static_cast<float>(i) * 2.3f + seed * 8.0f);
        blob(std::cos(ang) * d, std::sin(ang) * d * 0.6f, r * (0.05f + 0.03f * fl),
             255, 228, 150, 0.70f + 0.25f * fl);
    }

    // 火星：向上飘散
    for (int i = 0; i < 8; ++i) {
        const float ph = t * (1.4f + 0.22f * i) + static_cast<float>(i) * 2.3f + seed * 7.0f;
        const float lifeT = std::fmod(ph, 1.0f);
        const float ang = seed * 6.2831853f + static_cast<float>(i) * 1.1f;
        const float ox = std::cos(ang) * r * 0.32f * (0.4f + 0.6f * lifeT);
        const float oy = -lifeT * r * 0.70f + std::sin(ang) * r * 0.10f;
        blob(ox, oy, r * 0.016f * (1.0f - lifeT), 255, 225, 140, (1.0f - lifeT) * 0.9f);
    }
}

void CS2DRenderer::DrawFlashOverlay(Uint8 alpha) {
    if (!renderer_ || alpha == 0) return;
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, alpha);
    SDL_Rect full = { 0, 0, viewW_, viewH_ };
    SDL_RenderFillRect(renderer_, &full);
}

// 命中反馈：准星处画一个 X（四条短斜线）
void CS2DRenderer::DrawHitMarker(float screenX, float screenY, float scale, Uint8 alpha) {
    if (!renderer_ || alpha == 0) return;

    const float inner = 4.0f * scale;
    const float outer = 9.0f * scale;

    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, alpha);
    for (float t = 0.0f; t < 2.0f; t += 1.0f) {
        const float off = t * 0.5f;
        // 左上 → 右下
        SDL_RenderDrawLineF(renderer_, screenX - outer + off, screenY - outer + off,
                            screenX - inner + off, screenY - inner + off);
        SDL_RenderDrawLineF(renderer_, screenX + inner + off, screenY + inner + off,
                            screenX + outer + off, screenY + outer + off);
        // 右上 → 左下
        SDL_RenderDrawLineF(renderer_, screenX + outer + off, screenY - outer + off,
                            screenX + inner + off, screenY - inner + off);
        SDL_RenderDrawLineF(renderer_, screenX - inner + off, screenY + inner + off,
                            screenX - outer + off, screenY + outer + off);
    }
}

// 受伤反馈：屏幕四周红色渐隐边框
void CS2DRenderer::DrawDamageVignette(Uint8 alpha) {
    if (!renderer_ || alpha == 0) return;

    const int bands = 6;
    const int maxThick = 56;
    for (int i = 0; i < bands; ++i) {
        const int thick = maxThick * (bands - i) / bands;
        const Uint8 a = static_cast<Uint8>(alpha * (i + 1) / bands);
        SDL_SetRenderDrawColor(renderer_, 200, 30, 30, a);

        const int w = viewW_, h = viewH_;
        const SDL_Rect top    = { 0, 0, w, thick };
        const SDL_Rect bottom = { 0, h - thick, w, thick };
        const SDL_Rect left   = { 0, 0, thick, h };
        const SDL_Rect right  = { w - thick, 0, thick, h };
        SDL_RenderFillRect(renderer_, &top);
        SDL_RenderFillRect(renderer_, &bottom);
        SDL_RenderFillRect(renderer_, &left);
        SDL_RenderFillRect(renderer_, &right);
    }
}

void CS2DRenderer::DrawVisionFog(float playerX, float playerY, float aimAngle) {
    if (!visionTex_ || visionPixels_.empty() || !world_) return;

    const float px = playerX;
    const float py = playerY;

    for (int y = 0; y < maskH_; ++y) {
        for (int x = 0; x < maskW_; ++x) {
            const float worldX = cameraX_ + (x + 0.5f) * VISION_MASK_SCALE / zoom_;
            const float worldY = cameraY_ + (y + 0.5f) * VISION_MASK_SCALE / zoom_;

            // 与敌人剔除共用同一套几何，避免遮罩与可见性漂移
            const float vis = ComputeVisibility(px, py, aimAngle, worldX, worldY);
            const Uint8 alpha = static_cast<Uint8>(std::lround((1.0f - vis) * VISION_FOG_ALPHA));

            const size_t idx = (static_cast<size_t>(y) * maskW_ + x) * 4;
            visionPixels_[idx]     = VISION_FOG_R;
            visionPixels_[idx + 1] = VISION_FOG_G;
            visionPixels_[idx + 2] = VISION_FOG_B;
            visionPixels_[idx + 3] = alpha;
        }
    }

    SDL_UpdateTexture(visionTex_, nullptr, visionPixels_.data(), maskW_ * 4);
    SDL_Rect viewport = {0, 0, viewW_, viewH_};
    SDL_RenderCopy(renderer_, visionTex_, nullptr, &viewport);
}

// ============================================================================
// 计分板 / 雷达
// ============================================================================

void CS2DRenderer::DrawScoreboard(int total, int kills, int deaths, int viewW, int viewH) {
    char buf[128];
    snprintf(buf, sizeof(buf), "人数: %d    击杀: %d    阵亡: %d", total, kills, deaths);
    // 屏幕顶端正中黑底半透明条
    const int barW = 500, barH = 28;
    const int bx = (viewW - barW) / 2, by = 8;
    SDL_Rect rect = {bx, by, barW, barH};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 140);
    SDL_RenderFillRect(renderer_, &rect);
    SDL_SetRenderDrawColor(renderer_, 200, 200, 200, 120);
    SDL_RenderDrawRect(renderer_, &rect);

    // 用内置字体（简单方式：直接渲染文字省略；这里仅画背景条，文字由 ImGui 层覆盖更好）
    // 但为简单，这里用 SDL 无字体渲染；实际文字在 CS2DGame::Render 中由 ImGui DrawList 追加
    // 此处仅留作位
}

void CS2DRenderer::DrawRadarHUD(float playerX, float playerY, float playerAngle,
                                 const std::vector<RadarDot>& dots,
                                 float mapW, float mapH,
                                 bool playerCentered, bool rotateWithPlayer,
                                 bool blendBg, int bgAlpha,
                                 int hudSize, float mapZoom,
                                 bool showCenterRadar,
                                 int viewW, int viewH) {
    const int radarSize = showCenterRadar ? 320 : hudSize;
    const int margin = std::max(4, static_cast<int>(std::lround(8.0f * g_uiScale)));
    const int radarX = showCenterRadar ? (viewW - radarSize) / 2 : margin;
    const int radarY = showCenterRadar ? (viewH - radarSize) / 2 : margin;
    if (mapW <= 0.0f || mapH <= 0.0f) return;

    // 背景
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0,
                           static_cast<Uint8>(showCenterRadar ? 200 : bgAlpha));
    const SDL_Rect bgRect = {radarX, radarY, radarSize, radarSize};
    SDL_RenderFillRect(renderer_, &bgRect);

    // ---- 统一的坐标变换 ----
    // mapZoom 语义：1 = 整张地图刚好铺满雷达；>1 放大；<1 缩小。
    // 兼容旧存档：历史默认值 0.005 是另一套公式的产物，这里视为 1。
    float zoom = mapZoom;
    if (zoom < 0.5f || zoom > 8.0f) zoom = 1.0f;

    const float spanW = mapW / zoom;
    const float spanH = mapH / zoom;

    // 视野中心（并且限制在地图内，避免边缘看到空白导致点与图错位）
    float cx = playerCentered ? playerX : mapW * 0.5f;
    float cy = playerCentered ? playerY : mapH * 0.5f;
    if (spanW <= mapW) cx = std::clamp(cx, spanW * 0.5f, mapW - spanW * 0.5f);
    else              cx = mapW * 0.5f;
    if (spanH <= mapH) cy = std::clamp(cy, spanH * 0.5f, mapH - spanH * 0.5f);
    else              cy = mapH * 0.5f;

    // 世界 → 雷达像素
    const float sx = static_cast<float>(radarSize) / spanW;
    const float sy = static_cast<float>(radarSize) / spanH;
    const float halfSize = static_cast<float>(radarSize) * 0.5f;

    // 跟随旋转角（与地图贴图的 RenderCopyEx 用同一个角度）
    const float rotRad = rotateWithPlayer ? (playerAngle + 1.5707963f) : 0.0f;
    const float cosR = std::cos(rotRad);
    const float sinR = std::sin(rotRad);

    auto worldToRadar = [&](float wx, float wy) {
        float dx = (wx - cx) * sx;
        float dy = (wy - cy) * sy;
        if (rotateWithPlayer) {
            const float rx = dx * cosR - dy * sinR;
            const float ry = dx * sinR + dy * cosR;
            dx = rx;
            dy = ry;
        }
        return std::make_pair(radarX + halfSize + dx, radarY + halfSize + dy);
    };

    // ---- 地图底图：与点位共用同一套中心/缩放/旋转 ----
    if (radarMapTex_ && radarImgW_ > 0.0f && radarImgH_ > 0.0f) {
        const float kx = radarImgW_ / mapW;
        const float ky = radarImgH_ / mapH;

        SDL_Rect src;
        src.x = static_cast<int>(radarImgX_ + (cx - spanW * 0.5f) * kx);
        src.y = static_cast<int>(radarImgY_ + (cy - spanH * 0.5f) * ky);
        src.w = std::max(1, static_cast<int>(spanW * kx));
        src.h = std::max(1, static_cast<int>(spanH * ky));

        const SDL_Rect dst = {radarX, radarY, radarSize, radarSize};

        const Uint8 mod = static_cast<Uint8>(std::clamp(bgAlpha, 0, 255));
        SDL_SetTextureColorMod(radarMapTex_, mod, mod, mod);

        if (rotateWithPlayer) {
            const SDL_Point center = {radarSize / 2, radarSize / 2};
            SDL_RenderCopyEx(renderer_, radarMapTex_, &src, &dst,
                             static_cast<double>(rotRad * 180.0f / 3.14159265f),
                             &center, SDL_FLIP_NONE);
        } else {
            SDL_RenderCopy(renderer_, radarMapTex_, &src, &dst);
        }
        SDL_SetTextureColorMod(radarMapTex_, 255, 255, 255);
    }

    // 屏幕空间实心圆（半径很小时逐行扫描比多边形更干净）
    auto fillCircle = [&](float ccx, float ccy, float rad, Uint8 cr, Uint8 cg, Uint8 cb) {
        SDL_SetRenderDrawColor(renderer_, cr, cg, cb, 255);
        const int ri = static_cast<int>(std::ceil(rad));
        for (int dy = -ri; dy <= ri; ++dy) {
            const float w2 = rad * rad - static_cast<float>(dy * dy);
            if (w2 <= 0.0f) continue;
            const float halfW = std::sqrt(w2);
            SDL_RenderDrawLineF(renderer_, ccx - halfW, ccy + dy, ccx + halfW, ccy + dy);
        }
    };

    // 标记半径随缩放变化：铺满全图时很小，放大后变大
    const float dotR = std::clamp(1.5f + 0.9f * (zoom - 1.0f), 1.5f, 4.5f);

    // ---- 点位（友军 / 可见敌人）----
    for (const auto& d : dots) {
        auto [rx, ry] = worldToRadar(d.x, d.y);
        if (rx < radarX + 1 || rx > radarX + radarSize - 1 ||
            ry < radarY + 1 || ry > radarY + radarSize - 1) continue;

        // 深色描边 + 彩色圆点，避免与地图底色糊在一起
        fillCircle(rx, ry, dotR + 0.9f, 10, 12, 16);
        fillCircle(rx, ry, dotR, d.r, d.g, d.b);
    }

    // ---- 玩家：与 BOT 同样是圆点，用更亮的青色 + 外圈区分自己 ----
    {
        auto [px, py] = worldToRadar(playerX, playerY);
        const float selfR = dotR + 0.9f;
        fillCircle(px, py, selfR + 1.4f, 10, 12, 16);       // 深色描边
        fillCircle(px, py, selfR, 110, 215, 255);            // 青色实心
        // 外圈：只描边，进一步和友军区分
        SDL_SetRenderDrawColor(renderer_, 225, 245, 255, 235);
        const int ri = static_cast<int>(std::ceil(selfR + 2.2f));
        for (int deg = 0; deg < 360; deg += 12) {
            const float a = static_cast<float>(deg) * 0.0174533f;
            SDL_RenderDrawPointF(renderer_,
                                 px + std::cos(a) * (selfR + 2.2f),
                                 py + std::sin(a) * (selfR + 2.2f));
        }
        (void)ri;
    }

    // 外框最后画，压住底图边缘
    SDL_SetRenderDrawColor(renderer_, 120, 160, 200, 220);
    SDL_RenderDrawRect(renderer_, &bgRect);
}

// ============================================================================
// 回退方块
// ============================================================================

void CS2DRenderer::DrawTileFallback(int gid, const SDL_Rect& dst) const {
    const int hash = (gid * 2654435761u) & 0xFFFFFF;
    const Uint8 r = static_cast<Uint8>((hash >> 16) & 0xFF);
    const Uint8 g = static_cast<Uint8>((hash >> 8)  & 0xFF);
    const Uint8 b = static_cast<Uint8>(hash         & 0xFF);
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_RenderFillRect(renderer_, &dst);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 60);
    SDL_RenderDrawRect(renderer_, &dst);
}
