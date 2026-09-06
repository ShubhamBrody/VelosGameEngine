#include "rhi/d3d12/RayTracing.h"

#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace velos {
using Microsoft::WRL::ComPtr;
namespace {

void checkRay(HRESULT result, const char* operation) {
    if (FAILED(result)) { throw std::runtime_error(std::string(operation) + " failed."); }
}

void uavBarrier(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    commands->ResourceBarrier(1, &barrier);
}

}

struct RayTracingScene::Impl {
    ComPtr<ID3D12Device5> device;
    struct Buffer { ComPtr<ID3D12Resource> resource; UINT64 capacity = 0; UINT64 allocated = 0; };
    struct Mesh { Buffer structure; ComPtr<ID3D12Resource> vertices; ComPtr<ID3D12Resource> indices; };
    struct Frame {
        Buffer structure;
        Buffer scratch;
        Buffer instances;
        std::vector<Buffer> buildScratch;
        std::vector<std::string> geometryKeys;
        std::uint64_t revision = 0;
    };
    std::map<std::string, Mesh> meshes;
    std::array<Frame, 2> frames;
    UINT64 used = 0;
    UINT64 budget = 256 * 1024 * 1024;

    Buffer allocate(UINT64 size, D3D12_RESOURCE_STATES state, bool upload = false) {
        if (size == 0 || size > budget) { throw RayTracingBudgetError("Ray-tracing resource exceeds its memory budget."); }
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = size;
        description.Height = 1;
        description.DepthOrArraySize = description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = upload ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const auto allocation = device->GetResourceAllocationInfo(0, 1, &description);
        if (allocation.SizeInBytes > budget || used > budget - allocation.SizeInBytes) { throw RayTracingBudgetError("Ray-tracing residency budget exhausted."); }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        Buffer result;
        checkRay(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, state, nullptr, IID_PPV_ARGS(&result.resource)), "Allocate DXR resource");
        result.capacity = size;
        result.allocated = allocation.SizeInBytes;
        used += result.allocated;
        return result;
    }

    void grow(Buffer& buffer, UINT64 size, D3D12_RESOURCE_STATES state, bool upload = false) {
        if (buffer.capacity >= size) { return; }
        used -= buffer.allocated;
        buffer = {};
        buffer = allocate(size, state, upload);
    }

    Buffer scratch(ID3D12GraphicsCommandList* commands, UINT64 size) {
        auto result = allocate(size, D3D12_RESOURCE_STATE_COMMON);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = result.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &barrier);
        return result;
    }
};

RayTracingScene::RayTracingScene(ID3D12Device* source, std::uint64_t budget) : impl_(std::make_unique<Impl>()) {
    if (budget > 256 * 1024 * 1024) { throw std::invalid_argument("DXR memory budget must not exceed 256 MB."); }
    impl_->budget = budget;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_5};
    if (FAILED(source->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))
        || options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1
        || FAILED(source->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) || model.HighestShaderModel < D3D_SHADER_MODEL_6_5) { return; }
    static_cast<void>(source->QueryInterface(IID_PPV_ARGS(&impl_->device)));
}

RayTracingScene::~RayTracingScene() = default;
bool RayTracingScene::supported() const noexcept { return impl_->device != nullptr; }
std::uint64_t RayTracingScene::bytes() const noexcept { return impl_->used; }

