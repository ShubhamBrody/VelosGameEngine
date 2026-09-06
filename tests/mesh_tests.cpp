#include "assets/Mesh.h"
#include "render/Camera.h"

#include <iostream>
#include <stdexcept>

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