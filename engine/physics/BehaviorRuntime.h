#pragma once

#include "physics/PhysicsWorld.h"
#include <functional>
#include <set>
#include <map>

namespace velos {

class BehaviorRuntime {
public:
    void start(Scene& scene, PhysicsWorld& physics);
    void stop();
    void step(Scene& scene, PhysicsWorld& physics, float seconds, const std::function<bool(int)>& keyDown = {});
    [[nodiscard]] nlohmann::json state() const;
private:
    struct Program { EntityId owner; BehaviorGraph graph; std::map<std::uint32_t,std::size_t> indexes; };
    std::vector<Program> programs_;
    std::set<int> previousKeys_;
    std::map<std::pair<EntityId,std::uint32_t>,std::uint64_t> executed_;
    std::uint64_t tick_ = 0;
    std::size_t budget_ = 0;
    void execute(const Program& program, std::uint32_t entry, Scene& scene, PhysicsWorld& physics, float seconds);
};

}