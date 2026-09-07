#include "Editor.h"
#include "assets/Package.h"
#include "platform/Files.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>

namespace velos {
namespace {
using Json = nlohmann::json;
using automation::Error;

void only(const Json& values, std::initializer_list<std::string_view> allowed) {
    if (!values.is_object()) { throw Error("invalid_arguments", "Tool arguments must be an object."); }
    for (const auto& field : values.items()) {
        if (std::find(allowed.begin(), allowed.end(), field.key()) == allowed.end()) { throw Error("unknown_field", "Unknown argument: " + field.key()); }
    }
}

float number(const Json& source, const char* field, float current, float minimum, float maximum) {
    if (!source.contains(field)) { return current; }
    if (!source.at(field).is_number()) { throw Error("invalid_arguments", std::string(field) + " must be a number."); }
    const auto value = source.at(field).get<double>();
    if (!std::isfinite(value) || value < minimum || value > maximum) { throw Error("invalid_arguments", std::string(field) + " is outside its supported range."); }
    return static_cast<float>(value);
}

DirectX::XMFLOAT3 position(const Json& value) {
    if (!value.is_array() || value.size() != 3) { throw Error("invalid_arguments", "Expected a vector of three numbers."); }
    Json fields{{"x", value[0]}, {"y", value[1]}, {"z", value[2]}};
    return {number(fields,"x",0,-1e6f,1e6f), number(fields,"y",0,-1e6f,1e6f), number(fields,"z",0,-1e6f,1e6f)};
}

Json cameraData(const Camera& camera) {
    return {{"target", {camera.target.x,camera.target.y,camera.target.z}}, {"yaw_degrees", DirectX::XMConvertToDegrees(camera.yaw)},
        {"pitch_degrees", DirectX::XMConvertToDegrees(camera.pitch)}, {"distance", camera.distance}, {"orthographic", camera.orthographic}, {"aspect", camera.aspect}};
}

}

void Editor::enableAutomation(const std::string& pipe, const std::filesystem::path& root, bool readOnly) {
    if (root.empty() || !std::filesystem::is_directory(root)) { throw Error("invalid_workspace", "Automation requires an existing explicit workspace directory."); }
    if (control_) { throw Error("already_enabled", "Editor automation is already enabled."); }
    automationRoot_ = std::filesystem::canonical(root);
    automationReadOnly_ = readOnly;
    if (!scenePath_.empty()) {
        const auto path = std::filesystem::absolute(scenePath_).lexically_normal();
        static_cast<void>(automationPath(utf8(path.lexically_relative(automationRoot_).generic_wstring()), ".velos"));
        prepareAutomationAssets(scene_, path.parent_path());
        scenePath_ = path;
    }
    control_ = std::make_unique<automation::ControlPipe>(pipe, [window = window_] { PostMessageW(window, WM_APP + 37, 0, 0); });
    log("MCP control enabled: " + pipe + (readOnly ? " (read only)" : " (current Windows user)"));
}

void Editor::pumpAutomation() {
    if (!control_) { return; }
    if (controlWork_.valid() && controlWork_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto& job = controlJobs_.back();
        try {
            auto finish = controlWork_.get();
            if (cancelControlJob_) { job.state = "cancelled"; }
            else { job.result = finish(); job.state = "succeeded"; }
        } catch (const std::exception& error) { job.state = "failed"; job.error = error.what(); log("MCP job " + job.id + ": " + job.error, true); }
        cancelControlJob_ = false;
    }
    control_->pump([this](std::string_view method, const Json& params) {
        try { return automationRequest(method, params); }
        catch (const std::exception& error) { log("MCP " + std::string(method) + ": " + error.what(), true); throw; }
    }, 1);
}

std::filesystem::path Editor::automationPath(std::string_view relative, std::string_view extension) const {
    if (automationRoot_.empty() || relative.empty() || relative.size() > 2048 || relative.find('\0') != std::string_view::npos
        || relative.find_first_of(":\\*?\"<>|") != std::string_view::npos) { throw Error("invalid_path", "Paths must use forward slashes and stay relative to the automation workspace."); }
    const std::filesystem::path local = wide(relative);
    if (local.is_absolute() || local.has_root_name()) { throw Error("invalid_path", "Absolute paths are not accepted by automation tools."); }
    for (const auto& part : local) {
        const auto text = utf8(part.native());
        if (text == ".") { continue; }
        if (text.empty() || text.front() == '.' || text.back() == '.' || text.back() == ' ') { throw Error("invalid_path", "Hidden, traversal and ambiguous path components are not allowed."); }
        auto stem = utf8(part.stem().native());
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char letter) { return static_cast<char>(std::toupper(letter)); });
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$"
            || (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9')) {
            throw Error("invalid_path", "Windows device paths are not supported.");
        }
    }
    const auto path = projectAssetPath(automationRoot_, relative);
    if (!extension.empty() && path.extension() != wide(extension)) { throw Error("invalid_path", "This operation requires a " + std::string(extension) + " file."); }
    return path;
}

void Editor::requireAutomationEdit(const Json& params, bool requireRevision) const {
    if (automationReadOnly_) { throw Error("read_only", "This automation session was started read-only."); }
    if (playing_) { throw Error("simulation_active", "Stop simulation before modifying authored scene data."); }
    if (import_.valid() || textureImport_.valid() || export_.valid() || controlWork_.valid() || history_.pending() || ImGui::IsAnyItemActive() || showUnsaved_ || pendingAction_ != 0) {
        throw Error("editor_busy", "Finish the active editor operation before automation modifies the project.");
    }
    if (requireRevision && !params.contains("expected_revision")) { throw Error("revision_required", "Read the scene first and supply expected_revision."); }
    if (params.contains("expected_revision") && automation::identifier(params.at("expected_revision"), true) != scene_.revision()) {
        throw Error("revision_conflict", "The scene has changed; read its current revision before retrying.");
    }
}

