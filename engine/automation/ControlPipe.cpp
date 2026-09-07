#include "automation/ControlPipe.h"

#include <Windows.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace velos::automation {
namespace {

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) { CloseHandle(value); } }
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value && value != INVALID_HANDLE_VALUE) { CloseHandle(value); }
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

struct LocalFreeMemory { void operator()(void* pointer) const { if (pointer) { LocalFree(pointer); } } };
using LocalMemory = std::unique_ptr<void, LocalFreeMemory>;

LocalMemory ownerSecurity() {
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) { throw Error("ipc_security", "Cannot query the editor's Windows identity."); }
    Handle token(rawToken);
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token.value, TokenUser, nullptr, 0, &size));
    std::vector<std::byte> data(size);
    if (!GetTokenInformation(token.value, TokenUser, data.data(), size, &size)) { throw Error("ipc_security", "Cannot read the editor's Windows SID."); }
    LPWSTR rawSid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(data.data())->User.Sid, &rawSid)) { throw Error("ipc_security", "Cannot encode Windows SID."); }
    LocalMemory sid(rawSid);
    const std::wstring policy = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(rawSid) + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(policy.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) { throw Error("ipc_security", "Cannot restrict editor control to the current Windows user."); }
    return LocalMemory(descriptor);
}

Json failure(std::string code, std::string message, const Json& id = nullptr) {
    message.resize(std::min<std::size_t>(message.size(), 2048));
    return {{"id", id}, {"ok", false}, {"error", {{"code", std::move(code)}, {"message", std::move(message)}}}};
}

bool finishIo(HANDLE pipe, OVERLAPPED& operation, HANDLE stopped, DWORD timeout, DWORD& transferred) {
    const HANDLE events[]{stopped, operation.hEvent};
    if (WaitForMultipleObjects(2, events, FALSE, timeout) != WAIT_OBJECT_0 + 1) {
        static_cast<void>(CancelIoEx(pipe, &operation));
        static_cast<void>(GetOverlappedResult(pipe, &operation, &transferred, TRUE));
        return false;
    }
    return GetOverlappedResult(pipe, &operation, &transferred, FALSE) != FALSE;
}

bool transfer(HANDLE pipe, HANDLE stopped, void* data, std::size_t size, bool writing) {
    auto* cursor = static_cast<std::byte*>(data);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (size != 0) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) { return false; }
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) { return false; }
        OVERLAPPED operation{};
        operation.hEvent = event.value;
        DWORD transferred = 0;
        const auto count = static_cast<DWORD>(std::min<std::size_t>(size, 64 * 1024));
        const bool immediate = writing ? WriteFile(pipe, cursor, count, &transferred, &operation) != FALSE
            : ReadFile(pipe, cursor, count, &transferred, &operation) != FALSE;
        if (!immediate && (GetLastError() != ERROR_IO_PENDING || !finishIo(pipe, operation, stopped, static_cast<DWORD>(remaining), transferred))) { return false; }
        if (transferred == 0) { return false; }
        cursor += transferred;
        size -= transferred;
    }
    return true;
}

}

struct ControlPipe::Impl {
    struct Request {
        Json id;
        std::string method;
        Json params;
        std::size_t bytes = 0;
        std::atomic_bool abandoned = false;
        std::promise<Json> completion;
    };
    std::string name;
    std::function<void()> wake;
    LocalMemory security;
    Handle stopped;
    std::array<Handle, 4> pipes;
    std::vector<std::jthread> workers;
    mutable std::mutex mutex;
    std::deque<std::shared_ptr<Request>> pending;
    std::size_t pendingBytes = 0;
    bool closing = false;
    std::atomic_uint64_t completed = 0;
    std::atomic_uint64_t rejected = 0;

