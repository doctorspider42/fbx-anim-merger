# ---------------------------------------------------------------------------
# The version number, derived in exactly one place.
#
# The `VERSION` file at the repository root holds MAJOR.MINOR and is the only
# part a human ever edits. The patch component is the number of commits reachable
# from HEAD, so every push to main yields a version that is both new and strictly
# higher than the one before it - which is precisely what the in-app updater
# compares. Bumping MAJOR.MINOR keeps that ordering intact, because the commit
# count only ever grows.
#
# Overrides, highest priority first:
#   -DFAM_VERSION=1.2.3      full version, used by CI for tag builds
#   -DFAM_BUILD_NUMBER=42    patch only, when the commit count is unavailable
# ---------------------------------------------------------------------------

set(FAM_VERSION "" CACHE STRING "Full version x.y.z; normally derived from VERSION + the commit count")
set(FAM_BUILD_NUMBER "" CACHE STRING "Patch component; defaults to the number of commits on HEAD")
set(FAM_COMMIT "" CACHE STRING "Short commit hash shown in About; defaults to the checked-out one")
set(FAM_REPOSITORY "doctorspider42/fbx-anim-merger" CACHE STRING "GitHub owner/name the updater queries")

# Anything that shells out to git has to tolerate a source tree exported without
# a .git directory (a release tarball, a Docker COPY), hence the fallbacks.
function(_fam_git output)
    set(${output} "" PARENT_SCOPE)
    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        return()
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} ${ARGN}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _out
        ERROR_QUIET
        RESULT_VARIABLE _result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_result EQUAL 0)
        set(${output} "${_out}" PARENT_SCOPE)
    endif()
endfunction()

if(FAM_VERSION STREQUAL "")
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/VERSION _fam_base)
    string(STRIP "${_fam_base}" _fam_base)
    if(NOT _fam_base MATCHES "^[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "VERSION must hold MAJOR.MINOR, got '${_fam_base}'")
    endif()

    set(_fam_patch "${FAM_BUILD_NUMBER}")
    if(_fam_patch STREQUAL "")
        _fam_git(_fam_patch rev-list --count HEAD)
    endif()
    if(NOT _fam_patch MATCHES "^[0-9]+$")
        set(_fam_patch 0)
    endif()

    set(FAM_VERSION "${_fam_base}.${_fam_patch}")
endif()

if(NOT FAM_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "FAM_VERSION must be MAJOR.MINOR.PATCH, got '${FAM_VERSION}'")
endif()
set(FAM_VERSION_MAJOR ${CMAKE_MATCH_1})
set(FAM_VERSION_MINOR ${CMAKE_MATCH_2})
set(FAM_VERSION_PATCH ${CMAKE_MATCH_3})

if(FAM_COMMIT STREQUAL "")
    _fam_git(FAM_COMMIT rev-parse --short=7 HEAD)
endif()
if(FAM_COMMIT STREQUAL "")
    set(FAM_COMMIT "unknown")
endif()

message(STATUS "FbxAnimMerger version ${FAM_VERSION} (${FAM_COMMIT})")

# ---------------------------------------------------------------------------
# Attaches a Windows VERSIONINFO resource to `target` so the shipped .exe reports
# its version in Explorer's property sheet - which is also where the installer
# and any deployment tooling look for it. A no-op everywhere else.
# ---------------------------------------------------------------------------
function(fam_add_version_resource target description filename)
    if(NOT WIN32)
        return()
    endif()
    set(FAM_RC_DESCRIPTION "${description}")
    set(FAM_RC_FILENAME "${filename}")
    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}_version.rc")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.rc.in ${_rc} @ONLY)
    target_sources(${target} PRIVATE ${_rc})
endfunction()
