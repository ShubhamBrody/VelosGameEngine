#pragma once

#include "render/Camera.h"

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace velos {

struct DrawItem {
    std::string mesh;
    DirectX::XMFLOAT4X4 world{};
    DirectX::XMFLOAT4 color{1, 1, 1, 1};
    float roughness = 0.5f;
    float metallic = 0;
    bool selected = false;
    bool castShadow = true;
    bool unlit = false;
    bool grid = false;
};

struct RenderLight {
    DirectX::XMFLOAT4 positionRange{0, 0, 0, 10};
    DirectX::XMFLOAT4 colorIntensity{1, 1, 1, 1};
};

struct RenderFrame {
    Camera camera;
    std::vector<DrawItem> objects;
    std::array<RenderLight, 8> lights{};
    std::uint32_t lightCount = 0;
    DirectX::XMFLOAT3 sunDirection{-0.5f, -1, -0.3f};
    DirectX::XMFLOAT4 sunColor{1, 0.96f, 0.88f, 3};
    float ambient = 0.32f;
    float exposure = 1.0f;
    bool shadows = true;
    bool wireframe = false;
};

struct RendererStats {
    std::string adapter;
    double gpuMilliseconds = 0;
    std::uint64_t gpuUsage = 0;
    std::uint64_t gpuBudget = 0;
    std::uint64_t meshBytes = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t visibleObjects = 0;
    std::uint32_t shadowResolution = 1024;
    bool debugLayer = false;
};

}