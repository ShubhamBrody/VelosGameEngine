#pragma once

#include "scene/History.h"

#include <memory>

namespace velos {

class GraphPanel {
public:
    GraphPanel();
    ~GraphPanel();
    GraphPanel(const GraphPanel&) = delete;
    GraphPanel& operator=(const GraphPanel&) = delete;
    bool draw(Scene& scene, History& history, EntityId& selected, bool playing, const nlohmann::json& execution);
    void show(const std::string& view, bool arrange = false);
    [[nodiscard]] std::string view() const;
    [[nodiscard]] const std::string& error() const;
    [[nodiscard]] bool focused() const noexcept;
    [[nodiscard]] nlohmann::json state() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}