void Editor::prepareAutomationAssets(const Scene& candidate, const std::filesystem::path& project) {
    for (const auto& reference : candidate.assetReferences()) {
        const auto source = projectAssetPath(project, reference);
        const auto relative = utf8(source.lexically_relative(automationRoot_).generic_wstring());
        static_cast<void>(automationPath(relative));
        if (!std::filesystem::is_regular_file(source)) { throw Error("asset_not_found", "Referenced project asset does not exist: " + reference); }
    }
    for (const auto entity : candidate.entities()) {
        const auto* mesh = candidate.get<MeshRenderer>(entity);
        if (!mesh) { continue; }
        if (!renderer_.hasMesh(mesh->mesh)) { renderer_.addMesh(mesh->mesh, loadGlb(projectAssetPath(project, mesh->mesh), geometryCache_)); }
        for (std::size_t slot = 0; slot < mesh->textures.size(); ++slot) {
            if (mesh->textures[slot].empty()) { continue; }
            const auto type = static_cast<TextureSlot>(slot);
            const auto key = textureKey(mesh->textures[slot], type);
            if (!renderer_.hasTexture(key)) { renderer_.addTexture(key, loadTexture(projectAssetPath(project, mesh->textures[slot]), {type}, textureCache_)); }
        }
    }
}

