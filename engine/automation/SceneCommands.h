#pragma once

#include "scene/History.h"

#include <functional>
#include <stdexcept>
#include <string_view>

namespace velos::automation {

using Json = nlohmann::json;

class Error : public std::runtime_error {
public:
    Error(std::string code, std::string message) : std::runtime_error(std::move(message)), code_(std::move(code)) {}
    [[nodiscard]] const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

struct SceneEdit {
    Scene candidate;
    Json results = Json::array();
    std::string before;
    std::string after;
};

EntityId identifier(const Json& value, bool allowRoot = false);
Json componentCatalog();
Json describeScene(const Scene& scene, std::size_t offset = 0, std::size_t limit = 100);
SceneEdit prepareSceneEdit(const Scene& scene, const Json& request);
Json executeSceneEdit(Scene& scene, History& history, const Json& request,
    const std::function<void(const Scene&)>& prepareAssets = {});

}