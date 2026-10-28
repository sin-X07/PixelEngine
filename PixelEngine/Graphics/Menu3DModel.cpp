#include "Menu3DModel.hpp"
#include "../Core/PathUtils.hpp"

#define CGLTF_IMPLEMENTATION
#include "../ThirdParty/cgltf/cgltf.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool ContainsNoCase(const std::string& s, const std::string& needle) {
    if (needle.empty()) return false;
    auto it = std::search(s.begin(), s.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != s.end();
}

} // namespace

Menu3DModel::Mat4 Menu3DModel::Identity() {
    return Mat4{};
}

Menu3DModel::Mat4 Menu3DModel::Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

Menu3DModel::Mat4 Menu3DModel::FromTRS(const Vec3& t, const Quat& r, const Vec3& s) {
    const float xx = r.x * r.x, yy = r.y * r.y, zz = r.z * r.z;
    const float xy = r.x * r.y, xz = r.x * r.z, yz = r.y * r.z;
    const float wx = r.w * r.x, wy = r.w * r.y, wz = r.w * r.z;

    Mat4 m;
    m.m[0]  = (1.0f - 2.0f * (yy + zz)) * s.x;
    m.m[1]  = (2.0f * (xy + wz)) * s.x;
    m.m[2]  = (2.0f * (xz - wy)) * s.x;
    m.m[3]  = 0.0f;
    m.m[4]  = (2.0f * (xy - wz)) * s.y;
    m.m[5]  = (1.0f - 2.0f * (xx + zz)) * s.y;
    m.m[6]  = (2.0f * (yz + wx)) * s.y;
    m.m[7]  = 0.0f;
    m.m[8]  = (2.0f * (xz + wy)) * s.z;
    m.m[9]  = (2.0f * (yz - wx)) * s.z;
    m.m[10] = (1.0f - 2.0f * (xx + yy)) * s.z;
    m.m[11] = 0.0f;
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    m.m[15] = 1.0f;
    return m;
}

Menu3DModel::Vec3 Menu3DModel::TransformPoint(const Mat4& m, const Vec3& p) {
    return Vec3{
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]
    };
}

Menu3DModel::Vec3 Menu3DModel::TransformDir(const Mat4& m, const Vec3& v) {
    return Vec3{
        m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z
    };
}

Menu3DModel::Vec3 Menu3DModel::Normalize(const Vec3& v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) return Vec3{ 0, 0, 0 };
    return Vec3{ v.x / len, v.y / len, v.z / len };
}

Menu3DModel::Quat Menu3DModel::Normalize(const Quat& q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1e-6f) return Quat{};
    return Quat{ q.x / len, q.y / len, q.z / len, q.w / len };
}

Menu3DModel::Quat Menu3DModel::Slerp(const Quat& a, const Quat& b, float t) {
    Quat q1 = a;
    Quat q2 = b;
    float dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
    if (dot < 0.0f) {
        q2 = Quat{ -q2.x, -q2.y, -q2.z, -q2.w };
        dot = -dot;
    }
    if (dot > 0.9995f) {
        return Normalize(Quat{
            q1.x + (q2.x - q1.x) * t,
            q1.y + (q2.y - q1.y) * t,
            q1.z + (q2.z - q1.z) * t,
            q1.w + (q2.w - q1.w) * t
        });
    }
    const float theta0 = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float theta = theta0 * t;
    const float sinTheta = std::sin(theta);
    const float sinTheta0 = std::sin(theta0);
    const float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    const float s1 = sinTheta / sinTheta0;
    return Quat{
        q1.x * s0 + q2.x * s1,
        q1.y * s0 + q2.y * s1,
        q1.z * s0 + q2.z * s1,
        q1.w * s0 + q2.w * s1
    };
}

