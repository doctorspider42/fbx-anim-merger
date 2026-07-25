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
