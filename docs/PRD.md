# Product Requirements Document - Velos Engine

| Field | Value |
|---|---|
| Document | PRD |
| Version | 0.2 (draft) |
| Status | Product scope draft; native preview implements a limited subset |
| Owner | Project lead |
| Last updated | 2026-09-07 |

---

## 1. Problem statement

The requested product is a small Windows engine for authoring and shipping modest 3D and 2D games,
with explicit control over GPU work, resource budgets, caching and optional AI assistance.
Existing engines and frameworks can serve parts of this need; this project prioritizes ownership
and a focused workflow, not a claim that alternatives cannot run on low-end hardware.

Building an engine costs substantially more than integrating an existing one. We will reuse proven
libraries for physics, math, ECS, import and audio while owning the runtime, renderer, asset
pipeline and editor integration. The initial planning milestone delivered these documents; a
native implementation preview now exists, with its tested scope and limits in [../README.md](../README.md).

## 2. Product vision

> A lean Windows-native engine for creating, playing and packaging small games, using GPU
> acceleration where it improves measured performance and local or cloud AI when requested.

## 3. Target users

| Persona | Description | Primary need |
|---|---|---|
| **Solo/indie developer** | Ships small 3D or 2D games; hardware is a laptop | Runs well on their own weak GPU; fast iteration; doesn't need 90% of Unity |
| **Engine learner / student** | Wants to understand rendering, ECS, GPU pipelines | A readable, well-documented codebase with real architecture |
| **Technical artist** | Authors shaders, materials, lighting | Hot-reloading shaders, live material editing, a visible render graph |
| **Low-spec-market developer** | Targets regions where integrated GPUs dominate | Scalability tiers that actually work, deterministic frame budget |

**Non-target (v1.0):** AAA studios, console/mobile shipping, MMO-scale networking, film-quality
offline rendering.

## 4. Goals

### 4.1 Product goals

| ID | Goal | Success measure |
|---|---|---|
| G1 | Author and play a 3D scene end-to-end in the editor | New user goes from empty project -> lit 3D scene with a moving scripted object in **< 15 minutes** |
| G2 | Run acceptably on low-end hardware | Provisional reference-scene targets: **720p / 30 FPS on an iGPU**, **1080p / 60 FPS on a 2 GB discrete GPU**; profiles and measurement rules are in SRS section 5 |
| G3 | Use the GPU effectively | Rendering runs on the GPU; accelerated culling, skinning and simulation kernels must beat the reference path after upload, synchronization and memory costs are included |
| G4 | Iteration speed | Provisional targets on a pinned small project: shader reload < 1 s, script reload < 3 s, editor cold start < 3 s; record cold and warm measurements separately |
| G5 | Local AI without a cloud dependency | Supported assistance works with an installed, capability-compatible Ollama model; unavailable or unsuitable models produce a clear fallback, never a blocked editor |
| G6 | Ship a game | A standalone packaged sample runs on a supported Windows x64 installation with dependencies bundled and no engine or development SDK installed |
| G7 | 2D is first-class | A 2D game can be built without touching the 3D pipeline, sharing the same editor and ECS |

### 4.2 Engineering goals

- Deterministic, budgeted memory - no unbounded allocation during a frame.
- Every subsystem measurable: built-in CPU/GPU profiler, not an afterthought.
- The engine core has **zero** dependency on the editor. Editor is a client of the engine.
- Backend-agnostic rendering behind a thin RHI so D3D12 today, Vulkan later, without rewrites.

### 4.3 Scope and release gates

All releases below are **proposed**, not promises or completed milestones. The 27 milestone IDs
are work packages, not a requirement to build every advanced feature before a usable release.
Their detailed dependencies and partial gates are in [MILESTONES.md](MILESTONES.md).