bool Menu3DModel::Load(const std::string& path, const std::string& preferredAnimation) {
    ready_ = false;
    lastError_.clear();
    meshes_.clear();
    nodes_.clear();
    skins_.clear();
    animations_.clear();
    roots_.clear();
    animationIndex_ = -1;
    animationTime_ = 0.0f;
    animationName_.clear();

    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string resolved = PathUtils::ResolveProjectFile(path);
    cgltf_result result = cgltf_parse_file(&options, resolved.c_str(), &data);
    if (result != cgltf_result_success || !data) {
        lastError_ = "cgltf_parse_file failed: " + resolved;
        return false;
    }
    result = cgltf_load_buffers(&options, data, resolved.c_str());
    if (result != cgltf_result_success) {
        lastError_ = "cgltf_load_buffers failed";
        cgltf_free(data);
        return false;
    }

    std::unordered_map<const cgltf_node*, int> nodeIndex;
    nodeIndex.reserve(data->nodes_count);
    for (size_t i = 0; i < data->nodes_count; ++i) {
        nodeIndex[&data->nodes[i]] = static_cast<int>(i);
    }

    nodes_.resize(data->nodes_count);
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& src = data->nodes[i];
        Node& dst = nodes_[i];
        dst.baseT = Vec3{ 0, 0, 0 };
        dst.baseR = Quat{};
        dst.baseS = Vec3{ 1, 1, 1 };
        if (src.has_translation) dst.baseT = Vec3{ src.translation[0], src.translation[1], src.translation[2] };
        if (src.has_rotation) dst.baseR = Quat{ src.rotation[0], src.rotation[1], src.rotation[2], src.rotation[3] };
        if (src.has_scale) dst.baseS = Vec3{ src.scale[0], src.scale[1], src.scale[2] };
        if (src.has_matrix) {
            dst.hasBaseMatrix = true;
            std::memcpy(dst.baseLocal.m, src.matrix, sizeof(float) * 16);
        } else {
            dst.baseLocal = FromTRS(dst.baseT, dst.baseR, dst.baseS);
        }
        dst.curT = dst.baseT;
        dst.curR = dst.baseR;
        dst.curS = dst.baseS;
        dst.local = dst.baseLocal;
        dst.world = Identity();

        if (src.mesh) {
            // 建立 cgltf mesh -> 本地 mesh 的映射（按指针在数组中的位置）
            dst.mesh = static_cast<int>(src.mesh - data->meshes);
        }
        if (src.skin) {
            dst.skin = static_cast<int>(src.skin - data->skins);
        }
        for (size_t c = 0; c < src.children_count; ++c) {
            auto it = nodeIndex.find(src.children[c]);
            if (it != nodeIndex.end()) dst.children.push_back(it->second);
        }
    }

    // 根节点
    {
        std::vector<bool> hasParent(nodes_.size(), false);
        for (const Node& n : nodes_) {
            for (int c : n.children) {
                if (c >= 0 && c < static_cast<int>(hasParent.size())) hasParent[c] = true;
            }
        }
        for (size_t i = 0; i < nodes_.size(); ++i) {
            if (!hasParent[i]) roots_.push_back(static_cast<int>(i));
        }
    }

    // mesh
    meshes_.resize(data->meshes_count);
    for (size_t mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& cm = data->meshes[mi];
        Mesh& mesh = meshes_[mi];
        for (size_t pi = 0; pi < cm.primitives_count; ++pi) {
            const cgltf_primitive& cp = cm.primitives[pi];
            if (cp.type != cgltf_primitive_type_triangles) continue;

            const cgltf_accessor* posAcc = nullptr;
            const cgltf_accessor* normAcc = nullptr;
            const cgltf_accessor* jointAcc = nullptr;
            const cgltf_accessor* weightAcc = nullptr;
            for (size_t ai = 0; ai < cp.attributes_count; ++ai) {
                const cgltf_attribute& attr = cp.attributes[ai];
                if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_joints) jointAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_weights) weightAcc = attr.data;
            }
            if (!posAcc) continue;

            Primitive prim;
            prim.vertices.resize(posAcc->count);
            for (size_t vi = 0; vi < posAcc->count; ++vi) {
                float p[3] = { 0, 0, 0 };
                cgltf_accessor_read_float(posAcc, vi, p, 3);
                prim.vertices[vi].pos = Vec3{ p[0], p[1], p[2] };
                if (normAcc) {
                    float n[3] = { 0, 1, 0 };
                    cgltf_accessor_read_float(normAcc, vi, n, 3);
                    prim.vertices[vi].normal = Normalize(Vec3{ n[0], n[1], n[2] });
                }
                if (jointAcc) {
                    cgltf_uint j[4] = { 0, 0, 0, 0 };
                    cgltf_accessor_read_uint(jointAcc, vi, j, 4);
                    for (int k = 0; k < 4; ++k) prim.vertices[vi].joints[k] = static_cast<int>(j[k]);
                }
                if (weightAcc) {
                    float w[4] = { 1, 0, 0, 0 };
                    cgltf_accessor_read_float(weightAcc, vi, w, 4);
                    for (int k = 0; k < 4; ++k) prim.vertices[vi].weights[k] = w[k];
                }
            }
            if (cp.indices) {
                prim.indices.resize(cp.indices->count);
                for (size_t ii = 0; ii < cp.indices->count; ++ii) {
                    prim.indices[ii] = static_cast<uint32_t>(cgltf_accessor_read_index(cp.indices, ii));
                }
            } else {
                prim.indices.resize(prim.vertices.size());
                for (size_t ii = 0; ii < prim.vertices.size(); ++ii) prim.indices[ii] = static_cast<uint32_t>(ii);
            }
            prim.skinnedPos.resize(prim.vertices.size());
            prim.skinnedNormal.resize(prim.vertices.size());

            if (cp.material) {
                const float* c = cp.material->pbr_metallic_roughness.base_color_factor;
                prim.color = Vec4{ c[0], c[1], c[2], c[3] };
                prim.isSkin = cp.material->name && ContainsNoCase(cp.material->name, "skin");
                prim.materialName = cp.material->name ? cp.material->name : "";
            } else {
                // Chickensoft 的 GLB 没有材质，给一个偏黄的暖色
                prim.color = Vec4{ 0.94f, 0.62f, 0.12f, 1.0f };
            }
            mesh.primitives.push_back(std::move(prim));
        }
    }

    // skins
    skins_.resize(data->skins_count);
    for (size_t si = 0; si < data->skins_count; ++si) {
        const cgltf_skin& cs = data->skins[si];
        Skin& skin = skins_[si];
        skin.joints.reserve(cs.joints_count);
        for (size_t j = 0; j < cs.joints_count; ++j) {
            auto it = nodeIndex.find(cs.joints[j]);
            skin.joints.push_back(it == nodeIndex.end() ? -1 : it->second);
        }
        if (cs.inverse_bind_matrices) {
            skin.inverseBind.resize(cs.joints_count);
            for (size_t j = 0; j < cs.joints_count; ++j) {
                float m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
                cgltf_accessor_read_float(cs.inverse_bind_matrices, j, m, 16);
                std::memcpy(skin.inverseBind[j].m, m, sizeof(m));
            }
        } else {
            skin.inverseBind.assign(cs.joints_count, Identity());
        }
    }

    // animations
    animations_.reserve(data->animations_count);
    for (size_t ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& ca = data->animations[ai];
        Animation anim;
        anim.name = ca.name ? ca.name : "";
        for (size_t ci = 0; ci < ca.channels_count; ++ci) {
            const cgltf_animation_channel& cc = ca.channels[ci];
            if (!cc.target_node || !cc.sampler) continue;
            auto it = nodeIndex.find(cc.target_node);
            if (it == nodeIndex.end()) continue;
            int components = 0;
            int path = 0;
            if (cc.target_path == cgltf_animation_path_type_translation) { path = 0; components = 3; }
            else if (cc.target_path == cgltf_animation_path_type_rotation) { path = 1; components = 4; }
            else if (cc.target_path == cgltf_animation_path_type_scale) { path = 2; components = 3; }
            else continue;

            const cgltf_accessor* input = cc.sampler->input;
            const cgltf_accessor* output = cc.sampler->output;
            if (!input || !output) continue;

            Channel channel;
            channel.node = it->second;
            channel.path = path;
            channel.components = components;
            channel.times.resize(input->count);
            for (size_t k = 0; k < input->count; ++k) {
                float t = 0.0f;
                cgltf_accessor_read_float(input, k, &t, 1);
                channel.times[k] = t;
                anim.duration = std::max(anim.duration, t);
            }
            channel.values.resize(output->count * components);
            for (size_t k = 0; k < output->count; ++k) {
                float v[4] = { 0, 0, 0, 0 };
                cgltf_accessor_read_float(output, k, v, components);
                for (int c = 0; c < components; ++c) {
                    channel.values[k * components + c] = v[c];
                }
            }
            if (!channel.times.empty()) anim.channels.push_back(std::move(channel));
        }
        if (anim.duration > 0.0f) animations_.push_back(std::move(anim));
    }

    // 选择动画
    if (!animations_.empty()) {
        int chosen = -1;
        if (!preferredAnimation.empty()) {
            for (size_t i = 0; i < animations_.size(); ++i) {
                if (ContainsNoCase(animations_[i].name, preferredAnimation)) { chosen = static_cast<int>(i); break; }
            }
        }
        if (chosen < 0) {
            for (size_t i = 0; i < animations_.size(); ++i) {
                if (ContainsNoCase(animations_[i].name, "idle")) { chosen = static_cast<int>(i); break; }
            }
        }
        if (chosen < 0) chosen = 0;
        animationIndex_ = chosen;
        animationName_ = animations_[chosen].name;
    }

    cgltf_free(data);

    // 初始化世界矩阵 + 首帧蒙皮
    for (int r : roots_) ComputeWorld(r, Identity());
    SkinPrimitives();
    ready_ = true;
    return true;
}

