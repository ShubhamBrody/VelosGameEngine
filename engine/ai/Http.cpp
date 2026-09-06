#include "ai/Http.h"
#include "platform/Files.h"

#include <Windows.h>
#include <winhttp.h>
#include <wincred.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <stdexcept>

namespace velos {
namespace {

struct Url {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port;
    bool secure;
};

Url parseUrl(std::string_view url) {
    if (url.size() > 4096 || url.find('#') != std::string_view::npos) { throw std::runtime_error("Invalid AI endpoint URL."); }
    const auto text = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(text.c_str(), static_cast<DWORD>(text.size()), 0, &parts)
        || parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) {
        throw std::runtime_error("AI endpoints must be HTTP(S) URLs without embedded credentials.");
    }
    Url result{std::wstring(parts.lpszHostName, parts.dwHostNameLength),
        std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength), parts.nPort, parts.nScheme == INTERNET_SCHEME_HTTPS};
    if (parts.dwExtraInfoLength) { result.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength); }
    const bool loopback = _wcsicmp(result.host.c_str(), L"localhost") == 0 || result.host == L"127.0.0.1"
        || result.host == L"[::1]" || result.host == L"::1";
    if (parts.nScheme != INTERNET_SCHEME_HTTPS && !(parts.nScheme == INTERNET_SCHEME_HTTP && loopback)) {
        throw std::runtime_error("Remote AI endpoints require HTTPS. Plain HTTP is allowed only on loopback.");
    }
    if (result.path.empty()) { result.path = L"/"; }
    return result;
}

class HttpHandle {
public:
    explicit HttpHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~HttpHandle() { close(); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    [[nodiscard]] HINTERNET get() const { return handle_.load(); }
    void close() { if (const auto handle = handle_.exchange(nullptr)) { WinHttpCloseHandle(handle); } }
private:
    std::atomic<HINTERNET> handle_;
};

void checkHttp(BOOL success, std::stop_token stop) {
    if (stop.stop_requested()) { throw std::runtime_error("Request cancelled."); }
    if (!success) { throw std::runtime_error("AI network operation failed (Windows error " + std::to_string(GetLastError()) + ")."); }
}

std::wstring credentialName(const std::string& endpoint) { return L"Velos/AI/" + wide(sha256(endpoint)); }

}

void validateEndpoint(std::string_view url) { static_cast<void>(parseUrl(url)); }

void streamHttp(const HttpRequest& request, const HttpSink& sink, std::stop_token stop) {
    const auto url = parseUrl(request.url);
    if (stop.stop_requested()) { throw std::runtime_error("Request cancelled."); }
    if (request.body.size() > 128 * 1024 || request.key.size() > 2048) { throw std::runtime_error("AI request exceeds its configured size limit."); }
    if (request.key.find_first_of("\r\n") != std::string::npos || request.keyHeader.empty()
        || !std::all_of(request.keyHeader.begin(), request.keyHeader.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '-';
        })) { throw std::runtime_error("Invalid credential header."); }
    const HttpHandle session(WinHttpOpen(L"Velos/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    checkHttp(session.get() != nullptr, stop);
    const int timeout = std::clamp(request.timeoutSeconds, 5, 120) * 1000;
    checkHttp(WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, timeout), stop);
    const HttpHandle connection(WinHttpConnect(session.get(), url.host.c_str(), url.port, 0));
    checkHttp(connection.get() != nullptr, stop);
    HttpHandle active(WinHttpOpenRequest(connection.get(), request.body.empty() ? L"GET" : L"POST", url.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0));
    checkHttp(active.get() != nullptr, stop);
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    checkHttp(WinHttpSetOption(active.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)), stop);
    DWORD logonPolicy = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    checkHttp(WinHttpSetOption(active.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &logonPolicy, sizeof(logonPolicy)), stop);
    std::wstring headers = L"Content-Type: application/json\r\nAccept: text/event-stream, application/x-ndjson, application/json\r\n";
    if (!request.key.empty()) {
        headers += wide(request.keyHeader) + L": " + (request.bearer ? L"Bearer " : L"") + wide(request.key) + L"\r\n";
    }
    const auto nativeHandle = active.get();
    const std::stop_callback cancel(stop, [&active] { active.close(); });
    const auto started = std::chrono::steady_clock::now();
    const BOOL sent = WinHttpSendRequest(nativeHandle, headers.c_str(), static_cast<DWORD>(headers.size()),
        request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
        static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()), 0);
    SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
    checkHttp(sent, stop);
    checkHttp(WinHttpReceiveResponse(nativeHandle, nullptr), stop);
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    checkHttp(WinHttpQueryHeaders(nativeHandle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX), stop);
    if (status < 200 || status >= 300) {
        throw std::runtime_error("AI endpoint returned HTTP " + std::to_string(status) + ". Check the URL, credentials and model.");
    }
    std::array<char, 4096> buffer{};
    std::size_t total = 0;
    for (;;) {
        if (stop.stop_requested()) { throw std::runtime_error("Request cancelled."); }
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(request.timeoutSeconds)) {
            throw std::runtime_error("AI request deadline exceeded.");
        }
        DWORD available = 0;
        checkHttp(WinHttpQueryDataAvailable(nativeHandle, &available), stop);
        if (available == 0) { break; }
        DWORD received = 0;
        checkHttp(WinHttpReadData(nativeHandle, buffer.data(), std::min(available, static_cast<DWORD>(buffer.size())), &received), stop);
        if (received == 0) { break; }
        total += received;
        if (total > 4 * 1024 * 1024) { throw std::runtime_error("AI transport response limit exceeded."); }
        sink(std::string_view(buffer.data(), received));
    }
}

void saveCredential(const std::string& endpoint, std::string_view secret) {
    validateEndpoint(endpoint);
    if (secret.empty() || secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) { throw std::runtime_error("Credential is empty or too long."); }
    auto name = credentialName(endpoint);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = name.data();
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential, 0)) { throw std::runtime_error("Windows Credential Manager could not save the API key."); }
}

std::string loadCredential(const std::string& endpoint) {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(credentialName(endpoint).c_str(), CRED_TYPE_GENERIC, 0, &credential)) { return {}; }
    std::string value(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return value;
}

void removeCredential(const std::string& endpoint) {
    if (!CredDeleteW(credentialName(endpoint).c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
        throw std::runtime_error("Windows Credential Manager could not remove the API key.");
    }
}

}