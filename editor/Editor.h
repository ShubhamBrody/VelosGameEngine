#pragma once

#include "bridge/Extract.h"
#include "core/FixedStepClock.h"
#include "rhi/d3d12/Renderer.h"
#include "scene/History.h"

#include <array>
#include <deque>
#include <filesystem>
#include <string>

namespace velos {

class Editor {
public:
    Editor(HWND window, Renderer& renderer, bool isolated = false);
    ~Editor();
    void draw(double elapsed);
    void update(double elapsed);
    void openScene(const std::filesystem::path& path);
    bool saveScene(bool choosePath = false);
    void startPlay();
    void stopPlay();
    void stepPlay();
    void resetLayout() { layoutBuilt_ = false; }
    void requestClose();
    void runAuthoringCheck();
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
    bool wantsClose_ = false;
    bool showUnsaved_ = false;
    bool changed_ = false;
    bool gizmoActive_ = false;
    bool isolated_ = false;
    int operation_ = 0;
    int pendingAction_ = 0;
    EntityId pendingDelete_ = 0;
    EntityId pendingDuplicate_ = 0;

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
};

}