D3D12_GPU_VIRTUAL_ADDRESS RayTracingScene::record(ID3D12GraphicsCommandList* source, std::uint32_t frameSlot,
    std::uint64_t revision, std::span<const RayGeometryInstance> instances) {
    if (!supported() || instances.empty()) { return 0; }
    if (frameSlot >= impl_->frames.size() || instances.size() > 10000) { throw std::runtime_error("DXR instance capacity exceeded."); }
    auto& frame = impl_->frames[frameSlot];
    for (const auto& buffer : frame.buildScratch) { impl_->used -= buffer.allocated; }
    frame.buildScratch.clear();
    const bool sameGeometry = frame.geometryKeys.size() == instances.size()
        && std::equal(frame.geometryKeys.begin(), frame.geometryKeys.end(), instances.begin(),
            [](const auto& key, const auto& instance) { return key == instance.mesh; });
    if (frame.revision == revision && sameGeometry && frame.structure.resource) { return frame.structure.resource->GetGPUVirtualAddress(); }
    ComPtr<ID3D12GraphicsCommandList4> commands;
    checkRay(source->QueryInterface(IID_PPV_ARGS(&commands)), "Query DXR command interface");
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descriptors;
    descriptors.reserve(instances.size());
    for (const auto& instance : instances) {
        if (!instance.vertices || !instance.indices || instance.indexCount == 0 || instance.indexCount % 3 != 0 || instance.vertexCount == 0) {
            throw std::runtime_error("Invalid ray-tracing mesh geometry.");
        }
        auto found = impl_->meshes.find(instance.mesh);
        if (found == impl_->meshes.end()) {
            D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
            geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            geometry.Triangles.VertexBuffer = {instance.vertices->GetGPUVirtualAddress(), instance.vertexStride};
            geometry.Triangles.VertexCount = instance.vertexCount;
            geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometry.Triangles.IndexBuffer = instance.indices->GetGPUVirtualAddress() + instance.indexOffset;
            geometry.Triangles.IndexCount = instance.indexCount;
            geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            inputs.NumDescs = 1;
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.pGeometryDescs = &geometry;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
            impl_->device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
            auto temporary = impl_->scratch(commands.Get(), sizes.ScratchDataSizeInBytes);
            frame.buildScratch.push_back(std::move(temporary));
            Impl::Mesh mesh;
            mesh.structure = impl_->allocate(sizes.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            mesh.vertices = instance.vertices;
            mesh.indices = instance.indices;
            found = impl_->meshes.emplace(instance.mesh, std::move(mesh)).first;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
            build.Inputs = inputs;
            build.ScratchAccelerationStructureData = frame.buildScratch.back().resource->GetGPUVirtualAddress();
            build.DestAccelerationStructureData = found->second.structure.resource->GetGPUVirtualAddress();
            commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
            uavBarrier(commands.Get(), found->second.structure.resource.Get());
        }
        D3D12_RAYTRACING_INSTANCE_DESC descriptor{};
        writeRayTransform(descriptor.Transform, instance.world);
        descriptor.InstanceMask = 0xff;
        descriptor.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        descriptor.AccelerationStructure = found->second.structure.resource->GetGPUVirtualAddress();
        descriptors.push_back(descriptor);
    }
    impl_->grow(frame.instances, descriptors.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_RESOURCE_STATE_GENERIC_READ, true);
    void* mapped = nullptr;
    const D3D12_RANGE noReads{0,0};
    checkRay(frame.instances.resource->Map(0, &noReads, &mapped), "Map DXR instances");
    std::memcpy(mapped, descriptors.data(), descriptors.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    frame.instances.resource->Unmap(0, nullptr);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = static_cast<UINT>(descriptors.size());
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = frame.instances.resource->GetGPUVirtualAddress();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
    impl_->device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
    impl_->grow(frame.structure, sizes.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    if (frame.scratch.capacity < sizes.ScratchDataSizeInBytes) {
        impl_->used -= frame.scratch.allocated;
        frame.scratch = {};
        frame.scratch = impl_->scratch(commands.Get(), sizes.ScratchDataSizeInBytes);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.ScratchAccelerationStructureData = frame.scratch.resource->GetGPUVirtualAddress();
    build.DestAccelerationStructureData = frame.structure.resource->GetGPUVirtualAddress();
    commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    uavBarrier(commands.Get(), frame.structure.resource.Get());
    frame.geometryKeys.clear();
    frame.geometryKeys.reserve(instances.size());
    for (const auto& instance : instances) { frame.geometryKeys.push_back(instance.mesh); }
    frame.revision = revision;
    return frame.structure.resource->GetGPUVirtualAddress();
}

}