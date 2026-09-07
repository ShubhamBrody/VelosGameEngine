# Velos Engine

> Codename `Velos` - a placeholder name. Rename freely before Milestone 1.

A **native Windows engine/editor preview**, implemented in C++20 with D3D12 GPU rendering,
Jolt physics, an EnTT scene model, budgeted disk caching, and an asynchronous AI assistant with
OpenAI-compatible chat endpoints and **Ollama** fallback.

The control preview adds **60 MCP tools**, live scene/class/logic graphs, executable gameplay
behaviors, numeric variables and saved game cameras to the textured/DXR graphics preview.
This is not the completed v1.0 roadmap;
the implemented features and current limits below describe what is actually available.

This is **not** a Unity/Unreal clone. The goal is a lean, understandable, genuinely fast engine
where you can author a 3D or 2D scene, script it, and ship a standalone build.

---

## Pillars

| # | Pillar | What it means in practice |
|---|--------|---------------------------|
| 1 | **Measured GPU acceleration** | GPU rendering with optional accelerated culling/skinning/particles. CPU-authoritative gameplay and physics; transfers, synchronization and memory are included in performance decisions. |
| 2 | **Low-end first** | Separate provisional iGPU and 2 GB discrete-GPU profiles. Actual minimum hardware and frame-time gates must be confirmed before implementation commitments. |
| 3 | **Smart caching** | Dependency-aware asset cache, shader/driver-specific PSO caches, fence-safe GPU residency and scoped AI response caching. All are bounded; only disk artifacts persist. |
| 4 | **Optional AI integration** | Native chat plus an opt-in MCP control adapter for external clients. Capability/revision/path limits are explicit; runtime NPC AI remains future work. |
| 5 | **Understandable** | No mega-abstractions. Explicit data flow, explicit memory, readable render graph. |

## Documents

Read them in this order:

1. [docs/PRD.md](docs/PRD.md) - *what* we are building and *why*, scope and non-goals.
2. [docs/SRS.md](docs/SRS.md) - numbered functional + non-functional requirements.
3. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - *how* it is built: layers, subsystems, data flow.
4. [docs/MILESTONES.md](docs/MILESTONES.md) - the phased delivery roadmap (M0 -> M26).
5. [docs/adr/README.md](docs/adr/README.md) - **open decisions we still need to discuss.**
6. [docs/MCP.md](docs/MCP.md) - external-editor control, all tools, graph semantics and safety limits.

## Status

| | |
|---|---|
| Phase | **MCP and graph control preview: v0.1.0-preview.3** |
| Build | Windows x64, C++20, CMake, MSVC 2022/2026; Debug and Release |
| Applications | Native scene editor and standalone game runtime |
| Validation | Fourteen native test groups; official-SDK MCP authoring/import/play/graph/capture/export workflows; read-only and concurrent-client checks |
| Scope | Partial implementations across the roadmap, not completion of every M0-M23 gate |

The roadmap has 27 work packages, not 27 mandatory blockers before a playable result. The first
release gate combines scene authoring, scripting, physics, basic lighting/audio, bounded caches,
early AI chat (M18.A) and a standalone sample (M23.A). Basic v1.0 adds 2D, UI and production
workflows. Advanced GPU-driven rendering, GI, material/shader graphs and Vulkan remain
separately gated. The current preview already exercises authoring, GPU rendering, physics,
AI chat and standalone export, while full low-end performance acceptance remains unverified.

## Implemented now

- Docked Win32/ImGui editor with scene hierarchy, filtering, selection, inspectors and native dialogs.
- Perspective/orbit and orthographic cameras, object picking, transform gizmos, snapping and focus.
- Cube, sphere, plane and unlit-quad geometry; editable colors, roughness and metallic parameters.
- Albedo, normal, occlusion/roughness/metallic and emissive texture maps; UV transforms, normal
  strength, emission, opaque/masked/sorted-transparent surfaces and single/double-sided materials.
- GPU vertex/normal transforms, GGX lighting, up to sixteen point/spot lights, a directional PCF shadow,
  analytic ground grid, tone mapping, wireframe and adjustable render/shadow resolution.
