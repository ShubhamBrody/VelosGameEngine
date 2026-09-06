#include "physics/PhysicsWorld.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        velos::Scene scene;
        const auto floor = scene.addPrimitive("plane", "Floor");
        scene.get<velos::Transform>(floor)->scale = {20,1,20};
        scene.set<velos::RigidBody>(floor, {velos::BodyMotion::Static});
        const auto box = scene.addPrimitive("cube", "Falling box");
        scene.get<velos::Transform>(box)->position = {0,3,0};
        scene.set<velos::RigidBody>(box);
        const auto before = scene.serialize();
        velos::PhysicsWorld physics;
        physics.start(scene);
        if (physics.bodyCount() != 2) { throw std::runtime_error("Physics body registration failed."); }
        for (int step = 0; step < 240; ++step) { physics.step(scene, 1.0f / 60); }
        const auto position = scene.get<velos::Transform>(box)->position;
        if (std::abs(position.y - 0.5f) > 0.06f) { throw std::runtime_error("Falling body failed to settle on the floor."); }
        if (!physics.setPlanarVelocity(box, 2, 0)) { throw std::runtime_error("Dynamic velocity control failed."); }
        for (int step = 0; step < 30; ++step) {
            physics.setPlanarVelocity(box, 2, 0);
            physics.step(scene, 1.0f / 60);
        }
        if (scene.get<velos::Transform>(box)->position.x < 0.5f) { throw std::runtime_error("Body movement failed."); }
        physics.stop();
        std::string error;
        if (!scene.deserialize(before, error) || scene.serialize() != before) { throw std::runtime_error("Play snapshot restoration failed."); }
        const auto sphere = scene.addPrimitive("sphere");
        scene.get<velos::Transform>(sphere)->scale = {1,2,1};
        scene.set<velos::RigidBody>(sphere);
        bool rejected = false;
        try { physics.start(scene); } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) { throw std::runtime_error("Unsupported sphere scale must be rejected."); }
        std::cout << "PASS: Jolt body registration, gravity, contacts, velocity control, restore and collider validation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}