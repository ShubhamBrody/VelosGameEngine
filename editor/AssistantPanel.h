#pragma once

#include "ai/Assistant.h"
#include "scene/Scene.h"

#include <deque>

namespace velos {

class AssistantPanel {
public:
    explicit AssistantPanel(bool isolated);
    ~AssistantPanel();
    void draw(const Scene& scene, const std::filesystem::path& scenePath, bool playing);
    void cancel() { assistant_.cancel(); }
    void clearConversation();

private:
    Assistant assistant_;
    AiSettings settings_;
    std::filesystem::path settingsPath_;
    std::deque<std::pair<std::string, std::string>> conversation_;
    std::string prompt_;
    std::string secret_;
    std::string notice_;
    std::string scope_;
    bool includeScene_ = false;
    bool pendingAnswer_ = false;
    bool isolated_ = false;
    bool scrollToEnd_ = false;
    void send(const Scene& scene, const std::filesystem::path& scenePath);
    void settingsPopup();
};

}