- Optional DXR 1.1 directional hard shadows, with matched raster/ray mesh LODs and explicit
  raster fallback for unsupported adapters, masked casters, wireframe and exhausted DXR budgets.
- Material-compatible GPU instancing, CPU frustum culling, screen-size LOD selection and separate
  camera/shadow draw counters. Meshoptimizer cooks optimized geometry and up to two reduced LODs.
- Stable scene-local IDs, parent transforms, cycle/depth validation, atomic save/load and bounded
  undo/redo. Play/stop restores canonical authored scene data, not raw GPU/physics handles.
  Unchanged scene extraction and undo snapshots are reused instead of serialized every idle frame.
- Fixed-step Jolt physics for primitive static/dynamic bodies, rotation behavior and keyboard drive.
- Persisted numeric variables and executable behavior graphs: Begin Play/Tick/key events,
  translation/rotation, physics velocity, variable operations, branches, visibility and material color.
- Native graph canvases for editable hierarchy relationships, component classes and executable
  logic, with saved node layout, undo and live execution highlighting. Not Unreal Blueprint compatibility.
- Official-SDK MCP stdio adapter with 60 strict tools, nine resources and an authoring prompt.
  Scene edits are atomic/revision-checked, files are workspace-restricted, and native control is
  opt-in through a current-Windows-user-only pipe. External clients can author, test and export games.
- Background import of static self-contained GLB geometry, copied into project-relative assets,
  with hashed cooked-geometry reuse. Geometry imports retain node transforms.
- Background image import through DirectXTex, with linear-light sRGB mip generation, linear data
  maps, BC1/BC3/BC5 compression and content/settings/version-keyed DDS cooking. Imported images
  are copied into project-relative assets and included in Save As and standalone exports.
- Cached DXC bytecode, integrity-checked disk entries, LRU eviction, TTL for AI results, and visible
  GPU/draw/cache counters. The preview uses Windows SHA-256 rather than the proposed BLAKE3.
- Asynchronous SSE/NDJSON chat, exact-response caching, provider fallback and cross-thread cancel.
  Context inclusion is opt-in; keys use Windows Credential Manager. The built-in chat is read-only;
  external MCP clients can use the separately enabled authoring tools.
- Standalone folder export with a file whitelist, dependency notices and SHA-256 manifest checks.
  Existing nonempty output folders are rejected rather than overwritten.

## Run

```powershell
.\tools\build.ps1 -Configuration Release -BuildDirectory out/build-control -Test -Run
```

The editor opens a ready-to-edit workshop scene. This workspace's new preview is isolated from
the original builds in [out/build-control/Release/VelosEditor.exe](out/build-control/Release/VelosEditor.exe)
and [out/build-control/Release/VelosRuntime.exe](out/build-control/Release/VelosRuntime.exe).
The scripts default to `out/build` when `-BuildDirectory` is omitted.

Saved samples:
- [samples/workshop/scene.velos](samples/workshop/scene.velos): 3D materials, lights, falling bodies,
  a rotating object and a keyboard-driven sphere.
- [samples/shapes-2d/scene.velos](samples/shapes-2d/scene.velos): orthographic unlit-quad authoring.
- [samples/graphics-lab/scene.velos](samples/graphics-lab/scene.velos): four texture maps,
  alpha-clipped geometry and a transparent panel.
- [samples/graphics-lab/ray-shadows.velos](samples/graphics-lab/ray-shadows.velos): saved DXR
  shadow policy; automatically uses raster shadows when DXR is unavailable.
- [samples/graphics-lab/spotlight.velos](samples/graphics-lab/spotlight.velos): overhead spotlight
  with a soft cone edge. Local-light shadow maps are not implemented.
- [samples/signal-room/scene.velos](samples/signal-room/scene.velos): a complete small puzzle
  authored/tested/exported through MCP, with executable switch graphs and a visible solved state.
  [Sample controls and screenshots](samples/signal-room/README.md).

