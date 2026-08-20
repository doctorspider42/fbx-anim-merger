#include "core/Model.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace fam {

glm::mat4 Node::LocalMatrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), translation);
    m *= glm::mat4_cast(rotation);
    m = glm::scale(m, scale);
    return m;
}

void Model::Clear() {
    *this = Model{};
}

void Model::RebuildHierarchy() {
    nodeByName.clear();
    roots.clear();
    for (auto& n : nodes) n.children.clear();

    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        nodeByName.emplace(nodes[i].name, i);
        if (nodes[i].parent >= 0 && nodes[i].parent < static_cast<int>(nodes.size())) {
            nodes[nodes[i].parent].children.push_back(i);
        } else {
            nodes[i].parent = -1;
            roots.push_back(i);
        }
    }
}

int Model::FindNode(const std::string& name) const {
    auto it = nodeByName.find(name);
    return it == nodeByName.end() ? -1 : it->second;
}

void Model::ComputeBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    // Bind-pose world transforms.
    std::vector<glm::mat4> globals(nodes.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < nodes.size(); ++i) {
        const glm::mat4 local = nodes[i].LocalMatrix();
        globals[i] = nodes[i].parent >= 0 ? globals[nodes[i].parent] * local : local;
    }

    bool any = false;
    for (const Mesh& mesh : meshes) {
        const glm::mat4 xf = (mesh.skinned || mesh.nodeIndex < 0)
                                 ? glm::mat4(1.0f)
                                 : globals[static_cast<size_t>(mesh.nodeIndex)];
        // Transform the 8 AABB corners rather than the raw min/max.
        for (int c = 0; c < 8; ++c) {
            const glm::vec3 corner{(c & 1) ? mesh.aabbMax.x : mesh.aabbMin.x,
                                   (c & 2) ? mesh.aabbMax.y : mesh.aabbMin.y,
                                   (c & 4) ? mesh.aabbMax.z : mesh.aabbMin.z};
            const glm::vec3 p = glm::vec3(xf * glm::vec4(corner, 1.0f));
            outMin = glm::min(outMin, p);
            outMax = glm::max(outMax, p);
            any = true;
        }
    }

    if (!any) {
        outMin = glm::vec3(-0.5f);
        outMax = glm::vec3(0.5f);
    }
}

std::string Model::MakeUniqueAnimationName(const std::string& desired, int ignoreIndex) const {
    std::string base = desired.empty() ? std::string("Animation") : desired;

    auto taken = [&](const std::string& candidate) {
        for (int i = 0; i < static_cast<int>(animations.size()); ++i) {
            if (i == ignoreIndex) continue;
            if (animations[static_cast<size_t>(i)].name == candidate) return true;
        }
        return false;
    };

    if (!taken(base)) return base;
    for (int suffix = 1; suffix < 10000; ++suffix) {
        std::string candidate = base + "_" + std::to_string(suffix);
        if (!taken(candidate)) return candidate;
    }
    return base;
}

void NormalizeAnimation(Animation& anim) {
    auto sortKeys = [](auto& keys) {
        std::stable_sort(keys.begin(), keys.end(),
                         [](const auto& a, const auto& b) { return a.time < b.time; });
    };

    for (NodeTrack& track : anim.tracks) {
        sortKeys(track.positions);
        sortKeys(track.rotations);
        sortKeys(track.scales);
    }

    anim.tracks.erase(std::remove_if(anim.tracks.begin(), anim.tracks.end(),
                                     [](const NodeTrack& t) { return t.Empty(); }),
                      anim.tracks.end());

    float duration = 0.0f;
    for (const NodeTrack& track : anim.tracks) {
        if (!track.positions.empty()) duration = std::max(duration, track.positions.back().time);
        if (!track.rotations.empty()) duration = std::max(duration, track.rotations.back().time);
        if (!track.scales.empty()) duration = std::max(duration, track.scales.back().time);
    }
    anim.duration = std::max(anim.duration, duration);
}

namespace {

template <typename T, typename Mix>
std::vector<Key<T>> ResampleKeys(const std::vector<Key<T>>& source,
                                 const std::vector<float>& times, Mix mix) {
    if (source.size() <= 1) {
        std::vector<Key<T>> result = source;
        if (!result.empty()) result[0].time = 0.0f;
        return result;
    }

    std::vector<Key<T>> result;
    result.reserve(times.size());
    size_t cursor = 0;
    for (float time : times) {
        while (cursor + 1 < source.size() && source[cursor + 1].time <= time) ++cursor;

        T value = source.back().value;
        if (cursor + 1 < source.size()) {
            const Key<T>& a = source[cursor];
            const Key<T>& b = source[cursor + 1];
            const float span = b.time - a.time;
            const float blend = span > 1.0e-8f
                                    ? glm::clamp((time - a.time) / span, 0.0f, 1.0f)
                                    : 0.0f;
            value = mix(a.value, b.value, blend);
        }
        result.push_back({time, value});
    }
    return result;
}

}  // namespace

void ResampleAnimation(Animation& anim, float sampleRate) {
    if (!std::isfinite(sampleRate)) return;
    sampleRate = glm::clamp(sampleRate, kMinAnimationSampleRate, kMaxAnimationSampleRate);

    NormalizeAnimation(anim);
    const float duration = std::max(anim.duration, 0.0f);
    anim.sampleRate = sampleRate;
    if (duration <= 0.0f) return;

    // Keep a regular 1/FPS grid, then retain the exact source endpoint when the
    // duration is not an integer number of output frames. No key may sit beyond
    // mDuration: several FBX consumers behave badly when that invariant is broken.
    const int wholeFrames = static_cast<int>(std::floor(duration * sampleRate + 1.0e-5f));
    std::vector<float> times;
    times.reserve(static_cast<size_t>(wholeFrames) + 2);
    for (int frame = 0; frame <= wholeFrames; ++frame) {
        times.push_back(static_cast<float>(frame) / sampleRate);
    }
    if (times.empty() || duration - times.back() > 1.0e-5f) times.push_back(duration);
    else times.back() = duration;

    for (NodeTrack& track : anim.tracks) {
        track.positions = ResampleKeys(track.positions, times,
                                      [](const glm::vec3& a, const glm::vec3& b, float t) {
                                          return glm::mix(a, b, t);
                                      });
        track.scales = ResampleKeys(track.scales, times,
                                   [](const glm::vec3& a, const glm::vec3& b, float t) {
                                       return glm::mix(a, b, t);
                                   });
        track.rotations = ResampleKeys(track.rotations, times,
                                      [](const glm::quat& a, const glm::quat& b, float t) {
                                          return glm::normalize(glm::slerp(a, b, t));
                                      });

        // q and -q encode the same rotation. Keeping one hemisphere prevents an
        // exporter or engine from interpolating the equivalent values the long way.
        for (size_t i = 1; i < track.rotations.size(); ++i) {
            if (glm::dot(track.rotations[i - 1].value, track.rotations[i].value) < 0.0f) {
                track.rotations[i].value = -track.rotations[i].value;
            }
        }
    }
    anim.duration = duration;
}

}  // namespace fam
