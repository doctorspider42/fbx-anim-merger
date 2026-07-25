#include "core/FbxImport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "util/Log.h"

#include <ufbx.h>

namespace fs = std::filesystem;

namespace fam {
namespace {

glm::vec3 ToGlm(const ufbx_vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

glm::vec2 ToGlm(const ufbx_vec2& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y)};
}

glm::quat ToGlm(const ufbx_quat& q) {
    return glm::quat(static_cast<float>(q.w), static_cast<float>(q.x), static_cast<float>(q.y),
                     static_cast<float>(q.z));
}

// ufbx stores affine 3x4 matrices column-major, which lines up with glm::mat4.
glm::mat4 ToGlm(const ufbx_matrix& m) {
    glm::mat4 r(1.0f);
    for (int c = 0; c < 4; ++c) {
        r[c] = glm::vec4(static_cast<float>(m.cols[c].x), static_cast<float>(m.cols[c].y),
                         static_cast<float>(m.cols[c].z), c == 3 ? 1.0f : 0.0f);
    }
    return r;
}

std::string ToStd(const ufbx_string& s) {
    return std::string(s.data ? s.data : "", s.length);
}

// ---------------------------------------------------------------- vertex weld
struct VertexKey {
    Vertex v;

    bool operator==(const VertexKey& other) const {
        return std::memcmp(&v, &other.v, sizeof(Vertex)) == 0;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&key.v);
        size_t h = 1469598103934665603ull;  // FNV-1a
        for (size_t i = 0; i < sizeof(Vertex); ++i) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
        return h;
    }
};

void NormalizeInfluences(Vertex& v) {
    float sum = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
    if (sum > 1.0e-6f) {
        v.boneWeights /= sum;
    } else {
        v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        v.boneIndices = glm::ivec4(0);
    }
}

// FBX stores whatever separator the authoring machine used, so neither
// fs::path::filename nor a single delimiter is reliable here.
std::string BaseName(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

TextureSource MakeTextureSource(const ufbx_texture* tex, const fs::path& fbxDir) {
    TextureSource out;
    if (!tex) return out;

    const std::string filename = ToStd(tex->filename);
    out.name = filename.empty() ? ToStd(tex->name) : BaseName(filename);

    // Embedded content wins: it is guaranteed to be the right image, whereas the
    // recorded paths routinely point at a machine we have never seen.
    if (tex->content.size > 0 && tex->content.data != nullptr) {
        const auto* bytes = static_cast<const uint8_t*>(tex->content.data);
        out.content = std::make_shared<const std::vector<uint8_t>>(bytes, bytes + tex->content.size);
        return out;
    }

    auto tryPath = [](const fs::path& p) -> std::string {
        std::error_code ec;
        if (!p.empty() && fs::exists(p, ec) && fs::is_regular_file(p, ec)) return p.string();
        return {};
    };

    if (std::string r = tryPath(ToStd(tex->absolute_filename)); !r.empty()) {
        out.path = r;
        return out;
    }

    const std::string relative = ToStd(tex->relative_filename);
    if (!relative.empty()) {
        if (std::string r = tryPath(fbxDir / relative); !r.empty()) {
            out.path = r;
            return out;
        }
    }

    if (!filename.empty()) {
        if (std::string r = tryPath(filename); !r.empty()) {
            out.path = r;
            return out;
        }
        for (const char* sub : {"", "textures", "Textures", "tex", "maps"}) {
            if (std::string r = tryPath(fbxDir / sub / out.name); !r.empty()) {
                out.path = r;
                return out;
            }
        }
    }

    if (!out.name.empty()) {
        LogWarn("Texture '%s' is neither embedded nor found on disk - skipped.", out.name.c_str());
    }
    out.name.clear();
    return out;
}

// --------------------------------------------------------------- node import
void ImportNodes(const ufbx_scene* scene, Model& out, std::vector<int>& ufbxToModel) {
    ufbxToModel.assign(scene->nodes.count, -1);
    out.nodes.reserve(scene->nodes.count);

    std::unordered_map<std::string, int> used;
    int renamed = 0;

    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* src = scene->nodes.data[i];

        Node node;
        node.name = ToStd(src->name);
        if (node.name.empty()) node.name = "node_" + std::to_string(i);

        // Animation channels bind by name, so duplicates would silently cross-wire.
        auto [it, inserted] = used.emplace(node.name, 1);
        if (!inserted) {
            std::string unique;
            do {
                unique = node.name + "_" + std::to_string(it->second++);
            } while (used.count(unique) != 0);
            used.emplace(unique, 1);
            node.name = unique;
            ++renamed;
        }

        const ufbx_transform& t = src->local_transform;
        node.translation = ToGlm(t.translation);
        node.rotation = ToGlm(t.rotation);
        node.scale = ToGlm(t.scale);
        node.parent = src->parent ? static_cast<int>(src->parent->typed_id) : -1;

        ufbxToModel[i] = static_cast<int>(out.nodes.size());
        out.nodes.push_back(std::move(node));
    }

    // typed_id indexes scene->nodes directly, so parents need no remapping, but be
    // explicit about it in case ufbx ever reorders.
    for (size_t i = 0; i < out.nodes.size(); ++i) {
        int parent = out.nodes[i].parent;
        if (parent >= 0 && parent < static_cast<int>(ufbxToModel.size())) {
            out.nodes[i].parent = ufbxToModel[static_cast<size_t>(parent)];
        }
    }

    out.RebuildHierarchy();

    if (renamed > 0) {
        LogWarn("Renamed %d duplicate node name(s) to keep animation binding unambiguous.", renamed);
    }
}