nlohmann::json Editor::automationRequest(std::string_view method, const Json& params) {
    if (!control_) { throw Error("disabled", "Editor automation is disabled."); }
    if (method == "system.capabilities") {
        only(params,{});
        return {{"api_version",1},{"transport","MCP stdio -> Windows current-user named pipe"},{"read_only",automationReadOnly_},
            {"components",automation::componentCatalog()},{"behavior_nodes",behaviorNodeCatalog()},
            {"features",{"atomic_scene_transactions","scene_generation","local_world_transforms","component_fields","materials","lights","physics",
                "model_texture_import","save_open","undo_redo","simulation_input","gameplay_variables","executable_graphs","class_scene_graphs","gpu_capture","standalone_export"}},
            {"formats",{"static_glb","png","jpeg","bmp","tiff","tga","dds"}},
            {"limits",{{"entities",10000},{"batch_operations",256},{"scene_bytes",8 * 1024 * 1024},{"undo_bytes",history_.budgetBytes()},
                {"message_bytes",automation::ControlPipe::maximumMessageBytes},{"variables",64},{"graph_nodes",128},{"scene_graph_nodes",2048},{"local_lights",16}}},
            {"coordinate_conventions",{{"position","XYZ engine units"},{"quaternion","XYZW"},{"euler_degrees","pitch, yaw, roll"},{"graph_movement","local units/degrees per second"}}},
            {"not_implemented",{"audio","skeletal_animation","terrain","navigation","network_multiplayer","CSharp_scripts","Unreal_Blueprints","remote_network_transport"}},
            {"not_exposed",{"arbitrary_shell","arbitrary_code_execution","credential_store","filesystem_outside_workspace","AI_provider_secrets"}}};
    }
    if (method == "system.status") {
        only(params, {});
        return {{"engine", "Velos"}, {"api_version", 1}, {"revision", std::to_string(scene_.revision())},
            {"scene", scene_.name}, {"entities", scene_.entities().size()}, {"scene_path", scenePath_.empty() ? "" : utf8(scenePath_.lexically_relative(automationRoot_).generic_wstring())},
            {"read_only", automationReadOnly_}, {"playing", playing_}, {"paused", paused_}, {"selected", std::to_string(selected_)},
            {"dirty", (playing_ ? playSnapshot_ : scene_.serialize()) != savedState_}, {"can_undo", history_.canUndo()}, {"can_redo", history_.canRedo()},
            {"rendered_revision", std::to_string(renderedRevision_)}, {"rendered_frames", renderedFrames_}, {"graph",graphs_.state()}, {"control", control_->statistics()}};
    }
    if (method == "classes.list") { only(params, {}); return {{"classes", automation::componentCatalog()}}; }
    if (method == "scene.get") {
        only(params, {"offset", "limit"});
        return automation::describeScene(scene_, params.value("offset", std::size_t(0)), params.value("limit", std::size_t(100)));
    }
    if (method == "entity.get") {
        only(params, {"entity"});
        const auto entity = automation::identifier(params.at("entity"));
        const auto found = std::find(scene_.entities().begin(), scene_.entities().end(), entity);
        if (found == scene_.entities().end()) { throw Error("entity_not_found", "Entity does not exist."); }
        auto result = automation::describeScene(scene_, static_cast<std::size_t>(found - scene_.entities().begin()), 1);
        DirectX::XMFLOAT4X4 world;
        DirectX::XMStoreFloat4x4(&world, scene_.worldMatrix(entity));
        result["world_matrix"] = Json::array();
        for (const auto& row : world.m) { result["world_matrix"].push_back({row[0],row[1],row[2],row[3]}); }
        return result;
    }
    if (method == "scene.transaction") {
        requireAutomationEdit(params);
        const auto result = automation::executeSceneEdit(scene_, history_, params, [this](const Scene& candidate) {
            prepareAutomationAssets(candidate, scenePath_.empty() ? automationRoot_ : scenePath_.parent_path());
        });
        if (!scene_.contains(selected_)) { selected_ = 0; }
        return result;
    }
    if (method == "entity.create" || method == "entity.patch" || method == "entity.transform" || method == "entity.reparent"
        || method == "entity.delete" || method == "entity.duplicate" || method == "scene.settings") {
        Json operation = params;
        const auto revision = operation.at("expected_revision");
        operation.erase("expected_revision");
        const auto dryRun = operation.value("dry_run", false);
        operation.erase("dry_run");
        operation["op"] = std::string(method.substr(method.find('.') + 1));
        return automationRequest("scene.transaction", {{"expected_revision", revision}, {"dry_run", dryRun},
            {"label", "MCP " + std::string(method)}, {"operations", Json::array({std::move(operation)})}});
    }
    if (method == "component.set" || method == "component.remove") {
        only(params, {"entity", "component", "value", "expected_revision", "dry_run"});
        const auto component = params.at("component").get<std::string>();
        if (component != "mesh" && component != "light" && component != "body" && component != "spin" && component != "keyboardDrive" && component != "behavior") {
            throw Error("invalid_component", "Optional component keys are mesh, light, body, spin, keyboardDrive and behavior.");
        }
        return automationRequest("entity.patch", {{"entity", params.at("entity")}, {"expected_revision", params.at("expected_revision")},
            {"dry_run", params.value("dry_run", false)}, {"fields", {{component, method == "component.remove" ? Json(nullptr) : params.at("value")}}}});
    }
    if (method == "scene.generate") {
        only(params, {"expected_revision", "layout", "count", "columns", "spacing", "origin", "primitive", "name", "fields", "dry_run"});
        const auto count = params.value("count", 1);
        const auto columns = params.value("columns", 8);
        const auto layout = params.value("layout", std::string("grid"));
        if (count < 1 || count > 256 || columns < 1 || columns > 256 || (layout != "grid" && layout != "ring")) { throw Error("invalid_arguments", "Generation requires grid/ring layout, 1-256 objects and 1-256 columns."); }
        const auto spacing = number(params,"spacing",2,0.01f,1000);
        const auto origin = params.contains("origin") ? position(params.at("origin")) : DirectX::XMFLOAT3{0,0,0};
        Json operations = Json::array();
        for (int index = 0; index < count; ++index) {
            const auto angle = DirectX::XM_2PI * static_cast<float>(index) / static_cast<float>(count);
            auto fields = params.value("fields", Json::object());
            fields["position"] = {origin.x + (layout == "ring" ? std::cos(angle) * spacing : static_cast<float>(index % columns) * spacing), origin.y,
                origin.z + (layout == "ring" ? std::sin(angle) * spacing : static_cast<float>(index / columns) * spacing)};
            operations.push_back({{"op", "create"}, {"primitive", params.value("primitive", std::string("cube"))},
                {"name", params.value("name", std::string("Generated")) + " " + std::to_string(index + 1)}, {"fields", std::move(fields)}});
        }
        return automationRequest("scene.transaction", {{"expected_revision", params.at("expected_revision")}, {"dry_run", params.value("dry_run", false)},
            {"label", "MCP generate " + layout}, {"operations", std::move(operations)}});
    }
    if (method == "variables.get") { only(params, {}); return {{"revision", std::to_string(scene_.revision())}, {"values", scene_.variables}}; }
    if (method == "variables.set") {
        only(params, {"values","expected_revision","dry_run"});
        return automationRequest("scene.settings", {{"fields", {{"variables",params.at("values")}}}, {"expected_revision",params.at("expected_revision")}, {"dry_run",params.value("dry_run",false)}});
    }
    if (method == "graph.catalog") { only(params, {}); return {{"nodes",behaviorNodeCatalog()}, {"max_nodes_per_graph",128}, {"max_nodes_per_scene",2048}, {"max_execution_per_tick",8192}}; }
    if (method == "graph.state") { only(params, {}); return behaviors_.state(); }
    if (method == "graph.get") {
        only(params, {"entity"});
        const auto entity = automation::identifier(params.at("entity"));
        if (!scene_.contains(entity)) { throw Error("entity_not_found","Graph owner does not exist."); }
        const auto* graph = scene_.get<BehaviorGraph>(entity);
        return {{"entity",std::to_string(entity)}, {"revision",std::to_string(scene_.revision())}, {"graph",behaviorJson(graph ? *graph : BehaviorGraph{})}, {"variables",scene_.variables}};
    }
    if (method == "graph.set") {
        only(params, {"entity","graph","expected_revision","dry_run"});
        return automationRequest("entity.patch", {{"entity",params.at("entity")}, {"expected_revision",params.at("expected_revision")},
            {"dry_run",params.value("dry_run",false)}, {"fields",{{"behavior",params.at("graph")}}}});
    }
    if (method == "graph.add_node" || method == "graph.update_node" || method == "graph.remove_node" || method == "graph.connect" || method == "graph.disconnect") {
        if (method == "graph.add_node") { only(params, {"entity","node","expected_revision","dry_run"}); }
        if (method == "graph.update_node") { only(params, {"entity","node_id","fields","expected_revision","dry_run"}); }
        if (method == "graph.remove_node") { only(params, {"entity","node_id","expected_revision","dry_run"}); }
        if (method == "graph.connect") { only(params, {"entity","from","to","output","expected_revision","dry_run"}); }
        if (method == "graph.disconnect") { only(params, {"entity","from","output","expected_revision","dry_run"}); }
        requireAutomationEdit(params);
        auto document = automationRequest("graph.get",{{"entity",params.at("entity")}}).at("graph");
        if (method == "graph.add_node") { document["nodes"].push_back(params.at("node")); }
        else if (method == "graph.update_node" || method == "graph.remove_node") {
            const auto nodeId = params.at("node_id").get<std::uint32_t>();
            auto& nodes = document["nodes"];
            const auto found = std::find_if(nodes.begin(),nodes.end(),[&](const auto& node) { return node.at("id") == nodeId; });
            if (found == nodes.end()) { throw Error("node_not_found","The graph node does not exist."); }
            if (method == "graph.update_node") {
                if (params.at("fields").contains("id")) { throw Error("invalid_arguments","Node IDs cannot be changed by a field patch."); }
                found->update(params.at("fields"));
            } else {
                nodes.erase(found);
                auto& links = document["links"];
                links.erase(std::remove_if(links.begin(),links.end(),[&](const auto& link) { return link.at("from") == nodeId || link.at("to") == nodeId; }),links.end());
            }
        } else {
            const auto from = params.at("from").get<std::uint32_t>();
            const auto output = params.value("output",std::string("next"));
            auto& links = document["links"];
            if (method == "graph.connect") { links.push_back({{"from",from},{"to",params.at("to")},{"output",output}}); }
            else { links.erase(std::remove_if(links.begin(),links.end(),[&](const auto& link) { return link.at("from") == from && link.at("output") == output; }),links.end()); }
        }
        return automationRequest("graph.set",{{"entity",params.at("entity")},{"graph",std::move(document)},{"expected_revision",params.at("expected_revision")},{"dry_run",params.value("dry_run",false)}});
    }
    if (method == "scene.new" || method == "scene.replace") {
        only(params, {"name", "mode", "template", "scene", "expected_revision", "discard_changes", "dry_run"});
        requireAutomationEdit(params);
        if (scene_.serialize() != savedState_ && !params.value("discard_changes", false)) { throw Error("unsaved_changes", "Save the scene or explicitly set discard_changes before replacing it."); }
        Scene candidate;
        const auto sceneTemplate = params.value("template", std::string("empty"));
        if (sceneTemplate == "workshop") { candidate = Scene::demo(); }
        else if (sceneTemplate != "empty") { throw Error("invalid_arguments", "Supported scene templates are empty and workshop."); }
        auto document = method == "scene.replace" ? params.at("scene") : candidate.toJson();
        if (params.contains("name")) { document["name"] = params.at("name"); }
        if (params.contains("mode")) { document["mode"] = params.at("mode"); }
        std::string error;
        if (!candidate.deserialize(document.dump(), error)) { throw Error("invalid_scene", error); }
        const auto before = scene_.serialize();
        const auto after = candidate.serialize();
        if (before.size() + after.size() > history_.budgetBytes()) { throw Error("history_budget", "The replacement exceeds the undo budget."); }
        if (!params.value("dry_run", false)) {
            prepareAutomationAssets(candidate, scenePath_.empty() ? automationRoot_ : scenePath_.parent_path());
            history_.begin(scene_);
            scene_ = std::move(candidate);
            history_.commit(scene_, "MCP replace scene");
            selected_ = 0;
            assistant_.clearConversation();
        }
        return {{"revision", std::to_string(scene_.revision())}, {"dry_run", params.value("dry_run", false)}, {"changed", before != after}};
    }
    if (method == "history.undo" || method == "history.redo") {
        only(params, {"expected_revision"});
        requireAutomationEdit(params);
        std::string error;
        const bool applied = method == "history.undo" ? history_.undo(scene_, error) : history_.redo(scene_, error);
        if (!error.empty()) { throw Error("history_failed", error); }
        if (!scene_.contains(selected_)) { selected_ = 0; }
        return {{"applied", applied}, {"revision", std::to_string(scene_.revision())}};
    }
    if (method == "project.list") {
        only(params, {"directory", "offset", "limit"});
        const auto path = automationPath(params.value("directory", std::string(".")));
        if (!std::filesystem::is_directory(path)) { throw Error("path_not_found", "The requested directory does not exist."); }
        const auto limit = params.value("limit", std::size_t(100));
        const auto offset = params.value("offset", std::size_t(0));
        if (limit == 0 || limit > 1000) { throw Error("invalid_arguments", "Directory page size must be 1-1000."); }
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (paths.size() >= 10000) { throw Error("directory_budget", "Directory exceeds the supported enumeration limit."); }
            try {
                const auto relative = utf8(entry.path().lexically_relative(automationRoot_).generic_wstring());
                static_cast<void>(automationPath(relative));
                if (entry.is_directory() || entry.path().extension() == L".velos" || entry.path().extension() == L".glb"
                    || entry.path().extension() == L".png" || entry.path().extension() == L".jpg" || entry.path().extension() == L".dds"
                    || entry.path().extension() == L".tga" || entry.path().extension() == L".jpeg") { paths.push_back(entry.path()); }
            } catch (const std::exception&) {}
        }
        std::sort(paths.begin(), paths.end());
        Json entries = Json::array();
        for (std::size_t index = std::min(offset, paths.size()); index < paths.size() && index - offset < limit; ++index) {
            entries.push_back({{"path", utf8(paths[index].lexically_relative(automationRoot_).generic_wstring())}, {"directory", std::filesystem::is_directory(paths[index])}});
        }
        return {{"entries", std::move(entries)}, {"total", paths.size()}};
    }
    if (method == "project.open") {
        only(params, {"path", "expected_revision", "discard_changes"});
        requireAutomationEdit(params);
        if (scene_.serialize() != savedState_ && !params.value("discard_changes", false)) { throw Error("unsaved_changes", "Save or explicitly discard authored changes before opening another scene."); }
        const auto path = automationPath(params.at("path").get<std::string>(), ".velos");
        Scene candidate;
        std::string error;
        if (!candidate.deserialize(readText(path), error)) { throw Error("invalid_scene", error); }
        prepareAutomationAssets(candidate, path.parent_path());
        if (!openScene(path)) { throw Error("open_failed", "The scene could not be opened; inspect editor logs."); }
        return {{"path", params.at("path")}, {"revision", std::to_string(scene_.revision())}};
    }
    if (method == "project.save") {
        only(params, {"path", "expected_revision", "overwrite"});
        requireAutomationEdit(params);
        const auto relative = params.contains("path") ? params.at("path").get<std::string>()
            : scenePath_.empty() ? std::string{} : utf8(scenePath_.lexically_relative(automationRoot_).generic_wstring());
        const auto destination = automationPath(relative, ".velos");
        if (destination != scenePath_ && std::filesystem::exists(destination) && !params.value("overwrite", false)) { throw Error("file_exists", "Set overwrite explicitly to replace an existing scene file."); }
        const auto project = scenePath_.empty() ? automationRoot_ : scenePath_.parent_path();
        for (const auto& reference : scene_.assetReferences()) {
            const auto source = projectAssetPath(project, reference);
            static_cast<void>(automationPath(utf8(source.lexically_relative(automationRoot_).generic_wstring())));
            const auto target = projectAssetPath(destination.parent_path(), reference);
            const auto bytes = readBytes(source, 64 * 1024 * 1024);
            if (std::filesystem::exists(target) && sha256(readBytes(target)) != sha256(bytes)) { throw Error("asset_conflict", "Save As would replace a different project asset."); }
            if (!std::filesystem::exists(target)) { writeAtomic(target, bytes); }
        }
        const auto state = scene_.serialize();
        writeTextAtomic(destination, state);
        scenePath_ = destination;
        savedState_ = state;
        dirty_ = false;
        dirtyRevision_ = scene_.revision();
        autosaveSeconds_ = 0;
        return {{"path", relative}, {"revision", std::to_string(scene_.revision())}, {"sha256", sha256(state)}};
    }
    if (method == "editor.select") {
        only(params, {"entity", "focus"});
        if (automationReadOnly_) { throw Error("read_only", "This control session is read-only."); }
        const auto entity = automation::identifier(params.at("entity"), true);
        if (entity != 0 && !scene_.contains(entity)) { throw Error("entity_not_found", "Selection target does not exist."); }
        selected_ = entity;
        if (params.value("focus", false) && entity != 0) { focusSelection(); }
        return {{"selected", std::to_string(selected_)}, {"camera", cameraData(frame_.camera)}};
    }
    if (method == "editor.graph") {
        only(params,{"view","entity","arrange","expected_revision"});
        if (automationReadOnly_) { throw Error("read_only","This control session is read-only."); }
        if (params.value("arrange",false) && params.at("view") == "logic") { requireAutomationEdit(params); }
        if (params.contains("entity")) {
            const auto entity = automation::identifier(params.at("entity"));
            if (!scene_.contains(entity)) { throw Error("entity_not_found","Graph owner does not exist."); }
            selected_ = entity;
        }
        graphs_.show(params.at("view").get<std::string>(),params.value("arrange",false));
        return {{"view",graphs_.view()},{"selected",std::to_string(selected_)},{"last_error",graphs_.error()}};
    }
    if (method == "editor.focus") {
        only(params,{"panel"});
        if (automationReadOnly_) { throw Error("read_only","This control session is read-only."); }
        const auto panel = params.at("panel").get<std::string>();
        const std::set<std::string> panels{"Viewport","Graphs","Scene","Inspector","Assets","Performance","Console","Assistant"};
        if (!panels.contains(panel)) { throw Error("invalid_arguments","Unknown editor panel."); }
        ImGui::SetWindowFocus(panel.c_str());
        return {{"panel",panel}};
    }
    if (method == "graph.relationships") {
        only(params,{"offset","limit"});
        const auto page = automation::describeScene(scene_,params.value("offset",std::size_t(0)),params.value("limit",std::size_t(100)));
        Json links = Json::array();
        for (const auto& entity : page.at("entities")) {
            if (entity.at("parent") != "0") { links.push_back({{"from",entity.at("parent")},{"to",entity.at("id")},{"kind","parent"}}); }
        }
        return {{"nodes",page.at("entities")},{"links",std::move(links)},{"revision",page.at("revision")},{"total_entities",page.at("total_entities")}};
    }
    if (method == "assets.list") {
        only(params, {});
        Json references = Json::array();
        const auto project = scenePath_.empty() ? automationRoot_ : scenePath_.parent_path();
        for (const auto& reference : scene_.assetReferences()) {
            const auto source = projectAssetPath(project, reference);
            const auto relative = utf8(source.lexically_relative(automationRoot_).generic_wstring());
            static_cast<void>(automationPath(relative));
            references.push_back({{"reference", reference}, {"path", relative}, {"exists", std::filesystem::is_regular_file(source)}});
        }
        return {{"references", std::move(references)}, {"builtins", {"cube","sphere","plane","quad"}}};
    }
    if (method == "jobs.get" || method == "jobs.cancel") {
        only(params, {"job"});
        const auto id = params.at("job").get<std::string>();
        const auto found = std::find_if(controlJobs_.begin(), controlJobs_.end(), [&](const auto& job) { return job.id == id; });
        if (found == controlJobs_.end()) { throw Error("job_not_found", "The job does not exist or has expired from the 32-job history."); }
        if (method == "jobs.cancel") {
            if (automationReadOnly_) { throw Error("read_only", "This control session is read-only."); }
            if (found->kind == "build.export") { throw Error("not_cancellable", "An export already writing its output cannot be cancelled safely."); }
            if (found->state == "running") { cancelControlJob_ = true; found->state = "cancelling"; }
        }
        return {{"job", found->id}, {"kind", found->kind}, {"state", found->state}, {"result", found->result}, {"error", found->error}};
    }
    if (method == "assets.import_model" || method == "assets.import_texture" || method == "build.export") {
        if (method == "assets.import_model") { only(params, {"path", "name", "create_entity", "expected_revision"}); }
        if (method == "assets.import_texture") { only(params, {"path", "entity", "slot", "expected_revision"}); }
        if (method == "build.export") { only(params, {"directory", "expected_revision"}); }
        requireAutomationEdit(params);
        if (scenePath_.empty()) { throw Error("save_required", "Save the scene before importing project assets or exporting a runtime."); }
        static_cast<void>(automationPath(utf8(scenePath_.lexically_relative(automationRoot_).generic_wstring()), ".velos"));
        const auto source = method == "build.export" ? scenePath_ : automationPath(params.at("path").get<std::string>(), method == "assets.import_model" ? ".glb" : "");
        const auto project = scenePath_.parent_path();
        const auto revision = scene_.revision();
        const auto job = std::to_string(nextControlJob_++);
        std::future<std::function<Json()>> work;
        if (method == "build.export") {
            if (scene_.serialize() != savedState_) { throw Error("save_required", "Save authored changes before exporting."); }
            const auto destination = automationPath(params.at("directory").get<std::string>());
            if (destination == automationRoot_ || (std::filesystem::exists(destination) && !std::filesystem::is_empty(destination))) { throw Error("output_not_empty", "Exports require a new or empty workspace subdirectory."); }
            prepareAutomationAssets(scene_, project);
            const auto binaries = executableDirectory();
            const auto relative = params.at("directory");
            work = std::async(std::launch::async, [source, destination, binaries, relative]() -> std::function<Json()> {
                packageScene(source, destination, binaries);
                verifyPackage(destination);
                return [relative] { return Json{{"directory", relative}, {"runtime", relative.get<std::string>() + "/VelosRuntime.exe"}, {"verified", true}}; };
            });
        } else if (method == "assets.import_model") {
            const auto name = params.value("name", utf8(source.stem().native()));
            const bool createEntity = params.value("create_entity", true);
            work = std::async(std::launch::async, [this, source, project, revision, name, createEntity]() -> std::function<Json()> {
                auto bytes = readBytes(source, 64 * 1024 * 1024);
                const auto reference = "Assets/" + sha256(bytes) + ".glb";
                auto mesh = loadGlb(bytes, geometryCache_);
                return [this, project, revision, name, createEntity, reference, bytes = std::move(bytes), mesh = std::move(mesh)]() mutable {
                    requireAutomationEdit({{"expected_revision", std::to_string(revision)}});
                    if (scenePath_.parent_path() != project) { throw Error("revision_conflict", "The project changed during import."); }
                    const auto destination = projectAssetPath(project, reference);
                    static_cast<void>(automationPath(utf8(destination.lexically_relative(automationRoot_).generic_wstring())));
                    if (std::filesystem::exists(destination) && sha256(readBytes(destination)) != sha256(bytes)) { throw Error("asset_conflict", "The hashed asset destination contains different bytes."); }
                    if (!std::filesystem::exists(destination)) { writeAtomic(destination, bytes); }
                    renderer_.addMesh(reference, mesh);
                    Json result{{"reference", reference}, {"vertices", mesh.vertices.size()}, {"indices", mesh.indices.size()}};
                    if (createEntity) {
                        auto transaction = automationRequest("entity.create", {{"expected_revision", std::to_string(revision)}, {"name", name}, {"primitive", reference}});
                        result["entity"] = transaction.at("results")[0].at("entity");
                        selected_ = automation::identifier(result["entity"]);
                    }
                    result["revision"] = std::to_string(scene_.revision());
                    return result;
                };
            });
        } else {
            const auto entity = automation::identifier(params.at("entity"));
            if (!scene_.get<MeshRenderer>(entity)) { throw Error("component_missing", "A texture target must have a mesh material."); }
            const auto slot = params.at("slot").get<int>();
            if (slot < 0 || slot > 3) { throw Error("invalid_arguments", "Texture slots are 0 albedo, 1 normal, 2 ORM, 3 emissive."); }
            auto extension = utf8(source.extension().native());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
            const std::set<std::string> formats{".png",".jpg",".jpeg",".bmp",".tif",".tiff",".tga",".dds"};
            if (!formats.contains(extension)) { throw Error("unsupported_format", "Unsupported image format."); }
            work = std::async(std::launch::async, [this, source, project, revision, entity, slot, extension]() -> std::function<Json()> {
                auto bytes = readBytes(source, 64 * 1024 * 1024);
                const auto reference = "Assets/" + sha256(bytes) + extension;
                auto texture = loadTexture(bytes, extension, {static_cast<TextureSlot>(slot)}, textureCache_);
                return [this, project, revision, entity, slot, reference, bytes = std::move(bytes), texture = std::move(texture)]() mutable {
                    requireAutomationEdit({{"expected_revision", std::to_string(revision)}});
                    if (scenePath_.parent_path() != project) { throw Error("revision_conflict", "The project changed during import."); }
                    const auto* material = scene_.get<MeshRenderer>(entity);
                    if (!material) { throw Error("component_missing", "The target material was removed during import."); }
                    const auto destination = projectAssetPath(project, reference);
                    static_cast<void>(automationPath(utf8(destination.lexically_relative(automationRoot_).generic_wstring())));
                    if (std::filesystem::exists(destination) && sha256(readBytes(destination)) != sha256(bytes)) { throw Error("asset_conflict", "The hashed texture destination contains different bytes."); }
                    if (!std::filesystem::exists(destination)) { writeAtomic(destination, bytes); }
                    renderer_.addTexture(textureKey(reference, static_cast<TextureSlot>(slot)), texture);
                    auto textures = material->textures;
                    textures[static_cast<std::size_t>(slot)] = reference;
                    const auto result = automationRequest("entity.patch", {{"entity", std::to_string(entity)}, {"expected_revision", std::to_string(revision)}, {"fields", {{"mesh", {{"textures", textures}}}}}});
                    return Json{{"reference", reference}, {"entity", std::to_string(entity)}, {"mips", texture.mips.size()}, {"revision", result.at("revision")}};
                };
            });
        }
        while (controlJobs_.size() >= 32) { controlJobs_.pop_front(); }
        controlJobs_.push_back({job, std::string(method), "running", nullptr, {}});
        controlWork_ = std::move(work);
        return {{"job", job}, {"state", "running"}};
    }
    if (method == "build.verify") {
        only(params, {"directory"});
        const auto directory = automationPath(params.at("directory").get<std::string>());
        verifyPackage(directory);
        return {{"directory", params.at("directory")}, {"verified", true}};
    }
    if (method == "camera.get") { only(params, {}); return cameraData(frame_.camera); }
    if (method == "camera.set") {
        only(params, {"target", "yaw_degrees", "pitch_degrees", "distance", "save_to_scene", "expected_revision"});
        if (automationReadOnly_) { throw Error("read_only", "This control session is read-only."); }
        auto camera = frame_.camera;
        if (params.contains("target")) { camera.target = position(params.at("target")); }
        camera.yaw = DirectX::XMConvertToRadians(number(params, "yaw_degrees", DirectX::XMConvertToDegrees(camera.yaw), -36000,36000));
        camera.pitch = DirectX::XMConvertToRadians(number(params, "pitch_degrees", DirectX::XMConvertToDegrees(camera.pitch), -84,84));
        camera.distance = number(params,"distance",camera.distance,0.3f,180);
        if (params.value("save_to_scene",false)) {
            requireAutomationEdit(params);
            automationRequest("scene.settings",{{"expected_revision",params.at("expected_revision")},{"fields",{{"view",{
                {"target",{camera.target.x,camera.target.y,camera.target.z}},{"yaw",camera.yaw},{"pitch",camera.pitch},{"distance",camera.distance}
            }}}}});
        }
        frame_.camera = camera;
        auto result = cameraData(camera);
        result["revision"] = std::to_string(scene_.revision());
        result["saved_to_scene"] = params.value("save_to_scene",false);
        return result;
    }
    if (method.starts_with("simulation.")) {
        if (method == "simulation.step") { only(params,{"steps"}); }
        else if (method == "simulation.velocity") { only(params,{"entity","horizontal","forward"}); }
        else if (method == "simulation.input") { only(params,{"keys"}); }
        else { only(params,{}); }
        if (method != "simulation.state" && automationReadOnly_) { throw Error("read_only", "This control session is read-only."); }
        if (method == "simulation.play") {
            if (!playing_) { requireAutomationEdit(params, false); startPlay(); }
            paused_ = false;
            if (!playing_) { throw Error("simulation_failed", "Physics rejected this scene; inspect editor logs."); }
        } else if (method == "simulation.pause") {
            if (!playing_) { throw Error("simulation_inactive", "Start simulation before pausing."); }
            paused_ = true;
        } else if (method == "simulation.stop") { stopPlay(); }
        else if (method == "simulation.input") {
            if (!playing_) { throw Error("simulation_inactive","Start simulation before supplying input."); }
            const auto& keys = params.at("keys");
            if (!keys.is_array() || keys.size() > 16) { throw Error("invalid_arguments","Supply at most sixteen held keys; an empty array releases all keys."); }
            std::set<int> held;
            for (const auto& key : keys) { held.insert(behaviorKey(key.get<std::string>())); }
            controlKeys_ = std::move(held);
        }
        else if (method == "simulation.step") {
            const auto steps = params.value("steps", 1);
            if (steps < 1 || steps > 120) { throw Error("invalid_arguments", "A step request must contain 1-120 fixed ticks."); }
            if (!playing_) { requireAutomationEdit(params, false); }
            try { for (int index = 0; index < steps; ++index) { stepPlay(); } }
            catch (...) { stopPlay(); throw; }
            if (!playing_) { throw Error("simulation_failed", "The scene could not enter simulation."); }
        } else if (method == "simulation.velocity") {
            if (!playing_) { throw Error("simulation_inactive", "Start simulation before setting body velocity."); }
            const auto entity = automation::identifier(params.at("entity"));
            if (!physics_.setPlanarVelocity(entity, number(params,"horizontal",0,-100,100), number(params,"forward",0,-100,100))) {
                throw Error("body_not_found", "The entity has no controllable dynamic physics body.");
            }
        } else if (method != "simulation.state") { throw Error("unknown_method", "Unknown simulation operation."); }
        return {{"playing", playing_}, {"paused", paused_}, {"fixed_step_seconds", clock_.stepSeconds()}, {"bodies", physics_.bodyCount()}, {"revision", std::to_string(scene_.revision())}};
    }
    if (method == "renderer.get" || method == "renderer.set") {
        if (method == "renderer.set") {
            only(params, {"exposure", "resolution_scale", "shadow_resolution", "ray_budget_mb", "instancing", "lods", "lod_bias", "wireframe", "grid", "vsync"});
            if (automationReadOnly_) { throw Error("read_only", "This control session is read-only."); }
            const auto exposure = number(params,"exposure",frame_.exposure,0.1f,8);
            const auto scale = number(params,"resolution_scale",resolutionScale_,0.25f,1.5f);
            const auto lodBias = number(params,"lod_bias",frame_.lodBias,0.25f,4);
            const auto shadowSize = params.value("shadow_resolution", renderer_.stats().shadowResolution);
            if (shadowSize != 512 && shadowSize != 1024 && shadowSize != 2048) { throw Error("invalid_arguments", "Shadow resolution must be 512, 1024 or 2048."); }
            const auto rayBudget = params.value("ray_budget_mb", renderer_.stats().rayTracingBudget / (1024 * 1024));
            if (rayBudget > 256) { throw Error("invalid_arguments", "DXR budget must be 0-256 MB."); }
            const bool instancing = params.value("instancing", frame_.instancing);
            const bool lods = params.value("lods", frame_.lods);
            const bool wireframe = params.value("wireframe", frame_.wireframe);
            const bool grid = params.value("grid", grid_);
            const bool vsync = params.value("vsync", vsync_);
            if (params.contains("ray_budget_mb")) { renderer_.setRayTracingBudget(rayBudget * 1024 * 1024); }
            renderer_.setShadowResolution(shadowSize);
            frame_.exposure = exposure;
            resolutionScale_ = scale;
            frame_.lodBias = lodBias;
            frame_.instancing = instancing;
            frame_.lods = lods;
            frame_.wireframe = wireframe;
            grid_ = grid;
            vsync_ = vsync;
        } else { only(params, {}); }
        const auto stats = renderer_.stats();
        return {{"adapter", stats.adapter}, {"gpu_ms", stats.gpuMilliseconds}, {"gpu_usage", stats.gpuUsage}, {"gpu_budget", stats.gpuBudget},
            {"camera_draws", stats.cameraDraws}, {"shadow_draws", stats.shadowDraws}, {"triangles", stats.triangles}, {"visible", stats.visibleObjects},
            {"mesh_bytes", stats.meshBytes}, {"texture_bytes", stats.textureBytes}, {"texture_count", stats.textureCount}, {"ray_bytes", stats.rayTracingBytes},
            {"ray_supported", stats.rayTracingSupported}, {"shadow_path", stats.shadowStatus}, {"ray_budget_mb", stats.rayTracingBudget / (1024 * 1024)},
            {"shadow_resolution", stats.shadowResolution}, {"exposure", frame_.exposure}, {"resolution_scale", resolutionScale_},
            {"instancing", frame_.instancing}, {"lods", frame_.lods}, {"lod_bias", frame_.lodBias}, {"wireframe", frame_.wireframe}, {"grid", grid_}, {"vsync", vsync_}};
    }
    if (method == "renderer.reload_shaders") {
        only(params, {});
        if (automationReadOnly_) { throw Error("read_only", "This control session is read-only."); }
        std::string error;
        if (!renderer_.reloadShaders(error)) { throw Error("shader_error", error); }
        return {{"reloaded", true}};
    }
    if (method == "viewport.capture") {
        only(params, {"path", "expected_revision", "overwrite"});
        if (automationReadOnly_) { throw Error("read_only", "Read-only sessions cannot write screenshot files."); }
        if (renderedFrames_ == 0 || (params.contains("expected_revision") && automation::identifier(params.at("expected_revision"), true) != renderedRevision_)) {
            throw Error("frame_not_ready", "The requested scene revision has not been rendered yet; retry this read-only capture after another frame.");
        }
        const auto path = automationPath(params.at("path").get<std::string>(), ".png");
        if (std::filesystem::exists(path) && !params.value("overwrite", false)) { throw Error("file_exists", "The capture exists; set overwrite explicitly or choose a new path."); }
        renderer_.capture(path);
        return {{"path", params.at("path")}, {"mime_type", "image/png"}, {"rendered_revision", std::to_string(renderedRevision_)},
            {"frame", renderedFrames_}, {"bytes", std::filesystem::file_size(path)}};
    }
    if (method == "editor.logs") {
        only(params, {"limit"});
        const auto limit = params.value("limit", std::size_t(50));
        if (limit > 250) { throw Error("invalid_arguments", "Log pages are limited to 250 entries."); }
        Json entries = Json::array();
        for (std::size_t index = messages_.size() - std::min(limit, messages_.size()); index < messages_.size(); ++index) {
            entries.push_back({{"message", messages_[index].first}, {"error", messages_[index].second}});
        }
        return {{"entries", std::move(entries)}, {"gpu_validation", renderer_.validationErrors()}};
    }
    if (method == "editor.close") {
        only(params, {"expected_revision", "discard_changes"});
        requireAutomationEdit(params);
        if (scene_.serialize() != savedState_ && !params.value("discard_changes", false)) { throw Error("unsaved_changes", "Save or explicitly discard changes before closing the editor."); }
        wantsClose_ = true;
        return {{"closing", true}};
    }
    throw Error("unknown_method", "Unsupported editor command: " + std::string(method));
}

}