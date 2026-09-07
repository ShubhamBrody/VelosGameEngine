#include "physics/BehaviorRuntime.h"

#include <cmath>
#include <stdexcept>

namespace velos {

void BehaviorRuntime::start(Scene& scene, PhysicsWorld& physics) {
    stop();
    Scene validated;
    std::string error;
    if (!validated.deserialize(scene.serialize(), error)) { throw std::runtime_error(error); }
    for (const auto owner : scene.entities()) {
        const auto* graph = scene.get<BehaviorGraph>(owner);
        if (!graph || !graph->enabled) { continue; }
        Program program{owner,*graph,{}};
        for (std::size_t index = 0; index < graph->nodes.size(); ++index) { program.indexes.emplace(graph->nodes[index].id,index); }
        programs_.push_back(std::move(program));
    }
    budget_ = 8192;
    for (const auto& program : programs_) {
        for (const auto& node : program.graph.nodes) { if (node.kind == BehaviorKind::BeginPlay) { execute(program,node.id,scene,physics,0); } }
    }
}

void BehaviorRuntime::stop() { programs_.clear(); previousKeys_.clear(); executed_.clear(); tick_ = 0; }

void BehaviorRuntime::step(Scene& scene, PhysicsWorld& physics, float seconds, const std::function<bool(int)>& keyDown) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 0.25f) { throw std::runtime_error("Invalid behavior timestep."); }
    ++tick_;
    budget_ = 8192;
    std::set<int> pressed;
    for (const auto& program : programs_) {
        for (const auto& node : program.graph.nodes) {
            if (node.kind == BehaviorKind::KeyPressed && keyDown && keyDown(behaviorKey(node.key))) { pressed.insert(behaviorKey(node.key)); }
        }
    }
    for (const auto& program : programs_) {
        for (const auto& node : program.graph.nodes) {
            if (node.kind == BehaviorKind::Tick || (node.kind == BehaviorKind::KeyPressed && pressed.contains(behaviorKey(node.key)) && !previousKeys_.contains(behaviorKey(node.key)))) {
                execute(program,node.id,scene,physics,seconds);
            }
        }
    }
    previousKeys_ = std::move(pressed);
}

void BehaviorRuntime::execute(const Program& program, std::uint32_t entry, Scene& scene, PhysicsWorld& physics, float seconds) {
    using namespace DirectX;
    auto current = entry;
    while (current != 0) {
        if (budget_ == 0) { throw std::runtime_error("Behavior execution budget exceeded (8192 nodes per step)."); }
        --budget_;
        const auto& node = program.graph.nodes[program.indexes.at(current)];
        executed_[{program.owner,node.id}] = tick_;
        auto output = std::string("next");
        const double scalar = node.variable.empty() ? node.value : scene.variables.at(node.variable);
        bool changed = false;
        auto* transform = scene.get<Transform>(program.owner);
        if (!transform) { throw std::runtime_error("A running behavior owner was removed."); }
        if (node.kind == BehaviorKind::Translate) {
            const auto delta = XMVectorScale(XMLoadFloat3(&node.vector), static_cast<float>(scalar * seconds));
            XMFLOAT3 next;
            XMStoreFloat3(&next, XMVectorAdd(XMLoadFloat3(&transform->position), delta));
            if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.z) || std::abs(next.x) > 1e6 || std::abs(next.y) > 1e6 || std::abs(next.z) > 1e6) { throw std::runtime_error("Graph movement exceeded scene bounds."); }
            changed = next.x != transform->position.x || next.y != transform->position.y || next.z != transform->position.z;
            transform->position = next;
        } else if (node.kind == BehaviorKind::Rotate) {
            const auto angles = XMVectorScale(XMLoadFloat3(&node.vector), static_cast<float>(scalar * seconds) * XM_PI / 180);
            XMStoreFloat4(&transform->rotation, XMQuaternionNormalize(XMQuaternionMultiply(XMQuaternionRotationRollPitchYawFromVector(angles),XMLoadFloat4(&transform->rotation))));
            changed = scalar != 0 && seconds != 0;
        } else if (node.kind == BehaviorKind::Velocity) {
            const auto horizontal = node.vector.x * scalar;
            const auto forward = node.vector.z * scalar;
            if (std::abs(horizontal) > 100 || std::abs(forward) > 100 || !physics.setPlanarVelocity(program.owner,static_cast<float>(horizontal),static_cast<float>(forward))) {
                throw std::runtime_error("Graph velocity requires a dynamic body and planar speed in [-100,100].");
            }
        } else if (node.kind == BehaviorKind::SetVariable || node.kind == BehaviorKind::AddVariable) {
            auto& value = scene.variables.at(node.variable);
            const auto next = node.kind == BehaviorKind::SetVariable ? node.value : value + node.value;
            if (!std::isfinite(next) || std::abs(next) > 1e6) { throw std::runtime_error("Graph variable exceeded its numeric budget."); }
            changed = value != next;
            value = next;
        } else if (node.kind == BehaviorKind::Branch) {
            const auto value = scene.variables.at(node.variable);
            const bool condition = node.comparison == "less" ? value < node.value : node.comparison == "less_equal" ? value <= node.value
                : node.comparison == "equal" ? value == node.value : node.comparison == "not_equal" ? value != node.value
                : node.comparison == "greater_equal" ? value >= node.value : value > node.value;
            output = condition ? "true" : "false";
        } else if (node.kind == BehaviorKind::SetVisible) {
            auto& visible = scene.get<Identity>(program.owner)->visible;
            changed = visible != node.visible;
            visible = node.visible;
        } else if (node.kind == BehaviorKind::SetColor) {
            auto* mesh = scene.get<MeshRenderer>(program.owner);
            if (!mesh) { throw std::runtime_error("Graph color target has no material."); }
            changed = mesh->color.x != node.vector.x || mesh->color.y != node.vector.y || mesh->color.z != node.vector.z;
            mesh->color = {node.vector.x,node.vector.y,node.vector.z,mesh->color.w};
        }
        if (changed) { scene.touch(); }
        current = 0;
        for (const auto& link : program.graph.links) { if (link.from == node.id && link.output == output) { current = link.to; break; } }
    }
}

nlohmann::json BehaviorRuntime::state() const {
    auto executed = nlohmann::json::array();
    for (const auto& [node, tick] : executed_) { executed.push_back({{"entity",std::to_string(node.first)}, {"node",node.second}, {"last_tick",tick}}); }
    return {{"tick",tick_}, {"programs",programs_.size()}, {"executed",std::move(executed)}};
}

}