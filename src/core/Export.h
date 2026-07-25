#pragma once

#include <string>
#include <vector>

#include "core/Model.h"

namespace fam {

enum class ExportFormat {
    FbxBinary,
    FbxAscii,
    Glb,
    GltfSeparate,
};

struct ExportOptions {
    ExportFormat format = ExportFormat::FbxBinary;

    // The internal scene is in metres. FBX files conventionally store centimetres
    // (UnitScaleFactor = 1 means 1 unit = 1 cm), glTF mandates metres.
    float scale = 1.0f;

    bool includeGeometry = true;
    bool embedTextures = false;

    // Indices into Model::animations; empty exports every clip.
    std::vector<int> animations;
};

float DefaultScaleFor(ExportFormat format);
const char* AssimpFormatId(ExportFormat format);
const char* DefaultExtension(ExportFormat format);

struct ExportResult {
    bool ok = false;
    std::string error;
    size_t meshCount = 0;
    size_t animationCount = 0;
};

ExportResult ExportModel(const Model& model, const std::string& path, const ExportOptions& options);

}  // namespace fam
