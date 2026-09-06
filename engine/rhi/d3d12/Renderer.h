#pragma once

#include "assets/Mesh.h"
#include "render/RenderFrame.h"

#include <Windows.h>
#include <filesystem>
#include <memory>
#include <string>

struct ImDrawData;

namespace velos {

class Renderer {
public:
    Renderer(HWND window, const std::string& adapterPreference, bool debug);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initializeUi();
    void shutdownUi();
    void newUiFrame();
    void resizeWindow(std::uint32_t width, std::uint32_t height);
    void resizeScene(std::uint32_t width, std::uint32_t height);
    void setShadowResolution(std::uint32_t size);
    [[nodiscard]] std::uint64_t sceneTexture() const;
    void addMesh(const std::string& key, const MeshData& mesh);
    [[nodiscard]] const DirectX::BoundingBox* meshBounds(const std::string& key) const;
    [[nodiscard]] bool hasMesh(const std::string& key) const;
    void render(const RenderFrame& frame, ImDrawData* ui, bool vsync = true);
    bool reloadShaders(std::string& error);
    void capture(const std::filesystem::path& path);
    void waitIdle();
    [[nodiscard]] RendererStats stats() const;
    [[nodiscard]] CacheStats shaderCacheStats() const;
    [[nodiscard]] std::string validationErrors() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}