#pragma once

#include "AssistantPanel.h"
#include "GraphPanel.h"
#include "bridge/Extract.h"
#include "core/FixedStepClock.h"
#include "rhi/d3d12/Renderer.h"
#include "physics/PhysicsWorld.h"
#include "physics/BehaviorRuntime.h"
#include "scene/History.h"
#include "automation/ControlPipe.h"

#include <array>
#include <deque>
#include <filesystem>
#include <future>
#include <string>

namespace velos {

class Editor {
public:
    Editor(HWND window, Renderer& renderer, bool isolated = false);
    ~Editor();
    void draw(double elapsed);
    void update(double elapsed);
    bool openScene(const std::filesystem::path& path);
    bool saveScene(bool choosePath = false);
    void startPlay();
    void stopPlay();
    void stepPlay();
    void resetLayout() { layoutBuilt_ = false; forceLayout_ = true; }
    void requestClose();
    void runAuthoringCheck();
    void showGraph(const std::string& view) { graphs_.show(view); }
    void importGlb(const std::filesystem::path& path);
    void enableAutomation(const std::string& pipe, const std::filesystem::path& root, bool readOnly = false);
    void pumpAutomation();
    void renderedAutomationFrame() noexcept { renderedRevision_ = scene_.revision(); ++renderedFrames_; }
    nlohmann::json automationRequest(std::string_view method, const nlohmann::json& params);
    [[nodiscard]] bool automationEnabled() const noexcept { return control_ != nullptr; }
    [[nodiscard]] RenderFrame& frame() { return frame_; }
    [[nodiscard]] bool wantsClose() const noexcept { return wantsClose_; }
    [[nodiscard]] bool vsync() const noexcept { return vsync_; }
    [[nodiscard]] Scene& scene() noexcept { return scene_; }
    [[nodiscard]] const std::filesystem::path& scenePath() const noexcept { return scenePath_; }
    void log(std::string message, bool error = false);

private:
    HWND window_;
    Renderer& renderer_;
    Scene scene_;
    History history_;
    FixedStepClock clock_;
    PhysicsWorld physics_;
    BehaviorRuntime behaviors_;
    AssistantPanel assistant_;
    GraphPanel graphs_;
    DiskCache geometryCache_;
    DiskCache textureCache_;
    struct ImportedAsset { MeshData mesh; std::string reference; std::string name; std::filesystem::path project; };
    std::future<ImportedAsset> import_;
    struct ImportedTexture { TextureData texture; std::string reference; EntityId entity; TextureSlot slot; std::filesystem::path project; std::uint64_t revision; };
    std::future<ImportedTexture> textureImport_;
    std::future<std::filesystem::path> export_;
    std::string importStatus_;
    RenderFrame frame_;
    EntityId selected_ = 2;
    std::filesystem::path scenePath_;
    std::string savedState_;
    std::string playSnapshot_;
    std::string layoutPath_;
    std::string filter_;
    std::deque<std::pair<std::string, bool>> messages_;
    std::array<float, 180> frameTimes_{};
    std::size_t frameIndex_ = 0;
    float elapsedSmoothed_ = 0;
    float resolutionScale_ = 1;
    float snapSize_ = 0.5f;
    double autosaveSeconds_ = 0;
    bool playing_ = false;
    bool paused_ = false;
    bool grid_ = true;
    bool snap_ = false;
    bool localSpace_ = false;
    bool vsync_ = true;
    bool layoutBuilt_ = false;
    bool forceLayout_ = false;
    bool wantsClose_ = false;
    bool showUnsaved_ = false;
    bool changed_ = false;
    bool dirty_ = false;
    std::uint64_t dirtyRevision_ = 0;
    bool gizmoActive_ = false;
    bool isolated_ = false;
    int operation_ = 0;
    int pendingAction_ = 0;
    EntityId pendingDelete_ = 0;
    EntityId pendingDuplicate_ = 0;
    std::filesystem::path automationRoot_;
    bool automationReadOnly_ = false;
    std::uint64_t renderedRevision_ = 0;
    std::uint64_t renderedFrames_ = 0;
    struct ControlJob { std::string id; std::string kind; std::string state; nlohmann::json result; std::string error; };
    std::deque<ControlJob> controlJobs_;
    std::future<std::function<nlohmann::json()>> controlWork_;
    std::uint64_t nextControlJob_ = 1;
    bool cancelControlJob_ = false;
    std::set<int> controlKeys_;
    std::unique_ptr<automation::ControlPipe> control_;

    void toolbar();
    void hierarchy();
    void hierarchyItem(EntityId id);
    void inspector();
    void viewport();
    void assets();
    void diagnostics();
    void console();
    void dialogs();
    void buildLayout();
    void addPrimitive(const std::string& mesh);
    void focusSelection();
    void pick(float horizontal, float vertical, float width, float height);
    void applyPendingAction();
    std::filesystem::path chooseScenePath(bool save);
    void chooseImport();
    void chooseTexture(TextureSlot slot);
    void exportRuntime();
    void prepareAutomationAssets(const Scene& scene, const std::filesystem::path& project);
    std::filesystem::path automationPath(std::string_view relative, std::string_view extension = {}) const;
    void requireAutomationEdit(const nlohmann::json& params, bool requireRevision = true) const;
    void simulateTick();
};

}