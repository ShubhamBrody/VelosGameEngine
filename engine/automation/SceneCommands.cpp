#include "automation/SceneCommands.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace velos::automation {
namespace {

void keys(const Json& value, std::initializer_list<std::string_view> allowed) {
    if (!value.is_object()) { throw Error("invalid_arguments", "Expected an object."); }
    for (const auto& [name, ignored] : value.items()) {
        static_cast<void>(ignored);
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) { throw Error("unknown_field", "Unknown field: " + name); }
    }
}

const Json& defaults() {
    static const Json value = [] {
        Scene scene;
        const auto entity = scene.addPrimitive("cube", "Entity");
        scene.set<Light>(entity);
        scene.set<RigidBody>(entity);
        scene.set<Spin>(entity);
        scene.set<KeyboardDrive>(entity);
        scene.set<BehaviorGraph>(entity);
        return scene.toJson().at("entities").at(0);
    }();
    return value;
}

void assetReference(const Json& value, bool texture) {
    const auto path = value.get<std::string>();
    if (texture && path.empty()) { return; }
    if (!texture && (path == "cube" || path == "sphere" || path == "plane" || path == "quad")) { return; }
    if (path.size() > 1024 || !path.starts_with("Assets/") || path.find("..") != std::string::npos
        || path.find_first_of(":\\") != std::string::npos || path.find('\0') != std::string::npos) {
        throw Error("invalid_asset", "Asset references must be built-in meshes or safe project-relative Assets paths.");
    }
}

void patchEntity(Json& entity, const Json& patch) {
    keys(patch, {"name", "visible", "position", "rotation", "scale", "mesh", "light", "body", "spin", "keyboardDrive", "behavior"});
    for (const auto& [name, value] : patch.items()) {
        if (name == "mesh" || name == "light" || name == "body" || name == "behavior") {
            if (value.is_null()) { entity.erase(name); continue; }
            if (!value.is_object()) { throw Error("invalid_arguments", "Component values must be objects or null."); }
            for (const auto& field : value.items()) {
                if (!defaults().at(name).contains(field.key())) { throw Error("unknown_field", "Unknown " + name + " field: " + field.key()); }
            }
            if (!entity.contains(name)) { entity[name] = defaults().at(name); }
            entity[name].merge_patch(value);
        } else if ((name == "spin" || name == "keyboardDrive") && value.is_null()) {
            entity.erase(name);
        } else { entity[name] = value; }
    }
    if (entity.contains("mesh")) {
        const auto& mesh = entity.at("mesh");
        assetReference(mesh.at("asset"), false);
        if (mesh.contains("textures")) { for (const auto& texture : mesh.at("textures")) { assetReference(texture, true); } }
    }
}

Json& findEntity(Json& document, EntityId id) {
    for (auto& entity : document.at("entities")) { if (entity.at("id").get<EntityId>() == id) { return entity; } }
    throw Error("entity_not_found", "Entity does not exist: " + std::to_string(id));
}

Scene validated(const Json& document) {
    Scene scene;
    std::string error;
    if (!scene.deserialize(document.dump(), error)) { throw Error("invalid_scene", error); }
    return scene;
}

DirectX::XMVECTOR vector3(const Json& value) {
    if (!value.is_array() || value.size() != 3) { throw Error("invalid_arguments", "Expected a three-number vector."); }
    float components[3]{};
    for (std::size_t index = 0; index < 3; ++index) {
        if (!value[index].is_number()) { throw Error("invalid_arguments", "Vector components must be numbers."); }
        const auto component = value[index].get<double>();
        if (!std::isfinite(component) || std::abs(component) > 1e6) { throw Error("invalid_arguments", "Vector components must be finite and bounded."); }
        components[index] = static_cast<float>(component);
    }
    return DirectX::XMVectorSet(components[0], components[1], components[2], 0);
}

