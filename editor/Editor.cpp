#include "Editor.h"
#include "platform/Files.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cfloat>

namespace velos {
using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

constexpr ImVec4 accent{0.27f, 0.78f, 0.61f, 1};

bool iconButton(const char* glyph, const char* tooltip, bool active = false) {
    ImGui::PushID(tooltip);
    if (active) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f,0.37f,0.29f,1)); }
    const bool pressed = ImGui::Button(glyph, ImVec2(34, 28));
    if (active) { ImGui::PopStyleColor(); }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tooltip); }
    ImGui::PopID();
    return pressed;
}

void configureStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {12, 10};
    style.FramePadding = {7, 5};
    style.ItemSpacing = {8, 7};
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 3;
    style.GrabRounding = 2;
    style.TabRounding = 3;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.ScrollbarSize = 12;
    style.Colors[ImGuiCol_Text] = {0.86f,0.89f,0.88f,1};
    style.Colors[ImGuiCol_TextDisabled] = {0.48f,0.53f,0.52f,1};
    style.Colors[ImGuiCol_WindowBg] = {0.10f,0.115f,0.12f,1};
    style.Colors[ImGuiCol_ChildBg] = {0.10f,0.115f,0.12f,1};
    style.Colors[ImGuiCol_PopupBg] = {0.12f,0.135f,0.14f,1};
    style.Colors[ImGuiCol_Border] = {0.22f,0.245f,0.25f,1};
    style.Colors[ImGuiCol_FrameBg] = {0.145f,0.165f,0.17f,1};
    style.Colors[ImGuiCol_FrameBgHovered] = {0.22f,0.26f,0.25f,1};
    style.Colors[ImGuiCol_FrameBgActive] = {0.20f,0.32f,0.28f,1};
    style.Colors[ImGuiCol_Button] = {0.16f,0.18f,0.19f,1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.24f,0.30f,0.28f,1};
    style.Colors[ImGuiCol_ButtonActive] = {0.22f,0.39f,0.31f,1};
    style.Colors[ImGuiCol_Header] = {0.17f,0.23f,0.21f,1};
    style.Colors[ImGuiCol_HeaderHovered] = {0.21f,0.31f,0.27f,1};
    style.Colors[ImGuiCol_HeaderActive] = {0.23f,0.36f,0.29f,1};
    style.Colors[ImGuiCol_Tab] = {0.12f,0.14f,0.15f,1};
    style.Colors[ImGuiCol_TabSelected] = {0.19f,0.24f,0.23f,1};
    style.Colors[ImGuiCol_TabHovered] = {0.24f,0.34f,0.28f,1};
    style.Colors[ImGuiCol_TitleBg] = {0.10f,0.115f,0.12f,1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.16f,0.20f,0.19f,1};
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = {0.4f,0.9f,0.7f,1};
    style.Colors[ImGuiCol_DockingPreview] = {accent.x, accent.y, accent.z, 0.35f};
    style.Colors[ImGuiCol_PlotLines] = accent;
    style.Colors[ImGuiCol_Separator] = {0.20f,0.235f,0.23f,1};
}

}

Editor::Editor(HWND window, Renderer& renderer, bool isolated)
    : window_(window), renderer_(renderer), scene_(Scene::demo()), isolated_(isolated) {
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    layoutPath_ = utf8((localDataDirectory() / L"editor-layout.ini").native());
    io.IniFilename = isolated ? nullptr : layoutPath_.c_str();
    const auto font = std::filesystem::path(L"C:/Windows/Fonts/bahnschrift.ttf");
    if (std::filesystem::exists(font)) { io.Fonts->AddFontFromFileTTF(utf8(font.native()).c_str(), 17); }
    else { io.Fonts->AddFontDefault(); }
    const auto symbolFont = std::filesystem::path(L"C:/Windows/Fonts/segmdl2.ttf");
    if (std::filesystem::exists(symbolFont)) {
        ImFontConfig configuration;
        configuration.MergeMode = true;
        configuration.GlyphMinAdvanceX = 16;
        static const ImWchar ranges[] = {0xE700, 0xF000, 0};
        io.Fonts->AddFontFromFileTTF(utf8(symbolFont.native()).c_str(), 16, &configuration, ranges);
    }
    configureStyle();
    savedState_ = scene_.serialize();
    frame_.objects.reserve(128);
    log("D3D12 ready: " + renderer_.stats().adapter);
    log("Workshop loaded. " + std::to_string(scene_.entities().size()) + " entities.");
}

