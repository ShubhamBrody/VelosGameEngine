#include "platform/Files.h"

#include <Windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace velos {

std::string utf8(std::wstring_view text) {
    if (text.empty()) { return {}; }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) { throw std::runtime_error("Text is too long."); }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0) { throw std::runtime_error("Invalid Unicode text."); }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring wide(std::string_view text) {
    if (text.empty()) { return {}; }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) { throw std::runtime_error("Text is too long."); }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0) { throw std::runtime_error("Invalid UTF-8 text."); }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::filesystem::path localDataDirectory() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder))) {
        throw std::runtime_error("Cannot locate the local application data directory.");
    }
    const std::filesystem::path result = std::filesystem::path(folder) / L"Velos";
    CoTaskMemFree(folder);
    std::filesystem::create_directories(result);
    return result;
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) { throw std::runtime_error("Cannot locate the executable."); }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::vector<std::byte> readBytes(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { throw std::runtime_error("Cannot open file: " + utf8(path.native())); }
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) > limit) {
        throw std::runtime_error("File exceeds its supported size limit: " + utf8(path.filename().native()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("File read was incomplete.");
    }
    return bytes;
}

std::string readText(const std::filesystem::path& path, std::size_t limit) {
    const auto bytes = readBytes(path, limit);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void writeAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    if (!path.parent_path().empty()) { std::filesystem::create_directories(path.parent_path()); }
    static std::atomic_uint64_t sequence = 0;
    auto temporary = path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence);
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { throw std::runtime_error("Cannot create temporary output file."); }
    try {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto requested = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, requested, &written, nullptr) || written == 0) {
                throw std::runtime_error("File write failed; the disk may be full.");
            }
            offset += written;
        }
        if (!FlushFileBuffers(file)) { throw std::runtime_error("Could not flush the output file."); }
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Cannot replace the destination file atomically.");
        }
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); }
        DeleteFileW(temporary.c_str());
        throw;
    }
}

void writeTextAtomic(const std::filesystem::path& path, std::string_view text) {
    writeAtomic(path, std::as_bytes(std::span(text.data(), text.size())));
}

std::string sha256(std::span<const std::byte> bytes) {
    if (bytes.size() > std::numeric_limits<ULONG>::max()) { throw std::runtime_error("Hash input exceeds 4 GB."); }
    std::array<UCHAR, 32> digest{};
    const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())), static_cast<ULONG>(bytes.size()),
        digest.data(), static_cast<ULONG>(digest.size()));
    if (status < 0) { throw std::runtime_error("SHA-256 hashing failed."); }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < digest.size(); ++index) {
        result[index * 2] = digits[digest[index] >> 4];
        result[index * 2 + 1] = digits[digest[index] & 15];
    }
    return result;
}

std::string sha256(std::string_view text) { return sha256(std::as_bytes(std::span(text.data(), text.size()))); }

}