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
    glDepthMask(GL_FALSE);   // 影子不写深度，避免把脚底裁掉
    glColor4f(0.0f, 0.0f, 0.0f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, 0.010f, z);
    constexpr int kSegments = 24;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        glVertex3f(x + std::cos(a) * rx, 0.010f, z + std::sin(a) * rz);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void ApplyLookAt(const float eye[3], const float target[3], const float up[3]) {
    float fx = target[0] - eye[0];
    float fy = target[1] - eye[1];
    float fz = target[2] - eye[2];
    const float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (flen < 1e-6f) return;
    fx /= flen; fy /= flen; fz /= flen;

    float sx = fy * up[2] - fz * up[1];
    float sy = fz * up[0] - fx * up[2];
    float sz = fx * up[1] - fy * up[0];
    const float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (slen < 1e-6f) return;
    sx /= slen; sy /= slen; sz /= slen;

    const float ux = sy * fz - sz * fy;
    const float uy = sz * fx - sx * fz;
    const float uz = sx * fy - sy * fx;

    const float m[16] = {
        sx, ux, -fx, 0.0f,
        sy, uy, -fy, 0.0f,
        sz, uz, -fz, 0.0f,
        -(sx * eye[0] + sy * eye[1] + sz * eye[2]),
        -(ux * eye[0] + uy * eye[1] + uz * eye[2]),
         (fx * eye[0] + fy * eye[1] + fz * eye[2]),
        1.0f
    };
    glMultMatrixf(m);
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

    const GLfloat ambient[] = { 0.18f, 0.20f, 0.24f, 1.0f };
    const GLfloat diffuse[] = { 0.88f, 0.84f, 0.76f, 1.0f };
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
        // 中间那只离镜头最近，压暗一点避免灯光下泛白
        if (i == 1) chicken->SetClothingTint(0.55f, 0.55f, 0.45f);
        chickens_.push_back(std::move(chicken));
    }

    focusWeapons_.clear();
    const char* weaponPaths[3] = {
        "assets/models/quaternius/Guns/AK.gltf",
        "assets/models/quaternius/Guns/Pistol.gltf",
        "assets/models/quaternius/Guns/Knife_1.gltf"
    };
    for (int i = 0; i < 3; ++i) {
        auto weapon = std::make_unique<Menu3DModel>();
        if (!weapon->Load(weaponPaths[i])) {
            std::cerr << "[Menu3D] 武器模型加载失败: " << weaponPaths[i]
                      << " " << weapon->LastError() << std::endl;
            weapon.reset();
        }
        focusWeapons_.push_back(std::move(weapon));
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
        s.targetYaw = 0.0f;
        s.speed = 0.75f + (chickenRng_() % 100) / 200.0f;
        PickChickenTarget(i);
    }
}

void Menu3D::PickChickenTarget(int index) {
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    ChickenState& s = chickenState_[index];

    // 全场自由漫游：不再按左/中/右分区。
    // z 先随机，再按该 z 的可见宽度限制 x，避免小鸡跑出画面。
    const float z = -2.0f + 8.8f * dist01(chickenRng_);
    const float visibleHalfWidth = std::min(5.5f, 0.80f * (8.6f - z));
    s.targetZ = z;
    s.targetX = -visibleHalfWidth + 2.0f * visibleHalfWidth * dist01(chickenRng_);
    s.repathTimer = 1.2f + 2.8f * dist01(chickenRng_);
}

void Menu3D::UpdateChickens(float dt) {
    for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
        ChickenState& s = chickenState_[i];
        s.repathTimer -= dt;
        if (s.repathTimer <= 0.0f) PickChickenTarget(i);

        float dx = s.targetX - s.x;
        float dz = s.targetZ - s.z;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist <= 0.06f) {
            // 到点后立刻换下一个方向，保证主界面里一直有鸡在走
            PickChickenTarget(i);
            dx = s.targetX - s.x;
            dz = s.targetZ - s.z;
            dist = std::sqrt(dx * dx + dz * dz);
        }

        if (dist > 0.001f) {
            // 模型正面为 +Z：yaw 跟随行走方向，并做平滑转身
            s.targetYaw = std::atan2(dx, dz) * 180.0f / kPi;
            float delta = std::fmod(s.targetYaw - s.yaw + 540.0f, 360.0f) - 180.0f;
            const float maxTurn = 220.0f * dt;
            s.yaw += std::clamp(delta, -maxTurn, maxTurn);
            s.yaw = std::fmod(s.yaw + 360.0f, 360.0f);

            const float step = std::min(dist, s.speed * dt);
            s.x += dx / dist * step;
            s.z += dz / dist * step;
        }
    }
}

