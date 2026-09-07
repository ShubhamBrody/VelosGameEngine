#pragma once

#include <Windows.h>
#include <filesystem>
#include <memory>
#include <string>

namespace velos {

class StartupSplash {
public:
    explicit StartupSplash(std::wstring caption = L"Editor", UINT dpi = 0);
    ~StartupSplash();
    StartupSplash(const StartupSplash&) = delete;
    StartupSplash& operator=(const StartupSplash&) = delete;
    void setStatus(std::wstring text, float progress);
    void dismiss() noexcept;
    void capture(const std::filesystem::path& path) const;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] HWND handle() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}