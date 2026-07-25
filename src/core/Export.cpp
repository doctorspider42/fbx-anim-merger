#include "core/Export.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

#include "util/Log.h"

namespace fs = std::filesystem;

namespace fam {
namespace {

aiMatrix4x4 ToAssimp(const glm::mat4& m) {
    // glm is column-major, aiMatrix4x4 is row-major.
    return aiMatrix4x4(m[0][0], m[1][0], m[2][0], m[3][0],
                       m[0][1], m[1][1], m[2][1], m[3][1],
                       m[0][2], m[1][2], m[2][2], m[3][2],
                       m[0][3], m[1][3], m[2][3], m[3][3]);
}

aiString ToAssimp(const std::string& s) {
    aiString out;
    out.Set(s);
    return out;
}

// Uniformly scaling a rig means scaling geometry, every node translation, the
// translation column of each inverse bind matrix, and every position key.
void ApplyScale(Model& model, float scale) {
    if (std::abs(scale - 1.0f) < 1.0e-6f) return;

    for (Node& node : model.nodes) node.translation *= scale;

    for (Mesh& mesh : model.meshes) {
        for (Vertex& v : mesh.vertices) v.position *= scale;
        mesh.aabbMin *= scale;
        mesh.aabbMax *= scale;
    }

    for (Bone& bone : model.skeleton.bones) {
        bone.inverseBind[3] = glm::vec4(glm::vec3(bone.inverseBind[3]) * scale, 1.0f);
    }

    for (Animation& anim : model.animations) {
        for (NodeTrack& track : anim.tracks) {
            for (auto& key : track.positions) key.value *= scale;
        }
    }
}

std::shared_ptr<const std::vector<uint8_t>> ReadFileBytes(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return nullptr;
    auto bytes = std::make_shared<std::vector<uint8_t>>(std::istreambuf_iterator<char>(stream),
                                                        std::istreambuf_iterator<char>());
    if (bytes->empty()) return nullptr;
    return bytes;
}

const char* FormatHintFromPath(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".jpg" || ext == ".jpeg") return "jpg";
    if (ext == ".png") return "png";
    if (ext == ".tga") return "tga";
    if (ext == ".bmp") return "bmp";
    if (ext == ".dds") return "dds";
    return "";
}

// --------------------------------------------------------------- node export
struct NodeExport {
    std::vector<aiNode*> nodes;
    aiNode* root = nullptr;
};

NodeExport BuildNodes(const Model& model) {
    NodeExport out;
    out.nodes.resize(model.nodes.size(), nullptr);

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        auto* node = new aiNode();
        node->mName = ToAssimp(model.nodes[i].name);
        node->mTransformation = ToAssimp(model.nodes[i].LocalMatrix());
        out.nodes[i] = node;
    }

    out.root = new aiNode();
    out.root->mName.Set("RootNode");

    std::vector<std::vector<aiNode*>> children(model.nodes.size());
    std::vector<aiNode*> rootChildren;

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const int parent = model.nodes[i].parent;
        if (parent >= 0) {
            children[static_cast<size_t>(parent)].push_back(out.nodes[i]);
            out.nodes[i]->mParent = out.nodes[static_cast<size_t>(parent)];
        } else {
            rootChildren.push_back(out.nodes[i]);
            out.nodes[i]->mParent = out.root;
        }
    }

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& list = children[i];
        if (list.empty()) continue;
        out.nodes[i]->mNumChildren = static_cast<unsigned>(list.size());
        out.nodes[i]->mChildren = new aiNode*[list.size()];
        std::copy(list.begin(), list.end(), out.nodes[i]->mChildren);
    }

    out.root->mNumChildren = static_cast<unsigned>(rootChildren.size());
    if (!rootChildren.empty()) {
        out.root->mChildren = new aiNode*[rootChildren.size()];
        std::copy(rootChildren.begin(), rootChildren.end(), out.root->mChildren);
    }

    return out;
}

// --------------------------------------------------------------- mesh export
// One aiMesh per (mesh, submesh) pair because assimp binds a single material per
// mesh. Vertices are compacted so each aiMesh only carries what it references.
struct BuiltMesh {
    aiMesh* mesh = nullptr;
    int sourceNode = -1;
    bool skinned = false;
};