void Menu3D::DrawModels() {
    // 接地阴影
    DrawShadow(-4.4f, 0.0f, 0.66f, 0.40f, 0.46f);
    DrawShadow( 4.4f, 0.0f, 0.66f, 0.40f, 0.46f);
    for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
        const ChickenState& s = chickenState_[i];
        const float dist = 8.6f - s.z;
        const float shadowScale = std::max(0.04f, dist / 6.0f);
        DrawShadow(s.x, s.z, 0.20f * shadowScale, 0.15f * shadowScale, 0.34f);
    }

    // 角色
    glPushMatrix();
    glTranslatef(-4.4f, 0.0f, 0.0f);
    if (hoveredObject_ == 0) tModel_->DrawHighlight(1.22f, 0.0f, 0.0f);
    tModel_->Draw(1.22f, 0.0f, 0.0f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(4.4f, 0.0f, 0.0f);
    if (hoveredObject_ == 1) ctModel_->DrawHighlight(1.22f, 0.0f, 0.0f);
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
        if (hoveredObject_ == i + 2) chickens_[i]->DrawHighlight(scale, 0.0f, s.yaw);
        chickens_[i]->Draw(scale, 0.0f, s.yaw);
        glPopMatrix();
    }
}

void Menu3D::ObjectWorldPosition(int index, float& x, float& y, float& z) const {
    x = 0.0f; y = 1.2f; z = 0.0f;
    if (index == 0) { x = -4.4f; }
    else if (index == 1) { x = 4.4f; }
    else if (index >= 2 && index < 5 && index - 2 < static_cast<int>(chickenState_.size())) {
        const ChickenState& s = chickenState_[index - 2];
        x = s.x; y = 0.7f; z = s.z;
    }
}

void Menu3D::FocusCurrentPosition(float& x, float& z) const {
    const float raw = std::clamp(focusProgress_, 0.0f, 1.0f);
    const float t = raw * raw * (3.0f - 2.0f * raw);
    if (focusedObject_ == 0) {
        x = -4.4f + (focusTargetX_ + 4.4f) * t;
        z = 0.0f;
    } else if (focusedObject_ == 1) {
        x = 4.4f + (focusTargetX_ - 4.4f) * t;
        z = 0.0f;
    } else if (focusedObject_ >= 2 && focusedObject_ - 2 < static_cast<int>(chickenState_.size())) {
        const ChickenState& s = chickenState_[focusedObject_ - 2];
        x = s.x + (focusTargetX_ - s.x) * t;
        z = s.z + (focusTargetZ_ - s.z) * t;
    } else {
        x = focusTargetX_;
        z = focusTargetZ_;
    }
}

void Menu3D::SetOverlayMouse(float u, float v, bool active) {
    overlaySpotlightActive_ = active;
    if (!active) return;
    // 俯视相机：地面 y=0，相机高度约 9.45；用视锥范围把纹理坐标换到地面坐标。
    const float halfH = std::tan(28.0f * kPi / 180.0f) * 9.45f;
    const float halfW = halfH * (static_cast<float>(width_) / static_cast<float>(height_));
    overlayTargetX_ = (std::clamp(u, 0.0f, 1.0f) - 0.5f) * 2.0f * halfW;
    overlayTargetZ_ = (std::clamp(v, 0.0f, 1.0f) - 0.5f) * 2.0f * halfH;
}

void Menu3D::SetFocusedObject(int index) {
    if (index < -1 || index >= kObjectCount) index = -1;
    if (focusedObject_ == index) return;
    focusedObject_ = index;
    focusProgress_ = 0.0f;
    focusTargetX_ = (index < 2) ? -2.4f : -1.8f;
    focusTargetZ_ = 0.0f;
    spotScanTimer_ = 0.0f;
}

void Menu3D::SetObjectColor(int index, int option) {
    Menu3DModel* model = nullptr;
    if (index == 0) model = tModel_.get();
    else if (index == 1) model = ctModel_.get();
    else if (index >= 2 && index < 5 && index - 2 < static_cast<int>(chickens_.size())) {
        model = chickens_[index - 2].get();
    }
    if (!model) return;

    model->SetClothingTint(1.0f, 1.0f, 1.0f);
    model->SetBluePalette(index == 1 && option == 0);
    if (option == 1) model->SetClothingTint(1.0f, 0.82f, 0.30f);
    else if (option == 2) model->SetClothingTint(1.0f, 0.48f, 0.42f);
    else if (option == 3) model->SetClothingTint(0.50f, 0.72f, 1.15f);
}