```powershell
.\out\build-control\Release\VelosEditor.exe --scene=samples/signal-room/scene.velos
.\out\build-control\Release\VelosRuntime.exe --scene=samples/signal-room/scene.velos
.\tools\package.ps1 -BuildDirectory out/build-control -Scene samples/signal-room/scene.velos
```

The Export command saves the scene and builds into an empty folder you choose. Run the exported
runtime directly; it does not require the editor or AI. A tested export is available locally in
[out/graphics-preview/VelosRuntime.exe](out/graphics-preview/VelosRuntime.exe). Build output is not committed.

In the Material inspector, import/remove maps and adjust UVs, surface mode and opacity. Add a
spotlight from the Create menu and edit its cone angles in the Light inspector. The Performance
tab exposes instancing, mesh LODs, shadow resolution, the ray-shadow toggle, active/fallback status
and a 0-256 MB DXR budget. A zero budget forces raster fallback. Ray tracing is off by default
and requires a physical DXR 1.1 / Shader Model 6.5 adapter; the normal path remains SM 6.0.

## Controls

- Right-drag: orbit; middle-drag: pan; wheel: zoom; `F`: frame the selected object.
- In Edit mode, `W` / `E` / `R` select translate / rotate / scale gizmos.
- `Ctrl+S`: save; `Ctrl+Shift+S`: save as; `Ctrl+O`: open; `Ctrl+Z` / `Ctrl+Y`: undo/redo.
- `Ctrl+D`: duplicate selected entity; `Delete`: delete; hierarchy drag/drop reparents objects.
- During Play, `WASD` drives objects with Keyboard Drive. The workshop sphere has it enabled.
- Play/Pause/Step/Stop control simulation. Stop restores the authored scene. `Escape` exits runtime.
- Graphs > Scene/Classes/Logic exposes live node canvases. Drag execution links, edit node fields,
  use the Variables popup, and navigate with middle-drag/Alt-drag and the minimap.

## MCP Setup

```powershell
npm ci --prefix tools/mcp --ignore-scripts
.\tools\mcp-smoke.ps1 -BuildDirectory out/build-control -Adapter nvidia -Compact
```

[.vscode/mcp.json](.vscode/mcp.json) registers `velos-engine` for VS Code. Start it from **MCP:
List Servers** after building. It launches an isolated native editor on first tool use. Other
editors can use the same local stdio adapter; explicit attach/read-only configurations and all
60 tool descriptions are in [docs/MCP.md](docs/MCP.md). Node.js 22+ is required for MCP, not for
playing exported games. Basic MCP authoring does not depend on Ollama or a cloud account.

Automation covers the engine's implemented scene/material/light/physics/graph features, not
unimplemented audio, skeletal animation, terrain or multiplayer. It excludes arbitrary shell/code
execution and credentials. The built-in assistant and external MCP clients are separate clients.

## AI setup

Ollama must already be installed with a suitable local model. The preview defaults to
`qwen2.5-coder:1.5b`; the model-list button discovers installed models without downloading any.
The native client was tested with that installed model on the development machine.

For cloud or other compatible services, select OpenAI-compatible, open provider settings, enter
the full Chat Completions URL and model, and store the key through the app. The credential header
and Bearer prefix are configurable. The Responses API and provider-specific tool protocols are
not implemented. Do not put keys in scene files or this repository.

Requests are read-only, one at a time, and disabled/cancelled during Play. The scene-context
checkbox and preview show what extra data will be sent. Fallback uses the configured Ollama model;
cloud-backed `:cloud` model tags are excluded from offline fallback. Cloud compatibility has been
tested through a local HTTP contract server, not a paid provider account.

## Repository layout (target)

