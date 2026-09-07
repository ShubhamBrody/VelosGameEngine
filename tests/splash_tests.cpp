#include "platform/StartupSplash.h"
#include "platform/Window.h"
#include "platform/BrandResources.h"
#include "platform/Files.h"

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
}

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto initialized = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) { return 1; }
    int result = 0;
    try {
        const auto directory = argc > 1 ? std::filesystem::path(velos::wide(argv[1]))
            : std::filesystem::temp_directory_path() / (L"VelosSplash-" + std::to_wstring(GetCurrentProcessId()));
        const auto instance = GetModuleHandleW(nullptr);
        require(FindResourceW(instance,MAKEINTRESOURCEW(VELOS_BRAND_ICON),RT_GROUP_ICON) != nullptr,"Application icon must be embedded.");
        require(FindResourceW(instance,MAKEINTRESOURCEW(VELOS_BRAND_SPLASH),RT_RCDATA) != nullptr,"Splash bitmap must be embedded.");
        velos::Window mainWindow(L"Splash test",1024,720,false);
        require(!IsWindowVisible(mainWindow.handle()) && mainWindow.width() == 1024 && mainWindow.height() == 720,"Main-window dimensions must remain correct before presentation.");
        require(GetClassLongPtrW(mainWindow.handle(),GCLP_HICON) != 0 && GetClassLongPtrW(mainWindow.handle(),GCLP_HICONSM) != 0,"Native windows must have large and small app icons.");
        for (const UINT dpi : {96u,144u,192u,288u}) {
            velos::StartupSplash splash(L"Editor",dpi);
            require(splash.visible(),"The CPU-drawn splash must be visible before creating D3D12.");
            splash.setStatus(L"Preparing graphics",0.4f);
            const auto capture = directory / (L"splash-" + std::to_wstring(dpi) + L".png");
            splash.capture(capture);
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"Create capture decoder.");
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            require(SUCCEEDED(factory->CreateDecoderFromFilename(capture.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)),"Native splash capture must be a valid PNG.");
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> image;
            require(SUCCEEDED(decoder->GetFrame(0,&image)),"Read splash capture.");
            UINT width = 0;
            UINT height = 0;
            image->GetSize(&width,&height);
            require(width >= 320 && height >= 180 && width * 9 == height * 16,"Splash must preserve framing and usable dimensions at each DPI.");
            bool rejected = false;
            try { splash.setStatus(L"Invalid",std::numeric_limits<float>::quiet_NaN()); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected,"Invalid progress must not corrupt painting.");
            const auto handle = splash.handle();
            splash.dismiss();
            splash.dismiss();
            require(!splash.visible() && !IsWindow(handle),"Dismiss must safely release the splash window.");
            MSG message{};
            require(!PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE),"Dismissing a splash must not quit the main application.");
        }
        mainWindow.show();
        require(IsWindowVisible(mainWindow.handle()),"The main window must become visible when startup completes.");
        std::cout << "PASS: embedded logo/icon resources, pre-render splash, DPI framing, captures and clean main-window handoff.\n";
        std::cout << "Captures: " << velos::utf8(directory.native()) << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    CoUninitialize();
    return result;
}