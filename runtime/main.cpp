#include "assets/Package.h"
#include "bridge/Extract.h"
#include "core/FixedStepClock.h"
#include "physics/Gameplay.h"
#include "platform/Files.h"
#include "platform/Window.h"
#include "rhi/d3d12/Renderer.h"
#include "render/FrameMetrics.h"

#include <objbase.h>
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    std::filesystem::path scenePath;
    std::filesystem::path package;
    std::filesystem::path output;
    std::filesystem::path capture;
    std::filesystem::path report;
    std::uint32_t stressCount = 0;
    bool instancing = true;
    bool lods = true;
    std::string adapter = "auto";
    int frameLimit = 0;
    bool debug = false;
    bool explicitScene = false;
#ifndef NDEBUG
    debug = true;
#endif
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument.starts_with("--scene=")) { scenePath = velos::wide(argument.substr(8)); explicitScene = true; }
        else if (argument.starts_with("--package=")) { package = velos::wide(argument.substr(10)); }
        else if (argument.starts_with("--output=")) { output = velos::wide(argument.substr(9)); }
        else if (argument.starts_with("--capture=")) { capture = velos::wide(argument.substr(10)); }
        else if (argument.starts_with("--adapter=")) { adapter = argument.substr(10); }
        else if (argument.starts_with("--frames=")) { frameLimit = std::stoi(argument.substr(9)); }
        else if (argument == "--debug-gpu") { debug = true; }
        else if (argument == "--no-debug-gpu") { debug = false; }
        else if (argument == "--no-instancing") { instancing = false; }
        else if (argument == "--no-lods") { lods = false; }
        else if (argument.starts_with("--stress=")) { stressCount = static_cast<std::uint32_t>(std::stoul(argument.substr(9))); }
        else if (argument.starts_with("--report=")) { report = velos::wide(argument.substr(9)); }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { return 1; }
    int result = 0;
    try {
        if (!package.empty()) {
            if (output.empty()) { throw std::runtime_error("An empty --output directory is required for packaging."); }
            velos::packageScene(package, output, velos::executableDirectory());
            std::cout << "Packaged scene: " << velos::utf8(output.native()) << '\n';
            CoUninitialize();
            return 0;
        }
        if (scenePath.empty()) { scenePath = velos::executableDirectory() / L"game.velos"; }
        if (std::filesystem::exists(scenePath.parent_path() / L"velos-build.json")) { velos::verifyPackage(scenePath.parent_path()); }
        auto scene = velos::Scene::demo();
        if (std::filesystem::exists(scenePath)) {
            std::string error;
            if (!scene.deserialize(velos::readText(scenePath), error)) { throw std::runtime_error(error); }
        } else if (explicitScene) {
            throw std::runtime_error("The requested scene does not exist.");
        }
        if (stressCount != 0) { scene = velos::Scene::stress(stressCount); }
        velos::Window window(velos::wide(scene.name) + L" | Velos", 1280, 800);
        if (frameLimit == 0) { FreeConsole(); }
        velos::Renderer renderer(window.handle(), adapter, debug);
        velos::DiskCache cache(velos::localDataDirectory() / L"cache" / L"geometry", 512 * 1024 * 1024);
        velos::DiskCache textureCache(velos::localDataDirectory() / L"cache" / L"textures", 512 * 1024 * 1024);
        for (const auto id : scene.entities()) {
            const auto* mesh = scene.get<velos::MeshRenderer>(id);
            if (mesh && !renderer.hasMesh(mesh->mesh)) {
                renderer.addMesh(mesh->mesh, velos::loadGlb(velos::projectAssetPath(scenePath.parent_path(), mesh->mesh), cache));
            }
            if (mesh) {
                for (std::size_t slot = 0; slot < mesh->textures.size(); ++slot) {
                    const auto& reference = mesh->textures[slot];
                    if (reference.empty()) { continue; }
                    const auto type = static_cast<velos::TextureSlot>(slot);
                    const auto key = velos::textureKey(reference, type);
                    if (!renderer.hasTexture(key)) {
                        renderer.addTexture(key, velos::loadTexture(velos::projectAssetPath(scenePath.parent_path(), reference), {type}, textureCache));
                    }
                }
            }
        }
        velos::PhysicsWorld physics;
        physics.start(scene);
        velos::FixedStepClock clock;
        velos::RenderFrame frame;
        frame.instancing = instancing;
        frame.lods = lods;
        velos::FrameMetrics metrics;
        frame.camera.set2D(scene.twoDimensional);
        frame.objects.reserve(scene.entities().size());
        window.inputHandler = [&](HWND, UINT message, WPARAM wparam, LPARAM) -> LRESULT {
            if (message == WM_MOUSEWHEEL) { frame.camera.zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA); return 1; }
            return 0;
        };
        auto previous = std::chrono::steady_clock::now();
        POINT previousCursor{};
        GetCursorPos(&previousCursor);
        int frameIndex = 0;
        while (!window.closeRequested) {
            const auto frameStarted = std::chrono::steady_clock::now();
            window.pump();
            if (window.minimized()) { WaitMessage(); previous = std::chrono::steady_clock::now(); continue; }
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = frameLimit > 0 ? 1.0 / 60 : std::chrono::duration<double>(now - previous).count();
            previous = now;
            const bool focused = GetForegroundWindow() == window.handle();
            const auto down = [&](int key) { return focused && (GetAsyncKeyState(key) & 0x8000) != 0; };
            if (down(VK_ESCAPE)) { window.closeRequested = true; }
            POINT cursor{};
            GetCursorPos(&cursor);
            if (down(VK_RBUTTON)) { frame.camera.orbit(static_cast<float>(cursor.x - previousCursor.x), static_cast<float>(cursor.y - previousCursor.y)); }
            previousCursor = cursor;
            const auto tick = clock.advance(elapsed);
            for (std::uint32_t step = 0; step < tick.steps; ++step) {
                velos::driveBodies(scene, physics, frame.camera, static_cast<float>(down('D')) - static_cast<float>(down('A')),
                    static_cast<float>(down('W')) - static_cast<float>(down('S')));
                scene.tick(clock.stepSeconds());
                physics.step(scene, static_cast<float>(clock.stepSeconds()));
            }
            renderer.resizeWindow(window.width(), window.height());
            frame.camera.aspect = static_cast<float>(window.width()) / static_cast<float>(window.height());
            velos::extractScene(scene, frame, 0, true);
            renderer.render(frame, nullptr, frameLimit == 0);
            if (!report.empty() && frameIndex >= 30) {
                metrics.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStarted).count(), renderer.stats().gpuMilliseconds);
            }
            ++frameIndex;
            if (frameLimit > 0 && frameIndex >= frameLimit) { break; }
        }
        renderer.waitIdle();
        if (!capture.empty()) { renderer.capture(capture); }
        const auto errors = renderer.validationErrors();
        if (!errors.empty()) { std::cerr << errors; result = 2; }
        const auto statistics = renderer.stats();
        if (!report.empty()) { velos::writeTextAtomic(report, metrics.report(statistics, instancing, lods).dump(2)); }
        std::cout << "Camera draws: " << statistics.cameraDraws << " | Shadow draws: " << statistics.shadowDraws
            << " | LOD triangles saved: " << statistics.lodTrianglesSaved << '\n';
        std::cout << "Runtime adapter: " << statistics.adapter << "\nFrames: " << frameIndex
            << "\nGPU scene: " << statistics.gpuMilliseconds << " ms\nD3D12 validation: "
            << (!statistics.debugLayer ? "not available" : errors.empty() ? "PASS" : "FAIL") << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Velos runtime: " << error.what() << '\n';
        if (frameLimit == 0 && package.empty()) { MessageBoxW(nullptr, velos::wide(error.what()).c_str(), L"Velos runtime error", MB_OK | MB_ICONERROR); }
        result = 1;
    }
    CoUninitialize();
    return result;
}