    Impl(std::string pipeName, std::function<void()> wakeOwner) : name(std::move(pipeName)), wake(std::move(wakeOwner)), security(ownerSecurity()),
        stopped(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (!name.starts_with("velos-") || name.size() > 96 || !std::all_of(name.begin(), name.end(), [](unsigned char letter) {
            return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '-' || letter == '_';
        })) { throw Error("invalid_pipe", "Control pipe names must start with velos- and contain only ASCII letters, digits, dash or underscore."); }
        if (!stopped.value) { throw Error("ipc_startup", "Cannot create the control shutdown event."); }
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.get(), FALSE};
        const auto path = L"\\\\.\\pipe\\" + std::wstring(name.begin(), name.end());
        for (std::size_t index = 0; index < pipes.size(); ++index) {
            const DWORD flags = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (index == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
            pipes[index] = Handle(CreateNamedPipeW(path.c_str(), flags, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                static_cast<DWORD>(pipes.size()), 64 * 1024, 64 * 1024, 1000, &attributes));
            if (pipes[index].value == INVALID_HANDLE_VALUE) { throw Error("ipc_startup", "Cannot create the private control pipe; its name may already be in use."); }
        }
        try {
            for (auto& pipe : pipes) { workers.emplace_back([this, handle = pipe.value] { serve(handle); }); }
        } catch (...) { SetEvent(stopped.value); workers.clear(); throw; }
    }

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            closing = true;
            for (auto& request : pending) { request->completion.set_value(failure("shutdown", "The editor is shutting down.", request->id)); }
            pending.clear();
            pendingBytes = 0;
        }
        SetEvent(stopped.value);
        workers.clear();
    }

    Json enqueue(std::string_view payload) {
        bool tooDeep = false;
        auto input = Json::parse(payload.begin(), payload.end(), [&](int depth, Json::parse_event_t, Json&) { if (depth > 32) { tooDeep = true; } return !tooDeep; }, false);
        if (tooDeep || input.is_discarded() || !input.is_object() || !input.contains("id") || !input["id"].is_string()
            || input["id"].get_ref<const std::string&>().size() > 128 || !input.contains("method") || !input["method"].is_string()
            || input["method"].get_ref<const std::string&>().size() > 128 || !input.value("params", Json::object()).is_object()) {
            ++rejected;
            return failure("invalid_request", "Expected a bounded request with string id, method and object params.");
        }
        auto request = std::make_shared<Request>();
        request->id = input.at("id");
        request->method = input.at("method").get<std::string>();
        request->params = input.value("params", Json::object());
        request->bytes = payload.size();
        auto future = request->completion.get_future();
        {
            std::lock_guard lock(mutex);
            if (closing) { return failure("shutdown", "The editor is shutting down.", request->id); }
            if (pending.size() >= 32 || pendingBytes + request->bytes > 32 * 1024 * 1024) {
                ++rejected;
                return failure("busy", "The automation queue is full; retry after the current requests complete.", request->id);
            }
            pendingBytes += request->bytes;
            pending.push_back(request);
        }
        if (wake) { wake(); }
        if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
            request->abandoned = true;
            return failure("request_timeout", "The editor did not finish in time. Read current state before retrying a mutation; an executing request may complete.", request->id);
        }
        return future.get();
    }

    void serve(HANDLE pipe) {
        while (WaitForSingleObject(stopped.value, 0) == WAIT_TIMEOUT) {
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event.value) { return; }
            OVERLAPPED connection{};
            connection.hEvent = event.value;
            DWORD transferred = 0;
            if (!ConnectNamedPipe(pipe, &connection)) {
                const auto error = GetLastError();
                if (error != ERROR_PIPE_CONNECTED && (error != ERROR_IO_PENDING || !finishIo(pipe, connection, stopped.value, INFINITE, transferred))) { return; }
            }
            try {
                std::uint32_t length = 0;
                if (transfer(pipe, stopped.value, &length, sizeof(length), false)) {
                    Json response;
                    if (length == 0 || length > maximumMessageBytes) { response = failure("message_budget", "Control messages must contain 1-16777216 bytes."); ++rejected; }
                    else {
                        std::string payload(length, '\0');
                        if (transfer(pipe, stopped.value, payload.data(), payload.size(), false)) { response = enqueue(payload); }
                    }
                    if (!response.is_null()) {
                        auto output = response.dump();
                        if (output.size() > maximumMessageBytes) { output = failure("message_budget", "Response exceeds the message budget; request a smaller page.").dump(); }
                        length = static_cast<std::uint32_t>(output.size());
                        if (transfer(pipe, stopped.value, &length, sizeof(length), true)
                            && transfer(pipe, stopped.value, output.data(), output.size(), true)) {
                            std::byte receipt{};
                            static_cast<void>(transfer(pipe, stopped.value, &receipt, 1, false));
                        }
                    }
                }
            } catch (const std::exception&) { ++rejected; }
            static_cast<void>(DisconnectNamedPipe(pipe));
        }
    }
};

ControlPipe::ControlPipe(std::string name, std::function<void()> wakeOwner) : impl_(std::make_unique<Impl>(std::move(name), std::move(wakeOwner))) {}
ControlPipe::~ControlPipe() = default;
const std::string& ControlPipe::name() const noexcept { return impl_->name; }

std::size_t ControlPipe::pump(const Handler& handler, std::size_t maximum) {
    std::size_t count = 0;
    while (count < maximum) {
        std::shared_ptr<Impl::Request> request;
        {
            std::lock_guard lock(impl_->mutex);
            if (impl_->pending.empty()) { break; }
            request = impl_->pending.front();
            impl_->pending.pop_front();
            impl_->pendingBytes -= request->bytes;
        }
        Json response;
        if (request->abandoned) { response = failure("request_expired", "The request expired before execution.", request->id); }
        else {
            try { response = {{"id", request->id}, {"ok", true}, {"result", handler(request->method, request->params)}}; }
            catch (const Error& error) { response = failure(error.code(), error.what(), request->id); }
            catch (const std::exception& error) { response = failure("operation_failed", error.what(), request->id); }
        }
        request->completion.set_value(std::move(response));
        ++impl_->completed;
        ++count;
    }
    return count;
}

Json ControlPipe::statistics() const {
    std::lock_guard lock(impl_->mutex);
    return {{"pipe", impl_->name}, {"authentication", "Windows current-user ACL"}, {"remote_clients", false},
        {"queued", impl_->pending.size()}, {"queued_bytes", impl_->pendingBytes}, {"completed", impl_->completed.load()},
        {"rejected", impl_->rejected.load()}, {"max_message_bytes", maximumMessageBytes}};
}

}