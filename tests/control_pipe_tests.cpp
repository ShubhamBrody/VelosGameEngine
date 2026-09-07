#include "automation/ControlPipe.h"

#include <Windows.h>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace {
using Json = nlohmann::json;
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }

Json callPipe(const std::string& name, const std::string& message, bool oversize = false) {
    const auto path = L"\\\\.\\pipe\\" + std::wstring(name.begin(), name.end());
    const auto pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    require(pipe != INVALID_HANDLE_VALUE, "Current-user client must be able to connect.");
    try {
        DWORD written = 0;
        std::uint32_t size = oversize ? velos::automation::ControlPipe::maximumMessageBytes + 1 : static_cast<std::uint32_t>(message.size());
        require(WriteFile(pipe, &size, sizeof(size), &written, nullptr) && written == sizeof(size), "Write request header.");
        if (!oversize) { require(WriteFile(pipe, message.data(), size, &written, nullptr) && written == size, "Write request body."); }
        DWORD read = 0;
        require(ReadFile(pipe, &size, sizeof(size), &read, nullptr) && read == sizeof(size), "Read response header.");
        require(size > 0 && size <= velos::automation::ControlPipe::maximumMessageBytes, "Response must be bounded.");
        std::string response(size, '\0');
        std::size_t offset = 0;
        while (offset < response.size()) {
            require(ReadFile(pipe, response.data() + offset, static_cast<DWORD>(response.size() - offset), &read, nullptr) && read > 0, "Read response payload.");
            offset += read;
        }
        const std::byte receipt{1};
        require(WriteFile(pipe, &receipt, 1, &written, nullptr) && written == 1, "Acknowledge complete response.");
        CloseHandle(pipe);
        return Json::parse(response);
    } catch (...) { CloseHandle(pipe); throw; }
}
}

int main() {
    const auto wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    try {
        require(wake != nullptr, "Create test event.");
        const auto owner = std::this_thread::get_id();
        const auto name = "velos-control-test-" + std::to_string(GetCurrentProcessId());
        {
            velos::automation::ControlPipe server(name, [wake] { SetEvent(wake); });
            auto client = std::async(std::launch::async, [&] { return callPipe(name, Json{{"id", "first"}, {"method", "echo"}, {"params", {{"value", 42}}}}.dump()); });
            require(WaitForSingleObject(wake, 5000) == WAIT_OBJECT_0, "A queued request must wake the owner.");
            require(server.pump([&](std::string_view method, const Json& params) {
                require(std::this_thread::get_id() == owner && method == "echo", "Handlers must execute on the owner thread.");
                return params;
            }) == 1, "Exactly one request must execute.");
            const auto result = client.get();
            require(result.at("ok") == true && result.at("result").at("value") == 42 && result.at("id") == "first", "Roundtrip must preserve IDs and structured values.");
            require(!callPipe(name, "not JSON").at("ok").get<bool>(), "Malformed JSON must be rejected without owner dispatch.");
            require(callPipe(name, "", true).at("error").at("code") == "message_budget", "Oversized requests must be rejected before allocation.");
            auto failure = std::async(std::launch::async, [&] { return callPipe(name, Json{{"id", "error"}, {"method", "fail"}}.dump()); });
            require(WaitForSingleObject(wake, 5000) == WAIT_OBJECT_0, "Failure request must reach the owner.");
            server.pump([](std::string_view, const Json&) -> Json { throw velos::automation::Error("test_error", "Expected failure"); });
            require(failure.get().at("error").at("code") == "test_error", "Structured engine errors must survive the transport.");
            require(server.statistics().at("completed") == 2 && server.statistics().at("rejected") == 2, "Control counters must reflect real requests.");
            bool duplicateRejected = false;
            try { velos::automation::ControlPipe duplicate(name); } catch (const std::exception&) { duplicateRejected = true; }
            require(duplicateRejected, "An occupied control endpoint must not be hijacked.");
        }
        const auto started = std::chrono::steady_clock::now();
        {
            velos::automation::ControlPipe idle(name);
        }
        require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2), "Idle pipe shutdown must cancel accept waits promptly.");
        bool invalidRejected = false;
        try { velos::automation::ControlPipe invalid("other\\service"); } catch (const std::exception&) { invalidRejected = true; }
        require(invalidRejected, "Endpoint names must stay in the Velos namespace.");
        CloseHandle(wake);
        std::cout << "PASS: private control IPC, main-thread dispatch, framed JSON, limits, errors, endpoint ownership and shutdown.\n";
        return 0;
    } catch (const std::exception& error) {
        if (wake) { CloseHandle(wake); }
        std::cerr << error.what() << '\n';
        return 1;
    }
}