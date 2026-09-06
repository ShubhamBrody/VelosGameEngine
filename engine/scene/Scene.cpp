#include "scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>

namespace velos {
using namespace DirectX;
using Json = nlohmann::json;

namespace {

template<std::size_t Count>
std::array<float, Count> values(const Json& source) {
    if (!source.is_array() || source.size() != Count) {
        throw std::runtime_error("Invalid vector size in scene data.");
    }
    std::array<float, Count> result{};
    for (std::size_t index = 0; index < Count; ++index) {
        if (!source[index].is_number()) { throw std::runtime_error("Vector values must be numbers."); }
        const double value = source[index].get<double>();
        if (!std::isfinite(value) || std::abs(value) > 1e6) {
            throw std::runtime_error("Scene values must be finite and within supported bounds.");
        }
        result[index] = static_cast<float>(value);
    }
    return result;
}

float bounded(const Json& object, const char* key, float fallback, float minimum, float maximum) {
    const float value = object.value(key, fallback);
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::runtime_error(std::string("Invalid scene property: ") + key);
    }
    return value;
}

EntityId readId(const Json& value, bool allowRoot = false) {
    if (!value.is_number_unsigned()) { throw std::runtime_error("Entity IDs must be unsigned integers."); }
    const auto id = value.get<EntityId>();
    if ((!allowRoot && id == 0) || id >= std::numeric_limits<EntityId>::max() - 1) {
        throw std::runtime_error("Invalid entity ID.");
    }
    return id;
}

}

XMMATRIX Transform::matrix() const {
    return XMMatrixScaling(scale.x, scale.y, scale.z)
        * XMMatrixRotationQuaternion(XMLoadFloat4(&rotation))
        * XMMatrixTranslation(position.x, position.y, position.z);
}

EntityId Scene::createWithId(EntityId id, std::string entityName) {
    if (handles_.contains(id) || id == 0 || order_.size() >= 10000) {
        throw std::runtime_error("Duplicate ID or scene entity limit reached.");
    }
    const auto handle = registry_.create();
    registry_.emplace<Identity>(handle, id, std::move(entityName), true);
    registry_.emplace<Transform>(handle);
    registry_.emplace<WorldCache>(handle);
    handles_.emplace(id, handle);
    order_.push_back(id);
    nextId_ = std::max(nextId_, id + 1);
    touch();
    return id;
}

EntityId Scene::create(std::string entityName) {
    if (entityName.empty()) { entityName = "Entity"; }
    entityName.resize(std::min<std::size_t>(entityName.size(), 128));
    return createWithId(nextId_, std::move(entityName));
}

EntityId Scene::addPrimitive(std::string mesh, std::string entityName) {
    if (entityName.empty()) { entityName = mesh; }
    const auto id = create(std::move(entityName));
    MeshRenderer renderer;
    renderer.mesh = std::move(mesh);
    renderer.unlit = renderer.mesh == "quad";
    set<MeshRenderer>(id, renderer);
    return id;
}

EntityId Scene::duplicate(EntityId id) {
    if (!contains(id)) { return 0; }
    const auto copy = create(get<Identity>(id)->name + " copy");
    set<Transform>(copy, *get<Transform>(id));
    get<Transform>(copy)->position.x += 0.5f;
    if (const auto* value = get<MeshRenderer>(id)) { set<MeshRenderer>(copy, *value); }
    if (const auto* value = get<Light>(id)) { set<Light>(copy, *value); }
    if (const auto* value = get<RigidBody>(id)) { set<RigidBody>(copy, *value); }
    if (const auto* value = get<Spin>(id)) { set<Spin>(copy, *value); }
    if (const auto* value = get<KeyboardDrive>(id)) { set<KeyboardDrive>(copy, *value); }
    return copy;
}

void Scene::destroy(EntityId id) {
    if (!contains(id)) { return; }
    std::vector<EntityId> pending{id};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        for (const auto candidate : order_) {
            if (get<Transform>(candidate)->parent == pending[index]) { pending.push_back(candidate); }
        }
    }
    for (const auto removed : pending) {
        registry_.destroy(handles_.at(removed));
        handles_.erase(removed);
    }
    std::erase_if(order_, [&](EntityId candidate) { return !contains(candidate); });
    touch();
}

bool Scene::contains(EntityId id) const noexcept { return handles_.contains(id); }