// ----------------------------------------------------------- material import
void ImportMaterials(const ufbx_scene* scene, const fs::path& fbxDir, Model& out) {
    out.materials.reserve(scene->materials.count);
    for (size_t i = 0; i < scene->materials.count; ++i) {
        const ufbx_material* src = scene->materials.data[i];

        Material mat;
        mat.name = ToStd(src->name);
        if (mat.name.empty()) mat.name = "material_" + std::to_string(i);

        if (src->pbr.base_color.has_value) {
            const ufbx_vec4& c = src->pbr.base_color.value_vec4;
            mat.baseColor = {static_cast<float>(c.x), static_cast<float>(c.y),
                             static_cast<float>(c.z)};
        } else if (src->fbx.diffuse_color.has_value) {
            const ufbx_vec4& c = src->fbx.diffuse_color.value_vec4;
            mat.baseColor = {static_cast<float>(c.x), static_cast<float>(c.y),
                             static_cast<float>(c.z)};
        }
        if (src->pbr.metalness.has_value) {
            mat.metallic = static_cast<float>(src->pbr.metalness.value_real);
        }
        if (src->pbr.roughness.has_value) {
            mat.roughness = static_cast<float>(src->pbr.roughness.value_real);
        }
        if (src->pbr.opacity.has_value) {
            mat.opacity = static_cast<float>(src->pbr.opacity.value_real);
        }

        const ufbx_texture* baseColor = src->pbr.base_color.texture;
        if (!baseColor) baseColor = src->fbx.diffuse_color.texture;
        mat.baseColorTexture = MakeTextureSource(baseColor, fbxDir);

        const ufbx_texture* normal = src->pbr.normal_map.texture;
        if (!normal) normal = src->fbx.normal_map.texture;
        mat.normalTexture = MakeTextureSource(normal, fbxDir);

        if (mat.baseColorTexture.Embedded() || mat.normalTexture.Embedded()) {
            LogInfo("Material '%s': using embedded texture data.", mat.name.c_str());
        }

        out.materials.push_back(std::move(mat));
    }

    if (out.materials.empty()) {
        out.materials.push_back(Material{});
    }
}