void Menu3DModel::ComputeWorld(int nodeIndex, const Mat4& parent) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes_.size())) return;
    Node& node = nodes_[nodeIndex];
    node.world = Multiply(parent, node.local);
    for (int c : node.children) ComputeWorld(c, node.world);
}

void Menu3DModel::SampleAnimation(float time) {
    for (Node& n : nodes_) {
        n.curT = n.baseT;
        n.curR = n.baseR;
        n.curS = n.baseS;
    }
    if (animationIndex_ < 0 || animationIndex_ >= static_cast<int>(animations_.size())) {
        for (Node& n : nodes_) n.local = n.baseLocal;
        return;
    }

    const Animation& anim = animations_[animationIndex_];
    for (const Channel& ch : anim.channels) {
        if (ch.node < 0 || ch.node >= static_cast<int>(nodes_.size())) continue;
        if (ch.times.empty() || ch.values.empty()) continue;

        size_t i0 = 0, i1 = 0;
        float t = 0.0f;
        if (time <= ch.times.front()) {
            i0 = i1 = 0;
        } else if (time >= ch.times.back()) {
            i0 = i1 = ch.times.size() - 1;
        } else {
            auto it = std::upper_bound(ch.times.begin(), ch.times.end(), time);
            i1 = static_cast<size_t>(it - ch.times.begin());
            i0 = i1 - 1;
            const float span = ch.times[i1] - ch.times[i0];
            t = span > 1e-6f ? (time - ch.times[i0]) / span : 0.0f;
        }

        const float* a = &ch.values[i0 * ch.components];
        const float* b = &ch.values[i1 * ch.components];
        Node& node = nodes_[ch.node];
        if (ch.path == 0) {
            node.curT = Vec3{
                a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t
            };
        } else if (ch.path == 1) {
            Quat qa{ a[0], a[1], a[2], a[3] };
            Quat qb{ b[0], b[1], b[2], b[3] };
            node.curR = Slerp(qa, qb, t);
        } else if (ch.path == 2) {
            node.curS = Vec3{
                a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t
            };
        }
    }

    for (Node& n : nodes_) {
        n.local = n.hasBaseMatrix ? n.baseLocal : FromTRS(n.curT, n.curR, n.curS);
    }
}

