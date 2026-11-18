#include "CS2DRenderer.hpp"
#include "Visibility/BodyGeometry.hpp"
#include "../Graphics/TeamVisualStyle.hpp"
#include "../Graphics/UiScale.hpp"
#include "../Core/PathUtils.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <array>
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
    zoom_     = 3.0f;   // 初始镜头缩放，随后由视野与视口计算。
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

void CS2DRenderer::DrawDroppedKit(float x, float y) {
    if (!kitTexture_) kitTexture_ = IMG_LoadTexture(renderer_,PathUtils::ResolveProjectFile("assets/cs2d/sprites/defuse_kit_d.png").c_str());
    if (!kitTexture_) return;
    const float size=8*zoom_;
    SDL_FRect target{(x-cameraX_)*zoom_-size/2,(y-cameraY_)*zoom_-size/2,size,size};
    MaskedCopy(kitTexture_,nullptr,target,0);
}

void CS2DRenderer::Clean() {
    if (scopeSceneTex_) SDL_DestroyTexture(scopeSceneTex_);
    scopeSceneTex_ = nullptr;
    scopeReturnTarget_ = nullptr;
    if (kitTexture_) { SDL_DestroyTexture(kitTexture_); kitTexture_=nullptr; }
    for (auto*& tex : fireTileTex_) { if (tex) SDL_DestroyTexture(tex); tex = nullptr; }
    if (radarMapTex_) { SDL_DestroyTexture(radarMapTex_); radarMapTex_ = nullptr; }
    for (auto*& texture : radarFrameTex_) {
        if (texture) SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    radarFrameSize_.fill(0);
    if (backgroundTex_) { SDL_DestroyTexture(backgroundTex_); backgroundTex_ = nullptr; }
    if (perimeterTex_) { SDL_DestroyTexture(perimeterTex_); perimeterTex_ = nullptr; }
    if (perimeterGroundTex_) { SDL_DestroyTexture(perimeterGroundTex_); perimeterGroundTex_ = nullptr; }
    perimeterWorldRect_ = {};
    perimeterTileW_ = perimeterTileH_ = 0.0f;
    perimeterColor_ = {72, 68, 58, 255};
    perimeterDust_ = false;
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
    cameraInitialized_ = false;
    Clean();
    world_ = world;
    vis_.SetWorld(world);
    vis_.SetDistanceLimited(false);
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
            LoadPerimeter(imgPath);
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
    ctSheet_.frameW  = 512; ctSheet_.frameH = 512; ctSheet_.cols = 2; ctSheet_.rows = 3;

    tSheet_.tex      = LoadTexture(mapDir_ + "/../sprites/t_player.png");
    tSheet_.frameW   = 512; tSheet_.frameH  = 512; tSheet_.cols  = 2; tSheet_.rows  = 3;

    tAltSheet_.tex   = LoadTexture(mapDir_ + "/../sprites/t_player_alt.png");
    tAltSheet_.frameW = 512; tAltSheet_.frameH = 512; tAltSheet_.cols = 2; tAltSheet_.rows = 3;

    // 敌我轮廓素材（tools/gen_outline_sprites.py 生成；缺失则退回几何环）
    ctOutlineSheet_.tex     = LoadTexture(mapDir_ + "/../sprites/ct_player_outline.png");
    ctOutlineSheet_.frameW  = 512; ctOutlineSheet_.frameH = 512; ctOutlineSheet_.cols = 2; ctOutlineSheet_.rows = 3;
    tOutlineSheet_.tex      = LoadTexture(mapDir_ + "/../sprites/t_player_outline.png");
    tOutlineSheet_.frameW   = 512; tOutlineSheet_.frameH = 512; tOutlineSheet_.cols = 2; tOutlineSheet_.rows = 3;
    tAltOutlineSheet_.tex   = LoadTexture(mapDir_ + "/../sprites/t_player_alt_outline.png");
    tAltOutlineSheet_.frameW = 512; tAltOutlineSheet_.frameH = 512; tAltOutlineSheet_.cols = 2; tAltOutlineSheet_.rows = 3;

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
    if (!world_ || cameraFrozen_) return;

    // 视野是半径 VISION_RADIUS 的圆：先按「整圆刚好装进较窄的那一屏边」算 fit，
    // 再整体放大 15% —— 画面更近、角色更大；四角雾边被裁掉属预期。
    // 公式已抽到 VisibilityQuery，无头仿真用同一个函数算半径，保证判定一致。
    const float baseZoom = VisibilityQuery::BaseZoomForViewport(viewportW, viewportH);

    // 主场景距离档只乘一次，GetZoom() 返回最终世界到屏幕比例。
    zoomMultiplier = std::clamp(zoomMultiplier, 0.25f, 4.0f);
    zoom_ = baseZoom * zoomMultiplier;

    const float viewW = static_cast<float>(viewportW) / zoom_;
    const float viewH = static_cast<float>(viewportH) / zoom_;

    // 视野半径跟随主场景距离档，独立镜内放大沿用主场景可见性。
    visionRadius_ = VisibilityQuery::RadiusForViewport(viewportW, viewportH, baseZoom) / zoomMultiplier;
    vis_.SetVisionRadius(visionRadius_);

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

    const float targetX = std::clamp(playerCenterX - viewW * 0.5f, loX, hiX);
    const float targetY = std::clamp(playerCenterY - viewH * 0.5f, loY, hiY);
    const float blend = cameraInitialized_ && frameDelta_ > 0.0f
        ? 1.0f - std::exp(-Tuning::CameraSmoothRate * frameDelta_) : 1.0f;
    cameraX_ += (targetX - cameraX_) * blend;
    cameraY_ += (targetY - cameraY_) * blend;
    cameraSettled_ = std::hypot(targetX - cameraX_, targetY - cameraY_) * zoom_ <= Tuning::CameraSettlePixels;
    cameraInitialized_ = true;
}

// Solve framing before applying the camera transform. Zoom preserves the visible aim point.
void CS2DRenderer::UpdatePlayerCamera(float playerX, float playerY, float actorRadius,
                                      float aimX, float aimY, int viewportW, int viewportH,
                                      float zoomMultiplier, float lensRadius, SDL_FPoint shake) {
    if (!world_ || cameraFrozen_) return;
    const float baseZoom = VisibilityQuery::BaseZoomForViewport(viewportW, viewportH);
    constexpr float safe = 12.0f;
    const float availableX = std::max(1.0f, viewportW - safe * 2 - lensRadius);
    const float availableY = std::max(1.0f, viewportH - safe * 2 - lensRadius);
    const float fitZoom = std::min(availableX / (std::abs(aimX-playerX)+actorRadius),
                                   availableY / (std::abs(aimY-playerY)+actorRadius));
    const float targetZoom = std::max(.001f, std::min(baseZoom * zoomMultiplier, fitZoom));
    const float blend = cameraInitialized_ && frameDelta_ > 0
        ? 1.0f - std::exp(-Tuning::CameraSmoothRate * frameDelta_) : 1.0f;
    const float screenAimX = (aimX-cameraX_) * zoom_;
    const float screenAimY = (aimY-cameraY_) * zoom_;
    const float nextZoom = std::min(fitZoom, zoom_ + (targetZoom-zoom_) * blend);
    const float viewWidth = viewportW / nextZoom, viewHeight = viewportH / nextZoom;
    bool positionSettled = true;
    auto axis = [&](float actor, float aim, float extent, float worldExtent, float previousScreen, float recoil) {
        const float actorLo = actor + actorRadius - extent + safe / nextZoom;
        const float actorHi = actor - actorRadius - safe / nextZoom;
        const float aimLo = aim - extent + (lensRadius+safe) / nextZoom;
        const float aimHi = aim - (lensRadius+safe) / nextZoom;
        const float lo = std::max(actorLo, aimLo), hi = std::min(actorHi, aimHi);
        const float desiredCenter = actor + (aim-actor) * Tuning::CameraLookShift;
        const float mapLo = -Tuning::VisionRadius;
        const float mapHi = std::max(mapLo, worldExtent-extent+Tuning::VisionRadius);
        const float desired = std::clamp(desiredCenter-extent*.5f,mapLo,mapHi);
        const float target = std::clamp(desired,std::min(lo,hi),std::max(lo,hi));
        const float targetScreen = (aim-target)*nextZoom;
        const float screen = previousScreen + (targetScreen-previousScreen)*blend;
        const float position = aim-screen/nextZoom + recoil;
        const float finalPosition = std::clamp(position,std::min(lo,hi),std::max(lo,hi));
        positionSettled = positionSettled && std::abs(finalPosition-target)*nextZoom <= Tuning::CameraSettlePixels;
        return finalPosition;
    };
    cameraX_ = axis(playerX,aimX,viewWidth,std::max(world_->GetMapPixelW(),world_->GetImageW()),screenAimX,shake.x);
    cameraY_ = axis(playerY,aimY,viewHeight,std::max(world_->GetMapPixelH(),world_->GetImageH()),screenAimY,shake.y);
    zoom_ = nextZoom;
    visionRadius_ = VisibilityQuery::RadiusForViewport(viewportW,viewportH,baseZoom) / zoomMultiplier;
    vis_.SetVisionRadius(visionRadius_);
    cameraSettled_ = positionSettled && std::abs(zoom_-targetZoom) < .001f;
    cameraInitialized_ = true;
}

bool CS2DRenderer::BeginScopeScene(float aimX, float aimY, float centerX, float centerY,
                                  float magnification) {
    if (!renderer_) return false;
    if (!scopeSceneTex_ || scopeSceneW_ != viewW_ || scopeSceneH_ != viewH_) {
        if (scopeSceneTex_) SDL_DestroyTexture(scopeSceneTex_);
        scopeSceneTex_ = SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET,viewW_,viewH_);
        scopeSceneW_ = viewW_;
        scopeSceneH_ = viewH_;
        if (!scopeSceneTex_) return false;
        SDL_SetTextureBlendMode(scopeSceneTex_,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(scopeSceneTex_,SDL_ScaleModeLinear);
    }
    scopeReturnTarget_ = SDL_GetRenderTarget(renderer_);
    SDL_RenderGetViewport(renderer_,&scopeReturnViewport_);
    SDL_RenderGetClipRect(renderer_,&scopeReturnClip_);
    scopeReturnClipped_ = SDL_RenderIsClipEnabled(renderer_);
    SDL_RenderGetScale(renderer_,&scopeReturnScale_.x,&scopeReturnScale_.y);
    if (SDL_SetRenderTarget(renderer_,scopeSceneTex_) != 0) return false;
    SDL_RenderSetViewport(renderer_,nullptr);
    SDL_RenderSetClipRect(renderer_,nullptr);
    scopeMainCamera_ = {cameraX_,cameraY_,zoom_};
    scopeMainFogMs_ = lastFogMs_;
    zoom_ *= magnification;
    cameraX_ = aimX-centerX/zoom_;
    cameraY_ = aimY-centerY/zoom_;
    SDL_SetRenderDrawColor(renderer_,18,26,36,255);
    SDL_RenderClear(renderer_);
    return true;
}

void CS2DRenderer::EndScopeScene() {
    SDL_SetRenderTarget(renderer_,scopeReturnTarget_);
    SDL_RenderSetScale(renderer_,scopeReturnScale_.x,scopeReturnScale_.y);
    SDL_RenderSetViewport(renderer_,&scopeReturnViewport_);
    SDL_RenderSetClipRect(renderer_,scopeReturnClipped_ ? &scopeReturnClip_ : nullptr);
    cameraX_ = scopeMainCamera_[0];
    cameraY_ = scopeMainCamera_[1];
    zoom_ = scopeMainCamera_[2];
    lastFogMs_ += scopeMainFogMs_;
}

void CS2DRenderer::DrawScopeScene(float centerX, float centerY, float radius, float alpha) {
    if (!scopeSceneTex_ || radius <= 0) return;
    constexpr int segments = 128;
    std::array<SDL_Vertex,segments+2> vertices{};
    std::array<int,segments*3> indices{};
    const SDL_Color color{255,255,255,static_cast<Uint8>(255*std::clamp(alpha,0.0f,1.0f))};
    vertices[0] = {{centerX,centerY},color,{centerX/viewW_,centerY/viewH_}};
    for (int i=0;i<=segments;++i) {
        const float angle = i * Tuning::Pi * 2 / segments;
        const float x = centerX+std::cos(angle)*radius, y = centerY+std::sin(angle)*radius;
        vertices[i+1] = {{x,y},color,{x/viewW_,y/viewH_}};
        if (i<segments) {
            indices[i*3]=0;
            indices[i*3+1]=i+1;
            indices[i*3+2]=i+2;
        }
    }
    SDL_RenderGeometry(renderer_,scopeSceneTex_,vertices.data(),vertices.size(),indices.data(),indices.size());
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

void CS2DRenderer::LoadPerimeter(const std::string& imagePath) {
    const auto resolved = std::filesystem::path(PathUtils::ResolveAppPath(imagePath));
    const auto directory = resolved.parent_path().parent_path() / "perimeters";
    const std::string stem = resolved.stem().string();
    const auto scenePath = directory / (stem + "_scene.png");
    const auto groundPath = directory / (stem + "_ground.png");

    // A material-matched fallback also covers legacy maps without authored scenery.
    SDL_Surface* surface = IMG_Load(resolved.string().c_str());
    if (surface) {
        SDL_Surface* rgb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(surface);
        if (rgb) {
            uint64_t red = 0, green = 0, blue = 0, count = 0;
            for (int y = 0; y < rgb->h; y += std::max(1, rgb->h / 32)) {
                const auto* row = static_cast<const Uint8*>(rgb->pixels) + y * rgb->pitch;
                for (int x = 0; x < rgb->w; x += std::max(1, rgb->w / 32)) {
                    const auto* pixel = row + x * 4;
                    if (std::max({pixel[0], pixel[1], pixel[2]}) < 24) continue;
                    red += pixel[0]; green += pixel[1]; blue += pixel[2]; ++count;
                }
            }
            if (count) perimeterColor_ = {static_cast<Uint8>(red / count),
                static_cast<Uint8>(green / count), static_cast<Uint8>(blue / count), 255};
            SDL_FreeSurface(rgb);
        }
    }

    if (!std::filesystem::exists(scenePath) || !std::filesystem::exists(groundPath)) return;
    SDL_Texture* scene = IMG_LoadTexture(renderer_, scenePath.string().c_str());
    SDL_Texture* ground = IMG_LoadTexture(renderer_, groundPath.string().c_str());
    int mapW = 0, mapH = 0, sceneW = 0, sceneH = 0, tileW = 0, tileH = 0;
    SDL_QueryTexture(backgroundTex_, nullptr, nullptr, &mapW, &mapH);
    const bool valid = scene && ground && mapW > 0 && mapH > 0 &&
        SDL_QueryTexture(scene, nullptr, nullptr, &sceneW, &sceneH) == 0 &&
        SDL_QueryTexture(ground, nullptr, nullptr, &tileW, &tileH) == 0 &&
        tileW > 0 && tileH > 0 && sceneW > mapW && sceneH > mapH &&
        (sceneW-mapW) % 2 == 0 && (sceneH-mapH) % 2 == 0;
    if (!valid) {
        if (scene) SDL_DestroyTexture(scene);
        if (ground) SDL_DestroyTexture(ground);
        std::cerr << "[CS2DRenderer] Invalid perimeter bundle: " << stem << std::endl;
        return;
    }
    const float scaleX = world_->GetImageW() / mapW;
    const float scaleY = world_->GetImageH() / mapH;
    perimeterWorldRect_ = {-(sceneW-mapW)*.5f*scaleX, -(sceneH-mapH)*.5f*scaleY,
                            sceneW*scaleX, sceneH*scaleY};
    perimeterTileW_ = tileW*scaleX;
    perimeterTileH_ = tileH*scaleY;
    perimeterTex_ = scene;
    perimeterGroundTex_ = ground;
    perimeterDust_ = stem == "dust_2";
    SDL_SetTextureScaleMode(scene, SDL_ScaleModeLinear);
    SDL_SetTextureScaleMode(ground, SDL_ScaleModeLinear);
    std::cout << "[CS2DRenderer] Perimeter ready: " << stem << std::endl;
}

void CS2DRenderer::DrawPerimeterGround() {
    SDL_BlendMode previousBlend;
    SDL_GetRenderDrawBlendMode(renderer_, &previousBlend);
    SDL_SetRenderDrawColor(renderer_, perimeterColor_.r, perimeterColor_.g, perimeterColor_.b, 255);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_RenderFillRect(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, previousBlend);
    if (!perimeterGroundTex_) return;
    const int firstX = static_cast<int>(std::floor(cameraX_ / perimeterTileW_));
    const int firstY = static_cast<int>(std::floor(cameraY_ / perimeterTileH_));
    const int lastX = static_cast<int>(std::ceil((cameraX_ + viewW_/zoom_) / perimeterTileW_));
    const int lastY = static_cast<int>(std::ceil((cameraY_ + viewH_/zoom_) / perimeterTileH_));
    for (int y = firstY; y < lastY; ++y) {
        for (int x = firstX; x < lastX; ++x) {
            // Quantize shared endpoints together to avoid cracks at fractional zoom.
            const int left = static_cast<int>(std::lround((x*perimeterTileW_-cameraX_)*zoom_));
            const int top = static_cast<int>(std::lround((y*perimeterTileH_-cameraY_)*zoom_));
            const int right = static_cast<int>(std::lround(((x+1)*perimeterTileW_-cameraX_)*zoom_));
            const int bottom = static_cast<int>(std::lround(((y+1)*perimeterTileH_-cameraY_)*zoom_));
            const SDL_Rect dst{left, top, right-left, bottom-top};
            SDL_RenderCopy(renderer_, perimeterGroundTex_, nullptr, &dst);
        }
    }
}

void CS2DRenderer::DrawImageBackground() {
    if (!backgroundTex_ || !world_) return;
    if (perimeterTex_) {
        const SDL_FRect dst{(perimeterWorldRect_.x-cameraX_)*zoom_,
            (perimeterWorldRect_.y-cameraY_)*zoom_, perimeterWorldRect_.w*zoom_, perimeterWorldRect_.h*zoom_};
        SDL_RenderCopyF(renderer_, perimeterTex_, nullptr, &dst);
    }
    const float viewW = world_->GetImageW();
    const float viewH = world_->GetImageH();
    const SDL_FRect dst{-cameraX_ * zoom_, -cameraY_ * zoom_, viewW * zoom_, viewH * zoom_};
    SDL_RenderCopyF(renderer_, backgroundTex_, nullptr, &dst);
}

void CS2DRenderer::DrawPerimeterDust() {
    if (!perimeterDust_ || !smokeTex_) return;
    Uint8 red, green, blue, alpha;
    SDL_GetTextureColorMod(smokeTex_, &red, &green, &blue);
    SDL_GetTextureAlphaMod(smokeTex_, &alpha);
    SDL_SetTextureColorMod(smokeTex_, 225, 204, 151);
    const double seconds = SDL_GetTicks64()/1000.0;
    const float width = world_->GetImageW();
    const float height = world_->GetImageH();
    for (int side = 0; side < 4; ++side) {
        for (int puff = 0; puff < 3; ++puff) {
            const float phase = static_cast<float>(std::fmod(seconds*.012+puff/3.0+side*.19, 1.0));
            const float drift = std::sin(phase*6.2831853f)*6;
            // Whole sprites stay beyond the source rectangle, clear of gameplay.
            const bool vertical = side < 2;
            const float x = vertical ? (side == 0 ? -80 : width+80)+drift : -80+(width+160)*phase;
            const float y = vertical ? -40+(height+80)*phase : (side == 2 ? -64 : height+64)+drift;
            const SDL_FRect dst{(x-48-cameraX_)*zoom_, (y-18-cameraY_)*zoom_, 96*zoom_, 36*zoom_};
            SDL_SetTextureAlphaMod(smokeTex_, static_cast<Uint8>(12*std::sin(phase*3.14159265f)));
            SDL_RenderCopyF(renderer_, smokeTex_, nullptr, &dst);
        }
    }
    SDL_SetTextureColorMod(smokeTex_, red, green, blue);
    SDL_SetTextureAlphaMod(smokeTex_, alpha);
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
    DrawPerimeterGround();
    DrawImageBackground();
    DrawPerimeterDust();
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

void CS2DRenderer::MaskedCopy(SDL_Texture *texture, const SDL_Rect *source, const SDL_FRect &dst,
                              float angle) {
    if (!renderer_ || !texture || dst.w <= 0 || dst.h <= 0)
        return;
    if (!visibilityFilter_) {
        SDL_RenderCopyExF(renderer_, texture, source, &dst, angle, nullptr, SDL_FLIP_NONE);
        return;
    }
    int tw = 0, th = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th) != 0 || tw <= 0 || th <= 0)
        return;
    const SDL_Rect src = source ? *source : SDL_Rect{0, 0, tw, th};
    const float rad = angle * .01745329252f, c = std::cos(rad), s = std::sin(rad);
    const float cx = dst.x + dst.w * .5f, cy = dst.y + dst.h * .5f;
    const float ex = (std::abs(c) * dst.w + std::abs(s) * dst.h) * .5f;
    const float ey = (std::abs(s) * dst.w + std::abs(c) * dst.h) * .5f;
    Uint8 red = 255, green = 255, blue = 255, alpha = 255;
    SDL_GetTextureColorMod(texture, &red, &green, &blue);
    SDL_GetTextureAlphaMod(texture, &alpha);
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    auto uv = [&](float x, float y) {
        const float u = ((x - cx) * c + (y - cy) * s) / dst.w + .5f;
        const float v = (-(x - cx) * s + (y - cy) * c) / dst.h + .5f;
        return SDL_FPoint{(src.x + u * src.w) / tw, (src.y + v * src.h) / th};
    };
    const int left = std::max(0, static_cast<int>(cx - ex));
    const int right = std::min(viewW_, static_cast<int>(std::ceil(cx + ex)));
    auto visible = [&](int x, int y) {
        const auto point = uv(x + .5f, y + .5f);
        if (point.x < static_cast<float>(src.x) / tw || point.y < static_cast<float>(src.y) / th ||
            point.x >= static_cast<float>(src.x + src.w) / tw ||
            point.y >= static_cast<float>(src.y + src.h) / th)
            return false;
        auto &cached = (visibilityObserver_ ? observerVisibilityPixels_ : visibilityPixels_)[static_cast<size_t>(y) * viewW_ + x];
        if (cached < 0)
            cached = visibilityFilter_(cameraX_ + (x + .5f) / zoom_, cameraY_ + (y + .5f) / zoom_) ? 1 : 0;
        return cached != 0;
    };
    // Each contiguous visible row is one textured quad, with pixel-exact clipping.
    for (int y = std::max(0, static_cast<int>(cy - ey));
         y < std::min(viewH_, static_cast<int>(std::ceil(cy + ey))); ++y) {
        int x = left;
        while (x < right) {
            while (x < right && !visible(x, y))
                ++x;
            const int start = x;
            while (x < right && visible(x, y))
                ++x;
            if (start == x)
                continue;
            const int base = static_cast<int>(vertices.size());
            const SDL_FPoint corners[] = {{static_cast<float>(start), static_cast<float>(y)},
                                          {static_cast<float>(x), static_cast<float>(y)},
                                          {static_cast<float>(x), y + 1.f},
                                          {static_cast<float>(start), y + 1.f}};
            for (const auto point : corners) {
                auto coordinate = uv(point.x, point.y);
                coordinate.x = std::clamp(coordinate.x, static_cast<float>(src.x) / tw,
                                          static_cast<float>(src.x + src.w) / tw);
                coordinate.y = std::clamp(coordinate.y, static_cast<float>(src.y) / th,
                                          static_cast<float>(src.y + src.h) / th);
                vertices.push_back({point, {red, green, blue, alpha}, coordinate});
            }
            const int quad[] = {base, base + 1, base + 2, base, base + 2, base + 3};
            indices.insert(indices.end(), std::begin(quad), std::end(quad));
        }
    }
    if (!vertices.empty())
        SDL_RenderGeometry(renderer_, texture, vertices.data(), static_cast<int>(vertices.size()),
                           indices.data(), static_cast<int>(indices.size()));
}

void CS2DRenderer::DrawCharacter(const SpriteDrawParams &p) {
    // 选序列帧 sheet
    const SpriteSheetDef *sheet = nullptr;
    if (p.spriteKey == 0)
        sheet = &ctSheet_;
    else if (p.spriteKey == 1)
        sheet = &tSheet_;
    else
        sheet = &tAltSheet_;

    if (!sheet || !sheet->tex)
        return;

    // ---- 按武器槽固定切片（死亡用主武器帧 + 倾斜淡出） ----
    int frameCol = 0, frameRow = 0; // 默认：第1行第1列（主武器）
    if (p.animState != 3) {         // 非死亡状态才按槽位切帧
        switch (p.weaponSlot) {
        case 4:
        case 1:
            frameCol = 1;
            frameRow = 1;
            break; // 副武器：第2行第2列
        case 2:
        case 3:
            frameCol = 0;
            frameRow = 1;
            break; // 近战：  第2行第1列
        default:
            frameCol = 0;
            frameRow = 0;
            break; // 主武器：第1行第1列
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
        if (alpha < 5)
            return;
    }

    // 行走不再做上下 bobbing：角色与瞄准线共用同一个世界锚点，
    // 任何纵向偏移都会让准星与人物不同步（玩家反馈的「人跟着晃」）。
    const float yOff = 0.0f;

    // 射击后坐力（世界单位）
    float xOff = 0.0f;
    if (p.animState == 2) {
        xOff = -std::max(0.0f, 1.0f - p.animTimer * 6.0f) * 1.6f * zoom_;
    }

    SDL_FRect dst;
    dst.x = cx - sz * 0.5f + xOff;
    dst.y = cy - sz * 0.5f + yOff;
    dst.w = sz;
    dst.h = sz;

    SDL_SetTextureAlphaMod(sheet->tex, alpha);
    SDL_FPoint center = {sz * 0.5f, sz * 0.5f};
    // 最新三套人物的全部姿势均朝上，统一旋转到实际瞄准方向。
    constexpr float poseAngleOffset = 90.0f;
    const float charAngle =
        p.aimAngle * 180.0f / 3.14159265f + tiltDeg + (p.animState == 3 ? 0.0f : poseAngleOffset);

    // ---- 敌我轮廓素材（紧贴角色剪影）----
    // 友军蓝 / 敌军红 / 自己白；白色描边素材 + ColorMod，所以一份素材服务三色。
    // 先画轮廓再画角色，轮廓被本体盖住内侧，只剩贴边的一圈。
    {
        const SpriteSheetDef *outline = nullptr;
        if (p.spriteKey == 0)
            outline = &ctOutlineSheet_;
        else if (p.spriteKey == 1)
            outline = &tOutlineSheet_;
        else
            outline = &tAltOutlineSheet_;

        Uint8 rr = 236, gg = 88, bb = 78; // 敌军：红
        if (p.localPlayer) {
            rr = 240;
            gg = 244;
            bb = 250;
        } // 自己：白
        else if (p.friendly) {
            rr = 88;
            gg = 168;
            bb = 245;
        } // 友军：蓝

        if (outline && outline->tex && p.animState != 3) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
            // 最近邻采样：1px 描边不会被双线性插值糊成 2px
            SDL_SetTextureScaleMode(outline->tex, SDL_ScaleModeNearest);
#endif
            SDL_SetTextureColorMod(outline->tex, rr, gg, bb);
            // 略降不透明度：1px 描边在放大后不会显得"重"
            SDL_SetTextureAlphaMod(outline->tex, static_cast<Uint8>(alpha * 0.82f));
            // 轮廓素材本身就是外扩一圈，绘制框与本体相同即可贴合
            const auto characterVisibility = visibilityFilter_;
            if (characterVisibility)
                SetVisibilityFilter([this, p, characterVisibility](float x, float y) {
                    if (!characterVisibility(x, y))
                        return false;
                    const float step = 1 / std::max(.01f, zoom_);
                    for (int oy = -1; oy <= 1; ++oy)
                        for (int ox = -1; ox <= 1; ++ox) {
                            const float px = x + ox * step, py = y + oy * step;
                            if (BodyGeometry::Contains(p.spriteKey, p.weaponSlot, p.x, p.y, p.size,
                                                       p.aimAngle, p.animState, p.animTimer, px, py) &&
                                characterVisibility(px, py))
                                return true;
                        }
                    return false;
                });
            MaskedCopy(outline->tex, &src, dst, charAngle);
            if (characterVisibility)
                SetVisibilityFilter(characterVisibility);
            SDL_SetTextureColorMod(outline->tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(outline->tex, 255);
        }
    }

    MaskedCopy(sheet->tex, &src, dst, charAngle);
    SDL_SetTextureAlphaMod(sheet->tex, 255);

    // 武器
    if (p.animState != 3) {
        DrawWeaponForCharacter(cx + xOff, cy + yOff, p.size, p.aimAngle, p.weaponSlot, p.weaponId,
                               p.animState == 2, p.meleeSwing);
    }
}
// 枪口火焰：cx/cy 为武器中心的屏幕坐标
namespace {
// Cached Gaussian profile shared by the scope and world-space auxiliary line.
void DrawSoftSegment(SDL_Renderer* renderer, SDL_FPoint a, SDL_FPoint b,
                     float thickness, float blur, SDL_Color color) {
    const float length = std::hypot(b.x-a.x,b.y-a.y);
    if (length < .001f || thickness <= 0 || color.a == 0) return;
    const float nx = -(b.y-a.y)/length, ny = (b.x-a.x)/length;
    const float coverage = std::min(1.0f, thickness);
    const float half = std::max(1.0f, thickness)*.5f;
    const float feather = .85f + std::max(0.0f,blur);
    const float offsets[] = {-half-feather,-half,half,half+feather};
    const Uint8 coreAlpha = static_cast<Uint8>(std::lround(color.a*coverage /
        (1.0f+std::max(0.0f,blur)*.25f)));
    std::array<SDL_Vertex,8> vertices{};
    std::array<int,18> indices{};
    for (int i=0;i<4;++i) {
        SDL_Color sample = color;
        sample.a = i == 0 || i == 3 ? 0 : coreAlpha;
        vertices[i*2] = {{a.x+nx*offsets[i],a.y+ny*offsets[i]},sample,{0,0}};
        vertices[i*2+1] = {{b.x+nx*offsets[i],b.y+ny*offsets[i]},sample,{0,0}};
        if (i<3) {
            const int n=i*2;
            const int quad[]={n,n+1,n+3,n,n+3,n+2};
            std::copy(std::begin(quad),std::end(quad),indices.begin()+i*6);
        }
    }
    SDL_RenderGeometry(renderer,nullptr,vertices.data(),vertices.size(),indices.data(),indices.size());
}
}

void CS2DRenderer::DrawAimLinePreview(const AimLineStyle& style, float width, float height) {
    const float split=width*.55f, y=height*.5f;
    DrawSoftSegment(renderer_,{20,y},{split,y},style.solidThickness*g_uiScale,0,style.solid);
    const float dash=style.dashLength*g_uiScale, gap=style.gapLength*g_uiScale;
    for (float x=split;x<width-20;x+=dash+gap)
        DrawSoftSegment(renderer_,{x,y},{std::min(x+dash,width-20),y},style.hiddenThickness*g_uiScale,0,style.hidden);
}

void CS2DRenderer::DrawAimLine(float playerX, float playerY, float angle,
                                 const AimLineStyle& inputStyle, float uiScale, float blur) {
    if (!renderer_ || !world_) return;
    // A ray begins at the actor and extends beyond every visible viewport corner.
    const float muzzleX = playerX, muzzleY = playerY;
    const float viewWorldW = viewW_ / std::max(zoom_, 0.001f);
    const float viewWorldH = viewH_ / std::max(zoom_, 0.001f);
    const float farX = std::max(std::abs(cameraX_ - playerX), std::abs(cameraX_ + viewWorldW - playerX));
    const float farY = std::max(std::abs(cameraY_ - playerY), std::abs(cameraY_ + viewWorldH - playerY));
    const float rayLength = std::hypot(farX, farY) + viewWorldW + viewWorldH;
    const float targetX = playerX + std::cos(angle) * rayLength;
    const float targetY = playerY + std::sin(angle) * rayLength;
    const float fraction = vis_.FirstBulletWallFraction(muzzleX, muzzleY, targetX, targetY);
    const float startX = (muzzleX - cameraX_) * zoom_, startY = (muzzleY - cameraY_) * zoom_;
    const float endX = (targetX - cameraX_) * zoom_, endY = (targetY - cameraY_) * zoom_;
    const float dx = endX - startX, dy = endY - startY;
    const float length = std::hypot(dx, dy);
    if (length <= 0.0f) return;
    AimLineStyle style = inputStyle;
    style.Normalize();
    uiScale = std::isfinite(uiScale) ? std::max(uiScale, 0.1f) : 1.0f;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    auto segment = [&](float from, float to, float thickness, SDL_Color color) {
        if (to <= from || color.a == 0) return;
        const SDL_FPoint a{startX+dx*from,startY+dy*from};
        const SDL_FPoint b{startX+dx*to,startY+dy*to};
        DrawSoftSegment(renderer_,a,b,thickness*uiScale,blur*uiScale,color);
    };
    segment(0.0f, fraction, style.solidThickness, style.solid);
    for (float distance = length * fraction; distance < length;
         distance += (style.dashLength + style.gapLength) * uiScale) {
        const float from = distance / length;
        const float to = std::min(distance + style.dashLength * uiScale, length) / length;
        segment(from, to, style.hiddenThickness, style.hidden);
    }
}

void CS2DRenderer::DrawMuzzleFlash(float cx, float cy, float size, float aimAngle,
                                   int weaponSlot) {
    const float weaponSz = (weaponSlot == 1) ? Tuning::PistolDrawSize
                         : (weaponSlot == 2) ? 9.0f
                         : Tuning::PrimaryDrawSize;
    // 枪口位置 = 角色中心 + 瞄准方向 * (武器偏移 + 武器长度*/
    const float tip = size * Tuning::WeaponOffsetMul + weaponSz * Tuning::MuzzleLengthMul;
    const float mx = cx + std::cos(aimAngle) * tip * zoom_;
    const float my = cy + std::sin(aimAngle) * tip * zoom_;

    if (visibilityFilter_ && !visibilityFilter_(cameraX_+mx/zoom_,cameraY_+my/zoom_)) return;
    const float r = 5.2f * zoom_;

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
                                          int weaponSlot, int weaponId, bool muzzleFlash, float meleeSwing) {
    if (weaponId <= 0 || weaponId >= static_cast<int>(weaponSprites_.size())) return;
    SDL_Texture* tex = weaponSprites_[weaponId];
    if (!tex) return;

    const WeaponDef& def = WeaponDB::Get(static_cast<WeaponId>(weaponId));
    const float degOffset = def.degOffset;
    const float weaponSz  = (weaponSlot == 1 || weaponSlot == 4) ? Tuning::PistolDrawSize
                          : (weaponSlot == 2) ? 9.0f
                          : (weaponSlot == 3) ? 4.4f   // 投掷物手持：缩小
                          : Tuning::PrimaryDrawSize;

    // 武器绘制在精灵前方偏移
    const float offset = size * 0.35f * zoom_;
    const bool melee = def.cls == WeaponClass::Melee;
    const float rightOffset = melee ? size*Tuning::MeleeGripRightMul*zoom_ : 0;
    const float forwardOffset = melee ? size*Tuning::MeleeGripForwardMul*zoom_ : offset;
    const float weaponAngle = aimAngle + (melee ? std::sin(std::clamp(meleeSwing,0.0f,1.0f)*3.14159265f)*.6f : 0);
    const float extension = melee ? weaponSz*Tuning::MeleeGripToCenterMul*zoom_ : 0;
    const float wx = cx + std::cos(aimAngle)*forwardOffset - std::sin(aimAngle)*rightOffset + std::cos(weaponAngle)*extension;
    const float wy = cy + std::sin(aimAngle)*forwardOffset + std::cos(aimAngle)*rightOffset + std::sin(weaponAngle)*extension;
    const float drawSz = weaponSz * zoom_;

    int textureW = 0, textureH = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &textureW, &textureH) != 0 || textureW <= 0 || textureH <= 0) return;
    const float scale = drawSz / static_cast<float>(std::max(textureW, textureH));
    const float drawW = textureW * scale;
    const float drawH = textureH * scale;
    SDL_FRect wdst{wx - drawW * 0.5f, wy - drawH * 0.5f, drawW, drawH};
    SDL_FPoint wcenter{drawW * 0.5f, drawH * 0.5f};
    MaskedCopy(tex,nullptr,wdst,weaponAngle * 180.0f / 3.14159265f + degOffset);

    // 开枪瞬间的枪口火焰（近战/投掷物不画）
    if (muzzleFlash && def.cls != WeaponClass::Melee && def.cls != WeaponClass::Grenade) {
        DrawMuzzleFlash(cx, cy, size, aimAngle, weaponSlot);   // wx/wy 已是屏幕坐标
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
        // 雷达底图要和选阵营界面一样带上「地图边缘延伸」。
        // perimeterTex_ 是同一套 perimeter 场景图，比地图本体大一圈；
        // 但它不能超出 512 纹理，所以按「地图本体在该纹理里的缩放」等比缩放，
        // 并以地图本体的左上角为锚点向外铺——延伸部分自然落在 letterbox 之外的留白上。
        if (perimeterTex_) {
            const float mapScale = iw / std::max(1.0f, mapW);   // 世界px → 雷达px
            SDL_Rect dst = {
                static_cast<int>(std::lround(radarImgX_ + perimeterWorldRect_.x * mapScale)),
                static_cast<int>(std::lround(radarImgY_ + perimeterWorldRect_.y * mapScale)),
                static_cast<int>(std::lround(perimeterWorldRect_.w * mapScale)),
                static_cast<int>(std::lround(perimeterWorldRect_.h * mapScale)) };
            SDL_SetTextureBlendMode(perimeterTex_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(perimeterTex_, 165);
            SDL_RenderCopy(renderer_, perimeterTex_, nullptr, &dst);
            SDL_SetTextureAlphaMod(perimeterTex_, 255);
        }
        // 地图本体压在延伸之上（本体不透明，延伸只在外圈可见）
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
        if (!visibilityFilter_ || visibilityFilter_(cameraX_+(px+nx)*.5f/zoom_,cameraY_+(py+ny)*.5f/zoom_)) SDL_RenderDrawLineF(renderer_, px, py, nx, ny);
        px = nx; py = ny;
    }
}

void CS2DRenderer::DrawCircleFilled(float worldX, float worldY, float radius, Uint8 r, Uint8 g, Uint8 b,
                                    Uint8 a, int segments) {
    if (!renderer_ || segments < 3)
        return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);

    const float cx = (worldX - cameraX_) * zoom_;
    const float cy = (worldY - cameraY_) * zoom_;
    const float rr = radius * zoom_;

    if (visibilityFilter_) {
        std::vector<SDL_Vertex> pixels;
        std::vector<int> indices;
        for (int y = std::max(0, static_cast<int>(cy - rr));
             y < std::min(viewH_, static_cast<int>(std::ceil(cy + rr))); ++y)
            for (int x = std::max(0, static_cast<int>(cx - rr));
                 x < std::min(viewW_, static_cast<int>(std::ceil(cx + rr))); ++x) {
                if (std::hypot(x + .5f - cx, y + .5f - cy) > rr ||
                    !visibilityFilter_(cameraX_ + (x + .5f) / zoom_, cameraY_ + (y + .5f) / zoom_))
                    continue;
                const int base = static_cast<int>(pixels.size());
                const SDL_Color color{r, g, b, a};
                pixels.push_back({{static_cast<float>(x), static_cast<float>(y)}, color, {0, 0}});
                pixels.push_back({{x + 1.f, static_cast<float>(y)}, color, {0, 0}});
                pixels.push_back({{x + 1.f, y + 1.f}, color, {0, 0}});
                pixels.push_back({{static_cast<float>(x), y + 1.f}, color, {0, 0}});
                const int quad[] = {base, base + 1, base + 2, base, base + 2, base + 3};
                indices.insert(indices.end(), std::begin(quad), std::end(quad));
            }
        if (!pixels.empty())
            SDL_RenderGeometry(renderer_, nullptr, pixels.data(), static_cast<int>(pixels.size()),
                               indices.data(), static_cast<int>(indices.size()));
        return;
    }
    // 三角形扇
    float px = cx + rr, py = cy;
    for (int i = 1; i <= segments; ++i) {
        const float ang = 6.28318531f * static_cast<float>(i) / static_cast<float>(segments);
        const float nx = cx + std::cos(ang) * rr;
        const float ny = cy + std::sin(ang) * rr;
        SDL_Vertex v[3];
        v[0].position = {cx, cy};
        v[0].color = {r, g, b, a};
        v[0].tex_coord = {0, 0};
        v[1].position = {px, py};
        v[1].color = {r, g, b, a};
        v[1].tex_coord = {0, 0};
        v[2].position = {nx, ny};
        v[2].color = {r, g, b, a};
        v[2].tex_coord = {0, 0};
        SDL_RenderGeometry(renderer_, nullptr, v, 3, nullptr, 0);
        px = nx;
        py = ny;
    }
}

