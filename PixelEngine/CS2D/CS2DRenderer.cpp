#include "CS2DRenderer.hpp"
#include "../Core/PathUtils.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <filesystem>

bool CS2DRenderer::Initialize(SDL_Renderer* ren, const std::string& mapDir) {
    renderer_ = ren;
    mapDir_   = mapDir;
    const std::string absDir = PathUtils::ResolveAppPath(mapDir);
    if (std::filesystem::exists(absDir)) {
        mapDir_ = absDir;
    }
    std::cout << "[CS2DRenderer] 地图目录: " << mapDir_ << std::endl;
    return true;
}

void CS2DRenderer::Clean() {
    if (backgroundTex_) { SDL_DestroyTexture(backgroundTex_); backgroundTex_ = nullptr; }
    for (auto& tt : tilesetTextures_) {
        if (tt.texture) { SDL_DestroyTexture(tt.texture); tt.texture = nullptr; }
    }
    tilesetTextures_.clear();
    world_ = nullptr;
}

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
    if (!world_->GetMapData()) return;
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

void CS2DRenderer::UpdateCamera(float playerCenterX, float playerCenterY,
                                int viewportW, int viewportH) {
    if (!world_) return;
    const float viewW = static_cast<float>(viewportW) / zoom_;
    const float viewH = static_cast<float>(viewportH) / zoom_;

    // 使用 map 像素尺寸和背景图尺寸中的较大者
    float totalW = world_->GetMapPixelW();
    float totalH = world_->GetMapPixelH();
    if (world_->HasBackgroundImage()) {
        totalW = std::max(totalW, world_->GetImageW());
        totalH = std::max(totalH, world_->GetImageH());
    }

    cameraX_ = std::clamp(playerCenterX - viewW * 0.5f, 0.0f, std::max(0.0f, totalW - viewW));
    cameraY_ = std::clamp(playerCenterY - viewH * 0.5f, 0.0f, std::max(0.0f, totalH - viewH));
}

// ========= 背景图绘制 =========

void CS2DRenderer::DrawImageBackground() {
    if (!backgroundTex_ || !world_) return;

    const float viewW = world_->GetImageW();
    const float viewH = world_->GetImageH();

    // 背景图按原始像素绘制，跟随相机
    SDL_Rect dst;
    dst.x = static_cast<int>(std::lround(-cameraX_ * zoom_));
    dst.y = static_cast<int>(std::lround(-cameraY_ * zoom_));
    dst.w = static_cast<int>(std::lround(viewW * zoom_));
    dst.h = static_cast<int>(std::lround(viewH * zoom_));

    SDL_RenderCopy(renderer_, backgroundTex_, nullptr, &dst);
}

// ========= 瓦片层绘制 =========

void CS2DRenderer::DrawTileLayer(const std::vector<int>& data,
                                 int layerWidth, int layerHeight) {
    if (!world_ || data.empty()) return;

    const int tileW = world_->GetTileWidth();
    const int tileH = world_->GetTileHeight();

    const int viewW = static_cast<int>(std::ceil(800.0f / zoom_));
    const int viewH = static_cast<int>(std::ceil(600.0f / zoom_));

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

    // 1. 背景图（永远在最底层）
    DrawImageBackground();

    // 2. 瓦片层
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

// ========= 实体绘制 =========

void CS2DRenderer::DrawPlayer(float x, float y, float size, float aimAngle,
                              bool isCT, Uint8 alpha) {
    const float cx = (x - cameraX_) * zoom_;
    const float cy = (y - cameraY_) * zoom_;
    const float sz = size * zoom_;

    SDL_Rect body = {
        static_cast<int>(std::lround(cx - sz * 0.5f)),
        static_cast<int>(std::lround(cy - sz * 0.5f)),
        static_cast<int>(std::lround(sz)),
        static_cast<int>(std::lround(sz))
    };
    SDL_SetRenderDrawColor(renderer_, isCT ? 70 : 220, isCT ? 130 : 70,
                           isCT ? 240 : 70, alpha);
    SDL_RenderFillRect(renderer_, &body);

    const float lineLen = sz * 0.6f;
    const int lx = static_cast<int>(std::lround(cx + std::cos(aimAngle) * lineLen));
    const int ly = static_cast<int>(std::lround(cy + std::sin(aimAngle) * lineLen));
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, alpha);
    SDL_RenderDrawLine(renderer_,
                       static_cast<int>(std::lround(cx)),
                       static_cast<int>(std::lround(cy)), lx, ly);
}

void CS2DRenderer::DrawBOT(float x, float y, float size, float aimAngle,
                            bool isCT, Uint8 alpha) {
    const float cx = (x - cameraX_) * zoom_;
    const float cy = (y - cameraY_) * zoom_;
    const float sz = size * zoom_;

    SDL_Rect body = {
        static_cast<int>(std::lround(cx - sz * 0.5f)),
        static_cast<int>(std::lround(cy - sz * 0.5f)),
        static_cast<int>(std::lround(sz)),
        static_cast<int>(std::lround(sz))
    };
    SDL_SetRenderDrawColor(renderer_, isCT ? 50 : 200, isCT ? 100 : 50,
                           isCT ? 210 : 50, alpha);
    SDL_RenderFillRect(renderer_, &body);

    const float lineLen = sz * 0.6f;
    const int lx = static_cast<int>(std::lround(cx + std::cos(aimAngle) * lineLen));
    const int ly = static_cast<int>(std::lround(cy + std::sin(aimAngle) * lineLen));
    SDL_SetRenderDrawColor(renderer_, 255, 255, 200, alpha);
    SDL_RenderDrawLine(renderer_,
                       static_cast<int>(std::lround(cx)),
                       static_cast<int>(std::lround(cy)), lx, ly);
}

void CS2DRenderer::DrawBullet(float x, float y, float vx, float vy) {
    const float cx = (x - cameraX_) * zoom_;
    const float cy = (y - cameraY_) * zoom_;
    const int r = std::max(2, static_cast<int>(std::lround(3.0f * zoom_)));
    SDL_Rect rect = {static_cast<int>(cx) - r, static_cast<int>(cy) - r, r * 2, r * 2};
    SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 255);
    SDL_RenderFillRect(renderer_, &rect);
}

void CS2DRenderer::DrawCrosshair(float cx, float cy, float size) {
    const int s = static_cast<int>(std::lround(size));
    SDL_SetRenderDrawColor(renderer_, 0, 255, 0, 200);
    SDL_RenderDrawLine(renderer_, static_cast<int>(cx) - s, static_cast<int>(cy),
                       static_cast<int>(cx) + s, static_cast<int>(cy));
    SDL_RenderDrawLine(renderer_, static_cast<int>(cx), static_cast<int>(cy) - s,
                       static_cast<int>(cx), static_cast<int>(cy) + s);
}

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