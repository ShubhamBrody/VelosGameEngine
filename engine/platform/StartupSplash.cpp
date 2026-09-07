#include "platform/StartupSplash.h"
#include "platform/BrandResources.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace velos {
using Microsoft::WRL::ComPtr;
namespace {

void checkSplash(HRESULT result, const char* operation) {
    if (FAILED(result)) { throw std::runtime_error(std::string(operation) + " failed."); }
}

struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    BYTE* pixels = nullptr;

    ~Surface() {
        if (dc && previous) { SelectObject(dc,previous); }
        if (bitmap) { DeleteObject(bitmap); }
        if (dc) { DeleteDC(dc); }
    }

    void create(UINT width, UINT height) {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO description{};
        description.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        description.bmiHeader.biWidth = static_cast<LONG>(width);
        description.bmiHeader.biHeight = -static_cast<LONG>(height);
        description.bmiHeader.biPlanes = 1;
        description.bmiHeader.biBitCount = 32;
        description.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc,&description,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        if (!dc || !bitmap || !pixels) { throw std::runtime_error("Cannot allocate splash drawing surface."); }
        previous = SelectObject(dc,bitmap);
    }
};

}

struct StartupSplash::Impl {
    HWND window = nullptr;
    std::wstring caption;
    std::wstring status = L"Starting Velos";
    float progress = 0;
    UINT imageWidth = 0;
    UINT imageHeight = 0;
    std::vector<BYTE> pixels;
    ComPtr<IWICImagingFactory> factory;

