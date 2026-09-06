#pragma once

#include "render/RenderFrame.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>

namespace velos {

class FrameMetrics {
public:
    void add(double cpuMilliseconds, double gpuMilliseconds) {
        if (cpu_.size() >= 10000) { return; }
        cpu_.push_back(cpuMilliseconds);
        gpu_.push_back(gpuMilliseconds);
    }
    [[nodiscard]] nlohmann::json report(const RendererStats& stats, bool instancing, bool lods) const {
        const auto distribution = [](std::vector<double> values) {
            if (values.empty()) { return nlohmann::json{}; }
            std::sort(values.begin(), values.end());
            const auto percentile = [&](double fraction) { return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))]; };
            return nlohmann::json{{"median_ms", percentile(0.5)}, {"p95_ms", percentile(0.95)}, {"p99_ms", percentile(0.99)}};
        };
        return {{"adapter", stats.adapter}, {"samples", cpu_.size()}, {"cpu_frame", distribution(cpu_)}, {"gpu_scene", distribution(gpu_)},
            {"instancing", instancing}, {"lods", lods}, {"camera_draws", stats.cameraDraws}, {"shadow_draws", stats.shadowDraws},
            {"triangles", stats.triangles}, {"visible_objects", stats.visibleObjects}, {"culled_objects", stats.culledObjects},
            {"lod_triangles_saved", stats.lodTrianglesSaved}, {"gpu_memory_bytes", stats.gpuUsage},
            {"dxr_supported", stats.rayTracingSupported}, {"ray_shadows_active", stats.rayTracedShadows},
            {"shadow_path", stats.shadowStatus}, {"dxr_memory_bytes", stats.rayTracingBytes}, {"dxr_budget_bytes", stats.rayTracingBudget}};
    }
private:
    std::vector<double> cpu_;
    std::vector<double> gpu_;
};

}