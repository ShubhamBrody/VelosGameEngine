#include "physics/PhysicsWorld.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace velos {
namespace {

class JoltLifetime {
public:
    JoltLifetime() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltLifetime() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return static_cast<JPH::BroadPhaseLayer::Type>(layer) == 0 ? "Static" : "Dynamic";
    }
#endif
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        return first != 0 || second != 0;
    }
};

class BroadPairs final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override {
        return object != 0 || static_cast<JPH::BroadPhaseLayer::Type>(broad) != 0;
    }
};

}

struct PhysicsWorld::Impl {
    BroadPhaseLayers layers;
    ObjectPairs objectPairs;
    BroadPairs broadPairs;
    JPH::TempAllocatorImpl temporary{16 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u))};
    JPH::PhysicsSystem physics;
    struct Record { EntityId entity; JPH::BodyID body; bool dynamic; };
    std::vector<Record> bodies;

    Impl() {
        physics.Init(2048, 0, 8192, 8192, layers, broadPairs, objectPairs);
        bodies.reserve(2048);
    }
    ~Impl() {
        auto& bodyInterface = physics.GetBodyInterface();
        for (const auto& record : bodies) {
            bodyInterface.RemoveBody(record.body);
            bodyInterface.DestroyBody(record.body);
        }
    }
};

PhysicsWorld::PhysicsWorld() {
    static const JoltLifetime lifetime;
    static_cast<void>(lifetime);
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::start(const Scene& scene) {
    auto candidate = std::make_unique<Impl>();
    auto& bodies = candidate->physics.GetBodyInterface();
    for (const auto id : scene.entities()) {
        const auto* settings = scene.get<RigidBody>(id);
        const auto* mesh = scene.get<MeshRenderer>(id);
        if (!settings || !mesh) { continue; }
        const auto& transform = *scene.get<Transform>(id);
        if (transform.parent != 0) { throw std::runtime_error("Preview rigid bodies must be scene roots: " + scene.get<Identity>(id)->name); }
        if (mesh->mesh != "cube" && mesh->mesh != "sphere" && mesh->mesh != "plane") {
            throw std::runtime_error("Preview colliders support cube, sphere and plane primitives only.");
        }
        if (candidate->bodies.size() >= 2048) { throw std::runtime_error("Physics body budget exceeded (2048)."); }
        const auto scale = transform.scale;
        if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0) { throw std::runtime_error("Physics requires positive primitive scale."); }
        if (mesh->mesh == "sphere" && (std::abs(scale.x - scale.y) > 0.001f || std::abs(scale.x - scale.z) > 0.001f)) {
            throw std::runtime_error("Sphere colliders require uniform scale in this preview.");
        }
        const bool dynamic = settings->motion == BodyMotion::Dynamic;
        if (dynamic && scene.get<Spin>(id)) { throw std::runtime_error("Remove rotation behavior from dynamic rigid bodies; physics owns their rotation."); }
        if (!dynamic && scene.get<KeyboardDrive>(id)) { throw std::runtime_error("Keyboard drive requires a dynamic rigid body."); }
        if (mesh->mesh == "plane" && dynamic) { throw std::runtime_error("Plane colliders must be static."); }
        const JPH::Quat rotation(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w);
        JPH::RVec3 position(transform.position.x, transform.position.y, transform.position.z);
        JPH::ShapeRefC shape;
        if (mesh->mesh == "sphere") {
            shape = new JPH::SphereShape(scale.x * 0.5f);
        } else {
            const float halfHeight = mesh->mesh == "plane" ? 0.05f : scale.y * 0.5f;
            const float radius = std::min({0.02f, scale.x * 0.125f, halfHeight * 0.25f, scale.z * 0.125f});
            shape = new JPH::BoxShape(JPH::Vec3(scale.x * 0.5f, halfHeight, scale.z * 0.5f), radius);
            if (mesh->mesh == "plane") { position -= rotation * JPH::Vec3(0, 0.05f, 0); }
        }
        JPH::BodyCreationSettings creation(shape, position, rotation,
            dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, dynamic ? 1 : 0);
        creation.mRestitution = settings->restitution;
        creation.mFriction = 0.65f;
        if (dynamic) {
            creation.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            creation.mMassPropertiesOverride.mMass = settings->mass;
        }
        const auto body = bodies.CreateAndAddBody(creation, dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (body.IsInvalid()) { throw std::runtime_error("Jolt could not allocate a rigid body."); }
        candidate->bodies.push_back({id, body, dynamic});
    }
    candidate->physics.OptimizeBroadPhase();
    impl_ = std::move(candidate);
}

void PhysicsWorld::stop() { impl_.reset(); }

void PhysicsWorld::step(Scene& scene, float seconds) {
    if (!impl_) { return; }
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 0.1f) { throw std::runtime_error("Physics step must be in (0, 0.1] seconds."); }
    const auto result = impl_->physics.Update(seconds, 1, &impl_->temporary, &impl_->jobs);
    if (result != JPH::EPhysicsUpdateError::None) { throw std::runtime_error("Physics capacity exceeded; reduce active bodies/contacts."); }
    const auto& bodies = impl_->physics.GetBodyInterface();
    bool moved = false;
    for (const auto& record : impl_->bodies) {
        if (!record.dynamic || !scene.contains(record.entity)) { continue; }
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(record.body, position, rotation);
        auto& transform = *scene.get<Transform>(record.entity);
        moved |= transform.position.x != position.GetX() || transform.position.y != position.GetY() || transform.position.z != position.GetZ()
            || transform.rotation.x != rotation.GetX() || transform.rotation.y != rotation.GetY()
            || transform.rotation.z != rotation.GetZ() || transform.rotation.w != rotation.GetW();
        transform.position = {position.GetX(), position.GetY(), position.GetZ()};
        transform.rotation = {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()};
    }
    if (moved) { scene.touch(); }
}

bool PhysicsWorld::setPlanarVelocity(EntityId id, float horizontal, float forward) {
    if (!impl_ || !std::isfinite(horizontal) || !std::isfinite(forward)) { return false; }
    auto& bodies = impl_->physics.GetBodyInterface();
    for (const auto& record : impl_->bodies) {
        if (record.entity == id && record.dynamic) {
            const auto current = bodies.GetLinearVelocity(record.body);
            bodies.SetLinearVelocity(record.body, JPH::Vec3(horizontal, current.GetY(), forward));
            bodies.ActivateBody(record.body);
            return true;
        }
    }
    return false;
}

std::size_t PhysicsWorld::bodyCount() const noexcept { return impl_ ? impl_->bodies.size() : 0; }

}