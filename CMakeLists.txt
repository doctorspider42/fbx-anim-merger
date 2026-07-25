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
    add_test(NAME pipeline_roundtrip COMMAND fam_selftest)
endif()
