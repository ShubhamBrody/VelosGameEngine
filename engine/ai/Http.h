#pragma once

#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace velos {

struct HttpRequest {
    std::string url;
    std::string body;
    std::string key;
    std::string keyHeader = "Authorization";
    bool bearer = true;
    int timeoutSeconds = 60;
};

using HttpSink = std::function<void(std::string_view)>;
using HttpTransport = std::function<void(const HttpRequest&, const HttpSink&, std::stop_token)>;

void validateEndpoint(std::string_view url);
void streamHttp(const HttpRequest& request, const HttpSink& sink, std::stop_token stop);
void saveCredential(const std::string& endpoint, std::string_view secret);
std::string loadCredential(const std::string& endpoint);
void removeCredential(const std::string& endpoint);

}