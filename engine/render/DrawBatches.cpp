#include "render/DrawBatches.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace velos {

DrawBatches buildDrawBatches(std::span<const DrawItem> objects, std::vector<VisibleInstance> visible, bool instanceDraws) {
    for (const auto& instance : visible) {
        if (instance.object >= objects.size() || !std::isfinite(instance.depth)) { throw std::invalid_argument("Invalid visible draw instance."); }
    }
    const auto key = [&](const VisibleInstance& instance) {
        const auto& object = objects[instance.object];
        return std::tie(object.surface, object.mesh, instance.lod, object.textures, object.doubleSided);
    };
    std::stable_sort(visible.begin(), visible.end(), [&](const VisibleInstance& left, const VisibleInstance& right) {
        const bool leftTransparent = objects[left.object].surface == SurfaceMode::Transparent;
        const bool rightTransparent = objects[right.object].surface == SurfaceMode::Transparent;
        if (leftTransparent != rightTransparent) { return !leftTransparent; }
        if (leftTransparent) { return left.depth > right.depth; }
        return key(left) < key(right);
    });
    DrawBatches result;
    result.instances = std::move(visible);
    for (std::uint32_t index = 0; index < result.instances.size(); ++index) {
        if (instanceDraws && !result.batches.empty() && key(result.instances[index]) == key(result.instances[index - 1])) {
            ++result.batches.back().count;
        } else { result.batches.push_back({index, 1}); }
    }
    return result;
}

std::uint32_t selectMeshLod(float pixelDiameter, std::uint32_t levelCount, float bias) {
    if (levelCount == 0 || !std::isfinite(pixelDiameter) || !std::isfinite(bias) || bias <= 0) { return 0; }
    const float size = pixelDiameter * bias;
    const auto desired = size >= 180 ? 0u : size >= 65 ? 1u : 2u;
    return std::min(desired, levelCount - 1);
}

}