| Release | Required outcome | Work packages |
|---|---|---|
| Planning | Review PRD, SRS, architecture, roadmap and open decisions; initialize local Git | Current documentation milestone; not M0 implementation completion |
| Editor preview | Import a mesh, place objects, edit materials, save/reopen, undo and play/stop | M0-M8 |
| v0.1: first playable 3D game | Scripted movement, collisions, basic lighting/shadows/audio, bounded caches, AI chat with Ollama fallback, standalone runtime | Preview + baseline portions of M9/M11, M12-M14, M18.A and M23.A |
| v1.0: basic 3D/2D engine | 2D sample, game UI, basic animation, project/prefab workflow, diagnostics, reliable packaging and release tests | v0.1 + M15, baseline M16/M17, remaining core M18, M21-M23, M25-M26 |
| Advanced backlog | GPU-driven scene processing, advanced lighting/GI, predictive caching, AI tool edits/RAG/NPCs, material graph, Vulkan | M10, M19-M20, M24 and advanced portions of M9/M11/M16/M17/M18; individually approved |

Baseline lighting means PBR, IBL, a small light list and a budgeted directional shadow. Baseline
caching means correct keys, bounded storage, invalidation, safe eviction and instrumentation;
prediction and semantic similarity are not required for correctness. Basic animation means clip
playback and blending, not an animation-graph editor. Basic post means tonemapping, FXAA and
optional spatial resolution scaling, not mandatory temporal upscaling.

The `v0.1.0-preview.2` graphics checkpoint adds image-map materials, mip/compression cooking,
instancing, mesh LODs, point/spot lighting and opt-in DXR directional hard shadows to the earlier
authoring/physics/AI/export slice. Unsupported hardware retains raster shadows. It does not
complete the first-playable release gate: C# scripting and audio are still missing. IBL/GI,
full HDR post-processing, streaming, animation, game UI and the complete 2D toolchain also remain
outside this checkpoint. See [MILESTONES.md](MILESTONES.md) for partial evidence and remaining work.

The subsequent `v0.1.0-preview.3` control checkpoint adds official-SDK MCP scene/game authoring,
executable native behavior graphs, variables and a small complete puzzle sample. External editors
can operate all currently implemented authoring surfaces within explicit capability, revision,
path and permission boundaries. This does not imply audio/animation/terrain/networking support
or Unreal Blueprint compatibility. The API contract and tested workflow are in [MCP.md](MCP.md).

### 4.4 User scenarios

| ID | Priority | Given / When / Then |
|---|---|---|
| US-01 | P1 | Given a new project, when a developer imports a GLB, places objects and saves, reopening preserves the scene, stable IDs and settings |
| US-02 | P1 | Given a scene with scripts and physics, when Play is started and stopped, gameplay runs and the authored scene is restored |
| US-03 | P1 | Given a warm cache, when a dependent texture changes, only affected outputs rebuild; unrelated assets remain cache hits |
| US-04 | P1 | Given a configured cloud endpoint and local model, when the endpoint fails, chat falls back to Ollama without freezing the UI; with neither available, non-AI editing still works |
| US-05 | P1 | Given the sample game, when it is packaged, a user can play on the selected Windows test profile without installing the editor |
| US-06 | P2 | Given the 2D project template, when a developer adds sprites, tilemaps, collisions and UI, the same editor produces a standalone 2D game |

### 4.5 Assumptions and approval

The engine name, release hardware, available developer time and license remain open. Native
C++/Win32/D3D12/ImGui implementation defaults and measured development adapters are recorded in
[adr/README.md](adr/README.md). Short fixed-scene measurements now exist, but neither a schedule
nor the SRS minimum-hardware acceptance has been established. Freeze those choices before
claiming release gates or general low-end performance.

## 5. Non-goals (explicitly out of scope for v1.0)

| Non-goal | Rationale / revisit |
|---|---|
| Console (PS/Xbox/Switch) support | Requires NDA SDKs. Post-1.0. |
| Mobile (Android/iOS) | Post-1.0; the RHI + scalability design keeps the door open. |
| Linux / macOS | Windows-first by explicit product decision. Vulkan backend (M24) is the bridge. |
| Ray tracing / path tracing | Contradicts the low-end pillar. Optional high-tier feature post-1.0. |
| Nanite/Lumen-class virtualised geometry & GI | Way out of budget for baseline hardware. |
| Visual scripting (node graphs) for gameplay | C# is the proposed scripting path; a material graph is also deferred until separately approved. |
| Large-scale multiplayer / dedicated servers | Post-1.0; no networking work is hidden in hardening milestones. |
| Marketplace / asset store | Not a product concern. |
| Training our own AI models | We integrate models; we do not train them. |