// ============================================================================
// 掉落武器（用 _d 图标，与 HUD 状态栏同一套素材）
// ============================================================================

void CS2DRenderer::DrawDroppedWeapon(int weaponId, float worldX, float worldY,
                                     float angleDeg) {
    if (!renderer_ || weaponId <= 0) return;

    // 地面掉落使用主视图，与购买菜单和 HUD 共用图标。
    SDL_Texture* tex = nullptr;
    if (weaponId < static_cast<int>(weaponIcons_.size())) tex = weaponIcons_[weaponId];
    if (!tex && weaponId < static_cast<int>(weaponSprites_.size())) tex = weaponSprites_[weaponId];
    if (!tex) return;

    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    // 统一放进约 9 个世界单位的方框，保持宽高比
    const float box = 11.0f * zoom_;
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
    MaskedCopy(tex,nullptr,dst,angleDeg);

}
// ============================================================================
// 目标物标记（包点 / C4）
// ============================================================================

void CS2DRenderer::DrawObjectiveMarkers(float timeSec) {
    if (!world_ || !renderer_) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // 包点保留淡色地面填充。
    (void)timeSec;
    for (const auto& site : world_->GetBombSites()) {
        for (const auto& area : site.rects) {
            SDL_FRect rect{(area.x - cameraX_) * zoom_, (area.y - cameraY_) * zoom_,
                           area.width * zoom_, area.height * zoom_};
            SDL_SetRenderDrawColor(renderer_, 240, 190, 60, 34);
            SDL_RenderFillRectF(renderer_, &rect);
        }
    }
}



