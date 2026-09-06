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
| Phase | **Planning draft v0.2; M0 code work has not started** |
| Code | none yet (by design) |
| Next | Resolve the open decisions in [docs/adr/README.md](docs/adr/README.md), then start M0 scaffolding |

The roadmap has 27 work packages, not 27 mandatory blockers before a playable result. The first
release gate combines scene authoring, scripting, physics, basic lighting/audio, bounded caches,
early AI chat (M18.A) and a standalone sample (M23.A). Basic v1.0 adds 2D, UI and production
workflows. Advanced GPU-driven rendering, GI, AI scene tools, material graphs and Vulkan remain
separately gated. No performance measurements or runnable engine exist yet.

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

Not yet applicable. CMake/MSVC, D3D12, the UI toolkit and scripting host are proposals tracked in
[docs/adr/README.md](docs/adr/README.md). No ADR is accepted and no build toolchain is installed
or configured by this planning work.

## Git workflow

The local Git repository contains the planning history. Use small, validated Conventional Commits
and annotate completed milestone gates, with partial tags for early slices. Do not tag M0 complete
until its code/tests exist. Author identity and any remote/publication must be user-approved;
credentials, caches and generated build output stay out of source control.

## Licence

TBD (see open decision **OD-12**).
