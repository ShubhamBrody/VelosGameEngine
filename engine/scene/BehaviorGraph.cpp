#include "scene/BehaviorGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace velos {
namespace {
using Json = nlohmann::json;
constexpr std::array names{"begin_play", "tick", "key_pressed", "translate", "rotate", "velocity", "set_variable", "add_variable", "branch", "set_visible", "set_color"};

void fields(const Json& object, std::initializer_list<std::string_view> allowed) {
    if (!object.is_object()) { throw std::runtime_error("Graph data must contain objects."); }
    for (const auto& field : object.items()) {
        if (std::find(allowed.begin(), allowed.end(), field.key()) == allowed.end()) { throw std::runtime_error("Unknown graph field: " + field.key()); }
    }
}

double finite(const Json& value) {
    if (!value.is_number()) { throw std::runtime_error("Graph values must be numbers."); }
    const auto result = value.get<double>();
    if (!std::isfinite(result) || std::abs(result) > 1e6) { throw std::runtime_error("Graph values must be finite and bounded."); }
    return result;
}

std::uint32_t nodeId(const Json& value) {
    if (!value.is_number_integer() || value.get<std::int64_t>() < 1 || value.get<std::uint64_t>() > 1000000) { throw std::runtime_error("Graph node IDs must be integers from 1 to 1000000."); }
    return value.get<std::uint32_t>();
}
}

