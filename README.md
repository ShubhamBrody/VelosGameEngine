# Velos Engine

> Codename `Velos` — a placeholder name. Rename freely before Milestone 1.

A **GPU-first, low-end-friendly 3D/2D game engine** for Windows, written in modern C++ with a
C# scripting layer, an integrated editor, and a first-class AI assistance layer
(OpenAI-compatible endpoints with a local **Ollama** fallback).

This is **not** a Unity/Unreal clone. The goal is a lean, understandable, genuinely fast engine
where you can author a 3D or 2D scene, script it, and ship a standalone build.

---

## Pillars

| # | Pillar | What it means in practice |
|---|--------|---------------------------|
| 1 | **GPU-first** | Transforms, culling, skinning, particles, lighting and post run in compute/graphics shaders. The CPU orchestrates; it does not crunch. |
| 2 | **Low-end first** | The engine is designed against a *baseline* GPU (Intel UHD 620 / GTX 1050 class). High-end is the scaled-up path, not the default. |
| 3 | **Smart caching** | Content-addressed asset cache, PSO/shader cache, GPU residency cache, and AI response cache — all budgeted, persistent and telemetry-driven. |
| 4 | **AI integrated** | Provider-agnostic AI layer: any OpenAI-compatible endpoint, with Ollama as the offline fallback. Used in the editor *and* available at runtime. |
| 5 | **Understandable** | No mega-abstractions. Explicit data flow, explicit memory, readable render graph. |

## Documents

Read them in this order:

1. [docs/PRD.md](docs/PRD.md) — *what* we are building and *why*, scope and non-goals.
2. [docs/SRS.md](docs/SRS.md) — numbered functional + non-functional requirements.
3. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — *how* it is built: layers, subsystems, data flow.
4. [docs/MILESTONES.md](docs/MILESTONES.md) — the phased delivery roadmap (M0 → M26).
5. [docs/adr/README.md](docs/adr/README.md) — **open decisions we still need to discuss.**

## Status

| | |
|---|---|
| Phase | **M0 — Planning** |
| Code | none yet (by design) |
| Next | Resolve the open decisions in [docs/adr/README.md](docs/adr/README.md), then start M0 scaffolding |

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
  /physics            Jolt integration
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

Not yet applicable. Toolchain is decided in [ADR-0001](docs/adr/README.md) and stood up in
Milestone M0.

## Licence

TBD (see open decision **OD-12**).
