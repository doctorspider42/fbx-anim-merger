#include "core/AnimMerge.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

#include "util/Log.h"

namespace fam {
namespace {

std::string NormalizeName(std::string name, const MergeOptions& options) {
    if (options.stripNamespace) {
        const size_t pos = name.find_last_of(":|");
        if (pos != std::string::npos && pos + 1 < name.size()) {
            name = name.substr(pos + 1);
        }
    }
    if (options.caseInsensitive) {
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    return name;
}

// Resolver from a source track name to a target node index.
class NodeResolver {
public:
    NodeResolver(const Model& target, const MergeOptions& options) : m_options(options) {
        for (int i = 0; i < static_cast<int>(target.nodes.size()); ++i) {
            const std::string key = NormalizeName(target.nodes[static_cast<size_t>(i)].name, options);
            // First writer wins; a rig with colliding normalised names is already
            // ambiguous and picking deterministically beats picking randomly.
            m_normalized.emplace(key, i);
        }
    }

    int Resolve(const Model& target, const std::string& name) const {
        const int exact = target.FindNode(name);
        if (exact >= 0) return exact;

        auto it = m_normalized.find(NormalizeName(name, m_options));
        return it == m_normalized.end() ? -1 : it->second;
    }

private:
    MergeOptions m_options;
    std::unordered_map<std::string, int> m_normalized;
};

constexpr size_t kMaxReportedUnmatched = 24;

// Marks bones that have no bone among their ancestors - the hips of a character
// rig, or one entry per disjoint skeleton. These are the only bones whose
// translation is motion rather than a restatement of the source rig's proportions.
std::vector<bool> FindRootBones(const Model& model) {
    std::vector<bool> isRootBone(model.nodes.size(), false);
    if (model.skeleton.Empty()) return isRootBone;

    std::vector<bool> isBone(model.nodes.size(), false);
    for (const Bone& bone : model.skeleton.bones) {
        if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int>(isBone.size())) {
            isBone[static_cast<size_t>(bone.nodeIndex)] = true;
        }
    }

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        if (!isBone[i]) continue;
        bool ancestorIsBone = false;
        for (int parent = model.nodes[i].parent; parent >= 0;
             parent = model.nodes[static_cast<size_t>(parent)].parent) {
            if (isBone[static_cast<size_t>(parent)]) {
                ancestorIsBone = true;
                break;
            }
        }
        isRootBone[i] = !ancestorIsBone;
    }
    return isRootBone;
}

bool IsBoneNode(const Model& model, int nodeIndex) {
    if (nodeIndex < 0) return false;
    return model.skeleton.boneByName.count(model.nodes[static_cast<size_t>(nodeIndex)].name) != 0;
}

// Rest-pose world position of a node, walking the parent chain. Only the
// translation is accumulated through parent rotations, which is all the hip-height
// heuristic needs and avoids building a full matrix stack.
glm::vec3 RestGlobalTranslation(const Model& model, int nodeIndex) {
    glm::mat4 accumulated(1.0f);
    std::vector<int> chain;
    for (int i = nodeIndex; i >= 0; i = model.nodes[static_cast<size_t>(i)].parent) {
        chain.push_back(i);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        accumulated *= model.nodes[static_cast<size_t>(*it)].LocalMatrix();
    }
    return glm::vec3(accumulated[3]);
}

// How much taller the target rig stands than the source, measured at the root bone.
float HipHeightRatio(const Model& target, int targetNode, const Model& source, int sourceNode) {
    const float targetHeight = RestGlobalTranslation(target, targetNode).y;
    const float sourceHeight = RestGlobalTranslation(source, sourceNode).y;
    if (std::fabs(sourceHeight) < 1.0e-3f || std::fabs(targetHeight) < 1.0e-3f) return 1.0f;
    return glm::clamp(targetHeight / sourceHeight, 0.05f, 20.0f);
}

template <typename T>
bool IsConstant(const std::vector<Key<T>>& keys, float epsilon) {
    if (keys.size() < 2) return true;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (glm::length(keys[i].value - keys[0].value) > epsilon) return false;
    }
    return true;
}

// Returns true if anything was removed.
void ApplyPolicyToTrack(NodeTrack& track, bool isBone, bool isRootBone,
                        const MergeOptions& options, MergeReport& report) {
    bool stripTranslation = false;
    switch (options.translationMode) {
        case TranslationMode::RootBonesOnly:
            stripTranslation = isBone && !isRootBone;
            break;
        case TranslationMode::AnimatedOnly:
            stripTranslation = IsConstant(track.positions, 1.0e-5f);
            break;
        case TranslationMode::CopyAll:
            break;
    }

    if (stripTranslation && !track.positions.empty()) {
        track.positions.clear();
        ++report.translationChannelsStripped;
    }
    if (options.ignoreScaleTracks && !track.scales.empty()) {
        track.scales.clear();
        ++report.scaleChannelsStripped;
    }
}

}  // namespace

float EstimateCompatibility(const Model& target, const Model& source, const MergeOptions& options) {
    if (source.animations.empty() || target.nodes.empty()) return 0.0f;

    const NodeResolver resolver(target, options);
    std::unordered_set<std::string> animated;
    for (const Animation& anim : source.animations) {
        for (const NodeTrack& track : anim.tracks) animated.insert(track.nodeName);
    }
    if (animated.empty()) return 0.0f;

    size_t matched = 0;
    for (const std::string& name : animated) {
        if (resolver.Resolve(target, name) >= 0) ++matched;
    }
    return static_cast<float>(matched) / static_cast<float>(animated.size());
}

