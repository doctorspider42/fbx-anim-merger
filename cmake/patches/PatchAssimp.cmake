# Fixes applied to the fetched assimp 5.4.3 source tree at configure time.
#
# Written as a CMake script rather than .patch files so they are idempotent and
# immune to whitespace drift in the upstream sources. Each fix is skipped (with a
# warning) if its pattern is gone, so a newer assimp cannot silently break the build.
#
# Both are recorded in THIRD_PARTY_LICENSES.md as required by BSD 3-Clause.

if(NOT DEFINED ASSIMP_SOURCE_DIR)
    message(FATAL_ERROR "ASSIMP_SOURCE_DIR must be defined")
endif()

# fingerprint = a string present only after the fix has been applied
function(fam_patch_file relative_path fingerprint before after label)
    set(_file "${ASSIMP_SOURCE_DIR}/${relative_path}")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Not found: ${_file}")
    endif()

    file(READ "${_file}" _contents)

    string(FIND "${_contents}" "${fingerprint}" _applied)
    if(NOT _applied EQUAL -1)
        message(STATUS "assimp patch [${label}]: already applied")
        return()
    endif()

    string(FIND "${_contents}" "${before}" _position)
    if(_position EQUAL -1)
        message(WARNING "assimp patch [${label}]: pattern not found - upstream may have fixed "
                        "or changed it")
        return()
    endif()

    string(REPLACE "${before}" "${after}" _contents "${_contents}")
    file(WRITE "${_file}" "${_contents}")
    message(STATUS "assimp patch [${label}]: applied")
endfunction()

# ---------------------------------------------------------------------------
# 1. FBX exporter truncates keyframe times to whole seconds.
#
#     return (static_cast<int64_t>(ticks / anim->mTicksPerSecond)) * FBX::SECOND;
#
# The int64 cast happens *before* the multiplication, so every key of a 30 fps clip
# collapses onto second boundaries and the exported animation is unusable (assimp's
# own importer reads back zero animations). Multiply first, then truncate.
# ---------------------------------------------------------------------------
fam_patch_file(
    "code/AssetLib/FBX/FBXExporter.cpp"
    "static_cast<double>(FBX::SECOND)"
    "(static_cast<int64_t>(ticks / anim->mTicksPerSecond)) * FBX::SECOND"
    "static_cast<int64_t>((ticks / anim->mTicksPerSecond) * static_cast<double>(FBX::SECOND))"
    "fbx-key-times")

# ---------------------------------------------------------------------------
# 2. glTF2 exporter registers an animation's glTF id as the raw clip name, while
# each of its channels takes the id "<clipName>_<channelIndex>" through
# FindUniqueID. A clip literally named "X_3" therefore collides with channel 3 of
# clip "X", and LazyDict::Create throws "two objects with the same ID exist".
#
# This is not exotic: Mixamo names every take "mixamo.com", so any tool that
# disambiguates duplicates as name_1, name_2 hits it on the second clip.
#
# Routing the id through FindUniqueID fixes it. The human-readable name is
# unaffected - the very next line assigns `animRef->name = nameAnim;`.
# ---------------------------------------------------------------------------
fam_patch_file(
    "code/AssetLib/glTF2/glTF2Exporter.cpp"
    "animations.Create(mAsset->FindUniqueID(nameAnim"
    "Ref<Animation> animRef = mAsset->animations.Create(nameAnim)"
    "Ref<Animation> animRef = mAsset->animations.Create(mAsset->FindUniqueID(nameAnim, \"animation\"))"
    "gltf-animation-id-collision")

# ---------------------------------------------------------------------------
# 3. FBX exporter throws away aiBone::mOffsetMatrix and rebuilds each skin
# cluster's bind pose out of the node rest pose instead:
#
#     tr = inverse(world transform of the bone node) * world transform of the mesh
#
# That is only the bind pose when the file happens to have been authored with the
# skeleton sitting in it. Plenty of rigs are not - a character exported from
# Blender or Maya while the armature is posed stores a rest pose that differs from
# the bind pose, which is exactly what mOffsetMatrix is for. When they differ,
# every vertex is deformed by the difference and the mesh comes out shredded, with
# the parts furthest from the root worst hit. The glTF2 writer has no such bug: it
# writes mOffsetMatrix straight into inverseBindMatrices, which is why the same
# scene exports correctly to GLB and not to FBX.
#
# The exporter's own comment ("TODO, FIXME: this won't work if anything is not in
# the bind pose") acknowledges it, and the guard that was meant to catch the case
# is dead code - `not_in_bind_pose` is never filled in.
#
# Take the cluster transform from mOffsetMatrix whenever a bone supplied one, and
# derive TransformLink from it so the pair stays consistent:
#   Transform     = geometry -> bone at bind time  (mOffsetMatrix)
#   TransformLink = bone -> world at bind time     (mesh world * Transform^-1)
# Nodes in the skeleton chain that carry no aiBone keep the old derivation.
# ---------------------------------------------------------------------------
fam_patch_file(
    "code/AssetLib/FBX/FBXExporter.cpp"
    "tr = b->mOffsetMatrix"
    "            aiMatrix4x4 tr = inverse_bone_xform * mesh_xform;"
    "            aiMatrix4x4 tr = inverse_bone_xform * mesh_xform;
            if (b) {
                tr = b->mOffsetMatrix;
                bone_xform = tr;
                bone_xform.Inverse();
                bone_xform = mesh_xform * bone_xform;
            }"
    "fbx-bind-pose-from-offset-matrix")

# ---------------------------------------------------------------------------
# 4. FBX exporter connects every scaling curve to a property called "Lcl Scale".
# The property is "Lcl Scaling" - the exporter's own transform_types table spells
# it correctly, and so do the two other uses in the same file. The curve data is
# written out fine, it is just linked to a property that does not exist, so every
# reader silently ignores it and animated scale is lost on export.
# ---------------------------------------------------------------------------
fam_patch_file(
    "code/AssetLib/FBX/FBXExporter.cpp"
    "ids[2], \"S\", S, \"Lcl Scaling\""
    "ids[2], \"S\", S, \"Lcl Scale\""
    "ids[2], \"S\", S, \"Lcl Scaling\""
    "fbx-scale-curve-property-name")
