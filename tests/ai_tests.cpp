#include "ai/Assistant.h"
#include "platform/Files.h"

#include <Windows.h>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }

void testStreams() {
    const std::string openAi = "data: {\"choices\":[{\"delta\":{\"content\":\"Hello \"}}]}\r\n\r\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"world\"}}]}\n\n"
        "data: [DONE]\n\n";
    std::string text;
    velos::StreamDecoder decoder(velos::AiProtocol::OpenAI, [&](std::string_view chunk) { text.append(chunk); });
    for (const char character : openAi) { decoder.feed(std::string_view(&character, 1)); }
    decoder.finish();
    require(text == "Hello world", "SSE must survive arbitrary byte boundaries.");
    text.clear();
    velos::StreamDecoder local(velos::AiProtocol::Ollama, [&](std::string_view chunk) { text.append(chunk); });
    const std::string ndjson = "{\"message\":{\"content\":\"local\"},\"done\":false}\n{\"done\":true,\"eval_count\":2}";
    for (std::size_t index = 0; index < ndjson.size(); index += 3) { local.feed(std::string_view(ndjson).substr(index, 3)); }
    local.finish();
    require(text == "local" && local.outputTokens() == 2, "NDJSON final event and token count.");
    bool rejected = false;
    try { velos::StreamDecoder broken(velos::AiProtocol::Ollama, [](auto) {}); broken.feed("not json\n"); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Malformed provider JSON must fail.");
    rejected = false;
    try { velos::StreamDecoder truncated(velos::AiProtocol::OpenAI, [](auto) {}); truncated.feed("data: {}\n\n"); truncated.finish(); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Truncated responses must never count as completed.");
}

void testFallback(const std::filesystem::path& path) {
    velos::DiskCache cache(path, 1024 * 1024);
    velos::AiRequest request;
    request.settings.remote = true;
    request.settings.remoteModel = "remote-test";
    request.settings.localModel = "local-test";
    request.messages = nlohmann::json::array({{{"role", "user"}, {"content", "Test the engine"}}});
    request.projectScope = "isolated-test-project";
    int remoteCalls = 0;
    int localCalls = 0;
    const velos::HttpTransport mock = [&](const velos::HttpRequest& http, const velos::HttpSink& sink, std::stop_token) {
        if (http.url.starts_with("https://")) { ++remoteCalls; throw std::runtime_error("Mock cloud unavailable"); }
        ++localCalls;
        require(http.key.empty(), "Cloud credentials must not be sent to Ollama fallback.");
        sink("{\"message\":{\"content\":\"Fallback response\"},\"done\":false}\n");
        sink("{\"done\":true,\"eval_count\":3}\n");
    };
    const auto first = velos::runCompletion(request, cache, mock, [](const auto&) {});
    require(first.text == "Fallback response" && first.error.empty() && remoteCalls == 1 && localCalls == 1, "Cloud failure must fall back to local chat.");
    const auto second = velos::runCompletion(request, cache, mock, [](const auto&) {});
    require(second.cached && localCalls == 1, "Exact local response must be reused without inference.");
    request.projectScope = "another-project";
    static_cast<void>(velos::runCompletion(request, cache, mock, [](const auto&) {}));
    require(localCalls == 2, "AI responses must not leak across project scopes.");
    request.settings.fallback = false;
    const auto unavailable = velos::runCompletion(request, cache, mock, [](const auto&) {});
    require(unavailable.text.empty() && !unavailable.error.empty(), "Unavailable AI must return a bounded error state.");
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelled = velos::runCompletion(request, cache, mock, [](const auto&) {}, cancellation.get_token());
    require(cancelled.cancelled && cancelled.text.empty(), "Cancellation must not start another provider.");
}

}

int main() {
    const auto path = std::filesystem::temp_directory_path() / (L"VelosAiTest-" + std::to_wstring(GetCurrentProcessId()));
    try {
        testStreams();
        velos::validateEndpoint("http://localhost:11434/api/chat");
        velos::validateEndpoint("https://example.com/v1/chat/completions");
        bool denied = false;
        try { velos::validateEndpoint("http://example.com/v1/chat/completions"); }
        catch (const std::runtime_error&) { denied = true; }
        require(denied, "Remote plaintext HTTP must be denied.");
        denied = false;
        try { velos::validateEndpoint("https://secret@example.com/v1"); }
        catch (const std::runtime_error&) { denied = true; }
        require(denied, "Embedded URL credentials must be denied.");
        testFallback(path);
        std::filesystem::remove_all(path);
        std::cout << "PASS: streaming protocols, truncation, endpoint security, fallback, scoped cache and cancellation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}