Editor::~Editor() = default;

void Editor::log(std::string message, bool error) {
    messages_.emplace_back(std::move(message), error);
    while (messages_.size() > 250) { messages_.pop_front(); }
}

void Editor::update(double elapsed) {
    if (playing_ && !paused_) {
        const auto ticks = clock_.advance(elapsed);
        for (std::uint32_t index = 0; index < ticks.steps; ++index) { scene_.tick(clock_.stepSeconds()); }
    }
    if (!playing_ && !isolated_ && !scenePath_.empty()) {
        autosaveSeconds_ += elapsed;
        if (autosaveSeconds_ >= 300) {
            autosaveSeconds_ = 0;
            try { writeTextAtomic(std::filesystem::path(scenePath_.native() + L".autosave"), scene_.serialize()); }
            catch (const std::exception& error) { log(error.what(), true); }
        }
    }
}

void Editor::startPlay() {
    if (playing_) { paused_ = !paused_; return; }
    playSnapshot_ = scene_.serialize();
    playing_ = true;
    paused_ = false;
    history_.cancel();
    clock_.reset();
    log("Simulation started.");
}

void Editor::stopPlay() {
    if (!playing_) { return; }
    std::string error;
    if (!scene_.deserialize(playSnapshot_, error)) { log("Cannot restore authored scene: " + error, true); return; }
    playing_ = paused_ = false;
    playSnapshot_.clear();
    clock_.reset();
    log("Simulation stopped. Authored scene restored.");
}

void Editor::stepPlay() {
    if (!playing_) { startPlay(); }
    paused_ = true;
    scene_.tick(clock_.stepSeconds());
}