void CS2DRenderer::DrawBulletVisuals(
    const std::vector<Presentation::BulletVisualSegment>& segments) {
    if (!renderer_ || segments.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const auto& s : segments) {
        const float life = (s.durationSeconds > 0.0f)
                               ? std::clamp(s.remainingSeconds / s.durationSeconds, 0.0f, 1.0f)
                               : 0.0f;
        if (life <= 0.0f) continue;

        // ---- 按阻挡瓦片裁剪（步长 2px，命中即截断到上一格）----
        float toX = s.toX, toY = s.toY;
        bool clippedByWall = false;
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
                    clippedByWall = true;
                    break;
                }
            }
        }

        // ---- 屏幕坐标 ----
        float x0 = (s.fromX - cameraX_) * zoom_;
        float y0 = (s.fromY - cameraY_) * zoom_;
        float x1 = (toX - cameraX_) * zoom_;
        float y1 = (toY - cameraY_) * zoom_;

        // 长度上限 24 逻辑像素：只保留靠近弹头的一段（拖尾）
        constexpr float kMaxLen = 24.0f;
        float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        if (len > kMaxLen) {
            const float k = kMaxLen / len;
            x0 = x1 - (x1 - x0) * k;
            y0 = y1 - (y1 - y0) * k;
            len = kMaxLen;
        }
        if (len < 0.5f) {
            // 极短段（高速弹在当帧只剩一点点位移）：至少画一个亮点，避免"看不见"
            len = 0.5f;
        }

        const float dirX = (x1 - x0) / std::max(0.001f, len);
        const float dirY = (y1 - y0) / std::max(0.001f, len);
        const float nx = -dirY;
        const float ny = dirX;

        // 宽度：1.5~3 逻辑像素（不乘 zoom，避免高倍率下变成粗线）
        const float widthPx = 1.5f + 1.5f * life;
        const int   lines = std::max(1, static_cast<int>(std::lround(widthPx)));

        // 颜色：暖白弹头，命中段偏金黄；射程到期段偏暗
        const Uint8 r = 255;
        const Uint8 g = s.impact ? 226 : 214;
        const Uint8 b = s.impact ? 168 : 150;

        // 拖尾：3 段递增 alpha（尾淡 → 头亮）
        const int kTail = 3;
        for (int seg = 0; seg < kTail; ++seg) {
            const float t0 = static_cast<float>(seg) / static_cast<float>(kTail);
            const float t1 = static_cast<float>(seg + 1) / static_cast<float>(kTail);
            const float a0 = life * 0.20f + life * 0.80f * t0;
            const float a1 = life * 0.20f + life * 0.80f * t1;
            const float ax = x1 - dirX * len * (1.0f - t0);
            const float ay = y1 - dirY * len * (1.0f - t0);
            const float bx = x1 - dirX * len * (1.0f - t1);
            const float by = y1 - dirY * len * (1.0f - t1);
            const Uint8 alpha = static_cast<Uint8>(235.0f * (a0 + a1) * 0.5f);
            SDL_SetRenderDrawColor(renderer_, r, g, b, alpha);
            const int segLines = std::max(1, static_cast<int>(std::lround(widthPx * (0.6f + 0.4f * t1))));
            for (int i = 0; i < segLines; ++i) {
                const float off = static_cast<float>(i) - (static_cast<float>(segLines) - 1.0f) * 0.5f;
                SDL_RenderDrawLineF(renderer_, ax + nx * off, ay + ny * off,
                                    bx + nx * off, by + ny * off);
            }
        }

        // 弹头亮点（最后 60% 寿命内更明显）
        if (life > 0.25f) {
            const float dotR = std::clamp(widthPx * 0.9f, 1.0f, 2.6f);
            const Uint8 dotA = static_cast<Uint8>(255.0f * std::min(1.0f, life * 1.6f));
            SDL_SetRenderDrawColor(renderer_, 255, 246, 214, dotA);
            for (int i = 0; i <= static_cast<int>(lines); ++i) {
                const float off = static_cast<float>(i) - static_cast<float>(lines) * 0.5f;
                SDL_RenderDrawLineF(renderer_, x1 + nx * off - dirX * dotR,
                                    y1 + ny * off - dirY * dotR,
                                    x1 + nx * off + dirX * dotR,
                                    y1 + ny * off + dirY * dotR);
            }
        }

        // 命中火花：只在 impact 段的寿命前 40% 内画一小簇（不宣称精确命中点）
        // 被墙截断的段与真实角色命中都给同样的视觉反馈，符合"碰撞终点检测"的数据能力。
        if (s.impact && life > 0.6f) {
            const float k = (life - 0.6f) / 0.4f;         // 1 → 0
            const float spark = 2.0f + 4.0f * k;
            const Uint8 a = static_cast<Uint8>(200.0f * k);
            SDL_SetRenderDrawColor(renderer_, 255, 232, 170, a);
            for (int i = 0; i < 4; ++i) {
                const float ang = 0.7853982f + 1.5707963f * static_cast<float>(i); // 45° 十字
                SDL_RenderDrawLineF(renderer_, x1, y1,
                                    x1 + std::cos(ang) * spark, y1 + std::sin(ang) * spark);
            }
            if (clippedByWall) {
                // 撞墙：再补一小片尘土（低饱和），与击中角色的金色火花区分
                SDL_SetRenderDrawColor(renderer_, 196, 188, 176, static_cast<Uint8>(120.0f * k));
                for (int i = 0; i < 3; ++i) {
                    const float ang = -0.6f + 0.6f * static_cast<float>(i);
                    SDL_RenderDrawLineF(renderer_, x1, y1,
                                        x1 - dirX * spark * std::cos(ang) + nx * spark * 0.5f,
                                        y1 - dirY * spark * std::cos(ang) + ny * spark * 0.5f);
                }
            }
        }
    }
}

