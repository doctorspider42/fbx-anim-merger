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

}  // namespace fam
