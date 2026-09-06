#include "ai/Assistant.h"
#include "platform/Files.h"

#include <Windows.h>
#include <algorithm>
#include <stdexcept>

namespace velos {
using Json = nlohmann::json;

Json AiSettings::toJson() const {
    return {{"remote", remote}, {"endpoint", endpoint}, {"remoteModel", remoteModel}, {"ollamaBase", ollamaBase},
        {"localModel", localModel}, {"keyHeader", keyHeader}, {"bearer", bearer}, {"fallback", fallback},
        {"cacheResponses", cacheResponses}, {"outputTokens", outputTokens}};
}

AiSettings AiSettings::fromJson(const Json& json) {
    AiSettings result;
    result.remote = json.value("remote", false);
    result.endpoint = json.value("endpoint", result.endpoint);
    result.remoteModel = json.value("remoteModel", std::string{});
    result.ollamaBase = json.value("ollamaBase", result.ollamaBase);
    result.localModel = json.value("localModel", result.localModel);
    result.keyHeader = json.value("keyHeader", result.keyHeader);
    result.bearer = json.value("bearer", true);
    result.fallback = json.value("fallback", true);
    result.cacheResponses = json.value("cacheResponses", true);
    result.outputTokens = std::clamp(json.value("outputTokens", 1024), 64, 2048);
    validateEndpoint(result.endpoint);
    validateEndpoint(result.ollamaBase);
    return result;
}

AiState runCompletion(const AiRequest& input, DiskCache& cache, const HttpTransport& transport,
    const AiObserver& observe, std::stop_token stop) {
    AiState state;
    state.busy = true;
    if (!input.messages.is_array() || input.messages.empty() || input.messages.size() > 16 || input.messages.dump().size() > 48 * 1024) {
        throw std::runtime_error("Chat context must contain 1-16 messages and fit within 48 KB.");
    }
    struct Provider { AiProtocol protocol; std::string endpoint; std::string model; std::string name; };
    std::vector<Provider> providers;
    if (input.settings.remote) {
        providers.push_back({AiProtocol::OpenAI, input.settings.endpoint, input.settings.remoteModel, "OpenAI-compatible"});
    }
    if (!input.settings.remote || input.settings.fallback) {
        auto base = input.settings.ollamaBase;
        while (!base.empty() && base.back() == '/') { base.pop_back(); }
        providers.push_back({AiProtocol::Ollama, base + "/api/chat", input.settings.localModel, "Ollama"});
    }
    std::string failures;
    for (const auto& provider : providers) {
        if (stop.stop_requested()) { break; }
        state.text.clear();
        state.error.clear();
        state.provider = provider.name + " / " + provider.model;
        state.status = failures.empty() ? "Connecting" : "Falling back to Ollama";
        state.cached = false;
        state.tokens = 0;
        observe(state);
        try {
            if (provider.model.empty()) { throw std::runtime_error("No model selected for " + provider.name + "."); }
            if (provider.protocol == AiProtocol::Ollama && provider.model.ends_with(":cloud")) {
                throw std::runtime_error("Choose an installed local model; cloud-backed Ollama models are not used as offline fallback.");
            }
            validateEndpoint(provider.endpoint);
            Json body{{"model", provider.model}, {"messages", input.messages}, {"stream", true}};
            if (provider.protocol == AiProtocol::Ollama) {
                body["options"] = {{"num_predict", std::clamp(input.settings.outputTokens, 64, 2048)}, {"num_ctx", 4096}, {"temperature", 0.4}};
                body["keep_alive"] = "1m";
            } else {
                body["max_tokens"] = std::clamp(input.settings.outputTokens, 64, 2048);
                body["temperature"] = 0.4;
            }
            const auto key = sha256("velos-ai-v1:" + input.projectScope + ":" + provider.endpoint + ":" + body.dump()
                + ":" + (provider.protocol == AiProtocol::OpenAI ? sha256(input.credential) : "local"));
            if (input.settings.cacheResponses) {
                if (const auto cached = cache.get(key, std::chrono::hours(24 * 7))) {
                    const auto json = Json::parse(reinterpret_cast<const char*>(cached->data()),
                        reinterpret_cast<const char*>(cached->data() + cached->size()), nullptr, false);
                    if (json.is_object() && json.contains("text") && json["text"].is_string() && json["text"].get_ref<const std::string&>().size() <= 64 * 1024) {
                        state.text = json.at("text").get<std::string>();
                        state.tokens = json.value("tokens", std::uint64_t{});
                        state.cached = true;
                        state.busy = false;
                        state.status = "Cached response";
                        observe(state);
                        return state;
                    }
                }
            }
            HttpRequest request;
            request.url = provider.endpoint;
            request.body = body.dump();
            request.timeoutSeconds = provider.protocol == AiProtocol::Ollama ? 120 : 45;
            request.keyHeader = input.settings.keyHeader;
            request.bearer = input.settings.bearer;
            if (provider.protocol == AiProtocol::OpenAI) { request.key = input.credential; }
            StreamDecoder decoder(provider.protocol, [&](std::string_view text) {
                state.text.append(text);
                state.status = "Receiving";
                observe(state);
            });
            try {
                transport(request, [&](std::string_view bytes) { decoder.feed(bytes); }, stop);
                decoder.finish();
            } catch (...) {
                if (!request.key.empty()) { SecureZeroMemory(request.key.data(), request.key.size()); }
                throw;
            }
            if (!request.key.empty()) { SecureZeroMemory(request.key.data(), request.key.size()); }
            if (stop.stop_requested()) { break; }
            if (state.text.empty()) { throw std::runtime_error("The model completed without a text response."); }
            state.tokens = decoder.outputTokens();
            if (input.settings.cacheResponses) {
                const auto cached = Json{{"text", state.text}, {"tokens", state.tokens}}.dump();
                static_cast<void>(cache.put(key, std::as_bytes(std::span(cached.data(), cached.size()))));
            }
            state.busy = false;
            state.status = failures.empty() ? "Complete" : "Complete via local fallback";
            observe(state);
            return state;
        } catch (const std::exception& error) {
            if (stop.stop_requested()) { break; }
            failures += provider.name + ": " + error.what() + "\n";
        }
    }
    state.text.clear();
    state.busy = false;
    state.cancelled = stop.stop_requested();
    state.status = state.cancelled ? "Cancelled" : "AI unavailable";
    state.error = state.cancelled ? std::string{} : failures;
    observe(state);
    return state;
}

Assistant::Assistant(std::filesystem::path cachePath) : cache_(std::move(cachePath), 64 * 1024 * 1024) {}
Assistant::~Assistant() { cancel(); if (worker_.joinable()) { worker_.join(); } }

void Assistant::publish(const AiState& state) {
    const std::lock_guard lock(mutex_);
    const auto models = state_.models;
    state_ = state;
    if (state_.models.empty()) { state_.models = models; }
}

bool Assistant::request(AiRequest input) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) { return false; }
    if (worker_.joinable()) { worker_.join(); }
    AiState pending;
    pending.busy = true;
    pending.status = "Queued";
    publish(pending);
    worker_ = std::jthread([this, input = std::move(input)](std::stop_token stop) mutable {
        try { runCompletion(input, cache_, streamHttp, [&](const AiState& state) { publish(state); }, stop); }
        catch (const std::exception& error) {
            AiState failed;
            failed.status = "AI unavailable";
            failed.error = error.what();
            publish(failed);
        }
        if (!input.credential.empty()) { SecureZeroMemory(input.credential.data(), input.credential.size()); }
        busy_ = false;
    });
    return true;
}