MergeReport MergeAnimations(Model& target, const Model& source, const MergeOptions& options,
                            const std::vector<int>& sourceIndices) {
    MergeReport report;
    if (!target.Valid()) {
        LogError("Cannot merge: no base model loaded.");
        return report;
    }

    const NodeResolver resolver(target, options);
    const std::vector<bool> isRootBone = FindRootBones(target);

    std::vector<int> indices = sourceIndices;
    if (indices.empty()) {
        indices.resize(source.animations.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<int>(i);
    }

    std::unordered_set<std::string> unmatchedSeen;

    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(source.animations.size())) continue;
        const Animation& src = source.animations[static_cast<size_t>(index)];

        Animation merged;
        merged.sourceFile = src.sourceFile;
        merged.duration = src.duration;
        merged.sampleRate = src.sampleRate;
        merged.tracks.reserve(src.tracks.size());

        for (const NodeTrack& track : src.tracks) {
            const int nodeIndex = resolver.Resolve(target, track.nodeName);
            if (nodeIndex < 0) {
                ++report.tracksDropped;
                if (unmatchedSeen.insert(track.nodeName).second &&
                    report.unmatchedNodes.size() < kMaxReportedUnmatched) {
                    report.unmatchedNodes.push_back(track.nodeName);
                }
                continue;
            }
            if (options.skeletonTracksOnly && !target.skeleton.Empty()) {
                const std::string& targetName = target.nodes[static_cast<size_t>(nodeIndex)].name;
                if (target.skeleton.boneByName.find(targetName) == target.skeleton.boneByName.end()) {
                    ++report.tracksDropped;
                    continue;
                }
            }

            NodeTrack bound = track;
            bound.nodeName = target.nodes[static_cast<size_t>(nodeIndex)].name;
            bound.nodeIndex = nodeIndex;
            const bool trackIsRootBone = isRootBone[static_cast<size_t>(nodeIndex)];
            ApplyPolicyToTrack(bound, IsBoneNode(target, nodeIndex), trackIsRootBone, options,
                               report);

            // Root motion is the one translation we keep, so it is also the one that
            // has to be re-expressed in the target rig's proportions: anchor it to the
            // target's rest pose and scale the displacement by the hip-height ratio.
            const int sourceNode = source.FindNode(track.nodeName);
            if (options.retargetRootMotion && trackIsRootBone && !bound.positions.empty() &&
                sourceNode >= 0) {
                const glm::vec3 sourceRest = source.nodes[static_cast<size_t>(sourceNode)].translation;
                const glm::vec3 targetRest = target.nodes[static_cast<size_t>(nodeIndex)].translation;
                const float ratio = HipHeightRatio(target, nodeIndex, source, sourceNode);

                for (auto& key : bound.positions) {
                    key.value = targetRest + (key.value - sourceRest) * ratio;
                }
                report.rootMotionScale = ratio;
                ++report.rootTracksRetargeted;
            }

            if (bound.Empty()) {
                ++report.tracksDropped;
                continue;
            }
            merged.tracks.push_back(std::move(bound));
            ++report.tracksMatched;
        }

        if (merged.tracks.empty()) {
            LogWarn("Clip '%s': no track matched the base skeleton - not merged.", src.name.c_str());
            continue;
        }

        NormalizeAnimation(merged);
        merged.name = target.MakeUniqueAnimationName(options.namePrefix + src.name);
        report.addedNames.push_back(merged.name);
        target.animations.push_back(std::move(merged));
        ++report.animationsAdded;
    }

    return report;
}

MergeReport ApplyTrackPolicy(Model& model, const MergeOptions& options, bool includeSourceClips) {
    MergeReport report;
    if (!model.Valid()) return report;

    const std::vector<bool> isRootBone = FindRootBones(model);

    for (Animation& anim : model.animations) {
        // Clips that demonstrably came from the base file belong to this exact rig.
        // An unknown origin is treated as merged: unset paths must not be read as a
        // match against an unset sourcePath.
        if (!includeSourceClips && !model.sourcePath.empty() && anim.sourceFile == model.sourcePath) {
            continue;
        }

        for (NodeTrack& track : anim.tracks) {
            int nodeIndex = track.nodeIndex;
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()) ||
                model.nodes[static_cast<size_t>(nodeIndex)].name != track.nodeName) {
                nodeIndex = model.FindNode(track.nodeName);
            }
            if (nodeIndex < 0) continue;
            track.nodeIndex = nodeIndex;

            const bool trackIsRootBone = isRootBone[static_cast<size_t>(nodeIndex)];
            ApplyPolicyToTrack(track, IsBoneNode(model, nodeIndex), trackIsRootBone, options,
                               report);

            // The originating rig is long gone here, so the hip-height ratio cannot be
            // recovered. Re-anchoring the first key onto the rest pose still removes the
            // constant offset that leaves a character floating; the displacement itself
            // is left at the clip's own magnitude.
            if (options.retargetRootMotion && trackIsRootBone && !track.positions.empty()) {
                const glm::vec3 targetRest = model.nodes[static_cast<size_t>(nodeIndex)].translation;
                const glm::vec3 offset = targetRest - track.positions.front().value;
                if (glm::length(offset) > 1.0e-4f) {
                    for (auto& key : track.positions) key.value += offset;
                    ++report.rootTracksRetargeted;
                }
            }
        }

        const size_t before = anim.tracks.size();
        NormalizeAnimation(anim);
        report.tracksDropped += static_cast<int>(before - anim.tracks.size());
        ++report.animationsAdded;
    }

    return report;
}

}  // namespace fam
