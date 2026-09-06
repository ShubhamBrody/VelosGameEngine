# Velos Engine

> Codename `Velos` - a placeholder name. Rename freely before Milestone 1.

A proposal for a **GPU-accelerated, low-end-focused 3D/2D game engine** for Windows, using a
C++20 core, an optional C# scripting host, a native editor and AI assistance through configurable
OpenAI-compatible endpoints with a local **Ollama** fallback. These choices await discussion.

This is **not** a Unity/Unreal clone. The goal is a lean, understandable, genuinely fast engine
where you can author a 3D or 2D scene, script it, and ship a standalone build.

---

## Pillars

| # | Pillar | What it means in practice |
|---|--------|---------------------------|
| 1 | **Measured GPU acceleration** | GPU rendering with optional accelerated culling/skinning/particles. CPU-authoritative gameplay and physics; transfers, synchronization and memory are included in performance decisions. |
| 2 | **Low-end first** | Separate provisional iGPU and 2 GB discrete-GPU profiles. Actual minimum hardware and frame-time gates must be confirmed before implementation commitments. |
| 3 | **Smart caching** | Dependency-aware asset cache, shader/driver-specific PSO caches, fence-safe GPU residency and scoped AI response caching. All are bounded; only disk artifacts persist. |
| 4 | **Optional AI integration** | Verified Chat Completions endpoints and native Ollama, asynchronous streaming, clear capability/resource limits. Basic editor chat first; tools and runtime NPC AI later. |
| 5 | **Understandable** | No mega-abstractions. Explicit data flow, explicit memory, readable render graph. |

## Documents

Read them in this order:

1. [docs/PRD.md](docs/PRD.md) - *what* we are building and *why*, scope and non-goals.
2. [docs/SRS.md](docs/SRS.md) - numbered functional + non-functional requirements.
3. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - *how* it is built: layers, subsystems, data flow.
4. [docs/MILESTONES.md](docs/MILESTONES.md) - the phased delivery roadmap (M0 -> M26).
5. [docs/adr/README.md](docs/adr/README.md) - **open decisions we still need to discuss.**

## Status

| | |
|---|---|
| Phase | **Native preview in development; M0/M5 foundation is partially implemented** |
| Code | C++20 build, fixed-step clock, EnTT scenes, serialization, undo/redo, atomic files and bounded disk cache |
| Next | D3D12 viewport and native editor; full milestone acceptance and low-end budgets remain open |

The roadmap has 27 work packages, not 27 mandatory blockers before a playable result. The first
release gate combines scene authoring, scripting, physics, basic lighting/audio, bounded caches,
early AI chat (M18.A) and a standalone sample (M23.A). Basic v1.0 adds 2D, UI and production
workflows. Advanced GPU-driven rendering, GI, AI scene tools, material graphs and Vulkan remain
separately gated. Core tests run; the editor and hardware rendering benchmarks are the next slice.

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
.\tools\build.ps1 -Configuration Debug -Test
```

The script locates Visual Studio's bundled CMake without changing your global PATH. The initial
cache uses Windows SHA-256, integrity checks and byte-budgeted LRU; BLAKE3, streaming residency
and other advanced cache features remain future work. See [docs/adr/README.md](docs/adr/README.md)
for implementation defaults and decisions that are still open.

## Git workflow

The local Git repository contains the planning history. Use small, validated Conventional Commits
and annotate completed milestone gates, with partial tags for early slices. Do not tag M0 complete
until its code/tests exist. Author identity and any remote/publication must be user-approved;
credentials, caches and generated build output stay out of source control.

## Licence

TBD (see open decision **OD-12**).