void Menu3D::SetObjectPose(int index, int option) {
    Menu3DModel* model = nullptr;
    if (index == 0) model = tModel_.get();
    else if (index == 1) model = ctModel_.get();
    else if (index >= 2 && index < 5 && index - 2 < static_cast<int>(chickens_.size())) {
        model = chickens_[index - 2].get();
    }
    if (!model) return;

    if (index < 2) {
        if (option == 0) model->PlayAnimation("Idle");
        else if (option == 1) model->PlayAnimation("Idle_Shoot");
        else if (option == 2) model->PlayAnimation("Wave");
    } else {
        if (option == 0) model->PlayAnimation("walk");
        else if (option == 1) model->PlayAnimation("idle");
        else if (option == 2) model->PlayAnimation("run");
    }
}

void Menu3D::UpdateObjectScreenPositions() {
    double mv[16], pr[16];
    int vp[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, mv);
    glGetDoublev(GL_PROJECTION_MATRIX, pr);
    glGetIntegerv(GL_VIEWPORT, vp);

    auto project = [&](float x, float y, float z, float& u, float& v) -> bool {
        const double in[4] = { x, y, z, 1.0 };
        double a[4] = { 0, 0, 0, 0 };
        double clip[4] = { 0, 0, 0, 0 };
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) a[row] += mv[k * 4 + row] * in[k];
        }
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) clip[row] += pr[k * 4 + row] * a[k];
        }
        if (std::fabs(clip[3]) < 1e-6) return false;
        const double nx = clip[0] / clip[3];
        const double ny = clip[1] / clip[3];
        u = static_cast<float>(nx * 0.5 + 0.5);
        v = static_cast<float>(1.0 - (ny * 0.5 + 0.5));
        return true;
    };

    for (int i = 0; i < kObjectCount; ++i) {
        float x, y, z;
        ObjectWorldPosition(i, x, y, z);
        const float bottomY = 0.0f;
        const float topY = (i < 2) ? 2.55f : 1.55f;
        const float width = (i < 2) ? 2.20f : 1.05f;
        const float midY = (bottomY + topY) * 0.5f;

        float ub = 0, vb = 0, ut = 0, vt = 0;
        float ul = 0, vl = 0, ur = 0, vr = 0;
        if (!project(x, bottomY, z, ub, vb)) continue;
        if (!project(x, topY, z, ut, vt)) continue;
        if (!project(x - width * 0.5f, midY, z, ul, vl)) continue;
        if (!project(x + width * 0.5f, midY, z, ur, vr)) continue;

        // 使用覆盖脚底到头顶的屏幕包围盒，鼠标落在身体任意位置都能命中。
        screenU_[i] = (ul + ur) * 0.5f;
        // T/CT 的可选中区域再向外侧扩一点，避免点击角色边缘时落空
        if (i == 0) screenU_[i] -= 0.032f;
        else if (i == 1) screenU_[i] += 0.032f;
        screenV_[i] = (vb + vt) * 0.5f;
        screenRU_[i] = std::max(0.018f, std::fabs(ur - ul) * 0.5f * 1.30f);
        screenRV_[i] = std::max(0.024f, std::fabs(vt - vb) * 0.5f * 1.20f);
    }
}

int Menu3D::HitTestNormalized(float u, float v) const {
    int bestCharacter = -1;
    int bestChicken = -1;
    float bestCharacterD = 1e9f;
    float bestChickenD = 1e9f;
    for (int i = 0; i < kObjectCount; ++i) {
        const float rx = std::max(screenRU_[i], 0.005f);
        const float ry = std::max(screenRV_[i], 0.005f);
        const float dx = (u - screenU_[i]) / rx;
        const float dy = (v - screenV_[i]) / ry;
        if (std::fabs(dx) <= 1.0f && std::fabs(dy) <= 1.0f) {
            const float d = dx * dx + dy * dy;
            if (i < 2) {
                // T/CT 的命中盒更大、更靠外；与跑过的小鸡重叠时优先角色，
                // 避免玩家明明点在角色身上却选中前方小鸡。
                if (d < bestCharacterD) { bestCharacterD = d; bestCharacter = i; }
            } else if (d < bestChickenD) {
                bestChickenD = d;
                bestChicken = i;
            }
        }
    }
    return bestCharacter >= 0 ? bestCharacter : bestChicken;
}

