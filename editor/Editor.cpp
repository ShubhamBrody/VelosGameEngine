#include "Editor.h"
#include "platform/Files.h"
#include "physics/Gameplay.h"
#include "assets/Package.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <set>

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

bool vectorControl(const char* label, float* values, float speed, float minimum, float maximum,
    const char* format, ImGuiSliderFlags flags = 0) {
    if (ImGui::GetContentRegionAvail().x < 280) {
        ImGui::TextUnformatted(label);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-1);
        const bool changed = ImGui::DragFloat3("##Value", values, speed, minimum, maximum, format, flags);
        ImGui::PopID();
        return changed;
    }
    ImGui::SetNextItemWidth(-80);
    return ImGui::DragFloat3(label, values, speed, minimum, maximum, format, flags);
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
        : window_(window), renderer_(renderer), scene_(Scene::demo()), assistant_(isolated),
            geometryCache_(localDataDirectory() / L"cache" / L"geometry", 512 * 1024 * 1024),
            textureCache_(localDataDirectory() / L"cache" / L"textures", 512 * 1024 * 1024), isolated_(isolated) {
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
    if (textureImport_.valid() && textureImport_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto imported = textureImport_.get();
            auto* material = scene_.get<MeshRenderer>(imported.entity);
            if (!material || scenePath_.parent_path() != imported.project || scene_.revision() != imported.revision) {
                throw std::runtime_error("Texture import completed after the scene changed. Select the texture again to apply it.");
            }
            renderer_.addTexture(textureKey(imported.reference, imported.slot), imported.texture);
            history_.begin(scene_);
            material->textures[static_cast<std::size_t>(imported.slot)] = imported.reference;
            scene_.touch();
            history_.commit(scene_, "Assign material texture");
            log("Texture assigned: " + std::to_string(imported.texture.mips.front().width) + " x "
                + std::to_string(imported.texture.mips.front().height) + ", " + std::to_string(imported.texture.mips.size()) + " mip levels.");
        } catch (const std::exception& error) { log(error.what(), true); }
    }
    if (export_.valid() && export_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { log("Runtime exported to " + utf8(export_.get().native())); }
        catch (const std::exception& error) { log(error.what(), true); }
    }
    if (import_.valid() && import_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto asset = import_.get();
            if (scenePath_.parent_path() != asset.project) { throw std::runtime_error("Import completed for a project that is no longer open."); }
            renderer_.addMesh(asset.reference, asset.mesh);
            history_.begin(scene_);
            selected_ = scene_.addPrimitive(asset.reference, asset.name);
            history_.commit(scene_, "Import GLB");
            log("Imported " + asset.name + ": " + std::to_string(asset.mesh.vertices.size()) + " vertices.");
            focusSelection();
        } catch (const std::exception& error) { log(error.what(), true); }
        importStatus_.clear();
    }
    if (playing_ && !paused_) {
        const auto ticks = clock_.advance(elapsed);
        try {
            for (std::uint32_t index = 0; index < ticks.steps; ++index) {
                const bool inputEnabled = GetForegroundWindow() == window_ && !ImGui::GetIO().WantTextInput;
                const auto down = [&](int key) { return inputEnabled && (GetAsyncKeyState(key) & 0x8000) != 0; };
                driveBodies(scene_, physics_, frame_.camera, static_cast<float>(down('D')) - static_cast<float>(down('A')),
                    static_cast<float>(down('W')) - static_cast<float>(down('S')));
                scene_.tick(clock_.stepSeconds());
                physics_.step(scene_, static_cast<float>(clock_.stepSeconds()));
            }
        } catch (const std::exception& error) {
            log(error.what(), true);
            stopPlay();
        }
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
    if (import_.valid() || textureImport_.valid()) { log("Wait for the current import before starting simulation."); return; }
    assistant_.cancel();
    if (playing_) { paused_ = !paused_; return; }
    try { physics_.start(scene_); }
    catch (const std::exception& error) { log(error.what(), true); return; }
    playSnapshot_ = scene_.serialize();
    playing_ = true;
    paused_ = false;
    history_.cancel();
    clock_.reset();
    log("Simulation started.");
}

