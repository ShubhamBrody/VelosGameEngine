# Architecture Decision Records

Decisions are recorded before implementation (`NFR-PROC-004`). Each accepted decision becomes
`ADR-NNNN-title.md`. Until then it sits in the **open decisions** table below as `OD-NN`.

Status on 2026-09-07: the native preview uses the implementation defaults below; remaining choices
and full milestone acceptance are still open. Existing FR/NFR and milestone IDs remain stable
across drafts; record the chosen option, test evidence and consequences before changing architecture.

## Initial implementation defaults

The 2026-09-06 request to build the engine starts a native preview using C++20, Win32, D3D12,
bounded descriptor tables and Dear ImGui docking (OD-01/02/03/05). MSVC 2026 and its bundled
CMake are the available local toolchain (OD-18). These are implementation defaults for the first
testable slice, not acceptance of every proposed feature or performance target. Scene/ECS and
asset storage remain independent of GPU resources; scripting and advanced rendering stay gated.
Local development adapters are Intel UHD 770 and RTX 4070 Ti SUPER; OD-07's low-end release
baseline is still unconfirmed. No license or remote-publication choice is made by starting code.

The preview also uses EnTT, Jolt, cgltf and nlohmann/json at pinned commits. Static MSVC CRT
linkage keeps the native executables independent of a separate VC runtime install; SDK DXC is
bundled for shader compilation. Windows SHA-256 provides the initial content cache instead of
BLAKE3, and conventional 0..1 depth is implemented instead of the proposed reversed-Z path.
These differences are explicit preview choices, not claims that the full target design is done.

The subsequent request to build the missing rendering features authorizes the graphics preview
increment. It adds pinned DirectXTex and meshoptimizer, separate 256 MB mesh/texture admission
limits, and an optional 0-256 MB DXR structure budget. BC1/3/5 and full mip chains are cooked on
the CPU; the renderer owns GPU uploads. Textures are authored separately from geometry-only GLB
import. Instancing and screen-size index LODs retain an unbatched/base-mesh reference path.

Ray-traced directional hard shadows require physical DXR 1.1 / SM 6.5 support and are off by
default. Raster shadows remain the baseline and the fallback for masked casters, unsupported
adapters, wireframe or DXR budget exhaustion. Raster/ray LODs must agree; TLAS validity includes
camera-driven LOD changes. This choice does not select a GI system, add ray-traced reflections,
or change the baseline SM 6.0 requirement. Unified residency and device recovery remain future work.

Evidence: eleven native test groups in Debug/Release, Release graphics and export checks on
NVIDIA/Intel/WARP, actual ray queries on NVIDIA and image-based LOD/fallback regressions. The
earlier live small-model Ollama request is separate AI evidence. No UHD 620/GTX 1050 benchmark,
production security audit or clean-machine distribution gate has been completed.

## Open decisions - the discussion agenda

Ordered by how soon they block work.

