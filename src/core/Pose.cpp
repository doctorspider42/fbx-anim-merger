#include "core/Pose.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace fam {
namespace {

// Finds the key index `i` such that keys[i].time <= time < keys[i+1].time,
// starting from the previous answer. Playback is almost always sequential, so the
// loop below usually runs zero or one iteration.
template <typename T>
int LocateKey(const std::vector<Key<T>>& keys, float time, int cursor) {
    const int last = static_cast<int>(keys.size()) - 1;
    if (cursor < 0 || cursor > last) cursor = 0;

    while (cursor < last && keys[static_cast<size_t>(cursor) + 1].time <= time) ++cursor;
    while (cursor > 0 && keys[static_cast<size_t>(cursor)].time > time) --cursor;
    return cursor;
}

float KeyBlend(float a, float b, float time) {
    const float span = b - a;
    if (span <= 1.0e-8f) return 0.0f;
    return glm::clamp((time - a) / span, 0.0f, 1.0f);
}

}  // namespace

glm::vec3 SampleVec3(const std::vector<Key<glm::vec3>>& keys, float time, int& cursor,
                     const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;

    cursor = LocateKey(keys, time, cursor);
    const size_t i = static_cast<size_t>(cursor);
    if (i + 1 >= keys.size()) return keys.back().value;

    const float t = KeyBlend(keys[i].time, keys[i + 1].time, time);
    return glm::mix(keys[i].value, keys[i + 1].value, t);
}

glm::quat SampleQuat(const std::vector<Key<glm::quat>>& keys, float time, int& cursor,
                     const glm::quat& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;

    cursor = LocateKey(keys, time, cursor);
    const size_t i = static_cast<size_t>(cursor);
    if (i + 1 >= keys.size()) return keys.back().value;

    const float t = KeyBlend(keys[i].time, keys[i + 1].time, time);
    return glm::normalize(glm::slerp(keys[i].value, keys[i + 1].value, t));
}

void PoseEvaluator::SetModel(const Model* model) {
    m_model = model;
    m_boundAnimation = nullptr;
    m_order.clear();
    m_globals.clear();
    m_boneMatrices.clear();
    if (!model) return;

    m_globals.assign(model->nodes.size(), glm::mat4(1.0f));
    m_boneMatrices.assign(model->skeleton.bones.size(), glm::mat4(1.0f));

    // Depth-first traversal guarantees parents are evaluated first, which lets the
    // per-frame pass be a single flat loop.
    m_order.reserve(model->nodes.size());
    std::vector<int> stack(model->roots.rbegin(), model->roots.rend());
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        m_order.push_back(node);
        const auto& children = model->nodes[static_cast<size_t>(node)].children;
        for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(*it);
    }
    // Defensive: pick up nodes unreachable from any root (malformed hierarchies).
    if (m_order.size() != model->nodes.size()) {
        std::vector<bool> seen(model->nodes.size(), false);
        for (int n : m_order) seen[static_cast<size_t>(n)] = true;
        for (size_t i = 0; i < model->nodes.size(); ++i) {
            if (!seen[i]) m_order.push_back(static_cast<int>(i));
        }
    }
}

void PoseEvaluator::Rebind(const Animation* animation) {
    m_trackForNode.assign(m_model->nodes.size(), -1);
    m_cursors.clear();
    if (!animation) {
        m_boundAnimation = nullptr;
        return;
    }

    m_cursors.assign(animation->tracks.size(), glm::ivec3(0));
    for (size_t i = 0; i < animation->tracks.size(); ++i) {
        const NodeTrack& track = animation->tracks[i];
        int nodeIndex = track.nodeIndex;
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_model->nodes.size()) ||
            m_model->nodes[static_cast<size_t>(nodeIndex)].name != track.nodeName) {
            nodeIndex = m_model->FindNode(track.nodeName);
        }
        if (nodeIndex >= 0) m_trackForNode[static_cast<size_t>(nodeIndex)] = static_cast<int>(i);
    }
    m_boundAnimation = animation;
}

void PoseEvaluator::Evaluate(const Animation* animation, float time) {
    if (!m_model) return;

    // The animation pointer alone is not enough to tell a rebind is unnecessary: a
    // freed clip's address can be handed straight back to its replacement. The sizes
    // catch that case, so a missed InvalidateBinding cannot index out of bounds.
    if (animation != m_boundAnimation || m_trackForNode.size() != m_model->nodes.size() ||
        (animation != nullptr && m_cursors.size() != animation->tracks.size())) {
        Rebind(animation);
    }

    for (int nodeIndex : m_order) {
        const size_t i = static_cast<size_t>(nodeIndex);
        const Node& node = m_model->nodes[i];

        glm::vec3 translation = node.translation;
        glm::quat rotation = node.rotation;
        glm::vec3 scale = node.scale;

        if (animation) {
            const int trackIndex = m_trackForNode[i];
            if (trackIndex >= 0) {
                const NodeTrack& track = animation->tracks[static_cast<size_t>(trackIndex)];
                glm::ivec3& cursor = m_cursors[static_cast<size_t>(trackIndex)];
                translation = SampleVec3(track.positions, time, cursor.x, translation);
                rotation = SampleQuat(track.rotations, time, cursor.y, rotation);
                scale = SampleVec3(track.scales, time, cursor.z, scale);
            }
        }

        glm::mat4 local = glm::translate(glm::mat4(1.0f), translation);
        local *= glm::mat4_cast(rotation);
        local = glm::scale(local, scale);

        m_globals[i] = node.parent >= 0 ? m_globals[static_cast<size_t>(node.parent)] * local : local;
    }

    for (size_t b = 0; b < m_model->skeleton.bones.size(); ++b) {
        const Bone& bone = m_model->skeleton.bones[b];
        if (bone.nodeIndex < 0) {
            m_boneMatrices[b] = glm::mat4(1.0f);
            continue;
        }
        m_boneMatrices[b] = m_globals[static_cast<size_t>(bone.nodeIndex)] * bone.inverseBind;
    }
}

}  // namespace fam