void Menu3DModel::Update(float dt) {
    if (!ready_) return;
    if (animationIndex_ >= 0 && animationIndex_ < static_cast<int>(animations_.size())) {
        const float duration = animations_[animationIndex_].duration;
        animationTime_ += dt;
        if (duration > 0.0f) animationTime_ = std::fmod(animationTime_, duration);
        SampleAnimation(animationTime_);
    } else {
        SampleAnimation(0.0f);
    }
    for (int r : roots_) ComputeWorld(r, Identity());
    SkinPrimitives();
}

void Menu3DModel::SkinPrimitives() {
    float minY = 1e30f;
    for (const Node& node : nodes_) {
        if (node.mesh < 0 || node.mesh >= static_cast<int>(meshes_.size())) continue;
        const Mesh& mesh = meshes_[node.mesh];
        const Skin* skin = (node.skin >= 0 && node.skin < static_cast<int>(skins_.size()))
            ? &skins_[node.skin] : nullptr;

        std::vector<Mat4> jointMatrices;
        if (skin) {
            jointMatrices.resize(skin->joints.size());
            for (size_t j = 0; j < skin->joints.size(); ++j) {
                const int ni = skin->joints[j];
                const Mat4& inv = (j < skin->inverseBind.size()) ? skin->inverseBind[j] : Identity();
                const Mat4& world = (ni >= 0 && ni < static_cast<int>(nodes_.size()))
                    ? nodes_[ni].world : Identity();
                jointMatrices[j] = Multiply(world, inv);
            }
        }

        for (const Primitive& prim : mesh.primitives) {
            Primitive& dst = const_cast<Primitive&>(prim);
            for (size_t vi = 0; vi < prim.vertices.size(); ++vi) {
                const Vertex& v = prim.vertices[vi];
                if (skin && !jointMatrices.empty()) {
                    Vec3 p{ 0, 0, 0 };
                    Vec3 n{ 0, 0, 0 };
                    float totalWeight = 0.0f;
                    for (int k = 0; k < 4; ++k) {
                        const float w = v.weights[k];
                        if (w <= 0.0f) continue;
                        const int joint = v.joints[k];
                        if (joint < 0 || joint >= static_cast<int>(jointMatrices.size())) continue;
                        const Mat4& jm = jointMatrices[joint];
                        const Vec3 tp = TransformPoint(jm, v.pos);
                        const Vec3 tn = TransformDir(jm, v.normal);
                        p.x += tp.x * w; p.y += tp.y * w; p.z += tp.z * w;
                        n.x += tn.x * w; n.y += tn.y * w; n.z += tn.z * w;
                        totalWeight += w;
                    }
                    if (totalWeight > 1e-6f) {
                        dst.skinnedPos[vi] = Vec3{ p.x / totalWeight, p.y / totalWeight, p.z / totalWeight };
                        dst.skinnedNormal[vi] = Normalize(n);
                    } else {
                        dst.skinnedPos[vi] = Vec3{ 0, 0, 0 };
                        dst.skinnedNormal[vi] = Vec3{ 0, 1, 0 };
                    }
                } else {
                    dst.skinnedPos[vi] = TransformPoint(node.world, v.pos);
                    dst.skinnedNormal[vi] = Normalize(TransformDir(node.world, v.normal));
                }
                minY = std::min(minY, dst.skinnedPos[vi].y);
            }
        }
    }
    minY_ = (minY > 1e29f) ? 0.0f : minY;
}

