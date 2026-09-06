#include "assets/DiskCache.h"
#include "platform/Files.h"

#include <Windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"VelosTests-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        require(velos::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 reference vector.");
        velos::DiskCache cache(root, 320);
        const auto first = velos::sha256("first");
        const auto second = velos::sha256("second");
        const auto third = velos::sha256("third");
        const std::string content(40, 'a');
        const auto payload = std::as_bytes(std::span(content.data(), content.size()));
        require(cache.put(first, payload) && cache.put(second, payload), "Cache writes must succeed.");
        require(cache.get(first).has_value(), "Warm read must hit.");
        require(cache.put(third, payload), "Budgeted insertion must evict an old entry.");
        require(cache.stats().bytes <= 320 && cache.stats().evictions > 0, "Cache budget must be enforced.");
        require(!cache.get(second).has_value(), "LRU entry must be evicted.");
        require(cache.get(first).has_value(), "Recent entry must be retained.");
        require(!cache.get("../outside").has_value() && !cache.put("../outside", payload), "Invalid keys must not escape the cache.");
        const auto path = root / (first + ".cache");
        auto damaged = velos::readBytes(path);
        damaged.back() ^= std::byte{1};
        velos::writeAtomic(path, damaged);
        require(!cache.get(first).has_value() && cache.stats().corruptions == 1, "Corrupt payload must be rejected.");
        require(!cache.get(third, std::chrono::seconds(0)).has_value(), "Expired entries must not be reused.");
        const std::string large(400, 'x');
        require(!cache.put(first, std::as_bytes(std::span(large.data(), large.size()))), "Oversized entries must be rejected.");
        velos::writeTextAtomic(root / "source.txt", "keep me");
        cache.clear();
        require(velos::readText(root / "source.txt") == "keep me", "Cache clear must not delete source files.");
        velos::writeTextAtomic(root / "source.txt", "replacement");
        require(velos::readText(root / "source.txt") == "replacement", "Atomic replacement must preserve the complete new file.");
        std::filesystem::remove_all(root);
        std::cout << "PASS: hashing, cache hits, LRU, budgets, corruption, TTL, path validation and atomic files.\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}