std::vector<BuiltMesh> BuildMeshes(const Model& model) {
    std::vector<BuiltMesh> result;

    for (const Mesh& mesh : model.meshes) {
        for (const SubMesh& sub : mesh.subMeshes) {
            if (sub.indexCount == 0) continue;

            std::unordered_map<uint32_t, uint32_t> remap;
            remap.reserve(sub.indexCount);
            std::vector<uint32_t> sourceVertices;
            std::vector<uint32_t> localIndices;
            localIndices.reserve(sub.indexCount);

            for (uint32_t i = 0; i < sub.indexCount; ++i) {
                const uint32_t src = mesh.indices[sub.indexOffset + i];
                auto [it, inserted] =
                    remap.emplace(src, static_cast<uint32_t>(sourceVertices.size()));
                if (inserted) sourceVertices.push_back(src);
                localIndices.push_back(it->second);
            }

            auto* out = new aiMesh();
            out->mName = ToAssimp(mesh.subMeshes.size() > 1
                                      ? mesh.name + "_" + std::to_string(result.size())
                                      : mesh.name);
            out->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
            out->mMaterialIndex =
                static_cast<unsigned>(sub.materialIndex >= 0 ? sub.materialIndex : 0);

            const size_t vertexCount = sourceVertices.size();
            out->mNumVertices = static_cast<unsigned>(vertexCount);
            out->mVertices = new aiVector3D[vertexCount];
            out->mNormals = new aiVector3D[vertexCount];
            out->mTextureCoords[0] = new aiVector3D[vertexCount];
            out->mNumUVComponents[0] = 2;

            for (size_t i = 0; i < vertexCount; ++i) {
                const Vertex& v = mesh.vertices[sourceVertices[i]];
                out->mVertices[i] = aiVector3D(v.position.x, v.position.y, v.position.z);
                out->mNormals[i] = aiVector3D(v.normal.x, v.normal.y, v.normal.z);
                out->mTextureCoords[0][i] = aiVector3D(v.uv.x, v.uv.y, 0.0f);
            }

            const size_t faceCount = localIndices.size() / 3;
            out->mNumFaces = static_cast<unsigned>(faceCount);
            out->mFaces = new aiFace[faceCount];
            for (size_t f = 0; f < faceCount; ++f) {
                aiFace& face = out->mFaces[f];
                face.mNumIndices = 3;
                face.mIndices = new unsigned[3];
                face.mIndices[0] = localIndices[f * 3 + 0];
                face.mIndices[1] = localIndices[f * 3 + 1];
                face.mIndices[2] = localIndices[f * 3 + 2];
            }

            if (mesh.skinned && !model.skeleton.bones.empty()) {
                std::vector<std::vector<aiVertexWeight>> weights(model.skeleton.bones.size());
                for (size_t i = 0; i < vertexCount; ++i) {
                    const Vertex& v = mesh.vertices[sourceVertices[i]];
                    for (int k = 0; k < kMaxBoneInfluences; ++k) {
                        const float w = v.boneWeights[k];
                        if (w <= 1.0e-5f) continue;
                        const int bone = v.boneIndices[k];
                        if (bone < 0 || bone >= static_cast<int>(weights.size())) continue;
                        weights[static_cast<size_t>(bone)].push_back(
                            aiVertexWeight(static_cast<unsigned>(i), w));
                    }
                }

                std::vector<aiBone*> bones;
                for (size_t b = 0; b < weights.size(); ++b) {
                    if (weights[b].empty()) continue;
                    const Bone& src = model.skeleton.bones[b];
                    if (src.nodeIndex < 0) continue;

                    auto* bone = new aiBone();
                    bone->mName = ToAssimp(model.nodes[static_cast<size_t>(src.nodeIndex)].name);
                    bone->mOffsetMatrix = ToAssimp(src.inverseBind);
                    bone->mNumWeights = static_cast<unsigned>(weights[b].size());
                    bone->mWeights = new aiVertexWeight[weights[b].size()];
                    std::copy(weights[b].begin(), weights[b].end(), bone->mWeights);
                    bones.push_back(bone);
                }

                if (!bones.empty()) {
                    out->mNumBones = static_cast<unsigned>(bones.size());
                    out->mBones = new aiBone*[bones.size()];
                    std::copy(bones.begin(), bones.end(), out->mBones);
                }
            }

            result.push_back({out, mesh.nodeIndex, mesh.skinned});
        }
    }

    return result;
}

