#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/Model.h"

namespace fam {

// Samples an animation into node-global matrices and skinning palettes.
// Keeps per-track key cursors so linear playback costs O(1) per track.
class PoseEvaluator {
public:
    void SetModel(const Model* model);

    // `time` in seconds. Pass nullptr for the bind pose.
    void Evaluate(const Animation* animation, float time);

    const std::vector<glm::mat4>& Globals() const { return m_globals; }
    const std::vector<glm::mat4>& BoneMatrices() const { return m_boneMatrices; }

    void InvalidateBinding() { m_boundAnimation = nullptr; }

private:
    void Rebind(const Animation* animation);

    const Model* m_model = nullptr;
    const Animation* m_boundAnimation = nullptr;

    std::vector<int> m_order;          // parents guaranteed before children
    std::vector<int> m_trackForNode;   // node index -> track index (-1 = rest pose)
    std::vector<glm::ivec3> m_cursors; // per-track (position, rotation, scale) key hints

    std::vector<glm::mat4> m_globals;
    std::vector<glm::mat4> m_boneMatrices;
};

glm::vec3 SampleVec3(const std::vector<Key<glm::vec3>>& keys, float time, int& cursor,
                     const glm::vec3& fallback);
glm::quat SampleQuat(const std::vector<Key<glm::quat>>& keys, float time, int& cursor,
                     const glm::quat& fallback);

}  // namespace fam
