#include "bridge/Extract.h"
#include "rhi/d3d12/RayTracing.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
bool almostEqual(float first, float second) { return std::abs(first - second) < 0.0001f; }
}

int main() {
    try {
        velos::Scene scene;
        const auto parent = scene.create("Light rig");
        auto& rig = *scene.get<velos::Transform>(parent);
        rig.position = {4,3,2};
        DirectX::XMStoreFloat4(&rig.rotation, DirectX::XMQuaternionRotationAxis(DirectX::XMVectorSet(0,1,0,0), DirectX::XM_PIDIV2));
        const auto spot = scene.create("Spot");
        scene.get<velos::Transform>(spot)->parent = parent;
        velos::Light light;
        light.kind = velos::LightKind::Spot;
        light.direction = {1,0,0};
        light.innerAngle = 20;
        light.outerAngle = 60;
        scene.set<velos::Light>(spot, light);
        velos::RenderFrame frame;
        velos::extractScene(scene, frame, 0, true);
        require(frame.lightCount == 1 && frame.lights[0].cone.y == 1, "Spotlights must extract as cone lights.");
        const auto& extracted = frame.lights[0];
        require(almostEqual(extracted.positionRange.x, 4) && almostEqual(extracted.positionRange.y, 3) && almostEqual(extracted.positionRange.z, 2), "Light positions must include parent transforms.");
        require(almostEqual(extracted.directionOuter.x, 0) && almostEqual(extracted.directionOuter.z, -1), "Spot directions must include parent rotation.");
        require(almostEqual(extracted.directionOuter.w, std::cos(DirectX::XM_PI / 6)) && almostEqual(extracted.cone.x, std::cos(DirectX::XM_PI / 18)), "Spot cones use cosines of half angles.");
        const auto revision = frame.sourceRevision;
        frame.camera.set2D(true);
        velos::extractScene(scene, frame, 0, true);
        require(!frame.shadows && frame.sourceRevision == revision, "Camera-only orthographic changes must disable shadows without re-extracting geometry.");
        frame.camera.set2D(false);
        velos::extractScene(scene, frame, 0, true);
        require(frame.shadows, "Returning to perspective must restore the scene shadow setting.");
        for (int index = 0; index < 20; ++index) {
            const auto point = scene.create("Point");
            light.kind = velos::LightKind::Point;
            scene.set<velos::Light>(point, light);
        }
        velos::extractScene(scene, frame, 0, true);
        require(frame.lightCount == 16 && frame.lights[1].cone.y == 0, "Local lights must respect the sixteen-light GPU capacity and retain point-light behavior.");
        scene.get<velos::Identity>(spot)->visible = false;
        scene.touch();
        velos::extractScene(scene, frame, 0, true);
        require(frame.lights[0].cone.y == 0, "Hidden lights must not contribute.");

        DirectX::XMFLOAT4X4 world;
        const auto matrix = DirectX::XMMatrixScaling(2,3,0.5f) * DirectX::XMMatrixRotationRollPitchYaw(0.2f,-0.8f,0.1f) * DirectX::XMMatrixTranslation(7,5,-3);
        DirectX::XMStoreFloat4x4(&world, matrix);
        float rayTransform[3][4]{};
        velos::writeRayTransform(rayTransform, world);
        const float position[4]{1.5f,-0.5f,3,1};
        DirectX::XMFLOAT3 expected;
        DirectX::XMStoreFloat3(&expected, DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(position[0],position[1],position[2],1), matrix));
        float transformed[3]{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 4; ++column) { transformed[row] += rayTransform[row][column] * position[column]; }
        }
        require(almostEqual(transformed[0], expected.x) && almostEqual(transformed[1], expected.y) && almostEqual(transformed[2], expected.z), "DXR transforms must match raster translation, rotation and nonuniform scale.");
        std::cout << "PASS: spotlight transforms, cone angles, light capacity, cached camera policy and DXR matrix layout.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}