void Editor::buildLayout() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto dockId = ImGui::GetID("VelosDock");
    if (!layoutBuilt_ && (isolated_ || !ImGui::DockBuilderGetNode(dockId))) {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 43));
        auto center = dockId;
        const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
        const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.255f, nullptr, &center);
        const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Scene", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Performance", bottom);
        ImGui::DockBuilderFinish(dockId);
    }
    layoutBuilt_ = true;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + 43));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 43));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##DockHost", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
    ImGui::DockSpace(dockId, ImVec2(0,0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Editor::draw(double elapsed) {
    changed_ = false;
    gizmoActive_ = false;
    elapsedSmoothed_ = elapsedSmoothed_ * 0.92f + static_cast<float>(elapsed * 1000) * 0.08f;
    frameTimes_[frameIndex_++ % frameTimes_.size()] = static_cast<float>(elapsed * 1000);
    if (!playing_) { history_.begin(scene_); }
    buildLayout();
    toolbar();
    hierarchy();
    inspector();
    viewport();
    assets();
    diagnostics();
    console();
    dialogs();
    if (pendingDuplicate_ != 0 && !playing_) {
        selected_ = scene_.duplicate(pendingDuplicate_);
        pendingDuplicate_ = 0;
        changed_ = true;
    }
    if (pendingDelete_ != 0 && !playing_) {
        scene_.destroy(pendingDelete_);
        pendingDelete_ = 0;
        if (!scene_.contains(selected_)) { selected_ = 0; }
        changed_ = true;
    }
    if (changed_) { scene_.touch(); }
    if (!playing_ && !ImGui::IsAnyItemActive() && !gizmoActive_) { history_.commit(scene_, "Scene edit"); }
    extractScene(scene_, frame_, selected_, grid_);
    const auto title = L"Velos | " + wide(scene_.name) + (scene_.serialize() != savedState_ && !playing_ ? L" *" : L"");
    SetWindowTextW(window_, title.c_str());
}

void Editor::toolbar() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 43));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12,7));
    ImGui::Begin("##Toolbar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);
    ImGui::TextColored(accent, "VELOS");
    ImGui::SameLine(85);
    if (iconButton("\uE8A5", "New scene")) { pendingAction_ = 1; showUnsaved_ = true; }
    ImGui::SameLine();
    if (iconButton("\uE8E5", "Open scene")) { pendingAction_ = 2; showUnsaved_ = true; }
    ImGui::SameLine();
    ImGui::BeginDisabled(playing_);
    if (iconButton("\uE74E", "Save scene")) { saveScene(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.canUndo());
    if (iconButton("\uE7A7", "Undo")) { std::string error; if (!history_.undo(scene_, error)) { log(error, true); } }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.canRedo());
    if (iconButton("\uE7A6", "Redo")) { std::string error; if (!history_.redo(scene_, error)) { log(error, true); } }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (iconButton("\uE710", "Add entity")) { ImGui::OpenPopup("Add entity"); }
    if (ImGui::BeginPopup("Add entity")) {
        ImGui::BeginDisabled(playing_);
        for (const auto* mesh : {"cube", "sphere", "plane", "quad"}) {
            if (ImGui::MenuItem(mesh)) { addPrimitive(mesh); }
        }
        if (ImGui::MenuItem("Point light")) {
            selected_ = scene_.create("Point light");
            scene_.set<Light>(selected_);
            scene_.get<Transform>(selected_)->position = {0,3,0};
            changed_ = true;
        }
        if (ImGui::MenuItem("Empty entity")) { selected_ = scene_.create("Entity"); changed_ = true; }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::SameLine(std::max(420.0f, viewport->WorkSize.x * 0.45f));
    if (iconButton(playing_ && !paused_ ? "\uE769" : "\uE768", playing_ && !paused_ ? "Pause simulation" : "Play simulation", playing_)) { startPlay(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing_);
    if (iconButton("\uE71A", "Stop and restore scene")) { stopPlay(); }
    ImGui::SameLine();
    if (iconButton("\uE893", "Step simulation")) { stepPlay(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", playing_ ? (paused_ ? "PAUSED" : "PLAYING") : "EDIT");
    if (viewport->WorkSize.x > 1150) {
        ImGui::SameLine(viewport->WorkSize.x - 280);
        ImGui::TextDisabled("D3D12");
        ImGui::SameLine();
        ImGui::Text("%.1f ms", elapsedSmoothed_);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu entities", scene_.entities().size());
    }
    ImGui::End();
    ImGui::PopStyleVar();

    const auto& io = ImGui::GetIO();
    if (!io.WantTextInput && !ImGui::IsAnyItemActive()) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) { if (!playing_) { saveScene(io.KeyShift); } }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) { pendingAction_ = 2; showUnsaved_ = true; }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !playing_) { std::string error; history_.undo(scene_, error); }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && !playing_) { std::string error; history_.redo(scene_, error); }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && !playing_) { pendingDuplicate_ = selected_; }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !playing_) { pendingDelete_ = selected_; }
        if (ImGui::IsKeyPressed(ImGuiKey_F)) { focusSelection(); }
        if (ImGui::IsKeyPressed(ImGuiKey_W)) { operation_ = 0; }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) { operation_ = 1; }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) { operation_ = 2; }
    }
}

