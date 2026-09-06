#include "assets/Package.h"
#include "platform/Files.h"
#include "scene/Scene.h"

#include <Windows.h>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) { return 1; }
    const auto root = std::filesystem::temp_directory_path() / (L"VelosPackageTest-" + std::to_wstring(GetCurrentProcessId()));
    try {
        const auto source = root / L"source" / L"test.velos";
        velos::writeTextAtomic(source, velos::Scene::demo().serialize());
        velos::writeTextAtomic(source.parent_path() / L"ai_credentials.json", "must not be packaged");
        velos::writeTextAtomic(source.parent_path() / L"unrelated.txt", "must not be packaged");
        const auto output = root / L"build";
        velos::packageScene(source, output, velos::wide(argv[1]));
        velos::verifyPackage(output);
        if (std::filesystem::exists(output / "ai_credentials.json") || std::filesystem::exists(output / "unrelated.txt")) {
            throw std::runtime_error("Exporter copied unrelated or credential files.");
        }
        bool denied = false;
        try { velos::packageScene(source, output, velos::wide(argv[1])); }
        catch (const std::runtime_error&) { denied = true; }
        if (!denied) { throw std::runtime_error("Exporter must not overwrite nonempty directories."); }
        velos::writeTextAtomic(output / L"game.velos", "corrupted");
        denied = false;
        try { velos::verifyPackage(output); }
        catch (const std::runtime_error&) { denied = true; }
        if (!denied) { throw std::runtime_error("Package verification failed to detect corruption."); }
        std::filesystem::remove_all(root);
        std::cout << "PASS: standalone export, manifest integrity, dependency whitelist and non-destructive output policy.\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}