#include "Menu3D.hpp"
#include "../Core/PathUtils.hpp"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {

constexpr float kPi = 3.14159265358979323846f;

void DrawCube(float cx, float cy, float cz,
              float sx, float sy, float sz,
              float r, float g, float b) {
    const float x0 = cx - sx * 0.5f, x1 = cx + sx * 0.5f;
    const float y0 = cy - sy * 0.5f, y1 = cy + sy * 0.5f;
    const float z0 = cz - sz * 0.5f, z1 = cz + sz * 0.5f;
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    // +X
    glNormal3f(1, 0, 0);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z1); glVertex3f(x1, y1, z0);
    // -X
    glNormal3f(-1, 0, 0);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0);
    glVertex3f(x0, y1, z0); glVertex3f(x0, y1, z1);
    // +Y
    glNormal3f(0, 1, 0);
    glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
    // -Y
    glNormal3f(0, -1, 0);
    glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1);
    glVertex3f(x1, y0, z0); glVertex3f(x0, y0, z0);
    // +Z
    glNormal3f(0, 0, 1);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
    glVertex3f(x1, y1, z1); glVertex3f(x1, y0, z1);
    // -Z
    glNormal3f(0, 0, -1);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x0, y1, z0); glVertex3f(x0, y0, z0);
    glEnd();
}

void DrawSphere(float cx, float cy, float cz, float radius,
                float r, float g, float b, int slices = 12, int stacks = 8) {
    glColor3f(r, g, b);
    for (int i = 0; i < stacks; ++i) {
        const float lat0 = -kPi * 0.5f + kPi * static_cast<float>(i) / stacks;
        const float lat1 = -kPi * 0.5f + kPi * static_cast<float>(i + 1) / stacks;
        const float y0 = std::sin(lat0) * radius;
        const float y1 = std::sin(lat1) * radius;
        const float rr0 = std::cos(lat0) * radius;
        const float rr1 = std::cos(lat1) * radius;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; ++j) {
            const float lon = 2.0f * kPi * static_cast<float>(j) / slices;
            const float x = std::cos(lon), z = std::sin(lon);
            glNormal3f(x * std::cos(lat0), std::sin(lat0), z * std::cos(lat0));
            glVertex3f(cx + x * rr0, cy + y0, cz + z * rr0);
            glNormal3f(x * std::cos(lat1), std::sin(lat1), z * std::cos(lat1));
            glVertex3f(cx + x * rr1, cy + y1, cz + z * rr1);
        }
        glEnd();
    }
}

void DrawCharacter(float x, float z, bool ct, float bob) {
    const float r = ct ? 0.24f : 0.78f;
    const float g = ct ? 0.45f : 0.45f;
    const float b = ct ? 0.82f : 0.20f;
    const float y = 1.15f + bob;

    // legs
    DrawCube(x - 0.17f, 0.35f, z, 0.20f, 0.70f, 0.24f, 0.12f, 0.14f, 0.17f);
    DrawCube(x + 0.17f, 0.35f, z, 0.20f, 0.70f, 0.24f, 0.12f, 0.14f, 0.17f);
    // torso + vest
    DrawCube(x, y, z, 0.72f, 0.86f, 0.36f, r, g, b);
    DrawCube(x, y + 0.03f, z - 0.205f, 0.56f, 0.58f, 0.10f, 0.10f, 0.12f, 0.15f);
    // arms
    DrawCube(x - 0.49f, y - 0.02f, z, 0.16f, 0.66f, 0.20f, r * 0.75f, g * 0.75f, b * 0.75f);
    DrawCube(x + 0.49f, y - 0.02f, z, 0.16f, 0.66f, 0.20f, r * 0.75f, g * 0.75f, b * 0.75f);
    // head + helmet
    DrawSphere(x, y + 0.66f, z, 0.24f, 0.68f, 0.52f, 0.40f);
    DrawSphere(x, y + 0.72f, z, 0.255f, r * 0.55f, g * 0.55f, b * 0.55f, 12, 5);
    // simple rifle silhouette
    DrawCube(x + (ct ? -0.12f : 0.12f), y - 0.04f, z - 0.30f,
             0.72f, 0.08f, 0.08f, 0.10f, 0.10f, 0.11f);
}

