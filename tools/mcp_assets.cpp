#include "platform/Files.h"

#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void checkImage(Gdiplus::Status status) { if (status != Gdiplus::Ok) { throw std::runtime_error("Cannot generate the sample label image."); } }
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: velos_mcp_assets <empty-output-directory>\n"; return 1; }
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token,&startup,nullptr) != Gdiplus::Ok) { return 1; }
    int result = 0;
    try {
        const std::filesystem::path output = velos::wide(argv[1]);
        if (std::filesystem::exists(output) && !std::filesystem::is_empty(output)) { throw std::runtime_error("Sample asset destination must be empty."); }
        std::filesystem::create_directories(output);
        UINT count = 0;
        UINT size = 0;
        checkImage(Gdiplus::GetImageEncodersSize(&count,&size));
        std::vector<std::byte> encoders(size);
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoders.data());
        checkImage(Gdiplus::GetImageEncoders(count,size,codecs));
        const Gdiplus::ImageCodecInfo* png = nullptr;
        for (UINT index = 0; index < count; ++index) { if (std::wstring_view(codecs[index].MimeType) == L"image/png") { png = &codecs[index]; } }
        if (!png) { throw std::runtime_error("PNG encoder is unavailable."); }
        struct Label { const wchar_t* text; const wchar_t* file; int width; int height; float fontSize; };
        const std::array labels{
            Label{L"1",L"one.png",256,256,180}, Label{L"2",L"two.png",256,256,180}, Label{L"3",L"three.png",256,256,180},
            Label{L"SIGNAL ROOM",L"title.png",1120,224,128}, Label{L"TARGET",L"target.png",768,192,110}, Label{L"ALIGNED",L"aligned.png",768,192,110}
        };
        for (const auto& label : labels) {
            Gdiplus::Bitmap bitmap(label.width,label.height,PixelFormat32bppARGB);
            checkImage(bitmap.GetLastStatus());
            Gdiplus::Graphics graphics(&bitmap);
            checkImage(graphics.Clear(Gdiplus::Color(0,0,0,0)));
            checkImage(graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit));
            Gdiplus::Font font(L"Segoe UI",label.fontSize,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
            checkImage(font.GetLastStatus());
            Gdiplus::SolidBrush brush(Gdiplus::Color(255,229,238,234));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            checkImage(graphics.DrawString(label.text,-1,&font,Gdiplus::RectF(0,0,static_cast<float>(label.width),static_cast<float>(label.height)),&format,&brush));
            checkImage(bitmap.Save((output / label.file).c_str(),&png->Clsid,nullptr));
        }
        std::cout << "Generated six sample PNG labels.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    Gdiplus::GdiplusShutdown(token);
    return result;
}