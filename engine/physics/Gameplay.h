#pragma once

#include "physics/PhysicsWorld.h"
#include "render/Camera.h"

namespace velos {

inline void driveBodies(Scene& scene, PhysicsWorld& physics, const Camera& camera, float horizontal, float vertical) {
    using namespace DirectX;
    const auto inverseView = XMMatrixInverse(nullptr, camera.view());
    auto forward = XMVectorNegate(inverseView.r[2]);
    forward = XMVectorSetY(forward, 0);
    auto right = XMVectorSetY(inverseView.r[0], 0);
    forward = XMVector3Normalize(forward);
    right = XMVector3Normalize(right);
    auto direction = XMVectorAdd(XMVectorScale(right, horizontal), XMVectorScale(forward, vertical));
    if (XMVectorGetX(XMVector3LengthSq(direction)) > 1) { direction = XMVector3Normalize(direction); }
    for (const auto id : scene.entities()) {
        if (const auto* drive = scene.get<KeyboardDrive>(id)) {
            const auto velocity = XMVectorScale(direction, drive->speed);
            physics.setPlanarVelocity(id, XMVectorGetX(velocity), XMVectorGetZ(velocity));
        }
    }
}

}