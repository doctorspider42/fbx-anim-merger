cmake_minimum_required(VERSION 3.24)

# Some vendored third-party projects still declare ancient cmake_minimum_required
# values that CMake 4.x refuses outright. This makes them build again.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

project(FbxAnimMerger
    VERSION 0.1.0
    DESCRIPTION "FBX animation merger / retargeting-free animation library tool"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Static CRT under MSVC so the shipped .exe needs no redistributable. Must be set
# before the dependencies are declared so every subproject picks it up.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(cmake/Dependencies.cmake)

# Format-agnostic core: no OpenGL, no windowing. Shared with the self-test.
add_library(fam_core STATIC
    src/util/Log.cpp
    src/core/Model.cpp
    src/core/FbxImport.cpp
    src/core/AnimMerge.cpp
    src/core/Pose.cpp
    src/core/Export.cpp
)
target_include_directories(fam_core PUBLIC src)
target_link_libraries(fam_core PUBLIC fam::ufbx glm::glm assimp::assimp)
if(MSVC)
    target_compile_options(fam_core PRIVATE /W4 /permissive- /utf-8 /bigobj)
else()
    target_compile_options(fam_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(WIN32)
        target_compile_options(fam_core PRIVATE -Wa,-mbig-obj)
    endif()
endif()

# Headless front end over the same core. No OpenGL, no windowing, no file dialogs,
# so it builds and runs anywhere fam_core does.
option(FAM_BUILD_CLI "Build the command-line front end" ON)
if(FAM_BUILD_CLI)
    add_executable(fam_cli
        src/cli/Main.cpp
        src/cli/Cli.cpp
        src/cli/Commands.cpp
    )
    set_target_properties(fam_cli PROPERTIES OUTPUT_NAME fam-cli)
    target_link_libraries(fam_cli PRIVATE fam_core)
    if(MSVC)
        target_compile_options(fam_cli PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(fam_cli PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    if(MINGW)
        target_link_options(fam_cli PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()
endif()

add_executable(FbxAnimMerger
    src/main.cpp

    src/util/FileDialog.cpp

    src/gl/GL.cpp
    src/gl/Shader.cpp

    src/render/Camera.cpp
    src/render/GpuModel.cpp
    src/render/Renderer.cpp

    src/app/Application.cpp
    src/app/Ui.cpp
)

target_include_directories(FbxAnimMerger PRIVATE src)

target_link_libraries(FbxAnimMerger PRIVATE
    fam_core
    fam::imgui
    fam::stb
    glfw
    nfd
)

if(MSVC)
    target_compile_options(FbxAnimMerger PRIVATE /W4 /permissive- /utf-8 /bigobj)
else()
    target_compile_options(FbxAnimMerger PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(WIN32)
        # Heavy template instantiation (assimp/glm) blows past the COFF section limit.
        target_compile_options(FbxAnimMerger PRIVATE -Wa,-mbig-obj)
    endif()
endif()

if(WIN32)
    target_link_libraries(FbxAnimMerger PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(FbxAnimMerger PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
endif()

# MinGW: ship a self-contained binary so the .exe runs outside the dev shell.
if(MINGW)
    target_link_options(FbxAnimMerger PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# ---------------------------------------------------------------------------
# Headless round-trip check: synth rig -> FBX -> import -> merge -> FBX/GLB,
# then read the results back with assimp.
# ---------------------------------------------------------------------------
option(FAM_BUILD_SELFTEST "Build the headless pipeline self-test" ON)
if(FAM_BUILD_SELFTEST)
    add_executable(fam_selftest tools/SelfTest.cpp)
    target_link_libraries(fam_selftest PRIVATE fam_core)
    if(MINGW)
        target_link_options(fam_selftest PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()

    enable_testing()
    set(FAM_TEST_DIR ${CMAKE_BINARY_DIR}/selftest)
    add_test(NAME pipeline_roundtrip COMMAND fam_selftest ${FAM_TEST_DIR})
    # The rigs it writes are the inputs for the CLI tests below.
    set_tests_properties(pipeline_roundtrip PROPERTIES FIXTURES_SETUP fam_test_rigs)

    if(FAM_BUILD_CLI)
        add_test(NAME cli_merge COMMAND fam_cli merge
            --base ${FAM_TEST_DIR}/base.fbx
            --anim ${FAM_TEST_DIR}/clip.fbx
            --name-from-file --min-match 90
            --out ${FAM_TEST_DIR}/cli_merged.glb)
        add_test(NAME cli_info COMMAND fam_cli info ${FAM_TEST_DIR}/merged.fbx --json)
        # A rig match nowhere near the gate must stop the write, not warn and carry on.
        add_test(NAME cli_min_match_gate COMMAND fam_cli merge
            --base ${FAM_TEST_DIR}/base.fbx
            --anim ${FAM_TEST_DIR}/clip.fbx
            --min-match 101
            --out ${FAM_TEST_DIR}/cli_blocked.glb)
        set_tests_properties(cli_min_match_gate PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(cli_merge cli_info cli_min_match_gate
                             PROPERTIES FIXTURES_REQUIRED fam_test_rigs)
    endif()
endif()
