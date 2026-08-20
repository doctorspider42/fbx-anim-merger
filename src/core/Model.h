// Renderer- and format-agnostic scene representation.
//
// Everything that enters the application is normalised into this structure:
// right-handed, Y-up, 1 unit = 1 metre, TRS keyframe animation tracks. That is
// what makes merging clips coming from wildly different DCC exports possible at
// all -- without it a Blender Z-up/metre rig and a Mixamo Y-up/centimetre clip
// would never line up.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace fam {

inline constexpr int kMaxBoneInfluences = 4;
inline constexpr int kMaxBones = 200;  // must match the GLSL uniform block
inline constexpr float kMinAnimationSampleRate = 1.0f;
inline constexpr float kMaxAnimationSampleRate = 240.0f;

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::ivec4 boneIndices{0};
    glm::vec4 boneWeights{0.0f};
};

struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    int materialIndex = -1;
};

struct Mesh {
    std::string name;
    int nodeIndex = -1;  // node the (unskinned) geometry hangs off
    bool skinned = false;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;

    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};

// An image either sitting on disk or carried inside the FBX itself. Packages that
// bundle their skins (Mixamo, most marketplace assets) embed the bytes and leave
// behind file paths from the machine that authored them, so a path-only
// representation loses the texture entirely.
struct TextureSource {
    std::string name;  // "Arissa_DIFF_diffuse.png"
    std::string path;  // absolute path, empty when the image is embedded only
    // Shared so that copying a Model (as export does) stays cheap.
    std::shared_ptr<const std::vector<uint8_t>> content;

    bool Empty() const { return path.empty() && !content; }
    bool Embedded() const { return static_cast<bool>(content); }
    const std::string& Key() const { return path.empty() ? name : path; }
};

struct Material {
    std::string name = "material";
    glm::vec3 baseColor{0.8f, 0.8f, 0.8f};
    float opacity = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.6f;
    TextureSource baseColorTexture;
    TextureSource normalTexture;  // carried through to export; the preview ignores it
};

struct Node {
    std::string name;
    int parent = -1;
    std::vector<int> children;

    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 LocalMatrix() const;
};

struct Bone {
    int nodeIndex = -1;
    glm::mat4 inverseBind{1.0f};  // mesh/geometry space -> bone space
};

struct Skeleton {
    std::vector<Bone> bones;
    std::unordered_map<std::string, int> boneByName;

    bool Empty() const { return bones.empty(); }
};

template <typename T>
struct Key {
    float time = 0.0f;
    T value{};
};

struct NodeTrack {
    std::string nodeName;
    int nodeIndex = -1;  // resolved lazily against the target model

    std::vector<Key<glm::vec3>> positions;
    std::vector<Key<glm::quat>> rotations;
    std::vector<Key<glm::vec3>> scales;

    bool Empty() const { return positions.empty() && rotations.empty() && scales.empty(); }
};

struct Animation {
    std::string name = "Take 001";
    std::string sourceFile;
    float duration = 0.0f;    // seconds
    float sampleRate = 30.0f; // fps used when baking
    std::vector<NodeTrack> tracks;

    bool exportSelected = true;

    int FrameCount() const {
        size_t count = 0;
        for (const NodeTrack& track : tracks) {
            count = std::max(count, track.positions.size());
            count = std::max(count, track.rotations.size());
            count = std::max(count, track.scales.size());
        }
        return count > 0 ? static_cast<int>(count)
                         : static_cast<int>(duration * sampleRate + 0.5f) + 1;
    }
};

struct Model {
    std::string sourcePath;

    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    Skeleton skeleton;
    std::vector<Animation> animations;

    std::unordered_map<std::string, int> nodeByName;
    std::vector<int> roots;

    // Stats kept for the UI.
    size_t totalVertices = 0;
    size_t totalTriangles = 0;

    bool Valid() const { return !nodes.empty(); }
    void Clear();

    // Rebuilds nodeByName / roots / children from parent indices.
    void RebuildHierarchy();

    int FindNode(const std::string& name) const;

    // Union of all mesh bounds in bind pose (world space).
    void ComputeBounds(glm::vec3& outMin, glm::vec3& outMax) const;

    // Guarantees a name that is not yet used by any other animation.
    std::string MakeUniqueAnimationName(const std::string& desired, int ignoreIndex = -1) const;
};

// Sorts keys by time and removes tracks that carry no information.
void NormalizeAnimation(Animation& anim);

// Rebuilds every non-constant channel on a new frame grid while preserving the
// clip's duration in seconds. This changes both preview playback and the keys
// written on export; it is not merely an FPS metadata edit.
void ResampleAnimation(Animation& anim, float sampleRate);

}  // namespace fam