void transform(Scene& scene, EntityId id, const Json& operation) {
    using namespace DirectX;
    keys(operation, {"op", "entity", "space", "position", "rotation_degrees", "scale", "translate", "rotate_degrees", "scale_factor"});
    const auto space = operation.value("space", std::string("local"));
    if (space != "local" && space != "world") { throw Error("invalid_arguments", "Transform space must be local or world."); }
    if (!scene.contains(id)) { throw Error("entity_not_found", "Transform target does not exist."); }
    XMVECTOR scale;
    XMVECTOR rotation;
    XMVECTOR position;
    const auto matrix = space == "world" ? scene.worldMatrix(id) : scene.get<Transform>(id)->matrix();
    if (!XMMatrixDecompose(&scale, &rotation, &position, matrix)) { throw Error("invalid_transform", "Transform cannot be decomposed without shear."); }
    if (operation.contains("position")) { position = vector3(operation.at("position")); }
    if (operation.contains("translate")) { position = XMVectorAdd(position, vector3(operation.at("translate"))); }
    if (operation.contains("scale")) { scale = vector3(operation.at("scale")); }
    if (operation.contains("scale_factor")) { scale = XMVectorMultiply(scale, vector3(operation.at("scale_factor"))); }
    if (operation.contains("rotation_degrees")) { rotation = XMQuaternionRotationRollPitchYawFromVector(XMVectorScale(vector3(operation.at("rotation_degrees")), XM_PI / 180)); }
    if (operation.contains("rotate_degrees")) {
        const auto delta = XMQuaternionRotationRollPitchYawFromVector(XMVectorScale(vector3(operation.at("rotate_degrees")), XM_PI / 180));
        rotation = space == "local" ? XMQuaternionMultiply(delta, rotation) : XMQuaternionMultiply(rotation, delta);
    }
    const auto changed = XMMatrixScalingFromVector(scale) * XMMatrixRotationQuaternion(rotation) * XMMatrixTranslationFromVector(position);
    const bool accepted = space == "world" ? scene.setWorldMatrix(id, changed) : scene.setLocalMatrix(id, changed);
    if (!accepted) { throw Error("invalid_transform", "Transform is singular, out of bounds or requires unsupported shear."); }
}

}

EntityId identifier(const Json& value, bool allowRoot) {
    EntityId result = 0;
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) { throw Error("invalid_identifier", "Expected an unsigned decimal identifier."); }
    } else if (value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0)) {
        result = value.get<EntityId>();
        if (result > 9007199254740991ULL) { throw Error("invalid_identifier", "Large identifiers must be passed as decimal strings."); }
    } else { throw Error("invalid_identifier", "Identifiers must be decimal strings or nonnegative integers."); }
    if ((!allowRoot && result == 0) || result >= std::numeric_limits<EntityId>::max() - 1) { throw Error("invalid_identifier", "Identifier is out of range."); }
    return result;
}

Json componentCatalog() {
    const auto& values = defaults();
    return Json::array({
        {{"name", "Identity"}, {"storage", "entity"}, {"required", true}, {"defaults", {{"name", "Entity"}, {"visible", true}}}},
        {{"name", "Transform"}, {"storage", "entity"}, {"required", true}, {"defaults", {{"position", values.at("position")}, {"rotation", values.at("rotation")}, {"scale", values.at("scale")}, {"parent", "0"}}}},
        {{"name", "MeshRenderer"}, {"storage", "mesh"}, {"required", false}, {"defaults", values.at("mesh")}},
        {{"name", "Light"}, {"storage", "light"}, {"required", false}, {"defaults", values.at("light")}},
        {{"name", "RigidBody"}, {"storage", "body"}, {"required", false}, {"defaults", values.at("body")}},
        {{"name", "Spin"}, {"storage", "spin"}, {"required", false}, {"defaults", values.at("spin")}},
        {{"name", "KeyboardDrive"}, {"storage", "keyboardDrive"}, {"required", false}, {"defaults", values.at("keyboardDrive")}},
        {{"name", "BehaviorGraph"}, {"storage", "behavior"}, {"required", false}, {"defaults", values.at("behavior")}}
    });
}

Json describeScene(const Scene& scene, std::size_t offset, std::size_t limit) {
    if (limit == 0 || limit > 1000) { throw Error("invalid_arguments", "Entity page size must be between 1 and 1000."); }
    auto document = scene.toJson();
    const auto entities = std::move(document.at("entities"));
    document["entities"] = Json::array();
    for (std::size_t index = std::min(offset, entities.size()); index < entities.size() && index - offset < limit; ++index) {
        auto entity = entities[index];
        entity["id"] = std::to_string(entity.at("id").get<EntityId>());
        entity["parent"] = std::to_string(entity.at("parent").get<EntityId>());
        document["entities"].push_back(std::move(entity));
    }
    document["revision"] = std::to_string(scene.revision());
    document["total_entities"] = entities.size();
    document["offset"] = offset;
    return document;
}

