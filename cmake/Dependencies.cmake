include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(json SYSTEM
    URL https://codeload.github.com/nlohmann/json/tar.gz/65ee68451d8eb2b5f3a30b410476ab83deb3289b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(entt SYSTEM
    URL https://codeload.github.com/skypjack/entt/tar.gz/d4014c74dc3793aba95ae354d6e23a026c2796db
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/7e1b65d26d52e9dd199d889c148c72184de647b4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imguizmo
    URL https://codeload.github.com/CedricGuillemet/ImGuizmo/tar.gz/18cef5e031d8c6973d80284c67f60549fafd78c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _velos_sources_only)
FetchContent_Declare(cgltf
    URL https://codeload.github.com/jkuhlmann/cgltf/tar.gz/bbeb5b0b070ddacddac6852fb72143eb68454937
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(json entt imgui imguizmo cgltf)

if(WIN32)
    add_library(velos_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp")
    target_include_directories(velos_imgui SYSTEM PUBLIC
        "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imgui_SOURCE_DIR}/misc/cpp")
    target_compile_definitions(velos_imgui PRIVATE UNICODE _UNICODE)
    target_link_libraries(velos_imgui PUBLIC d3d12 dxgi d3dcompiler dwmapi)

    if(EXISTS "${imguizmo_SOURCE_DIR}/ImGuizmo.cpp")
        set(VELOS_GIZMO_DIR "${imguizmo_SOURCE_DIR}")
    else()
        set(VELOS_GIZMO_DIR "${imguizmo_SOURCE_DIR}/src")
    endif()
    add_library(velos_gizmo STATIC "${VELOS_GIZMO_DIR}/ImGuizmo.cpp")
    target_include_directories(velos_gizmo SYSTEM PUBLIC "${VELOS_GIZMO_DIR}")
    target_link_libraries(velos_gizmo PUBLIC velos_imgui)
endif()

set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(jolt SYSTEM
    URL https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/0373ec0dd762e4bc2f6acdb08371ee84fa23c6db
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR Build)
FetchContent_MakeAvailable(jolt)