// --------------------------------------------------------------- mesh import
void ImportMeshes(const ufbx_scene* scene, Model& out, const std::vector<int>& ufbxToModel) {
    std::unordered_map<const ufbx_node*, int> boneNodeToIndex;

    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* src = scene->meshes.data[mi];
        if (src->instances.count == 0) continue;

        const ufbx_node* meshNode = src->instances.data[0];
        const ufbx_skin_deformer* skin = src->skin_deformers.count ? src->skin_deformers.data[0] : nullptr;

        Mesh mesh;
        mesh.name = ToStd(src->name);
        if (mesh.name.empty()) mesh.name = ToStd(meshNode->name);
        if (mesh.name.empty()) mesh.name = "mesh_" + std::to_string(mi);
        mesh.nodeIndex = ufbxToModel[meshNode->typed_id];
        mesh.skinned = skin != nullptr;

        // Non-skinned geometry carries FBX's geometric transform, which is *not*
        // part of the node hierarchy. Bake it so a plain node matrix is enough.
        const glm::mat4 geomToNode = ToGlm(meshNode->geometry_to_node);
        const glm::mat3 geomNormal = glm::mat3(glm::transpose(glm::inverse(geomToNode)));

        // Map every cluster onto a global skeleton slot.
        std::vector<int> clusterToBone;
        if (skin) {
            clusterToBone.resize(skin->clusters.count, 0);
            for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
                const ufbx_skin_cluster* cluster = skin->clusters.data[ci];
                const ufbx_node* boneNode = cluster->bone_node;
                if (!boneNode) continue;

                auto it = boneNodeToIndex.find(boneNode);
                if (it == boneNodeToIndex.end()) {
                    Bone bone;
                    bone.nodeIndex = ufbxToModel[boneNode->typed_id];
                    bone.inverseBind = ToGlm(cluster->geometry_to_bone);
                    const int index = static_cast<int>(out.skeleton.bones.size());
                    out.skeleton.bones.push_back(bone);
                    out.skeleton.boneByName.emplace(out.nodes[static_cast<size_t>(bone.nodeIndex)].name,
                                                    index);
                    it = boneNodeToIndex.emplace(boneNode, index).first;
                }
                clusterToBone[ci] = it->second;
            }
        }

        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> weld;
        weld.reserve(src->num_indices);

        // material index (global) -> triangle indices
        std::vector<std::pair<int, std::vector<uint32_t>>> buckets;
        auto bucketFor = [&buckets](int material) -> std::vector<uint32_t>& {
            for (auto& b : buckets) {
                if (b.first == material) return b.second;
            }
            buckets.emplace_back(material, std::vector<uint32_t>{});
            return buckets.back().second;
        };

        std::vector<uint32_t> triIndices(static_cast<size_t>(src->max_face_triangles) * 3 + 6);

        glm::vec3 aabbMin(std::numeric_limits<float>::max());
        glm::vec3 aabbMax(std::numeric_limits<float>::lowest());

        for (size_t fi = 0; fi < src->faces.count; ++fi) {
            const ufbx_face face = src->faces.data[fi];
            if (face.num_indices < 3) continue;

            const uint32_t numTris =
                ufbx_triangulate_face(triIndices.data(), triIndices.size(), src, face);

            int globalMaterial = -1;
            if (src->materials.count > 0) {
                uint32_t local = src->face_material.count ? src->face_material.data[fi] : 0;
                if (local < src->materials.count) {
                    globalMaterial = static_cast<int>(src->materials.data[local]->typed_id);
                }
            }
            std::vector<uint32_t>& bucket = bucketFor(globalMaterial);

            for (uint32_t t = 0; t < numTris * 3; ++t) {
                const uint32_t ix = triIndices[t];

                Vertex v;
                v.position = ToGlm(ufbx_get_vertex_vec3(&src->vertex_position, ix));
                if (src->vertex_normal.exists) {
                    v.normal = ToGlm(ufbx_get_vertex_vec3(&src->vertex_normal, ix));
                }
                if (src->vertex_uv.exists) {
                    v.uv = ToGlm(ufbx_get_vertex_vec2(&src->vertex_uv, ix));
                }

                if (!mesh.skinned) {
                    v.position = glm::vec3(geomToNode * glm::vec4(v.position, 1.0f));
                    v.normal = glm::normalize(geomNormal * v.normal);
                } else if (skin) {
                    const uint32_t vertexIndex = src->vertex_indices.data[ix];
                    if (vertexIndex < skin->vertices.count) {
                        const ufbx_skin_vertex& sv = skin->vertices.data[vertexIndex];

                        // Keep the 4 heaviest influences.
                        float weights[kMaxBoneInfluences] = {0, 0, 0, 0};
                        int bones[kMaxBoneInfluences] = {0, 0, 0, 0};
                        for (uint32_t w = 0; w < sv.num_weights; ++w) {
                            const ufbx_skin_weight& sw = skin->weights.data[sv.weight_begin + w];
                            const float weight = static_cast<float>(sw.weight);
                            const int bone = sw.cluster_index < clusterToBone.size()
                                                 ? clusterToBone[sw.cluster_index]
                                                 : 0;
                            int slot = -1;
                            float smallest = weight;
                            for (int k = 0; k < kMaxBoneInfluences; ++k) {
                                if (weights[k] < smallest) {
                                    smallest = weights[k];
                                    slot = k;
                                }
                            }
                            if (slot >= 0) {
                                weights[slot] = weight;
                                bones[slot] = bone;
                            }
                        }
                        v.boneIndices = glm::ivec4(bones[0], bones[1], bones[2], bones[3]);
                        v.boneWeights = glm::vec4(weights[0], weights[1], weights[2], weights[3]);
                    }
                    NormalizeInfluences(v);
                }

                if (!mesh.skinned) {
                    v.boneWeights = glm::vec4(0.0f);
                    v.boneIndices = glm::ivec4(0);
                }

                aabbMin = glm::min(aabbMin, v.position);
                aabbMax = glm::max(aabbMax, v.position);

                VertexKey key{v};
                auto [it, inserted] = weld.emplace(key, static_cast<uint32_t>(mesh.vertices.size()));
                if (inserted) mesh.vertices.push_back(v);
                bucket.push_back(it->second);
            }
        }

        if (mesh.vertices.empty()) continue;

        for (auto& [material, indices] : buckets) {
            if (indices.empty()) continue;
            SubMesh sub;
            sub.indexOffset = static_cast<uint32_t>(mesh.indices.size());
            sub.indexCount = static_cast<uint32_t>(indices.size());
            sub.materialIndex = material;
            mesh.indices.insert(mesh.indices.end(), indices.begin(), indices.end());
            mesh.subMeshes.push_back(sub);
        }

        mesh.aabbMin = aabbMin;
        mesh.aabbMax = aabbMax;

        out.totalVertices += mesh.vertices.size();
        out.totalTriangles += mesh.indices.size() / 3;
        out.meshes.push_back(std::move(mesh));
    }
}

