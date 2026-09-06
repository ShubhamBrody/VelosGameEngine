#pragma once

#include "render/RenderFrame.h"

#include <span>
#include <vector>

namespace velos {

struct VisibleInstance {
    std::uint32_t object = 0;
    std::uint32_t lod = 0;
    float depth = 0;
};

struct DrawBatch {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct DrawBatches {
    std::vector<VisibleInstance> instances;
    std::vector<DrawBatch> batches;
};

DrawBatches buildDrawBatches(std::span<const DrawItem> objects, std::vector<VisibleInstance> visible, bool instanceDraws);
std::uint32_t selectMeshLod(float pixelDiameter, std::uint32_t levelCount, float bias = 1.0f);

}