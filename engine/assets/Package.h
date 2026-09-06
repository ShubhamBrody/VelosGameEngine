#pragma once

#include <filesystem>

namespace velos {

void packageScene(const std::filesystem::path& scene, const std::filesystem::path& output,
    const std::filesystem::path& binaries);
void verifyPackage(const std::filesystem::path& directory);

}