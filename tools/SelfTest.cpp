// Headless end-to-end check of the core pipeline:
//
//   synthetic rig -> FBX -> ufbx import -> merge -> FBX + GLB -> assimp re-import
//
// It never touches OpenGL, so it runs on a build machine with no GPU.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
            // V deliberately spans 0.1..0.5 rather than 0..1: an asymmetric range is
            // what makes the glTF V-flip observable, and these particular values
            // cannot collide with anything else written into the file.
            v.uv = glm::vec2(static_cast<float>(corner) / 4.0f, 0.1f + y * 0.2f);
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

// Scans a file for the little-endian byte pattern of a float. glTF buffers store
// raw floats, so this observes what actually landed in the file - unlike a
// re-import, where a flip on the way out and a flip on the way in cancel out and
// hide the very bug we are guarding against.
bool FileContainsFloat(const fs::path& path, float value) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),
                                           std::istreambuf_iterator<char>());
    unsigned char needle[sizeof(float)];
    std::memcpy(needle, &value, sizeof(float));
    return std::search(bytes.begin(), bytes.end(), needle, needle + sizeof(float)) != bytes.end();
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
    unsigned textures = 0;
    unsigned textureBytes = 0;
    float minV = 0.0f;
    float maxV = 0.0f;
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

    out.textures = scene->mNumTextures;
    for (unsigned i = 0; i < scene->mNumTextures; ++i) {
        // mHeight == 0 means the payload is a compressed file and mWidth its length.
        if (scene->mTextures[i]->mHeight == 0) out.textureBytes += scene->mTextures[i]->mWidth;
    }

    out.minV = 1.0e9f;
    out.maxV = -1.0e9f;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh->HasTextureCoords(0)) continue;
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            out.minV = std::min(out.minV, mesh->mTextureCoords[0][v].y);
            out.maxV = std::max(out.maxV, mesh->mTextureCoords[0][v].y);
        }
    }
    return out;
}

}  // namespace

