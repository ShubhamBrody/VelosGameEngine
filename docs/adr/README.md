# Architecture Decision Records

Decisions are recorded before implementation (`NFR-PROC-004`). Each accepted decision becomes
`ADR-NNNN-title.md`. Until then it sits in the **open decisions** table below as `OD-NN`.

## Open decisions — the discussion agenda

Ordered by how soon they block work.

| ID | Decision | Blocks | Options | Leaning |
|---|---|---|---|---|
| **OD-07** | **Baseline hardware.** What machines do we physically test on? Everything in the SRS is calibrated to this. | M2 | (a) Intel UHD 620 iGPU + GTX 1050 2 GB (b) something weaker (c) something stronger | Need your actual hardware |
| **OD-03** | **Graphics API.** | M2 | (a) D3D12 first, Vulkan later (b) D3D11 first for fast bring-up, D3D12 after (c) both from day one | (a) — D3D11 can't do bindless or GPU-driven properly, and we'd rewrite |
| **OD-05** | **Binding model.** | M2 | (a) Bindless-first with a bound fallback (b) classic bound descriptors only for v1 | (a) — GPU-driven rendering requires it |
| **OD-01** | **Editor UI toolkit.** | M7 | (a) Dear ImGui in-engine (b) C# WinUI 3 shell hosting the engine swapchain (c) Avalonia (d) Qt | (a) for velocity; (b) is the "proper Windows app" you mentioned — worth debating |
| **OD-02** | **Editor language.** | M7 | (a) All C++ (b) C++ engine, C# editor (c) C++ editor core, C# tool plugins | (c) |
| **OD-04** | **Scripting language.** | M12 | (a) C# .NET 8 hosted (b) Lua/luajit (c) AngelScript (d) native C++ hot-reload DLL only | (a) |
| **OD-08** | **2D architecture.** | M15 | (a) 2D as a mode of the 3D pipeline (b) a separate lean 2D pipeline | (a) — one renderer, less code |
| **OD-06** | **Static GI.** | M19 | (a) Baked lightmaps (b) irradiance probe volumes (c) both (d) none for 1.0 | (b) — cheaper to author, handles dynamic objects |
| **OD-09** | **Mesh import.** | M4 | (a) glTF only (b) glTF + FBX via the Autodesk/ufbx path | (a) for v1, (b) later — FBX is a licensing and complexity tax |
| **OD-10** | **Physics library.** | M13 | (a) Jolt (b) PhysX 5 (c) Bullet (d) custom | (a) |
| **OD-11** | **Engine name.** `Velos` is a placeholder. | M1 | — | Your call |
| **OD-12** | **Licence & repo visibility.** | M0 | (a) MIT/Apache-2.0, public from day one (b) private until 0.5 (c) source-available | — |
| **OD-13** | **Default AI models.** Which Ollama model do we optimise prompts for? | M18 | e.g. `qwen2.5-coder:7b`, `llama3.1:8b`, `phi4` | Prompts must work on a 7B-class model |
| **OD-14** | **Target project scale.** How big a game must the engine handle? Affects streaming, scene partitioning, GPU buffer sizing. | M9 | (a) single-level indie (b) multi-level with streaming (c) open world | (b) |

## Suggested discussion order

1. **OD-07** (hardware) — nothing else can be calibrated without it.
2. **OD-03 + OD-05** (graphics API + binding) — these define the RHI and are expensive to reverse.
3. **OD-01 + OD-02** (editor UI) — the "Windows application" question you raised; big UX and
   effort implications.
4. **OD-04** (scripting) — determines interop design and hot-reload strategy.
5. **OD-14 + OD-06** (scale + GI) — shape the renderer's long-term structure.
6. **OD-11 + OD-12** (name + licence) — quick, but they touch every file header.

## Template

```markdown
# ADR-NNNN — <Title>

- **Status:** Proposed | Accepted | Superseded by ADR-XXXX
- **Date:** YYYY-MM-DD
- **Deciders:** …
- **Related requirements:** FR-…, NFR-…

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
