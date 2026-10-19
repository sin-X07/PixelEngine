#include "CS2DRenderer.hpp"
#include "../Core/PathUtils.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <filesystem>

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
    if (ak47Tex_)   { SDL_DestroyTexture(ak47Tex_);   ak47Tex_   = nullptr; }
    if (ak47dTex_)  { SDL_DestroyTexture(ak47dTex_);  ak47dTex_  = nullptr; }
    if (m4a1dTex_)  { SDL_DestroyTexture(m4a1dTex_);  m4a1dTex_  = nullptr; }
    if (m4a1Tex_)   { SDL_DestroyTexture(m4a1Tex_);   m4a1Tex_   = nullptr; }
    if (deagleTex_) { SDL_DestroyTexture(deagleTex_); deagleTex_ = nullptr; }
    if (knifeTex_)  { SDL_DestroyTexture(knifeTex_);  knifeTex_  = nullptr; }
    if (bulletTex_) { SDL_DestroyTexture(bulletTex_); bulletTex_ = nullptr; }

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

    ak47Tex_   = LoadTexture(mapDir_ + "/../sprites/ak47.png");
    ak47dTex_  = LoadTexture("assets/images/weapons/AK47_d.png");
    m4a1dTex_  = LoadTexture("assets/images/weapons/M4A1_d.png");
    m4a1Tex_   = LoadTexture(mapDir_ + "/../sprites/m4a1.png");
    deagleTex_ = LoadTexture(mapDir_ + "/../sprites/deagle.png");
    knifeTex_  = LoadTexture(mapDir_ + "/../sprites/knife.png");
    bulletTex_ = LoadTexture("assets/images/effects/bullet.png");

    std::cout << "[CS2DRenderer] 精灵加载: CT=" << (ctSheet_.tex ? "OK" : "FAIL")
              << " T=" << (tSheet_.tex ? "OK" : "FAIL")
              << " T_alt=" << (tAltSheet_.tex ? "OK" : "FAIL")
              << " AK47=" << (ak47Tex_ ? "OK" : "FAIL")
              << " M4A1=" << (m4a1Tex_ ? "OK" : "FAIL")
              << " Deagle=" << (deagleTex_ ? "OK" : "FAIL")
              << " Knife=" << (knifeTex_ ? "OK" : "FAIL")
              << " Bullet=" << (bulletTex_ ? "OK" : "FAIL") << std::endl;

    // Clean() 已销毁视野纹理，此处重建（修复视野雾失效 bug）
    CreateVisionTexture();
}

SDL_Texture* CS2DRenderer::LoadTexture(const std::string& filename) {
    const std::string resolved = PathUtils::ResolveAppPath(filename);
    SDL_Texture* tex = IMG_LoadTexture(renderer_, resolved.c_str());
    if (!tex) {
        std::cerr << "[CS2DRenderer] 加载图片失败: " << filename
                  << " (" << IMG_GetError() << ")" << std::endl;
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
                                int viewportW, int viewportH) {
    if (!world_) return;
    const float viewW = static_cast<float>(viewportW) / zoom_;
    const float viewH = static_cast<float>(viewportH) / zoom_;

    float totalW = world_->GetMapPixelW();
    float totalH = world_->GetMapPixelH();
    if (world_->HasBackgroundImage()) {
        totalW = std::max(totalW, world_->GetImageW());
        totalH = std::max(totalH, world_->GetImageH());
    }

    cameraX_ = std::clamp(playerCenterX - viewW * 0.5f, 0.0f, std::max(0.0f, totalW - viewW));
    cameraY_ = std::clamp(playerCenterY - viewH * 0.5f, 0.0f, std::max(0.0f, totalH - viewH));
}

// ============================================================================
// 背景图 & tile 层
// ============================================================================

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
                               p.weaponSlot, p.weaponType);
    }
}

void CS2DRenderer::DrawWeaponForCharacter(float cx, float cy, float size, float aimAngle,
                                          int weaponSlot, int weaponType) {
    SDL_Texture* weaponTex = nullptr;
    float degOffset = 90.0f;   // 多数贴图原生朝上，需要 +90° 使其指向 aimAngle
    float weaponSz   = 4.0f;   // 世界单位（×zoom 后约 16px）

    switch (weaponSlot) {
        case 1:  // 副武器：Deagle（原生朝右，偏移 0°）
            weaponTex = deagleTex_;
            degOffset = 0.0f;
            weaponSz  = 3.0f;
            break;
        case 2:  // 近战：Knife（原生朝上）
            weaponTex = knifeTex_;
            degOffset = 90.0f;
            weaponSz  = 4.5f;
            break;
        default: // 主武器：AK47 或 M4A1（原生朝上）
            weaponTex = (weaponType == 0) ? ak47Tex_ : m4a1Tex_;
            degOffset = 90.0f;
            weaponSz  = 4.0f;
            break;
    }
    if (!weaponTex) return;

    // 武器绘制在精灵前方偏移（世界单位，随缩放）
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
    SDL_RenderCopyExF(renderer_, weaponTex, nullptr, &wdst,
                      aimAngle * 180.0f / 3.14159265f + degOffset,
                      &wcenter, SDL_FLIP_NONE);
}