// ---------------------------------------------------------- animation export
aiAnimation* BuildAnimation(const Model& model, const Animation& anim) {
    auto* out = new aiAnimation();
    out->mName = ToAssimp(anim.name);
    out->mTicksPerSecond = static_cast<double>(anim.sampleRate);
    out->mDuration = static_cast<double>(anim.duration * anim.sampleRate);

    std::vector<aiNodeAnim*> channels;
    channels.reserve(anim.tracks.size());

    for (const NodeTrack& track : anim.tracks) {
        const int nodeIndex = track.nodeIndex >= 0 ? track.nodeIndex : model.FindNode(track.nodeName);
        const Node* rest = nodeIndex >= 0 ? &model.nodes[static_cast<size_t>(nodeIndex)] : nullptr;

        auto* channel = new aiNodeAnim();
        channel->mNodeName = ToAssimp(track.nodeName);

        // Downstream tools assume every channel carries all three components, so
        // missing ones are filled from the rest pose.
        const size_t posCount = std::max<size_t>(track.positions.size(), 1);
        channel->mNumPositionKeys = static_cast<unsigned>(posCount);
        channel->mPositionKeys = new aiVectorKey[posCount];
        for (size_t i = 0; i < posCount; ++i) {
            const glm::vec3 value = i < track.positions.size()
                                        ? track.positions[i].value
                                        : (rest ? rest->translation : glm::vec3(0.0f));
            const double time = i < track.positions.size()
                                    ? track.positions[i].time * anim.sampleRate
                                    : 0.0;
            channel->mPositionKeys[i] = aiVectorKey(time, aiVector3D(value.x, value.y, value.z));
        }

        const size_t rotCount = std::max<size_t>(track.rotations.size(), 1);
        channel->mNumRotationKeys = static_cast<unsigned>(rotCount);
        channel->mRotationKeys = new aiQuatKey[rotCount];
        for (size_t i = 0; i < rotCount; ++i) {
            const glm::quat value = i < track.rotations.size()
                                        ? track.rotations[i].value
                                        : (rest ? rest->rotation : glm::quat(1, 0, 0, 0));
            const double time = i < track.rotations.size()
                                    ? track.rotations[i].time * anim.sampleRate
                                    : 0.0;
            channel->mRotationKeys[i] = aiQuatKey(time, aiQuaternion(value.w, value.x, value.y, value.z));
        }

        const size_t scaleCount = std::max<size_t>(track.scales.size(), 1);
        channel->mNumScalingKeys = static_cast<unsigned>(scaleCount);
        channel->mScalingKeys = new aiVectorKey[scaleCount];
        for (size_t i = 0; i < scaleCount; ++i) {
            const glm::vec3 value = i < track.scales.size() ? track.scales[i].value
                                                            : (rest ? rest->scale : glm::vec3(1.0f));
            const double time = i < track.scales.size() ? track.scales[i].time * anim.sampleRate : 0.0;
            channel->mScalingKeys[i] = aiVectorKey(time, aiVector3D(value.x, value.y, value.z));
        }

        channels.push_back(channel);
    }

    out->mNumChannels = static_cast<unsigned>(channels.size());
    if (!channels.empty()) {
        out->mChannels = new aiNodeAnim*[channels.size()];
        std::copy(channels.begin(), channels.end(), out->mChannels);
    }
    return out;
}

}  // namespace

float DefaultScaleFor(ExportFormat format) {
    switch (format) {
        case ExportFormat::FbxBinary:
        case ExportFormat::FbxAscii:
            return 100.0f;  // metres -> FBX centimetres
        default:
            return 1.0f;    // glTF is metres
    }
}

const char* AssimpFormatId(ExportFormat format) {
    switch (format) {
        case ExportFormat::FbxBinary:    return "fbx";
        case ExportFormat::FbxAscii:     return "fbxa";
        case ExportFormat::Glb:          return "glb2";
        case ExportFormat::GltfSeparate: return "gltf2";
    }
    return "fbx";
}

const char* DefaultExtension(ExportFormat format) {
    switch (format) {
        case ExportFormat::FbxBinary:
        case ExportFormat::FbxAscii:
            return "fbx";
        case ExportFormat::Glb:          return "glb";
        case ExportFormat::GltfSeparate: return "gltf";
    }
    return "fbx";
}