void CS2DRenderer::SetFogOpacityPct(int pct) {
    fogOpacityPct_ = std::clamp(pct, 0, 100);
}

int CS2DRenderer::EffectiveFogAlpha() const {
    const int baseAlpha = VISION_FOG_ALPHA;
    return std::clamp(static_cast<int>(std::lround(
                          static_cast<float>(baseAlpha) *
                          (static_cast<float>(fogOpacityPct_) / 100.0f))),
                      0, 255);
}

void CS2DRenderer::DrawScopeOverlay(const Presentation::ScopeOverlayParams& p) {
    if (!renderer_) return;
    const float t = std::clamp(p.scope.transition, 0.0f, 1.0f);
    const float radiusPx = p.radiusPx;
    if (t <= 0.001f || radiusPx <= 1.0f) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const float cx = p.centerX;
    const float cy = p.centerY;
    const float r  = radiusPx * t;   // 开镜瞬间由小放大到镜筒尺寸

    // Cover every pixel outside the lens, including the viewport corners.
    SDL_SetRenderDrawColor(renderer_, 6, 8, 10, static_cast<Uint8>(VISION_FOG_ALPHA * t));
    for (int y = 0; y < viewH_; ++y) {
        const float dy = y + 0.5f - cy;
        if (std::abs(dy) >= r) {
            SDL_RenderDrawLine(renderer_, 0, y, viewW_ - 1, y);
        } else {
            const float half = std::sqrt(r * r - dy * dy);
            const int left = static_cast<int>(std::floor(cx - half));
            const int right = static_cast<int>(std::ceil(cx + half));
            if (left > 0) SDL_RenderDrawLine(renderer_, 0, y, left - 1, y);
            if (right < viewW_) SDL_RenderDrawLine(renderer_, right, y, viewW_ - 1, y);
        }
    }

    // 屏幕空间圆：SDL 没有线宽，用多段折线近似
    auto ring = [&](float rr, int segments, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a) {
        if (rr <= 0.5f) return;
        SDL_SetRenderDrawColor(renderer_, cr, cg, cb, a);
        for (int i = 0; i < segments; ++i) {
            const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(segments);
            SDL_RenderDrawLineF(renderer_, cx + std::cos(a0) * rr, cy + std::sin(a0) * rr,
                                cx + std::cos(a1) * rr, cy + std::sin(a1) * rr);
        }
    };

    // 1) 向内收的暗角：让圆边缘看起来有"镜筒厚度"（不在圆外重复遮挡）
    for (int i = 0; i < 10; ++i) {
        const float k = static_cast<float>(i) / 9.0f;            // 0=最外
        const float rr = r * (1.0f - 0.055f * k);
        const Uint8 a = static_cast<Uint8>((150.0f * (1.0f - k) + 25.0f) * t);
        ring(rr, 76, 10, 12, 15, a);
    }
    // 2) 外圈暗环（3 圈）形成硬边
    for (int i = 0; i < 3; ++i) {
        ring(r + 1.0f + static_cast<float>(i) * 1.4f, 76, 6, 8, 10,
             static_cast<Uint8>(215.0f * t));
    }
    // 3) 内缘细亮线
    ring(r - 0.6f, 76, 214, 228, 238, static_cast<Uint8>(150.0f * t));

    // 4) 刻度：8 个方向，四个正向更长（借用望远镜分划的视觉语言）
    {
        const Uint8 tickA = static_cast<Uint8>(190.0f * t);
        SDL_SetRenderDrawColor(renderer_, 226, 236, 244, tickA);
        for (int i = 0; i < 8; ++i) {
            const float ang = 6.2831853f * static_cast<float>(i) / 8.0f;
            const float len = (i % 2 == 0) ? 14.0f : 7.0f;
            const float inner = r - 6.0f;
            const float outer = inner - len;
            SDL_RenderDrawLineF(renderer_,
                                cx + std::cos(ang) * inner, cy + std::sin(ang) * inner,
                                cx + std::cos(ang) * outer, cy + std::sin(ang) * outer);
        }
    }

    const float gap = std::clamp(p.crossGap,0.0f,r);
    const float thickness = std::clamp(p.crossThickness,1.0f,8.0f) * g_uiScale;
    const float blur = p.motionBlur * g_uiScale;
    auto cross = [&](float width, SDL_Color color) {
        DrawSoftSegment(renderer_,{cx,cy-r},{cx,cy-gap},width,blur,color);
        DrawSoftSegment(renderer_,{cx,cy+gap},{cx,cy+r},width,blur,color);
        DrawSoftSegment(renderer_,{cx-r,cy},{cx-gap,cy},width,blur,color);
        DrawSoftSegment(renderer_,{cx+gap,cy},{cx+r,cy},width,blur,color);
    };
    cross(thickness+2,{8,10,12,static_cast<Uint8>(120*t)});
    cross(thickness,{240,246,250,static_cast<Uint8>(225*t)});

    // 6) 档位提示：2x 时在右下内侧多一小段刻度，不用文字（渲染层没有字体）
    if (p.scope.level >= 2) {
        const float bx = cx + r * 0.72f;
        const float by = cy + r * 0.72f;
        SDL_SetRenderDrawColor(renderer_, 240, 205, 130, static_cast<Uint8>(200.0f * t));
        for (int i = 0; i < 2; ++i) {
            const float off = static_cast<float>(i) * 4.0f;
            SDL_RenderDrawLineF(renderer_, bx + off, by, bx + off, by + 7.0f);
        }
    }
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


    const float cx = (worldX - cameraX_) * zoom_;
    const float cy = (worldY - cameraY_) * zoom_;
    const float box = 6.0f * zoom_;   // 投出状态：缩小到原来的 3/7
    int width = 0, height = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &width, &height) != 0 || width <= 0 || height <= 0) return;
    const float fit = box / static_cast<float>(std::max(width, height));
    const float drawW = width * fit, drawH = height * fit;
    SDL_FRect dst{cx - drawW * 0.5f, cy - drawH * 0.5f, drawW, drawH};
    const SDL_FPoint center{drawW * 0.5f, drawH * 0.5f};
    MaskedCopy(tex,nullptr,dst,spinDeg);
}

