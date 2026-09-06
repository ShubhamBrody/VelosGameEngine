#include "assets/DiskCache.h"
#include "platform/Files.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace velos {
namespace {

struct Header {
    std::array<char, 8> magic{'V', 'L', 'C', 'A', 'C', 'H', 'E', '1'};
    std::uint64_t created = 0;
    std::uint64_t size = 0;
    std::array<char, 64> checksum{};
};
static_assert(sizeof(Header) == 88);

class DirectoryLock {
public:
    explicit DirectoryLock(const std::wstring& name) {
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle_) { throw std::runtime_error("Cannot create cache lock."); }
        const auto result = WaitForSingleObject(handle_, 5000);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("Cache is busy.");
        }
    }
    ~DirectoryLock() { if (handle_) { ReleaseMutex(handle_); CloseHandle(handle_); } }
    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
private:
    HANDLE handle_ = nullptr;
};

bool validKey(const std::string& key) {
    return key.size() == 64 && std::all_of(key.begin(), key.end(), [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

std::uint64_t nowSeconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

}

DiskCache::DiskCache(std::filesystem::path directory, std::uint64_t byteBudget)
    : directory_(std::move(directory)), budget_(byteBudget) {
    std::filesystem::create_directories(directory_);
    directory_ = std::filesystem::weakly_canonical(directory_);
    mutexName_ = L"Local\\VelosCache-" + wide(sha256(utf8(directory_.native())));
    const DirectoryLock lock(mutexName_);
    static_cast<void>(reserve(0));
}

bool DiskCache::reserve(std::uint64_t incoming) {
    struct Entry { std::filesystem::path path; std::uint64_t size; std::filesystem::file_time_type used; };
    std::vector<Entry> entries;
    std::uint64_t total = 0;
    for (const auto& file : std::filesystem::directory_iterator(directory_)) {
        if (!file.is_regular_file()) { continue; }
        if (file.path().extension() == L".cache") {
            const auto size = file.file_size();
            entries.push_back({file.path(), size, file.last_write_time()});
            total += size;
        } else if (file.path().filename().wstring().find(L".cache.tmp.") != std::wstring::npos) {
            std::error_code error;
            std::filesystem::remove(file.path(), error);
            if (error) { total += file.file_size(); }
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) { return left.used < right.used; });
    for (const auto& entry : entries) {
        if (total <= budget_ && incoming <= budget_ - total) { break; }
        std::error_code error;
        if (std::filesystem::remove(entry.path, error)) {
            total -= entry.size;
            ++evictions_;
        }
    }
    bytes_ = total;
    return total <= budget_ && incoming <= budget_ - total;
}

std::optional<std::vector<std::byte>> DiskCache::get(const std::string& key, std::chrono::seconds ttl) {
    if (!validKey(key)) { ++misses_; return std::nullopt; }
    try {
        const DirectoryLock lock(mutexName_);
        const auto path = directory_ / (key + ".cache");
        if (!std::filesystem::exists(path)) { ++misses_; return std::nullopt; }
        const auto bytes = readBytes(path, static_cast<std::size_t>(budget_));
        Header header;
        bool valid = bytes.size() >= sizeof(Header);
        if (valid) {
            std::memcpy(&header, bytes.data(), sizeof(Header));
            valid = header.magic == Header{}.magic && header.size == bytes.size() - sizeof(Header);
        }
        const auto now = nowSeconds();
        const bool expired = header.created > now || (ttl != std::chrono::seconds::max()
            && (ttl.count() <= 0 || now - header.created >= static_cast<std::uint64_t>(ttl.count())));
        if (valid && !expired) {
            const std::span<const std::byte> payload(bytes.data() + sizeof(Header), bytes.size() - sizeof(Header));
            valid = sha256(payload) == std::string(header.checksum.data(), header.checksum.size());
            if (valid) {
                std::error_code error;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), error);
                ++hits_;
                return std::vector<std::byte>(payload.begin(), payload.end());
            }
        }
        if (!expired) { ++corruptions_; }
        std::error_code error;
        std::filesystem::remove(path, error);
        ++misses_;
        return std::nullopt;
    } catch (const std::exception&) {
        ++misses_;
        return std::nullopt;
    }
}

bool DiskCache::put(const std::string& key, std::span<const std::byte> value) {
    if (!validKey(key) || value.size() > budget_ || sizeof(Header) > budget_ - value.size()) { return false; }
    try {
        const DirectoryLock lock(mutexName_);
        const auto cost = sizeof(Header) + value.size();
        if (!reserve(cost)) { return false; }
        Header header;
        header.created = nowSeconds();
        header.size = value.size();
        const auto checksum = sha256(value);
        std::copy(checksum.begin(), checksum.end(), header.checksum.begin());
        std::vector<std::byte> bytes(cost);
        std::memcpy(bytes.data(), &header, sizeof(Header));
        if (!value.empty()) { std::memcpy(bytes.data() + sizeof(Header), value.data(), value.size()); }
        writeAtomic(directory_ / (key + ".cache"), bytes);
        static_cast<void>(reserve(0));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void DiskCache::clear() {
    const DirectoryLock lock(mutexName_);
    for (const auto& file : std::filesystem::directory_iterator(directory_)) {
        if (file.path().extension() == L".cache") {
            std::error_code error;
            std::filesystem::remove(file.path(), error);
        }
    }
    static_cast<void>(reserve(0));
}

CacheStats DiskCache::stats() const noexcept {
    return {hits_.load(), misses_.load(), evictions_.load(), corruptions_.load(), bytes_.load()};
}

}