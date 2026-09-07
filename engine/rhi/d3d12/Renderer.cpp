#include "rhi/d3d12/Renderer.h"
#include "rhi/d3d12/RayTracing.h"
#include "platform/Files.h"
#include "render/DrawBatches.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

namespace velos {
using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

constexpr UINT frameCount = 2;
constexpr UINT descriptorCount = 4096;
constexpr UINT maxObjects = 10000;
constexpr UINT objectStride = 256;
constexpr UINT frameDataSize = 2048;
constexpr UINT uploadSize = frameDataSize + maxObjects * 2 * objectStride;

void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ").";
        throw std::runtime_error(message.str());
    }
}

D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC bufferDescription(UINT64 size) {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

struct FrameConstants {
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 lightViewProjection;
    XMFLOAT4 eyeExposure;
    XMFLOAT4 sunDirectionAmbient;
    XMFLOAT4 sunColorIntensity;
    XMFLOAT4 settings;
    std::array<RenderLight, 16> lights;
};
static_assert(sizeof(FrameConstants) <= frameDataSize);

struct ObjectConstants {
    XMFLOAT4X4 world;
    XMFLOAT4X4 normal;
    XMFLOAT4 color;
    XMFLOAT4 material;
    XMFLOAT4 options;
    XMFLOAT4 uvTransform;
    XMFLOAT4 emission;
    std::array<XMFLOAT4, 3> padding{};
};
static_assert(sizeof(ObjectConstants) == objectStride);

void savePng(const std::filesystem::path& path, UINT width, UINT height, UINT stride, BYTE* pixels, UINT bytes) {
    if (width == 0 || height == 0 || static_cast<UINT64>(width) * 4 > stride
        || static_cast<UINT64>(stride) * (height - 1) + static_cast<UINT64>(width) * 4 > bytes) {
        throw std::runtime_error("Incomplete screenshot buffer.");
    }
    std::vector<BYTE> converted(static_cast<std::size_t>(width) * height * 4);
    for (UINT row = 0; row < height; ++row) {
        for (UINT column = 0; column < width; ++column) {
            const auto source = static_cast<std::size_t>(row) * stride + column * 4;
            const auto destination = (static_cast<std::size_t>(row) * width + column) * 4;
            converted[destination] = pixels[source + 2];
            converted[destination + 1] = pixels[source + 1];
            converted[destination + 2] = pixels[source];
            converted[destination + 3] = 255;
        }
    }
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Create WIC factory");
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), "Create WIC stream");
    if (!path.parent_path().empty()) { std::filesystem::create_directories(path.parent_path()); }
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Open screenshot");
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Create PNG encoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&frame, &properties), "Create PNG frame");
    check(frame->Initialize(properties.Get()), "Initialize PNG frame");
    check(frame->SetSize(width, height), "Set screenshot size");
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "Set PNG format");
    if (format != GUID_WICPixelFormat32bppBGRA) { throw std::runtime_error("PNG encoder does not support BGRA."); }
    check(frame->WritePixels(height, width * 4, static_cast<UINT>(converted.size()), converted.data()), "Write screenshot");
    check(frame->Commit(), "Commit screenshot frame");
    check(encoder->Commit(), "Commit screenshot");
}

}

struct Renderer::Impl {
    HWND window;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter3> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12InfoQueue> infoQueue;
    HANDLE fenceEvent = nullptr;
    UINT64 nextFence = 0;
    UINT64 timestampFrequency = 1;
    UINT rtvStride = 0;
    UINT dsvStride = 0;
    UINT srvStride = 0;
    UINT width = 1;
    UINT height = 1;
    UINT sceneWidth = 1280;
    UINT sceneHeight = 720;
    UINT lastBackbuffer = 0;
    UINT shadowSize = 1024;
    bool uiReady = false;
    bool win32Ready = false;
    std::array<bool, descriptorCount> descriptors{};
    std::vector<std::pair<UINT, UINT64>> retiredDescriptors;

