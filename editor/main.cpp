#include "Editor.h"
#include "platform/Files.h"
#include "platform/Window.h"

#include <Windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <ImGuizmo.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

int main(int argc, char** argv) {
    bool smoke = false;
    bool selfTest = false;
    bool debug = false;
#ifndef NDEBUG
    debug = true;
#endif
    int frameLimit = 0;
    int width = 1600;
    int height = 1000;
    std::string adapter = "auto";
    std::filesystem::path capture;
    std::filesystem::path scenePath;
    std::filesystem::path automationRoot;
    std::string controlPipe;
    bool automationReadOnly = false;
    bool isolated = false;
    std::string graphView;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument.starts_with("--adapter=")) { adapter = argument.substr(10); }
        else if (argument.starts_with("--frames=")) { frameLimit = std::stoi(argument.substr(9)); smoke = true; }
        else if (argument.starts_with("--capture=")) { capture = velos::wide(argument.substr(10)); }
        else if (argument.starts_with("--scene=")) { scenePath = velos::wide(argument.substr(8)); }
        else if (argument.starts_with("--width=")) { width = std::stoi(argument.substr(8)); }
        else if (argument.starts_with("--height=")) { height = std::stoi(argument.substr(9)); }
        else if (argument == "--self-test") { selfTest = true; smoke = true; frameLimit = 12; }
        else if (argument == "--debug-gpu") { debug = true; }
        else if (argument == "--no-debug-gpu") { debug = false; }
        else if (argument.starts_with("--control-pipe=")) { controlPipe = argument.substr(15); }
        else if (argument.starts_with("--automation-root=")) { automationRoot = velos::wide(argument.substr(18)); }
        else if (argument == "--automation-read-only") { automationReadOnly = true; }
        else if (argument == "--isolated") { isolated = true; }
        else if (argument.starts_with("--graph=")) { graphView = argument.substr(8); }
        else if (argument.starts_with("--write-sample=")) {
            velos::writeTextAtomic(velos::wide(argument.substr(15)), velos::Scene::demo().serialize());
            std::cout << "Sample scene written.\n";
            return 0;
        }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) { std::cerr << "COM initialization failed.\n"; return 1; }
    if (!smoke) { FreeConsole(); }
    int result = 0;
    try {
        velos::Window window(L"Velos | Workshop", width, height);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        {
            velos::Renderer renderer(window.handle(), adapter, debug);
            velos::Editor editor(window.handle(), renderer, smoke || isolated);
            renderer.initializeUi();
            window.inputHandler = ImGui_ImplWin32_WndProcHandler;
            if (!scenePath.empty() && !editor.openScene(scenePath)) { throw std::runtime_error("The requested scene could not be loaded."); }
            if (!graphView.empty()) { editor.showGraph(graphView); }
            if (!controlPipe.empty()) {
                editor.enableAutomation(controlPipe, automationRoot, automationReadOnly);
                std::cout << "VELOS_CONTROL_READY " << controlPipe << std::endl;
            }
            if (selfTest) { editor.runAuthoringCheck(); }
            auto previous = std::chrono::steady_clock::now();
            int frameIndex = 0;
            while (!editor.wantsClose()) {
                window.pump();
                if (window.closeRequested) {
                    window.closeRequested = false;
                    if (smoke) { break; }
                    editor.requestClose();
                }
                if (window.minimized() && !editor.automationEnabled()) { WaitMessage(); continue; }
                const auto now = std::chrono::steady_clock::now();
                const double elapsed = smoke ? 1.0 / 60.0 : std::chrono::duration<double>(now - previous).count();
                previous = now;
                renderer.resizeWindow(window.width(), window.height());
                renderer.newUiFrame();
                ImGui::NewFrame();
                ImGuizmo::BeginFrame();
                editor.pumpAutomation();
                editor.update(elapsed);
                editor.draw(elapsed);
                ImGui::Render();
                renderer.render(editor.frame(), ImGui::GetDrawData(), smoke ? false : editor.vsync());
                editor.renderedAutomationFrame();
                ++frameIndex;
                if (selfTest && frameIndex == 4) { window.resize(1100, 740); editor.resetLayout(); }
                if (selfTest && frameIndex == 8) {
                    window.resize(width, height);
                    editor.resetLayout();
                    if (!graphView.empty()) { editor.showGraph(graphView); }
                }
                if (frameLimit > 0 && frameIndex >= frameLimit) { break; }
            }
            renderer.waitIdle();
            if (!capture.empty()) { renderer.capture(capture); }
            const auto errors = renderer.validationErrors();
            if (!errors.empty()) { std::cerr << errors; result = 2; }
            const auto statistics = renderer.stats();
            std::cout << "Adapter: " << statistics.adapter << "\nFrames: " << frameIndex
                << "\nGPU scene: " << statistics.gpuMilliseconds << " ms\nDraws: " << statistics.drawCalls
                << "\nTriangles: " << statistics.triangles << "\nVisible objects: " << statistics.visibleObjects
                << "\nD3D12 validation: " << (!statistics.debugLayer ? "not available" : errors.empty() ? "PASS" : "FAIL") << '\n';
            renderer.shutdownUi();
            window.inputHandler = {};
        }
        ImGui::DestroyContext();
    } catch (const std::exception& error) {
        std::cerr << "Velos: " << error.what() << '\n';
        if (!smoke) { MessageBoxW(nullptr, velos::wide(error.what()).c_str(), L"Velos startup error", MB_OK | MB_ICONERROR); }
        result = 1;
    }
    CoUninitialize();
    return result;
}