## 6. Key product decisions (proposed - see ADR log)

| Area | Proposal | Why |
|---|---|---|
| Core language | **C++20** | Control over memory/layout, no GC pauses, direct GPU API access |
| Scripting language | **C# on a supported .NET LTS**, currently .NET 10, hosted via `hostfxr` | Productive gameplay authoring; state-preserving assembly reload requires explicit lifetime management |
| Graphics API | **D3D12** proposed, Vulkan 1.3 deferred | Explicit GPU control on Windows; verify actual driver, shader-model and descriptor capabilities, rather than assuming universal low-end support |
| Editor UI | **C++/Win32 + Dear ImGui docking** for the first editor | A real native Windows executable with a GPU viewport; C# WPF/WinUI remains an alternative when accessibility and desktop controls outweigh integration cost |
| Lighting | **Simple forward first; clustered forward+ as a measured extension** | Keep low-light-count scenes cheap; avoid a large G-buffer by default |
| Physics | **Jolt for 3D; Box2D proposed for 2D** | Reuse established solvers; physics and gameplay remain CPU-authoritative |
| ECS | **EnTT proposed; Flecs as an alternative** | Keep dense data and stable engine IDs, extract render data into independent GPU buffers; custom ECS only after a measured limitation |
| AI transport | **OpenAI-compatible Chat Completions + native Ollama** | Configurable endpoint/model/auth with capability checks; Azure or non-compatible APIs may require adapters |

> Open: Editor UI and graphics API are the two decisions most worth challenging before M2.
> See open decisions **OD-01** and **OD-03**.

## 7. Feature overview

### 7.1 Editor
Scene hierarchy - inspector with reflection-driven property editing - asset browser - viewport with
translate/rotate/scale gizmos - play/pause/step in editor - material editor (node graph) - render
graph visualiser - profiler panel - console - AI assistant panel - undo/redo - prefabs.

### 7.2 Rendering
Clustered forward+ - PBR metal/roughness - cascaded shadow maps + spot/point shadows - IBL from
HDR cubemaps - GPU frustum + occlusion culling with indirect draw - instancing - LODs - post stack
(tonemap, bloom, colour grading, FXAA/TAA) - temporal upscaling for low-end - 2D sprite/tilemap
renderer sharing the same RHI.

### 7.3 Compute / GPU-first
GPU work includes rasterization, lighting and post-processing; culling, skinning and particles
are added where their measured benefit exceeds dispatch, transfer and synchronization costs.
CPU simulation owns gameplay transforms, physics, input, scripts and editor changes. Render-only
data may be GPU-resident; ordinary gameplay must not require synchronous GPU readback each frame.
Disabled effects use simpler GPU paths or are omitted, not a complete CPU software renderer.

### 7.4 Smart caching (a headline feature, not plumbing)
Four cooperating tiers, each with explicit accounting, invalidation and eviction:
1. **Asset cache** - content-addressed (BLAKE3) cooked assets; identical inputs never re-import.
2. **Shader/PSO cache**: compiled shader artifacts may be shared; driver-specific PSO blobs are
   separately keyed to the adapter, driver and engine schema and rebuilt when incompatible.
3. **GPU residency cache** - texture/mesh streaming with a VRAM budget and LRU + usage-prediction
   eviction; on a 2 GB card the engine degrades gracefully instead of thrashing.
4. **AI cache**: exact request/context/model keys with TTL and a clear provenance label; semantic
   reuse is optional for read-only answers and never automatically replays tools or edits.

Only disk caches persist across restarts. GPU residency is reconstructed; pinned/in-flight
resources are released only after their fences complete. New work is deferred or downgraded if
it cannot fit, and disk-full/corruption paths preserve the original source assets.

