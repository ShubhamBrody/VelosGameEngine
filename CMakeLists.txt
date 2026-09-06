cmake_minimum_required(VERSION 3.28)
project(Velos VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native Velos preview currently targets Windows x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(cmake/Dependencies.cmake)

add_library(velos_core INTERFACE)
target_include_directories(velos_core INTERFACE "${PROJECT_SOURCE_DIR}/engine")
target_compile_features(velos_core INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(velos_core INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    target_compile_definitions(velos_core INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

add_library(velos_scene STATIC engine/scene/Scene.cpp engine/scene/History.cpp)
target_link_libraries(velos_scene PUBLIC velos_core EnTT::EnTT nlohmann_json::nlohmann_json)

add_library(velos_platform STATIC engine/platform/Files.cpp engine/platform/Window.cpp)
target_link_libraries(velos_platform PUBLIC velos_core bcrypt shell32 ole32)
add_library(velos_cache STATIC engine/assets/DiskCache.cpp)
target_link_libraries(velos_cache PUBLIC velos_platform)

add_library(velos_mesh STATIC engine/assets/Mesh.cpp)
target_include_directories(velos_mesh SYSTEM PRIVATE "${cgltf_SOURCE_DIR}")
target_link_libraries(velos_mesh PUBLIC velos_cache)

add_library(velos_renderer STATIC engine/rhi/d3d12/Renderer.cpp)
target_link_libraries(velos_renderer PUBLIC velos_mesh PRIVATE velos_imgui d3d12 dxgi dxguid windowscodecs)

add_library(velos_bridge STATIC engine/bridge/Extract.cpp)
target_link_libraries(velos_bridge PUBLIC velos_scene)
add_executable(VelosEditor editor/main.cpp editor/Editor.cpp)
target_link_libraries(VelosEditor PRIVATE velos_renderer velos_bridge velos_gizmo)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT VelosEditor)

file(GLOB VELOS_DXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
list(SORT VELOS_DXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
if(NOT VELOS_DXC_CANDIDATES)
    message(FATAL_ERROR "Install the Windows SDK including DXC (dxcompiler.dll).")
endif()
list(GET VELOS_DXC_CANDIDATES 0 VELOS_DXC_DLL)
get_filename_component(VELOS_DXC_DIR "${VELOS_DXC_DLL}" DIRECTORY)
add_custom_command(TARGET VelosEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VELOS_DXC_DLL}" "$<TARGET_FILE_DIR:VelosEditor>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VELOS_DXC_DIR}/dxil.dll" "$<TARGET_FILE_DIR:VelosEditor>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:VelosEditor>/shaders")

include(CTest)
if(BUILD_TESTING)
    add_executable(velos_core_tests tests/core_tests.cpp)
    target_link_libraries(velos_core_tests PRIVATE velos_core)
    add_test(NAME velos.core COMMAND velos_core_tests)
    add_executable(velos_scene_tests tests/scene_tests.cpp)
    target_link_libraries(velos_scene_tests PRIVATE velos_scene)
    add_test(NAME velos.scene COMMAND velos_scene_tests)
    add_executable(velos_cache_tests tests/cache_tests.cpp)
    target_link_libraries(velos_cache_tests PRIVATE velos_cache)
    add_test(NAME velos.cache COMMAND velos_cache_tests)
    add_executable(velos_mesh_tests tests/mesh_tests.cpp)
    target_link_libraries(velos_mesh_tests PRIVATE velos_mesh)
    add_test(NAME velos.mesh COMMAND velos_mesh_tests)
endif()