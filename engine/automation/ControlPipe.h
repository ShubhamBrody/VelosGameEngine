#pragma once

#include "automation/SceneCommands.h"

#include <functional>
#include <memory>

namespace velos::automation {

class ControlPipe {
public:
    using Handler = std::function<Json(std::string_view, const Json&)>;
    explicit ControlPipe(std::string name, std::function<void()> wakeOwner = {});
    ~ControlPipe();
    ControlPipe(const ControlPipe&) = delete;
    ControlPipe& operator=(const ControlPipe&) = delete;
    std::size_t pump(const Handler& handler, std::size_t maximum = 4);
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] Json statistics() const;
    static constexpr std::uint32_t maximumMessageBytes = 16 * 1024 * 1024;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}