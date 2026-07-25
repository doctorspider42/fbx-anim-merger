# assimp 5.4.3 FBXExporter::to_ktime truncates keyframe times to whole seconds:
#
#     return (static_cast<int64_t>(ticks / anim->mTicksPerSecond)) * FBX::SECOND;
#
# The int64 cast happens *before* the multiplication, so every key of a 30 fps clip
# collapses onto second boundaries and the exported animation is unusable (assimp's
# own importer reads back zero animations). Multiply first, then truncate.
#
# Written as a CMake script rather than a .patch so it is idempotent and immune to
# whitespace drift in the upstream file.

if(NOT DEFINED ASSIMP_SOURCE_DIR)
    message(FATAL_ERROR "ASSIMP_SOURCE_DIR must be defined")
endif()

set(_file "${ASSIMP_SOURCE_DIR}/code/AssetLib/FBX/FBXExporter.cpp")
if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Not found: ${_file}")
endif()

file(READ "${_file}" _contents)

set(_bug "(static_cast<int64_t>(ticks / anim->mTicksPerSecond)) * FBX::SECOND")
set(_fix "static_cast<int64_t>((ticks / anim->mTicksPerSecond) * static_cast<double>(FBX::SECOND))")

if(_contents MATCHES "static_cast<double>\\(FBX::SECOND\\)")
    message(STATUS "assimp FBX key-time fix: already applied")
    return()
endif()

string(FIND "${_contents}" "${_bug}" _position)
if(_position EQUAL -1)
    message(WARNING "assimp FBX key-time fix: pattern not found - upstream may have fixed or changed it")
    return()
endif()

string(REPLACE "${_bug}" "${_fix}" _contents "${_contents}")
file(WRITE "${_file}" "${_contents}")
message(STATUS "assimp FBX key-time fix: applied")
