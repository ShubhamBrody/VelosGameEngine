#include "assets/Mesh.h"
#include "render/Camera.h"
#include "platform/Files.h"

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

std::vector<std::byte> fixture(float translate) {
    const auto mesh = velos::makeCube();
    const auto vertexBytes = mesh.vertices.size() * sizeof(velos::Vertex);
    const auto indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
    using Json = nlohmann::json;
    Json document{
        {"asset", {{"version", "2.0"}}}, {"scene", 0},
        {"scenes", Json::array({{{"nodes", {0}}}})},
        {"nodes", Json::array({{{"mesh", 0}, {"translation", {translate,0,0}}}})},
        {"meshes", Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION",0},{"NORMAL",1}}}, {"indices",2}}})}}})},
        {"buffers", Json::array({{{"byteLength", vertexBytes + indexBytes}}})},
        {"bufferViews", Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",vertexBytes},{"byteStride",32}},
            {{"buffer",0},{"byteOffset",vertexBytes},{"byteLength",indexBytes}}})},
        {"accessors", Json::array({
            {{"bufferView",0},{"byteOffset",0},{"componentType",5126},{"count",mesh.vertices.size()},{"type","VEC3"},{"min",{-0.5,-0.5,-0.5}},{"max",{0.5,0.5,0.5}}},
            {{"bufferView",0},{"byteOffset",12},{"componentType",5126},{"count",mesh.vertices.size()},{"type","VEC3"}},
            {{"bufferView",1},{"byteOffset",0},{"componentType",5125},{"count",mesh.indices.size()},{"type","SCALAR"}}
        })}
    };
    auto json = document.dump();
    while (json.size() % 4 != 0) { json.push_back(' '); }
    const auto total = 12 + 8 + json.size() + 8 + vertexBytes + indexBytes;
    std::vector<std::byte> result(total);
    const std::uint32_t header[] = {0x46546c67, 2, static_cast<std::uint32_t>(total), static_cast<std::uint32_t>(json.size()), 0x4e4f534a};
    std::memcpy(result.data(), header, sizeof(header));
    std::memcpy(result.data() + sizeof(header), json.data(), json.size());
    const std::uint32_t binaryHeader[] = {static_cast<std::uint32_t>(vertexBytes + indexBytes), 0x004e4942};
    const auto binaryOffset = sizeof(header) + json.size();
    std::memcpy(result.data() + binaryOffset, binaryHeader, sizeof(binaryHeader));
    std::memcpy(result.data() + binaryOffset + 8, mesh.vertices.data(), vertexBytes);
    std::memcpy(result.data() + binaryOffset + 8 + vertexBytes, mesh.indices.data(), indexBytes);
    return result;
}

}

int main() {
    try {
        for (const auto& mesh : {velos::makeCube(), velos::makeSphere(), velos::makePlane(), velos::makePlane(true)}) {
            if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) { throw std::runtime_error("Invalid triangle list."); }
            for (const auto index : mesh.indices) {
                if (index >= mesh.vertices.size()) { throw std::runtime_error("Index out of bounds."); }
            }
            for (const auto& vertex : mesh.vertices) {
                const auto length = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&vertex.normal)));
                if (std::abs(length - 1.0f) > 1e-4f) { throw std::runtime_error("Non-unit primitive normal."); }
            }
        }
        bool rejected = false;
        try { static_cast<void>(velos::decodeGlb({})); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) { throw std::runtime_error("Empty GLB must be rejected."); }
        const auto root = std::filesystem::temp_directory_path() / (L"VelosMeshTest-" + std::to_wstring(GetCurrentProcessId()));
        velos::DiskCache cache(root / L"cache", 1024 * 1024);
        const auto encoded = fixture(2);
        const auto imported = velos::loadGlb(encoded, cache);
        if (imported.vertices.size() != 24 || imported.indices.size() != 36 || std::abs(imported.bounds.Center.x - 2) > 1e-5f) {
            throw std::runtime_error("GLB transform/geometry import failed.");
        }
        static_cast<void>(velos::loadGlb(encoded, cache));
        if (cache.stats().hits != 1) { throw std::runtime_error("Second GLB import must use the cooked cache."); }
        const auto modified = velos::loadGlb(fixture(3), cache);
        if (std::abs(modified.bounds.Center.x - 3) > 1e-5f || cache.stats().misses != 2) { throw std::runtime_error("Changed GLB source must invalidate the cache."); }
        velos::writeAtomic(root / L"fixture.glb", encoded);
        static_cast<void>(velos::loadGlb(root / L"fixture.glb", cache));
        std::filesystem::remove_all(root);
        velos::Camera camera;
        camera.set2D(true);
        camera.zoom(1000);
        if (camera.distance < 0.3f || !camera.orthographic) { throw std::runtime_error("Camera zoom bounds."); }
        camera.set2D(false);
        camera.orbit(0, 10000);
        if (camera.pitch > 1.48f) { throw std::runtime_error("Camera orbit must not flip."); }
        std::cout << "PASS: primitive geometry, normals, bounds, invalid imports and camera constraints.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}