| ID | Decision | Blocks | Options | Leaning |
|---|---|---|---|---|
| **OD-07** | **Minimum hardware and OS.** Separate test machines, CPU/RAM channels, supported Windows version, drivers, internal/output resolution and AI coexistence | M0 planning / M2 acceptance | (a) SRS A/B candidate profiles (b) user-specified weaker/stronger profiles; Windows 10 support requires an explicit lifecycle decision | Need actual test hardware; current targets are unmeasured |
| **OD-03** | **Graphics API.** | M2 | (a) D3D12 first, optional Vulkan later (b) D3D11 baseline for older requirements (c) both from day one | (a), subject to SM6/driver checks. D3D11 supports compute/indirect draws but has a different resource/submission model; it is not inherently incapable of GPU-driven work |
| **OD-05** | **Binding model.** | M2 | (a) Bindless-first plus bound fallback (b) bounded tables first, indexed descriptors after capability/performance tests | (b); indirect rendering does not require universal bindless support |
| **OD-01** | **Editor UI toolkit.** | M7; discuss before M0 | (a) Win32 + Dear ImGui docking (b) C# WPF/WinUI shell with native viewport (c) Avalonia (d) Qt | (a) for an initial native Windows editor; compare (b) for accessibility, text editing and desktop controls |
| **OD-02** | **Editor language.** | M7 | (a) C++ editor (b) C++ engine, C# editor (c) C++ editor with later C# tool plugins | (a) initially; (c) only when an actual plugin need appears |
| **OD-04** | **Scripting language/runtime.** | M12 | (a) Supported C#/.NET LTS (currently .NET 10) (b) Lua (c) AngelScript (d) native C++ DLL iteration | (a), with explicit ABI/lifetime/reload costs; not an accepted dependency yet |
| **OD-08** | **2D architecture.** | M15 | (a) Reuse all 3D passes (b) lean 2D passes sharing RHI/assets/ECS | (b); pure 2D should not allocate 3D lighting or temporal resources |
| **OD-06** | **Optional static GI.** | Advanced M9 | (a) Baked lightmaps (b) baked irradiance probes (c) both (d) defer | (d) initially, IBL first. Compare UV/bake cost, dynamic-object sampling and leaks before choosing (a)/(b) |
| **OD-09** | **Mesh import.** | M4 | (a) glTF/GLB first, simple OBJ secondary (b) add FBX through ufbx or Autodesk SDK | (a) initially; verify each import library's license separately, rather than treating all FBX readers as proprietary |
| **OD-10** | **Physics library.** | M13 | (a) Jolt (b) PhysX 5 (c) Bullet (d) custom | (a) |
| **OD-11** | **Engine name.** `Velos` is a placeholder. | M1 | - | Your call |
| **OD-12** | **Licence & repo visibility.** | M0 | (a) MIT/Apache-2.0, public from day one (b) private until 0.5 (c) source-available | - |
| **OD-13** | **Local AI model and budget.** Model, quantization, context, license and actual tool capability | M18.A | (a) fitting small local model (b) CPU inference (c) capable remote endpoint with consent; all retain unavailable/offline behavior | Benchmark on selected hardware; no mandatory 7B model or background inference during play |
| **OD-14** | **First game and scale.** | M0 scope / M9 | (a) small single-level 3D game (b) 2D-first game (c) multi-level streaming (d) open world | (a), then 2D for basic v1.0; large-world work is not assumed |
| **OD-15** | **ECS and core libraries.** | M0 library selection / M5 | (a) EnTT + render extraction (b) Flecs archetypes (c) custom ECS | (a) proposed; custom storage only if profiling demonstrates a limitation |
| **OD-16** | **2D physics.** | M15 | (a) Box2D (b) constrained 3D Jolt | (a), with distinct 2D/3D components and no implicit cross-solver collisions |
| **OD-17** | **Team capacity and release scope.** | Before scheduling M0 | Solo/team, available hours, existing C++/graphics experience, which optional packages matter | Freeze the v0.1 sample first; estimate dates only after the initial technical spike |
| **OD-18** | **Toolchain and build support.** | M0 | CMake/MSVC primary, optional clang-cl; supported Windows SDK, dependency acquisition and CI runner | Pin tested versions; benchmark non-unity build and validate chosen .NET LTS/OS compatibility |

## Suggested discussion order

1. **OD-07** (hardware) - nothing else can be calibrated without it.
2. **OD-03 + OD-05** (graphics API + binding) - these define the RHI and are expensive to reverse.
3. **OD-01 + OD-02** (editor UI) - the "Windows application" question you raised; big UX and
   effort implications.
4. **OD-04** (scripting) - determines interop design and hot-reload strategy.
5. **OD-14 + OD-06** (scale + GI) - shape the renderer's long-term structure.
6. **OD-11 + OD-12** (name, license and visibility): decide repository metadata/distribution;
   do not add blanket copyright headers or publish code before agreement.
7. **OD-15 + OD-17 + OD-18** (library reuse, capacity and toolchain): select a small first build
   and realistic schedule. The full roadmap is not a commitment to build every advanced item.

## Evidence needed for early decisions

| Decision | Smallest useful experiment | Acceptance evidence |
|---|---|---|
| OD-07 / OD-03 / OD-05 | D3D12 capability probe, textured scene, CPU and GPU timings | Named physical machine/driver, supported shader/binding features, measured A/B budgets |
| OD-01 / OD-02 | Docked viewport, resize/DPI, text input, selection inspector | Stable native integration and a conscious accessibility/workflow tradeoff |
| OD-04 | Host a tiny script, call a batched native API, reload repeatedly | Startup/RSS, allocation/call cost, no stale callbacks/handles after reload |
| OD-13 | Stream/cancel a small prompt; disconnect providers while rendering | Correct fallback, bounded memory, no blocked UI and recorded model/resource limits |

Bring-up and basic UI/provider experiments have now run on the development machine. The C# host
experiment, final minimum-hardware choice and complete release acceptance remain open. Final
lighting/GI choices should follow larger representative scene measurements.

## Template

```markdown
# ADR-NNNN - <Title>

- **Status:** Proposed | Accepted | Superseded by ADR-XXXX
- **Date:** YYYY-MM-DD
- **Deciders:** ...
- **Related requirements:** FR-..., NFR-...

## Context
What forces are at play? What constraints (hardware, schedule, skill, licence)?

## Options considered
| Option | Pros | Cons |
|---|---|---|

## Decision
What we chose and why.

## Consequences
Positive, negative, and what this makes harder later. What would make us revisit.
```
