#pragma once

#include "scene/Scene.h"

#include <memory>

namespace velos {

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    void start(const Scene& scene);
    void stop();
    void step(Scene& scene, float seconds);
    bool setPlanarVelocity(EntityId id, float horizontal, float forward);
    [[nodiscard]] std::size_t bodyCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}