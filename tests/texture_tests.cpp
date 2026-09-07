#include "assets/Texture.h"
#include "platform/Files.h"

#include <Windows.h>
#include <objbase.h>
#include <DirectXTex.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }

std::vector<std::byte> imageFixture(std::size_t width = 16, std::size_t height = 16) {
    DirectX::ScratchImage source;
    require(SUCCEEDED(source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1)), "Fixture allocation.");
    const auto* image = source.GetImage(0, 0, 0);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            auto* pixel = image->pixels + row * image->rowPitch + column * 4;
            const auto intensity = static_cast<std::uint8_t>((row + column) % 2 ? 255 : 0);
            pixel[0] = pixel[1] = pixel[2] = intensity;
            pixel[3] = column < width / 2 ? 0 : 255;
        }
    }
    DirectX::Blob blob;
    require(SUCCEEDED(DirectX::SaveToWICMemory(*image, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), blob)), "Fixture PNG encode.");
    const auto* data = reinterpret_cast<const std::byte*>(blob.GetBufferPointer());
    return {data, data + blob.GetBufferSize()};
}
}

int main() {
    const auto status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto root = std::filesystem::temp_directory_path() / (L"VelosTextureTest-" + std::to_wstring(GetCurrentProcessId()));
    int result = 0;
    try {
        const auto png = imageFixture();
        velos::TextureImportSettings settings;
        settings.compress = false;
        const auto srgb = velos::decodeCookedTexture(velos::cookTexture(png, ".png", settings));
        require(srgb.mips.size() == 5 && srgb.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "Albedo must have full sRGB mip chain.");
        const auto gray = std::to_integer<int>(srgb.mips.back().pixels[0]);
        require(gray > 175 && gray < 200, "sRGB mip averaging must happen in linear light, not average to 128.");
        settings.slot = velos::TextureSlot::Orm;
        const auto linear = velos::decodeCookedTexture(velos::cookTexture(png, ".png", settings));
        require(std::abs(std::to_integer<int>(linear.mips.back().pixels[0]) - 128) <= 2, "Data-map mip averaging must remain linear.");
        settings.slot = velos::TextureSlot::Normal;
        settings.compress = true;
        const auto normal = velos::decodeCookedTexture(velos::cookTexture(png, ".png", settings));
        require(normal.format == DXGI_FORMAT_BC5_UNORM && normal.bytes() < srgb.bytes(), "Normal maps must use linear BC5 storage.");
        settings.slot = velos::TextureSlot::Albedo;
        settings.maximumDimension = 8;
        const auto limited = velos::decodeCookedTexture(velos::cookTexture(png, ".png", settings));
        require(limited.mips.front().width == 8 && limited.format == DXGI_FORMAT_BC3_UNORM_SRGB, "Texture quality cap and alpha-preserving compression.");
        velos::writeAtomic(root / "source.png", png);
        velos::DiskCache cache(root / "cache", 1024 * 1024);
        static_cast<void>(velos::loadTexture(root / "source.png", settings, cache));
        static_cast<void>(velos::loadTexture(root / "source.png", settings, cache));
        require(cache.stats().hits == 1 && cache.stats().misses == 1, "Cooked textures must hit cache on repeat import.");
        settings.maximumDimension = 16;
        static_cast<void>(velos::loadTexture(root / "source.png", settings, cache));
        require(cache.stats().misses == 2, "Changed texture import settings must invalidate cache.");
        settings.maximumDimension = 2048;
        const auto wide = velos::decodeCookedTexture(velos::cookTexture(imageFixture(1120,224),".png",settings));
        require(wide.mips.front().width == 1120 && wide.mips.front().height == 224 && wide.mips.back().width == 1 && wide.mips.back().height == 1
            && wide.format == DXGI_FORMAT_BC3_UNORM_SRGB, "Non-power-of-two labels must retain dimensions and generate a complete compressed mip chain.");
        const auto odd = velos::decodeCookedTexture(velos::cookTexture(imageFixture(31,17),".png",settings));
        require(odd.mips.front().width == 31 && odd.mips.front().height == 17 && odd.mips.back().width == 1 && odd.mips.back().height == 1
            && odd.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "Odd top-level dimensions must use valid uncompressed storage instead of invalid BC resources.");
        bool denied = false;
        try { static_cast<void>(velos::cookTexture({}, ".png", settings)); }
        catch (const std::runtime_error&) { denied = true; }
        require(denied, "Empty texture must be rejected.");
        auto broken = velos::solidTexture({255,255,255,255});
        broken.mips[0].rowPitch = 3;
        denied = false;
        try { broken.validate(); } catch (const std::runtime_error&) { denied = true; }
        require(denied, "Invalid GPU upload pitch must be rejected.");
        std::cout << "PASS: texture decoding, color-space-correct mips, BC formats, dimension budgets, cache invalidation and upload validation.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    if (SUCCEEDED(status)) { CoUninitialize(); }
    return result;
}