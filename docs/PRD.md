# Product Requirements Document — Velos Engine

| Field | Value |
|---|---|
| Document | PRD |
| Version | 0.1 (draft) |
| Status | For review |
| Owner | Project lead |
| Last updated | 2026-09-06 |

---

## 1. Problem statement

Existing engines force a bad trade:

- **Unity / Unreal** — enormous, opaque, heavy editors, long iteration times, and their default
  rendering paths assume mid-to-high-end GPUs. Getting good performance on integrated graphics or
  a 1050-class card means fighting the engine.
- **Frameworks (raylib, MonoGame, bgfx, three.js)** — fast and small, but they are *libraries*, not
  engines. No editor, no asset pipeline, no scene authoring, no gameplay framework.
- **AI in engines today** — bolted-on chat panels that generate code snippets. Nothing that
  understands the project's asset graph, scene state, or shader code, and nothing that works
  offline.

There is no engine that is (a) small enough to fully understand, (b) explicitly engineered for
low-end GPUs, (c) GPU-compute-first, and (d) has AI as an architectural layer rather than a plugin.

## 2. Product vision

> A lean Windows-native 3D/2D game engine where the GPU does the maths, the caches do the waiting,
> and an AI assistant that runs locally or in the cloud is part of the toolchain — not an add-on.

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
| G1 | Author and play a 3D scene end-to-end in the editor | New user goes from empty project → lit 3D scene with a moving scripted object in **< 15 minutes** |
| G2 | Run acceptably on baseline hardware | Reference scene ≥ **60 FPS @ 1080p** on Intel UHD 620 / GTX 1050 at *Low* tier |
| G3 | GPU-first compute | ≥ 80% of per-frame per-object work (transforms, culling, skinning, particles, lighting) executes on GPU |
| G4 | Iteration speed | Shader edit → visible on screen in **< 1 s**; C# script edit → live in **< 3 s**; editor cold start **< 3 s** |
| G5 | AI that works offline | Every AI feature functions with **only** a local Ollama model; cloud endpoints are an upgrade, never a requirement |
| G6 | Ship a game | A standalone packaged build of the sample game runs on a clean Windows 10 machine with no engine install |
| G7 | 2D is first-class | A 2D game can be built without touching the 3D pipeline, sharing the same editor and ECS |

### 4.2 Engineering goals

- Deterministic, budgeted memory — no unbounded allocation during a frame.
- Every subsystem measurable: built-in CPU/GPU profiler, not an afterthought.
- The engine core has **zero** dependency on the editor. Editor is a client of the engine.
- Backend-agnostic rendering behind a thin RHI so D3D12 today, Vulkan later, without rewrites.

## 5. Non-goals (explicitly out of scope for v1.0)

| Non-goal | Rationale / revisit |
|---|---|
| Console (PS/Xbox/Switch) support | Requires NDA SDKs. Post-1.0. |
| Mobile (Android/iOS) | Post-1.0; the RHI + scalability design keeps the door open. |
| Linux / macOS | Windows-first by explicit product decision. Vulkan backend (M24) is the bridge. |
| Ray tracing / path tracing | Contradicts the low-end pillar. Optional high-tier feature post-1.0. |
| Nanite/Lumen-class virtualised geometry & GI | Way out of budget for baseline hardware. |
| Visual scripting (node graphs) for gameplay | C# covers it. Node graphs *are* planned for materials only. |
| Large-scale multiplayer / dedicated servers | Post-1.0. Basic transform replication may land in M25. |
| Marketplace / asset store | Not a product concern. |
| Training our own AI models | We integrate models; we do not train them. |

## 6. Key product decisions (proposed — see ADR log)

| Area | Proposal | Why |
|---|---|---|
| Core language | **C++20** | Control over memory/layout, no GC pauses, direct GPU API access |
| Scripting language | **C# on .NET 8** (hosted via `hostfxr`) | Fast to write gameplay, huge ecosystem, hot reload, familiar to Unity refugees |
| Graphics API | **D3D12** primary, Vulkan 1.3 later | Best driver quality on Windows incl. weak Intel/AMD iGPUs; explicit control for GPU-driven work |
| Editor UI | **Dear ImGui (docking)** in-engine, C# tool panels via the script host | Zero-latency, renders through our own RHI, no WinUI/WPF interop tax; proven for engine tooling |
| Lighting | **Clustered forward+** | MSAA-friendly, low bandwidth (critical on iGPU), no fat G-buffer |
| Physics | **Jolt Physics** | Fast, deterministic, MIT, multicore, actively maintained |
| ECS | Custom **archetype-based** ECS | Cache coherence is the whole point; off-the-shelf ECS won't integrate with our GPU-driven buffers |
| AI transport | OpenAI-compatible HTTP + **Ollama fallback** | One protocol covers OpenAI, Azure OpenAI, Groq, OpenRouter, LM Studio, llama.cpp server |

> ⚠️ Editor UI and graphics API are the two decisions most worth challenging before M2.
> See open decisions **OD-01** and **OD-03**.

## 7. Feature overview

### 7.1 Editor
Scene hierarchy · inspector with reflection-driven property editing · asset browser · viewport with
translate/rotate/scale gizmos · play/pause/step in editor · material editor (node graph) · render
graph visualiser · profiler panel · console · AI assistant panel · undo/redo · prefabs.