XMMATRIX Scene::worldMatrix(EntityId id) const {
    const auto* transform = get<Transform>(id);
    if (!transform) { return XMMatrixIdentity(); }
    const auto* cached = get<WorldCache>(id);
    if (cached->revision != revision_) {
        const auto world = transform->matrix() * worldMatrix(transform->parent);
        XMStoreFloat4x4(&cached->value, world);
        cached->revision = revision_;
    }
    return XMLoadFloat4x4(&cached->value);
}

bool Scene::setLocalMatrix(EntityId id, FXMMATRIX matrix) {
    auto* transform = get<Transform>(id);
    if (!transform) { return false; }
    XMVECTOR scale;
    XMVECTOR rotation;
    XMVECTOR translation;
    if (!XMMatrixDecompose(&scale, &rotation, &translation, matrix)) { return false; }
    XMFLOAT3 nextScale;
    XMStoreFloat3(&nextScale, scale);
    if (std::abs(nextScale.x) < 0.001f || std::abs(nextScale.y) < 0.001f || std::abs(nextScale.z) < 0.001f) {
        return false;
    }
    transform->scale = nextScale;
    XMStoreFloat4(&transform->rotation, XMQuaternionNormalize(rotation));
    XMStoreFloat3(&transform->position, translation);
    touch();
    return true;
}

bool Scene::setWorldMatrix(EntityId id, FXMMATRIX matrix) {
    const auto* transform = get<Transform>(id);
    if (!transform) { return false; }
    XMVECTOR determinant;
    const auto inverse = XMMatrixInverse(&determinant, worldMatrix(transform->parent));
    if (std::abs(XMVectorGetX(determinant)) < 1e-9f) { return false; }
    return setLocalMatrix(id, matrix * inverse);
}

bool Scene::reparent(EntityId id, EntityId parent, bool preserveWorld) {
    if (!contains(id) || (parent != 0 && !contains(parent))) { return false; }
    auto ancestor = parent;
    std::size_t depth = 0;
    while (ancestor != 0) {
        if (ancestor == id || ++depth > 64) { return false; }
        ancestor = get<Transform>(ancestor)->parent;
    }
    for (const auto descendant : order_) {
        auto current = descendant;
        std::size_t levels = 0;
        while (current != 0) {
            if (++levels > 65) { return false; }
            current = current == id ? parent : get<Transform>(current)->parent;
        }
    }
    const auto original = *get<Transform>(id);
    const auto world = worldMatrix(id);
    get<Transform>(id)->parent = parent;
    touch();
    if (preserveWorld && !setWorldMatrix(id, world)) {
        *get<Transform>(id) = original;
        touch();
        return false;
    }
    return true;
}

void Scene::tick(double seconds) {
    auto view = registry_.view<Transform, Spin>();
    for (const auto handle : view) {
        auto& transform = view.get<Transform>(handle);
        const auto& spin = view.get<Spin>(handle);
        const auto delta = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0),
            XMConvertToRadians(spin.degreesPerSecond * static_cast<float>(seconds)));
        XMStoreFloat4(&transform.rotation, XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&transform.rotation), delta)));
    }
    if (view.begin() != view.end()) { touch(); }
}