void CS2DRenderer::DrawCrosshair(float cx, float cy,
                                  Uint8 r, Uint8 g, Uint8 b, Uint8 a,
                                  float thickness, bool centerDot,
                                  float length, float gap) {
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    const SDL_Color color{r,g,b,a};
    DrawSoftSegment(renderer_,{cx-gap-length,cy},{cx-gap,cy},thickness,0,color);
    DrawSoftSegment(renderer_,{cx+gap,cy},{cx+gap+length,cy},thickness,0,color);
    DrawSoftSegment(renderer_,{cx,cy-gap-length},{cx,cy-gap},thickness,0,color);
    DrawSoftSegment(renderer_,{cx,cy+gap},{cx,cy+gap+length},thickness,0,color);
    if (centerDot) {
        constexpr int segments=32;
        const float radius=1.5f*g_uiScale;
        for (int i=0;i<segments;++i) {
            const float a0=6.283185307f*i/segments, a1=6.283185307f*(i+1)/segments;
            const SDL_Color clear{r,g,b,0};
            const SDL_Vertex vertices[]={{{cx,cy},color,{0,0}},
                {{cx+std::cos(a0)*radius,cy+std::sin(a0)*radius},color,{0,0}},
                {{cx+std::cos(a1)*radius,cy+std::sin(a1)*radius},color,{0,0}},
                {{cx+std::cos(a0)*(radius+.75f),cy+std::sin(a0)*(radius+.75f)},clear,{0,0}},
                {{cx+std::cos(a1)*(radius+.75f),cy+std::sin(a1)*(radius+.75f)},clear,{0,0}}};
            const int indices[]={0,1,2,1,3,4,1,4,2};
            SDL_RenderGeometry(renderer_,nullptr,vertices,5,indices,9);
        }
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

// 几何判定已抽到 VisibilityQuery（不依赖 SDL），渲染器只做转发。
// 见 CS2D/Visibility/VisibilityQuery.hpp：无头仿真与渲染共用同一份实现。
bool CS2DRenderer::HasLineOfSight(float fromX, float fromY, float toX, float toY) const {
    return vis_.HasLineOfSight(fromX, fromY, toX, toY);
}

float CS2DRenderer::ComputeVisibility(float playerX, float playerY, float aimAngle,
                                     float targetX, float targetY) const {
    return vis_.ComputeVisibility(playerX, playerY, aimAngle, targetX, targetY);
}
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
    // 视觉半径盖住判定半径之外的一圈：判定仍用原始有效半径，
    // 这样「看得到的烟」一定覆盖「判定被挡住的区域」，不会出现判定/视觉错位。
    const float r = Tuning::SmokeEffectiveRadius(worldRadius,age,expandTime,life) *
                    Tuning::SmokeVisualRadiusMul * zoom_;
    if (r < 1.5f) return;

    // 出现时快速淡入，最后 2 秒淡出
    const float fadeIn  = std::clamp(age / 0.28f, 0.0f, 1.0f);
    const float fadeOut = std::clamp(life / std::min(2.0f, std::max(0.1f, maxLife)), 0.0f, 1.0f);
    const float k = fadeIn * fadeOut;
    if (k <= 0.01f) return;

    auto drawPuff = [&](float ox, float oy, float scale, float rot, float alphaMul) {
        const float size = r * 2.0f * std::min(scale,std::max(.01f,.98f-std::hypot(ox,oy)/r));
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

void CS2DRenderer::EnsureFireTiles() {
    static const char* kFireTilePaths[3] = {
        "assets/cs2d/sprites/fire_tile_1.png",
        "assets/cs2d/sprites/fire_tile_2.png",
        "assets/cs2d/sprites/fire_tile_3.png",
    };
    for (int i = 0; i < 3; ++i) {
        if (!fireTileTex_[i]) fireTileTex_[i] = LoadTexture(kFireTilePaths[i]);
    }
}

void CS2DRenderer::DrawFireCells(const std::vector<SDL_FPoint> &cells, float x, float y, float radius,
                                 float age, float life, float seed) {
    const float activeRadius = Tuning::FireEffectiveRadius(radius, age, life);
    const float fade = std::clamp(life / .6f, 0.0f, 1.0f);
    if (activeRadius <= 0.5f || fade <= 0.01f) return;

    // 火区地面层：三张无缝火焰贴图拼贴。
    // 「一整套火焰舌」的实时程序化渲染已下线——现在火焰完全走地图贴图逻辑
    // （贴图只用地图层的方式平铺在地面，不再叠一层矢量火焰与火星）。
    EnsureFireTiles();
    const bool haveTiles = fireTileTex_[0] && fireTileTex_[1] && fireTileTex_[2];

    const float fadeIn = std::clamp(age / 0.30f, 0.0f, 1.0f);
    const float k = fade * fadeIn;
    if (k <= 0.01f) return;

    // 单块贴图的世界尺寸：既不能碎成满屏小块，也不能大到只剩几块巨型贴图。
    // 面积下限：(π·r²)/64  →  tile >= r·√(π/64) ≈ 0.2216·r。
    // 在此基础上再按 PIX-38 要求把面积缩到 1/16（线尺寸 ×1/4）：
    //   0.26·r / 4 = 0.065·r，面积约为圆面积的 1/(64·16) ≈ 1/1024。
    // 火区判定半径（FireRadius / FireEffectiveRadius）完全不变。
    const float minTile = activeRadius * 0.065f;
    float tile = std::max(4.0f, minTile);
    tile = std::ceil(tile / 2.0f) * 2.0f;

    // 按 cell 收集有火的格子，再归并到 tile 网格（一格贴图可覆盖多个 cell）
    struct FireTileSlot { int tx = 0, ty = 0; bool visible = false; };
    std::vector<FireTileSlot> tiles;
    for (const auto cell : cells) {
        if (std::hypot(cell.x - x, cell.y - y) > activeRadius) continue;
        const int tx = static_cast<int>(std::floor(cell.x / tile));
        const int ty = static_cast<int>(std::floor(cell.y / tile));
        int slotIdx = -1;
        for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
            if (tiles[i].tx == tx && tiles[i].ty == ty) { slotIdx = i; break; }
        }
        if (slotIdx < 0) {
            tiles.push_back({tx, ty, false});
            slotIdx = static_cast<int>(tiles.size()) - 1;
        }
        if (!visibilityFilter_ || visibilityFilter_(cell.x, cell.y)) tiles[slotIdx].visible = true;
    }
    if (!haveTiles || tiles.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const auto& t : tiles) {
        if (!t.visible) continue;
        const float wx = t.tx * tile, wy = t.ty * tile;
        const float sx = (wx - cameraX_) * zoom_, sy = (wy - cameraY_) * zoom_;
        const float sw = tile * zoom_;
        if (sx + sw < -8 * zoom_ || sx > viewW_ + 8 * zoom_ ||
            sy + sw < -4 * zoom_ || sy > viewH_ + 26 * zoom_) continue;
        const int variant = ((t.tx * 7 + t.ty * 13 + static_cast<int>(seed)) % 3 + 3) % 3;
        const float flicker = 0.8f + 0.2f * std::sin(age * 9.0f + t.tx * 2.1f + t.ty * 1.7f + seed);
        const Uint8 alpha = static_cast<Uint8>(std::clamp(k * flicker, 0.0f, 1.0f) * 255.0f);
        if (alpha == 0) continue;
        SDL_Texture* tex = fireTileTex_[variant];
        SDL_SetTextureAlphaMod(tex, alpha);
        const SDL_FRect dst{sx, sy, sw, sw};
        SDL_RenderCopyF(renderer_, tex, nullptr, &dst);
    }
}

void CS2DRenderer::DrawFlashOverlay(Uint8 alpha) {
    if (!renderer_ || alpha == 0) return;
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, alpha);
    SDL_Rect full = { 0, 0, viewW_, viewH_ };
    SDL_RenderFillRect(renderer_, &full);
}

void CS2DRenderer::DrawBackpackC4(float worldX, float worldY, float size, float aimAngle,
                                  float timeSec) {
    if (!renderer_ || (visibilityFilter_ && !visibilityFilter_(worldX, worldY))) return;
    const int c4Id = static_cast<int>(WeaponId::C4);
    SDL_Texture* tex = nullptr;
    if (c4Id < static_cast<int>(weaponIcons_.size())) tex = weaponIcons_[c4Id];
    if (!tex && c4Id < static_cast<int>(weaponSprites_.size())) tex = weaponSprites_[c4Id];
    if (!tex) return;
    int tw = 0, th = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th) != 0 || tw <= 0 || th <= 0) return;

    // 背在身后：沿朝向的反方向偏移到人物背部。
    // 尺寸刻意小于人物盒（0.62），否则背包会把整个角色盖住，看不出是谁在带包。
    const float cx = (worldX - cameraX_) * zoom_;
    const float cy = (worldY - cameraY_) * zoom_;
    const float back = size * zoom_ * 0.34f;
    const float px = cx - std::cos(aimAngle) * back;
    const float py = cy - std::sin(aimAngle) * back;
    const float box = size * zoom_ * 0.62f;
    float dw = box, dh = box;
    if (tw >= th) dh = box * static_cast<float>(th) / static_cast<float>(tw);
    else          dw = box * static_cast<float>(tw) / static_cast<float>(th);
    // 轻微呼吸感，提示这是「活的携带者」而不是地面贴图
    const float breathe = 1.0f + 0.04f * std::sin(timeSec * 3.0f);
    dw *= breathe;
    dh *= breathe;
    const SDL_FRect dst{px - dw * 0.5f, py - dh * 0.5f, dw, dh};
    // 背包随人物朝向转动（素材原生朝上，与角色同一套 90° 偏移）。
    // 刻意不用 MaskedCopy：那条路径按人物 alpha 几何逐像素剔除，
    // 会把背包切出一堆方块缺口（背包比角色剪影宽）。整块贴图随可见性即可。
    const float deg = aimAngle * 180.0f / 3.14159265f + 90.0f;
    const SDL_FPoint pivot{dw * 0.5f, dh * 0.5f};
    SDL_RenderCopyExF(renderer_, tex, nullptr, &dst, deg, &pivot, SDL_FLIP_NONE);
}