### 7.2 Rendering
Clustered forward+ · PBR metal/roughness · cascaded shadow maps + spot/point shadows · IBL from
HDR cubemaps · GPU frustum + occlusion culling with indirect draw · instancing · LODs · post stack
(tonemap, bloom, colour grading, FXAA/TAA) · temporal upscaling for low-end · 2D sprite/tilemap
renderer sharing the same RHI.

### 7.3 Compute / GPU-first
Transform hierarchy evaluation, culling, skinning, particle simulation, light clustering,
post-processing and (optionally) navmesh/pathfinding grids run as compute passes. A CPU reference
path exists for every GPU pass, used for validation and as a fallback on broken drivers.

### 7.4 Smart caching (a headline feature, not plumbing)
Four cooperating tiers, each with an explicit byte budget, persistence, and eviction policy:
1. **Asset cache** — content-addressed (BLAKE3) cooked assets; identical inputs never re-import.
2. **Shader/PSO cache** — compiled DXIL + serialised pipeline state, keyed by permutation hash;
   warmed at load, persisted to disk, shared across projects.
3. **GPU residency cache** — texture/mesh streaming with a VRAM budget and LRU + usage-prediction
   eviction; on a 2 GB card the engine degrades gracefully instead of thrashing.
4. **AI cache** — prompt+context hash → response, with semantic near-duplicate lookup and TTL, so
   repeat questions are free and offline-capable.

### 7.5 AI layer
Provider abstraction (OpenAI-compatible / Ollama / custom endpoint) · automatic fallback chain ·
streaming responses · tool-calling into engine APIs (query scene, create entity, write script,
tag assets) · editor copilot · runtime AI services for NPC dialogue and behaviour with a hard
per-frame budget · full offline operation.

### 7.6 Gameplay
Archetype ECS · C# component scripts with hot reload · input mapping · physics (rigid bodies,
character controller, triggers, raycasts) · audio (3D spatial + mixer buses) · skeletal animation
with blend trees · prefabs · scene serialisation · timers/coroutines · save system.

### 7.7 2D
Sprites, sprite atlases, tilemaps, 2D physics (Jolt in 2D constraint mode), orthographic camera,
canvas UI, sprite animation. Shares ECS, assets, scripting, AI and editor with 3D.

## 8. Success criteria for v1.0

The engine is v1.0 when **all** of the following hold:

1. The sample 3D game (a small third-person level: movement, physics, enemies, audio, UI, save)
   is authored entirely in the editor and packaged as a standalone build.
2. The sample 2D game (a platformer) ships from the same editor.
3. Reference 3D scene: ≥ 60 FPS @ 1080p Low tier on baseline hardware; ≥ 144 FPS @ 1440p High tier
   on an RTX 3060 class card.
4. Cold editor start < 3 s; project with 5 000 assets opens < 10 s on second launch (cache warm).
5. Every AI feature demonstrably works with the network cable unplugged.
6. Documented: architecture doc current, API reference generated, 6+ tutorials, 4+ samples.
7. Crash-free: 8-hour editor soak test and 4-hour runtime soak test with zero crashes and no
   unbounded memory growth.

## 9. Risks

| ID | Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| R1 | Scope explosion — an engine is 10 subsystems, each a product | Fatal | High | Strict milestone gates; nothing starts until the previous exit criteria are met; non-goals enforced |
| R2 | D3D12 GPU-driven rendering underperforms on Intel iGPU (indirect draw is weak there) | High | Medium | Feature tiers: GPU culling on High/Medium, CPU culling path on Low. Measure on real baseline HW from M2. |
| R3 | Editor UI choice (ImGui) limits polish later | Medium | Medium | Keep editor logic in engine-side services; UI is a thin view layer, replaceable |
| R4 | C# hosting adds startup cost / GC hitches | Medium | Medium | Server GC off, workstation concurrent GC, pooled objects, no per-frame allocation in the binding layer; budget-tracked |
| R5 | AI features become a novelty that rots | Medium | High | AI ships behind tool-calling into real engine APIs and is covered by tests; every AI feature must save measurable time |
| R6 | Solo-developer bandwidth | High | High | Milestones sized to be independently shippable; each ends in a tagged commit and a demo |
| R7 | Cache correctness bugs (stale assets/shaders) are brutal to debug | High | Medium | Content addressing everywhere, cache versioning key, `--no-cache` and `--verify-cache` modes from day one |
| R8 | Vulkan backend divergence | Medium | Low | RHI validated by a conformance test suite both backends must pass |

## 10. Open questions for discussion

Tracked as OD-01 … OD-14 in [adr/README.md](adr/README.md). The critical ones before any code:

1. Editor UI: Dear ImGui vs a C# WinUI 3 / Avalonia shell hosting the engine swapchain?
2. Graphics API: D3D12-first, or start on D3D11 (simpler, superb iGPU drivers) and add D3D12?
3. Scripting: C# .NET hosting vs a lighter option (Lua/AngelScript) for v1?
4. Exact baseline hardware target — what machines do we actually test on?
5. Repository/licence model — open source from day one, or private until 0.5?
