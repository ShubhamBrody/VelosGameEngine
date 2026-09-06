#include "assets/Package.h"
#include "platform/Files.h"
#include "scene/Scene.h"

#include <set>
#include <stdexcept>

namespace velos {

void packageScene(const std::filesystem::path& source, const std::filesystem::path& output,
    const std::filesystem::path& binaries) {
    if (std::filesystem::exists(output) && !std::filesystem::is_empty(output)) {
        throw std::runtime_error("Export requires an empty destination folder; existing files will not be overwritten.");
    }
    Scene scene;
    std::string error;
    if (!scene.deserialize(readText(source), error)) { throw std::runtime_error(error); }
    std::set<std::string> assets;
    for (const auto id : scene.entities()) {
        if (const auto* mesh = scene.get<MeshRenderer>(id)) {
            if (mesh->mesh == "cube" || mesh->mesh == "sphere" || mesh->mesh == "plane" || mesh->mesh == "quad") { continue; }
            if (!mesh->mesh.starts_with("Assets/")) { throw std::runtime_error("Unknown project mesh reference."); }
            const auto asset = projectAssetPath(source.parent_path(), mesh->mesh);
            if (!std::filesystem::is_regular_file(asset)) { throw std::runtime_error("Cannot package a missing asset."); }
            assets.insert(mesh->mesh);
        }
    }
    for (const auto* name : {L"VelosRuntime.exe", L"dxcompiler.dll", L"dxil.dll"}) {
        if (!std::filesystem::is_regular_file(binaries / name)) { throw std::runtime_error("Build the standalone runtime before exporting."); }
    }
    const auto shader = binaries / L"shaders" / L"scene.hlsl";
    if (!std::filesystem::is_regular_file(shader)) { throw std::runtime_error("Runtime shader source is missing."); }
    std::filesystem::create_directories(output);
    nlohmann::json manifest{{"schema", 1}, {"engine", "Velos native preview"}, {"scene", "game.velos"}, {"files", nlohmann::json::array()}};
    const auto write = [&](const std::filesystem::path& relative, std::span<const std::byte> bytes) {
        writeAtomic(output / relative, bytes);
        manifest["files"].push_back({{"path", utf8(relative.generic_wstring())}, {"sha256", sha256(bytes)}, {"bytes", bytes.size()}});
    };
    for (const auto* name : {L"VelosRuntime.exe", L"dxcompiler.dll", L"dxil.dll"}) { write(name, readBytes(binaries / name, 128 * 1024 * 1024)); }
    write(L"shaders/scene.hlsl", readBytes(shader));
    const auto text = scene.serialize();
    write(L"game.velos", std::as_bytes(std::span(text.data(), text.size())));
    for (const auto& reference : assets) { write(wide(reference), readBytes(projectAssetPath(source.parent_path(), reference))); }
    const auto notices = binaries / L"licenses";
    if (std::filesystem::exists(notices)) {
        for (const auto& entry : std::filesystem::directory_iterator(notices)) {
            if (entry.is_regular_file()) { write(std::filesystem::path(L"licenses") / entry.path().filename(), readBytes(entry.path())); }
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(binaries)) {
        const auto name = entry.path().filename().wstring();
        if (entry.is_regular_file() && entry.path().extension() == L".dll"
            && (name.starts_with(L"vcruntime") || name.starts_with(L"msvcp") || name.starts_with(L"concrt"))) {
            write(entry.path().filename(), readBytes(entry.path()));
        }
    }
    writeTextAtomic(output / L"velos-build.json", manifest.dump(2));
}

void verifyPackage(const std::filesystem::path& directory) {
    const auto manifest = nlohmann::json::parse(readText(directory / L"velos-build.json", 1024 * 1024));
    if (manifest.value("schema", 0) != 1 || !manifest.at("files").is_array() || manifest.at("files").size() > 10000) {
        throw std::runtime_error("Unsupported or malformed runtime package manifest.");
    }
    std::set<std::string> files;
    for (const auto& file : manifest.at("files")) {
        const auto relative = file.at("path").get<std::string>();
        if (!files.insert(relative).second) { throw std::runtime_error("Duplicate package file entry."); }
        const auto source = projectAssetPath(directory, relative);
        const auto bytes = readBytes(source, 128 * 1024 * 1024);
        if (file.at("bytes").get<std::uint64_t>() != bytes.size() || file.at("sha256").get<std::string>() != sha256(bytes)) {
            throw std::runtime_error("Package integrity check failed: " + relative);
        }
    }
    if (!files.contains("game.velos") || !files.contains("VelosRuntime.exe") || !files.contains("shaders/scene.hlsl")) {
        throw std::runtime_error("Runtime package is missing a required file.");
    }
}

}