// ============================================================================
// 子弹 / 准星
// ============================================================================

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

void CS2DRenderer::DrawWeaponStatus(int weaponSlot, int weaponType, int viewW, int viewH) {
    // 右下角武器栏视觉 — 相对视口右下角定位
    const int panelW = 160, panelH = 56;
    const int panelX = viewW - panelW - 10;
    const int panelY = viewH - panelH - 10;
    SDL_Rect panel = {panelX, panelY, panelW, panelH};
    SDL_SetRenderDrawColor(renderer_, 18, 28, 38, 240);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 200, 155, 60, 200);
    SDL_RenderDrawRect(renderer_, &panel);
    // 底沿金色高亮线
    SDL_SetRenderDrawColor(renderer_, 230, 185, 80, 140);
    SDL_RenderDrawLine(renderer_, panel.x + 2, panel.y + panel.h - 1, panel.x + panel.w - 2, panel.y + panel.h - 1);

    // 按武器槽选图标：AK47 用 AK47_d.png 专有图
    SDL_Texture* icon = nullptr;
    switch (weaponSlot) {
        case 1:  icon = deagleTex_; break;
        case 2:  icon = knifeTex_;  break;
        default: icon = (weaponType == 0) ? ak47dTex_ : m4a1dTex_;
                 if (!icon) icon = (weaponType == 0) ? ak47Tex_ : m4a1Tex_;
                 break;
    }
    if (!icon) return;

    // 图标放在面板左侧，文字放在右侧（通过 anchored offset 与 ImGui 文字错开）
    int tw = 0, th = 0;
    SDL_QueryTexture(icon, nullptr, nullptr, &tw, &th);
    const int box = 44;
    int dw = box;
    int dh = box;
    if (tw > 0 && th > 0) {
        if (tw >= th) {
            dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * th / tw)));
        } else {
            dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(box) * tw / th)));
        }
    }
    // 图标偏左放置，给右侧文字留空间
    const int iconX = panel.x + 10 + box / 2;
    const int iconY = panel.y + panel.h / 2;
    SDL_Rect iconRect = {iconX - dw / 2, iconY - dh / 2, dw, dh};
    SDL_RenderCopy(renderer_, icon, nullptr, &iconRect);
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
    const float fullyLitRadius = VISION_RADIUS - VISION_SOFT_EDGE;
    float distFactor = 0.0f;
    if (dist <= fullyLitRadius) {
        distFactor = 1.0f;
    } else if (dist < VISION_RADIUS) {
        float blend = (dist - fullyLitRadius) / VISION_SOFT_EDGE;
        blend = blend * blend * (3.0f - 2.0f * blend);  // smoothstep
        distFactor = 1.0f - blend;
    }
    if (distFactor <= 0.0f) return 0.0f;   // 超出半径，无需再算

    // 2) 角度因子：扇形内 1.0，边缘 smoothstep → 0.0
    const float halfConeRad  = (VISION_CONE_DEG * 0.5f - VISION_ANGLE_SOFT * 0.5f) * 3.14159265f / 180.0f;
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
                                 const std::vector<float>& botXs,
                                 const std::vector<float>& botYs,
                                 const std::vector<bool>& botAlive,
                                 float mapW, float mapH,
                                 bool playerCentered, bool rotateWithPlayer,
                                 bool blendBg, int bgAlpha,
                                 int hudSize, float mapZoom,
                                 bool showCenterRadar,
                                 int viewW, int viewH) {
    const int radarSize = showCenterRadar ? 320 : hudSize;
    const int radarX = showCenterRadar ? (viewW - radarSize) / 2 : 8;
    const int radarY = showCenterRadar ? (viewH - radarSize) / 2 : 8;

    // 背景
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, static_cast<Uint8>(showCenterRadar ? 200 : bgAlpha));
    const SDL_Rect bgRect = {radarX, radarY, radarSize, radarSize};
    SDL_RenderFillRect(renderer_, &bgRect);
    SDL_SetRenderDrawColor(renderer_, 100, 100, 100, 180);
    SDL_RenderDrawRect(renderer_, &bgRect);

    const float scale = static_cast<float>(radarSize) * mapZoom;
    const float half = static_cast<float>(radarSize) * 0.5f;

    // 计算地图在雷达中的中心偏移
    float mapCenterX = playerCentered ? playerX : (mapW * 0.5f);
    float mapCenterY = playerCentered ? playerY : (mapH * 0.5f);

    // ---- 从碰撞网格逐像素采样, 渲染地图地形（替代纯黑背景） ----
    if (world_) {
        const auto& grid = world_->GetCollisionGrid();
        const int gridW = world_->GetMapWidth();
        const int gridH = world_->GetMapHeight();
        if (!grid.empty() && gridW > 0 && gridH > 0) {
            const float invScale = 1.0f / scale;
            // 每个雷达像素 → 世界坐标 → 碰撞类型 → 着色
            for (int ry = 0; ry < radarSize; ++ry) {
                for (int rx = 0; rx < radarSize; ++rx) {
                    const float radarWorldX = (rx - half) * invScale + mapCenterX;
                    const float radarWorldY = (ry - half) * invScale + mapCenterY;
                    if (radarWorldX < 0.0f || radarWorldX >= mapW ||
                        radarWorldY < 0.0f || radarWorldY >= mapH) continue;
                    const int gx = static_cast<int>(radarWorldX / world_->GetTileWidth());
                    const int gy = static_cast<int>(radarWorldY / world_->GetTileHeight());
                    if (gx < 0 || gx >= gridW || gy < 0 || gy >= gridH) continue;
                    const auto cell = grid[gy * gridW + gx];
                    Uint8 cr = 40, cg = 40, cb = 40;
                    switch (cell) {
                        case CS2DWorld::CellType::Solid:      cr = 70; cg = 60; cb = 55; break;
                        case CS2DWorld::CellType::Penetrable: cr = 50; cg = 55; cb = 50; break;
                        case CS2DWorld::CellType::Damage:     cr = 80; cg = 30; cb = 20; break;
                        default:                               cr = 30; cg = 35; cb = 30; break;
                    }
                    SDL_SetRenderDrawColor(renderer_, cr, cg, cb, 255);
                    SDL_RenderDrawPoint(renderer_, radarX + rx, radarY + ry);
                }
            }
        }
    }

    // 辅助函数：世界坐标→雷达屏幕坐标
    auto worldToRadar = [&](float wx, float wy) {
        float rx, ry;
        if (rotateWithPlayer) {
            const float dx = wx - mapCenterX;
            const float dy = wy - mapCenterY;
            const float cosA = std::cos(-playerAngle);
            const float sinA = std::sin(-playerAngle);
            const float rx_rot = dx * cosA - dy * sinA;
            const float ry_rot = dx * sinA + dy * cosA;
            rx = rx_rot * scale + half + radarX;
            ry = ry_rot * scale + half + radarY;
        } else {
            rx = (wx - mapCenterX) * scale + half + radarX;
            ry = (wy - mapCenterY) * scale + half + radarY;
        }
        return std::make_pair(rx, ry);
    };

    // 绘制 BOT 点（已隐藏——雷达不显示敌人位置）
    // 如需启用，取消注释即可
    /*
    for (size_t i = 0; i < botXs.size(); ++i) {
        if (!botAlive[i]) continue;
        auto [bx, by] = worldToRadar(botXs[i], botYs[i]);
        if (bx < radarX || bx > radarX + radarSize ||
            by < radarY || by > radarY + radarSize) continue;
        SDL_SetRenderDrawColor(renderer_, 255, 80, 80, 220);
        const int r = 3;
        const SDL_Rect dot = {static_cast<int>(bx) - r, static_cast<int>(by) - r, r * 2, r * 2};
        SDL_RenderFillRect(renderer_, &dot);
    }
    */

    // 绘制玩家点（三角箭头）
    auto [px, py] = worldToRadar(playerX, playerY);
    const float arrowAngle = rotateWithPlayer ? 0.0f : playerAngle;
    const float arrowSize = 5.0f;
    SDL_SetRenderDrawColor(renderer_, 80, 180, 255, 255);
    // 画三角形
    const float tipX = px + std::cos(arrowAngle) * arrowSize;
    const float tipY = py + std::sin(arrowAngle) * arrowSize;
    const float leftX = px + std::cos(arrowAngle + 2.5f) * (arrowSize * 0.6f);
    const float leftY = py + std::sin(arrowAngle + 2.5f) * (arrowSize * 0.6f);
    const float rightX = px + std::cos(arrowAngle - 2.5f) * (arrowSize * 0.6f);
    const float rightY = py + std::sin(arrowAngle - 2.5f) * (arrowSize * 0.6f);
    SDL_RenderDrawLineF(renderer_, tipX, tipY, leftX, leftY);
    SDL_RenderDrawLineF(renderer_, tipX, tipY, rightX, rightY);
    SDL_RenderDrawLineF(renderer_, leftX, leftY, rightX, rightY);
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