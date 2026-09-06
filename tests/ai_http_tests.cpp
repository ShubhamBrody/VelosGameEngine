#include "ai/Assistant.h"
#include "platform/Files.h"

#include <Windows.h>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Expected the loopback test server URL.\n"; return 1; }
    const std::string base = argv[1];
    const auto path = std::filesystem::temp_directory_path() / (L"VelosHttpTest-" + std::to_wstring(GetCurrentProcessId()));
    const auto credentialEndpoint = base + "/test-credential";
    try {
        velos::DiskCache cache(path, 1024 * 1024);
        velos::AiRequest request;
        request.projectScope = "native-http-test";
        request.settings.remote = true;
        request.settings.endpoint = base + "/openai";
        request.settings.remoteModel = "test-remote";
        request.settings.ollamaBase = base + "/ollama";
        request.settings.localModel = "test-local";
        request.credential = "disposable-test-value";
        request.messages = nlohmann::json::array({{{"role", "user"}, {"content", "Network test"}}});
        const auto remote = velos::runCompletion(request, cache, velos::streamHttp, [](const auto&) {});
        require(remote.error.empty() && remote.text == "Native caf\xc3\xa9", "WinHTTP SSE streaming failed.");
        request.settings.endpoint = base + "/fail";
        const auto fallback = velos::runCompletion(request, cache, velos::streamHttp, [](const auto&) {});
        require(fallback.error.empty() && fallback.text == "Native local fallback", "WinHTTP cloud-to-Ollama fallback failed.");
        velos::HttpRequest cancellation;
        cancellation.url = base + "/cancel";
        cancellation.body = "{}";
        std::atomic_bool cancelled = false;
        std::atomic_bool announced = false;
        std::promise<void> receivedChunk;
        auto ready = receivedChunk.get_future();
        const auto started = std::chrono::steady_clock::now();
        std::jthread network([&](std::stop_token stop) {
            try {
                velos::streamHttp(cancellation, [&](std::string_view) {
                    if (!announced.exchange(true)) { receivedChunk.set_value(); }
                }, stop);
            } catch (const std::runtime_error&) { cancelled = stop.stop_requested(); }
        });
        const bool received = ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        network.request_stop();
        network.join();
        require(received && cancelled && std::chrono::steady_clock::now() - started < std::chrono::seconds(5), "Cross-thread native request cancellation was not responsive.");
        velos::saveCredential(credentialEndpoint, "disposable-credential-test");
        require(velos::loadCredential(credentialEndpoint) == "disposable-credential-test", "Windows credential round trip failed.");
        velos::removeCredential(credentialEndpoint);
        require(velos::loadCredential(credentialEndpoint).empty(), "Credential deletion failed.");
        std::filesystem::remove_all(path);
        std::cout << "PASS: native WinHTTP SSE/NDJSON, fallback, cancellation, credential storage and cleanup.\n";
        return 0;
    } catch (const std::exception& error) {
        try { velos::removeCredential(credentialEndpoint); } catch (...) {}
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}