// ---------------------------------------------------------- animation import
bool TrackIsRestPose(const NodeTrack& track, const Node& rest, const ImportOptions& options) {
    for (const auto& key : track.positions) {
        if (glm::length(key.value - rest.translation) > options.epsilonPosition) return false;
    }
    for (const auto& key : track.rotations) {
        // Quaternions double-cover, so compare via |dot|.
        if (1.0f - std::fabs(glm::dot(key.value, rest.rotation)) > options.epsilonRotation) return false;
    }
    for (const auto& key : track.scales) {
        if (glm::length(key.value - rest.scale) > options.epsilonScale) return false;
    }
    return true;
}

template <typename T, typename EqualFn>
void CollapseConstant(std::vector<Key<T>>& keys, EqualFn equal) {
    if (keys.size() < 2) return;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (!equal(keys[i].value, keys[0].value)) return;
    }
    keys.resize(1);
    keys[0].time = 0.0f;
}

void ImportAnimations(const ufbx_scene* scene, const ImportOptions& options,
                      const std::string& sourceFile, Model& out,
                      const std::vector<int>& ufbxToModel) {
    const float rate = std::max(1.0f, options.sampleRate);

    for (size_t si = 0; si < scene->anim_stacks.count; ++si) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[si];

        const double begin = stack->time_begin;
        const double end = std::max(stack->time_end, stack->time_begin);
        const double lengthSeconds = end - begin;

        const int frameCount =
            std::max(1, static_cast<int>(std::lround(lengthSeconds * static_cast<double>(rate))) + 1);

        Animation anim;
        anim.name = ToStd(stack->name);
        if (anim.name.empty()) anim.name = "Take " + std::to_string(si + 1);
        anim.sourceFile = sourceFile;
        anim.sampleRate = rate;
        anim.duration = static_cast<float>(lengthSeconds);
        anim.tracks.reserve(scene->nodes.count);

        for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
            const ufbx_node* node = scene->nodes.data[ni];
            const int modelIndex = ufbxToModel[ni];
            if (modelIndex < 0) continue;

            NodeTrack track;
            track.nodeName = out.nodes[static_cast<size_t>(modelIndex)].name;
            track.nodeIndex = modelIndex;
            track.positions.reserve(static_cast<size_t>(frameCount));
            track.rotations.reserve(static_cast<size_t>(frameCount));
            track.scales.reserve(static_cast<size_t>(frameCount));

            glm::quat previous(1.0f, 0.0f, 0.0f, 0.0f);
            for (int f = 0; f < frameCount; ++f) {
                const double t = begin + static_cast<double>(f) / static_cast<double>(rate);
                const ufbx_transform xf =
                    ufbx_evaluate_transform(stack->anim, node, std::min(t, end));
                const float time = static_cast<float>(f) / rate;

                glm::quat rotation = ToGlm(xf.rotation);
                // Keep the quaternion path continuous so slerp never takes the long way.
                if (f > 0 && glm::dot(rotation, previous) < 0.0f) rotation = -rotation;
                previous = rotation;

                track.positions.push_back({time, ToGlm(xf.translation)});
                track.rotations.push_back({time, rotation});
                track.scales.push_back({time, ToGlm(xf.scale)});
            }

            const Node& rest = out.nodes[static_cast<size_t>(modelIndex)];
            if (TrackIsRestPose(track, rest, options)) continue;

            CollapseConstant(track.positions, [&](const glm::vec3& a, const glm::vec3& b) {
                return glm::length(a - b) <= options.epsilonPosition;
            });
            CollapseConstant(track.rotations, [&](const glm::quat& a, const glm::quat& b) {
                return 1.0f - std::fabs(glm::dot(a, b)) <= options.epsilonRotation;
            });
            CollapseConstant(track.scales, [&](const glm::vec3& a, const glm::vec3& b) {
                return glm::length(a - b) <= options.epsilonScale;
            });

            anim.tracks.push_back(std::move(track));
        }

        if (anim.tracks.empty()) {
            LogWarn("Animation stack '%s' has no moving nodes - skipped.", anim.name.c_str());
            continue;
        }

        anim.name = out.MakeUniqueAnimationName(anim.name);
        out.animations.push_back(std::move(anim));
    }
}

}  // namespace

