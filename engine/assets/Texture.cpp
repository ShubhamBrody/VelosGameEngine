#include "assets/Texture.h"
#include "platform/Files.h"

#include <Windows.h>
#include <objbase.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace velos {
using namespace DirectX;
namespace {

void checkTexture(HRESULT result, const char* operation) {
    if (FAILED(result)) { throw std::runtime_error(std::string(operation) + " failed (texture format may be invalid or unsupported)."); }
}

class ComScope {
public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE) { throw std::runtime_error("Cannot initialize texture decoding services."); }
    }
    ~ComScope() { if (SUCCEEDED(result_)) { CoUninitialize(); } }
private:
    HRESULT result_;
};

void validateMetadata(const TexMetadata& metadata) {
    if (metadata.dimension != TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 || metadata.IsCubemap()
        || metadata.width == 0 || metadata.height == 0 || metadata.width > 8192 || metadata.height > 8192
        || metadata.width * metadata.height > 16 * 1024 * 1024 || metadata.mipLevels > 14) {
        throw std::runtime_error("Textures must be single 2D images, at most 8192 pixels per axis and 16 million pixels total.");
    }
}

bool isColor(TextureSlot slot) { return slot == TextureSlot::Albedo || slot == TextureSlot::Emissive; }

TextureData unpack(const ScratchImage& source) {
    validateMetadata(source.GetMetadata());
    TextureData output;
    output.format = source.GetMetadata().format;
    for (std::size_t mip = 0; mip < source.GetMetadata().mipLevels; ++mip) {
        const auto* image = source.GetImage(mip, 0, 0);
        if (!image) { throw std::runtime_error("Texture mip data is incomplete."); }
        TextureMip level;
        level.width = static_cast<std::uint32_t>(image->width);
        level.height = static_cast<std::uint32_t>(image->height);
        level.rowPitch = image->rowPitch;
        const auto* first = reinterpret_cast<const std::byte*>(image->pixels);
        level.pixels.assign(first, first + image->slicePitch);
        output.mips.push_back(std::move(level));
    }
    output.validate();
    return output;
}

}

std::size_t TextureData::bytes() const noexcept {
    std::size_t total = 0;
    for (const auto& mip : mips) { total += mip.pixels.size(); }
    return total;
}

