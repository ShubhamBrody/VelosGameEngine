#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace velos {

std::string utf8(std::wstring_view text);
std::wstring wide(std::string_view text);
std::filesystem::path localDataDirectory();
std::filesystem::path executableDirectory();
std::filesystem::path projectAssetPath(const std::filesystem::path& projectDirectory, std::string_view relative);
std::vector<std::byte> readBytes(const std::filesystem::path& path, std::size_t limit = 64 * 1024 * 1024);
std::string readText(const std::filesystem::path& path, std::size_t limit = 8 * 1024 * 1024);
void writeAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes);
void writeTextAtomic(const std::filesystem::path& path, std::string_view text);
std::string sha256(std::span<const std::byte> bytes);
std::string sha256(std::string_view text);

}