Json Scene::entityJson(EntityId id) const {
    const auto& identity = *get<Identity>(id);
    const auto& transform = *get<Transform>(id);
    Json result{
        {"id", id}, {"name", identity.name}, {"visible", identity.visible}, {"parent", transform.parent},
        {"position", {transform.position.x, transform.position.y, transform.position.z}},
        {"rotation", {transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w}},
        {"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}
    };
    if (const auto* mesh = get<MeshRenderer>(id)) {
        result["mesh"] = {{"asset", mesh->mesh}, {"color", {mesh->color.x, mesh->color.y, mesh->color.z, mesh->color.w}},
            {"roughness", mesh->roughness}, {"metallic", mesh->metallic}, {"shadow", mesh->castShadow}, {"unlit", mesh->unlit}};
        auto& material = result["mesh"];
        material["textures"] = mesh->textures;
        material["uvScale"] = {mesh->uvScale.x, mesh->uvScale.y};
        material["uvOffset"] = {mesh->uvOffset.x, mesh->uvOffset.y};
        material["emission"] = {mesh->emission.x, mesh->emission.y, mesh->emission.z};
        material["emissionStrength"] = mesh->emissionStrength;
        material["normalStrength"] = mesh->normalStrength;
        material["alphaCutoff"] = mesh->alphaCutoff;
        material["surface"] = static_cast<std::uint32_t>(mesh->surface);
        material["doubleSided"] = mesh->doubleSided;
    }
    if (const auto* light = get<Light>(id)) {
        result["light"] = {{"kind", light->kind == LightKind::Directional ? "directional" : "point"},
            {"color", {light->color.x, light->color.y, light->color.z}},
            {"direction", {light->direction.x, light->direction.y, light->direction.z}},
            {"intensity", light->intensity}, {"range", light->range}};
    }
    if (const auto* body = get<RigidBody>(id)) {
        result["body"] = {{"motion", body->motion == BodyMotion::Static ? "static" : "dynamic"},
            {"mass", body->mass}, {"restitution", body->restitution}};
    }
    if (const auto* spin = get<Spin>(id)) { result["spin"] = spin->degreesPerSecond; }
    if (const auto* drive = get<KeyboardDrive>(id)) { result["keyboardDrive"] = drive->speed; }
    return result;
}

Json Scene::toJson() const {
    Json result{{"schema", 1}, {"name", name}, {"ambient", ambient}, {"shadows", shadows},
        {"mode", twoDimensional ? "2d" : "3d"}, {"entities", Json::array()}};
    for (const auto id : order_) { result["entities"].push_back(entityJson(id)); }
    return result;
}

std::string Scene::serialize() const { return toJson().dump(2); }

std::vector<std::string> Scene::assetReferences() const {
    std::set<std::string> references;
    for (const auto id : order_) {
        const auto* mesh = get<MeshRenderer>(id);
        if (!mesh) { continue; }
        if (mesh->mesh != "cube" && mesh->mesh != "sphere" && mesh->mesh != "plane" && mesh->mesh != "quad") {
            references.insert(mesh->mesh);
        }
        for (const auto& texture : mesh->textures) { if (!texture.empty()) { references.insert(texture); } }
    }
    return {references.begin(), references.end()};
}

bool Scene::deserialize(std::string_view text, std::string& error) {
    try {
        if (text.size() > 8 * 1024 * 1024) { throw std::runtime_error("Scene files are limited to 8 MB."); }
        bool tooDeep = false;
        const auto root = Json::parse(text.begin(), text.end(), [&](int depth, Json::parse_event_t, Json&) {
            if (depth > 32) { tooDeep = true; }
            return !tooDeep;
        }, false);
        if (tooDeep || root.is_discarded() || !root.is_object() || root.value("schema", 0) != 1) {
            throw std::runtime_error("Invalid or unsupported scene schema.");
        }
        Scene candidate;
        candidate.name = root.value("name", std::string("Untitled"));
        if (candidate.name.empty() || candidate.name.size() > 128) { throw std::runtime_error("Invalid scene name."); }
        candidate.ambient = bounded(root, "ambient", 0.32f, 0.0f, 4.0f);
        candidate.shadows = root.value("shadows", true);
        const auto mode = root.value("mode", std::string("3d"));
        if (mode != "3d" && mode != "2d") { throw std::runtime_error("Unknown scene view mode."); }
        candidate.twoDimensional = mode == "2d";
        const auto& entities = root.at("entities");
        if (!entities.is_array() || entities.size() > 10000) { throw std::runtime_error("Invalid entity list."); }
        for (const auto& entity : entities) {
            const auto id = readId(entity.at("id"));
            const auto entityName = entity.at("name").get<std::string>();
            if (entityName.empty() || entityName.size() > 128) { throw std::runtime_error("Invalid entity name."); }
            candidate.createWithId(id, entityName);
            candidate.get<Identity>(id)->visible = entity.value("visible", true);
            auto& transform = *candidate.get<Transform>(id);
            transform.parent = readId(entity.at("parent"), true);
            const auto position = values<3>(entity.at("position"));
            const auto rotation = values<4>(entity.at("rotation"));
            const auto scale = values<3>(entity.at("scale"));
            transform.position = {position[0], position[1], position[2]};
            transform.rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
            const float length = XMVectorGetX(XMVector4LengthSq(XMLoadFloat4(&transform.rotation)));
            if (length < 1e-8f) { throw std::runtime_error("Zero rotation quaternion."); }
            if (std::abs(length - 1.0f) > 1e-5f) {
                XMStoreFloat4(&transform.rotation, XMQuaternionNormalize(XMLoadFloat4(&transform.rotation)));
            }
            for (const auto value : scale) {
                if (std::abs(value) < 0.001f || std::abs(value) > 1000.0f) { throw std::runtime_error("Invalid transform scale."); }
            }
            transform.scale = {scale[0], scale[1], scale[2]};
            if (entity.contains("mesh")) {
                const auto& source = entity.at("mesh");
                MeshRenderer mesh;
                mesh.mesh = source.at("asset").get<std::string>();
                if (mesh.mesh.empty() || mesh.mesh.size() > 1024) { throw std::runtime_error("Invalid mesh asset reference."); }
                const auto color = values<4>(source.at("color"));
                for (const float channel : color) {
                    if (channel < 0 || channel > 1) { throw std::runtime_error("Material color must be in [0,1]."); }
                }
                mesh.color = {color[0], color[1], color[2], color[3]};
                mesh.roughness = bounded(source, "roughness", 0.45f, 0.04f, 1.0f);
                mesh.metallic = bounded(source, "metallic", 0.0f, 0.0f, 1.0f);
                mesh.castShadow = source.value("shadow", true);
                mesh.unlit = source.value("unlit", false);
                if (source.contains("textures")) {
                    const auto& textures = source.at("textures");
                    if (!textures.is_array() || textures.size() != mesh.textures.size()) { throw std::runtime_error("A material must have four texture slots."); }
                    for (std::size_t slot = 0; slot < mesh.textures.size(); ++slot) {
                        mesh.textures[slot] = textures[slot].get<std::string>();
                        const auto& path = mesh.textures[slot];
                        if (path.empty()) { continue; }
                        if (path.size() > 1024 || !path.starts_with("Assets/") || path.find("..") != std::string::npos
                            || path.find_first_of(":\\") != std::string::npos || path.find('\0') != std::string::npos) {
                            throw std::runtime_error("Texture references must be safe project-relative Assets paths.");
                        }
                    }
                }
                if (source.contains("uvScale")) {
                    const auto scaleValues = values<2>(source.at("uvScale"));
                    mesh.uvScale = {scaleValues[0], scaleValues[1]};
                }
                if (source.contains("uvOffset")) {
                    const auto offset = values<2>(source.at("uvOffset"));
                    mesh.uvOffset = {offset[0], offset[1]};
                }
                if (source.contains("emission")) {
                    const auto emission = values<3>(source.at("emission"));
                    for (const auto channel : emission) {
                        if (channel < 0 || channel > 1) { throw std::runtime_error("Emission color must be in [0,1]."); }
                    }
                    mesh.emission = {emission[0], emission[1], emission[2]};
                }
                mesh.emissionStrength = bounded(source, "emissionStrength", 0, 0, 100);
                mesh.normalStrength = bounded(source, "normalStrength", 1, 0, 4);
                mesh.alphaCutoff = bounded(source, "alphaCutoff", 0.5f, 0, 1);
                const auto surface = source.value("surface", 0);
                if (surface < 0 || surface > 2) { throw std::runtime_error("Unsupported material surface mode."); }
                mesh.surface = static_cast<SurfaceMode>(surface);
                mesh.doubleSided = source.value("doubleSided", true);
                candidate.set<MeshRenderer>(id, mesh);
            }
            if (entity.contains("light")) {
                const auto& source = entity.at("light");
                Light light;
                const auto kind = source.at("kind").get<std::string>();
                if (kind != "directional" && kind != "point") { throw std::runtime_error("Unknown light type."); }
                light.kind = kind == "directional" ? LightKind::Directional : LightKind::Point;
                const auto color = values<3>(source.at("color"));
                const auto direction = values<3>(source.at("direction"));
                light.color = {color[0], color[1], color[2]};
                light.direction = {direction[0], direction[1], direction[2]};
                if (XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&light.direction))) < 1e-8f) { throw std::runtime_error("Light direction cannot be zero."); }
                light.intensity = bounded(source, "intensity", 3, 0, 1000);
                light.range = bounded(source, "range", 12, 0.1f, 10000);
                candidate.set<Light>(id, light);
            }
            if (entity.contains("body")) {
                const auto& source = entity.at("body");
                RigidBody body;
                const auto motion = source.at("motion").get<std::string>();
                if (motion != "static" && motion != "dynamic") { throw std::runtime_error("Unknown body motion."); }
                body.motion = motion == "static" ? BodyMotion::Static : BodyMotion::Dynamic;
                body.mass = bounded(source, "mass", 1, 0.01f, 100000);
                body.restitution = bounded(source, "restitution", 0.15f, 0, 1);
                candidate.set<RigidBody>(id, body);
            }
            if (entity.contains("spin")) {
                candidate.set<Spin>(id, {bounded(entity, "spin", 30, -3600, 3600)});
            }
            if (entity.contains("keyboardDrive")) {
                candidate.set<KeyboardDrive>(id, {bounded(entity, "keyboardDrive", 4, 0.1f, 100)});
            }
        }
        for (const auto id : candidate.order_) {
            auto ancestor = candidate.get<Transform>(id)->parent;
            std::size_t depth = 0;
            while (ancestor != 0) {
                if (!candidate.contains(ancestor) || ancestor == id || ++depth > 64) {
                    throw std::runtime_error("Invalid, cyclic or excessively deep scene hierarchy.");
                }
                ancestor = candidate.get<Transform>(ancestor)->parent;
            }
        }
        candidate.revision_ = revision_ + 1;
        *this = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

