#pragma once

#include "assets/DiskCache.h"
#include "assets/Material.h"

#include <dxgiformat.h>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace velos {

struct TextureMip {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t rowPitch = 0;
    std::vector<std::byte> pixels;
};

struct TextureData {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<TextureMip> mips;
    [[nodiscard]] std::size_t bytes() const noexcept;
    void validate() const;
};

struct TextureImportSettings {
    TextureSlot slot = TextureSlot::Albedo;
    std::uint32_t maximumDimension = 2048;
    bool compress = true;
};

TextureData decodeCookedTexture(std::span<const std::byte> dds);
std::vector<std::byte> cookTexture(std::span<const std::byte> source, std::string extension, const TextureImportSettings& settings);
TextureData loadTexture(const std::filesystem::path& path, const TextureImportSettings& settings, DiskCache& cache);
TextureData loadTexture(std::span<const std::byte> source, std::string extension, const TextureImportSettings& settings, DiskCache& cache);
TextureData solidTexture(std::array<std::uint8_t, 4> color, bool srgb = false);
std::string textureKey(std::string_view reference, TextureSlot slot);

}