void DrawChicken(float x, float z, float phase) {
    const float bob = std::sin(phase) * 0.035f;
    const float y = 0.28f + bob;
    DrawSphere(x, y, z, 0.24f, 0.92f, 0.78f, 0.36f, 10, 6);
    DrawSphere(x, y + 0.21f, z - 0.02f, 0.14f, 0.98f, 0.86f, 0.46f, 10, 6);
    DrawCube(x, y + 0.21f, z - 0.16f, 0.09f, 0.07f, 0.10f, 0.95f, 0.48f, 0.15f);
    // comb / tail
    DrawCube(x, y + 0.36f, z, 0.06f, 0.10f, 0.10f, 0.85f, 0.20f, 0.16f);
    DrawCube(x, y + 0.02f, z + 0.22f, 0.10f, 0.18f, 0.08f, 0.75f, 0.58f, 0.22f);
}

void DrawShadow(float x, float z, float rx, float rz, float alpha) {
    glDisable(GL_LIGHTING);
    glColor4f(0.0f, 0.0f, 0.0f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, 0.012f, z);
    constexpr int kSegments = 24;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        glVertex3f(x + std::cos(a) * rx, 0.012f, z + std::sin(a) * rz);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

} // namespace

bool Menu3D::Initialize(SDL_Renderer* mainRenderer, int width, int height) {
    Shutdown();
    if (!mainRenderer || width <= 0 || height <= 0) {
        lastError_ = "invalid renderer/size";
        return false;
    }
    mainRenderer_ = mainRenderer;
    width_ = width;
    height_ = height;

    if (!InitGL()) {
        Shutdown();
        return false;
    }

    LoadModels();

    texture_ = SDL_CreateTexture(mainRenderer_, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STREAMING, width_, height_);
    if (!texture_) {
        lastError_ = std::string("SDL_CreateTexture failed: ") + SDL_GetError();
        Shutdown();
        return false;
    }
    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture_, SDL_ScaleModeLinear);

    pixels_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
    flipped_.assign(pixels_.size(), 0);
    animationTime_ = 0.0f;
    lastRenderTicks_ = 0;
    ready_ = true;

    std::cout << "[Menu3D] 实时 3D 背景就绪 (" << width_ << "x" << height_
              << ", GL " << reinterpret_cast<const char*>(glGetString(GL_VERSION))
              << ")" << std::endl;
    return true;
}