// 命中反馈：准星处画一个 X（四条短斜线）
void CS2DRenderer::DrawHitMarker(float screenX, float screenY, float scale, Uint8 alpha, SDL_Color color) {
    if (!renderer_ || alpha == 0) return;

    const float inner = 4.0f * scale;
    const float outer = 9.0f * scale;

    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, alpha);
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
    const VisibilityQuery::Observer observer{playerX, playerY, aimAngle};
    DrawVisionFogUnion(&observer, 1);
}

void CS2DRenderer::DrawVisionFogUnion(const VisibilityQuery::Observer* observers, std::size_t count) {
    const Uint64 fogStart = SDL_GetPerformanceCounter();
    lastFogMs_ = 0.0f;
    if (!visionTex_ || visionPixels_.empty() || !world_ || !observers || count == 0) return;

    // 契约 v1.2：雾 alpha 再乘用户设置的透明度百分比（只影响视觉，不影响可见性判定）
    const int baseAlpha = VISION_FOG_ALPHA;
    const float pctScale = static_cast<float>(std::clamp(fogOpacityPct_, 0, 100)) / 100.0f;
    const Uint8 maxFogAlpha = static_cast<Uint8>(
        std::lround(static_cast<float>(baseAlpha) * pctScale));

    for (int y = 0; y < maskH_; ++y) {
        for (int x = 0; x < maskW_; ++x) {
            const float worldX = cameraX_ + (x + 0.5f) * VISION_MASK_SCALE / zoom_;
            const float worldY = cameraY_ + (y + 0.5f) * VISION_MASK_SCALE / zoom_;

            // 与敌人剔除共用同一套几何，避免遮罩与可见性漂移。
            // 多观察者（观战自由视角）取并集最大值：任一队友看得见即点亮。
            const float vis = vis_.ComputeVisibilityUnion(observers, count, worldX, worldY);
            const Uint8 alpha = static_cast<Uint8>(std::lround((1.0f - vis) * maxFogAlpha));

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
    lastFogMs_ = static_cast<float>((SDL_GetPerformanceCounter() - fogStart) * 1000.0 / SDL_GetPerformanceFrequency());
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
    const std::vector<RadarDot>& dots, float mapW, float mapH,
    bool playerCentered, bool rotateWithPlayer, bool blendBg, int bgAlpha,
    int hudSize, float mapZoom, bool showCenterRadar, int viewW, int viewH, bool fullMap, SDL_Texture* previewMap) {
    if (!renderer_ || mapW <= 0 || mapH <= 0) return;
    const int size = std::clamp(hudSize, 24, std::max(24, std::min(viewW, viewH) - 16));
    const float hudScale = fullMap ? viewH / kDesignH : g_uiScale;
    const int margin = std::max(4, static_cast<int>(std::lround(8 * hudScale)));
    const float x = showCenterRadar ? (viewW-size)*.5f : static_cast<float>(margin);
    const float y = showCenterRadar ? (viewH-size)*.5f : static_cast<float>(margin);
    const float half = size*.5f;
    const int frameIndex = showCenterRadar || fullMap ? 1 : 0;
    auto*& frameTexture = radarFrameTex_[frameIndex];
    if (!frameTexture || radarFrameSize_[frameIndex] != size) {
        if (frameTexture) SDL_DestroyTexture(frameTexture);
        frameTexture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, size, size);
        radarFrameSize_[frameIndex] = size;
        if (!frameTexture) return;
        SDL_SetTextureBlendMode(frameTexture, SDL_BLENDMODE_BLEND);
    }
    SDL_Texture* oldTarget = SDL_GetRenderTarget(renderer_);
    SDL_Rect oldViewport, oldClip;
    SDL_RenderGetViewport(renderer_, &oldViewport);
    SDL_RenderGetClipRect(renderer_, &oldClip);
    const bool clipped = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
    SDL_BlendMode oldBlend;
    SDL_GetRenderDrawBlendMode(renderer_, &oldBlend);
    Uint8 oldR, oldG, oldB, oldA;
    SDL_GetRenderDrawColor(renderer_, &oldR, &oldG, &oldB, &oldA);
    float oldScaleX, oldScaleY;
    SDL_RenderGetScale(renderer_, &oldScaleX, &oldScaleY);
    SDL_SetRenderTarget(renderer_, frameTexture);
    SDL_RenderSetViewport(renderer_, nullptr);
    SDL_RenderSetClipRect(renderer_, nullptr);
    SDL_RenderSetScale(renderer_, 1, 1);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, fullMap ? 15 : 0, fullMap ? 19 : 0, fullMap ? 25 : 0, fullMap ? 225 : 0);
    SDL_RenderClear(renderer_);
    const float zoom = mapZoom >= .5f && mapZoom <= 8 ? mapZoom : 1;
    const float scale = size * zoom / std::max(mapW, mapH);
    const float centerX = playerCentered ? playerX : mapW*.5f;
    const float centerY = playerCentered ? playerY : mapH*.5f;
    const float angle = rotateWithPlayer ? -playerAngle - 1.570796327f : 0;
    const float cs = std::cos(angle), sn = std::sin(angle);
    auto point = [&](float wx, float wy) {
        const float dx = (wx-centerX)*scale, dy = (wy-centerY)*scale;
        return SDL_FPoint{half + dx*cs-dy*sn, half + dx*sn+dy*cs};
    };
    // Draw the whole map with one affine transform, including map-edge blank space.
    SDL_Texture* mapTexture = previewMap ? previewMap : radarMapTex_;
    if (mapTexture) {
        int tw = 0, th = 0;
        SDL_QueryTexture(mapTexture, nullptr, nullptr, &tw, &th);
        const SDL_Color color{255,255,255,static_cast<Uint8>(blendBg ? std::clamp(bgAlpha,0,255) : 255)};
        const float u0 = previewMap ? 0 : radarImgX_/tw, v0 = previewMap ? 0 : radarImgY_/th;
        const float u1 = previewMap ? 1 : (radarImgX_+radarImgW_)/tw, v1 = previewMap ? 1 : (radarImgY_+radarImgH_)/th;
        const SDL_Vertex vertices[] = {{point(0,0),color,{u0,v0}}, {point(mapW,0),color,{u1,v0}},
            {point(mapW,mapH),color,{u1,v1}}, {point(0,mapH),color,{u0,v1}}};
        const int indices[] = {0,1,2,0,2,3};
        SDL_BlendMode textureBlend;
        Uint8 tr,tg,tb,ta;
        SDL_GetTextureBlendMode(mapTexture, &textureBlend);
        SDL_GetTextureColorMod(mapTexture, &tr,&tg,&tb);
        SDL_GetTextureAlphaMod(mapTexture, &ta);
        SDL_SetTextureBlendMode(mapTexture, SDL_BLENDMODE_NONE);
        SDL_SetTextureColorMod(mapTexture,255,255,255);
        SDL_SetTextureAlphaMod(mapTexture,255);
        SDL_RenderGeometry(renderer_,mapTexture,vertices,4,indices,6);
        SDL_SetTextureBlendMode(mapTexture, textureBlend);
        SDL_SetTextureColorMod(mapTexture,tr,tg,tb);
        SDL_SetTextureAlphaMod(mapTexture,ta);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    auto dot = [&](SDL_FPoint p, float radius, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
        for (int row = -static_cast<int>(std::ceil(radius)); row <= static_cast<int>(std::ceil(radius)); ++row) {
            const float extent = std::sqrt(std::max(0.0f,radius*radius-row*row));
            SDL_RenderDrawLineF(renderer_,p.x-extent,p.y+row,p.x+extent,p.y+row);
        }
    };
    const float dotRadius = std::clamp(1.5f+.5f*(zoom-1),1.5f,4.5f) * std::max(.7f,hudScale);
    const auto self = point(playerX,playerY);
    dot(self,dotRadius+2,{220,240,255,255});
    dot(self,dotRadius+1,{8,12,16,255});
    dot(self,dotRadius,{110,215,255,255});
    for (const auto& d : dots) {
        const auto p = point(d.x,d.y);
        if (d.shape == RadarDot::Shape::Circle) {
            dot(p,dotRadius+1,{8,12,16,d.alpha});
            dot(p,dotRadius,{d.r,d.g,d.b,d.alpha});
            continue;
        }
        const float radius = std::max(3.5f, 5.0f*hudScale);
        // 携带者与掉落的 C4 都用真实 C4 素材的小图标表示（取代旧的菱形/方块标记）。
        // 携带者图标叠在本方小圆点之上，掉落物则单独一枚略大的图标。
        if (d.shape == RadarDot::Shape::Carrier || d.shape == RadarDot::Shape::DroppedBomb) {
            const int c4Id = static_cast<int>(WeaponId::C4);
            SDL_Texture* icon = nullptr;
            if (c4Id < static_cast<int>(weaponIcons_.size())) icon = weaponIcons_[c4Id];
            if (!icon && c4Id < static_cast<int>(weaponSprites_.size())) icon = weaponSprites_[c4Id];
            const bool dropped = d.shape == RadarDot::Shape::DroppedBomb;
            if (dropped) {
                dot(p, dotRadius + 1, {8, 12, 16, d.alpha});
                dot(p, dotRadius, {255, 170, 60, d.alpha});
            }
            if (icon) {
                int tw = 0, th = 0;
                if (SDL_QueryTexture(icon, nullptr, nullptr, &tw, &th) == 0 && tw > 0 && th > 0) {
                    // 「非常小」：携带者约 7px，掉落物约 10px（随 HUD 缩放）
                    const float box = (dropped ? 10.0f : 7.0f) * std::max(.75f, hudScale);
                    float dw = box, dh = box;
                    if (tw >= th) dh = box * static_cast<float>(th) / static_cast<float>(tw);
                    else          dw = box * static_cast<float>(tw) / static_cast<float>(th);
                    SDL_SetTextureBlendMode(icon, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(icon, d.alpha);
                    const SDL_FRect dst{p.x - dw * 0.5f, p.y - dh * 0.5f, dw, dh};
                    SDL_RenderCopyF(renderer_, icon, nullptr, &dst);
                    SDL_SetTextureAlphaMod(icon, 255);
                }
            } else {
                // 素材缺失兜底：保留一个橙色小方块，绝不回退成旧菱形
                const SDL_FRect rect{p.x-radius*.6f,p.y-radius*.6f,radius*1.2f,radius*1.2f};
                SDL_SetRenderDrawColor(renderer_,d.r,d.g,d.b,d.alpha);
                SDL_RenderFillRectF(renderer_,&rect);
            }
            if (!dropped) continue;
        }
        const float pulse = d.shape == RadarDot::Shape::DroppedBomb ?
            1.5f + 2.0f*(.5f+.5f*std::sin(SDL_GetTicks()*0.005f)) : 2.0f;
        const float ring = radius+pulse;
        SDL_SetRenderDrawColor(renderer_,d.r,d.g,d.b,static_cast<Uint8>(d.alpha*.7f));
        for (int i=0;i<40;++i) {
            const float a=i*6.2831853f/40, b=(i+1)*6.2831853f/40;
            SDL_RenderDrawLineF(renderer_,p.x+std::cos(a)*ring,p.y+std::sin(a)*ring,
                p.x+std::cos(b)*ring,p.y+std::sin(b)*ring);
        }
    }
    SDL_SetRenderTarget(renderer_,oldTarget);
    SDL_RenderSetScale(renderer_,oldScaleX,oldScaleY);
    SDL_RenderSetViewport(renderer_,&oldViewport);
    SDL_RenderSetClipRect(renderer_,clipped ? &oldClip : nullptr);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    // Circular texture mesh with an antialiased outer ring clips every radar layer.
    constexpr int segments = 128;
    std::array<SDL_Vertex,1+segments*2> mesh{};
    std::array<int,segments*9> indices{};
    const float radius = half-1;
    mesh[0] = {{x+half,y+half},{255,255,255,255},{.5f,.5f}};
    for (int i=0; i<segments; ++i) {
        const float a = 6.283185307f*i/segments;
        for (int ring=0;ring<2;++ring) {
            const float rr = radius+ring;
            mesh[1+i*2+ring] = {{x+half+std::cos(a)*rr,y+half+std::sin(a)*rr},
                {255,255,255,static_cast<Uint8>(ring ? 0 : 255)},
                {.5f+std::cos(a)*rr/size,.5f+std::sin(a)*rr/size}};
        }
        const int inner=1+i*2, outer=inner+1, next=1+((i+1)%segments)*2;
        const int tris[] = {0,inner,next,inner,outer,next+1,inner,next+1,next};
        std::copy(std::begin(tris),std::end(tris),indices.begin()+i*9);
    }
    SDL_RenderGeometry(renderer_,frameTexture,mesh.data(),static_cast<int>(mesh.size()),indices.data(),static_cast<int>(indices.size()));
    SDL_SetRenderDrawColor(renderer_,130,150,168,220);
    for (int i=0;i<segments;++i) {
        const float a=6.283185307f*i/segments, b=6.283185307f*(i+1)/segments;
        SDL_RenderDrawLineF(renderer_,x+half+std::cos(a)*radius,y+half+std::sin(a)*radius,
            x+half+std::cos(b)*radius,y+half+std::sin(b)*radius);
    }
    SDL_SetRenderDrawColor(renderer_,oldR,oldG,oldB,oldA);
    SDL_SetRenderDrawBlendMode(renderer_,oldBlend);
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
