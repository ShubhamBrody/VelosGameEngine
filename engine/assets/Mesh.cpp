#include "assets/Mesh.h"
#include "platform/Files.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace velos {
using namespace DirectX;

void MeshData::updateBounds() {
    if (vertices.empty()) { throw std::runtime_error("Mesh contains no vertices."); }
    BoundingBox::CreateFromPoints(bounds, vertices.size(), &vertices.front().position, sizeof(Vertex));
}

void optimizeMesh(MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0) { throw std::runtime_error("Cannot optimize invalid mesh geometry."); }
    for (const auto index : mesh.indices) {
        if (index >= mesh.vertices.size()) { throw std::runtime_error("Mesh index exceeds its vertex buffer."); }
    }
    mesh.lods.clear();
    meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(), mesh.vertices.size());
    const auto vertexCount = meshopt_optimizeVertexFetch(mesh.vertices.data(), mesh.indices.data(), mesh.indices.size(),
        mesh.vertices.data(), mesh.vertices.size(), sizeof(Vertex));
    mesh.vertices.resize(vertexCount);
    if (mesh.indices.size() >= 384) {
        auto previous = mesh.indices;
        for (int level = 0; level < 2; ++level) {
            const auto target = std::max<std::size_t>(12, (previous.size() / 2) / 3 * 3);
            std::vector<std::uint32_t> simplified(previous.size());
            const auto count = meshopt_simplify(simplified.data(), previous.data(), previous.size(), &mesh.vertices[0].position.x,
                mesh.vertices.size(), sizeof(Vertex), target, 0.02f, 0, nullptr);
            if (count == 0 || count >= previous.size()) { break; }
            simplified.resize(count);
            meshopt_optimizeVertexCache(simplified.data(), simplified.data(), simplified.size(), mesh.vertices.size());
            mesh.lods.push_back(simplified);
            previous = std::move(simplified);
        }
    }
    mesh.updateBounds();
}