void Editor::hierarchy() {
    if (ImGui::Begin("Scene")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Filter", "Filter entities", &filter_);
        ImGui::SeparatorText(scene_.name.c_str());
        ImGui::BeginDisabled(playing_);
        if (!filter_.empty()) {
            for (const auto id : scene_.entities()) {
                const auto* identity = scene_.get<Identity>(id);
                if (identity->name.find(filter_) != std::string::npos
                    && ImGui::Selectable((identity->name + "##" + std::to_string(id)).c_str(), selected_ == id)) { selected_ = id; }
            }
        } else {
            for (const auto id : scene_.entities()) { if (scene_.get<Transform>(id)->parent == 0) { hierarchyItem(id); } }
        }
        ImGui::Separator();
        ImGui::Selectable("Scene root", false, ImGuiSelectableFlags_None, ImVec2(0, 25));
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("VELOS_ENTITY")) {
                const auto id = *static_cast<const EntityId*>(payload->Data);
                changed_ |= scene_.reparent(id, 0);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void Editor::hierarchyItem(EntityId id) {
    const auto& identity = *scene_.get<Identity>(id);
    bool hasChildren = false;
    for (const auto child : scene_.entities()) { if (scene_.get<Transform>(child)->parent == id) { hasChildren = true; break; } }
    auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (selected_ == id) { flags |= ImGuiTreeNodeFlags_Selected; }
    if (!hasChildren) { flags |= ImGuiTreeNodeFlags_Leaf; }
    ImGui::PushID(static_cast<int>(id));
    const bool open = ImGui::TreeNodeEx("##Entity", flags, "%s  %s", scene_.get<Light>(id) ? "\uE706" : "\uE7B8", identity.name.c_str());
    if (ImGui::IsItemClicked()) { selected_ = id; }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { selected_ = id; focusSelection(); }
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("VELOS_ENTITY", &id, sizeof(id));
        ImGui::TextUnformatted(identity.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("VELOS_ENTITY")) {
            const auto moved = *static_cast<const EntityId*>(payload->Data);
            if (scene_.reparent(moved, id)) { changed_ = true; }
            else { log("Reparent rejected: hierarchy would be invalid.", true); }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Duplicate")) { pendingDuplicate_ = id; }
        if (ImGui::MenuItem("Move to root")) { changed_ |= scene_.reparent(id, 0); }
        if (ImGui::MenuItem("Delete")) { pendingDelete_ = id; }
        ImGui::EndPopup();
    }
    if (open) {
        for (const auto child : scene_.entities()) { if (scene_.get<Transform>(child)->parent == id) { hierarchyItem(child); } }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Editor::inspector() {
    if (ImGui::Begin("Inspector")) {
        if (!scene_.contains(selected_)) {
            ImGui::TextDisabled("No selection");
        } else {
            ImGui::BeginDisabled(playing_);
            auto& identity = *scene_.get<Identity>(selected_);
            ImGui::SetNextItemWidth(-1);
            changed_ |= ImGui::InputText("##Name", &identity.name);
            ImGui::TextDisabled("Entity %llu", static_cast<unsigned long long>(selected_));
            changed_ |= ImGui::Checkbox("Visible", &identity.visible);
            ImGui::SeparatorText("Transform");
            auto& transform = *scene_.get<Transform>(selected_);
            ImGui::PushItemWidth(-80);
            changed_ |= ImGui::DragFloat3("Position", &transform.position.x, 0.02f, -10000, 10000, "%.2f");
            XMFLOAT4X4 local;
            XMStoreFloat4x4(&local, transform.matrix());
            float translation[3]{};
            float rotation[3]{};
            float scale[3]{};
            ImGuizmo::DecomposeMatrixToComponents(&local._11, translation, rotation, scale);
            if (ImGui::DragFloat3("Rotation", rotation, 0.5f, -360, 360, "%.1f")) {
                XMStoreFloat4(&transform.rotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation[0]),
                    XMConvertToRadians(rotation[1]), XMConvertToRadians(rotation[2])));
                changed_ = true;
            }
            changed_ |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.01f, 1000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopItemWidth();
            if (auto* mesh = scene_.get<MeshRenderer>(selected_)) {
                ImGui::SeparatorText("Material");
                ImGui::TextDisabled("%s", mesh->mesh.c_str());
                changed_ |= ImGui::ColorEdit3("Albedo", &mesh->color.x, ImGuiColorEditFlags_NoInputs);
                ImGui::SetNextItemWidth(-90);
                changed_ |= ImGui::SliderFloat("Roughness", &mesh->roughness, 0.04f, 1, "%.2f");
                ImGui::SetNextItemWidth(-90);
                changed_ |= ImGui::SliderFloat("Metallic", &mesh->metallic, 0, 1, "%.2f");
                changed_ |= ImGui::Checkbox("Cast shadow", &mesh->castShadow);
                changed_ |= ImGui::Checkbox("Unlit", &mesh->unlit);
            }
            if (auto* light = scene_.get<Light>(selected_)) {
                ImGui::SeparatorText("Light");
                changed_ |= ImGui::ColorEdit3("Color", &light->color.x, ImGuiColorEditFlags_NoInputs);
                ImGui::SetNextItemWidth(-90);
                changed_ |= ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0, 1000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                if (light->kind == LightKind::Directional) {
                    auto direction = light->direction;
                    if (ImGui::DragFloat3("Direction", &direction.x, 0.01f, -1, 1)) {
                        if (XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&direction))) > 1e-5f) { light->direction = direction; changed_ = true; }
                    }
                } else {
                    changed_ |= ImGui::DragFloat("Range", &light->range, 0.1f, 0.1f, 10000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                }
            }
            if (auto* spin = scene_.get<Spin>(selected_)) {
                ImGui::SeparatorText("Rotation behavior");
                changed_ |= ImGui::DragFloat("Degrees / sec", &spin->degreesPerSecond, 0.5f, -3600, 3600);
                if (ImGui::SmallButton("Remove rotation behavior")) { scene_.remove<Spin>(selected_); changed_ = true; }
            } else if (ImGui::Button("Add rotation behavior", ImVec2(-1,0))) {
                scene_.set<Spin>(selected_);
                changed_ = true;
            }
            if (scene_.get<RigidBody>(selected_)) {
                ImGui::SeparatorText("Rigid body");
                ImGui::TextDisabled("%s", scene_.get<RigidBody>(selected_)->motion == BodyMotion::Static ? "Static" : "Dynamic");
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::End();
}

void Editor::viewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::SetCursorPos(ImVec2(8, ImGui::GetCursorPosY() + 5));
        if (iconButton("\uE7C2", "Translate", operation_ == 0)) { operation_ = 0; }
        ImGui::SameLine();
        if (iconButton("\uE7AD", "Rotate", operation_ == 1)) { operation_ = 1; }
        ImGui::SameLine();
        if (iconButton("\uE740", "Scale", operation_ == 2)) { operation_ = 2; }
        ImGui::SameLine();
        ImGui::Checkbox("Local", &localSpace_);
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &snap_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        int mode = frame_.camera.orthographic ? 1 : 0;
        if (ImGui::Combo("##Camera mode", &mode, "3D\0 2D\0")) { frame_.camera.set2D(mode == 1); }
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &grid_);
        ImGui::SameLine();
        if (iconButton("\uE71D", "Frame selection")) { focusSelection(); }
        ImGui::Separator();
        const auto origin = ImGui::GetCursorScreenPos();
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(size.x, 64.0f);
        size.y = std::max(size.y, 64.0f);
        frame_.camera.aspect = size.x / size.y;
        renderer_.resizeScene(static_cast<UINT>(size.x * resolutionScale_), static_cast<UINT>(size.y * resolutionScale_));
        ImGui::Image(static_cast<ImTextureID>(renderer_.sceneTexture()), size);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("VELOS_PRIMITIVE")) {
                if (!playing_) { addPrimitive(static_cast<const char*>(payload->Data)); }
            }
            ImGui::EndDragDropTarget();
        }
        auto& io = ImGui::GetIO();
        if (hovered) {
            if (io.MouseWheel != 0) { frame_.camera.zoom(io.MouseWheel); }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) { frame_.camera.orbit(io.MouseDelta.x, io.MouseDelta.y); }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { frame_.camera.pan(io.MouseDelta.x, io.MouseDelta.y); }
        }
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetOrthographic(frame_.camera.orthographic);
        ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
        if (scene_.contains(selected_) && !playing_) {
            XMFLOAT4X4 view;
            XMFLOAT4X4 projection;
            XMFLOAT4X4 world;
            XMStoreFloat4x4(&view, frame_.camera.view());
            XMStoreFloat4x4(&projection, frame_.camera.projection());
            XMStoreFloat4x4(&world, scene_.worldMatrix(selected_));
            const auto operation = operation_ == 0 ? ImGuizmo::TRANSLATE : operation_ == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
            const float snapValues[] = {operation_ == 1 ? 15.0f : snapSize_, snapSize_, snapSize_};
            if (ImGuizmo::Manipulate(&view._11, &projection._11, operation, localSpace_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                &world._11, nullptr, snap_ ? snapValues : nullptr)) {
                changed_ |= scene_.setWorldMatrix(selected_, XMLoadFloat4x4(&world));
            }
            gizmoActive_ = ImGuizmo::IsUsing();
        }
        if (hovered && !ImGuizmo::IsOver() && !gizmoActive_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            pick(io.MousePos.x - origin.x, io.MousePos.y - origin.y, size.x, size.y);
        }
        auto* drawing = ImGui::GetWindowDrawList();
        drawing->AddText(ImVec2(origin.x + 14, origin.y + size.y - 28), IM_COL32(160,182,177,220),
            frame_.camera.orthographic ? "ORTHOGRAPHIC" : "PERSPECTIVE");
        if (playing_) { drawing->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(75,198,150,255), 0.0f, 2.0f); }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::assets() {
    if (ImGui::Begin("Assets")) {
        ImGui::TextDisabled("BUILT-IN GEOMETRY");
        ImGui::Separator();
        for (const auto* mesh : {"cube", "sphere", "plane", "quad"}) {
            ImGui::PushID(mesh);
            if (ImGui::Selectable((std::string("\uE7B8  ") + mesh).c_str(), false, 0, ImVec2(150, 27))) {
                if (!playing_) { addPrimitive(mesh); }
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("VELOS_PRIMITIVE", mesh, std::strlen(mesh) + 1);
                ImGui::TextUnformatted(mesh);
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void Editor::diagnostics() {
    if (ImGui::Begin("Performance")) {
        const auto statistics = renderer_.stats();
        ImGui::TextUnformatted(statistics.adapter.c_str());
        ImGui::Text("GPU scene %.2f ms  |  %u draws  |  %u triangles  |  %u visible", statistics.gpuMilliseconds,
            statistics.drawCalls, statistics.triangles, statistics.visibleObjects);
        ImGui::Text("GPU memory %.1f / %.1f MB  |  Undo %.1f KB", static_cast<double>(statistics.gpuUsage) / 1048576,
            static_cast<double>(statistics.gpuBudget) / 1048576, static_cast<double>(history_.bytes()) / 1024);
        const auto cache = renderer_.shaderCacheStats();
        ImGui::Text("Shader cache: %llu hits, %llu misses, %.1f KB", static_cast<unsigned long long>(cache.hits),
            static_cast<unsigned long long>(cache.misses), static_cast<double>(cache.bytes) / 1024);
        ImGui::PlotLines("##Frame times", frameTimes_.data(), static_cast<int>(frameTimes_.size()),
            static_cast<int>(frameIndex_ % frameTimes_.size()), nullptr, 0, 50, ImVec2(-1,55));
        ImGui::Checkbox("VSync", &vsync_);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &frame_.wireframe);
        ImGui::SameLine();
        changed_ |= ImGui::Checkbox("Shadows", &scene_.shadows);
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("Resolution scale", &resolutionScale_, 0.5f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("Exposure", &frame_.exposure, 0.3f, 3, "%.2f");
        ImGui::SetNextItemWidth(180);
        changed_ |= ImGui::SliderFloat("Ambient", &scene_.ambient, 0, 1.5f, "%.2f");
        int shadowSize = statistics.shadowResolution == 512 ? 0 : statistics.shadowResolution == 1024 ? 1 : 2;
        ImGui::SetNextItemWidth(180);
        if (ImGui::Combo("Shadow map", &shadowSize, "512\0 1024\0 2048\0")) { renderer_.setShadowResolution(512u << shadowSize); }
        if (ImGui::Button("Reload shaders")) {
            std::string error;
            if (renderer_.reloadShaders(error)) { log("Shaders reloaded."); }
            else { log(error, true); }
        }
    }
    ImGui::End();
}

void Editor::console() {
    if (ImGui::Begin("Console")) {
        if (ImGui::SmallButton("Clear")) { messages_.clear(); }
        ImGui::Separator();
        for (const auto& [message, error] : messages_) {
            if (error) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.47f,0.37f,1)); }
            ImGui::TextWrapped("%s", message.c_str());
            if (error) { ImGui::PopStyleColor(); }
        }
    }
    ImGui::End();
}

void Editor::addPrimitive(const std::string& mesh) {
    selected_ = scene_.addPrimitive(mesh);
    scene_.get<Transform>(selected_)->position = frame_.camera.orthographic ? XMFLOAT3{0,0,0} : XMFLOAT3{0,0.5f,0};
    changed_ = true;
}

void Editor::focusSelection() {
    if (!scene_.contains(selected_)) { return; }
    const auto world = scene_.worldMatrix(selected_);
    XMStoreFloat3(&frame_.camera.target, world.r[3]);
    frame_.camera.distance = 5;
    if (const auto* mesh = scene_.get<MeshRenderer>(selected_)) {
        if (const auto* bounds = renderer_.meshBounds(mesh->mesh)) {
            BoundingBox transformed;
            bounds->Transform(transformed, world);
            frame_.camera.target = transformed.Center;
            frame_.camera.distance = std::clamp(std::max({transformed.Extents.x, transformed.Extents.y, transformed.Extents.z}) * 5, 2.0f, 100.0f);
        }
    }
}

void Editor::pick(float horizontal, float vertical, float width, float height) {
    const auto nearPoint = XMVector3Unproject(XMVectorSet(horizontal, vertical, 0, 1), 0,0,width,height,0,1,
        frame_.camera.projection(), frame_.camera.view(), XMMatrixIdentity());
    const auto farPoint = XMVector3Unproject(XMVectorSet(horizontal, vertical, 1, 1), 0,0,width,height,0,1,
        frame_.camera.projection(), frame_.camera.view(), XMMatrixIdentity());
    const auto direction = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
    float nearest = FLT_MAX;
    selected_ = 0;
    for (const auto id : scene_.entities()) {
        if (!scene_.get<Identity>(id)->visible) { continue; }
        const auto* mesh = scene_.get<MeshRenderer>(id);
        if (!mesh) { continue; }
        const auto* bounds = renderer_.meshBounds(mesh->mesh);
        if (!bounds) { continue; }
        const auto world = scene_.worldMatrix(id);
        const auto inverse = XMMatrixInverse(nullptr, world);
        const auto localOrigin = XMVector3TransformCoord(nearPoint, inverse);
        const auto localDirection = XMVector3Normalize(XMVector3TransformNormal(direction, inverse));
        float distance = 0;
        if (bounds->Intersects(localOrigin, localDirection, distance)) {
            const auto hit = XMVector3TransformCoord(XMVectorAdd(localOrigin, XMVectorScale(localDirection, distance)), world);
            const float worldDistance = XMVectorGetX(XMVector3Length(XMVectorSubtract(hit, nearPoint)));
            if (worldDistance < nearest) { nearest = worldDistance; selected_ = id; }
        }
    }
}

std::filesystem::path Editor::chooseScenePath(bool save) {
    ComPtr<IFileDialog> dialog;
    const auto result = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) { log("Cannot open the Windows file dialog.", true); return {}; }
    const COMDLG_FILTERSPEC filters[] = {{L"Velos scene", L"*.velos"}, {L"JSON scene", L"*.json"}};
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetDefaultExtension(L"velos");
    if (save) { dialog->SetFileName((wide(scene_.name) + L".velos").c_str()); }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    if (dialog->Show(window_) != S_OK) { return {}; }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) { return {}; }
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { return {}; }
    const std::filesystem::path selected(path);
    CoTaskMemFree(path);
    return selected;
}

