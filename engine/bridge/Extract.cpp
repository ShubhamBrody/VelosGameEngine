#include "bridge/Extract.h"

namespace velos {

void extractScene(const Scene& scene, RenderFrame& frame, EntityId selected, bool grid) {
    frame.shadows = scene.shadows && !frame.camera.orthographic;
    if (frame.sourceRevision == scene.revision() && frame.selectedSource == selected && frame.sourceGrid == grid) { return; }
    frame.sourceRevision = scene.revision();
    frame.selectedSource = selected;
    frame.sourceGrid = grid;
    frame.objects.clear();
    frame.lightCount = 0;
    frame.ambient = scene.ambient;
    frame.rayTracedShadows = scene.rayTracedShadows;
    frame.sunDirection = {-0.5f, -1, -0.3f};
    frame.sunColor = {1, 0.96f, 0.88f, 0};
    bool foundSun = false;
    for (const auto id : scene.entities()) {
        if (!scene.get<Identity>(id)->visible) { continue; }
        const auto world = scene.worldMatrix(id);
        if (const auto* mesh = scene.get<MeshRenderer>(id)) {
            DrawItem item;
            static_cast<Material&>(item) = static_cast<const Material&>(*mesh);
            item.mesh = mesh->mesh;
            DirectX::XMStoreFloat4x4(&item.world, world);
            item.color = mesh->color;
            item.roughness = mesh->roughness;
            item.metallic = mesh->metallic;
            item.selected = id == selected;
            item.castShadow = mesh->castShadow;
            item.unlit = mesh->unlit;
            item.grid = grid && mesh->mesh == "plane";
            frame.objects.push_back(std::move(item));
        }
        if (const auto* light = scene.get<Light>(id)) {
            if (light->kind == LightKind::Directional && !foundSun) {
                DirectX::XMStoreFloat3(&frame.sunDirection, DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&light->direction), world)));
                frame.sunColor = {light->color.x, light->color.y, light->color.z, light->intensity};
                foundSun = true;
            } else if (light->kind != LightKind::Directional && frame.lightCount < frame.lights.size()) {
                auto& output = frame.lights[frame.lightCount++];
                DirectX::XMStoreFloat4(&output.positionRange, world.r[3]);
                output.positionRange.w = light->range;
                output.colorIntensity = {light->color.x, light->color.y, light->color.z, light->intensity};
                DirectX::XMStoreFloat4(&output.directionOuter, DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&light->direction), world)));
                output.directionOuter.w = std::cos(DirectX::XMConvertToRadians(light->outerAngle * 0.5f));
                output.cone = {std::cos(DirectX::XMConvertToRadians(light->innerAngle * 0.5f)), light->kind == LightKind::Spot ? 1.0f : 0.0f, 0, 0};
            }
        }
    }
}

}