    explicit Impl(std::wstring title, UINT requestedDpi) : caption(std::move(title)) {
        const auto instance = GetModuleHandleW(nullptr);
        const auto resource = FindResourceW(instance,MAKEINTRESOURCEW(VELOS_BRAND_SPLASH),RT_RCDATA);
        if (!resource) { throw std::runtime_error("Embedded Velos splash artwork is missing."); }
        const auto bytes = SizeofResource(instance,resource);
        const auto memory = LoadResource(instance,resource);
        auto* source = static_cast<BYTE*>(LockResource(memory));
        if (!source || bytes == 0 || bytes > 4 * 1024 * 1024) { throw std::runtime_error("Invalid embedded splash image."); }
        checkSplash(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create splash decoder");
        ComPtr<IWICStream> stream;
        checkSplash(factory->CreateStream(&stream),"Create splash stream");
        checkSplash(stream->InitializeFromMemory(source,bytes),"Read splash resource");
        ComPtr<IWICBitmapDecoder> decoder;
        checkSplash(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode splash image");
        ComPtr<IWICBitmapFrameDecode> image;
        checkSplash(decoder->GetFrame(0,&image),"Read splash frame");
        checkSplash(image->GetSize(&imageWidth,&imageHeight),"Read splash dimensions");
        if (imageWidth != 1280 || imageHeight != 720) { throw std::runtime_error("Unexpected splash image dimensions."); }
        ComPtr<IWICFormatConverter> converter;
        checkSplash(factory->CreateFormatConverter(&converter),"Create splash pixel converter");
        checkSplash(converter->Initialize(image.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert splash pixels");
        pixels.resize(static_cast<std::size_t>(imageWidth) * imageHeight * 4);
        checkSplash(converter->CopyPixels(nullptr,imageWidth * 4,static_cast<UINT>(pixels.size()),pixels.data()),"Load splash pixels");

        WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
        windowClass.lpfnWndProc = procedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance,MAKEINTRESOURCEW(VELOS_BRAND_ICON));
        windowClass.lpszClassName = L"VelosStartupSplash";
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { throw std::runtime_error("Cannot register splash window."); }
        const auto foreground = GetForegroundWindow();
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromWindow(foreground,MONITOR_DEFAULTTOPRIMARY),&monitor)) { throw std::runtime_error("Cannot position splash window."); }
        const auto dpi = requestedDpi != 0 ? requestedDpi : foreground ? GetDpiForWindow(foreground) : 96;
        const auto availableWidth = monitor.rcWork.right - monitor.rcWork.left;
        const auto availableHeight = monitor.rcWork.bottom - monitor.rcWork.top;
        const auto scale = std::min({static_cast<float>(dpi ? dpi : 96) / 96,
            static_cast<float>(availableWidth) * 0.9f / 640,static_cast<float>(availableHeight) * 0.9f / 360});
        const int units = std::max(1,static_cast<int>(40 * scale));
        const int width = units * 16;
        const int height = units * 9;
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,windowClass.lpszClassName,L"Velos startup",WS_POPUP,
            monitor.rcWork.left + (availableWidth - width) / 2,monitor.rcWork.top + (availableHeight - height) / 2,
            width,height,nullptr,nullptr,instance,this);
        if (!window) { throw std::runtime_error("Cannot create splash window."); }
        ShowWindow(window,SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }

    ~Impl() { if (window) { DestroyWindow(window); } }

    void paint(HDC target, UINT width, UINT height) const {
        BITMAPINFO image{};
        image.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        image.bmiHeader.biWidth = static_cast<LONG>(imageWidth);
        image.bmiHeader.biHeight = -static_cast<LONG>(imageHeight);
        image.bmiHeader.biPlanes = 1;
        image.bmiHeader.biBitCount = 32;
        image.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(target,HALFTONE);
        SetBrushOrgEx(target,0,0,nullptr);
        StretchDIBits(target,0,0,static_cast<int>(width),static_cast<int>(height),0,0,
            static_cast<int>(imageWidth),static_cast<int>(imageHeight),pixels.data(),&image,DIB_RGB_COLORS,SRCCOPY);
        const auto horizontal = [&](int value) { return MulDiv(value,static_cast<int>(width),1280); };
        const auto vertical = [&](int value) { return MulDiv(value,static_cast<int>(height),720); };
        const auto brush = CreateSolidBrush(RGB(72,221,176));
        RECT bar{horizontal(64),vertical(624),horizontal(64 + static_cast<int>(1152 * progress)),vertical(628)};
        FillRect(target,&bar,brush);
        DeleteObject(brush);
        const auto font = CreateFontW(-std::max(11,horizontal(24)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto previous = SelectObject(target,font);
        SetBkMode(target,TRANSPARENT);
        SetTextColor(target,RGB(183,200,191));
        RECT captionRect{horizontal(64),vertical(86),horizontal(920),vertical(124)};
        DrawTextW(target,caption.c_str(),-1,&captionRect,DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT statusRect{horizontal(64),vertical(648),horizontal(996),vertical(695)};
        DrawTextW(target,status.c_str(),-1,&statusRect,DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(target,RGB(126,150,135));
        RECT versionRect{horizontal(1020),vertical(648),horizontal(1216),vertical(695)};
        DrawTextW(target,L"0.1.0",-1,&versionRect,DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(target,previous);
        DeleteObject(font);
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<const CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (message == WM_ERASEBKGND) { return 1; }
        if (message == WM_CLOSE) { ShowWindow(window,SW_HIDE); return 0; }
        if (message == WM_DESTROY) { return 0; }
        if (self && (message == WM_PAINT || message == WM_PRINTCLIENT)) {
            RECT rectangle{};
            GetClientRect(window,&rectangle);
            const auto width = static_cast<UINT>(std::max<LONG>(1,rectangle.right));
            const auto height = static_cast<UINT>(std::max<LONG>(1,rectangle.bottom));
            if (message == WM_PRINTCLIENT) { self->paint(reinterpret_cast<HDC>(wparam),width,height); return 0; }
            PAINTSTRUCT paint{};
            const auto target = BeginPaint(window,&paint);
            try {
                Surface buffer;
                buffer.create(width,height);
                self->paint(buffer.dc,width,height);
                BitBlt(target,0,0,static_cast<int>(width),static_cast<int>(height),buffer.dc,0,0,SRCCOPY);
            } catch (...) { self->paint(target,width,height); }
            EndPaint(window,&paint);
            return 0;
        }
        return DefWindowProcW(window,message,wparam,lparam);
    }
};

StartupSplash::StartupSplash(std::wstring caption, UINT dpi) : impl_(std::make_unique<Impl>(std::move(caption),dpi)) {}
StartupSplash::~StartupSplash() = default;
bool StartupSplash::visible() const noexcept { return impl_->window && IsWindowVisible(impl_->window); }
HWND StartupSplash::handle() const noexcept { return impl_->window; }

void StartupSplash::setStatus(std::wstring text, float progress) {
    if (!std::isfinite(progress) || progress < 0 || progress > 1) { throw std::invalid_argument("Splash progress must be in [0,1]."); }
    text.resize(std::min<std::size_t>(text.size(),256));
    impl_->status = std::move(text);
    impl_->progress = progress;
    if (impl_->window) { InvalidateRect(impl_->window,nullptr,FALSE); UpdateWindow(impl_->window); }
}

void StartupSplash::dismiss() noexcept {
    if (impl_->window) { DestroyWindow(impl_->window); impl_->window = nullptr; }
}

void StartupSplash::capture(const std::filesystem::path& path) const {
    if (!impl_->window) { throw std::runtime_error("Cannot capture a dismissed splash."); }
    RECT client{};
    GetClientRect(impl_->window,&client);
    const auto width = static_cast<UINT>(client.right);
    const auto height = static_cast<UINT>(client.bottom);
    Surface image;
    image.create(width,height);
    SendMessageW(impl_->window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT);
    GdiFlush();
    const auto bytes = width * height * 4;
    for (std::size_t offset = 3; offset < bytes; offset += 4) { image.pixels[offset] = 255; }
    if (!path.parent_path().empty()) { std::filesystem::create_directories(path.parent_path()); }
    ComPtr<IWICStream> stream;
    checkSplash(impl_->factory->CreateStream(&stream),"Create splash capture stream");
    checkSplash(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Open splash capture");
    ComPtr<IWICBitmapEncoder> encoder;
    checkSplash(impl_->factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create splash PNG encoder");
    checkSplash(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize splash PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    checkSplash(encoder->CreateNewFrame(&frame,&properties),"Create splash PNG frame");
    checkSplash(frame->Initialize(properties.Get()),"Initialize splash PNG frame");
    checkSplash(frame->SetSize(width,height),"Set splash PNG size");
    auto format = GUID_WICPixelFormat32bppBGRA;
    checkSplash(frame->SetPixelFormat(&format),"Set splash PNG pixels");
    if (format != GUID_WICPixelFormat32bppBGRA) { throw std::runtime_error("PNG encoder rejected splash pixels."); }
    checkSplash(frame->WritePixels(height,width * 4,bytes,image.pixels),"Write splash capture");
    checkSplash(frame->Commit(),"Commit splash PNG frame");
    checkSplash(encoder->Commit(),"Commit splash PNG");
}

}