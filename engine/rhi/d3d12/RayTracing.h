#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace velos {

struct RayGeometryInstance {
    std::string mesh;
    ID3D12Resource* vertices = nullptr;
    ID3D12Resource* indices = nullptr;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t vertexStride = 0;
    std::uint64_t indexOffset = 0;
    DirectX::XMFLOAT4X4 world{};
};

inline void writeRayTransform(float (&destination)[3][4], const DirectX::XMFLOAT4X4& world) {
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 4; ++column) { destination[row][column] = world.m[column][row]; }
    }
}

class RayTracingBudgetError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class RayTracingScene {
public:
    explicit RayTracingScene(ID3D12Device* device, std::uint64_t budget = 256 * 1024 * 1024);
    ~RayTracingScene();
    [[nodiscard]] bool supported() const noexcept;
    [[nodiscard]] std::uint64_t bytes() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS record(ID3D12GraphicsCommandList* commands, std::uint32_t frameSlot,
        std::uint64_t revision, std::span<const RayGeometryInstance> instances);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}