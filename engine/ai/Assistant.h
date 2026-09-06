#pragma once

#include "ai/Http.h"
#include "ai/StreamDecoder.h"
#include "assets/DiskCache.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace velos {

struct AiSettings {
    bool remote = false;
    std::string endpoint = "https://api.openai.com/v1/chat/completions";
    std::string remoteModel;
    std::string ollamaBase = "http://localhost:11434";
    std::string localModel = "qwen2.5-coder:1.5b";
    std::string keyHeader = "Authorization";
    bool bearer = true;
    bool fallback = true;
    bool cacheResponses = true;
    int outputTokens = 1024;
    [[nodiscard]] nlohmann::json toJson() const;
    static AiSettings fromJson(const nlohmann::json& json);
};

struct AiState {
    std::string text;
    std::string status = "Idle";
    std::string provider;
    std::string error;
    std::vector<std::string> models;
    std::uint64_t tokens = 0;
    bool cached = false;
    bool busy = false;
    bool cancelled = false;
};

struct AiRequest {
    AiSettings settings;
    nlohmann::json messages;
    std::string projectScope;
    std::string credential;
};

using AiObserver = std::function<void(const AiState&)>;
AiState runCompletion(const AiRequest& request, DiskCache& cache, const HttpTransport& transport,
    const AiObserver& observe, std::stop_token stop = {});

class Assistant {
public:
    explicit Assistant(std::filesystem::path cachePath);
    ~Assistant();
    bool request(AiRequest request);
    bool discoverModels(const std::string& baseUrl);
    void cancel();
    [[nodiscard]] AiState state() const;
    [[nodiscard]] CacheStats cacheStats() const noexcept { return cache_.stats(); }

private:
    DiskCache cache_;
    mutable std::mutex mutex_;
    AiState state_;
    std::atomic_bool busy_ = false;
    std::jthread worker_;
    void publish(const AiState& state);
};

}