Scene Scene::demo() {
    Scene scene;
    scene.name = "Workshop";
    const auto floor = scene.addPrimitive("plane", "Ground");
    scene.get<Transform>(floor)->scale = {20, 1, 20};
    scene.get<MeshRenderer>(floor)->color = {0.27f, 0.29f, 0.30f, 1};
    scene.get<MeshRenderer>(floor)->roughness = 0.9f;
    scene.set<RigidBody>(floor, {BodyMotion::Static});
    const auto cube = scene.addPrimitive("cube", "Mint block");
    scene.get<Transform>(cube)->position = {-1.6f, 1.2f, 0};
    scene.get<Transform>(cube)->scale = {1.7f, 1.7f, 1.7f};
    XMStoreFloat4(&scene.get<Transform>(cube)->rotation, XMQuaternionRotationRollPitchYaw(0, 0.45f, 0));
    scene.set<RigidBody>(cube);
    const auto sphere = scene.addPrimitive("sphere", "Copper sphere");
    scene.get<Transform>(sphere)->position = {1.25f, 0.92f, 0.4f};
    scene.get<Transform>(sphere)->scale = {1.8f, 1.8f, 1.8f};
    scene.get<MeshRenderer>(sphere)->color = {0.95f, 0.43f, 0.28f, 1};
    scene.get<MeshRenderer>(sphere)->roughness = 0.25f;
    scene.get<MeshRenderer>(sphere)->metallic = 0.55f;
    scene.set<RigidBody>(sphere);
    scene.set<KeyboardDrive>(sphere);
    const auto pedestal = scene.addPrimitive("cube", "Plinth");
    scene.get<Transform>(pedestal)->position = {0, 0.3f, -2.2f};
    scene.get<Transform>(pedestal)->scale = {2.2f, 0.6f, 1.8f};
    scene.get<MeshRenderer>(pedestal)->color = {0.62f, 0.65f, 0.68f, 1};
    scene.set<RigidBody>(pedestal, {BodyMotion::Static});
    const auto spinner = scene.addPrimitive("cube", "Rotating study");
    scene.get<Transform>(spinner)->position = {0, 1.35f, -2.2f};
    scene.get<Transform>(spinner)->scale = {1, 1, 1};
    scene.get<MeshRenderer>(spinner)->color = {0.91f, 0.76f, 0.28f, 1};
    scene.set<Spin>(spinner, {28});
    const auto sun = scene.create("Sun");
    scene.set<Light>(sun, {LightKind::Directional});
    scene.get<Transform>(sun)->position = {-4, 7, 3};
    const auto fill = scene.create("Cool fill");
    Light point;
    point.color = {0.4f, 0.72f, 1.0f};
    point.intensity = 18;
    scene.set<Light>(fill, point);
    scene.get<Transform>(fill)->position = {3, 3, -2};
    scene.touch();
    return scene;
}

}