void Menu3D::UpdateSpotlight(float dt) {
    float tx = 0.0f, tz = 0.0f;
    if (overlaySpotlightActive_) {
        tx = overlayTargetX_;
        tz = overlayTargetZ_;
    } else if (focusedObject_ >= 0) {
        FocusCurrentPosition(tx, tz);
    } else if (hoveredObject_ >= 0) {
        float x, y, z;
        ObjectWorldPosition(hoveredObject_, x, y, z);
        tx = x;
        tz = z;
    } else {
        spotScanTimer_ -= dt;
        if (spotScanTimer_ <= 0.0f) {
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
            spotTargetX_ = -4.0f + 8.0f * dist01(chickenRng_);
            spotTargetZ_ = 0.5f + 5.5f * dist01(chickenRng_);
            spotScanTimer_ = 1.8f + 2.8f * dist01(chickenRng_);
        }
        tx = spotTargetX_;
        tz = spotTargetZ_;
    }

    const float speed = overlaySpotlightActive_ ? 16.0f
                      : (focusedObject_ >= 0) ? 14.0f
                      : (hoveredObject_ >= 0) ? 10.0f : 2.2f;
    const float a = 1.0f - std::exp(-speed * dt);
    spotX_ += (tx - spotX_) * a;
    spotZ_ += (tz - spotZ_) * a;
}

void Menu3D::DrawObjectRing(float x, float z, float radius, float alpha) {
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glColor4f(0.68f, 0.88f, 1.0f, alpha);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    constexpr int kSegments = 40;
    for (int i = 0; i < kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        glVertex3f(x + std::cos(a) * radius, 0.018f, z + std::sin(a) * radius);
    }
    glEnd();
    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void Menu3D::DrawSpotlight() {
    if (spotlightSuppressed_ || spotlightIntensity_ <= 0.001f) {
        glDisable(GL_LIGHT1);
        return;
    }
    const float si = std::clamp(spotlightIntensity_, 0.0f, 1.0f);
    const int targetIndex = (focusedObject_ >= 0) ? focusedObject_ : hoveredObject_;
    const float targetRadius = overlaySpotlightActive_ ? 0.95f
                             : (targetIndex < 0) ? 0.72f
                             : (targetIndex < 2) ? 1.05f : 0.34f;
    const float targetY = (targetIndex < 0) ? 0.05f
                        : (targetIndex < 2) ? 1.15f : 0.55f;

    // 发射源固定在场景上方中央，只让光锥/聚光灯方向跟随目标。
    const GLfloat pos[] = { 0.0f, 6.4f, 0.0f, 1.0f };
    float dirX = spotX_;
    float dirY = targetY - 6.4f;
    float dirZ = spotZ_;
    const float dirLen = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (dirLen > 1e-5f) { dirX /= dirLen; dirY /= dirLen; dirZ /= dirLen; }
    const GLfloat dir[] = { dirX, dirY, dirZ };
    const GLfloat diff[] = { 0.86f * si, 0.93f * si, 1.0f * si, 1.0f };
    const float cutoff = (targetIndex >= 2) ? 20.0f : 31.0f;
    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT1, GL_POSITION, pos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, diff);
    glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, dir);
    glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, cutoff);
    glLightf(GL_LIGHT1, GL_SPOT_EXPONENT, 7.0f);

    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    // 可见光锥：顶端固定在 (0, 6.2, 0)，底端跟随目标
    glColor4f(0.72f, 0.86f, 1.0f, 0.24f * si);
    glBegin(GL_QUAD_STRIP);
    constexpr int kSegments = 24;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        const float topR = 0.045f;
        glVertex3f(std::cos(a) * topR, 6.2f, std::sin(a) * topR);
        glVertex3f(spotX_ + std::cos(a) * targetRadius, 0.02f,
                   spotZ_ + std::sin(a) * targetRadius);
    }
    glEnd();

    // 地面光斑
    glColor4f(0.78f, 0.90f, 1.0f, 0.27f * si);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(spotX_, 0.008f, spotZ_);
    for (int i = 0; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        glVertex3f(spotX_ + std::cos(a) * targetRadius, 0.008f,
                   spotZ_ + std::sin(a) * targetRadius);
    }
    glEnd();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);

    // 随机探照时也画一个小光圈，明确灯在扫地面
    if (hoveredObject_ < 0 && focusedObject_ < 0) {
        DrawObjectRing(spotX_, spotZ_, 0.36f, 0.45f);
    }

    if (hoveredObject_ >= 0 || focusedObject_ >= 0) {
        const int idx = focusedObject_ >= 0 ? focusedObject_ : hoveredObject_;
        float x, y, z;
        if (focusedObject_ >= 0) { FocusCurrentPosition(x, z); }
        else ObjectWorldPosition(idx, x, y, z);
        const float r = (idx < 2) ? 0.82f : 0.30f;
        const float pulse = 0.65f + 0.35f * std::sin(animationTime_ * 5.0f);
        DrawObjectRing(x, z, r, 0.95f * pulse * si);
        DrawObjectRing(x, z, r * 0.72f, 0.55f * pulse * si);
    }
}