void TextureData::validate() const {
    if (mips.empty() || mips.size() > 14 || bytes() > 96 * 1024 * 1024) { throw std::runtime_error("Invalid texture mip chain size."); }
    if (format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        && format != DXGI_FORMAT_BC3_UNORM_SRGB && format != DXGI_FORMAT_BC1_UNORM
        && format != DXGI_FORMAT_BC5_UNORM) { throw std::runtime_error("Unsupported cooked texture format."); }
    auto width = mips.front().width;
    auto height = mips.front().height;
    for (const auto& mip : mips) {
        if (mip.width != width || mip.height != height || width == 0 || height == 0 || width > 8192 || height > 8192) {
            throw std::runtime_error("Texture mip dimensions are inconsistent.");
        }
        std::size_t expectedRow = 0;
        std::size_t expectedSlice = 0;
        checkTexture(ComputePitch(format, width, height, expectedRow, expectedSlice), "Validate texture pitch");
        if (mip.rowPitch != expectedRow || mip.pixels.size() != expectedSlice) { throw std::runtime_error("Texture mip byte layout is invalid."); }
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
}

std::string textureKey(std::string_view reference, TextureSlot slot) {
    return std::string(reference) + "#" + std::to_string(static_cast<std::uint32_t>(slot));
}

TextureData solidTexture(std::array<std::uint8_t, 4> color, bool srgb) {
    TextureData result;
    result.format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    TextureMip mip;
    mip.width = mip.height = 1;
    mip.rowPitch = 4;
    mip.pixels.resize(4);
    std::memcpy(mip.pixels.data(), color.data(), 4);
    result.mips.push_back(std::move(mip));
    return result;
}

TextureData decodeCookedTexture(std::span<const std::byte> dds) {
    if (dds.size() > 96 * 1024 * 1024) { throw std::runtime_error("Cooked texture exceeds the memory limit."); }
    TexMetadata metadata{};
    checkTexture(GetMetadataFromDDSMemory(reinterpret_cast<const std::uint8_t*>(dds.data()), dds.size(), DDS_FLAGS_NONE, metadata), "Read DDS header");
    validateMetadata(metadata);
    ScratchImage image;
    checkTexture(LoadFromDDSMemory(dds.data(), dds.size(), DDS_FLAGS_NONE, nullptr, image), "Decode cooked DDS");
    return unpack(image);
}

std::vector<std::byte> cookTexture(std::span<const std::byte> source, std::string extension, const TextureImportSettings& settings) {
    if (source.empty() || source.size() > 64 * 1024 * 1024 || settings.maximumDimension < 4
        || settings.maximumDimension > 4096 || settings.slot >= TextureSlot::Count) {
        throw std::runtime_error("Invalid texture source size or import settings.");
    }
    const ComScope com;
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(source.data());
    TexMetadata metadata{};
    if (extension == ".dds") {
        checkTexture(GetMetadataFromDDSMemory(bytes, source.size(), DDS_FLAGS_NONE, metadata), "Read DDS metadata");
    } else if (extension == ".tga") {
        checkTexture(GetMetadataFromTGAMemory(bytes, source.size(), TGA_FLAGS_NONE, metadata), "Read TGA metadata");
    } else {
        checkTexture(GetMetadataFromWICMemory(bytes, source.size(), WIC_FLAGS_IGNORE_SRGB, metadata), "Read image metadata");
    }
    validateMetadata(metadata);
    ScratchImage decoded;
    if (extension == ".dds") { checkTexture(LoadFromDDSMemory(bytes, source.size(), DDS_FLAGS_NONE, nullptr, decoded), "Load DDS"); }
    else if (extension == ".tga") { checkTexture(LoadFromTGAMemory(bytes, source.size(), TGA_FLAGS_NONE, nullptr, decoded), "Load TGA"); }
    else { checkTexture(LoadFromWICMemory(bytes, source.size(), WIC_FLAGS_IGNORE_SRGB, nullptr, decoded), "Load image"); }
    ScratchImage rgba;
    const auto* original = decoded.GetImage(0, 0, 0);
    if (IsCompressed(original->format)) {
        checkTexture(Decompress(*original, DXGI_FORMAT_R8G8B8A8_UNORM, rgba), "Decompress source");
    } else if (original->format != DXGI_FORMAT_R8G8B8A8_UNORM && original->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        checkTexture(Convert(*original, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, rgba), "Convert source pixels");
    } else {
        checkTexture(rgba.InitializeFromImage(*original), "Copy source pixels");
    }
    const auto format = isColor(settings.slot) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!rgba.OverrideFormat(format)) { throw std::runtime_error("Cannot assign the texture color space."); }
    const auto filter = static_cast<TEX_FILTER_FLAGS>(TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC);
    auto width = metadata.width;
    auto height = metadata.height;
    while (width > settings.maximumDimension || height > settings.maximumDimension) {
        width = std::max<std::size_t>(1, width / 2);
        height = std::max<std::size_t>(1, height / 2);
    }
    ScratchImage resized;
    if (width != metadata.width || height != metadata.height) {
        checkTexture(Resize(*rgba.GetImage(0,0,0), width, height, filter, resized), "Resize source texture");
        rgba = std::move(resized);
    }
    ScratchImage chain;
    checkTexture(GenerateMipMaps(*rgba.GetImage(0,0,0), filter, 0, chain), "Generate texture mips");
    ScratchImage compressed;
    if (settings.compress && width >= 4 && height >= 4) {
        const auto compressedFormat = isColor(settings.slot) ? DXGI_FORMAT_BC3_UNORM_SRGB
            : settings.slot == TextureSlot::Normal ? DXGI_FORMAT_BC5_UNORM : DXGI_FORMAT_BC1_UNORM;
        checkTexture(Compress(chain.GetImages(), chain.GetImageCount(), chain.GetMetadata(), compressedFormat,
            TEX_COMPRESS_DEFAULT, TEX_THRESHOLD_DEFAULT, compressed), "Compress texture mips");
        chain = std::move(compressed);
    }
    Blob blob;
    checkTexture(SaveToDDSMemory(chain.GetImages(), chain.GetImageCount(), chain.GetMetadata(), DDS_FLAGS_FORCE_DX10_EXT, blob), "Serialize texture");
    const auto* first = reinterpret_cast<const std::byte*>(blob.GetBufferPointer());
    return {first, first + blob.GetBufferSize()};
}

TextureData loadTexture(const std::filesystem::path& path, const TextureImportSettings& settings, DiskCache& cache) {
    const auto source = readBytes(path, 64 * 1024 * 1024);
    const auto key = sha256("velos-texture-v1-dxt-6c235c3:" + sha256(source) + ":" + utf8(path.extension().native())
        + ":" + std::to_string(static_cast<std::uint32_t>(settings.slot)) + ":" + std::to_string(settings.maximumDimension)
        + ":" + (settings.compress ? "bc" : "rgba"));
    if (const auto cached = cache.get(key)) {
        try { return decodeCookedTexture(*cached); } catch (const std::exception&) {}
    }
    const auto cooked = cookTexture(source, utf8(path.extension().native()), settings);
    auto result = decodeCookedTexture(cooked);
    static_cast<void>(cache.put(key, cooked));
    return result;
}

}