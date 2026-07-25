#pragma once

#include <string>
#include <vector>

#include "core/Model.h"

namespace fam {

// What to do with the translation channel of a merged track.
//
// A clip's per-bone translation keys encode the *source* rig's bone lengths. Copying
// them onto a different rig overwrites its rest offsets and visibly stretches the
// mesh, so the default keeps translation only where it carries actual motion.
enum class TranslationMode {
    RootBonesOnly,  // root/hips keeps its motion, every other bone is rotation-only
    AnimatedOnly,   // drop translation channels that never change over the clip
    CopyAll,        // verbatim; only correct when both rigs share proportions
};

struct MergeOptions {
    // "mixamorig:Hips" / "Armature|Hips" -> "Hips"
    bool stripNamespace = true;
    bool caseInsensitive = true;
    TranslationMode translationMode = TranslationMode::RootBonesOnly;
    // Scale is essentially never animated in character clips, while a mismatched
    // bind scale between rigs distorts the mesh just as badly as translation.
    bool ignoreScaleTracks = true;
    // Drop tracks that resolve to a node which is not part of the target skeleton
    // (mesh nodes, helpers, cameras...). Usually the right thing for clip merging.
    bool skeletonTracksOnly = false;
    // Applied to every merged clip name, e.g. "run_" -> "run_Take 001".
    std::string namePrefix;
};

struct MergeReport {
    int animationsAdded = 0;
    int tracksMatched = 0;
    int tracksDropped = 0;
    int translationChannelsStripped = 0;
    int scaleChannelsStripped = 0;
    std::vector<std::string> unmatchedNodes;  // deduplicated, capped
    std::vector<std::string> addedNames;
};

// Percentage [0..1] of the source's animated nodes that exist in the target rig.
float EstimateCompatibility(const Model& target, const Model& source, const MergeOptions& options);

// Copies the selected animations from `source` into `target`, rebinding every
// track to the target node names. `sourceIndices` empty means "all".
MergeReport MergeAnimations(Model& target, const Model& source, const MergeOptions& options,
                            const std::vector<int>& sourceIndices = {});

// Re-applies the translation/scale policy to clips already sitting in the model, so
// a wrong choice can be corrected without re-importing every file. Clips that came
// with the base model itself are left alone - they belong to this exact rig.
MergeReport ApplyTrackPolicy(Model& model, const MergeOptions& options);

}  // namespace fam