### 7.5 AI layer
First release: configurable provider, streaming chat panel, explicit context preview, request
cancel, timeouts, exact-response cache and local fallback. Models and Ollama are user-installed
prerequisites, not bundled weights or a guaranteed service. No remote context upload occurs
without consent. A 7B model is not assumed to fit or respond quickly on a low-end GPU.

Low-end mode defaults to one request at a time and pauses local inference during play unless
coexistence has been tested. Later stages add approved, undoable scene tools, retrieval, generated
scripts and optional runtime dialogue. Authored gameplay remains playable without AI.

### 7.6 Gameplay
Data-oriented ECS  C# component scripts with hot reload  input mapping  physics (rigid bodies,
character controller, triggers, raycasts) - audio (3D spatial + mixer buses) - skeletal animation
with blend trees - prefabs - scene serialisation - timers/coroutines - save system.

### 7.7 2D
Sprites, sprite atlases, tilemaps, 2D physics (Box2D proposed), orthographic camera,
canvas UI, sprite animation. Shares ECS, assets, scripting, AI and editor with 3D.

## 8. Success criteria for v1.0

Once the scope and hardware profiles are approved, the basic engine is v1.0 when:

1. The sample 3D game (a small level: movement, physics, a scripted objective, audio, UI, save)
   is authored entirely in the editor and packaged as a standalone build.
2. The sample 2D game (a platformer) ships from the same editor.
3. The selected low-end profiles meet the approved frame-time and memory gates in SRS section 5;
   internal and output resolution are reported separately. High-end results are stretch targets.
4. Startup, imports and warm cache behavior meet the approved SRS workloads, not unspecified assets.
5. Core AI chat works offline with an installed supported model; editor and game workflows still
   function with no model or network. Deferred tools are not a release dependency.
6. Documented: architecture doc current, API reference generated, 6+ tutorials, 4+ samples.
7. Crash-free: 8-hour editor soak test and 4-hour runtime soak test with zero crashes and no
   unbounded memory growth.

## 9. Risks

| ID | Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| R1 | Engine scope exceeds available time | High | High | Ship the first playable gate; advance only when dependencies pass; advanced packages need separate approval |
| R2 | GPU-driven work costs more than it saves on some scenes or drivers | High | Medium | Compare CPU and GPU paths on actual target machines from M2; choose by measured workload, not vendor assumptions |
| R3 | Editor UI choice (ImGui) limits polish later | Medium | Medium | Keep editor logic in engine-side services; UI is a thin view layer, replaceable |
| R4 | C# hosting adds startup cost / GC hitches | Medium | Medium | Server GC off, workstation concurrent GC, pooled objects, no per-frame allocation in the binding layer; budget-tracked |
| R5 | AI features become a novelty that rots | Medium | High | AI ships behind tool-calling into real engine APIs and is covered by tests; every AI feature must save measurable time |
| R6 | Solo-developer bandwidth | High | High | Milestones sized to be independently shippable; each ends in a tagged commit and a demo |
| R7 | Cache correctness bugs (stale assets/shaders) are brutal to debug | High | Medium | Content addressing everywhere, cache versioning key, `--no-cache` and `--verify-cache` modes from day one |
| R8 | Vulkan backend divergence | Medium | Low | RHI validated by a conformance test suite both backends must pass |
| R9 | Local inference competes with rendering for RAM, bandwidth and GPU time | High | High | Small-model/CPU configurations, bounded context, default pause during play, no concurrent-performance promise without measurement |
| R10 | Compatibility and release targets are guessed before hardware is known | High | High | OD-07 is a gate; freeze OS, driver, CPU, RAM, shader capabilities and reference workloads before accepting performance targets |

## 10. Open questions for discussion

Tracked in [adr/README.md](adr/README.md). The critical ones before any code:

1. Editor UI: Dear ImGui vs a C# WinUI 3 / Avalonia shell hosting the engine swapchain?
2. Graphics API: D3D12-first, or D3D11 if the confirmed minimum hardware cannot meet the chosen shader/driver requirements?
3. Scripting: C# .NET hosting vs a lighter option (Lua/AngelScript) for v1?
4. Exact baseline hardware target - what machines do we actually test on?
5. Repository/licence model - open source from day one, or private until 0.5?