bool Editor::saveScene(bool choosePath) {
    if (playing_) { return false; }
    try {
        auto destination = scenePath_;
        if (choosePath || destination.empty()) { destination = chooseScenePath(true); }
        if (destination.empty()) { return false; }
        const auto state = scene_.serialize();
        writeTextAtomic(destination, state);
        scenePath_ = destination;
        savedState_ = state;
        autosaveSeconds_ = 0;
        log("Saved " + utf8(destination.filename().native()));
        return true;
    } catch (const std::exception& error) { log(error.what(), true); return false; }
}

void Editor::openScene(const std::filesystem::path& path) {
    try {
        Scene candidate;
        std::string error;
        if (!candidate.deserialize(readText(path), error)) { throw std::runtime_error(error); }
        stopPlay();
        scene_ = std::move(candidate);
        scenePath_ = path;
        savedState_ = scene_.serialize();
        selected_ = scene_.entities().empty() ? 0 : scene_.entities().front();
        history_.clear();
        autosaveSeconds_ = 0;
        log("Opened " + utf8(path.filename().native()));
    } catch (const std::exception& error) { log(error.what(), true); }
}

void Editor::requestClose() { pendingAction_ = 3; showUnsaved_ = true; }

void Editor::applyPendingAction() {
    stopPlay();
    history_.clear();
    if (pendingAction_ == 1) {
        scene_ = Scene{};
        scene_.name = "Untitled";
        selected_ = 0;
        scenePath_.clear();
        savedState_ = scene_.serialize();
    } else if (pendingAction_ == 2) {
        const auto path = chooseScenePath(false);
        if (!path.empty()) { openScene(path); }
    } else if (pendingAction_ == 3) { wantsClose_ = true; }
    pendingAction_ = 0;
}

