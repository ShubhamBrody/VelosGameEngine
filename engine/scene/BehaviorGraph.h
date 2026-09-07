#pragma once

#include <DirectXMath.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace velos {

enum class BehaviorKind { BeginPlay, Tick, KeyPressed, Translate, Rotate, Velocity, SetVariable, AddVariable, Branch, SetVisible, SetColor };

struct BehaviorNode {
    std::uint32_t id = 1;
    BehaviorKind kind = BehaviorKind::Tick;
    DirectX::XMFLOAT2 position{};
    DirectX::XMFLOAT3 vector{0,1,0};
    double value = 1;
    std::string variable;
    std::string key = "Space";
    std::string comparison = "greater";
    bool visible = true;
};

struct BehaviorLink { std::uint32_t from = 0; std::uint32_t to = 0; std::string output = "next"; };
struct BehaviorGraph { bool enabled = true; std::vector<BehaviorNode> nodes; std::vector<BehaviorLink> links; };

std::string behaviorKindName(BehaviorKind kind);
BehaviorKind parseBehaviorKind(const std::string& name);
bool behaviorEvent(BehaviorKind kind);
int behaviorKey(const std::string& key);
bool validVariableName(const std::string& name);
nlohmann::json behaviorJson(const BehaviorGraph& graph);
BehaviorGraph parseBehaviorGraph(const nlohmann::json& source);
nlohmann::json behaviorNodeCatalog();

}