SceneEdit prepareSceneEdit(const Scene& scene, const Json& request) {
    keys(request, {"operations", "expected_revision", "label", "dry_run"});
    if (request.contains("expected_revision") && identifier(request.at("expected_revision"), true) != scene.revision()) {
        throw Error("revision_conflict", "The scene changed after it was read. Read the latest revision and retry.");
    }
    const auto& operations = request.at("operations");
    if (!operations.is_array() || operations.empty() || operations.size() > 256) { throw Error("invalid_arguments", "A transaction requires 1-256 operations."); }
    const auto label = request.value("label", std::string("Automation edit"));
    if (label.empty() || label.size() > 128) { throw Error("invalid_arguments", "Undo label must contain 1-128 bytes."); }
    SceneEdit edit;
    edit.before = scene.serialize();
    auto document = scene.toJson();
    EntityId next = 1;
    for (const auto& entity : document.at("entities")) { next = std::max(next, entity.at("id").get<EntityId>() + 1); }
    std::unordered_map<std::string, EntityId> aliases;
    const auto resolve = [&](const Json& value, bool allowRoot = false) {
        if (value.is_string() && value.get_ref<const std::string&>().starts_with("@")) {
            const auto found = aliases.find(value.get<std::string>().substr(1));
            if (found == aliases.end()) { throw Error("invalid_identifier", "Unknown transaction alias."); }
            return found->second;
        }
        return identifier(value, allowRoot);
    };
    for (const auto& operation : operations) {
        const auto kind = operation.at("op").get<std::string>();
        EntityId affected = 0;
        if (kind == "create") {
            keys(operation, {"op", "name", "primitive", "fields", "as"});
            affected = next++;
            auto entity = defaults();
            for (const auto* component : {"mesh", "light", "body", "spin", "keyboardDrive", "behavior"}) { entity.erase(component); }
            entity["id"] = affected;
            entity["name"] = operation.value("name", std::string("Entity"));
            if (operation.contains("primitive")) {
                const auto mesh = operation.at("primitive").get<std::string>();
                assetReference(mesh, false);
                entity["mesh"] = defaults().at("mesh");
                entity["mesh"]["asset"] = mesh;
                entity["mesh"]["unlit"] = mesh == "quad";
            }
            if (operation.contains("fields")) { patchEntity(entity, operation.at("fields")); }
            if (operation.contains("as")) {
                const auto alias = operation.at("as").get<std::string>();
                if (alias.empty() || alias.size() > 64 || aliases.contains(alias)) { throw Error("invalid_arguments", "Transaction aliases must be unique and contain 1-64 bytes."); }
                aliases.emplace(alias, affected);
            }
            document["entities"].push_back(std::move(entity));
        } else if (kind == "patch") {
            keys(operation, {"op", "entity", "fields"});
            affected = resolve(operation.at("entity"));
            patchEntity(findEntity(document, affected), operation.at("fields"));
        } else if (kind == "settings") {
            keys(operation, {"op", "fields"});
            const auto& fields = operation.at("fields");
            keys(fields, {"name", "ambient", "shadows", "rayTracedShadows", "mode", "variables"});
            for (const auto& field : fields.items()) {
                if (field.key() == "variables") {
                    if (!field.value().is_object()) { throw Error("invalid_arguments", "Variable changes must be an object."); }
                    document["variables"].merge_patch(field.value());
                } else { document[field.key()] = field.value(); }
            }
        } else if (kind == "delete" || kind == "duplicate" || kind == "reparent" || kind == "transform") {
            affected = resolve(operation.at("entity"));
            static_cast<void>(findEntity(document, affected));
            auto working = validated(document);
            if (kind == "delete") { keys(operation, {"op", "entity"}); working.destroy(affected); }
            if (kind == "duplicate") {
                keys(operation, {"op", "entity"});
                affected = working.duplicate(affected);
                next = std::max(next, affected + 1);
            }
            if (kind == "reparent") {
                keys(operation, {"op", "entity", "parent", "preserve_world"});
                if (!working.reparent(affected, resolve(operation.at("parent"), true), operation.value("preserve_world", true))) {
                    throw Error("invalid_hierarchy", "Reparenting would create an invalid hierarchy or transform.");
                }
            }
            if (kind == "transform") { transform(working, affected, operation); }
            document = working.toJson();
        } else { throw Error("unknown_operation", "Unsupported scene operation: " + kind); }
        edit.results.push_back({{"op", kind}, {"entity", std::to_string(affected)}});
    }
    edit.candidate = validated(document);
    edit.after = edit.candidate.serialize();
    if (edit.after.size() > 8 * 1024 * 1024) { throw Error("scene_budget", "The serialized scene exceeds the 8 MB limit."); }
    return edit;
}

Json executeSceneEdit(Scene& scene, History& history, const Json& request, const std::function<void(const Scene&)>& prepareAssets) {
    if (history.pending()) { throw Error("editor_busy", "Finish the active editor gesture before submitting an automation edit."); }
    auto edit = prepareSceneEdit(scene, request);
    const bool changed = edit.before != edit.after;
    if (changed && edit.before.size() + edit.after.size() > history.budgetBytes()) { throw Error("history_budget", "The transaction exceeds the undo budget; no changes were applied."); }
    const bool dryRun = request.value("dry_run", false);
    if (changed && !dryRun) {
        if (prepareAssets) { prepareAssets(edit.candidate); }
        history.begin(scene);
        scene = std::move(edit.candidate);
        history.commit(scene, request.value("label", std::string("Automation edit")));
    }
    return {{"revision", std::to_string(scene.revision())}, {"changed", changed}, {"dry_run", dryRun}, {"results", std::move(edit.results)}};
}

}