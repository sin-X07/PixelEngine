#pragma once
#include <string>
#include <vector>
#include <array>
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
    void Draw(float scale, float yOffset = 0.0f, float yawDegrees = 0.0f,
              float opacity = 1.0f) const;
    void DrawHighlight(float scale, float yOffset = 0.0f, float yawDegrees = 0.0f) const;
    // 不施加「贴地/缩放/朝向」外层变换，直接按模型空间绘制（给配件用）
    void DrawRaw(float opacity = 1.0f) const;
    bool PlayAnimation(const std::string& name);
    void SetClothingTint(float r, float g, float b) { tint_ = Vec4{ r, g, b, 1.0f }; }
    void SetBluePalette(bool enabled) { bluePalette_ = enabled; }
    void SetUnlit(bool enabled) { unlit_ = enabled; }

    // ---- 材质槽换色（按材质名归类，角色调色板用）----
    enum class Slot { Skin = 0, Main, Pants, Boots, Gear, Wood, Count };
    void SetSlotColor(Slot slot, float r, float g, float b);
    void ClearSlotColors();
    // 整体覆盖颜色（小鸡没有材质，用这个换羽色）
    void SetSolidColor(float r, float g, float b);
    void ClearSolidColor();

    // ---- 节点显隐（角色自带多把武器节点，只显示选中的那一把）----
    void SetNodeVisible(const std::string& name, bool visible);
    bool GetNodeVisible(const std::string& name) const;
    // 节点不参与材质槽换色（武器保持自己的木/金属色，不跟随服装调色板）
    void SetNodeColorExcluded(const std::string& name, bool excluded);

    // 节点在模型空间的世界矩阵（列主序），用于调试/扩展
    bool GetNodeWorldMatrix(const std::string& name, float out[16]) const;
    // 在模型空间的指定位置绘制另一个模型（帽子/眼镜/背包）。
    // 位置用模型空间坐标，和蒙皮顶点在同一空间，避免骨骼绑定空间的缩放差异。
    bool DrawAttached(const Menu3DModel& accessory,
                      float px, float py, float pz,
                      float scale, float yOffset, float yawDegrees,
                      float socketYaw, float accessoryScale,
                      float opacity = 1.0f) const;

    // 蒙皮后模型的最低点（Draw 用它贴地；挂配件时要复现同一变换）
    float GroundOffset() const { return minY_; }

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
        Slot slot = Slot::Gear;
    };
    struct Mesh {
        std::vector<Primitive> primitives;
    };
    struct Node {
        std::string name;
        bool visible = true;
        bool colorExcluded = false;   // 不参与调色板换色（武器）
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
    float minY_ = 0.0f;
    Vec4 tint_{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool bluePalette_ = false;
    bool unlit_ = false;

    struct SlotColor {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        bool  set = false;
    };
    std::array<SlotColor, static_cast<size_t>(Slot::Count)> slotColor_{};
    bool  solidColorSet_ = false;
    Vec4  solidColor_{ 1.0f, 1.0f, 1.0f, 1.0f };

    int FindNode(const std::string& name) const;
    void ApplyPrimitiveColor(const Primitive& prim, float alpha, bool allowSlot) const;

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
