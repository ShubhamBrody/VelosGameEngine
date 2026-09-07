#include "physics/BehaviorRuntime.h"

#include <iostream>
#include <cmath>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
template<class Function> void rejects(Function action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected,message);
}
}

int main() {
    try {
        velos::Scene scene;
        scene.variables = {{"speed",2},{"score",0}};
        const auto entity = scene.addPrimitive("cube","Graph controlled");
        Json graph{{"nodes",Json::array({
            {{"id",1},{"kind","tick"}}, {{"id",2},{"kind","translate"},{"vector",{1,0,0}},{"variable","speed"}},
            {{"id",3},{"kind","key_pressed"},{"key","Space"}}, {{"id",4},{"kind","add_variable"},{"variable","score"},{"value",1}},
            {{"id",5},{"kind","branch"},{"variable","score"},{"comparison","greater_equal"},{"value",2}},
            {{"id",6},{"kind","set_color"},{"vector",{1,0,0}}}, {{"id",7},{"kind","begin_play"}},
            {{"id",8},{"kind","set_visible"},{"visible",true}}
        })}, {"links",Json::array({{{"from",1},{"to",2}},{{"from",3},{"to",4}},{{"from",4},{"to",5}},{{"from",5},{"to",6},{"output","true"}},{{"from",7},{"to",8}}})}};
        scene.set<velos::BehaviorGraph>(entity,velos::parseBehaviorGraph(graph));
        std::string error;
        velos::Scene loaded;
        require(loaded.deserialize(scene.serialize(),error) && loaded.get<velos::BehaviorGraph>(entity)->nodes.size() == 8, "Graphs and variables must round trip.");
        velos::PhysicsWorld physics;
        physics.start(scene);
        velos::BehaviorRuntime runtime;
        runtime.start(scene,physics);
        for (int index = 0; index < 60; ++index) { runtime.step(scene,physics,1.0f/60); }
        require(std::abs(scene.get<velos::Transform>(entity)->position.x - 2) < 0.0001f, "Tick graph must move at variable-driven units per second.");
        runtime.step(scene,physics,1.0f/60,[](int key) { return key == 32; });
        runtime.step(scene,physics,1.0f/60,[](int key) { return key == 32; });
        require(scene.variables.at("score") == 1, "Key-pressed events must be edge-triggered.");
        runtime.step(scene,physics,1.0f/60);
        runtime.step(scene,physics,1.0f/60,[](int key) { return key == 32; });
        require(scene.variables.at("score") == 2 && scene.get<velos::MeshRenderer>(entity)->color.x == 1 && scene.get<velos::MeshRenderer>(entity)->color.y == 0, "Branch nodes must execute only the selected output.");
        require(runtime.state().at("programs") == 1 && runtime.state().at("executed").size() >= 6, "Graph execution must be observable.");
        auto cyclic = graph;
        cyclic["links"].push_back({{"from",2},{"to",2}});
        rejects([&] { velos::parseBehaviorGraph(cyclic); }, "Cyclic graphs must fail.");
        auto duplicate = graph;
        duplicate["links"].push_back({{"from",1},{"to",6}});
        rejects([&] { velos::parseBehaviorGraph(duplicate); }, "An output cannot have ambiguous duplicate links.");
        auto invalid = scene.toJson();
        invalid["entities"][0]["behavior"]["nodes"][1]["variable"] = "missing";
        require(!loaded.deserialize(invalid.dump(),error), "Unknown variable references must fail scene validation.");
        invalid = scene.toJson();
        invalid["entities"][0]["body"] = {{"motion","dynamic"}};
        require(!loaded.deserialize(invalid.dump(),error), "Graph transforms must not override physics-owned transforms.");
        const auto copy = scene.duplicate(entity);
        require(scene.get<velos::BehaviorGraph>(copy) != nullptr, "Entity duplication must preserve behavior graphs.");
        runtime.stop();
        require(runtime.state().at("programs") == 0, "Stop must release graph runtime state.");
        std::cout << "PASS: executable graphs, variables, events, branches, physics ownership, serialization and execution limits.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}