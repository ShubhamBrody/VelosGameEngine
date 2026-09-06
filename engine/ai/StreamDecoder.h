#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace velos {

enum class AiProtocol { OpenAI, Ollama };

class StreamDecoder {
public:
    StreamDecoder(AiProtocol protocol, std::function<void(std::string_view)> output);
    void feed(std::string_view bytes);
    void finish();
    [[nodiscard]] bool done() const noexcept { return done_; }
    [[nodiscard]] std::uint64_t outputTokens() const noexcept { return outputTokens_; }

private:
    AiProtocol protocol_;
    std::function<void(std::string_view)> output_;
    std::string pending_;
    std::string event_;
    std::size_t received_ = 0;
    std::size_t outputBytes_ = 0;
    std::uint64_t outputTokens_ = 0;
    bool done_ = false;
    void line(std::string_view value);
    void decode(std::string_view value);
};

}