void Editor::dialogs() {
    if (showUnsaved_) {
        showUnsaved_ = false;
        const auto authoredState = playing_ ? playSnapshot_ : scene_.serialize();
        if (authoredState != savedState_) { ImGui::OpenPopup("Unsaved scene"); }
        else { applyPendingAction(); }
    }
    if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes to the current scene?");
        if (ImGui::Button("Save", ImVec2(100,0))) {
            stopPlay();
            if (saveScene()) { applyPendingAction(); ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(100,0))) { applyPendingAction(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0))) { pendingAction_ = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void Editor::runAuthoringCheck() {
    const auto original = scene_.serialize();
    history_.begin(scene_);
    const auto added = scene_.addPrimitive("cube", "Smoke test cube");
    history_.commit(scene_, "Smoke create");
    std::string error;
    if (!history_.undo(scene_, error) || scene_.serialize() != original) { throw std::runtime_error("Editor undo smoke check failed."); }
    if (!history_.redo(scene_, error) || !scene_.contains(added)) { throw std::runtime_error("Editor redo smoke check failed."); }
    history_.undo(scene_, error);
    startPlay();
    for (int step = 0; step < 30; ++step) { update(1.0 / 60); }
    stopPlay();
    if (scene_.serialize() != original) { throw std::runtime_error("Editor play/stop restoration failed."); }
    history_.clear();
    log("Authoring smoke check passed.");
}

}