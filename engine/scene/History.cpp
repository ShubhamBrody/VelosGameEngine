#include "scene/History.h"

namespace velos {

void History::begin(const Scene& scene) {
    if (pending_) { return; }
    if (!snapshot_ || snapshotRevision_ != scene.revision()) {
        snapshot_ = std::make_shared<const std::string>(scene.serialize());
        snapshotRevision_ = scene.revision();
        ++serializations_;
    }
    pending_ = snapshot_;
    pendingRevision_ = scene.revision();
}

void History::commit(const Scene& scene, std::string label, bool mayHaveChanged) {
    if (!pending_) { return; }
    if (!mayHaveChanged && pendingRevision_ == scene.revision()) { pending_.reset(); return; }
    auto before = *pending_;
    pending_.reset();
    auto after = scene.serialize();
    ++serializations_;
    snapshot_ = std::make_shared<const std::string>(after);
    snapshotRevision_ = scene.revision();
    if (before == after) { return; }
    while (commands_.size() > cursor_) {
        bytes_ -= commands_.back().before.size() + commands_.back().after.size();
        commands_.pop_back();
    }
    const auto cost = before.size() + after.size();
    if (cost > budget_) { clear(); return; }
    while (!commands_.empty() && (bytes_ + cost > budget_ || commands_.size() >= 128)) {
        bytes_ -= commands_.front().before.size() + commands_.front().after.size();
        commands_.pop_front();
        --cursor_;
    }
    commands_.push_back({std::move(before), std::move(after), std::move(label)});
    bytes_ += cost;
    cursor_ = commands_.size();
}

bool History::undo(Scene& scene, std::string& error) {
    if (!canUndo()) { return false; }
    if (!scene.deserialize(commands_[cursor_ - 1].before, error)) { return false; }
    --cursor_;
    pending_.reset();
    return true;
}

bool History::redo(Scene& scene, std::string& error) {
    if (!canRedo()) { return false; }
    if (!scene.deserialize(commands_[cursor_].after, error)) { return false; }
    ++cursor_;
    pending_.reset();
    return true;
}

void History::clear() {
    commands_.clear();
    pending_.reset();
    cursor_ = 0;
    bytes_ = 0;
    snapshot_.reset();
    snapshotRevision_ = 0;
}

}