#include "ai/StreamDecoder.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace velos {

StreamDecoder::StreamDecoder(AiProtocol protocol, std::function<void(std::string_view)> output)
    : protocol_(protocol), output_(std::move(output)) {}

void StreamDecoder::feed(std::string_view bytes) {
    received_ += bytes.size();
    if (received_ > 4 * 1024 * 1024) { throw std::runtime_error("AI response exceeded its 4 MB transport limit."); }
    pending_.append(bytes);
    std::size_t offset = 0;
    for (;;) {
        const auto newline = pending_.find('\n', offset);
        if (newline == std::string::npos) { break; }
        auto value = std::string_view(pending_).substr(offset, newline - offset);
        if (!value.empty() && value.back() == '\r') { value.remove_suffix(1); }
        line(value);
        offset = newline + 1;
    }
    pending_.erase(0, offset);
    if (pending_.size() > 256 * 1024 || event_.size() > 256 * 1024) { throw std::runtime_error("AI stream event exceeded its size limit."); }
}

void StreamDecoder::line(std::string_view value) {
    if (protocol_ == AiProtocol::Ollama) {
        if (!value.empty()) { decode(value); }
    } else if (value.empty()) {
        if (!event_.empty()) { decode(event_); event_.clear(); }
    } else if (value.starts_with("data:")) {
        value.remove_prefix(5);
        if (!value.empty() && value.front() == ' ') { value.remove_prefix(1); }
        if (!event_.empty()) { event_.push_back('\n'); }
        event_.append(value);
        if (event_.size() > 256 * 1024) { throw std::runtime_error("AI stream event exceeded its size limit."); }
    }
}

void StreamDecoder::decode(std::string_view value) {
    if (done_) { return; }
    if (protocol_ == AiProtocol::OpenAI && value == "[DONE]") { done_ = true; return; }
    const auto json = nlohmann::json::parse(value.begin(), value.end(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) { throw std::runtime_error("Provider returned malformed streaming JSON."); }
    if (json.contains("error")) { throw std::runtime_error("Provider rejected the request. Check the endpoint and selected model."); }
    std::string text;
    if (protocol_ == AiProtocol::Ollama) {
        if (json.contains("message")) {
            const auto& message = json.at("message");
            if (message.contains("tool_calls") && !message.at("tool_calls").empty()) { throw std::runtime_error("This preview supports chat responses, not tool execution."); }
            text = message.value("content", std::string{});
        } else { text = json.value("response", std::string{}); }
        done_ = json.value("done", false);
        if (json.contains("eval_count")) { outputTokens_ = json.at("eval_count").get<std::uint64_t>(); }
    } else {
        if (json.contains("usage") && json.at("usage").is_object()) {
            outputTokens_ = json.at("usage").value("completion_tokens", std::uint64_t{});
        }
        if (json.contains("choices") && json.at("choices").is_array() && !json.at("choices").empty()) {
            const auto& choice = json.at("choices")[0];
            if (choice.contains("delta")) {
                const auto& delta = choice.at("delta");
                if (delta.contains("tool_calls") || delta.contains("function_call")) { throw std::runtime_error("This preview supports chat responses, not tool execution."); }
                if (delta.contains("content") && !delta.at("content").is_null()) { text = delta.at("content").get<std::string>(); }
            }
        }
    }
    outputBytes_ += text.size();
    if (outputBytes_ > 64 * 1024) { throw std::runtime_error("AI output exceeded its 64 KB text limit."); }
    if (!text.empty()) { output_(text); }
}

void StreamDecoder::finish() {
    if (!pending_.empty()) { line(pending_); pending_.clear(); }
    if (!event_.empty()) { decode(event_); event_.clear(); }
    if (!done_) { throw std::runtime_error("AI stream ended before completion; no response was cached."); }
}

}