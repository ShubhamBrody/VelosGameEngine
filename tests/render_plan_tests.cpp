#include "render/DrawBatches.h"
#include "assets/Mesh.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
}

int main() {
    try {
        std::vector<velos::DrawItem> objects(1000);
        std::vector<velos::VisibleInstance> visible;
        for (std::uint32_t index = 0; index < objects.size(); ++index) {
            objects[index].mesh = "cube";
            objects[index].color.x = static_cast<float>(index) / 1000;
            visible.push_back({index, 0, 1});
        }
        const auto instanced = velos::buildDrawBatches(objects, visible, true);
        require(instanced.batches.size() == 1 && instanced.batches[0].count == 1000, "Compatible meshes with varying per-instance colors must batch.");
        require(velos::buildDrawBatches(objects, visible, false).batches.size() == 1000, "Reference unbatched path must remain available.");
        objects[2].textures[0] = "Assets/other.png";
        require(velos::buildDrawBatches(objects, visible, true).batches.size() == 2, "Different texture bindings must split batches.");
        objects[3].surface = velos::SurfaceMode::Transparent;
        objects[4].surface = velos::SurfaceMode::Transparent;
        visible[3].depth = 4;
        visible[4].depth = 20;
        const auto transparent = velos::buildDrawBatches(objects, visible, true);
        require(transparent.instances[998].object == 4 && transparent.instances[999].object == 3, "Transparency must remain back-to-front.");
        require(velos::selectMeshLod(300, 3) == 0 && velos::selectMeshLod(100, 3) == 1 && velos::selectMeshLod(30, 3) == 2, "Screen-size LOD selection.");
        require(velos::selectMeshLod(1, 1) == 0 && velos::selectMeshLod(1, 0) == 0, "Missing LODs must fall back safely.");
        auto sphere = velos::makeSphere();
        const auto original = sphere.indices.size();
        velos::optimizeMesh(sphere);
        require(sphere.indices.size() == original && !sphere.lods.empty(), "Mesh optimization must preserve base geometry and generate reduced LODs.");
        auto count = sphere.indices.size();
        for (const auto& level : sphere.lods) {
            require(level.size() < count && level.size() % 3 == 0, "Each LOD must reduce a valid triangle list.");
            for (const auto index : level) { require(index < sphere.vertices.size(), "LOD indices must stay within the vertex buffer."); }
            count = level.size();
        }
        std::cout << "PASS: 1000-to-1 draw grouping, texture splits, transparency order, screen-size LODs and optimized geometry.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}