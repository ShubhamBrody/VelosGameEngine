#pragma once

#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace velos {

using EntityId = std::uint64_t;

struct Identity {
    EntityId id = 0;
    std::string name;
    bool visible = true;
};

struct Transform {
    EntityId parent = 0;
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT4 rotation{0, 0, 0, 1};
    DirectX::XMFLOAT3 scale{1, 1, 1};
    [[nodiscard]] DirectX::XMMATRIX matrix() const;
};

struct MeshRenderer {
    std::string mesh = "cube";
    DirectX::XMFLOAT4 color{0.24f, 0.73f, 0.64f, 1.0f};
    float roughness = 0.45f;
    float metallic = 0.0f;
    bool castShadow = true;
    bool unlit = false;
};

enum class LightKind { Directional, Point };
struct Light {
    LightKind kind = LightKind::Point;
    DirectX::XMFLOAT3 color{1, 0.96f, 0.88f};
    DirectX::XMFLOAT3 direction{-0.5f, -1, -0.3f};
    float intensity = 3.0f;
    float range = 12.0f;
};

enum class BodyMotion { Static, Dynamic };
struct RigidBody {
    BodyMotion motion = BodyMotion::Dynamic;
    float mass = 1.0f;
    float restitution = 0.15f;
};

struct Spin {
    float degreesPerSecond = 30.0f;
};

class Scene {
public:
    Scene() = default;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    std::string name = "Untitled";
    float ambient = 0.32f;
    bool shadows = true;

    EntityId create(std::string name);
    EntityId addPrimitive(std::string mesh, std::string name = {});
    EntityId duplicate(EntityId id);
    void destroy(EntityId id);
    [[nodiscard]] bool contains(EntityId id) const noexcept;
    [[nodiscard]] const std::vector<EntityId>& entities() const noexcept { return order_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    void touch() noexcept { ++revision_; }

    template<class Component> Component* get(EntityId id) {
        const auto found = handles_.find(id);
        return found == handles_.end() ? nullptr : registry_.try_get<Component>(found->second);
    }
    template<class Component> const Component* get(EntityId id) const {
        const auto found = handles_.find(id);
        return found == handles_.end() ? nullptr : registry_.try_get<Component>(found->second);
    }
    template<class Component> Component& set(EntityId id, const Component& value = {}) {
        touch();
        return registry_.emplace_or_replace<Component>(handles_.at(id), value);
    }
    template<class Component> void remove(EntityId id) {
        if (contains(id)) {
            registry_.remove<Component>(handles_.at(id));
            touch();
        }
    }

    [[nodiscard]] DirectX::XMMATRIX worldMatrix(EntityId id) const;
    bool setLocalMatrix(EntityId id, DirectX::FXMMATRIX matrix);
    bool setWorldMatrix(EntityId id, DirectX::FXMMATRIX matrix);
    bool reparent(EntityId id, EntityId parent, bool preserveWorld = true);
    void tick(double seconds);

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] std::string serialize() const;
    bool deserialize(std::string_view text, std::string& error);
    [[nodiscard]] static Scene demo();

private:
    struct WorldCache {
        mutable DirectX::XMFLOAT4X4 value{};
        mutable std::uint64_t revision = 0;
    };
    entt::registry registry_;
    std::unordered_map<EntityId, entt::entity> handles_;
    std::vector<EntityId> order_;
    EntityId nextId_ = 1;
    std::uint64_t revision_ = 1;
    EntityId createWithId(EntityId id, std::string name);
    [[nodiscard]] nlohmann::json entityJson(EntityId id) const;
};

}