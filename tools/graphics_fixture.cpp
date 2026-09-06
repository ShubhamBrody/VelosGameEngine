#include "scene/Scene.h"
#include "assets/Texture.h"
#include "platform/Files.h"

#include <Windows.h>
#include <objbase.h>
#include <DirectXTex.h>
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: velos_graphics_fixture <empty-output-directory>\n"; return 1; }
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int result = 0;
    try {
        const std::filesystem::path directory = velos::wide(argv[1]);
        if (std::filesystem::exists(directory) && !std::filesystem::is_empty(directory)) { throw std::runtime_error("Fixture destination must be empty."); }
        auto scene = velos::Scene::demo();
        scene.name = "Material Laboratory";
        auto& cube = *scene.get<velos::MeshRenderer>(2);
        for (int type = 0; type < 4; ++type) {
            DirectX::ScratchImage image;
            if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 128,128,1,1))) { throw std::runtime_error("Fixture allocation failed."); }
            const auto* pixels = image.GetImage(0,0,0);
            for (std::size_t row = 0; row < 128; ++row) {
                for (std::size_t column = 0; column < 128; ++column) {
                    auto* pixel = pixels->pixels + row * pixels->rowPitch + column * 4;
                    const bool checker = ((row / 16) + (column / 16)) % 2 == 0;
                    if (type == 0) { pixel[0] = pixel[1] = pixel[2] = checker ? 250 : 75; }
                    if (type == 1) {
                        pixel[0] = static_cast<std::uint8_t>(128 + 55 * std::sin(static_cast<double>(column) * 0.2));
                        pixel[1] = static_cast<std::uint8_t>(128 + 55 * std::sin(static_cast<double>(row) * 0.2));
                        pixel[2] = 245;
                    }
                    if (type == 2) { pixel[0] = 255; pixel[1] = checker ? 200 : 70; pixel[2] = 160; }
                    if (type == 3) { pixel[0] = 255; pixel[1] = 180; pixel[2] = 60; }
                    pixel[3] = type == 0 && !checker ? 60 : 255;
                }
            }
            DirectX::Blob png;
            if (FAILED(DirectX::SaveToWICMemory(*pixels, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), png))) { throw std::runtime_error("Cannot encode fixture image."); }
            const auto reference = "Assets/map-" + std::to_string(type) + ".png";
            velos::writeAtomic(directory / velos::wide(reference), std::span(reinterpret_cast<const std::byte*>(png.GetBufferPointer()), png.GetBufferSize()));
            cube.textures[static_cast<std::size_t>(type)] = reference;
        }
        cube.uvScale = {2,2};
        cube.metallic = 0.4f;
        cube.emissionStrength = 0.1f;
        scene.get<velos::MeshRenderer>(1)->textures[0] = cube.textures[0];
        scene.get<velos::MeshRenderer>(1)->uvScale = {8,8};
        scene.get<velos::MeshRenderer>(5)->emissionStrength = 1.5f;
        const auto masked = scene.addPrimitive("quad", "Alpha clipped grid");
        auto& mask = *scene.get<velos::MeshRenderer>(masked);
        mask.textures[0] = cube.textures[0];
        mask.surface = velos::SurfaceMode::Masked;
        mask.unlit = false;
        mask.castShadow = true;
        scene.get<velos::Transform>(masked)->position = {-3.5f,1.3f,-2};
        scene.get<velos::Transform>(masked)->scale = {2,2,2};
        const auto glass = scene.addPrimitive("quad", "Transparent panel");
        auto& transparent = *scene.get<velos::MeshRenderer>(glass);
        transparent.color = {0.5f,0.85f,1,0.35f};
        transparent.surface = velos::SurfaceMode::Transparent;
        scene.get<velos::Transform>(glass)->position = {2.8f,1.4f,1.5f};
        scene.get<velos::Transform>(glass)->scale = {2,2,2};
        velos::writeTextAtomic(directory / "scene.velos", scene.serialize());
        auto lighting = velos::Scene::demo();
        lighting.name = "Directional Ray Shadows";
        lighting.rayTracedShadows = true;
        velos::writeTextAtomic(directory / "ray-shadows.velos", lighting.serialize());
        for (const auto entity : lighting.entities()) { lighting.remove<velos::Light>(entity); }
        lighting.name = "Spotlight Laboratory";
        lighting.ambient = 0.05f;
        lighting.shadows = false;
        const auto spotlight = lighting.create("Overhead spotlight");
        velos::Light spot;
        spot.kind = velos::LightKind::Spot;
        spot.direction = {0,-1,0};
        spot.intensity = 100;
        spot.range = 16;
        spot.innerAngle = 35;
        spot.outerAngle = 70;
        lighting.set<velos::Light>(spotlight, spot);
        lighting.get<velos::Transform>(spotlight)->position = {0,6,0};
        velos::writeTextAtomic(directory / "spotlight.velos", lighting.serialize());
        lighting.get<velos::Light>(spotlight)->kind = velos::LightKind::Point;
        velos::writeTextAtomic(directory / "point-reference.velos", lighting.serialize());
        velos::Scene detail;
        detail.name = "Ray and Raster LOD Consistency";
        detail.rayTracedShadows = true;
        const auto sphere = detail.addPrimitive("sphere", "Isolated convex caster");
        detail.get<velos::Transform>(sphere)->position = {0,1,0};
        detail.get<velos::Transform>(sphere)->scale = {0.5f,0.5f,0.5f};
        detail.get<velos::MeshRenderer>(sphere)->color = {0.85f,0.35f,0.12f,1};
        const auto sun = detail.create("Sun");
        detail.set<velos::Light>(sun, velos::Light{});
        velos::writeTextAtomic(directory / "ray-lod.velos", detail.serialize());
        detail.shadows = false;
        velos::writeTextAtomic(directory / "ray-lod-reference.velos", detail.serialize());
        std::cout << "Generated material fixture: " << velos::utf8(directory.native()) << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (SUCCEEDED(initialized)) { CoUninitialize(); }
    return result;
}