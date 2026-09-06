#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

namespace velos {

enum class TextureSlot : std::uint32_t { Albedo, Normal, Orm, Emissive, Count };
enum class SurfaceMode : std::uint32_t { Opaque, Masked, Transparent };

struct Material {
    DirectX::XMFLOAT4 color{0.24f, 0.73f, 0.64f, 1.0f};
    float roughness = 0.45f;
    float metallic = 0.0f;
    bool unlit = false;
    std::array<std::string, 4> textures;
    DirectX::XMFLOAT2 uvScale{1, 1};
    DirectX::XMFLOAT2 uvOffset{0, 0};
    DirectX::XMFLOAT3 emission{1, 1, 1};
    float emissionStrength = 0;
    float normalStrength = 1;
    float alphaCutoff = 0.5f;
    SurfaceMode surface = SurfaceMode::Opaque;
    bool doubleSided = true;
};

}