bool Menu3D::InitGL() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);

    glWindow_ = SDL_CreateWindow("Menu3D",
                                 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 width_, height_,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (!glWindow_) {
        lastError_ = std::string("SDL_CreateWindow(OpenGL) failed: ") + SDL_GetError();
        return false;
    }

    glContext_ = SDL_GL_CreateContext(glWindow_);
    if (!glContext_) {
        lastError_ = std::string("SDL_GL_CreateContext failed: ") + SDL_GetError();
        return false;
    }
    if (SDL_GL_MakeCurrent(glWindow_, glContext_) != 0) {
        lastError_ = std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError();
        return false;
    }
    SDL_GL_SetSwapInterval(0);

    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glShadeModel(GL_SMOOTH);

    const GLfloat ambient[] = { 0.20f, 0.22f, 0.26f, 1.0f };
    const GLfloat diffuse[] = { 1.00f, 0.92f, 0.78f, 1.0f };
    const GLfloat position[] = { -4.0f, 8.0f, 6.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    return true;
}

void Menu3D::Shutdown() {
    ready_ = false;
    tModel_.reset();
    ctModel_.reset();
    chickens_.clear();
    if (texture_) { SDL_DestroyTexture(texture_); texture_ = nullptr; }
    if (glContext_) { SDL_GL_DeleteContext(glContext_); glContext_ = nullptr; }
    if (glWindow_) { SDL_DestroyWindow(glWindow_); glWindow_ = nullptr; }
    mainRenderer_ = nullptr;
    pixels_.clear();
    flipped_.clear();
}

bool Menu3D::LoadModels() {
    tModel_ = std::make_unique<Menu3DModel>();
    if (!tModel_->Load("assets/models/quaternius/Character_Enemy.gltf", "Idle")) {
        std::cerr << "[Menu3D] T 模型加载失败: " << tModel_->LastError() << std::endl;
        tModel_.reset();
    }

    ctModel_ = std::make_unique<Menu3DModel>();
    if (!ctModel_->Load("assets/models/quaternius/Character_Soldier.gltf", "Idle")) {
        std::cerr << "[Menu3D] CT 模型加载失败: " << ctModel_->LastError() << std::endl;
        ctModel_.reset();
    } else {
        ctModel_->SetBluePalette(true);
    }

    chickens_.clear();
    for (int i = 0; i < 3; ++i) {
        auto chicken = std::make_unique<Menu3DModel>();
        if (!chicken->Load("assets/models/chickensoft/chicken.glb", "walk")) {
            std::cerr << "[Menu3D] 小鸡模型加载失败: " << chicken->LastError() << std::endl;
            chickens_.clear();
            break;
        }
        // 错开三只鸡的动画相位
        chicken->Update(0.37f * static_cast<float>(i));
        chickens_.push_back(std::move(chicken));
    }

    if (tModel_ && ctModel_) {
        std::cout << "[Menu3D] glTF 角色已加载: T=" << tModel_->AnimationName()
                  << " CT=" << ctModel_->AnimationName()
                  << " 小鸡=" << chickens_.size() << std::endl;
    }
    InitChickenState();
    return tModel_ && ctModel_;
}

void Menu3D::InitChickenState() {
    const float xs[3] = { -2.35f, 0.0f, 2.25f };
    const float zs[3] = { 3.2f, 7.0f, 3.0f };
    for (int i = 0; i < 3; ++i) {
        ChickenState& s = chickenState_[i];
        s.x = xs[i];
        s.z = zs[i];
        s.yaw = 0.0f;
        s.speed = 0.75f + (chickenRng_() % 100) / 200.0f;
        PickChickenTarget(i);
    }
}

void Menu3D::PickChickenTarget(int index) {
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    ChickenState& s = chickenState_[index];
    float minX = -3.4f, maxX = 3.4f;
    float minZ = 2.0f, maxZ = 4.5f;
    if (index == 0) {
        minX = -3.6f; maxX = -1.7f;
    } else if (index == 2) {
        minX = 1.7f; maxX = 3.6f;
    } else {
        // 中间的小鸡始终在按钮组下方，避免被按钮遮挡
        minX = -0.8f; maxX = 0.8f;
        minZ = 6.7f; maxZ = 7.5f;
    }
    s.targetX = minX + (maxX - minX) * dist01(chickenRng_);
    s.targetZ = minZ + (maxZ - minZ) * dist01(chickenRng_);
    s.repathTimer = 1.6f + 2.4f * dist01(chickenRng_);
}

void Menu3D::UpdateChickens(float dt) {
    for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
        ChickenState& s = chickenState_[i];
        s.repathTimer -= dt;
        if (s.repathTimer <= 0.0f) PickChickenTarget(i);

        const float dx = s.targetX - s.x;
        const float dz = s.targetZ - s.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > 0.06f) {
            const float step = std::min(dist, s.speed * dt);
            s.x += dx / dist * step;
            s.z += dz / dist * step;
            // 装饰用：不管跑向哪里都保持面向镜头（向前）
            s.yaw = 0.0f;
        } else {
            s.yaw = 0.0f;
        }
    }
}

void Menu3D::DrawModels() {
    // 接地阴影
    DrawShadow(-4.0f, 0.0f, 0.62f, 0.38f, 0.46f);
    DrawShadow( 4.0f, 0.0f, 0.62f, 0.38f, 0.46f);
    for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
        const ChickenState& s = chickenState_[i];
        const float dist = 8.6f - s.z;
        const float shadowScale = std::max(0.04f, dist / 6.0f);
        DrawShadow(s.x, s.z, 0.20f * shadowScale, 0.15f * shadowScale, 0.34f);
    }

    // 角色
    glPushMatrix();
    glTranslatef(-4.0f, 0.0f, 0.0f);
    tModel_->Draw(1.22f, 0.0f, 0.0f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(4.0f, 0.0f, 0.0f);
    ctModel_->Draw(1.22f, 0.0f, 0.0f);
    glPopMatrix();

    // 三只鸡：左侧、按钮下方、右侧，各自在区域内随机跑动
    for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
        const ChickenState& s = chickenState_[i];
        const float dist = 8.6f - s.z;
        // 透视下用距离补偿世界缩放，让三只鸡在屏幕上大小接近
        const float scale = std::clamp(0.30f * dist / 6.0f, 0.055f, 0.34f);
        glPushMatrix();
        glTranslatef(s.x, 0.0f, s.z);
        chickens_[i]->Draw(scale, 0.0f, s.yaw);
        glPopMatrix();
    }
}

