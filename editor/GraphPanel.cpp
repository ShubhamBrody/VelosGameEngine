#include "GraphPanel.h"
#include "automation/SceneCommands.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <imnodes.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cmath>

namespace velos {
namespace {
using Json = nlohmann::json;
constexpr std::array views{"scene","classes","logic"};
constexpr std::array labels{"Begin Play","Fixed Tick","Key Pressed","Translate","Rotate","Body Velocity","Set Variable","Add Variable","Branch","Set Visible","Set Color"};

bool toolButton(const char* icon, const char* title) {
    const bool clicked = ImGui::Button(icon,ImVec2(30,27));
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s",title); }
    return clicked;
}

void title(const char* text) {
    ImNodes::BeginNodeTitleBar();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 215);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImNodes::EndNodeTitleBar();
}

bool variablePicker(const char* label, std::string& value, const Scene& scene, bool constant) {
    bool changed = false;
    ImGui::SetNextItemWidth(190);
    if (ImGui::BeginCombo(label,value.empty() ? "Constant" : value.c_str())) {
        if (constant && ImGui::Selectable("Constant",value.empty())) { value.clear(); changed = true; }
        for (const auto& [name, ignored] : scene.variables) {
            static_cast<void>(ignored);
            if (ImGui::Selectable(name.c_str(),value == name)) { value = name; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}
}

struct GraphPanel::Impl {
    ImNodesContext* context = ImNodes::CreateContext();
    std::array<ImNodesEditorContext*,3> editors{ImNodes::EditorContextCreate(),ImNodes::EditorContextCreate(),ImNodes::EditorContextCreate()};
    std::array<bool,3> arrange{true,true,false};
    int mode = 0;
    bool focus = false;
    std::size_t page = 0;
    int addKind = 1;
    EntityId logicOwner = 0;
    std::map<std::uint32_t,DirectX::XMFLOAT2> positions;
    std::string variableName;
    std::string error;
    bool removeNodes = false;
    bool visible = false;
    bool focused = false;
    std::size_t visibleNodes = 0;
    std::size_t visibleLinks = 0;

    Impl() {
        ImNodes::SetCurrentContext(context);
        auto& style = ImNodes::GetStyle();
        style.NodeCornerRounding = 4;
        style.NodePadding = {12,10};
        style.GridSpacing = 24;
        style.Colors[ImNodesCol_GridBackground] = IM_COL32(25,29,31,255);
        style.Colors[ImNodesCol_GridLine] = IM_COL32(38,43,46,255);
        style.Colors[ImNodesCol_GridLinePrimary] = IM_COL32(50,58,60,255);
        style.Colors[ImNodesCol_NodeBackground] = IM_COL32(38,44,47,255);
        style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(43,51,54,255);
        style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(44,61,57,255);
        style.Colors[ImNodesCol_TitleBar] = IM_COL32(40,79,75,255);
        style.Colors[ImNodesCol_TitleBarSelected] = IM_COL32(47,106,85,255);
        style.Colors[ImNodesCol_Link] = IM_COL32(103,187,157,255);
        style.Colors[ImNodesCol_Pin] = IM_COL32(171,193,189,255);
        ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;
        ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
    }
    ~Impl() {
        ImNodes::SetCurrentContext(context);
        for (auto* editor : editors) { ImNodes::EditorContextFree(editor); }
        ImNodes::DestroyContext(context);
    }

    bool apply(Scene& scene, History& history, const Json& operations) {
        try {
            auto edit = automation::prepareSceneEdit(scene,{{"operations",operations}});
            if (edit.before == edit.after) { return false; }
            if (edit.before.size() + edit.after.size() > history.budgetBytes()) { throw std::runtime_error("Graph edit exceeds the undo budget."); }
            scene = std::move(edit.candidate);
            error.clear();
            return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool sceneGraph(Scene& scene, History& history, EntityId& selected, bool playing) {
        constexpr std::size_t pageSize = 128;
        if (page * pageSize >= scene.entities().size()) { page = 0; }
        ImGui::BeginDisabled(page == 0);
        if (toolButton("<","Previous graph page")) { --page; arrange[0] = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled((page + 1) * pageSize >= scene.entities().size());
        if (toolButton(">","Next graph page")) { ++page; arrange[0] = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%zu entities | Page %zu",scene.entities().size(),page + 1);
        std::map<EntityId,int> indexes;
        std::map<int,EntityId> owners;
        const auto begin = page * pageSize;
        const auto end = std::min(begin + pageSize,scene.entities().size());
        const auto columns = std::max(1,static_cast<int>(ImGui::GetContentRegionAvail().x / 260));
        visibleNodes = end - begin;
        visibleLinks = 0;
        ImNodes::BeginNodeEditor();
        for (std::size_t index = begin; index < end; ++index) {
            const auto owner = scene.entities()[index];
            const auto node = static_cast<int>(index - begin + 1);
            indexes.emplace(owner,node);
            owners.emplace(node,owner);
            const auto* identity = scene.get<Identity>(owner);
            const auto* transform = scene.get<Transform>(owner);
            if (arrange[0]) { ImNodes::SetNodeGridSpacePos(node,ImVec2(24 + static_cast<float>((index - begin) % columns) * 260,24 + static_cast<float>((index - begin) / columns) * 325)); }
            ImNodes::BeginNode(node);
            ImGui::PushID(node);
            title(identity->name.c_str());
            ImNodes::BeginInputAttribute(node * 4);
            ImGui::Text("Entity %llu",static_cast<unsigned long long>(owner));
            ImNodes::EndInputAttribute();
            ImNodes::BeginStaticAttribute(node * 4 + 2);
            ImGui::Dummy(ImVec2(210,0));
            ImGui::TextDisabled("Position  %.1f  %.1f  %.1f",transform->position.x,transform->position.y,transform->position.z);
            if (scene.get<MeshRenderer>(owner)) { ImGui::TextUnformatted("MeshRenderer"); }
            if (scene.get<Light>(owner)) { ImGui::TextUnformatted("Light"); }
            if (scene.get<RigidBody>(owner)) { ImGui::TextUnformatted("RigidBody"); }
            if (scene.get<BehaviorGraph>(owner)) { ImGui::TextUnformatted("BehaviorGraph"); }
            ImNodes::EndStaticAttribute();
            ImNodes::BeginOutputAttribute(node * 4 + 1);
            ImGui::TextUnformatted("Children");
            ImNodes::EndOutputAttribute();
            ImGui::PopID();
            ImNodes::EndNode();
        }
        for (const auto& [owner,node] : indexes) {
            const auto parent = scene.get<Transform>(owner)->parent;
            if (indexes.contains(parent)) { ImNodes::Link(node,indexes.at(parent) * 4 + 1,node * 4); ++visibleLinks; }
        }
        ImNodes::MiniMap(0.15f,ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        arrange[0] = false;
        int hovered = 0;
        if (ImNodes::IsNodeHovered(&hovered) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && owners.contains(hovered)) { selected = owners.at(hovered); }
        int from = 0;
        int to = 0;
        if (!playing && ImNodes::IsLinkCreated(&from,&to)) {
            if (from % 4 == 0) { std::swap(from,to); }
            if (from % 4 == 1 && to % 4 == 0 && owners.contains(from / 4) && owners.contains(to / 4)) {
                return apply(scene,history,Json::array({{{"op","reparent"},{"entity",std::to_string(owners.at(to / 4))},{"parent",std::to_string(owners.at(from / 4))},{"preserve_world",true}}}));
            }
        }
        int destroyed = 0;
        if (!playing && ImNodes::IsLinkDestroyed(&destroyed) && owners.contains(destroyed)) {
            return apply(scene,history,Json::array({{{"op","reparent"},{"entity",std::to_string(owners.at(destroyed))},{"parent","0"},{"preserve_world",true}}}));
        }
        return false;
    }

    void classes() {
        const auto catalog = automation::componentCatalog();
        ImGui::Text("Entity composition | %zu component classes",catalog.size());
        const auto columns = std::clamp(static_cast<int>((ImGui::GetContentRegionAvail().x - 270) / 250),1,3);
        visibleNodes = catalog.size() + 1;
        visibleLinks = catalog.size();
        ImNodes::BeginNodeEditor();
        if (arrange[1]) { ImNodes::SetNodeGridSpacePos(1,ImVec2(24,60)); }
        ImNodes::BeginNode(1);
        title("Entity");
        ImGui::Dummy(ImVec2(160,0));
        ImGui::TextUnformatted("Stable scene-local ID");
        for (std::size_t index = 0; index < catalog.size(); ++index) {
            ImNodes::BeginOutputAttribute(10 + static_cast<int>(index));
            ImGui::TextUnformatted(catalog[index].at("name").get_ref<const std::string&>().c_str());
            ImNodes::EndOutputAttribute();
        }
        ImNodes::EndNode();
        for (std::size_t index = 0; index < catalog.size(); ++index) {
            const auto node = static_cast<int>(index) + 2;
            if (arrange[1]) { ImNodes::SetNodeGridSpacePos(node,ImVec2(275 + static_cast<float>(index % columns) * 250,24 + static_cast<float>(index / columns) * 450)); }
            const auto& component = catalog[index];
            ImNodes::BeginNode(node);
            title(component.at("name").get_ref<const std::string&>().c_str());
            ImNodes::BeginInputAttribute(100 + node);
            ImGui::TextUnformatted(component.at("required").get<bool>() ? "Required component" : "Optional component");
            ImNodes::EndInputAttribute();
            ImGui::Dummy(ImVec2(205,0));
            const auto& values = component.at("defaults");
            if (values.is_object()) {
                for (const auto& field : values.items()) {
                    const auto type = field.value().is_array() ? "array" : field.value().is_object() ? "object" : field.value().is_string() ? "string" : field.value().is_boolean() ? "bool" : "number";
                    ImGui::Text("%s : %s",field.key().c_str(),type);
                }
            } else { ImGui::TextUnformatted("value : number"); }
            ImNodes::EndNode();
        }
        for (std::size_t index = 0; index < catalog.size(); ++index) { ImNodes::Link(static_cast<int>(index) + 1,10 + static_cast<int>(index),102 + static_cast<int>(index)); }
        ImNodes::MiniMap(0.15f,ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        arrange[1] = false;
    }

    bool logic(Scene& scene, History& history, EntityId selected, bool playing, const Json& execution) {
        if (!scene.contains(selected)) { ImGui::TextDisabled("No entity selected"); return false; }
        if (logicOwner != selected) { logicOwner = selected; positions.clear(); ImNodes::ClearNodeSelection(); ImNodes::ClearLinkSelection(); }
        const auto* stored = scene.get<BehaviorGraph>(selected);
        auto graph = stored ? *stored : BehaviorGraph{};
        bool changed = false;
        ImGui::TextUnformatted(scene.get<Identity>(selected)->name.c_str());
        ImGui::NewLine();
        ImGui::BeginDisabled(playing);
        ImGui::SetNextItemWidth(145);
        if (ImGui::BeginCombo("##NodeType",labels[static_cast<std::size_t>(addKind)])) {
            for (std::size_t index = 0; index < labels.size(); ++index) { if (ImGui::Selectable(labels[index],addKind == static_cast<int>(index))) { addKind = static_cast<int>(index); } }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (toolButton("+","Add behavior node")) {
            BehaviorNode node;
            for (const auto& existing : graph.nodes) { node.id = std::max(node.id,existing.id + 1); }
            node.kind = static_cast<BehaviorKind>(addKind);
            node.position = {24 + static_cast<float>(graph.nodes.size() % 3) * 285,24 + static_cast<float>(graph.nodes.size() / 3) * 240};
            if (node.kind == BehaviorKind::SetVariable || node.kind == BehaviorKind::AddVariable || node.kind == BehaviorKind::Branch) {
                if (!scene.variables.empty()) { node.variable = scene.variables.begin()->first; }
            }
            if (node.kind == BehaviorKind::SetColor) { node.vector = {0.27f,0.78f,0.61f}; }
            graph.nodes.push_back(std::move(node));
            changed = true;
        }
        ImGui::SameLine();
        removeNodes = toolButton("\xEE\x9D\x8D","Remove selected nodes or links")
            || (focused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete));
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Enabled",&graph.enabled);
        ImGui::EndDisabled();
        visibleNodes = graph.nodes.size();
        visibleLinks = graph.links.size();
        const auto tick = execution.value("tick",std::uint64_t(0));
        std::set<std::uint32_t> active;
        for (const auto& item : execution.at("executed")) {
            if (item.at("entity") == std::to_string(selected) && item.at("last_tick").get<std::uint64_t>() + 1 >= tick) { active.insert(item.at("node").get<std::uint32_t>()); }
        }
        ImNodes::BeginNodeEditor();
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            auto& node = graph.nodes[index];
            const auto nodeId = static_cast<int>(node.id);
            if (arrange[2] && !playing) {
                node.position = {24 + static_cast<float>(index % 3) * 300,24 + static_cast<float>(index / 3) * 265};
                changed = stored != nullptr;
            }
            const auto previous = positions.find(node.id);
            if (previous == positions.end() || (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && (previous->second.x != node.position.x || previous->second.y != node.position.y))) {
                ImNodes::SetNodeGridSpacePos(nodeId,ImVec2(node.position.x,node.position.y));
                positions[node.id] = node.position;
            }
            const auto color = playing && active.contains(node.id) ? IM_COL32(38,128,87,255) : behaviorEvent(node.kind) ? IM_COL32(120,51,61,255)
                : node.kind == BehaviorKind::Branch ? IM_COL32(127,97,44,255) : IM_COL32(43,79,105,255);
            ImNodes::PushColorStyle(ImNodesCol_TitleBar,color);
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected,color);
            ImNodes::BeginNode(nodeId);
            ImGui::PushID(nodeId);
            title(labels[static_cast<std::size_t>(node.kind)]);
            if (!behaviorEvent(node.kind)) {
                ImNodes::BeginInputAttribute(nodeId * 4,ImNodesPinShape_TriangleFilled);
                ImGui::TextUnformatted("In");
                ImNodes::EndInputAttribute();
            }
            ImNodes::BeginStaticAttribute(nodeId * 4 + 3);
            ImGui::Dummy(ImVec2(220,0));
            ImGui::BeginDisabled(playing);
            if (node.kind == BehaviorKind::Translate || node.kind == BehaviorKind::Rotate || node.kind == BehaviorKind::Velocity) {
                ImGui::SetNextItemWidth(220);
                changed |= ImGui::DragFloat3("##Vector",&node.vector.x,0.05f,-1000,1000,"%.2f");
                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s",node.kind == BehaviorKind::Rotate ? "Degrees per second (pitch, yaw, roll)" : "Direction/rate (X, Y, Z)"); }
                changed |= variablePicker("##Variable",node.variable,scene,true);
                if (node.variable.empty()) { ImGui::SetNextItemWidth(190); changed |= ImGui::InputDouble("##Value",&node.value,0.1,1,"%.2f"); }
            } else if (node.kind == BehaviorKind::SetVariable || node.kind == BehaviorKind::AddVariable || node.kind == BehaviorKind::Branch) {
                changed |= variablePicker("##Variable",node.variable,scene,false);
                ImGui::SetNextItemWidth(190);
                changed |= ImGui::InputDouble("##Value",&node.value,0.1,1,"%.2f");
                if (node.kind == BehaviorKind::Branch) {
                    ImGui::SetNextItemWidth(190);
                    if (ImGui::BeginCombo("##Comparison",node.comparison.c_str())) {
                        for (const auto* comparison : {"less","less_equal","equal","not_equal","greater_equal","greater"}) { if (ImGui::Selectable(comparison,node.comparison == comparison)) { node.comparison = comparison; changed = true; } }
                        ImGui::EndCombo();
                    }
                }
            } else if (node.kind == BehaviorKind::KeyPressed) {
                ImGui::SetNextItemWidth(190);
                changed |= ImGui::InputText("##Key",&node.key);
            } else if (node.kind == BehaviorKind::SetVisible) { changed |= ImGui::Checkbox("Visible",&node.visible); }
            else if (node.kind == BehaviorKind::SetColor) { ImGui::SetNextItemWidth(220); changed |= ImGui::ColorEdit3("##Color",&node.vector.x,ImGuiColorEditFlags_NoInputs); }
            if (playing && !node.variable.empty()) { ImGui::Text("%s = %.3f",node.variable.c_str(),scene.variables.at(node.variable)); }
            ImGui::EndDisabled();
            ImNodes::EndStaticAttribute();
            ImNodes::BeginOutputAttribute(nodeId * 4 + 1,ImNodesPinShape_TriangleFilled);
            ImGui::TextUnformatted(node.kind == BehaviorKind::Branch ? "True" : "Next");
            ImNodes::EndOutputAttribute();
            if (node.kind == BehaviorKind::Branch) {
                ImNodes::BeginOutputAttribute(nodeId * 4 + 2,ImNodesPinShape_TriangleFilled);
                ImGui::TextUnformatted("False");
                ImNodes::EndOutputAttribute();
            }
            ImGui::PopID();
            ImNodes::EndNode();
            ImNodes::SetNodeDraggable(nodeId,!playing);
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
        for (std::size_t index = 0; index < graph.links.size(); ++index) {
            const auto& link = graph.links[index];
            ImNodes::Link(static_cast<int>(index) + 1,static_cast<int>(link.from) * 4 + (link.output == "false" ? 2 : 1),static_cast<int>(link.to) * 4);
        }
        ImNodes::MiniMap(0.15f,ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        arrange[2] = false;
        if (!playing) {
            int from = 0;
            int to = 0;
            if (ImNodes::IsLinkCreated(&from,&to)) {
                if (from % 4 == 0) { std::swap(from,to); }
                if (to % 4 == 0 && (from % 4 == 1 || from % 4 == 2)) {
                    const auto owner = std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& node) { return node.id == static_cast<std::uint32_t>(from / 4); });
                    if (owner != graph.nodes.end()) { graph.links.push_back({static_cast<std::uint32_t>(from / 4),static_cast<std::uint32_t>(to / 4),owner->kind == BehaviorKind::Branch ? from % 4 == 2 ? "false" : "true" : "next"}); changed = true; }
                }
            }
            int removedLink = 0;
            if (ImNodes::IsLinkDestroyed(&removedLink) && removedLink > 0 && static_cast<std::size_t>(removedLink) <= graph.links.size()) { graph.links.erase(graph.links.begin() + removedLink - 1); changed = true; }
            if (removeNodes) {
                std::vector<int> selectedNodes(static_cast<std::size_t>(ImNodes::NumSelectedNodes()));
                if (!selectedNodes.empty()) { ImNodes::GetSelectedNodes(selectedNodes.data()); }
                std::vector<int> selectedLinks(static_cast<std::size_t>(ImNodes::NumSelectedLinks()));
                if (!selectedLinks.empty()) { ImNodes::GetSelectedLinks(selectedLinks.data()); }
                for (const auto node : selectedNodes) {
                    std::erase_if(graph.nodes,[&](const auto& item) { return item.id == static_cast<std::uint32_t>(node); });
                    std::erase_if(graph.links,[&](const auto& item) { return item.from == static_cast<std::uint32_t>(node) || item.to == static_cast<std::uint32_t>(node); });
                }
                std::sort(selectedLinks.rbegin(),selectedLinks.rend());
                if (selectedNodes.empty()) { for (const auto link : selectedLinks) { if (link > 0 && static_cast<std::size_t>(link) <= graph.links.size()) { graph.links.erase(graph.links.begin() + link - 1); } } }
                changed |= !selectedNodes.empty() || !selectedLinks.empty();
                ImNodes::ClearNodeSelection();
                ImNodes::ClearLinkSelection();
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                for (auto& node : graph.nodes) {
                    const auto position = ImNodes::GetNodeGridSpacePos(static_cast<int>(node.id));
                    if (std::abs(position.x - node.position.x) > 0.5f || std::abs(position.y - node.position.y) > 0.5f) { node.position = {position.x,position.y}; changed = true; }
                }
            }
        }
        if (changed) { return apply(scene,history,Json::array({{{"op","patch"},{"entity",std::to_string(selected)},{"fields",{{"behavior",behaviorJson(graph)}}}}})); }
        return false;
    }
};

GraphPanel::GraphPanel() : impl_(std::make_unique<Impl>()) {}
GraphPanel::~GraphPanel() = default;
void GraphPanel::show(const std::string& view, bool arrange) {
    const auto found = std::find(views.begin(),views.end(),view);
    if (found == views.end()) { throw std::runtime_error("Graph view must be scene, classes or logic."); }
    impl_->mode = static_cast<int>(found - views.begin());
    impl_->focus = true;
    if (arrange) { impl_->arrange[static_cast<std::size_t>(impl_->mode)] = true; }
}
std::string GraphPanel::view() const { return views[static_cast<std::size_t>(impl_->mode)]; }
const std::string& GraphPanel::error() const { return impl_->error; }
bool GraphPanel::focused() const noexcept { return impl_->focused; }
nlohmann::json GraphPanel::state() const {
    return {{"view",view()},{"visible",impl_->visible},{"nodes",impl_->visibleNodes},{"links",impl_->visibleLinks},{"error",impl_->error}};
}

bool GraphPanel::draw(Scene& scene, History& history, EntityId& selected, bool playing, const nlohmann::json& execution) {
    ImNodes::SetCurrentContext(impl_->context);
    if (impl_->focus) { ImGui::SetNextWindowFocus(); impl_->focus = false; }
    impl_->visible = ImGui::Begin("Graphs");
    impl_->focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    impl_->visibleNodes = impl_->visibleLinks = 0;
    if (!impl_->visible) { ImGui::End(); return false; }
    for (int index = 0; index < 3; ++index) {
        if (index != 0) { ImGui::SameLine(); }
        ImGui::RadioButton(index == 0 ? "Scene" : index == 1 ? "Classes" : "Logic",&impl_->mode,index);
    }
    ImGui::SameLine();
    if (toolButton("\xEE\xA2\xA8","Arrange graph")) { impl_->arrange[static_cast<std::size_t>(impl_->mode)] = true; }
    ImGui::SameLine();
    if (ImGui::Button("Variables")) { ImGui::OpenPopup("Scene Variables"); }
    bool changed = false;
    if (ImGui::BeginPopup("Scene Variables")) {
        ImGui::TextUnformatted("Scene Variables");
        ImGui::BeginDisabled(playing);
        Json updates = Json::object();
        ImGui::SetNextItemWidth(190);
        ImGui::InputText("##VariableName",&impl_->variableName);
        ImGui::SameLine();
        if (toolButton("+","Add numeric variable")) { updates[impl_->variableName] = 0; }
        for (const auto& [name, current] : scene.variables) {
            ImGui::PushID(name.c_str());
            ImGui::TextUnformatted(name.c_str());
            ImGui::SetNextItemWidth(190);
            double value = current;
            if (ImGui::InputDouble("##Value",&value,0.1,1,"%.3f")) { updates[name] = value; }
            ImGui::SameLine();
            if (toolButton("\xEE\x9D\x8D","Remove unused variable")) { updates[name] = nullptr; }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        if (!updates.empty()) { changed = impl_->apply(scene,history,Json::array({{{"op","settings"},{"fields",{{"variables",updates}}}}})); }
        ImGui::EndPopup();
    }
    ImNodes::EditorContextSet(impl_->editors[static_cast<std::size_t>(impl_->mode)]);
    if (!impl_->error.empty()) { ImGui::TextWrapped("%s",impl_->error.c_str()); }
    if (impl_->mode == 0) { changed |= impl_->sceneGraph(scene,history,selected,playing); }
    else if (impl_->mode == 1) { impl_->classes(); }
    else { changed |= impl_->logic(scene,history,selected,playing,execution); }
    ImGui::End();
    return changed;
}

}