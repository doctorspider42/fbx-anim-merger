#pragma once

#include <string>

#include "core/Model.h"

namespace fam {

struct ImportOptions {
    // Curves are baked to a uniform key rate. It costs a little memory but makes
    // merging, playback and export trivially correct regardless of how the source
    // package authored its tangents.
    float sampleRate = 30.0f;

    bool importGeometry = true;
    bool importAnimations = true;

    // Tracks that stay within these tolerances of the rest pose are dropped.
    float epsilonPosition = 1.0e-5f;
    float epsilonRotation = 1.0e-5f;
    float epsilonScale = 1.0e-5f;
};

struct ImportResult {
    bool ok = false;
    std::string error;

    size_t nodeCount = 0;
    size_t meshCount = 0;
    size_t boneCount = 0;
    size_t animationCount = 0;
    float sourceUnitMeters = 1.0f;
};

// Loads an FBX file and normalises it into `out` (Y-up, metres, baked TRS tracks).
ImportResult ImportFbx(const std::string& path, const ImportOptions& options, Model& out);

}  // namespace fam
