include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- GLFW (zlib)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# ----------------------------------------------------------------- GLM (MIT)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

# ------------------------------------------------------- Dear ImGui (MIT)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9-docking
    GIT_SHALLOW    TRUE)

# ------------------------------------------------------------- ufbx (MIT)
FetchContent_Declare(ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG        v0.23.0
    GIT_SHALLOW    TRUE)

# --------------------------------------------------- stb_image (MIT / PD)
# stb publishes no tags; pinned to the commit this project was verified against.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4)

# --------------------------------------------- nativefiledialog-ext (zlib)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.2.1
    GIT_SHALLOW    TRUE)

# ------------------------------------------------------- Assimp (BSD-3)
# Used purely as an *export* backend (FBX + glTF2/GLB). Import goes through ufbx.
set(ASSIMP_BUILD_TESTS                       OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS                OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES                     OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL                           OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS                OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX              OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB                        ON  CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT                         OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT    OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT    OFF CACHE BOOL "" FORCE)
# The exporter sources live inside the importer "groups", so both must be on.
set(ASSIMP_BUILD_FBX_IMPORTER                ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_EXPORTER                ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER               ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_EXPORTER               ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER                ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_EXPORTER                ON  CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS                        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v5.4.3
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(glfw glm imgui ufbx stb nfd assimp)

# assimp 5.4.3 writes unusable FBX animation key times, loses the bind pose and every
# scale curve on FBX export, and throws on clip names that collide with its own glTF
# channel ids; see the script for details. Applied post-populate so it also repairs an
# already-checked-out source tree.
execute_process(
    COMMAND ${CMAKE_COMMAND} -DASSIMP_SOURCE_DIR=${assimp_SOURCE_DIR}
            -P ${CMAKE_CURRENT_LIST_DIR}/patches/PatchAssimp.cmake
    RESULT_VARIABLE _fam_patch_result
    OUTPUT_VARIABLE _fam_patch_output
    ERROR_VARIABLE  _fam_patch_output)
message(STATUS "${_fam_patch_output}")
if(NOT _fam_patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to patch assimp: ${_fam_patch_output}")
endif()

# ---------------------------------------------------------------------------
# Dear ImGui has no build system of its own.
# ---------------------------------------------------------------------------
add_library(fam_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(fam_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(fam_imgui PUBLIC glfw)
target_compile_definitions(fam_imgui PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    GLFW_INCLUDE_NONE)
add_library(fam::imgui ALIAS fam_imgui)

# ---------------------------------------------------------------------------
# ufbx ships as a single translation unit.
# ---------------------------------------------------------------------------
add_library(fam_ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(fam_ufbx PUBLIC ${ufbx_SOURCE_DIR})
if(NOT MSVC)
    target_compile_options(fam_ufbx PRIVATE -w)
else()
    target_compile_options(fam_ufbx PRIVATE /w)
endif()
add_library(fam::ufbx ALIAS fam_ufbx)

# ---------------------------------------------------------------------------
# stb is header-only; we compile the implementation in Renderer.cpp.
# ---------------------------------------------------------------------------
add_library(fam_stb INTERFACE)
target_include_directories(fam_stb INTERFACE ${stb_SOURCE_DIR})
add_library(fam::stb ALIAS fam_stb)
