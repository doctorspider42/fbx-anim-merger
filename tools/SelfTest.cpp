// Headless end-to-end check of the core pipeline:
//
//   synthetic rig -> FBX -> ufbx import -> merge -> FBX + GLB -> assimp re-import
//
// It never touches OpenGL, so it runs on a build machine with no GPU.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "core/AnimMerge.h"
#include "core/Export.h"
#include "core/FbxImport.h"
#include "core/Model.h"
#include "core/Pose.h"

namespace fs = std::filesystem;
using namespace fam;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? " ok " : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

// A two-bone rig with a box skinned across the joint. `boneLength` is the offset of
// the second bone and `hipHeight` lifts the root bone off the scene root - together
// they stand in for a character rig's proportions. A non-zero `hipHeight` also gives
// the clip a root-motion track that bobs by 10% of that height.
Model MakeRig(const std::string& clipName, float twistDegrees, float boneLength = 1.0f,
              float hipHeight = 0.0f) {
    Model model;

    Node root;
    root.name = "Root";
    Node boneA;
    boneA.name = "Bone_A";
    boneA.parent = 0;
    boneA.translation = glm::vec3(0.0f, hipHeight, 0.0f);
    Node boneB;
    boneB.name = "Bone_B";
    boneB.parent = 1;
    boneB.translation = glm::vec3(0.0f, boneLength, 0.0f);

    model.nodes = {root, boneA, boneB};
    model.RebuildHierarchy();

    // Bind pose globals: A at hip height, B one bone length above it.
    model.skeleton.bones.push_back(
        {1, glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, hipHeight, 0.0f)))});
    model.skeleton.bones.push_back(
        {2, glm::inverse(glm::translate(glm::mat4(1.0f),
                                        glm::vec3(0.0f, hipHeight + boneLength, 0.0f)))});
    model.skeleton.boneByName = {{"Bone_A", 0}, {"Bone_B", 1}};

    Material material;
    material.name = "TestMaterial";
    material.baseColor = glm::vec3(0.7f, 0.4f, 0.2f);
    model.materials.push_back(material);

    // Box spanning y = 0..2; the lower half rides Bone_A, the upper half Bone_B.
    Mesh mesh;
    mesh.name = "TestMesh";
    mesh.skinned = true;
    mesh.nodeIndex = 0;

    const float halfWidth = 0.25f;
    for (int level = 0; level < 3; ++level) {
        const float y = static_cast<float>(level);
        for (int corner = 0; corner < 4; ++corner) {
            const float x = (corner == 0 || corner == 3) ? -halfWidth : halfWidth;
            const float z = (corner < 2) ? -halfWidth : halfWidth;

            Vertex v;
            v.position = glm::vec3(x, y, z);
            v.normal = glm::normalize(glm::vec3(x, 0.0f, z));
            v.uv = glm::vec2(static_cast<float>(corner) / 4.0f, y * 0.5f);
            const int bone = (level == 0) ? 0 : (level == 2 ? 1 : 0);
            v.boneIndices = glm::ivec4(bone, 0, 0, 0);
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            mesh.vertices.push_back(v);
        }
    }
    for (int level = 0; level < 2; ++level) {
        for (int corner = 0; corner < 4; ++corner) {
            const uint32_t a = static_cast<uint32_t>(level * 4 + corner);
            const uint32_t b = static_cast<uint32_t>(level * 4 + (corner + 1) % 4);
            const uint32_t c = a + 4;
            const uint32_t d = b + 4;
            mesh.indices.insert(mesh.indices.end(), {a, b, d, a, d, c});
        }
    }
    mesh.subMeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), 0});
    mesh.aabbMin = glm::vec3(-halfWidth, 0.0f, -halfWidth);
    mesh.aabbMax = glm::vec3(halfWidth, 2.0f, halfWidth);
    model.totalVertices = mesh.vertices.size();
    model.totalTriangles = mesh.indices.size() / 3;
    model.meshes.push_back(mesh);

    // One clip: Bone_B swings back and forth.
    Animation anim;
    anim.name = clipName;
    anim.sampleRate = 30.0f;
    anim.duration = 1.0f;

    NodeTrack track;
    track.nodeName = "Bone_B";
    track.nodeIndex = 2;
    const int frames = 31;
    for (int f = 0; f < frames; ++f) {
        const float t = static_cast<float>(f) / 30.0f;
        const float angle = glm::radians(twistDegrees) * std::sin(t * 6.2831853f);
        track.positions.push_back({t, glm::vec3(0.0f, boneLength, 0.0f)});
        track.rotations.push_back({t, glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f))});
        track.scales.push_back({t, glm::vec3(1.0f)});
    }
    anim.tracks.push_back(track);

    if (hipHeight > 0.0f) {
        NodeTrack rootTrack;
        rootTrack.nodeName = "Bone_A";
        rootTrack.nodeIndex = 1;
        for (int f = 0; f < frames; ++f) {
            const float t = static_cast<float>(f) / 30.0f;
            const float bob = hipHeight * 0.1f * std::sin(t * 6.2831853f);
            rootTrack.positions.push_back({t, glm::vec3(0.0f, hipHeight + bob, 0.0f)});
            rootTrack.rotations.push_back({t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)});
            rootTrack.scales.push_back({t, glm::vec3(1.0f)});
        }
        anim.tracks.push_back(rootTrack);
    }

    model.animations.push_back(anim);

    return model;
}

