#include "automation/SceneCommands.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
template<class Function> void rejects(Function action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
}

int main() {
    try {
        velos::Scene scene;
        velos::History history;
        const auto original = scene.serialize();
        const auto revision = scene.revision();
        const Json request{{"expected_revision", std::to_string(revision)}, {"label", "Create and configure objects"}, {"operations", Json::array({
            {{"op", "create"}, {"name", "Rig"}, {"as", "rig"}, {"fields", {{"position", {3,0,0}}}}},
            {{"op", "create"}, {"name", "Player"}, {"primitive", "sphere"}, {"as", "player"}, {"fields", {{"position", {1,2,3}}, {"mesh", {{"roughness", 0.2}}}, {"keyboardDrive", 6}}}},
            {{"op", "reparent"}, {"entity", "@player"}, {"parent", "@rig"}, {"preserve_world", true}},
            {{"op", "transform"}, {"entity", "@player"}, {"space", "world"}, {"translate", {0,1,0}}, {"rotation_degrees", {0,90,0}}, {"scale", {2,2,2}}}
        })}};
        const auto result = velos::automation::executeSceneEdit(scene, history, request);
        const auto player = velos::automation::identifier(result.at("results")[1].at("entity"));
        const auto rig = velos::automation::identifier(result.at("results")[0].at("entity"));
        require(scene.entities().size() == 2 && scene.get<velos::Transform>(player)->parent == rig, "Create aliases and reparenting must work in one transaction.");
        const auto world = scene.worldMatrix(player);
        require(std::abs(DirectX::XMVectorGetX(world.r[3]) - 1) < 0.001f && std::abs(DirectX::XMVectorGetY(world.r[3]) - 3) < 0.001f, "World-space movement must preserve the intended position under a parent.");
        require(std::abs(scene.get<velos::MeshRenderer>(player)->roughness - 0.2f) < 0.001f && scene.get<velos::KeyboardDrive>(player)->speed == 6, "Material and gameplay variables must be editable.");
        const auto committed = scene.serialize();
        const auto committedRevision = scene.revision();
        const auto historyBytes = history.bytes();
        rejects([&] { velos::automation::executeSceneEdit(scene, history, {{"operations", Json::array({
            {{"op", "patch"}, {"entity", player}, {"fields", {{"name", "Must roll back"}}}},
            {{"op", "patch"}, {"entity", player}, {"fields", {{"scale", {0,1,1}}}}}
        })}}); }, "Invalid batch must fail.");
        require(scene.serialize() == committed && scene.revision() == committedRevision && history.bytes() == historyBytes, "A failed batch must preserve state, revision and undo history.");
        rejects([&] { velos::automation::executeSceneEdit(scene, history, request); }, "Stale revision must fail.");
        rejects([&] { velos::automation::executeSceneEdit(scene, history, {{"operations", Json::array({
            {{"op", "patch"}, {"entity", player}, {"fields", {{"mesh", {{"roughnes", 0.4}}}}}}
        })}}); }, "Misspelled component fields must not be silently accepted.");
        rejects([&] { velos::automation::executeSceneEdit(scene, history, {{"operations", Json::array({
            {{"op", "patch"}, {"entity", player}, {"fields", {{"mesh", {{"asset", "../private.glb"}}}}}}
        })}}); }, "Unsafe mesh references must fail.");
        rejects([&] { velos::automation::executeSceneEdit(scene, history, {{"operations", Json::array({
            {{"op", "reparent"}, {"entity", rig}, {"parent", player}}
        })}}); }, "Hierarchy cycles must fail.");
        auto dryRun = Json{{"dry_run", true}, {"operations", Json::array({{{"op", "delete"}, {"entity", rig}}})}};
        const auto preview = velos::automation::executeSceneEdit(scene, history, dryRun);
        require(preview.at("changed") == true && scene.serialize() == committed && history.bytes() == historyBytes, "Dry runs must not modify scene or history.");
        std::string error;
        require(history.undo(scene, error) && scene.serialize() == original && !history.canUndo(), "Entire batch must undo in one step.");
        require(history.redo(scene, error) && scene.serialize() == committed, "Entire batch must redo.");
        const auto page = velos::automation::describeScene(scene, 1, 1);
        require(page.at("entities").size() == 1 && page.at("entities")[0].at("id").is_string() && page.at("total_entities") == 2, "Queries must paginate and return lossless string IDs.");
        require(velos::automation::componentCatalog().size() == 8, "All existing component classes must be discoverable.");
        velos::History smallHistory(64);
        rejects([&] { velos::automation::executeSceneEdit(scene, smallHistory, {{"operations", Json::array({{{"op", "delete"}, {"entity", rig}}})}}); }, "Oversized undo transactions must fail explicitly.");
        require(scene.serialize() == committed && !smallHistory.canUndo(), "Budget rejection must preserve the scene.");
        history.begin(scene);
        rejects([&] { velos::automation::executeSceneEdit(scene, history, dryRun); }, "In-progress UI edits must not be interrupted.");
        history.cancel();
        rejects([&] { velos::automation::executeSceneEdit(scene, history, {{"operations", Json::array({{{"op", "delete"}, {"entity", rig}}})}}, [](const auto&) { throw std::runtime_error("Missing asset"); }); }, "Asset preparation failure must reject the transaction.");
        require(scene.serialize() == committed, "Asset preparation must happen before scene mutation.");
        auto largeDocument = scene.toJson();
        largeDocument["entities"] = Json::array({largeDocument.at("entities")[0]});
        largeDocument["entities"][0]["id"] = 9007199254740992ULL;
        require(scene.deserialize(largeDocument.dump(), error), "Large native entity IDs must load.");
        history.clear();
        velos::automation::executeSceneEdit(scene, history, {{"operations", Json::array({
            {{"op", "patch"}, {"entity", "9007199254740992"}, {"fields", {{"name", "Lossless ID"}}}}
        })}});
        require(scene.get<velos::Identity>(9007199254740992ULL)->name == "Lossless ID", "String IDs must retain precision through mutation.");
        rejects([&] { static_cast<void>(velos::automation::identifier(Json(9007199254740992ULL))); }, "Unsafe JSON number IDs must require strings.");
        std::cout << "PASS: atomic scene automation, transforms, fields, aliases, revisions, dry runs, undo and failure isolation.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}