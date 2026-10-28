#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// Menu3DModel — 轻量 glTF 2.0 模型 + CPU 蒙皮
//
// 只服务于主菜单装饰：
//   - 读取 .gltf/.glb（内嵌 buffer 或 .bin 均可）；
//   - 解析 skin / 骨骼 / animation；
//   - 每帧在 CPU 上计算骨骼矩阵并蒙皮；
//   - 用 OpenGL 固定管线绘制纯色低模。
//
// 不追求完整 glTF 支持：只处理 Quaternius/Chickensoft 这类
// “单场景、TRS 节点、LINEAR/STEP 动画、无纹理”的装饰模型。
// ============================================================================
class Menu3DModel {
public:
    struct Vec3 { float x = 0, y = 0, z = 0; };
    struct Vec4 { float x = 0, y = 0, z = 0, w = 1; };

    Menu3DModel() = default;
    ~Menu3DModel() = default;

    bool Load(const std::string& path, const std::string& preferredAnimation = "");
    bool IsReady() const { return ready_; }
    const std::string& LastError() const { return lastError_; }
    const std::string& AnimationName() const { return animationName_; }

    void Update(float dt);
    void Draw(float scale, float yOffset = 0.0f, float yawDegrees = 0.0f) const;
    void SetClothingTint(float r, float g, float b) { tint_ = Vec4{ r, g, b, 1.0f }; }
    void SetBluePalette(bool enabled) { bluePalette_ = enabled; }

private:
    struct Mat4 {
        float m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    };
    struct Quat { float x = 0, y = 0, z = 0, w = 1; };
    struct Vertex {
        Vec3 pos;
        Vec3 normal;
        int joints[4] = { 0, 0, 0, 0 };
        float weights[4] = { 0, 0, 0, 0 };
    };
    struct Primitive {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Vec3> skinnedPos;
        std::vector<Vec3> skinnedNormal;
        Vec4 color{ 0.8f, 0.8f, 0.8f, 1.0f };
        bool isSkin = false;
        std::string materialName;
    };
    struct Mesh {
        std::vector<Primitive> primitives;
    };
    struct Node {
        Mat4 baseLocal;
        Vec3 baseT{ 0, 0, 0 };
        Quat baseR;
        Vec3 baseS{ 1, 1, 1 };
        bool hasBaseMatrix = false;
        Mat4 local;
        Mat4 world;
        Vec3 curT{ 0, 0, 0 };
        Quat curR;
        Vec3 curS{ 1, 1, 1 };
        int mesh = -1;
        int skin = -1;
        std::vector<int> children;
    };
    struct Skin {
        std::vector<int> joints;
        std::vector<Mat4> inverseBind;
    };
    struct Channel {
        int node = -1;
        int path = 0;   // 0=T,1=R,2=S
        int components = 0;
        std::vector<float> times;
        std::vector<float> values;
    };
    struct Animation {
        std::string name;
        float duration = 0.0f;
        std::vector<Channel> channels;
    };

    bool ready_ = false;
    std::string lastError_;
    std::vector<Mesh> meshes_;
    std::vector<Node> nodes_;
    std::vector<Skin> skins_;
    std::vector<Animation> animations_;
    std::vector<int> roots_;
    int animationIndex_ = -1;
    std::string animationName_;
    float animationTime_ = 0.0f;
    Vec4 tint_{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool bluePalette_ = false;

    void ComputeWorld(int nodeIndex, const Mat4& parent);
    void SampleAnimation(float time);
    void SkinPrimitives();

    static Mat4 Identity();
    static Mat4 Multiply(const Mat4& a, const Mat4& b);
    static Mat4 FromTRS(const Vec3& t, const Quat& r, const Vec3& s);
    static Vec3 TransformPoint(const Mat4& m, const Vec3& p);
    static Vec3 TransformDir(const Mat4& m, const Vec3& v);
    static Vec3 Normalize(const Vec3& v);
    static Quat Normalize(const Quat& q);
    static Quat Slerp(const Quat& a, const Quat& b, float t);
};