const NodeTrack* FindTrack(const Animation& anim, const std::string& nodeName) {
    for (const NodeTrack& track : anim.tracks) {
        if (track.nodeName == nodeName) return &track;
    }
    return nullptr;
}

bool WriteFbx(const Model& model, const fs::path& path) {
    ExportOptions options;
    options.format = ExportFormat::FbxBinary;
    options.scale = DefaultScaleFor(ExportFormat::FbxBinary);
    const ExportResult result = ExportModel(model, path.string(), options);
    if (!result.ok) std::printf("    export error: %s\n", result.error.c_str());
    return result.ok;
}

struct ReadBack {
    bool ok = false;
    unsigned meshes = 0;
    unsigned bones = 0;
    unsigned animations = 0;
    unsigned channels = 0;
};

ReadBack ReadWithAssimp(const fs::path& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path.string(), 0);
    ReadBack out;
    if (!scene) {
        std::printf("    assimp read error: %s\n", importer.GetErrorString());
        return out;
    }
    out.ok = true;
    out.meshes = scene->mNumMeshes;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) out.bones += scene->mMeshes[i]->mNumBones;
    out.animations = scene->mNumAnimations;
    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        out.channels += scene->mAnimations[i]->mNumChannels;
    }
    return out;
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "fam_selftest";
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::printf("Working directory: %s\n\n", dir.string().c_str());

    // ------------------------------------------------- 1. write two FBX files
    std::printf("1. Export synthetic rigs to FBX\n");
    const fs::path basePath = dir / "base.fbx";
    const fs::path clipPath = dir / "clip.fbx";
    Check(WriteFbx(MakeRig("Idle", 20.0f), basePath), "base.fbx written");
    Check(WriteFbx(MakeRig("Twist", 60.0f), clipPath), "clip.fbx written");

    if (g_failures > 0) {
        std::printf("\nAborting: could not produce input files.\n");
        return 1;
    }

    // -------------------------------------------------------- 2. import back
    std::printf("\n2. Import through ufbx\n");
    ImportOptions importOptions;
    importOptions.sampleRate = 30.0f;

    Model base;
    ImportResult baseResult = ImportFbx(basePath.string(), importOptions, base);
    if (!baseResult.ok) std::printf("    import error: %s\n", baseResult.error.c_str());
    Check(baseResult.ok, "base.fbx imported");
    Check(base.meshes.size() == 1, "one mesh (got " + std::to_string(base.meshes.size()) + ")");
    Check(base.skeleton.bones.size() == 2,
          "two bones (got " + std::to_string(base.skeleton.bones.size()) + ")");
    Check(base.animations.size() == 1,
          "one clip (got " + std::to_string(base.animations.size()) + ")");
    Check(base.FindNode("Bone_B") >= 0, "Bone_B present in hierarchy");

    // Round-tripping through FBX centimetres must land back on metres.
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    base.ComputeBounds(boundsMin, boundsMax);
    const float height = boundsMax.y - boundsMin.y;
    Check(std::fabs(height - 2.0f) < 0.05f,
          "model height preserved (" + std::to_string(height) + " m, expected 2)");

    ImportOptions clipOptions = importOptions;
    clipOptions.importGeometry = false;
    Model clip;
    ImportResult clipResult = ImportFbx(clipPath.string(), clipOptions, clip);
    Check(clipResult.ok && !clip.animations.empty(), "clip.fbx imported (animation only)");

    // ------------------------------------------------------------- 3. merge
    std::printf("\n3. Merge clips onto the base rig\n");
    MergeOptions mergeOptions;
    const float compatibility = EstimateCompatibility(base, clip, mergeOptions);
    Check(compatibility > 0.99f,
          "rig compatibility " + std::to_string(static_cast<int>(compatibility * 100.0f)) + "%");

    const MergeReport report = MergeAnimations(base, clip, mergeOptions);
    Check(report.animationsAdded == 1, "one clip merged");
    Check(report.tracksMatched >= 1, "tracks bound: " + std::to_string(report.tracksMatched));
    Check(base.animations.size() == 2,
          "library now holds 2 clips (got " + std::to_string(base.animations.size()) + ")");

    // Namespaced rigs are the common real-world case, so exercise the fallback.
    Model namespaced = clip;
    for (Node& node : namespaced.nodes) node.name = "mixamorig:" + node.name;
    for (Animation& anim : namespaced.animations) {
        anim.name = "Namespaced";
        for (NodeTrack& track : anim.tracks) track.nodeName = "mixamorig:" + track.nodeName;
    }
    namespaced.RebuildHierarchy();
    const MergeReport nsReport = MergeAnimations(base, namespaced, mergeOptions);
    Check(nsReport.animationsAdded == 1 && nsReport.tracksMatched >= 1,
          "namespace-stripped names matched the base rig");

    // ------------------------------------- 3b. proportions across mismatched rigs
    // Regression guard: a clip authored on a shorter rig must not drag the target's
    // bones to the source's lengths. That is what stretches a merged character.
    std::printf("\n3b. Bone proportions survive a cross-rig merge\n");
    {
        constexpr float kTargetBoneLength = 1.6f;
        constexpr float kSourceBoneLength = 1.0f;

        Model tall = MakeRig("Idle", 20.0f, kTargetBoneLength);
        Model shortRig = MakeRig("Twist", 60.0f, kSourceBoneLength);
        shortRig.sourcePath = "clip.fbx";
        for (Animation& anim : shortRig.animations) anim.sourceFile = "clip.fbx";

        MergeOptions keepProportions;  // defaults to TranslationMode::RootBonesOnly
        const MergeReport kept = MergeAnimations(tall, shortRig, keepProportions);
        Check(kept.animationsAdded == 1, "clip merged from the shorter rig");
        Check(kept.translationChannelsStripped >= 1,
              "non-root bone translation stripped (" +
                  std::to_string(kept.translationChannelsStripped) + " channel(s))");

        PoseEvaluator evaluator;
        evaluator.SetModel(&tall);
        evaluator.Evaluate(&tall.animations[1], 0.3f);
        const float boneY = evaluator.Globals()[2][3].y;
        Check(std::fabs(boneY - kTargetBoneLength) < 1.0e-3f,
              "bone stays at the target's length " + std::to_string(kTargetBoneLength) +
                  " m (got " + std::to_string(boneY) + ")");

        // And the escape hatch still does the naive thing when asked.
        Model verbatim = MakeRig("Idle", 20.0f, kTargetBoneLength);
        verbatim.sourcePath = "base.fbx";
        for (Animation& anim : verbatim.animations) anim.sourceFile = "base.fbx";
        MergeOptions copyAll;
        copyAll.translationMode = TranslationMode::CopyAll;
        copyAll.ignoreScaleTracks = false;
        MergeAnimations(verbatim, shortRig, copyAll);

        PoseEvaluator naive;
        naive.SetModel(&verbatim);
        naive.Evaluate(&verbatim.animations[1], 0.3f);
        const float naiveY = naive.Globals()[2][3].y;
        Check(std::fabs(naiveY - kSourceBoneLength) < 1.0e-3f,
              "'copy everything' reproduces the source length " +
                  std::to_string(kSourceBoneLength) + " m (got " + std::to_string(naiveY) + ")");

        // Fixing already-merged clips must give the same result as merging correctly.
        const MergeReport fixed = ApplyTrackPolicy(verbatim, keepProportions);
        Check(fixed.animationsAdded == 1,
              "policy touched only the merged clip, not the base model's own");
        bool nativeKeptTranslation = false;
        for (const NodeTrack& track : verbatim.animations[0].tracks) {
            if (!track.positions.empty()) nativeKeptTranslation = true;
        }
        Check(nativeKeptTranslation, "the base model's own clip was left intact");
        naive.InvalidateBinding();
        naive.Evaluate(&verbatim.animations[1], 0.3f);
        Check(std::fabs(naive.Globals()[2][3].y - kTargetBoneLength) < 1.0e-3f,
              "re-applying the policy restores the target's proportions");
    }

    // ---------------------------------- 3c. root motion lands on the target's floor
    // A clip authored on a 1.0 m rig must not leave a 1.6 m character hovering.
    std::printf("\n3c. Root motion retargeted to the target rig\n");
    {
        constexpr float kTargetHip = 1.6f;
        constexpr float kSourceHip = 1.0f;

        Model tall = MakeRig("Idle", 20.0f, 1.6f, kTargetHip);
        Model shortRig = MakeRig("Walk", 20.0f, 1.0f, kSourceHip);
        shortRig.sourcePath = "walk.fbx";
        for (Animation& anim : shortRig.animations) anim.sourceFile = "walk.fbx";

        MergeOptions retarget;
        const MergeReport report = MergeAnimations(tall, shortRig, retarget);
        Check(report.rootTracksRetargeted == 1, "root track retargeted");
        Check(std::fabs(report.rootMotionScale - kTargetHip / kSourceHip) < 1.0e-3f,
              "hip-height ratio " + std::to_string(report.rootMotionScale) + " (expected 1.6)");

        const NodeTrack* root = FindTrack(tall.animations.back(), "Bone_A");
        Check(root != nullptr && !root->positions.empty(), "merged clip kept its root track");
        if (root != nullptr && !root->positions.empty()) {
            const float start = root->positions.front().value.y;
            Check(std::fabs(start - kTargetHip) < 1.0e-3f,
                  "root starts at the target's hip height " + std::to_string(kTargetHip) +
                      " m (got " + std::to_string(start) + ")");

            float amplitude = 0.0f;
            for (const auto& key : root->positions) {
                amplitude = std::max(amplitude, std::fabs(key.value.y - kTargetHip));
            }
            // 10% bob on a 1.0 m rig, scaled by 1.6.
            Check(std::fabs(amplitude - 0.16f) < 5.0e-3f,
                  "bob scaled with the rig (" + std::to_string(amplitude) + " m, expected 0.16)");
        }

        // Without retargeting the character is left standing at the source's height -
        // exactly the symptom this guards against.
        Model naive = MakeRig("Idle", 20.0f, 1.6f, kTargetHip);
        MergeOptions noRetarget;
        noRetarget.retargetRootMotion = false;
        MergeAnimations(naive, shortRig, noRetarget);
        const NodeTrack* naiveRoot = FindTrack(naive.animations.back(), "Bone_A");
        Check(naiveRoot != nullptr && !naiveRoot->positions.empty() &&
                  std::fabs(naiveRoot->positions.front().value.y - kSourceHip) < 1.0e-3f,
              "retargeting off reproduces the source hip height (the floating bug)");

        // And the retroactive path removes the offset too.
        const MergeReport fixed = ApplyTrackPolicy(naive, retarget);
        const NodeTrack* fixedRoot = FindTrack(naive.animations.back(), "Bone_A");
        Check(fixed.rootTracksRetargeted == 1 && fixedRoot != nullptr &&
                  std::fabs(fixedRoot->positions.front().value.y - kTargetHip) < 1.0e-3f,
              "'apply to loaded clips' re-anchors the root onto the rest pose");
    }

    // ---------------------------------------------------------- 4. sampling
    std::printf("\n4. Pose evaluation\n");
    PoseEvaluator pose;
    pose.SetModel(&base);
    pose.Evaluate(&base.animations[1], 0.25f);
    const std::vector<glm::mat4>& palette = pose.BoneMatrices();
    Check(palette.size() == 2, "skinning palette sized to the skeleton");
    bool paletteMoves = false;
    for (const glm::mat4& m : palette) {
        if (glm::length(glm::vec3(m[3])) > 1.0e-4f || std::fabs(m[0][1]) > 1.0e-4f) {
            paletteMoves = true;
        }
    }
    Check(paletteMoves, "animated clip produces a non-identity palette");

    // ------------------------------------------------------------ 5. export
    std::printf("\n5. Export merged result\n");
    const fs::path outFbx = dir / "merged.fbx";
    const fs::path outGlb = dir / "merged.glb";

    ExportOptions fbxOptions;
    fbxOptions.format = ExportFormat::FbxBinary;
    fbxOptions.scale = DefaultScaleFor(ExportFormat::FbxBinary);
    ExportResult fbxExport = ExportModel(base, outFbx.string(), fbxOptions);
    if (!fbxExport.ok) std::printf("    fbx export error: %s\n", fbxExport.error.c_str());
    Check(fbxExport.ok, "merged.fbx written");

    ExportOptions glbOptions;
    glbOptions.format = ExportFormat::Glb;
    glbOptions.scale = DefaultScaleFor(ExportFormat::Glb);
    ExportResult glbExport = ExportModel(base, outGlb.string(), glbOptions);
    if (!glbExport.ok) std::printf("    glb export error: %s\n", glbExport.error.c_str());
    Check(glbExport.ok, "merged.glb written");

    // ------------------------------------------------- 6. verify with assimp
    std::printf("\n6. Read the exports back with assimp\n");
    const ReadBack fbxBack = ReadWithAssimp(outFbx);
    Check(fbxBack.ok, "merged.fbx parses");
    Check(fbxBack.meshes >= 1, "fbx: " + std::to_string(fbxBack.meshes) + " mesh(es)");
    Check(fbxBack.bones >= 2, "fbx: " + std::to_string(fbxBack.bones) + " bone binding(s)");
    Check(fbxBack.animations == 3,
          "fbx: " + std::to_string(fbxBack.animations) + " animation(s), expected 3");

    const ReadBack glbBack = ReadWithAssimp(outGlb);
    Check(glbBack.ok, "merged.glb parses");
    Check(glbBack.meshes >= 1, "glb: " + std::to_string(glbBack.meshes) + " mesh(es)");
    Check(glbBack.bones >= 2, "glb: " + std::to_string(glbBack.bones) + " bone binding(s)");
    Check(glbBack.animations == 3,
          "glb: " + std::to_string(glbBack.animations) + " animation(s), expected 3");

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL CHECKS PASSED" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