```
/docs                 Design documents, ADRs, specs
/engine
  /core               Platform-agnostic foundation (memory, jobs, math, log, reflect)
  /platform           Win32 window, input, filesystem, threads, dialogs
  /rhi                Render Hardware Interface (abstract) + backends
    /d3d12
    /vulkan           (later)
  /render             Render graph, passes, materials, lighting, post
  /assets             Importers, cooker, content-addressed cache
  /scene              ECS, transforms, hierarchy, serialization, prefabs
  /bridge             Scene-to-render snapshot extraction
  /physics            Jolt 3D and proposed Box2D integration
  /audio              Mixer + spatialisation
  /anim               Skeletal animation, blend trees, GPU skinning
  /script             C# host (.NET), binding generation
  /ai                 Provider abstraction, Ollama/OpenAI clients, cache, tools
/editor               Editor application (panels, gizmos, inspectors)
/runtime              Standalone game runtime executable
/shaders              HLSL sources + shared headers
/samples              Sample projects and the demo game
/tools                Build tooling, shader compiler driver, packaging
/tests                Unit, golden-image and performance tests
/third_party          Vendored dependencies
```

## Building

Requires Windows x64, Visual Studio 2022/2026 with Desktop development with C++, CMake tools and
the Windows SDK. The first configure downloads dependencies at immutable commit hashes.

```powershell
.\tools\build.ps1 -Configuration Debug -BuildDirectory out/build-control -Test
```

The script locates Visual Studio's bundled CMake without changing your global PATH. Dependencies
are pinned in [cmake/Dependencies.cmake](cmake/Dependencies.cmake); the C runtime is linked
statically, and SDK DXC binaries and third-party notices are copied alongside the applications.
Node.js is optional for building the engine but required for the HTTP integration test and MCP adapter.

```powershell
.\tools\smoke.ps1 -Configuration Release -BuildDirectory out/build-control -Adapter auto
.\tools\graphics-smoke.ps1 -BuildDirectory out/build-control -Adapter nvidia -RequireRayTracing
.\tools\mcp-smoke.ps1 -BuildDirectory out/build-control -Adapter warp
.\out\build-control\Release\velos_ai_probe.exe qwen2.5-coder:1.5b
```

Tests cover the clock, scene/undo, cache/files, GLB import, physics, AI protocols, real loopback
WinHTTP/credentials, packaging, texture color spaces/mips, draw planning, light/ray transforms,
atomic automation, private IPC and behavior graph validation/execution.
Graphics smoke checks generate fresh fixtures, assert the actual shadow path, compare instanced
and unbatched images, check ray/LOD self-shadowing, compare source and exported scenes, and exercise
play restoration and resizing. All GPU warnings/errors fail the run. Captures and JSON reports
are generated under `out/validation`; each graphics run uses a new directory.
The Windows CI workflow also includes the Release WARP MCP workflow. Remote CI results are
reported separately from local validation; this repository is published under ShubhamBrody.

## Development measurements

Measured on 2026-09-07 with renderer revision `628c74d`: Release, 1280x800, `--stress=1000`,
180 fixed-step frames, first 30 excluded, VSync and GPU validation disabled. The same scene/camera
is used for all three modes. CPU frame time is wall time including submission/fence/present waits;
GPU scene time covers the scene passes, not a complete application profiler.

| Adapter | Mode | Median CPU frame ms | Median GPU scene ms |
|---|---|---:|---:|
| RTX 4070 Ti SUPER | Unbatched, base meshes | 0.911 | 0.164 |
| RTX 4070 Ti SUPER | Instanced, base meshes | 0.426 | 0.162 |
| RTX 4070 Ti SUPER | Instanced + LODs | 0.444 | 0.147 |
| Intel UHD 770 | Unbatched, base meshes | 4.622 | 3.565 |
| Intel UHD 770 | Instanced, base meshes | 4.481 | 3.436 |
| Intel UHD 770 | Instanced + LODs | 4.148 | 3.123 |

Instancing reduced camera draws from 691 to 3 and shadow draws from 1,001 to 3; sampled images
matched exactly. With LODs, camera draws were 4 and submitted triangles fell from 855,988 to
606,537. These short runs are development evidence, not the SRS reference workload, a general
FPS promise or minimum-hardware acceptance. Small timing differences may be noise.