MeshData makeCube() {
    MeshData mesh;
    constexpr std::array<XMFLOAT3, 6> normals{{{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}}};
    constexpr std::array<XMFLOAT3, 24> positions{{
        {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f},
        {0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{0.5f,0.5f,-0.5f},
        {0.5f,-0.5f,0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,0.5f,0.5f},
        {-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f},{-0.5f,0.5f,-0.5f},
        {-0.5f,0.5f,0.5f},{0.5f,0.5f,0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,-0.5f,0.5f},{-0.5f,-0.5f,0.5f}
    }};
    constexpr std::array<XMFLOAT2, 4> uvs{{{0,1},{1,1},{1,0},{0,0}}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t corner = 0; corner < 4; ++corner) {
            mesh.vertices.push_back({positions[face * 4 + corner], normals[face], uvs[corner]});
        }
        const auto base = face * 4;
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    mesh.updateBounds();
    return mesh;
}

MeshData makeSphere() {
    MeshData mesh;
    constexpr std::uint32_t sectors = 40;
    constexpr std::uint32_t stacks = 24;
    for (std::uint32_t row = 0; row <= stacks; ++row) {
        const float vertical = static_cast<float>(row) / stacks;
        const float theta = vertical * XM_PI;
        for (std::uint32_t column = 0; column <= sectors; ++column) {
            const float horizontal = static_cast<float>(column) / sectors;
            const float phi = horizontal * XM_2PI;
            const XMFLOAT3 normal{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            mesh.vertices.push_back({{normal.x * 0.5f, normal.y * 0.5f, normal.z * 0.5f}, normal, {horizontal, vertical}});
        }
    }
    for (std::uint32_t row = 0; row < stacks; ++row) {
        for (std::uint32_t column = 0; column < sectors; ++column) {
            const auto first = row * (sectors + 1) + column;
            const auto second = first + sectors + 1;
            mesh.indices.insert(mesh.indices.end(), {first, second, first + 1, second, second + 1, first + 1});
        }
    }
    mesh.updateBounds();
    return mesh;
}

MeshData makePlane(bool vertical) {
    MeshData mesh;
    if (vertical) {
        mesh.vertices = {{{-0.5f,-0.5f,0},{0,0,1},{0,1}},{{0.5f,-0.5f,0},{0,0,1},{1,1}},
            {{0.5f,0.5f,0},{0,0,1},{1,0}},{{-0.5f,0.5f,0},{0,0,1},{0,0}}};
    } else {
        mesh.vertices = {{{-0.5f,0,-0.5f},{0,1,0},{0,0}},{{0.5f,0,-0.5f},{0,1,0},{1,0}},
            {{0.5f,0,0.5f},{0,1,0},{1,1}},{{-0.5f,0,0.5f},{0,1,0},{0,1}}};
    }
    mesh.indices = {0,1,2,0,2,3};
    mesh.updateBounds();
    return mesh;
}

MeshData decodeGlb(std::span<const std::byte> bytes) {
    if (bytes.size() < 12 || bytes.size() > 64 * 1024 * 1024) { throw std::runtime_error("GLB files must be between 12 bytes and 64 MB."); }
    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    if (magic != 0x46546c67) { throw std::runtime_error("Only self-contained GLB 2.0 files are supported in this preview."); }
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &raw) != cgltf_result_success) {
        throw std::runtime_error("Cannot parse the GLB model.");
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    for (cgltf_size index = 0; index < data->buffers_count; ++index) {
        if (data->buffers[index].uri) { throw std::runtime_error("External GLB buffers are not permitted; embed geometry before importing."); }
    }
    if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success || cgltf_validate(data.get()) != cgltf_result_success) {
        throw std::runtime_error("GLB buffers or accessors are invalid.");
    }
    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        const cgltf_node* ancestor = &data->nodes[nodeIndex];
        std::size_t depth = 0;
        while (ancestor) {
            if (++depth > 64) { throw std::runtime_error("GLB hierarchy is cyclic or exceeds 64 levels."); }
            ancestor = ancestor->parent;
        }
    }
    MeshData mesh;
    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        const auto& node = data->nodes[nodeIndex];
        if (!node.mesh) { continue; }
        if (node.skin) { throw std::runtime_error("Skinned GLB models need the future animation pipeline; import a static mesh."); }
        std::array<float, 16> transformValues{};
        cgltf_node_transform_world(&node, transformValues.data());
        XMFLOAT4X4 transformStorage;
        std::memcpy(&transformStorage, transformValues.data(), sizeof(transformStorage));
        const auto transform = XMLoadFloat4x4(&transformStorage);
        const auto normalTransform = XMMatrixTranspose(XMMatrixInverse(nullptr, transform));
        for (cgltf_size primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex) {
            const auto& primitive = node.mesh->primitives[primitiveIndex];
            if (primitive.type != cgltf_primitive_type_triangles) { continue; }
            if (primitive.has_draco_mesh_compression) { throw std::runtime_error("Draco-compressed meshes are not supported by this preview."); }
            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* uvs = nullptr;
            for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute) {
                const auto& source = primitive.attributes[attribute];
                if (source.type == cgltf_attribute_type_position) { positions = source.data; }
                if (source.type == cgltf_attribute_type_normal) { normals = source.data; }
                if (source.type == cgltf_attribute_type_texcoord && source.index == 0) { uvs = source.data; }
            }
            if (!positions || positions->count == 0 || positions->count > 1000000
                || mesh.vertices.size() + positions->count > 1000000) { throw std::runtime_error("Invalid or oversized GLB geometry."); }
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (cgltf_size vertexIndex = 0; vertexIndex < positions->count; ++vertexIndex) {
                Vertex vertex{};
                if (!cgltf_accessor_read_float(positions, vertexIndex, &vertex.position.x, 3)) { throw std::runtime_error("Cannot decode GLB positions."); }
                if (normals && !cgltf_accessor_read_float(normals, vertexIndex, &vertex.normal.x, 3)) { throw std::runtime_error("Cannot decode GLB normals."); }
                if (uvs) { static_cast<void>(cgltf_accessor_read_float(uvs, vertexIndex, &vertex.uv.x, 2)); }
                XMStoreFloat3(&vertex.position, XMVector3TransformCoord(XMLoadFloat3(&vertex.position), transform));
                if (normals) { XMStoreFloat3(&vertex.normal, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&vertex.normal), normalTransform))); }
                if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z)
                    || std::abs(vertex.position.x) > 1e6f || std::abs(vertex.position.y) > 1e6f || std::abs(vertex.position.z) > 1e6f) {
                    throw std::runtime_error("GLB vertex position is outside supported bounds.");
                }
                mesh.vertices.push_back(vertex);
            }
            const auto count = primitive.indices ? primitive.indices->count : positions->count;
            if (count % 3 != 0 || count > 3000000 || mesh.indices.size() + count > 3000000) { throw std::runtime_error("Invalid triangle index count."); }
            for (cgltf_size index = 0; index < count; index += 3) {
                std::array<std::uint32_t, 3> triangle{};
                for (cgltf_size corner = 0; corner < 3; ++corner) {
                    const auto sourceIndex = primitive.indices ? cgltf_accessor_read_index(primitive.indices, index + corner) : index + corner;
                    if (sourceIndex >= positions->count) { throw std::runtime_error("GLB index exceeds its vertex buffer."); }
                    triangle[corner] = base + static_cast<std::uint32_t>(sourceIndex);
                    mesh.indices.push_back(triangle[corner]);
                }
                if (!normals) {
                    const auto first = XMLoadFloat3(&mesh.vertices[triangle[0]].position);
                    const auto second = XMLoadFloat3(&mesh.vertices[triangle[1]].position);
                    const auto third = XMLoadFloat3(&mesh.vertices[triangle[2]].position);
                    const auto normal = XMVector3Cross(XMVectorSubtract(second, first), XMVectorSubtract(third, first));
                    for (const auto vertexIndex : triangle) {
                        auto& output = mesh.vertices[vertexIndex].normal;
                        XMStoreFloat3(&output, XMVectorAdd(XMLoadFloat3(&output), normal));
                    }
                }
            }
            if (!normals) {
                for (std::size_t index = base; index < mesh.vertices.size(); ++index) {
                    auto& normal = mesh.vertices[index].normal;
                    const auto source = XMLoadFloat3(&normal);
                    XMStoreFloat3(&normal, XMVectorGetX(XMVector3LengthSq(source)) > 1e-10f ? XMVector3Normalize(source) : XMVectorSet(0,1,0,0));
                }
            }
        }
    }
    if (mesh.indices.empty()) { throw std::runtime_error("GLB contains no supported static triangle mesh."); }
    mesh.updateBounds();
    return mesh;
}