ImportResult ImportFbx(const std::string& path, const ImportOptions& options, Model& out) {
    ImportResult result;
    out.Clear();

    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    // MODIFY_GEOMETRY (rather than ADJUST_TRANSFORMS) also rescales vertex data, so
    // mesh positions really are in metres. Without it a centimetre source would keep
    // 100x geometry while its transforms were converted, and the export scale would
    // then be applied on top of an inconsistent scene.
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;
    opts.load_external_files = true;
    opts.ignore_embedded = false;
    opts.evaluate_skinning = false;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        char buffer[1024];
        ufbx_format_error(buffer, sizeof(buffer), &error);
        result.error = buffer;
        return result;
    }

    const fs::path fbxDir = fs::path(path).parent_path();

    out.sourcePath = path;
    result.sourceUnitMeters = static_cast<float>(scene->settings.original_unit_meters);

    std::vector<int> ufbxToModel;
    ImportNodes(scene, out, ufbxToModel);
    ImportMaterials(scene, fbxDir, out);

    if (options.importGeometry) {
        ImportMeshes(scene, out, ufbxToModel);
    }
    if (options.importAnimations) {
        ImportAnimations(scene, options, path, out, ufbxToModel);
    }

    result.ok = true;
    result.nodeCount = out.nodes.size();
    result.meshCount = out.meshes.size();
    result.boneCount = out.skeleton.bones.size();
    result.animationCount = out.animations.size();

    ufbx_free_scene(scene);

    if (out.skeleton.bones.size() > static_cast<size_t>(kMaxBones)) {
        LogWarn("Skeleton has %zu bones; the preview shader supports %d. Extra bones render in bind pose.",
                out.skeleton.bones.size(), kMaxBones);
    }

    return result;
}

}  // namespace fam
