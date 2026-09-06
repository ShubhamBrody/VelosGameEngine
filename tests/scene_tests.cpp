#include "scene/Scene.h"
#include "scene/History.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

void testScene() {
    auto scene = velos::Scene::demo();
    const auto saved = scene.serialize();
    velos::Scene loaded;
    std::string error;
    require(loaded.deserialize(saved, error), error.c_str());
    require(saved == loaded.serialize(), "Canonical scene round trip.");
    const auto parent = scene.create("Parent");
    scene.get<velos::Transform>(parent)->position = {10, 0, 0};
    const auto child = scene.addPrimitive("cube", "Child");
    scene.get<velos::Transform>(child)->position = {3, 2, 1};
    scene.touch();
    require(scene.reparent(child, parent), "Reparent should succeed.");
    DirectX::XMFLOAT4X4 world;
    DirectX::XMStoreFloat4x4(&world, scene.worldMatrix(child));
    require(std::abs(world._41 - 3) < 1e-5f, "Reparent must preserve world position.");
    require(!scene.reparent(parent, child), "Hierarchy cycles must be rejected.");
    require(!scene.reparent(child, 99999), "Missing parents must be rejected.");
    const auto clone = scene.duplicate(child);
    require(clone != child && scene.get<velos::Transform>(clone)->parent == parent, "Duplicate must have a new ID.");
    scene.destroy(parent);
    require(!scene.contains(child) && !scene.contains(clone), "Subtree deletion must remove descendants.");
    const auto beforeFailure = scene.serialize();
    require(!scene.deserialize("{broken", error), "Malformed JSON must be rejected.");
    require(scene.serialize() == beforeFailure, "Failed load must leave the current scene unchanged.");
    auto invalid = loaded.toJson();
    invalid["entities"][1]["id"] = invalid["entities"][0]["id"];
    require(!scene.deserialize(invalid.dump(), error), "Duplicate IDs must be rejected.");
    invalid = loaded.toJson();
    invalid["entities"][0]["parent"] = invalid["entities"][0]["id"];
    require(!scene.deserialize(invalid.dump(), error), "Serialized hierarchy cycles must be rejected.");
    invalid = loaded.toJson();
    invalid["entities"][0]["scale"] = {0, 1, 1};
    require(!scene.deserialize(invalid.dump(), error), "Zero scales must be rejected.");
    invalid = loaded.toJson();
    invalid["schema"] = 99;
    require(!scene.deserialize(invalid.dump(), error), "Future schemas must not be silently loaded.");
    velos::Scene deep;
    auto chain = deep.create("Root");
    for (int level = 0; level < 60; ++level) {
        const auto childId = deep.create("Chain");
        require(deep.reparent(childId, chain, false), "Valid hierarchy depth.");
        chain = childId;
    }
    const auto subtree = deep.create("Subtree");
    auto leaf = subtree;
    for (int level = 0; level < 6; ++level) {
        const auto childId = deep.create("Leaf");
        require(deep.reparent(childId, leaf, false), "Valid small subtree.");
        leaf = childId;
    }
    require(!deep.reparent(subtree, chain, false), "Reparent must account for descendant depth.");
}

void testHistory() {
    auto scene = velos::Scene::demo();
    velos::History history;
    const auto before = scene.serialize();
    history.begin(scene);
    scene.addPrimitive("cube", "New cube");
    history.commit(scene, "Create cube");
    const auto after = scene.serialize();
    std::string error;
    require(history.undo(scene, error) && scene.serialize() == before, "Undo must restore canonical state.");
    require(history.redo(scene, error) && scene.serialize() == after, "Redo must restore the edit.");
    require(history.undo(scene, error), "Second undo.");
    history.begin(scene);
    scene.addPrimitive("sphere");
    history.commit(scene, "Create sphere");
    require(!history.canRedo(), "A new edit must discard the redo branch.");
    velos::History tiny(16);
    tiny.begin(scene);
    scene.addPrimitive("cube");
    tiny.commit(scene, "Over budget");
    require(tiny.bytes() <= 16 && !tiny.canUndo(), "History must respect its byte budget.");
    velos::History idle;
    for (int frame = 0; frame < 600; ++frame) { idle.begin(scene); idle.commit(scene, "Idle", false); }
    require(idle.serializationCount() == 1 && !idle.canUndo(), "Unchanged editor frames must reuse their snapshot.");
}

void testMaterials() {
    velos::Scene scene;
    const auto id = scene.addPrimitive("cube");
    auto& material = *scene.get<velos::MeshRenderer>(id);
    material.textures = {"Assets/base.png", "Assets/normal.dds", "Assets/orm.png", "Assets/emissive.png"};
    material.uvScale = {3, 2};
    material.uvOffset = {0.1f, -0.25f};
    material.emissionStrength = 4;
    material.surface = velos::SurfaceMode::Masked;
    material.doubleSided = false;
    const auto saved = scene.serialize();
    velos::Scene loaded;
    std::string error;
    require(loaded.deserialize(saved, error) && loaded.serialize() == saved, "Textured material round trip.");
    velos::History history;
    history.begin(scene);
    material.textures[0] = "Assets/replacement.png";
    history.commit(scene, "Replace texture");
    require(history.undo(scene, error) && scene.serialize() == saved, "Texture edits must be undoable.");
    auto invalid = loaded.toJson();
    invalid["entities"][0]["mesh"]["textures"][0] = "Assets/../secret.png";
    require(!loaded.deserialize(invalid.dump(), error), "Texture paths must remain inside the project.");
    require(loaded.serialize() == saved, "Invalid material loads must not change the scene.");
    invalid = scene.toJson();
    invalid["entities"][0]["mesh"]["surface"] = 17;
    require(!loaded.deserialize(invalid.dump(), error), "Unknown surface mode must be rejected.");
    auto legacy = scene.toJson();
    for (const auto* property : {"textures", "uvScale", "uvOffset", "emission", "emissionStrength", "normalStrength", "alphaCutoff", "surface", "doubleSided"}) {
        legacy["entities"][0]["mesh"].erase(property);
    }
    require(loaded.deserialize(legacy.dump(), error), "Legacy untextured scene must still load.");
    require(loaded.get<velos::MeshRenderer>(id)->textures[0].empty(), "Legacy material default texture.");
}

}

int main() {
    try {
        testScene();
        testHistory();
        testMaterials();
        std::cout << "PASS: scene round trips, hierarchy, validation, transactional load and bounded undo/redo.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}