void Menu3DModel::Draw(float scale, float yOffset, float yawDegrees) const {
    if (!ready_) return;
    glPushMatrix();
    // 蒙皮后取最低点贴地，避免走路动画时脚底穿进地面
    glTranslatef(0.0f, yOffset - minY_ * scale, 0.0f);
    glRotatef(yawDegrees, 0.0f, 1.0f, 0.0f);
    glScalef(scale, scale, scale);

    if (unlit_) glDisable(GL_LIGHTING);
    for (const Node& node : nodes_) {
        if (node.mesh < 0 || node.mesh >= static_cast<int>(meshes_.size())) continue;
        const Mesh& mesh = meshes_[node.mesh];
        for (const Primitive& prim : mesh.primitives) {
            if (prim.indices.empty()) continue;
            const float tr = prim.isSkin ? 1.0f : tint_.x;
            const float tg = prim.isSkin ? 1.0f : tint_.y;
            const float tb = prim.isSkin ? 1.0f : tint_.z;
            float r = prim.color.x * tr;
            float g = prim.color.y * tg;
            float b = prim.color.z * tb;
            if (bluePalette_ && !prim.isSkin) {
                if (ContainsNoCase(prim.materialName, "Character_Main")) {
                    r = 0.18f; g = 0.34f; b = 0.72f;
                } else if (ContainsNoCase(prim.materialName, "Pants")) {
                    r = 0.09f; g = 0.14f; b = 0.26f;
                } else if (ContainsNoCase(prim.materialName, "Red")) {
                    r = 0.18f; g = 0.42f; b = 0.85f;
                }
            }
            glColor4f(r, g, b, prim.color.w);
            glBegin(GL_TRIANGLES);
            for (uint32_t idx : prim.indices) {
                if (idx >= prim.skinnedPos.size()) continue;
                const Vec3& p = prim.skinnedPos[idx];
                const Vec3& n = prim.skinnedNormal[idx];
                glNormal3f(n.x, n.y, n.z);
                glVertex3f(p.x, p.y, p.z);
            }
            glEnd();
        }
    }
    if (unlit_) glEnable(GL_LIGHTING);
    glPopMatrix();
}