MeshData loadGlb(std::span<const std::byte> source, DiskCache& cache) {
    const auto key = sha256("velos-static-glb-v2-meshopt-c645e49:" + sha256(source));
    struct Header { std::uint32_t version; std::uint32_t vertices; std::uint32_t indices; std::array<std::uint32_t, 2> lods; };
    if (const auto cached = cache.get(key); cached && cached->size() >= sizeof(Header)) {
        Header header{};
        std::memcpy(&header, cached->data(), sizeof(header));
        const std::uint64_t expected = sizeof(Header) + static_cast<std::uint64_t>(header.vertices) * sizeof(Vertex)
            + (static_cast<std::uint64_t>(header.indices) + header.lods[0] + header.lods[1]) * sizeof(std::uint32_t);
        if (header.version == 2 && header.vertices > 0 && header.vertices <= 1000000
            && header.indices > 0 && header.indices <= 3000000 && header.indices % 3 == 0
            && header.lods[0] <= header.indices && header.lods[1] <= header.lods[0]
            && header.lods[0] % 3 == 0 && header.lods[1] % 3 == 0 && expected == cached->size()) {
            MeshData mesh;
            mesh.vertices.resize(header.vertices);
            mesh.indices.resize(header.indices);
            std::memcpy(mesh.vertices.data(), cached->data() + sizeof(Header), mesh.vertices.size() * sizeof(Vertex));
            std::memcpy(mesh.indices.data(), cached->data() + sizeof(Header) + mesh.vertices.size() * sizeof(Vertex), mesh.indices.size() * sizeof(std::uint32_t));
            auto offset = sizeof(Header) + mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(std::uint32_t);
            bool valid = std::all_of(mesh.indices.begin(), mesh.indices.end(), [&](auto index) { return index < mesh.vertices.size(); });
            for (const auto count : header.lods) {
                if (count == 0) { continue; }
                std::vector<std::uint32_t> indices(count);
                std::memcpy(indices.data(), cached->data() + offset, count * sizeof(std::uint32_t));
                offset += count * sizeof(std::uint32_t);
                valid &= std::all_of(indices.begin(), indices.end(), [&](auto index) { return index < mesh.vertices.size(); });
                mesh.lods.push_back(std::move(indices));
            }
            if (valid) {
                mesh.updateBounds();
                return mesh;
            }
        }
    }
    auto mesh = decodeGlb(source);
    optimizeMesh(mesh);
    Header header{2, static_cast<std::uint32_t>(mesh.vertices.size()), static_cast<std::uint32_t>(mesh.indices.size()), {0,0}};
    std::size_t lodBytes = 0;
    for (std::size_t level = 0; level < mesh.lods.size(); ++level) {
        header.lods[level] = static_cast<std::uint32_t>(mesh.lods[level].size());
        lodBytes += mesh.lods[level].size() * sizeof(std::uint32_t);
    }
    std::vector<std::byte> cooked(sizeof(Header) + mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(std::uint32_t) + lodBytes);
    std::memcpy(cooked.data(), &header, sizeof(header));
    std::memcpy(cooked.data() + sizeof(header), mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
    std::memcpy(cooked.data() + sizeof(header) + mesh.vertices.size() * sizeof(Vertex), mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
    auto offset = sizeof(Header) + mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(std::uint32_t);
    for (const auto& level : mesh.lods) {
        std::memcpy(cooked.data() + offset, level.data(), level.size() * sizeof(std::uint32_t));
        offset += level.size() * sizeof(std::uint32_t);
    }
    static_cast<void>(cache.put(key, cooked));
    return mesh;
}

MeshData loadGlb(const std::filesystem::path& path, DiskCache& cache) {
    const auto source = readBytes(path, 64 * 1024 * 1024);
    return loadGlb(source, cache);
}

}