std::string behaviorKindName(BehaviorKind kind) { return names.at(static_cast<std::size_t>(kind)); }
BehaviorKind parseBehaviorKind(const std::string& name) {
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) { throw std::runtime_error("Unknown behavior node type: " + name); }
    return static_cast<BehaviorKind>(found - names.begin());
}
bool behaviorEvent(BehaviorKind kind) { return kind == BehaviorKind::BeginPlay || kind == BehaviorKind::Tick || kind == BehaviorKind::KeyPressed; }
bool validVariableName(const std::string& name) {
    const auto letter = [](char character) { return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_'; };
    return !name.empty() && name.size() <= 64 && letter(name[0]) && std::all_of(name.begin(), name.end(), [&](char character) { return letter(character) || (character >= '0' && character <= '9'); });
}
int behaviorKey(const std::string& key) {
    if (key.size() == 1 && ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9'))) { return key[0]; }
    const std::map<std::string, int> special{{"Space",32},{"Enter",13},{"Left",37},{"Up",38},{"Right",39},{"Down",40}};
    const auto found = special.find(key);
    if (found == special.end()) { throw std::runtime_error("Graph input keys must be A-Z, 0-9, Space, Enter or arrow keys."); }
    return found->second;
}

nlohmann::json behaviorJson(const BehaviorGraph& graph) {
    Json result{{"enabled", graph.enabled}, {"nodes", Json::array()}, {"links", Json::array()}};
    for (const auto& node : graph.nodes) {
        result["nodes"].push_back({{"id",node.id}, {"kind",behaviorKindName(node.kind)}, {"position",{node.position.x,node.position.y}},
            {"vector",{node.vector.x,node.vector.y,node.vector.z}}, {"value",node.value}, {"variable",node.variable},
            {"key",node.key}, {"comparison",node.comparison}, {"visible",node.visible}});
    }
    for (const auto& link : graph.links) { result["links"].push_back({{"from",link.from},{"to",link.to},{"output",link.output}}); }
    return result;
}

BehaviorGraph parseBehaviorGraph(const nlohmann::json& source) {
    fields(source, {"enabled","nodes","links"});
    const auto& nodes = source.at("nodes");
    const auto& links = source.at("links");
    if (!nodes.is_array() || nodes.size() > 128 || !links.is_array() || links.size() > 256) { throw std::runtime_error("A behavior graph supports at most 128 nodes and 256 links."); }
    BehaviorGraph graph;
    graph.enabled = source.value("enabled", true);
    std::map<std::uint32_t, std::size_t> indexes;
    for (const auto& item : nodes) {
        fields(item, {"id","kind","position","vector","value","variable","key","comparison","visible"});
        BehaviorNode node;
        node.id = nodeId(item.at("id"));
        node.position = {24 + static_cast<float>(graph.nodes.size() % 3) * 300,24 + static_cast<float>(graph.nodes.size() / 3) * 265};
        if (!indexes.emplace(node.id, graph.nodes.size()).second) { throw std::runtime_error("Duplicate graph node ID."); }
        node.kind = parseBehaviorKind(item.at("kind").get<std::string>());
        if (item.contains("position")) {
            const auto& position = item.at("position");
            if (!position.is_array() || position.size() != 2) { throw std::runtime_error("Graph layout position requires two numbers."); }
            node.position = {static_cast<float>(finite(position[0])), static_cast<float>(finite(position[1]))};
        }
        if (item.contains("vector")) {
            const auto& vector = item.at("vector");
            if (!vector.is_array() || vector.size() != 3) { throw std::runtime_error("Graph action vectors require three numbers."); }
            node.vector = {static_cast<float>(finite(vector[0])), static_cast<float>(finite(vector[1])), static_cast<float>(finite(vector[2]))};
        }
        node.value = finite(item.value("value", Json(1)));
        node.variable = item.value("variable", std::string{});
        if (!node.variable.empty() && !validVariableName(node.variable)) { throw std::runtime_error("Invalid graph variable name."); }
        if ((node.kind == BehaviorKind::SetVariable || node.kind == BehaviorKind::AddVariable || node.kind == BehaviorKind::Branch) && node.variable.empty()) {
            throw std::runtime_error("This graph node requires a variable name.");
        }
        node.key = item.value("key", std::string("Space"));
        static_cast<void>(behaviorKey(node.key));
        node.comparison = item.value("comparison", std::string("greater"));
        const std::set<std::string> comparisons{"less","less_equal","equal","not_equal","greater_equal","greater"};
        if (!comparisons.contains(node.comparison)) { throw std::runtime_error("Unsupported graph comparison."); }
        node.visible = item.value("visible", true);
        if (node.kind == BehaviorKind::SetColor && (node.vector.x < 0 || node.vector.x > 1 || node.vector.y < 0 || node.vector.y > 1 || node.vector.z < 0 || node.vector.z > 1)) {
            throw std::runtime_error("Graph material colors must be in [0,1].");
        }
        graph.nodes.push_back(std::move(node));
    }
    std::set<std::pair<std::uint32_t,std::string>> usedOutputs;
    std::vector<std::uint32_t> incoming(graph.nodes.size());
    std::vector<std::vector<std::size_t>> outgoing(graph.nodes.size());
    for (const auto& item : links) {
        fields(item, {"from","to","output"});
        BehaviorLink link{nodeId(item.at("from")),nodeId(item.at("to")),item.value("output",std::string("next"))};
        if (!indexes.contains(link.from) || !indexes.contains(link.to)) { throw std::runtime_error("Graph link references a missing node."); }
        const auto from = indexes.at(link.from);
        const auto to = indexes.at(link.to);
        if (behaviorEvent(graph.nodes[to].kind)) { throw std::runtime_error("Graph event nodes do not accept execution inputs."); }
        if (graph.nodes[from].kind == BehaviorKind::Branch ? (link.output != "true" && link.output != "false") : link.output != "next") {
            throw std::runtime_error("Graph link uses an invalid execution output.");
        }
        if (!usedOutputs.emplace(link.from,link.output).second) { throw std::runtime_error("Each execution output accepts one link."); }
        outgoing[from].push_back(to);
        ++incoming[to];
        graph.links.push_back(std::move(link));
    }
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < incoming.size(); ++index) { if (incoming[index] == 0) { ready.push_back(index); } }
    for (std::size_t index = 0; index < ready.size(); ++index) {
        for (const auto next : outgoing[ready[index]]) { if (--incoming[next] == 0) { ready.push_back(next); } }
    }
    if (ready.size() != graph.nodes.size()) { throw std::runtime_error("Behavior graphs must be acyclic; use tick/key events for repeated work."); }
    return graph;
}

nlohmann::json behaviorNodeCatalog() {
    Json result = Json::array();
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto kind = static_cast<BehaviorKind>(index);
        result.push_back({{"kind",names[index]}, {"event",behaviorEvent(kind)},
            {"outputs", kind == BehaviorKind::Branch ? Json::array({"true","false"}) : Json::array({"next"})},
            {"target","owner entity"}, {"time_scaled",kind == BehaviorKind::Translate || kind == BehaviorKind::Rotate}});
    }
    return result;
}

}