void Menu3D::DrawWeaponProp(float x, float y, float z) {
    if (focusWeapon_ >= 0 && focusWeapon_ < static_cast<int>(focusWeapons_.size()) &&
        focusWeapons_[focusWeapon_] && focusWeapons_[focusWeapon_]->IsReady()) {
        glPushMatrix();
        glTranslatef(x, y, z);
        glRotatef(focusWeapon_ == 2 ? 35.0f : -20.0f, 0.0f, 1.0f, 0.0f);
        focusWeapons_[focusWeapon_]->Draw(
            focusWeapon_ == 0 ? 0.28f : (focusWeapon_ == 1 ? 0.38f : 0.50f),
            0.0f, 0.0f);
        glPopMatrix();
        return;
    }

    glDisable(GL_LIGHTING);
    if (focusWeapon_ == 0) {
        // 步枪
        DrawCube(x, y, z, 0.95f, 0.10f, 0.10f, 0.10f, 0.11f, 0.13f);
        DrawCube(x - 0.35f, y - 0.02f, z, 0.36f, 0.22f, 0.18f, 0.22f, 0.18f, 0.13f);
        DrawCube(x + 0.25f, y, z - 0.06f, 0.30f, 0.06f, 0.06f, 0.38f, 0.32f, 0.22f);
    } else if (focusWeapon_ == 1) {
        // 手枪
        DrawCube(x, y, z, 0.38f, 0.12f, 0.10f, 0.10f, 0.11f, 0.13f);
        DrawCube(x - 0.06f, y - 0.20f, z, 0.12f, 0.32f, 0.10f, 0.18f, 0.16f, 0.14f);
    } else if (focusWeapon_ == 2) {
        // 匕首
        DrawCube(x, y, z, 0.52f, 0.06f, 0.04f, 0.72f, 0.74f, 0.78f);
        DrawCube(x - 0.30f, y, z, 0.16f, 0.10f, 0.08f, 0.20f, 0.16f, 0.10f);
    }
    glEnable(GL_LIGHTING);
}