```powershell
.\out\build-next\Release\VelosRuntime.exe --stress=1000 --frames=180 --no-debug-gpu --no-instancing --no-lods --report=out/reference.json
.\out\build-next\Release\VelosRuntime.exe --stress=1000 --frames=180 --no-debug-gpu --no-lods --report=out/instanced.json
.\out\build-next\Release\VelosRuntime.exe --stress=1000 --frames=180 --no-debug-gpu --report=out/lods.json
```

Use `--adapter=intel` for the iGPU path. Reports include median/P95/P99, draw/triangle counts and
active shadow policy. `--ray-shadows` requests DXR; `--ray-budget-mb=0` exercises budget fallback.

## Limits and next work

This preview is for trusted local projects, not production distribution or hostile-asset ingestion.
No complete fuzzing, long soak, coverage-percentage or baseline UHD 620/GTX 1050 acceptance run has
been completed. Intel UHD 770, RTX 4070 Ti SUPER and WARP have passed the small reference-scene
checks; those results do not establish performance on all low-end hardware.

- Physics supports root-level cube/sphere/plane colliders; spheres need uniform positive scale,
  planes are static, and dynamic physics owns rotation. Imported mesh colliders are not implemented.
- GLB import is geometry-only, up to 64 MB, 1 million vertices and 3 million indices. Embedded textures,
  multi-material preservation, skeletal animation, Draco and external buffers are not supported.
  Image maps are assigned separately. The image cooker supports PNG/JPEG/BMP/TIFF/TGA/DDS within
  bounded 2D limits; HDR environment maps, BC6H/BC7, alpha-coverage mips and virtual texturing remain ahead.
- The 2D sample proves orthographic/unlit geometry, not a complete sprite/tilemap/Box2D/UI pipeline.
- C# scripting/hot reload, audio, prefabs, animation, full game UI and source reflection remain
  ahead. Native gameplay graphs are implemented, but not Unreal-compatible Blueprints, a general
  dataflow/function system, collision events or arbitrary script execution.
- Rendering uses explicit passes, CPU frustum culling and GPU graphics shaders. A full render graph,
  compute/indirect or occlusion culling, bindless paths, IBL/GI, reflection probes, SSAO/SSR,
  FXAA/TAA, bloom and predictive VRAM streaming remain ahead. Transparency blends after per-object
  tone mapping into an 8-bit target, not a physically correct floating-point HDR compositor.
- Ray tracing currently means directional hard shadows only, not GI, reflections, area-light
  soft shadows or path tracing. Point/spot shadows, cascades and shadow atlases remain ahead.
  Masked casters trigger raster fallback; transparent and unlit objects do not cast shadows.
- Meshes and textures each have separate 256 MB GPU admission caps; DXR structures have a separate
  configurable 0-256 MB cap. These are not a unified VRAM budget. Textures and mesh LODs are fully
  resident, and descriptors/meshes have no eviction during a session. Disk tiers have individual
  limits (512 MB geometry, 512 MB textures, 128 MB shaders, 64 MB AI). Unified predictive budgets
  and driver-specific PSO caching are not done.
- Shader reload is manual and synchronous for this preview. Initial project asset loading and GPU
  mesh/texture uploads can block; complete async streaming and device-removal recovery are still planned.
- Animation, audio, navigation, networking, terrain/foliage, world partitioning, full VFX and
  additional platforms remain unimplemented; this graphics checkpoint does not imply Unity/Unreal parity.
- AI model revisions behind mutable tags are not automatically discovered; disable response caching
  after changing model contents. There are no semantic-cache actions, generated-code execution or NPC AI.
- Export is an unsigned folder build, not a sealed archive or installer. A clean-machine deployment
  test, SDK redistribution/license review and project-license choice are required before public release.

The detailed future work stays in [docs/MILESTONES.md](docs/MILESTONES.md). No full roadmap milestone
is marked complete solely because this preview exists.

## Git workflow

The local Git repository contains the planning history. Use small, validated Conventional Commits
and annotate completed milestone gates, with partial tags for early slices. Do not tag M0 complete
until its code/tests exist. Author identity and any remote/publication must be user-approved;
credentials, caches and generated build output stay out of source control.

## Licence

TBD (see open decision **OD-12**).
