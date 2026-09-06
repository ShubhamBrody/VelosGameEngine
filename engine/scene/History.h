#pragma once

#include "scene/Scene.h"

#include <deque>
#include <optional>
#include <memory>

namespace velos {

class History {
public:
    explicit History(std::size_t budget = 8 * 1024 * 1024) : budget_(budget) {}
    void begin(const Scene& scene);
    void commit(const Scene& scene, std::string label, bool mayHaveChanged = true);
    void cancel() noexcept { pending_.reset(); }
    bool undo(Scene& scene, std::string& error);
    bool redo(Scene& scene, std::string& error);
    void clear();
    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < commands_.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::uint64_t serializationCount() const noexcept { return serializations_; }

private:
    struct Command { std::string before; std::string after; std::string label; };
    std::deque<Command> commands_;
    std::shared_ptr<const std::string> pending_;
    std::shared_ptr<const std::string> snapshot_;
    std::uint64_t snapshotRevision_ = 0;
    std::uint64_t pendingRevision_ = 0;
    std::uint64_t serializations_ = 0;
    std::size_t cursor_ = 0;
    std::size_t bytes_ = 0;
    std::size_t budget_;
};

}