void Editor::stopPlay() {
    if (!playing_) { return; }
    physics_.stop();
    std::string error;
    if (!scene_.deserialize(playSnapshot_, error)) { log("Cannot restore authored scene: " + error, true); return; }
    playing_ = paused_ = false;
    playSnapshot_.clear();
    clock_.reset();
    log("Simulation stopped. Authored scene restored.");
}

void Editor::stepPlay() {
    if (!playing_) { startPlay(); }
    if (!playing_) { return; }
    paused_ = true;
    scene_.tick(clock_.stepSeconds());
    physics_.step(scene_, static_cast<float>(clock_.stepSeconds()));
}

void Editor::buildLayout() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto dockId = ImGui::GetID("VelosDock");
    if (!layoutBuilt_ && (isolated_ || forceLayout_ || !ImGui::DockBuilderGetNode(dockId))) {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 43));
        auto center = dockId;
        const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
        auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.255f, nullptr, &center);
        const auto assistant = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.46f, nullptr, &right);
        const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Scene", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Assistant", assistant);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Performance", bottom);
        ImGui::DockBuilderFinish(dockId);
    }
    layoutBuilt_ = true;
    forceLayout_ = false;
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
    if (frame_.camera.orthographic != scene_.twoDimensional) { frame_.camera.set2D(scene_.twoDimensional); }
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
    assistant_.draw(scene_, scenePath_, playing_);
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
    if (!playing_ && !ImGui::IsAnyItemActive() && !gizmoActive_) { history_.commit(scene_, "Scene edit", changed_); }
    extractScene(scene_, frame_, selected_, grid_);
    if (dirtyRevision_ != scene_.revision() && !playing_) {
        dirty_ = scene_.serialize() != savedState_;
        dirtyRevision_ = scene_.revision();
    }
    const auto title = L"Velos | " + wide(scene_.name) + (dirty_ && !playing_ ? L" *" : L"");
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
        if (ImGui::MenuItem("Spotlight")) {
            selected_ = scene_.create("Spotlight");
            Light light;
            light.kind = LightKind::Spot;
            light.direction = {0,-1,0};
            light.intensity = 30;
            scene_.set<Light>(selected_, light);
            scene_.get<Transform>(selected_)->position = {0,4,0};
            changed_ = true;
        }
        if (ImGui::MenuItem("Empty entity")) { selected_ = scene_.create("Entity"); changed_ = true; }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(playing_ || import_.valid() || export_.valid());
    if (ImGui::Button("\uE898  Export", ImVec2(90,28))) { exportRuntime(); }
    ImGui::EndDisabled();
    ImGui::SameLine(std::max(500.0f, viewport->WorkSize.x * 0.45f));
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
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", identity.name.c_str()); }
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
            if (identity.name.size() > 128) { identity.name.resize(128); }
            if (ImGui::IsItemDeactivatedAfterEdit() && identity.name.empty()) { identity.name = "Entity"; }
            ImGui::TextDisabled("Entity %llu", static_cast<unsigned long long>(selected_));
            changed_ |= ImGui::Checkbox("Visible", &identity.visible);
            ImGui::SeparatorText("Transform");
            auto& transform = *scene_.get<Transform>(selected_);
            ImGui::PushItemWidth(-80);
            changed_ |= vectorControl("Position", &transform.position.x, 0.02f, -10000, 10000, "%.3g");
            XMFLOAT4X4 local;
            XMStoreFloat4x4(&local, transform.matrix());
            float translation[3]{};
            float rotation[3]{};
            float scale[3]{};
            ImGuizmo::DecomposeMatrixToComponents(&local._11, translation, rotation, scale);
            if (vectorControl("Rotation", rotation, 0.5f, -360, 360, "%.3g")) {
                XMStoreFloat4(&transform.rotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation[0]),
                    XMConvertToRadians(rotation[1]), XMConvertToRadians(rotation[2])));
                changed_ = true;
            }
            changed_ |= vectorControl("Scale", &transform.scale.x, 0.01f, 0.01f, 1000, "%.3g", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopItemWidth();
            if (auto* mesh = scene_.get<MeshRenderer>(selected_)) {
                ImGui::SeparatorText("Material");
                ImGui::TextDisabled("%s", mesh->mesh.c_str());
                changed_ |= ImGui::ColorEdit4("Albedo", &mesh->color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::SetNextItemWidth(-90);
                changed_ |= ImGui::SliderFloat("Roughness", &mesh->roughness, 0.04f, 1, "%.2f");
                ImGui::SetNextItemWidth(-90);
                changed_ |= ImGui::SliderFloat("Metallic", &mesh->metallic, 0, 1, "%.2f");
                changed_ |= ImGui::Checkbox("Cast shadow", &mesh->castShadow);
                changed_ |= ImGui::Checkbox("Unlit", &mesh->unlit);
                changed_ |= ImGui::Checkbox("Double sided", &mesh->doubleSided);
                int surface = static_cast<int>(mesh->surface);
                ImGui::SetNextItemWidth(-85);
                if (ImGui::Combo("Surface", &surface, "Opaque\0Alpha clip\0Transparent\0")) { mesh->surface = static_cast<SurfaceMode>(surface); changed_ = true; }
                if (mesh->surface == SurfaceMode::Masked) {
                    ImGui::SetNextItemWidth(-85);
                    changed_ |= ImGui::SliderFloat("Alpha cutoff", &mesh->alphaCutoff, 0, 1);
                }
                if (ImGui::CollapsingHeader("Texture maps", ImGuiTreeNodeFlags_DefaultOpen)) {
                    constexpr const char* names[] = {"Albedo", "Normal", "ORM", "Emissive"};
                    for (std::size_t slot = 0; slot < mesh->textures.size(); ++slot) {
                        ImGui::PushID(static_cast<int>(slot));
                        ImGui::TextUnformatted(names[slot]);
                        ImGui::SameLine(85);
                        ImGui::BeginDisabled(textureImport_.valid() || import_.valid());
                        if (iconButton("\uE8E5", "Import texture")) { chooseTexture(static_cast<TextureSlot>(slot)); }
                        ImGui::SameLine();
                        ImGui::BeginDisabled(mesh->textures[slot].empty());
                        if (iconButton("\uE74D", "Remove texture")) { mesh->textures[slot].clear(); changed_ = true; }
                        ImGui::EndDisabled();
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", mesh->textures[slot].empty() ? "None" : "Set");
                        if (ImGui::IsItemHovered() && !mesh->textures[slot].empty()) { ImGui::SetTooltip("%s", mesh->textures[slot].c_str()); }
                        ImGui::PopID();
                    }
                    ImGui::SetNextItemWidth(-85);
                    changed_ |= ImGui::DragFloat2("UV scale", &mesh->uvScale.x, 0.02f, -100, 100, "%.2f");
                    ImGui::SetNextItemWidth(-85);
                    changed_ |= ImGui::DragFloat2("UV offset", &mesh->uvOffset.x, 0.01f, -100, 100, "%.2f");
                    ImGui::SetNextItemWidth(-85);
                    changed_ |= ImGui::SliderFloat("Normal scale", &mesh->normalStrength, 0, 4, "%.2f");
                    changed_ |= ImGui::ColorEdit3("Emission", &mesh->emission.x, ImGuiColorEditFlags_NoInputs);
                    ImGui::SetNextItemWidth(-85);
                    changed_ |= ImGui::SliderFloat("Emission gain", &mesh->emissionStrength, 0, 20, "%.2f");
                    if (textureImport_.valid()) { ImGui::TextDisabled("Cooking texture..."); }
                }
            }
            if (auto* light = scene_.get<Light>(selected_)) {
                ImGui::SeparatorText("Light");
                int kind = static_cast<int>(light->kind);
                ImGui::SetNextItemWidth(-90);
                if (ImGui::Combo("Type", &kind, "Directional\0Point\0Spot\0")) { light->kind = static_cast<LightKind>(kind); changed_ = true; }
                changed_ |= ImGui::ColorEdit3("Color", &light->color.x, ImGuiColorEditFlags_NoInputs);
                ImGui::SetNextItemWidth(-90);
                changed_ |= ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0, 1000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                if (light->kind != LightKind::Point) {
                    auto direction = light->direction;
                    if (vectorControl("Direction", &direction.x, 0.01f, -1, 1, "%.3g")) {
                        if (XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&direction))) > 1e-5f) { light->direction = direction; changed_ = true; }
                    }
                }
                if (light->kind != LightKind::Directional) {
                    changed_ |= ImGui::DragFloat("Range", &light->range, 0.1f, 0.1f, 10000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                }
                if (light->kind == LightKind::Spot) {
                    ImGui::SetNextItemWidth(-90);
                    changed_ |= ImGui::SliderFloat("Inner cone", &light->innerAngle, 0.1f, light->outerAngle - 0.1f, "%.1f");
                    ImGui::SetNextItemWidth(-90);
                    changed_ |= ImGui::SliderFloat("Outer cone", &light->outerAngle, light->innerAngle + 0.1f, 179, "%.1f");
                }
            }
            if (auto* spin = scene_.get<Spin>(selected_)) {
                ImGui::SeparatorText("Rotation behavior");
                changed_ |= ImGui::DragFloat("Degrees / sec", &spin->degreesPerSecond, 0.5f, -3600, 3600);
                if (ImGui::SmallButton("Remove rotation behavior")) { scene_.remove<Spin>(selected_); changed_ = true; }
            } else {
                const auto* body = scene_.get<RigidBody>(selected_);
                ImGui::BeginDisabled(body != nullptr);
                if (ImGui::Button("Add rotation behavior", ImVec2(-1,0))) {
                    scene_.set<Spin>(selected_);
                    changed_ = true;
                }
                ImGui::EndDisabled();
            }
            if (auto* body = scene_.get<RigidBody>(selected_)) {
                ImGui::SeparatorText("Rigid body");
                int motion = body->motion == BodyMotion::Static ? 0 : 1;
                if (ImGui::Combo("Motion", &motion, "Static\0Dynamic\0")) {
                    body->motion = motion == 0 ? BodyMotion::Static : BodyMotion::Dynamic;
                    if (motion == 0) { scene_.remove<KeyboardDrive>(selected_); }
                    else { scene_.remove<Spin>(selected_); }
                    changed_ = true;
                }
                changed_ |= ImGui::DragFloat("Mass (kg)", &body->mass, 0.1f, 0.01f, 100000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed_ |= ImGui::SliderFloat("Restitution", &body->restitution, 0, 1);
                if (ImGui::SmallButton("Remove rigid body")) {
                    scene_.remove<RigidBody>(selected_);
                    scene_.remove<KeyboardDrive>(selected_);
                    changed_ = true;
                }
                if (auto* drive = scene_.get<KeyboardDrive>(selected_)) {
                    ImGui::SeparatorText("Keyboard drive");
                    changed_ |= ImGui::DragFloat("Speed", &drive->speed, 0.1f, 0.1f, 100, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                    if (ImGui::SmallButton("Remove keyboard drive")) { scene_.remove<KeyboardDrive>(selected_); changed_ = true; }
                } else {
                    const auto* currentBody = scene_.get<RigidBody>(selected_);
                    ImGui::BeginDisabled(!currentBody || currentBody->motion != BodyMotion::Dynamic);
                    if (ImGui::Button("Add keyboard drive", ImVec2(-1,0))) {
                        scene_.set<KeyboardDrive>(selected_);
                        changed_ = true;
                    }
                    ImGui::EndDisabled();
                }
            } else if (const auto* mesh = scene_.get<MeshRenderer>(selected_);
                mesh && (mesh->mesh == "cube" || mesh->mesh == "sphere" || mesh->mesh == "plane")) {
                if (ImGui::Button("Add rigid body", ImVec2(-1,0))) {
                    scene_.set<RigidBody>(selected_, {mesh->mesh == "plane" ? BodyMotion::Static : BodyMotion::Dynamic});
                    scene_.remove<Spin>(selected_);
                    changed_ = true;
                }
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
        if (ImGui::Combo("##Camera mode", &mode, "3D\0 2D\0")) {
            frame_.camera.set2D(mode == 1);
            scene_.twoDimensional = mode == 1;
            changed_ = true;
        }
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
        ImGui::BeginDisabled(playing_ || import_.valid());
        if (ImGui::Button("\uE8E5  Import GLB")) { chooseImport(); }
        ImGui::EndDisabled();
        if (import_.valid()) { ImGui::SameLine(); ImGui::TextDisabled("%s", importStatus_.c_str()); }
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
        std::set<std::string> listed;
        for (const auto id : scene_.entities()) {
            const auto* mesh = scene_.get<MeshRenderer>(id);
            if (!mesh || !mesh->mesh.starts_with("Assets/") || !listed.insert(mesh->mesh).second) { continue; }
            if (ImGui::Selectable((scene_.get<Identity>(id)->name + "##asset" + mesh->mesh).c_str(), selected_ == id)) { selected_ = id; }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", mesh->mesh.c_str()); }
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
        ImGui::Text("Camera/shadow draws %u / %u | Frustum culled %u | LOD triangles saved %u", statistics.cameraDraws,
            statistics.shadowDraws, statistics.culledObjects, statistics.lodTrianglesSaved);
        ImGui::Checkbox("Instance batching", &frame_.instancing);
        ImGui::SameLine();
        ImGui::Checkbox("Mesh LODs", &frame_.lods);
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("LOD quality bias", &frame_.lodBias, 0.25f, 3, "%.2f");
        ImGui::Text("GPU memory %.1f / %.1f MB  |  Undo %.1f KB", static_cast<double>(statistics.gpuUsage) / 1048576,
            static_cast<double>(statistics.gpuBudget) / 1048576, static_cast<double>(history_.bytes()) / 1024);
        const auto cache = renderer_.shaderCacheStats();
        ImGui::Text("Shader cache: %llu hits, %llu misses, %.1f KB", static_cast<unsigned long long>(cache.hits),
            static_cast<unsigned long long>(cache.misses), static_cast<double>(cache.bytes) / 1024);
        const auto geometry = geometryCache_.stats();
        const auto textures = textureCache_.stats();
        ImGui::Text("GPU textures: %u, %.1f MB | Texture cache: %llu hits / %llu misses", statistics.textureCount,
            static_cast<double>(statistics.textureBytes) / 1048576, static_cast<unsigned long long>(textures.hits), static_cast<unsigned long long>(textures.misses));
        ImGui::Text("Geometry cache: %llu hits, %llu misses | Physics: %zu bodies", static_cast<unsigned long long>(geometry.hits),
            static_cast<unsigned long long>(geometry.misses), physics_.bodyCount());
        ImGui::PlotLines("##Frame times", frameTimes_.data(), static_cast<int>(frameTimes_.size()),
            static_cast<int>(frameIndex_ % frameTimes_.size()), nullptr, 0, 50, ImVec2(-1,55));
        ImGui::Checkbox("VSync", &vsync_);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &frame_.wireframe);
        ImGui::SameLine();
        changed_ |= ImGui::Checkbox("Shadows", &scene_.shadows);
        ImGui::BeginDisabled(!statistics.rayTracingSupported);
        changed_ |= ImGui::Checkbox("Ray-traced sun shadows", &scene_.rayTracedShadows);
        ImGui::EndDisabled();
        ImGui::Text("Shadow path: %s", statistics.shadowStatus.c_str());
        ImGui::Text("DXR: %.1f MB", static_cast<double>(statistics.rayTracingBytes) / (1024 * 1024));
        int rayBudget = static_cast<int>(statistics.rayTracingBudget / (1024 * 1024));
        ImGui::SetNextItemWidth(-90);
        if (ImGui::InputInt("DXR budget MB", &rayBudget, 16, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
            renderer_.setRayTracingBudget(static_cast<std::uint64_t>(std::clamp(rayBudget, 0, 256)) * 1024 * 1024);
        }
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
    if (playing_ || textureImport_.valid()) { return false; }
    try {
        auto destination = scenePath_;
        if (choosePath || destination.empty()) { destination = chooseScenePath(true); }
        if (destination.empty()) { return false; }
        if (!scenePath_.empty() && destination.parent_path() != scenePath_.parent_path()) {
            for (const auto& reference : scene_.assetReferences()) {
                const auto source = projectAssetPath(scenePath_.parent_path(), reference);
                const auto target = projectAssetPath(destination.parent_path(), reference);
                const auto bytes = readBytes(source);
                if (std::filesystem::exists(target) && sha256(readBytes(target)) != sha256(bytes)) {
                    throw std::runtime_error("Save As asset conflict; choose an empty destination project folder.");
                }
                if (!std::filesystem::exists(target)) { writeAtomic(target, bytes); }
            }
        }
        const auto state = scene_.serialize();
        writeTextAtomic(destination, state);
        scenePath_ = destination;
        savedState_ = state;
        dirty_ = false;
        dirtyRevision_ = scene_.revision();
        autosaveSeconds_ = 0;
        log("Saved " + utf8(destination.filename().native()));
        return true;
    } catch (const std::exception& error) { log(error.what(), true); return false; }
}

bool Editor::openScene(const std::filesystem::path& path) {
    try {
        if (import_.valid() || textureImport_.valid()) { throw std::runtime_error("Wait for the current import before opening another project."); }
        Scene candidate;
        std::string error;
        if (!candidate.deserialize(readText(path), error)) { throw std::runtime_error(error); }
        for (const auto id : candidate.entities()) {
            const auto* mesh = candidate.get<MeshRenderer>(id);
            if (!mesh) { continue; }
            if (!renderer_.hasMesh(mesh->mesh)) {
                if (!mesh->mesh.starts_with("Assets/")) { throw std::runtime_error("Unknown mesh reference: " + mesh->mesh); }
                const auto source = projectAssetPath(path.parent_path(), mesh->mesh);
                renderer_.addMesh(mesh->mesh, loadGlb(source, geometryCache_));
            }
            for (std::size_t slot = 0; slot < mesh->textures.size(); ++slot) {
                const auto& reference = mesh->textures[slot];
                if (reference.empty()) { continue; }
                const auto type = static_cast<TextureSlot>(slot);
                const auto key = textureKey(reference, type);
                if (!renderer_.hasTexture(key)) {
                    renderer_.addTexture(key, loadTexture(projectAssetPath(path.parent_path(), reference), {type}, textureCache_));
                }
            }
        }
        stopPlay();
        assistant_.clearConversation();
        scene_ = std::move(candidate);
        frame_.camera.set2D(scene_.twoDimensional);
        scenePath_ = path;
        savedState_ = scene_.serialize();
        selected_ = scene_.entities().empty() ? 0 : scene_.entities().front();
        history_.clear();
        autosaveSeconds_ = 0;
        log("Opened " + utf8(path.filename().native()));
        return true;
    } catch (const std::exception& error) { log(error.what(), true); return false; }
}

void Editor::requestClose() { pendingAction_ = 3; showUnsaved_ = true; }

void Editor::applyPendingAction() {
    if (import_.valid() || textureImport_.valid()) { log("Wait for the current import before switching projects."); pendingAction_ = 0; return; }
    stopPlay();
    history_.clear();
    if (pendingAction_ == 1) {
        assistant_.clearConversation();
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

void Editor::chooseImport() {
    if (scenePath_.empty() && !saveScene()) { return; }
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        log("Cannot open the model import dialog.", true);
        return;
    }
    const COMDLG_FILTERSPEC filter{L"Static glTF binary model", L"*.glb"};
    dialog->SetFileTypes(1, &filter);
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
    if (dialog->Show(window_) != S_OK) { return; }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) { return; }
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { return; }
    const std::filesystem::path source(path);
    CoTaskMemFree(path);
    importGlb(source);
}

void Editor::importGlb(const std::filesystem::path& path) {
    if (import_.valid() || playing_ || scenePath_.empty()) { log("Save the scene and finish the active operation before importing.", true); return; }
    const auto project = scenePath_.parent_path();
    importStatus_ = "Importing " + utf8(path.filename().native());
    import_ = std::async(std::launch::async, [this, path, project] {
        const auto bytes = readBytes(path, 64 * 1024 * 1024);
        const auto digest = sha256(bytes);
        auto geometry = loadGlb(bytes, geometryCache_);
        const auto reference = "Assets/" + digest + ".glb";
        const auto target = projectAssetPath(project, reference);
        if (std::filesystem::exists(target) && sha256(readBytes(target)) != digest) {
            throw std::runtime_error("An existing project asset conflicts with the imported content hash.");
        }
        if (!std::filesystem::exists(target)) { writeAtomic(target, bytes); }
        return ImportedAsset{std::move(geometry), reference, utf8(path.stem().native()), project};
    });
}

void Editor::chooseTexture(TextureSlot slot) {
    if (textureImport_.valid() || import_.valid() || playing_ || !scene_.get<MeshRenderer>(selected_)) { return; }
    if (scenePath_.empty() && !saveScene()) { return; }
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) { log("Cannot open texture dialog.", true); return; }
    const COMDLG_FILTERSPEC filter{L"Material texture", L"*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp;*.tif;*.tiff"};
    dialog->SetFileTypes(1, &filter);
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
    if (dialog->Show(window_) != S_OK) { return; }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) { return; }
    PWSTR selected = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) { return; }
    const std::filesystem::path source(selected);
    CoTaskMemFree(selected);
    const auto project = scenePath_.parent_path();
    const auto entity = selected_;
    const auto revision = scene_.revision();
    textureImport_ = std::async(std::launch::async, [this, source, project, entity, slot, revision] {
        const auto bytes = readBytes(source);
        const auto reference = "Assets/" + sha256(bytes) + utf8(source.extension().native());
        auto texture = loadTexture(source, {slot}, textureCache_);
        const auto destination = projectAssetPath(project, reference);
        if (!std::filesystem::exists(destination)) { writeAtomic(destination, bytes); }
        else if (sha256(readBytes(destination)) != sha256(bytes)) { throw std::runtime_error("Existing project texture conflicts with imported content."); }
        return ImportedTexture{std::move(texture), reference, entity, slot, project, revision};
    });
}

void Editor::exportRuntime() {
    if (playing_ || import_.valid() || export_.valid()) { return; }
    if (!saveScene()) { return; }
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        log("Cannot open the export folder dialog.", true);
        return;
    }
    dialog->SetTitle(L"Choose an empty runtime export folder");
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    if (dialog->Show(window_) != S_OK) { return; }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) { return; }
    PWSTR selected = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) { return; }
    const std::filesystem::path destination(selected);
    CoTaskMemFree(selected);
    const auto source = scenePath_;
    const auto binaries = executableDirectory();
    log("Exporting standalone runtime...");
    export_ = std::async(std::launch::async, [source, destination, binaries] {
        packageScene(source, destination, binaries);
        verifyPackage(destination);
        return destination;
    });
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