bool Assistant::discoverModels(const std::string& baseUrl) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) { return false; }
    if (worker_.joinable()) { worker_.join(); }
    AiState pending;
    pending.busy = true;
    pending.status = "Loading local models";
    publish(pending);
    worker_ = std::jthread([this, baseUrl](std::stop_token stop) {
        AiState result;
        try {
            auto base = baseUrl;
            while (!base.empty() && base.back() == '/') { base.pop_back(); }
            HttpRequest request;
            request.url = base + "/api/tags";
            request.timeoutSeconds = 10;
            std::string response;
            streamHttp(request, [&](std::string_view text) { response.append(text); }, stop);
            const auto json = Json::parse(response);
            for (const auto& model : json.at("models")) {
                if (result.models.size() >= 128) { break; }
                const auto name = model.at("name").get<std::string>();
                if (!name.ends_with(":cloud")) { result.models.push_back(name); }
            }
            result.status = result.models.empty() ? "No local models installed" : "Local models loaded";
        } catch (const std::exception& error) {
            result.status = stop.stop_requested() ? "Cancelled" : "Ollama unavailable";
            result.error = stop.stop_requested() ? std::string{} : error.what();
        }
        publish(result);
        busy_ = false;
    });
    return true;
}

void Assistant::cancel() { if (worker_.joinable()) { worker_.request_stop(); } }

AiState Assistant::state() const {
    const std::lock_guard lock(mutex_);
    auto result = state_;
    result.busy = busy_.load();
    return result;
}

}