void Menu3D::DrawFocusScene() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float nearZ = 0.1f;
    const float top = nearZ * std::tan(28.0f * kPi / 180.0f);
    const float right = top * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, nearZ, 40.0f);

    // 与主场景保持同一个相机，只让被选中的模型逐帧移动到左侧并放大
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -1.25f, -8.6f);
    glRotatef(-7.0f, 1.0f, 0.0f, 0.0f);

    glDisable(GL_LIGHTING);
    glColor4f(0.135f, 0.125f, 0.105f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-13.0f, 0.0f, -5.0f);
    glVertex3f( 13.0f, 0.0f, -5.0f);
    glVertex3f( 13.0f, 0.0f,  8.0f);
    glVertex3f(-13.0f, 0.0f,  8.0f);
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

    const float raw = std::clamp(focusProgress_, 0.0f, 1.0f);
    const float t = raw * raw * (3.0f - 2.0f * raw);

    float normalX = 0.0f, normalZ = 0.0f, normalScale = 1.22f;
    float targetScale = 1.9f;
    if (focusedObject_ == 0) {
        normalX = -4.4f;
    } else if (focusedObject_ == 1) {
        normalX = 4.4f;
    } else if (focusedObject_ >= 2 && focusedObject_ - 2 < static_cast<int>(chickenState_.size())) {
        const ChickenState& s = chickenState_[focusedObject_ - 2];
        normalX = s.x;
        normalZ = s.z;
        normalScale = std::clamp(0.30f * (8.6f - s.z) / 6.0f, 0.055f, 0.34f);
        targetScale = 1.5f;
    }
    const float x = normalX + (focusTargetX_ - normalX) * t;
    const float z = normalZ + (focusTargetZ_ - normalZ) * t;
    const float scale = normalScale + (targetScale - normalScale) * t;

    DrawSpotlight();

    // 其他物体随聚焦进度渐隐/渐显，退出时不会突然出现
    const float otherAlpha = 1.0f - t;
    if (otherAlpha > 0.01f) {
        if (tModel_ && focusedObject_ != 0) {
            glPushMatrix();
            glTranslatef(-4.4f, 0.0f, 0.0f);
            tModel_->Draw(1.22f, 0.0f, 0.0f, otherAlpha);
            glPopMatrix();
        }
        if (ctModel_ && focusedObject_ != 1) {
            glPushMatrix();
            glTranslatef(4.4f, 0.0f, 0.0f);
            ctModel_->Draw(1.22f, 0.0f, 0.0f, otherAlpha);
            glPopMatrix();
        }
        for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
            if (focusedObject_ == i + 2) continue;
            const ChickenState& s = chickenState_[i];
            const float dist = 8.6f - s.z;
            const float scaleChicken = std::clamp(0.30f * dist / 6.0f, 0.055f, 0.34f);
            glPushMatrix();
            glTranslatef(s.x, 0.0f, s.z);
            chickens_[i]->Draw(scaleChicken, 0.0f, s.yaw, otherAlpha);
            glPopMatrix();
        }
    }

    if (focusedObject_ >= 0 && focusedObject_ < 2) {
        Menu3DModel* model = (focusedObject_ == 0) ? tModel_.get() : ctModel_.get();
        if (model) {
            glPushMatrix();
            glTranslatef(x, 0.0f, z);
            model->DrawHighlight(scale, 0.0f, 0.0f);
            model->Draw(scale, 0.0f, 0.0f);
            glPopMatrix();
            if (t > 0.45f) DrawWeaponProp(x + 0.95f, 1.25f, z);
        }
    } else if (focusedObject_ >= 2 && focusedObject_ - 2 < static_cast<int>(chickens_.size())) {
        Menu3DModel* model = chickens_[focusedObject_ - 2].get();
        if (model) {
            glPushMatrix();
            glTranslatef(x, 0.0f, z);
            model->DrawHighlight(scale, 0.0f, 0.0f);
            model->Draw(scale, 0.0f, 0.0f);
            glPopMatrix();
            if (t > 0.45f) DrawWeaponProp(x + 0.32f, 0.45f, z + 0.1f);
        }
    }

    DrawObjectRing(x, z, focusedObject_ < 2 ? 0.95f : 0.40f, 0.8f);
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
    if (focusedObject_ < 0) UpdateChickens(animDt);
    UpdateSpotlight(animDt);

    if (focusedObject_ >= 0) {
        DrawFocusScene();
    } else {
        DrawScene(animationTime_);
        UpdateObjectScreenPositions();
    }
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
    if (cameraTopDown_ > 0.001f) {
        // 前视 → 四分之一圆弧 → 正上方俯视
        const float raw = std::clamp(cameraTopDown_, 0.0f, 1.0f);
        const float t = raw * raw * (3.0f - 2.0f * raw);
        const float radius = 8.6f;
        const float centerY = 0.85f;
        const float angle = t * (kPi * 0.5f);
        const float eye[3] = {
            0.0f,
            centerY + radius * std::sin(angle),
            radius * std::cos(angle) + 0.001f
        };
        const float target[3] = { 0.0f, centerY, 0.0f };
        // 从“屏幕上方为 +Y”平滑过渡到俯视时的“屏幕上方为 -Z”
        float up[3] = {
            0.0f,
            1.0f - t,
            -t
        };
        const float ul = std::sqrt(up[1] * up[1] + up[2] * up[2]);
        if (ul > 1e-5f) { up[1] /= ul; up[2] /= ul; }
        ApplyLookAt(eye, target, up);
    } else {
        glTranslatef(0.0f, -1.25f, -8.6f);
        glRotatef(-7.0f, 1.0f, 0.0f, 0.0f);
    }
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

    DrawSpotlight();

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

    if (hoveredObject_ >= 0) {
        float x, y, z;
        ObjectWorldPosition(hoveredObject_, x, y, z);
        const float r = (hoveredObject_ < 2) ? 0.78f : 0.30f;
        const float pulse = 0.65f + 0.35f * std::sin(animationTime_ * 5.0f);
        DrawObjectRing(x, z, r, 0.95f * pulse);
        DrawObjectRing(x, z, r * 0.72f, 0.55f * pulse);
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