void Menu3D::Render(float dt) {
    if (!ready_ || !glContext_) return;

    animationTime_ += std::clamp(dt, 0.0f, 0.05f);
    const uint64_t now = SDL_GetTicks64();
    if (lastRenderTicks_ != 0 && now - lastRenderTicks_ < 33) return;
    lastRenderTicks_ = now;

    if (SDL_GL_MakeCurrent(glWindow_, glContext_) != 0) return;
    glViewport(0, 0, width_, height_);

    const float animDt = std::clamp(dt, 0.0f, 0.05f);
    if (tModel_) tModel_->Update(animDt);
    if (ctModel_) ctModel_->Update(animDt);
    for (auto& chicken : chickens_) chicken->Update(animDt);
    UpdateChickens(animDt);

    DrawScene(animationTime_);
    ReadbackToTexture();
    SDL_GL_SwapWindow(glWindow_);
}

void Menu3D::DrawScene(float timeSec) {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float nearZ = 0.1f;
    const float farZ = 40.0f;
    const float top = nearZ * std::tan(28.0f * kPi / 180.0f);
    const float right = top * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, nearZ, farZ);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -1.25f, -8.6f);
    glRotatef(-7.0f, 1.0f, 0.0f, 0.0f);

    // 地面：暖灰褐色，和上方深蓝黑背景形成明确分界
    glDisable(GL_LIGHTING);
    glColor4f(0.135f, 0.125f, 0.105f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-13.0f, 0.0f, -5.0f);
    glVertex3f( 13.0f, 0.0f, -5.0f);
    glVertex3f( 13.0f, 0.0f,  8.0f);
    glVertex3f(-13.0f, 0.0f,  8.0f);
    glEnd();

    // 轻微棋盘格，让地面有材质感
    glColor4f(0.19f, 0.165f, 0.12f, 0.55f);
    glBegin(GL_QUADS);
    for (int iz = -5; iz < 8; ++iz) {
        for (int ix = -13; ix < 13; ++ix) {
            if (((ix + iz) & 1) == 0) continue;
            glVertex3f(static_cast<float>(ix), 0.003f, static_cast<float>(iz));
            glVertex3f(static_cast<float>(ix + 1), 0.003f, static_cast<float>(iz));
            glVertex3f(static_cast<float>(ix + 1), 0.003f, static_cast<float>(iz + 1));
            glVertex3f(static_cast<float>(ix), 0.003f, static_cast<float>(iz + 1));
        }
    }
    glEnd();

    glColor4f(0.52f, 0.40f, 0.22f, 0.42f);
    glBegin(GL_LINES);
    for (int i = -12; i <= 12; ++i) {
        glVertex3f(static_cast<float>(i), 0.006f, -5.0f);
        glVertex3f(static_cast<float>(i), 0.006f, 8.0f);
    }
    for (int i = -5; i <= 8; ++i) {
        glVertex3f(-13.0f, 0.006f, static_cast<float>(i));
        glVertex3f( 13.0f, 0.006f, static_cast<float>(i));
    }
    glEnd();
    glEnable(GL_LIGHTING);

    const float bobT = std::sin(timeSec * 1.7f) * 0.035f;
    if (tModel_ && ctModel_ && chickens_.size() == 3) {
        DrawModels();
    } else {
        DrawCharacter(-3.6f, 0.0f, false, bobT);
        DrawCharacter( 3.6f, 0.0f, true, -bobT);

        // 三只小鸡：错开相位
        DrawChicken(-1.6f, 2.2f, timeSec * 2.4f);
        DrawChicken( 0.0f, 2.8f, timeSec * 2.4f + 1.7f);
        DrawChicken( 1.55f, 2.1f, timeSec * 2.4f + 3.4f);
    }
}

void Menu3D::ReadbackToTexture() {
    if (!texture_ || pixels_.empty()) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());

    const size_t stride = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        const uint8_t* src = pixels_.data() + static_cast<size_t>(height_ - 1 - y) * stride;
        uint8_t* dst = flipped_.data() + static_cast<size_t>(y) * stride;
        std::memcpy(dst, src, stride);
    }
    SDL_UpdateTexture(texture_, nullptr, flipped_.data(), static_cast<int>(stride));
}
