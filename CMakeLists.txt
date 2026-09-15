cmake_minimum_required(VERSION 3.21)
project(gcrender LANGUAGES CXX)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/meshoptimizer EXCLUDE_FROM_ALL)

# GoldClient is 32-bit; this must be a Win32 DLL.
add_library(gcrender SHARED
    src/dllmain.cpp
    src/studio.cpp
    src/studio_fastbones.cpp
    src/studio_fastcache.cpp
    src/studio_fastblend.cpp
    src/studio_fastconcat.cpp
    src/studio_fastlighting.cpp
    src/studio_fastchrome.cpp
    src/studio_fasttexture.cpp
    src/studio_textureopt.cpp
    src/studio_drawbatch.cpp
    src/studio_renderer.cpp
    src/studio_fastskin.cpp
    src/studio_fastlight.cpp
    src/profile.cpp
    src/perf_control.cpp
    src/gl_state.cpp
    src/vis_cache.cpp
    src/world_vbo.cpp
    src/particle_vbo.cpp
    src/beam_vbo.cpp
    src/sprite_vbo.cpp
    src/inline_hook.cpp
    src/log.cpp
    src/gcrender.def)

target_include_directories(gcrender PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hlsdk)

target_link_libraries(gcrender PRIVATE meshoptimizer)

set_target_properties(gcrender PROPERTIES
    OUTPUT_NAME "gcrender"
    SUFFIX ".asi"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

# Static CRT so the .asi has no runtime-DLL dependency next to GoldClient.
set_property(TARGET gcrender PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET meshoptimizer PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_definitions(gcrender PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
