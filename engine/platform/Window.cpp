#include "platform/Window.h"

#include <algorithm>
#include <stdexcept>

namespace velos {

Window::Window(const std::wstring& title, int width, int height) {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.lpszClassName = L"VelosNativeWindow";
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot register the native window class.");
    }
    RECT rectangle{0, 0, width, height};
    AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0);
    handle_ = CreateWindowExW(0, windowClass.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        nullptr, nullptr, instance, this);
    if (!handle_) { throw std::runtime_error("Cannot create the native editor window."); }
    RECT client{};
    if (!GetClientRect(handle_, &client)) { throw std::runtime_error("Cannot query native window dimensions."); }
    width_ = static_cast<std::uint32_t>(std::max<LONG>(1, client.right - client.left));
    height_ = static_cast<std::uint32_t>(std::max<LONG>(1, client.bottom - client.top));
    ShowWindow(handle_, SW_SHOWDEFAULT);
    UpdateWindow(handle_);
}

Window::~Window() { if (handle_) { DestroyWindow(handle_); } }

void Window::pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) { closeRequested = true; }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Window::resize(int width, int height) {
    RECT rectangle{0,0,width,height};
    AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0);
    SetWindowPos(handle_, nullptr, 0, 0, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK Window::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<Window*>(creation->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && self->inputHandler && self->inputHandler(window, message, wparam, lparam)) { return 1; }
    switch (message) {
    case WM_SIZE:
        if (self) {
            self->minimized_ = wparam == SIZE_MINIMIZED;
            self->width_ = static_cast<std::uint32_t>(LOWORD(lparam));
            self->height_ = static_cast<std::uint32_t>(HIWORD(lparam));
        }
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
            suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        if (self) { self->closeRequested = true; }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize = {900, 600};
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) { return 0; }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}