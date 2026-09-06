#include "ai/Assistant.h"
#include "platform/Files.h"

#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: velos_ai_probe <installed-local-model>\n"; return 1; }
    try {
        velos::DiskCache cache(velos::localDataDirectory() / L"cache" / L"ai-probe", 1024 * 1024);
        velos::AiRequest request;
        request.settings.localModel = argv[1];
        request.settings.outputTokens = 64;
        request.settings.cacheResponses = false;
        request.projectScope = "connection-probe";
        request.messages = nlohmann::json::array({{{"role", "user"}, {"content", "Reply in one short sentence: what is a game engine?"}}});
        const auto start = std::chrono::steady_clock::now();
        std::size_t printed = 0;
        const auto response = velos::runCompletion(request, cache, velos::streamHttp, [&](const velos::AiState& state) {
            if (state.text.size() > printed) {
                std::cout << state.text.substr(printed) << std::flush;
                printed = state.text.size();
            }
        });
        if (!response.error.empty()) { std::cerr << response.error; return 1; }
        std::cout << "\nProvider: " << response.provider << "\nReported output tokens: " << response.tokens
            << "\nDuration: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}