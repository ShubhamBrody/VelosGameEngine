#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace velos {

struct CacheStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t corruptions = 0;
    std::uint64_t bytes = 0;
};

class DiskCache {
public:
    DiskCache(std::filesystem::path directory, std::uint64_t byteBudget);
    [[nodiscard]] std::optional<std::vector<std::byte>> get(const std::string& key,
        std::chrono::seconds ttl = std::chrono::seconds::max());
    bool put(const std::string& key, std::span<const std::byte> value);
    void clear();
    [[nodiscard]] CacheStats stats() const noexcept;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

private:
    std::filesystem::path directory_;
    std::wstring mutexName_;
    std::uint64_t budget_;
    std::atomic_uint64_t hits_ = 0;
    std::atomic_uint64_t misses_ = 0;
    std::atomic_uint64_t evictions_ = 0;
    std::atomic_uint64_t corruptions_ = 0;
    std::atomic_uint64_t bytes_ = 0;
    bool reserve(std::uint64_t bytes);
};

}