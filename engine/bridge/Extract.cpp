#include "bridge/Extract.h"

namespace velos {

void extractScene(const Scene& scene, RenderFrame& frame, EntityId selected, bool grid) {
    frame.objects.clear();
    frame.lightCount = 0;
    frame.ambient = scene.ambient;
    frame.shadows = scene.shadows && !frame.camera.orthographic;
    frame.sunDirection = {-0.5f, -1, -0.3f};
    frame.sunColor = {1, 0.96f, 0.88f, 0};
    bool foundSun = false;
    for (const auto id : scene.entities()) {
        if (!scene.get<Identity>(id)->visible) { continue; }
        const auto world = scene.worldMatrix(id);
        if (const auto* mesh = scene.get<MeshRenderer>(id)) {
            DrawItem item;
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
                frame.sunDirection = light->direction;
                frame.sunColor = {light->color.x, light->color.y, light->color.z, light->intensity};
                foundSun = true;
            } else if (light->kind == LightKind::Point && frame.lightCount < frame.lights.size()) {
                auto& output = frame.lights[frame.lightCount++];
                DirectX::XMStoreFloat4(&output.positionRange, world.r[3]);
                output.positionRange.w = light->range;
                output.colorIntensity = {light->color.x, light->color.y, light->color.z, light->intensity};
            }
        }
    }
}

}