ExportResult ExportModel(const Model& source, const std::string& path, const ExportOptions& options) {
    ExportResult result;

    if (!source.Valid()) {
        result.error = "Nothing to export - no model loaded.";
        return result;
    }

    Model model = source;
    if (!options.includeGeometry) {
        model.meshes.clear();
    }
    ApplyScale(model, options.scale);

    // glTF places the UV origin at the top-left of the image, FBX at the bottom-left.
    // Everything is held in FBX convention, so glTF targets need the flip.
    if (options.format == ExportFormat::Glb || options.format == ExportFormat::GltfSeparate) {
        for (Mesh& mesh : model.meshes) {
            for (Vertex& vertex : mesh.vertices) vertex.uv.y = 1.0f - vertex.uv.y;
        }
    }

    std::vector<int> animIndices = options.animations;
    if (animIndices.empty()) {
        animIndices.resize(model.animations.size());
        for (size_t i = 0; i < animIndices.size(); ++i) animIndices[i] = static_cast<int>(i);
    }

    aiScene scene;
    scene.mName.Set("FbxAnimMerger");
    scene.mMetaData = new aiMetadata();

    // FBX's UnitScaleFactor is "centimetres per unit". The scene is metres before
    // ApplyScale, so after it one unit measures 100/scale centimetres. Declaring it
    // keeps the file self-describing whatever scale the user picked.
    const double unitScaleFactor = 100.0 / static_cast<double>(std::max(options.scale, 1.0e-6f));
    scene.mMetaData->Add("UnitScaleFactor", unitScaleFactor);
    scene.mMetaData->Add("OriginalUnitScaleFactor", unitScaleFactor);

    // -------------------------------------------------------------- materials
    const size_t materialCount = std::max<size_t>(model.materials.size(), 1);
    scene.mNumMaterials = static_cast<unsigned>(materialCount);
    scene.mMaterials = new aiMaterial*[materialCount];

    struct EmbeddedTexture {
        std::string name;
        std::string hint;
        std::shared_ptr<const std::vector<uint8_t>> bytes;
    };
    std::vector<EmbeddedTexture> embedded;

    // Resolves one texture into either a file reference or an embedded blob, then
    // binds it to every assimp slot the target writers look at.
    auto addTexture = [&](aiMaterial* material, const TextureSource& source,
                          std::initializer_list<aiTextureType> slots) {
        if (source.Empty()) return;

        const std::string hint = FormatHintFromPath(source.name.empty() ? source.path : source.name);

        // A texture that only exists inside the source FBX has to be embedded no
        // matter what the checkbox says - a path reference to it would dangle.
        const bool mustEmbed = source.path.empty();
        std::string reference = source.path;

        if ((options.embedTextures || mustEmbed) && !hint.empty()) {
            std::shared_ptr<const std::vector<uint8_t>> bytes = source.content;
            if (!bytes) bytes = ReadFileBytes(source.path);
            if (bytes) {
                reference = "*" + std::to_string(embedded.size());
                embedded.push_back({source.name, hint, std::move(bytes)});
            }
        }

        if (reference.empty()) {
            LogWarn("Texture '%s' could not be resolved for export.", source.Key().c_str());
            return;
        }

        aiString value = ToAssimp(reference);
        for (aiTextureType slot : slots) {
            material->AddProperty(&value, AI_MATKEY_TEXTURE(slot, 0));
        }
    };

    for (size_t i = 0; i < materialCount; ++i) {
        auto* material = new aiMaterial();
        const Material src = i < model.materials.size() ? model.materials[i] : Material{};

        aiString name = ToAssimp(src.name);
        material->AddProperty(&name, AI_MATKEY_NAME);

        const aiColor3D diffuse(src.baseColor.x, src.baseColor.y, src.baseColor.z);
        material->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
        const aiColor4D base(src.baseColor.x, src.baseColor.y, src.baseColor.z, src.opacity);
        material->AddProperty(&base, 1, AI_MATKEY_BASE_COLOR);
        material->AddProperty(&src.metallic, 1, AI_MATKEY_METALLIC_FACTOR);
        material->AddProperty(&src.roughness, 1, AI_MATKEY_ROUGHNESS_FACTOR);
        material->AddProperty(&src.opacity, 1, AI_MATKEY_OPACITY);

        addTexture(material, src.baseColorTexture,
                   {aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR});
        addTexture(material, src.normalTexture, {aiTextureType_NORMALS});

        scene.mMaterials[i] = material;
    }

    // ------------------------------------------------------ embedded textures
    if (!embedded.empty()) {
        std::vector<aiTexture*> textures;
        textures.reserve(embedded.size());
        for (const EmbeddedTexture& entry : embedded) {
            const std::vector<uint8_t>& bytes = *entry.bytes;

            auto* texture = new aiTexture();
            // mHeight == 0 marks the payload as a compressed file, not raw texels.
            texture->mHeight = 0;
            texture->mWidth = static_cast<unsigned>(bytes.size());
            std::snprintf(texture->achFormatHint, sizeof(texture->achFormatHint), "%s",
                          entry.hint.c_str());
            texture->pcData = reinterpret_cast<aiTexel*>(new char[bytes.size()]);
            std::memcpy(texture->pcData, bytes.data(), bytes.size());
            texture->mFilename = ToAssimp(entry.name);
            textures.push_back(texture);
        }
        LogInfo("Embedding %zu texture(s) into the export.", textures.size());
        scene.mNumTextures = static_cast<unsigned>(textures.size());
        scene.mTextures = new aiTexture*[textures.size()];
        std::copy(textures.begin(), textures.end(), scene.mTextures);
    }

    // ------------------------------------------------------------ hierarchy
    NodeExport hierarchy = BuildNodes(model);
    scene.mRootNode = hierarchy.root;

    // ---------------------------------------------------------------- meshes
    std::vector<BuiltMesh> meshes = BuildMeshes(model);
    if (!meshes.empty()) {
        scene.mNumMeshes = static_cast<unsigned>(meshes.size());
        scene.mMeshes = new aiMesh*[meshes.size()];
        for (size_t i = 0; i < meshes.size(); ++i) scene.mMeshes[i] = meshes[i].mesh;

        // Skinned geometry lives in a bone-relative space, so it must not inherit a
        // node transform; parent it to the root instead.
        std::unordered_map<aiNode*, std::vector<unsigned>> attachment;
        for (size_t i = 0; i < meshes.size(); ++i) {
            aiNode* owner = hierarchy.root;
            if (!meshes[i].skinned && meshes[i].sourceNode >= 0 &&
                meshes[i].sourceNode < static_cast<int>(hierarchy.nodes.size())) {
                owner = hierarchy.nodes[static_cast<size_t>(meshes[i].sourceNode)];
            }
            attachment[owner].push_back(static_cast<unsigned>(i));
        }
        for (auto& [node, list] : attachment) {
            node->mNumMeshes = static_cast<unsigned>(list.size());
            node->mMeshes = new unsigned[list.size()];
            std::copy(list.begin(), list.end(), node->mMeshes);
        }
    }

    // ------------------------------------------------------------ animations
    std::vector<aiAnimation*> animations;
    animations.reserve(animIndices.size());
    for (int index : animIndices) {
        if (index < 0 || index >= static_cast<int>(model.animations.size())) continue;
        animations.push_back(BuildAnimation(model, model.animations[static_cast<size_t>(index)]));
    }
    if (!animations.empty()) {
        scene.mNumAnimations = static_cast<unsigned>(animations.size());
        scene.mAnimations = new aiAnimation*[animations.size()];
        std::copy(animations.begin(), animations.end(), scene.mAnimations);
    }

    if (scene.mNumMeshes == 0 && scene.mNumAnimations == 0) {
        result.error = "Nothing selected for export (no geometry and no animations).";
        return result;
    }
    // assimp refuses scenes flagged as incomplete unless we say so explicitly.
    if (scene.mNumMeshes == 0) {
        scene.mFlags |= AI_SCENE_FLAGS_INCOMPLETE;
    }

    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    Assimp::Exporter exporter;
    const aiReturn status = exporter.Export(&scene, AssimpFormatId(options.format), path, 0);
    if (status != AI_SUCCESS) {
        const char* message = exporter.GetErrorString();
        result.error = message && *message ? message : "assimp export failed";
        return result;
    }

    result.ok = true;
    result.meshCount = scene.mNumMeshes;
    result.animationCount = scene.mNumAnimations;
    return result;
}

}  // namespace fam