    struct FrameSlot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> upload;
        ComPtr<ID3D12Resource> timestamps;
        std::byte* mapped = nullptr;
        UINT64 fenceValue = 0;
        bool timed = false;
    };
    std::array<FrameSlot, frameCount> frames;
    std::array<ComPtr<ID3D12Resource>, frameCount> backbuffers;
    ComPtr<ID3D12Resource> sceneColor;
    ComPtr<ID3D12Resource> sceneDepth;
    ComPtr<ID3D12Resource> shadow;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> litPipeline;
    ComPtr<ID3D12PipelineState> wirePipeline;
    ComPtr<ID3D12PipelineState> shadowPipeline;
    ComPtr<ID3D12PipelineState> backgroundPipeline;
    std::array<ComPtr<ID3D12PipelineState>, 4> surfacePipelines;
    std::array<ComPtr<ID3D12PipelineState>, 4> raySurfacePipelines;
    std::unique_ptr<RayTracingScene> rayScene;
    bool rayBudgetExceeded = false;
    HMODULE dxcModule = nullptr;
    std::string compilerDigest;
    DiskCache shaderCache;
    RendererStats statistics;

    struct GpuMesh {
        ComPtr<ID3D12Resource> vertices;
        ComPtr<ID3D12Resource> indices;
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        D3D12_INDEX_BUFFER_VIEW indexView{};
        BoundingBox bounds;
        UINT indexCount = 0;
        struct Level { D3D12_INDEX_BUFFER_VIEW view; UINT count; };
        std::vector<Level> levels;
    };
    std::map<std::string, GpuMesh> meshes;
    struct GpuTexture { ComPtr<ID3D12Resource> resource; UINT descriptor = 0; };
    std::map<std::string, GpuTexture> textures;
    std::map<std::array<std::string, 4>, UINT> materialTables;

    UINT allocateDescriptors(UINT count) {
        for (UINT first = 8; first + count <= descriptorCount; ++first) {
            bool available = true;
            for (UINT offset = 0; offset < count; ++offset) { if (descriptors[first + offset]) { available = false; break; } }
            if (!available) { continue; }
            for (UINT offset = 0; offset < count; ++offset) { descriptors[first + offset] = true; }
            return first;
        }
        throw std::runtime_error("GPU descriptor budget exhausted.");
    }

    UINT materialTable(const Material& material) {
        std::array<std::string, 4> keys;
        for (std::size_t slot = 0; slot < keys.size(); ++slot) {
            const auto requested = textureKey(material.textures[slot], static_cast<TextureSlot>(slot));
            keys[slot] = textures.contains(requested) ? requested : "__default" + std::to_string(slot);
        }
        if (const auto found = materialTables.find(keys); found != materialTables.end()) { return found->second; }
        const auto first = allocateDescriptors(4);
        for (UINT slot = 0; slot < 4; ++slot) {
            const auto& texture = textures.at(keys[slot]);
            const auto resource = texture.resource->GetDesc();
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = resource.Format;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = resource.MipLevels;
            device->CreateShaderResourceView(texture.resource.Get(), &view, srvCpu(first + slot));
        }
        materialTables.emplace(keys, first);
        return first;
    }

    Impl(HWND handle, const std::string& preference, bool debug)
        : window(handle), shaderCache(localDataDirectory() / L"cache" / L"shaders", 128 * 1024 * 1024) {
        if (debug) {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
                debugController->EnableDebugLayer();
                statistics.debugLayer = true;
                ComPtr<ID3D12Debug1> validation;
                if (SUCCEEDED(debugController.As(&validation))) { validation->SetEnableGPUBasedValidation(TRUE); }
            }
        }
        check(CreateDXGIFactory2(statistics.debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
        ComPtr<IDXGIAdapter1> chosen;
        if (preference == "warp") {
            check(factory->EnumWarpAdapter(IID_PPV_ARGS(&chosen)), "Find WARP adapter");
        } else {
            for (UINT index = 0;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                if (factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) { break; }
                DXGI_ADAPTER_DESC1 description{};
                check(candidate->GetDesc1(&description), "Read GPU description");
                if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { continue; }
                if (preference == "intel" && description.VendorId != 0x8086) { continue; }
                if (preference == "nvidia" && description.VendorId != 0x10de) { continue; }
                if (preference == "amd" && description.VendorId != 0x1002) { continue; }
                if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                    chosen = candidate;
                    break;
                }
            }
        }
        if (!chosen) { throw std::runtime_error("No compatible D3D12 adapter was found. Update your GPU driver or use --adapter=warp for diagnostics."); }
        DXGI_ADAPTER_DESC1 description{};
        check(chosen->GetDesc1(&description), "Read selected adapter");
        statistics.adapter = utf8(description.Description);
        check(chosen.As(&adapter), "Query adapter memory interface");
        check(D3D12CreateDevice(chosen.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "Create D3D12 device");
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_0};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
            || shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0) {
            throw std::runtime_error("This preview requires Shader Model 6.0. Update the GPU driver or choose another adapter.");
        }
        if (statistics.debugLayer) { static_cast<void>(device.As(&infoQueue)); }
        rayScene = std::make_unique<RayTracingScene>(device.Get());
        statistics.rayTracingSupported = rayScene->supported() && (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)), "Create graphics queue");
        check(queue->GetTimestampFrequency(&timestampFrequency), "Read timestamp frequency");
        RECT client{};
        GetClientRect(window, &client);
        width = static_cast<UINT>(std::max<LONG>(1, client.right));
        height = static_cast<UINT>(std::max<LONG>(1, client.bottom));
        DXGI_SWAP_CHAIN_DESC1 swapDescription{};
        swapDescription.Width = width;
        swapDescription.Height = height;
        swapDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDescription.SampleDesc.Count = 1;
        swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDescription.BufferCount = frameCount;
        swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initialSwapchain;
        check(factory->CreateSwapChainForHwnd(queue.Get(), window, &swapDescription, nullptr, nullptr, &initialSwapchain), "Create swapchain");
        check(initialSwapchain.As(&swapchain), "Query swapchain");
        check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "Configure window association");
        createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, frameCount + 1, false, rtvHeap);
        createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false, dsvHeap);
        createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptorCount, true, srvHeap);
        rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dsvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT index = 0; index < 8; ++index) { descriptors[index] = true; }
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create frame fence");
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent) { throw std::runtime_error("Cannot create GPU fence event."); }
        for (auto& frame : frames) {
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)), "Create frame allocator");
            frame.upload = createBuffer(uploadSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            const D3D12_RANGE emptyRange{0, 0};
            check(frame.upload->Map(0, &emptyRange, reinterpret_cast<void**>(&frame.mapped)), "Map frame data");
            frame.timestamps = createBuffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        }
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&commands)), "Create graphics commands");
        check(commands->Close(), "Close initial commands");
        D3D12_QUERY_HEAP_DESC queryDescription{};
        queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDescription.Count = frameCount * 2;
        check(device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&queries)), "Create GPU timestamp queries");
        createBackbuffers();
        createSceneTargets();
        createShadow();
        createRoot();
        const auto compilerPath = executableDirectory() / L"dxcompiler.dll";
        dxcModule = LoadLibraryExW(compilerPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dxcModule) { throw std::runtime_error("DXC is missing. Rebuild to copy dxcompiler.dll and dxil.dll beside the executable."); }
        compilerDigest = sha256(readBytes(compilerPath));
        createPipelines();
    }

    ~Impl() {
        if (queue && fence && fenceEvent) { try { idle(); } catch (...) {} }
        if (uiReady) { ImGui_ImplDX12_Shutdown(); }
        if (win32Ready) { ImGui_ImplWin32_Shutdown(); }
        for (auto& frame : frames) { if (frame.upload && frame.mapped) { frame.upload->Unmap(0, nullptr); } }
        if (fenceEvent) { CloseHandle(fenceEvent); }
        if (dxcModule) { FreeLibrary(dxcModule); }
    }

    void createHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible, ComPtr<ID3D12DescriptorHeap>& heap) {
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = type;
        description.NumDescriptors = count;
        description.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&heap)), "Create descriptor heap");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv(UINT index) const {
        auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * rtvStride;
        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv(UINT index) const {
        auto handle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * dsvStride;
        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(UINT index) const {
        auto handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * srvStride;
        return handle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT index) const {
        auto handle = srvHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * srvStride;
        return handle;
    }

    ComPtr<ID3D12Resource> createBuffer(UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        const auto heap = heapProperties(type);
        const auto description = bufferDescription(size);
        ComPtr<ID3D12Resource> resource;
        const auto initialState = type == D3D12_HEAP_TYPE_DEFAULT && state == D3D12_RESOURCE_STATE_COPY_DEST
            ? D3D12_RESOURCE_STATE_COMMON : state;
        check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, initialState, nullptr, IID_PPV_ARGS(&resource)), "Allocate GPU buffer");
        return resource;
    }

    void wait(UINT64 value) {
        if (value == 0 || fence->GetCompletedValue() >= value) { return; }
        check(fence->SetEventOnCompletion(value, fenceEvent), "Arm GPU fence");
        const auto result = WaitForSingleObject(fenceEvent, 10000);
        if (result != WAIT_OBJECT_0) { throw std::runtime_error("GPU fence timed out. The device may have been removed."); }
    }

    void idle() {
        const auto signal = ++nextFence;
        check(queue->Signal(fence.Get(), signal), "Signal graphics fence");
        wait(signal);
    }

    void immediate(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create transfer allocator");
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create transfer commands");
        record(list.Get());
        check(list->Close(), "Close transfer commands");
        ID3D12CommandList* submitted[] = {list.Get()};
        queue->ExecuteCommandLists(1, submitted);
        idle();
    }

    void createBackbuffers() {
        for (UINT index = 0; index < frameCount; ++index) {
            check(swapchain->GetBuffer(index, IID_PPV_ARGS(&backbuffers[index])), "Get swapchain buffer");
            device->CreateRenderTargetView(backbuffers[index].Get(), nullptr, rtv(index));
            backbuffers[index]->SetName((L"Presentation " + std::to_wstring(index)).c_str());
        }
    }

    ComPtr<ID3D12Resource> createTexture(UINT targetWidth, UINT targetHeight, DXGI_FORMAT format,
        D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear) {
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = targetWidth;
        description.Height = targetHeight;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Flags = flags;
        const auto heap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> resource;
        check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, state, clear, IID_PPV_ARGS(&resource)), "Allocate render texture");
        return resource;
    }

    void createSceneTargets() {
        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorClear.Color[3] = 1;
        sceneColor = createTexture(sceneWidth, sceneHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClear);
        sceneColor->SetName(L"Scene color");
        device->CreateRenderTargetView(sceneColor.Get(), nullptr, rtv(frameCount));
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(sceneColor.Get(), &view, srvCpu(0));
        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = DXGI_FORMAT_D32_FLOAT;
        depthClear.DepthStencil.Depth = 1;
        sceneDepth = createTexture(sceneWidth, sceneHeight, DXGI_FORMAT_D32_FLOAT,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear);
        sceneDepth->SetName(L"Scene depth");
        device->CreateDepthStencilView(sceneDepth.Get(), nullptr, dsv(0));
    }

    void createShadow() {
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1;
        shadow = createTexture(shadowSize, shadowSize, DXGI_FORMAT_R32_TYPELESS,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear);
        shadow->SetName(L"Directional shadow");
        D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};
        depthView.Format = DXGI_FORMAT_D32_FLOAT;
        depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(shadow.Get(), &depthView, dsv(1));
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(shadow.Get(), &view, srvCpu(1));
        statistics.shadowResolution = shadowSize;
    }

    void createRoot() {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_DESCRIPTOR_RANGE maps{};
        maps.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        maps.NumDescriptors = 4;
        maps.BaseShaderRegister = 1;
        maps.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        std::array<D3D12_ROOT_PARAMETER, 6> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister = 0;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants.ShaderRegister = 1;
        parameters[1].Constants.Num32BitValues = 1;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[2].DescriptorTable.NumDescriptorRanges = 1;
        parameters[2].DescriptorTable.pDescriptorRanges = &range;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[3].DescriptorTable.NumDescriptorRanges = 1;
        parameters[3].DescriptorTable.pDescriptorRanges = &maps;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[4].Descriptor.ShaderRegister = 5;
        parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[5].Descriptor.ShaderRegister = 6;
        parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.MaxAnisotropy = 1;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        auto materialSampler = sampler;
        materialSampler.Filter = D3D12_FILTER_ANISOTROPIC;
        materialSampler.AddressU = materialSampler.AddressV = materialSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        materialSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        materialSampler.MaxAnisotropy = 4;
        materialSampler.ShaderRegister = 1;
        const std::array samplers{sampler, materialSampler};
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = static_cast<UINT>(parameters.size());
        description.pParameters = parameters.data();
        description.NumStaticSamplers = static_cast<UINT>(samplers.size());
        description.pStaticSamplers = samplers.data();
        description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        check(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Serialize root signature");
        check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&rootSignature)), "Create root signature");
    }

    std::vector<std::byte> compile(std::string_view source, const wchar_t* entry, const wchar_t* target) {
        const auto key = sha256(compilerDigest + ":hlsl2021-O3-v1:" + utf8(entry) + ":" + utf8(target) + ":" + std::string(source));
        if (auto cached = shaderCache.get(key)) { return std::move(*cached); }
        const auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxcModule, "DxcCreateInstance"));
        if (!createInstance) { throw std::runtime_error("DXC has no DxcCreateInstance export."); }
        ComPtr<IDxcCompiler3> compiler;
        check(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)), "Create DXC compiler");
        const DxcBuffer buffer{source.data(), source.size(), DXC_CP_UTF8};
        const wchar_t* arguments[] = {L"-E", entry, L"-T", target, L"-HV", L"2021", L"-O3", L"-Ges"};
        ComPtr<IDxcResult> result;
        check(compiler->Compile(&buffer, arguments, static_cast<UINT>(std::size(arguments)), nullptr, IID_PPV_ARGS(&result)), "Compile shader");
        HRESULT status = S_OK;
        check(result->GetStatus(&status), "Read shader status");
        if (FAILED(status)) {
            ComPtr<IDxcBlobUtf8> errors;
            static_cast<void>(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
            throw std::runtime_error(errors ? std::string(errors->GetStringPointer(), errors->GetStringLength()) : "Shader compilation failed.");
        }
        ComPtr<IDxcBlob> object;
        check(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr), "Read compiled shader");
        const auto* begin = static_cast<const std::byte*>(object->GetBufferPointer());
        std::vector<std::byte> output(begin, begin + object->GetBufferSize());
        static_cast<void>(shaderCache.put(key, output));
        return output;
    }

    void createPipelines() {
        const auto source = readText(executableDirectory() / L"shaders" / L"scene.hlsl");
        const auto vertex = compile(source, L"VSMain", L"vs_6_0");
        const auto pixel = compile(source, L"PSMain", L"ps_6_0");
        const auto rayPixel = statistics.rayTracingSupported ? compile("#define VELOS_RAY_SHADOWS 1\n" + source, L"PSMain", L"ps_6_5") : std::vector<std::byte>{};
        const auto shadowVertex = compile(source, L"VSShadow", L"vs_6_0");
        const auto shadowPixel = compile(source, L"PSShadow", L"ps_6_0");
        const auto backgroundVertex = compile(source, L"VSBackground", L"vs_6_0");
        const auto backgroundPixel = compile(source, L"PSBackground", L"ps_6_0");
        constexpr D3D12_INPUT_ELEMENT_DESC input[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature.Get();
        description.VS = {vertex.data(), vertex.size()};
        description.PS = {pixel.data(), pixel.size()};
        description.InputLayout = {input, static_cast<UINT>(std::size(input))};
        auto& blend = description.BlendState.RenderTarget[0];
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        description.SampleMask = UINT_MAX;
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        description.RasterizerState.FrontCounterClockwise = TRUE;
        description.RasterizerState.DepthClipEnable = TRUE;
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        description.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        description.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        description.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> nextLit;
        ComPtr<ID3D12PipelineState> nextWire;
        ComPtr<ID3D12PipelineState> nextShadow;
        ComPtr<ID3D12PipelineState> nextBackground;
        std::array<ComPtr<ID3D12PipelineState>, 4> nextSurfaces;
        std::array<ComPtr<ID3D12PipelineState>, 4> nextRaySurfaces;
        check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&nextLit)), "Create scene pipeline");
        for (UINT mode = 0; mode < 4; ++mode) {
            description.RasterizerState.CullMode = mode % 2 == 0 ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
            description.BlendState.RenderTarget[0].BlendEnable = mode >= 2;
            description.BlendState.RenderTarget[0].SrcBlend = mode >= 2 ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
            description.BlendState.RenderTarget[0].DestBlend = mode >= 2 ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
            description.DepthStencilState.DepthWriteMask = mode >= 2 ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
            check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&nextSurfaces[mode])), "Create material surface pipeline");
            if (!rayPixel.empty()) {
                description.PS = {rayPixel.data(), rayPixel.size()};
                check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&nextRaySurfaces[mode])), "Create ray-shadow material pipeline");
                description.PS = {pixel.data(), pixel.size()};
            }
        }
        description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        description.BlendState.RenderTarget[0].BlendEnable = FALSE;
        description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        description.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&nextWire)), "Create wireframe pipeline");
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.VS = {shadowVertex.data(), shadowVertex.size()};
        description.PS = {shadowPixel.data(), shadowPixel.size()};
        description.NumRenderTargets = 0;
        description.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
        description.RasterizerState.DepthBias = 1200;
        description.RasterizerState.SlopeScaledDepthBias = 1.5f;
        check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&nextShadow)), "Create shadow pipeline");
        description.VS = {backgroundVertex.data(), backgroundVertex.size()};
        description.PS = {backgroundPixel.data(), backgroundPixel.size()};
        description.InputLayout = {};
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.RasterizerState.DepthBias = 0;
        description.RasterizerState.SlopeScaledDepthBias = 0;
        description.DepthStencilState.DepthEnable = FALSE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&nextBackground)), "Create background pipeline");
        idle();
        litPipeline = std::move(nextLit);
        wirePipeline = std::move(nextWire);
        shadowPipeline = std::move(nextShadow);
        backgroundPipeline = std::move(nextBackground);
        surfacePipelines = std::move(nextSurfaces);
        raySurfacePipelines = std::move(nextRaySurfaces);
    }

    void draw(const RenderFrame& frame, ImDrawData* ui, bool vsync) {
        if (!ui && (sceneWidth != width || sceneHeight != height)) {
            idle();
            sceneWidth = width;
            sceneHeight = height;
            createSceneTargets();
        }
        if (frame.objects.size() > maxObjects) { throw std::runtime_error("The preview supports at most 10,000 rendered objects."); }
        const UINT slotIndex = swapchain->GetCurrentBackBufferIndex();
        lastBackbuffer = slotIndex;
        auto& slot = frames[slotIndex];
        wait(slot.fenceValue);
        if (slot.timed) {
            UINT64* timings = nullptr;
            const D3D12_RANGE range{0, 16};
            check(slot.timestamps->Map(0, &range, reinterpret_cast<void**>(&timings)), "Read GPU timestamps");
            statistics.gpuMilliseconds = static_cast<double>(timings[1] - timings[0]) * 1000.0 / static_cast<double>(timestampFrequency);
            const D3D12_RANGE noWrites{0, 0};
            slot.timestamps->Unmap(0, &noWrites);
        }
        const auto completed = fence->GetCompletedValue();
        std::erase_if(retiredDescriptors, [&](const auto& retired) {
            if (retired.second <= completed) { descriptors[retired.first] = false; return true; }
            return false;
        });
        FrameConstants constants{};
        XMStoreFloat4x4(&constants.viewProjection, frame.camera.view() * frame.camera.projection());
        const auto direction = XMVector3Normalize(XMLoadFloat3(&frame.sunDirection));
        const auto target = XMLoadFloat3(&frame.camera.target);
        const auto lightPosition = XMVectorSubtract(target, XMVectorScale(direction, 28));
        const auto up = std::abs(XMVectorGetY(direction)) > 0.95f ? XMVectorSet(0,0,1,0) : XMVectorSet(0,1,0,0);
        XMStoreFloat4x4(&constants.lightViewProjection, XMMatrixLookAtRH(lightPosition, target, up)
            * XMMatrixOrthographicRH(28, 28, 0.1f, 70));
        XMStoreFloat4(&constants.eyeExposure, frame.camera.eye());
        constants.eyeExposure.w = frame.exposure;
        constants.sunDirectionAmbient = {frame.sunDirection.x, frame.sunDirection.y, frame.sunDirection.z, frame.ambient};
        constants.sunColorIntensity = frame.sunColor;
        constants.settings = {frame.shadows ? 1.0f : 0.0f, static_cast<float>(std::min<UINT>(frame.lightCount, 16)), 1.0f / static_cast<float>(shadowSize), 0};
        constants.lights = frame.lights;
        const bool maskedCasters = std::any_of(frame.objects.begin(), frame.objects.end(), [](const auto& object) {
            return object.castShadow && !object.unlit && object.surface == SurfaceMode::Masked;
        });
        const bool requestRays = frame.shadows && frame.rayTracedShadows && statistics.rayTracingSupported && !maskedCasters && !frame.wireframe && !rayBudgetExceeded;
        std::vector<RayGeometryInstance> rayInstances;
        if (requestRays) { rayInstances.reserve(frame.objects.size()); }
        BoundingFrustum localFrustum;
        BoundingFrustum worldFrustum;
        if (!frame.camera.orthographic) {
            BoundingFrustum::CreateFromMatrix(localFrustum, frame.camera.projection(), true);
            localFrustum.Transform(worldFrustum, XMMatrixInverse(nullptr, frame.camera.view()));
        }
        std::vector<VisibleInstance> visibleInstances;
        std::vector<VisibleInstance> shadowInstances;
        visibleInstances.reserve(frame.objects.size());
        shadowInstances.reserve(frame.objects.size());
        statistics.lodTrianglesSaved = 0;
        for (std::size_t index = 0; index < frame.objects.size(); ++index) {
            const auto& object = frame.objects[index];
            if (frame.shadows && object.castShadow && !object.unlit && object.surface != SurfaceMode::Transparent) {
                shadowInstances.push_back({static_cast<UINT>(index), 0, 0});
            }
            auto found = meshes.find(object.mesh);
            if (found == meshes.end()) { found = meshes.find("cube"); }
            if (found == meshes.end()) { continue; }
            const auto& mesh = found->second;
            BoundingBox bounds;
            mesh.bounds.Transform(bounds, XMLoadFloat4x4(&object.world));
            const bool cameraVisible = frame.camera.orthographic || worldFrustum.Contains(bounds) != DISJOINT;
            const auto center = XMVector3TransformCoord(XMLoadFloat3(&bounds.Center), frame.camera.view());
            const float depth = std::max(0.001f, -XMVectorGetZ(center));
            const float radius = XMVectorGetX(XMVector3Length(XMLoadFloat3(&bounds.Extents)));
            const float diameter = frame.camera.orthographic ? radius * 2 * static_cast<float>(sceneHeight) / frame.camera.distance
                : radius * XMVectorGetY(frame.camera.projection().r[1]) * static_cast<float>(sceneHeight) / depth;
            const auto lod = frame.lods ? selectMeshLod(diameter, static_cast<UINT>(mesh.levels.size()), frame.lodBias) : 0;
            if (requestRays && object.castShadow && !object.unlit && object.surface != SurfaceMode::Transparent) {
                const auto rayLod = cameraVisible ? lod : 0;
                const auto& level = mesh.levels[rayLod];
                rayInstances.push_back({found->first + "|lod=" + std::to_string(rayLod), mesh.vertices.Get(), mesh.indices.Get(),
                    mesh.vertexView.SizeInBytes / sizeof(Vertex), level.count, sizeof(Vertex),
                    level.view.BufferLocation - mesh.indices->GetGPUVirtualAddress(), object.world});
            }
            if (!cameraVisible) { continue; }
            visibleInstances.push_back({static_cast<UINT>(index), lod, depth});
            statistics.lodTrianglesSaved += (mesh.indexCount - mesh.levels[lod].count) / 3;
        }
        const auto shadowPlan = buildDrawBatches(frame.objects, std::move(shadowInstances), frame.instancing);
        const auto cameraPlan = buildDrawBatches(frame.objects, std::move(visibleInstances), frame.instancing);
        const auto shadowCount = static_cast<UINT>(shadowPlan.instances.size());
        const auto writeInstance = [&](std::uint32_t destination, std::uint32_t source) {
            const auto& object = frame.objects[source];
            ObjectConstants data{};
            data.world = object.world;
            XMStoreFloat4x4(&data.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, XMLoadFloat4x4(&object.world))));
            data.color = object.color;
            data.material = {object.roughness, object.metallic, object.selected ? 1.0f : 0.0f, object.unlit ? 1.0f : 0.0f};
            data.options = {object.grid ? 1.0f : 0.0f, static_cast<float>(object.surface), object.alphaCutoff, object.normalStrength};
            data.uvTransform = {object.uvScale.x, object.uvScale.y, object.uvOffset.x, object.uvOffset.y};
            data.emission = {object.emission.x, object.emission.y, object.emission.z, object.emissionStrength};
            std::memcpy(slot.mapped + frameDataSize + static_cast<std::size_t>(destination) * objectStride, &data, sizeof(data));
        };
        for (UINT index = 0; index < shadowPlan.instances.size(); ++index) { writeInstance(index, shadowPlan.instances[index].object); }
        for (UINT index = 0; index < cameraPlan.instances.size(); ++index) { writeInstance(shadowCount + index, cameraPlan.instances[index].object); }
        statistics.uploadedInstances = shadowCount + static_cast<UINT>(cameraPlan.instances.size());
        statistics.cameraDraws = statistics.shadowDraws = 0;
        statistics.culledObjects = static_cast<UINT>(frame.objects.size() - cameraPlan.instances.size());
        statistics.visibleObjects = static_cast<UINT>(cameraPlan.instances.size());
        check(slot.allocator->Reset(), "Reset frame allocator");
        check(commands->Reset(slot.allocator.Get(), nullptr), "Reset frame commands");
        commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 2);
        D3D12_GPU_VIRTUAL_ADDRESS rayAddress = 0;
        if (requestRays) {
            try { rayAddress = rayScene->record(commands.Get(), slotIndex, frame.sourceRevision, rayInstances); }
            catch (const RayTracingBudgetError&) { rayBudgetExceeded = true; }
        }
        statistics.rayTracedShadows = rayAddress != 0;
        statistics.rayTracingBytes = rayScene->bytes();
        statistics.shadowStatus = !frame.shadows ? "Off" : statistics.rayTracedShadows ? "DXR directional"
            : !frame.rayTracedShadows ? "Raster" : !statistics.rayTracingSupported ? "Raster (DXR unsupported)"
            : maskedCasters ? "Raster (masked casters)" : frame.wireframe ? "Raster (wireframe)"
            : rayBudgetExceeded ? "Raster (DXR memory budget)" : "Raster (no ray casters)";
        constants.settings.w = statistics.rayTracedShadows ? 1.0f : 0.0f;
        std::memcpy(slot.mapped, &constants, sizeof(constants));
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootSignature(rootSignature.Get());
        commands->SetGraphicsRootConstantBufferView(0, slot.upload->GetGPUVirtualAddress());
        commands->SetGraphicsRootDescriptorTable(2, srvGpu(1));
        commands->SetGraphicsRootShaderResourceView(4, slot.upload->GetGPUVirtualAddress() + frameDataSize);
        if (rayAddress != 0) { commands->SetGraphicsRootShaderResourceView(5, rayAddress); }
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        statistics.drawCalls = statistics.triangles = 0;
        const auto drawBatch = [&](const DrawBatches& plan, const DrawBatch& batch, UINT instanceOffset) {
            const auto& instance = plan.instances[batch.first];
            const auto& object = frame.objects[instance.object];
            const auto found = meshes.find(object.mesh);
            const auto fallback = meshes.find("cube");
            if (found == meshes.end() && fallback == meshes.end()) { return; }
            const auto& mesh = found != meshes.end() ? found->second : fallback->second;
            commands->SetGraphicsRootDescriptorTable(3, srvGpu(materialTable(object)));
            commands->SetGraphicsRoot32BitConstant(1, instanceOffset + batch.first, 0);
            commands->IASetVertexBuffers(0, 1, &mesh.vertexView);
            const auto& level = mesh.levels[instance.lod];
            commands->IASetIndexBuffer(&level.view);
            commands->DrawIndexedInstanced(level.count, batch.count, 0, 0, 0);
            ++statistics.drawCalls;
            statistics.triangles += level.count / 3 * batch.count;
        };
        if (frame.shadows && !statistics.rayTracedShadows) {
            auto barrier = transition(shadow.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            commands->ResourceBarrier(1, &barrier);
            const D3D12_VIEWPORT viewport{0,0,static_cast<float>(shadowSize),static_cast<float>(shadowSize),0,1};
            const D3D12_RECT scissor{0,0,static_cast<LONG>(shadowSize),static_cast<LONG>(shadowSize)};
            commands->RSSetViewports(1, &viewport);
            commands->RSSetScissorRects(1, &scissor);
            const auto depthHandle = dsv(1);
            commands->OMSetRenderTargets(0, nullptr, FALSE, &depthHandle);
            commands->ClearDepthStencilView(depthHandle, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
            commands->SetPipelineState(shadowPipeline.Get());
            for (const auto& batch : shadowPlan.batches) {
                drawBatch(shadowPlan, batch, 0);
                ++statistics.shadowDraws;
            }
            barrier = transition(shadow.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commands->ResourceBarrier(1, &barrier);
        }
        auto colorBarrier = transition(sceneColor.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commands->ResourceBarrier(1, &colorBarrier);
        const D3D12_VIEWPORT viewport{0,0,static_cast<float>(sceneWidth),static_cast<float>(sceneHeight),0,1};
        const D3D12_RECT scissor{0,0,static_cast<LONG>(sceneWidth),static_cast<LONG>(sceneHeight)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        const auto targetHandle = rtv(frameCount);
        const auto depthHandle = dsv(0);
        commands->OMSetRenderTargets(1, &targetHandle, FALSE, &depthHandle);
        const float clear[] = {0,0,0,1};
        commands->ClearRenderTargetView(targetHandle, clear, 0, nullptr);
        commands->ClearDepthStencilView(depthHandle, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        commands->SetPipelineState(backgroundPipeline.Get());
        commands->DrawInstanced(3, 1, 0, 0);
        commands->SetPipelineState(frame.wireframe ? wirePipeline.Get() : litPipeline.Get());
        for (const auto& batch : cameraPlan.batches) {
            const auto& object = frame.objects[cameraPlan.instances[batch.first].object];
            const auto pipeline = (object.surface == SurfaceMode::Transparent ? 2u : 0u) + (object.doubleSided ? 1u : 0u);
            const auto& surfaces = statistics.rayTracedShadows ? raySurfacePipelines : surfacePipelines;
            commands->SetPipelineState(frame.wireframe ? wirePipeline.Get() : surfaces[pipeline].Get());
            drawBatch(cameraPlan, batch, shadowCount);
            ++statistics.cameraDraws;
        }
        colorBarrier = transition(sceneColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands->ResourceBarrier(1, &colorBarrier);
        commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 2 + 1);
        commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 2, 2, slot.timestamps.Get(), 0);
        slot.timed = true;
        auto backBarrier = transition(backbuffers[slotIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commands->ResourceBarrier(1, &backBarrier);
        const auto backTarget = rtv(slotIndex);
        commands->OMSetRenderTargets(1, &backTarget, FALSE, nullptr);
        const float background[] = {0.085f,0.09f,0.095f,1};
        commands->ClearRenderTargetView(backTarget, background, 0, nullptr);
        if (ui && uiReady) { ImGui_ImplDX12_RenderDrawData(ui, commands.Get()); }
        backBarrier = transition(backbuffers[slotIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commands->ResourceBarrier(1, &backBarrier);
        if (!ui) {
            const std::array copyBarriers{
                transition(sceneColor.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
                transition(backbuffers[slotIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            commands->ResourceBarrier(static_cast<UINT>(copyBarriers.size()), copyBarriers.data());
            commands->CopyResource(backbuffers[slotIndex].Get(), sceneColor.Get());
            const std::array restoreBarriers{
                transition(sceneColor.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                transition(backbuffers[slotIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
            };
            commands->ResourceBarrier(static_cast<UINT>(restoreBarriers.size()), restoreBarriers.data());
        }
        check(commands->Close(), "Close frame commands");
        ID3D12CommandList* submitted[] = {commands.Get()};
        queue->ExecuteCommandLists(1, submitted);
        check(swapchain->Present(vsync ? 1 : 0, 0), "Present frame");
        slot.fenceValue = ++nextFence;
        check(queue->Signal(fence.Get(), slot.fenceValue), "Signal frame completion");
        DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
        if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory))) {
            statistics.gpuUsage = memory.CurrentUsage;
            statistics.gpuBudget = memory.Budget;
        }
    }
};

Renderer::Renderer(HWND window, const std::string& preference, bool debug)
    : impl_(std::make_unique<Impl>(window, preference, debug)) {
    addTexture("__default0", solidTexture({255,255,255,255}, true));
    addTexture("__default1", solidTexture({128,128,255,255}));
    addTexture("__default2", solidTexture({255,255,255,255}));
    addTexture("__default3", solidTexture({255,255,255,255}, true));
    addMesh("cube", makeCube());
    addMesh("sphere", makeSphere());
    addMesh("plane", makePlane());
    addMesh("quad", makePlane(true));
}

Renderer::~Renderer() = default;

void Renderer::initializeUi() {
    if (impl_->uiReady) { return; }
    if (!ImGui_ImplWin32_Init(impl_->window)) { throw std::runtime_error("Cannot initialize the native editor input backend."); }
    impl_->win32Ready = true;
    ImGui_ImplDX12_InitInfo information;
    information.Device = impl_->device.Get();
    information.CommandQueue = impl_->queue.Get();
    information.NumFramesInFlight = frameCount;
    information.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    information.DSVFormat = DXGI_FORMAT_UNKNOWN;
    information.SrvDescriptorHeap = impl_->srvHeap.Get();
    information.UserData = impl_.get();
    information.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto& implementation = *static_cast<Impl*>(info->UserData);
        for (UINT index = 8; index < descriptorCount; ++index) {
            if (!implementation.descriptors[index]) {
                implementation.descriptors[index] = true;
                *cpu = implementation.srvCpu(index);
                *gpu = implementation.srvGpu(index);
                return;
            }
        }
        throw std::runtime_error("Editor texture descriptor budget exhausted.");
    };
    information.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto& implementation = *static_cast<Impl*>(info->UserData);
        const auto index = static_cast<UINT>((cpu.ptr - implementation.srvCpu(0).ptr) / implementation.srvStride);
        if (index >= 8 && index < descriptorCount) { implementation.retiredDescriptors.emplace_back(index, implementation.nextFence + frameCount); }
    };
    if (!ImGui_ImplDX12_Init(&information)) { throw std::runtime_error("Cannot initialize the D3D12 editor renderer."); }
    impl_->uiReady = true;
}

void Renderer::shutdownUi() {
    impl_->idle();
    if (impl_->uiReady) { ImGui_ImplDX12_Shutdown(); impl_->uiReady = false; }
    if (impl_->win32Ready) { ImGui_ImplWin32_Shutdown(); impl_->win32Ready = false; }
}

void Renderer::newUiFrame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
}

void Renderer::resizeWindow(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || (width == impl_->width && height == impl_->height)) { return; }
    impl_->idle();
    for (auto& buffer : impl_->backbuffers) { buffer.Reset(); }
    check(impl_->swapchain->ResizeBuffers(frameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "Resize presentation buffers");
    impl_->width = width;
    impl_->height = height;
    impl_->createBackbuffers();
}

void Renderer::resizeScene(std::uint32_t width, std::uint32_t height) {
    width = std::clamp(width, 64u, 3840u);
    height = std::clamp(height, 64u, 2160u);
    if (width == impl_->sceneWidth && height == impl_->sceneHeight) { return; }
    impl_->idle();
    impl_->sceneWidth = width;
    impl_->sceneHeight = height;
    impl_->createSceneTargets();
}

void Renderer::setShadowResolution(std::uint32_t size) {
    if (size != 512 && size != 1024 && size != 2048) { return; }
    if (size == impl_->shadowSize) { return; }
    impl_->idle();
    impl_->shadowSize = size;
    impl_->createShadow();
}

std::uint64_t Renderer::sceneTexture() const { return impl_->srvGpu(0).ptr; }

void Renderer::setRayTracingBudget(std::uint64_t bytes) {
    if (bytes > 256 * 1024 * 1024) { throw std::invalid_argument("DXR memory budget must not exceed 256 MB."); }
    impl_->idle();
    impl_->rayScene = std::make_unique<RayTracingScene>(impl_->device.Get(), bytes);
    impl_->rayBudgetExceeded = false;
    impl_->statistics.rayTracingBytes = 0;
    impl_->statistics.rayTracingBudget = bytes;
    impl_->statistics.rayTracedShadows = false;
}

void Renderer::addMesh(const std::string& key, const MeshData& source) {
    if (impl_->meshes.contains(key)) { return; }
    auto optimized = source;
    if (optimized.lods.empty()) { optimizeMesh(optimized); }
    const auto& data = optimized;
    if (data.vertices.empty() || data.indices.empty()) { throw std::runtime_error("Cannot upload empty mesh."); }
    const auto vertexBytes = data.vertices.size() * sizeof(Vertex);
    auto indexBytes = data.indices.size() * sizeof(std::uint32_t);
    for (const auto& level : data.lods) { indexBytes += level.size() * sizeof(std::uint32_t); }
    if (vertexBytes + indexBytes > 64 * 1024 * 1024 || impl_->statistics.meshBytes + vertexBytes + indexBytes > 256 * 1024 * 1024) {
        throw std::runtime_error("Mesh GPU budget exceeded (256 MB preview limit).");
    }
    Impl::GpuMesh mesh;
    mesh.vertices = impl_->createBuffer(vertexBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    mesh.indices = impl_->createBuffer(indexBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    auto staging = impl_->createBuffer(vertexBytes + indexBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    std::byte* mapped = nullptr;
    const D3D12_RANGE noRead{0, 0};
    check(staging->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Map mesh upload");
    std::memcpy(mapped, data.vertices.data(), vertexBytes);
    std::memcpy(mapped + vertexBytes, data.indices.data(), data.indices.size() * sizeof(std::uint32_t));
    auto offset = data.indices.size() * sizeof(std::uint32_t);
    for (const auto& level : data.lods) {
        std::memcpy(mapped + vertexBytes + offset, level.data(), level.size() * sizeof(std::uint32_t));
        offset += level.size() * sizeof(std::uint32_t);
    }
    staging->Unmap(0, nullptr);
    impl_->immediate([&](ID3D12GraphicsCommandList* commands) {
        commands->CopyBufferRegion(mesh.vertices.Get(), 0, staging.Get(), 0, vertexBytes);
        commands->CopyBufferRegion(mesh.indices.Get(), 0, staging.Get(), vertexBytes, indexBytes);
        const std::array barriers{
            transition(mesh.vertices.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(mesh.indices.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        };
        commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    });
    mesh.vertexView = {mesh.vertices->GetGPUVirtualAddress(), static_cast<UINT>(vertexBytes), sizeof(Vertex)};
    mesh.indexView = {mesh.indices->GetGPUVirtualAddress(), static_cast<UINT>(data.indices.size() * sizeof(std::uint32_t)), DXGI_FORMAT_R32_UINT};
    mesh.indexCount = static_cast<UINT>(data.indices.size());
    mesh.levels.push_back({mesh.indexView, mesh.indexCount});
    offset = data.indices.size() * sizeof(std::uint32_t);
    for (const auto& level : data.lods) {
        mesh.levels.push_back({{mesh.indices->GetGPUVirtualAddress() + offset,
            static_cast<UINT>(level.size() * sizeof(std::uint32_t)), DXGI_FORMAT_R32_UINT}, static_cast<UINT>(level.size())});
        offset += level.size() * sizeof(std::uint32_t);
    }
    mesh.bounds = data.bounds;
    mesh.vertices->SetName(wide(key + " vertices").c_str());
    mesh.indices->SetName(wide(key + " indices").c_str());
    impl_->meshes.emplace(key, std::move(mesh));
    impl_->statistics.meshBytes += vertexBytes + indexBytes;
}

const BoundingBox* Renderer::meshBounds(const std::string& key) const {
    const auto found = impl_->meshes.find(key);
    return found == impl_->meshes.end() ? nullptr : &found->second.bounds;
}

bool Renderer::hasMesh(const std::string& key) const { return impl_->meshes.contains(key); }
bool Renderer::hasTexture(const std::string& key) const { return impl_->textures.contains(key); }

void Renderer::addTexture(const std::string& key, const TextureData& texture) {
    if (impl_->textures.contains(key)) { return; }
    texture.validate();
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = texture.mips.front().width;
    description.Height = texture.mips.front().height;
    description.DepthOrArraySize = 1;
    description.MipLevels = static_cast<UINT16>(texture.mips.size());
    description.Format = texture.format;
    description.SampleDesc.Count = 1;
    const auto allocation = impl_->device->GetResourceAllocationInfo(0, 1, &description);
    if (allocation.SizeInBytes > 256 * 1024 * 1024 || impl_->statistics.textureBytes + allocation.SizeInBytes > 256 * 1024 * 1024) {
        throw std::runtime_error("Texture residency budget exceeded (256 MB). Use lower texture resolution or fewer textures.");
    }
    const auto heap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    Impl::GpuTexture gpu;
    check(impl_->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gpu.resource)), "Allocate material texture");
    const auto count = static_cast<UINT>(texture.mips.size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(count);
    std::vector<UINT> rows(count);
    std::vector<UINT64> rowSizes(count);
    UINT64 total = 0;
    impl_->device->GetCopyableFootprints(&description, 0, count, 0, footprints.data(), rows.data(), rowSizes.data(), &total);
    auto upload = impl_->createBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    std::byte* mapped = nullptr;
    const D3D12_RANGE empty{0,0};
    check(upload->Map(0, &empty, reinterpret_cast<void**>(&mapped)), "Map texture upload");
    for (UINT mip = 0; mip < count; ++mip) {
        const auto& source = texture.mips[mip];
        if (source.rowPitch != rowSizes[mip] || source.pixels.size() < source.rowPitch * rows[mip]) {
            upload->Unmap(0, nullptr);
            throw std::runtime_error("Texture subresource layout does not match its GPU footprint.");
        }
        for (UINT row = 0; row < rows[mip]; ++row) {
            std::memcpy(mapped + footprints[mip].Offset + static_cast<std::size_t>(row) * footprints[mip].Footprint.RowPitch,
                source.pixels.data() + row * source.rowPitch, source.rowPitch);
        }
    }
    upload->Unmap(0, nullptr);
    impl_->immediate([&](ID3D12GraphicsCommandList* commands) {
        for (UINT mip = 0; mip < count; ++mip) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = gpu.resource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = mip;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprints[mip];
            commands->CopyTextureRegion(&destination, 0,0,0, &source, nullptr);
        }
        const auto barrier = transition(gpu.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands->ResourceBarrier(1, &barrier);
    });
    gpu.descriptor = impl_->allocateDescriptors(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = texture.format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = count;
    impl_->device->CreateShaderResourceView(gpu.resource.Get(), &view, impl_->srvCpu(gpu.descriptor));
    gpu.resource->SetName(wide(key).c_str());
    impl_->textures.emplace(key, std::move(gpu));
    impl_->statistics.textureBytes += allocation.SizeInBytes;
    impl_->statistics.textureCount = static_cast<UINT>(impl_->textures.size());
}

void Renderer::render(const RenderFrame& frame, ImDrawData* ui, bool vsync) { impl_->draw(frame, ui, vsync); }
void Renderer::waitIdle() { impl_->idle(); }

bool Renderer::reloadShaders(std::string& error) {
    try { impl_->createPipelines(); error.clear(); return true; }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}

RendererStats Renderer::stats() const { return impl_->statistics; }
CacheStats Renderer::shaderCacheStats() const { return impl_->shaderCache.stats(); }

void Renderer::capture(const std::filesystem::path& path) {
    impl_->idle();
    const auto description = impl_->backbuffers[impl_->lastBackbuffer]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = 0;
    impl_->device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    auto readback = impl_->createBuffer(size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    impl_->immediate([&](ID3D12GraphicsCommandList* commands) {
        auto barrier = transition(impl_->backbuffers[impl_->lastBackbuffer].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = impl_->backbuffers[impl_->lastBackbuffer].Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        barrier = transition(impl_->backbuffers[impl_->lastBackbuffer].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        commands->ResourceBarrier(1, &barrier);
    });
    BYTE* pixels = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
    check(readback->Map(0, &range, reinterpret_cast<void**>(&pixels)), "Map screenshot");
    try {
        savePng(path, static_cast<UINT>(description.Width), description.Height, footprint.Footprint.RowPitch, pixels, static_cast<UINT>(size));
    } catch (...) {
        readback->Unmap(0, nullptr);
        throw;
    }
    const D3D12_RANGE noWrites{0, 0};
    readback->Unmap(0, &noWrites);
}

std::string Renderer::validationErrors() const {
    if (!impl_->infoQueue) { return {}; }
    std::string result;
    const auto count = impl_->infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 index = 0; index < count; ++index) {
        SIZE_T length = 0;
        if (FAILED(impl_->infoQueue->GetMessage(index, nullptr, &length))) { continue; }
        std::vector<std::byte> buffer(length);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        if (SUCCEEDED(impl_->infoQueue->GetMessage(index, message, &length))
            && message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
            result.append(message->pDescription);
            result.push_back('\n');
        }
    }
    return result;
}

}