// An explicit working directory lets the CLI tests run against the files this pass
// produces instead of guessing where the temp directory landed.
int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "fam_selftest";
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

    // ------------------------------ 7. embedded textures and glTF UV orientation
    // Packaged characters (Mixamo and friends) carry their skins inside the FBX and
    // leave behind paths from the machine that built them, so an export that only
    // knows about files on disk silently drops every texture.
    std::printf("\n7. Embedded textures and UV orientation\n");
    {
        Model textured = MakeRig("Idle", 20.0f);
        // No clips: keeps animation key times out of the file so the raw float scan
        // below can only be seeing UV data.
        textured.animations.clear();

        std::vector<uint8_t> raw = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        raw.resize(264, 0x5A);
        const size_t textureBytes = raw.size();

        TextureSource embedded;
        embedded.name = "test_diffuse.png";
        embedded.content = std::make_shared<const std::vector<uint8_t>>(std::move(raw));
        textured.materials[0].baseColorTexture = embedded;

        const fs::path texturedGlb = dir / "textured.glb";
        const fs::path texturedFbx = dir / "textured.fbx";

        ExportOptions glbOpts;
        glbOpts.format = ExportFormat::Glb;
        glbOpts.scale = DefaultScaleFor(ExportFormat::Glb);
        // Left off on purpose: an image that exists only inside the source file has
        // to be embedded regardless, otherwise the reference dangles.
        glbOpts.embedTextures = false;
        Check(ExportModel(textured, texturedGlb.string(), glbOpts).ok, "textured.glb written");

        ExportOptions fbxOpts;
        fbxOpts.format = ExportFormat::FbxBinary;
        fbxOpts.scale = DefaultScaleFor(ExportFormat::FbxBinary);
        Check(ExportModel(textured, texturedFbx.string(), fbxOpts).ok, "textured.fbx written");

        const ReadBack glbTex = ReadWithAssimp(texturedGlb);
        Check(glbTex.textures == 1,
              "glb carries " + std::to_string(glbTex.textures) + " embedded texture(s), expected 1");
        Check(glbTex.textureBytes == textureBytes,
              "glb texture payload intact (" + std::to_string(glbTex.textureBytes) + " of " +
                  std::to_string(textureBytes) + " bytes)");

        const ReadBack fbxTex = ReadWithAssimp(texturedFbx);
        Check(fbxTex.textures == 1 && fbxTex.textureBytes == textureBytes,
              "fbx carries the embedded texture intact");

        // Source V spans 0.1..0.5 in FBX convention. assimp's FBX writer stores it
        // untouched, so a re-import must hand back the same range.
        Check(std::fabs(fbxTex.minV - 0.1f) < 1.0e-3f && std::fabs(fbxTex.maxV - 0.5f) < 1.0e-3f,
              "fbx keeps V as authored (" + std::to_string(fbxTex.minV) + ".." +
                  std::to_string(fbxTex.maxV) + ")");

        // glTF's origin is the opposite corner, and assimp's glTF2 writer performs
        // that flip on its own. Checking the bytes on disk rather than a re-import
        // is the point: the matching flip in assimp's glTF2 *reader* would mask a
        // double flip, which is exactly how upside-down textures shipped once.
        // Verified to fail if the flip is applied twice: the buffer then holds
        // 1-(1-0.1), which is not 0.9. (A companion "0.1 must be absent" assertion
        // was dropped - float rounding makes it pass under the bug as well, so it
        // claimed coverage it did not have.)
        Check(FileContainsFloat(texturedGlb, 1.0f - 0.1f),
              "glb stores V flipped to glTF orientation (0.1 -> 0.9 present in the buffer)");
    }

    // ------------------------------- 8. clip names that collide with channel ids
    // assimp's glTF2 writer ids each channel "<clipName>_<index>", so a clip named
    // "clip_1" collides with channel 1 of clip "clip" and the whole export throws.
    // Mixamo names every take "mixamo.com", so disambiguating duplicates as name_1
    // walks straight into it on the second clip.
    std::printf("\n8. Clip names colliding with animation channel ids\n");
    {
        Model colliding = MakeRig("clip", 20.0f, 1.0f, 1.0f);  // hip height => 2 tracks
        Check(colliding.animations[0].tracks.size() >= 2,
              "base clip has " + std::to_string(colliding.animations[0].tracks.size()) +
                  " channels (need >= 2 for the collision)");

        Animation second = colliding.animations[0];
        second.name = "clip_1";  // == channel 1 of "clip"
        colliding.animations.push_back(second);

        const fs::path collidingGlb = dir / "colliding.glb";
        ExportOptions options;
        options.format = ExportFormat::Glb;
        options.scale = DefaultScaleFor(ExportFormat::Glb);

        const ExportResult result = ExportModel(colliding, collidingGlb.string(), options);
        if (!result.ok) std::printf("    export error: %s\n", result.error.c_str());
        Check(result.ok, "glb export survives 'clip' + 'clip_1'");

        const ReadBack back = ReadWithAssimp(collidingGlb);
        Check(back.ok && back.animations == 2,
              "both clips present in the glb (got " + std::to_string(back.animations) + ")");
    }

    // --------------------------------- 9. bind pose that is not the rest pose
    // A rig exported from a DCC while the armature was posed stores a rest pose that
    // is not the bind pose; the skin cluster matrices are what say where the skeleton
    // stood when the mesh was bound. assimp's FBX writer used to ignore them and
    // rebuild the bind pose from the rest pose, which shreds exactly these models
    // (the glTF writer never did, hence "the GLB is fine").
    std::printf("\n9. Bind pose is preserved when it differs from the rest pose\n");
    {
        auto bindOf = [](const Model& model, const std::string& bone) {
            const auto it = model.skeleton.boneByName.find(bone);
            return it == model.skeleton.boneByName.end()
                       ? glm::mat4(0.0f)
                       : model.skeleton.bones[static_cast<size_t>(it->second)].inverseBind;
        };
        auto maxDifference = [](const glm::mat4& a, const glm::mat4& b) {
            float worst = 0.0f;
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) worst = std::max(worst, std::fabs(a[c][r] - b[c][r]));
            }
            return worst;
        };

        Model posed = MakeRig("Idle", 20.0f);
        posed.animations.clear();
        // Move the rest pose away from the bind pose the inverse-bind matrices record.
        posed.nodes[1].rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        posed.nodes[2].translation += glm::vec3(0.0f, 0.4f, 0.0f);

        const fs::path posedFbx = dir / "posed.fbx";
        Check(WriteFbx(posed, posedFbx), "posed.fbx written");

        Model posedBack;
        ImportOptions posedOptions = importOptions;
        Check(ImportFbx(posedFbx.string(), posedOptions, posedBack).ok, "posed.fbx imported");

        for (const std::string& bone : {std::string("Bone_A"), std::string("Bone_B")}) {
            const float delta = maxDifference(bindOf(posed, bone), bindOf(posedBack, bone));
            Check(delta < 2.0e-3f,
                  bone + " keeps its bind matrix through FBX (max element delta " +
                      std::to_string(delta) + ")");
        }

        // And the rest pose is still the posed one - the fix must not quietly move the
        // skeleton into the bind pose instead.
        const int restIndex = posedBack.FindNode("Bone_B");
        Check(restIndex >= 0 &&
                  std::fabs(posedBack.nodes[static_cast<size_t>(restIndex)].translation.y - 1.4f) <
                      2.0e-3f,
              "the authored rest pose survives alongside it");
    }

    // ------------------------------------------- 10. scale tracks reach the file
    // assimp's FBX writer linked scaling curves to a property named "Lcl Scale"; the
    // property is "Lcl Scaling", so the data was written and then ignored by every
    // reader.
    std::printf("\n10. Animated scale survives the FBX round trip\n");
    {
        Model scaled = MakeRig("Grow", 0.0f);
        for (NodeTrack& track : scaled.animations[0].tracks) {
            for (auto& key : track.scales) key.value = glm::vec3(2.0f);
        }

        const fs::path scaledFbx = dir / "scaled.fbx";
        Check(WriteFbx(scaled, scaledFbx), "scaled.fbx written");

        Model scaledBack;
        Check(ImportFbx(scaledFbx.string(), importOptions, scaledBack).ok, "scaled.fbx imported");

        const NodeTrack* track =
            scaledBack.animations.empty() ? nullptr : FindTrack(scaledBack.animations[0], "Bone_B");
        Check(track != nullptr && !track->scales.empty(), "the scale track came back");
        if (track != nullptr && !track->scales.empty()) {
            const float value = track->scales.front().value.x;
            Check(std::fabs(value - 2.0f) < 1.0e-3f,
                  "scale key preserved (got " + std::to_string(value) + ", expected 2)");
        }
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL CHECKS PASSED" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
