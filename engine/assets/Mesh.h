#pragma once

#include "assets/DiskCache.h"

#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace velos {

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};
static_assert(sizeof(Vertex) == 32);

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    DirectX::BoundingBox bounds;
    void updateBounds();
};

MeshData makeCube();
MeshData makeSphere();
MeshData makePlane(bool vertical = false);
MeshData decodeGlb(std::span<const std::byte> bytes);
MeshData loadGlb(const std::filesystem::path& path, DiskCache& cache);

}