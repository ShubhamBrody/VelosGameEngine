#pragma once

#include <Windows.h>
#include <cstdint>
#include <functional>
#include <string>

namespace velos {

class Window {
public:
    Window(const std::wstring& title, int width = 1600, int height = 1000);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    void pump();
    void resize(int width, int height);
    [[nodiscard]] HWND handle() const noexcept { return handle_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] bool minimized() const noexcept { return minimized_; }
    bool closeRequested = false;
    std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> inputHandler;

private:
    HWND handle_ = nullptr;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
    bool minimized_ = false;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
};

}