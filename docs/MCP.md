# Velos MCP Control

Status: native control preview, 2026-09-07. The adapter exposes **60 tools, nine resources and
one authoring prompt** using the official MCP SDK. It controls the real Windows editor and
its engine services; this is not a mock scene generator or a command-line text-only scene editor.

## Setup

### VS Code Plugin

[Velos MCP Tools](../tools/vscode-extension/README.md) is an installable VS Code extension that
registers this server through the stable MCP provider API. It bundles the official adapter and
its runtime dependencies, uses VS Code's Node runtime, and supplies native start/attach/status
commands and per-workspace read-only settings. It requires local Windows x64 and VS Code 1.136+.

```powershell
.\tools\package-vscode.ps1 -Test
code --install-extension out/vsix/velos-mcp-tools-0.1.0-win32-x64.vsix
```

Build the engine first. **Velos: Configure Connection** selects an editor executable; the engine
checkout's control-preview build is detected automatically. **Velos: Start or Connect Editor**
starts or reuses the configured pipe. Enable the plugin's Velos entry through **MCP: List Servers**
and approve its tools in Copilot. Discovery never launches the editor. Untrusted, virtual and
remote workspaces do not get a server definition.

The legacy workspace configuration below is an alternative. The plugin preserves it; avoid
enabling both registrations for the same game connection. This is a local VSIX, not a Marketplace
release. Plugin installation does not include the native engine binaries.

### Standalone Adapter

Requirements: Windows x64, a supported D3D12/SM6 GPU, the native build prerequisites in
[../README.md](../README.md), and Node.js 22 or later. The adapter dependencies are pinned in
[../tools/mcp/package.json](../tools/mcp/package.json) and its lockfile.

```powershell
.\tools\build.ps1 -Configuration Release -BuildDirectory out/build-control -Test
npm ci --prefix tools/mcp --ignore-scripts
```

VS Code's [../.vscode/mcp.json](../.vscode/mcp.json) registers `velos-engine`. Use **MCP: List
Servers** to start it after building, then approve the tools your workflow needs. This launches
an isolated editor on first use, rooted at this workspace, with pipe name `velos-vscode`.
The user must trust the project and the MCP client before allowing writes. Source files,
API keys or a whole home directory should not be used as an automation workspace by default.

For another editor that supports local MCP stdio, use this configuration with actual absolute
paths on your machine. Clients vary between `servers` and `mcpServers` as their top-level key.

```json
{
  "mcpServers": {
    "velos-engine": {
      "command": "node",
      "args": [
        "D:/Projects/Velos/tools/mcp/server.mjs",
        "--editor=D:/Projects/Velos/out/build-control/Release/VelosEditor.exe",
        "--root=D:/Projects/MyGame",
        "--pipe=velos-my-game"
      ]
    }
  }
}
```

The workspace root must already exist. It bounds imports, scenes, captures and export outputs.
`--adapter=auto|nvidia|intel|amd|warp`, `--width=1600`, `--height=1000` and `--debug-gpu` are
optional adapter launch arguments. `--read-only` restricts both the adapter and a newly launched
native editor. No model download, Ollama installation or cloud credential is required for MCP.

### Attach and reconnect

To share an editor between multiple MCP clients, launch it explicitly and omit `--editor` from
each adapter configuration:

```powershell
.\out\build-control\Release\VelosEditor.exe --control-pipe=velos-shared --automation-root="D:/Projects/MyGame"
node tools/mcp/server.mjs --pipe=velos-shared --root="D:/Projects/MyGame"
```

The adapter speaks protocol messages on stdout; diagnostics go to stderr. It never forwards
native editor stdout into the MCP stream. A disconnected adapter leaves its editor open to
preserve work. Reconnect with `--pipe` and no `--editor`, or close the old editor before launching
another with the same name. The pipe name is exclusive: a second server cannot take it over.

This can be debugged with VS Code's Node debugger or any SDK-compatible MCP client. Use the
`build_game_scene` prompt as a starting workflow, or call tools directly.

## Safe Authoring Workflow

1. Read `velos_system_capabilities`, `velos_system_status` and `velos_scene_get`.
2. Preserve unsaved work. Open/new/replace/close require explicit discard when the scene is dirty.
3. Submit a small revision-checked transaction, optionally using `dry_run:true` first.
4. Wait for import/export jobs to succeed. Read errors instead of assuming queued means complete.
5. Test through Play/Pause/Step and virtual input; Stop restores the authored scene and variables.
6. Capture the rendered revision, inspect the PNG, save, export into an empty directory and verify.

All tool names below have the `velos_` prefix. The tables show the remaining suffix. The exact
strict JSON schemas are discoverable with MCP `tools/list` and maintained in
[../tools/mcp/catalog.mjs](../tools/mcp/catalog.mjs).

### Inspection

| Tool suffix | Result |
|---|---|
| `system_status` | Scene/revision, selection, dirty state, history, simulation, rendered frame, graph visibility and control counters |
| `system_capabilities` | Implemented features, classes, graph nodes, coordinate conventions, budgets and explicit exclusions |
| `classes_list` | Eight native component classes and their storage/default fields |
| `scene_get` | Paged entities with every supported component, scene settings, variables and optional game camera |
| `entity_get` | One entity plus its row-major world matrix |
| `assets_list` | Built-in meshes and referenced project assets |
| `project_list` | Paged supported files/directories within the automation root |
| `camera_get` | Current editor camera |
| `renderer_get` | GPU/memory/draw counters and current graphics settings |
| `editor_logs` | Bounded editor messages and D3D12 validation errors |

### Scene Authoring

| Tool suffix | Action |
|---|---|
| `scene_transaction` | Atomic batch of 1-256 operations, one undo entry, optional dry run and label |
| `entity_create` | Empty entity or built-in/imported mesh with optional component fields |
| `entity_patch` | Patch identity/transform/material/light/body/gameplay/behavior fields |
| `entity_transform` | Local/world absolute or relative position, rotation and scale |
| `entity_reparent` | Parent/unparent with optional world-transform preservation; rejects cycles/shear |
| `entity_duplicate` | Copy one entity and all its supported components, not its descendants |
| `entity_delete` | Delete an entity and descendants, undoably |
| `component_set` | Add/patch `mesh`, `light`, `body`, `spin`, `keyboardDrive` or `behavior` |
| `component_remove` | Remove an optional component; identity/transform cannot be removed |
| `scene_settings` | Name, mode, ambient/shadow policy, variables and optional saved view |
| `scene_generate` | Editable grid/ring generation with shared initial fields |
| `scene_new` | Empty/workshop template, retaining the current save destination |
| `scene_replace` | Complete native schema-1 scene replacement with normal validation and undo |
| `history_undo` | Undo one authored transaction |
| `history_redo` | Redo one authored transaction |
| `variables_get` | Current numeric gameplay variables |
| `variables_set` | Add/change variables or remove unused variables with null |

Identity and transform fields are `name`, `visible`, `position`, quaternion `rotation` and `scale`.
Parenting has its own command. Materials expose `asset`, RGBA `color`, `roughness`, `metallic`,
`shadow`, `unlit`, four `textures`, `uvScale`, `uvOffset`, `emission`, `emissionStrength`,
`normalStrength`, `alphaCutoff`, numeric `surface` (0 opaque, 1 masked, 2 transparent), and
`doubleSided`. Light, body and gameplay fields are listed with defaults by `classes_list`.

IDs and revisions are returned as decimal strings. Safe numeric IDs are accepted, but use strings
to avoid JavaScript integer precision loss. `expected_revision` is required on authored mutations.
Two clients editing the same revision cannot both succeed. Failed batches leave scene, revision
and history unchanged. A batch that exceeds the undo budget is rejected before mutation.
Dry runs validate scene data and history cost, but do not upload or import assets.

Example `velos_scene_transaction` arguments, replacing `123` with the current revision:

```json
{
  "expected_revision": "123",
  "label": "Create a moving platform",
  "operations": [
    { "op": "settings", "fields": { "variables": { "speed": 1.5 } } },
    {
      "op": "create", "as": "platform", "name": "Moving platform", "primitive": "cube",
      "fields": { "position": [0, 1, 0], "scale": [3, 0.4, 2], "mesh": { "roughness": 0.7 } }
    },
    {
      "op": "patch", "entity": "@platform",
      "fields": {
        "behavior": {
          "nodes": [
            { "id": 1, "kind": "tick", "position": [24, 24] },
            { "id": 2, "kind": "translate", "vector": [1, 0, 0], "variable": "speed", "position": [320, 24] }
          ],
          "links": [{ "from": 1, "to": 2 }]
        }
      }
    }
  ]
}
```

This example moves a visual platform; graph transform actions cannot override a physics body.
For dynamic-body motion use a `velocity` graph node or `simulation_velocity`.

### Assets and Builds

| Tool suffix | Action |
|---|---|
| `project_open` | Open a workspace-relative `.velos` scene and validate/load dependencies |
| `project_save` | Atomic save or Save As with dependency copying; explicit overwrite for another existing file |
| `assets_import_model` | Background static GLB import; optionally create the entity |
| `assets_import_texture` | Background image cooking/assignment, slots 0 albedo / 1 normal / 2 ORM / 3 emission |
| `jobs_get` | Status/result/error for one of the last 32 background jobs |
| `jobs_cancel` | Cancel import application before completion; ongoing decode may finish off-thread |
| `build_export` | Saved scene -> empty output folder, runtime, shaders, notices, assets and manifest |
| `build_verify` | Verify exported package whitelist and hashes |

Imports use source-content hashes, immutable project asset paths and versioned cooking. PNG,
JPEG, BMP, TIFF, TGA and DDS image maps are supported; GLB remains static geometry-only. Non-power-
of-two images receive valid mip chains; odd top-level dimensions use uncompressed storage when
BC resource dimensions would be invalid. Files must already be inside the automation root.

Imports return immediately with a job ID. Completion verifies that the target project/revision
still matches. One background control job is active at a time. Import cancellation discards the
pending scene application; it does not forcibly interrupt a codec or undo already completed work.
Exports already writing an output folder cannot be cancelled. Failed exports can leave a partial
folder; the engine never deletes or overwrites it automatically.

`build_export` packages an existing native runtime. It is not arbitrary C++ compilation, an
installer builder, a project-wide shell endpoint or a code-generation execution service.

### Editor and Simulation

| Tool suffix | Action |
|---|---|
| `editor_select` | Select/clear an entity and optionally frame it |
| `editor_focus` | Activate a known native panel/docked tab |
| `editor_graph` | Open Scene/Classes/Logic graph view; optional owner and explicit layout arrangement |
| `camera_set` | Transient viewport camera; `save_to_scene:true` plus revision stores it for exports |
| `simulation_state` | Play/Pause state, fixed timestep and physics body count |
| `simulation_play` | Start/resume real physics and behavior execution |
| `simulation_pause` | Pause without restoring authored data |
| `simulation_stop` | Restore authored scene/variables and clear virtual input |
| `simulation_step` | Advance 1-120 fixed ticks, entering paused Play if needed |
| `simulation_velocity` | Set dynamic-body X/Z velocity during simulation |
| `simulation_input` | Set held A-Z/0-9/Space/Enter/arrow keys for engine gameplay, not global OS input |
| `renderer_set` | Exposure, resolution scale, shadow size, DXR budget, instancing, LODs, wireframe, grid, VSync |
| `renderer_reload_shaders` | Reload fixed engine shader entries; preserve last-good pipelines on failure |
| `viewport_capture` | Actual GPU-readback editor PNG, with rendered revision/frame and optional inline MCP image |
| `editor_close` | Close after save or explicit discard, using the current revision |

The viewport uses a right-handed camera and XYZ scene units. Stored quaternions use XYZW.
`entity_transform` Euler inputs are degrees in `[pitch, yaw, roll]` order; local/world space is
explicit. Geometry scale cannot be singular. Game camera settings persist separately from
ordinary orbit/pan/zoom, so viewing a scene does not dirty it.

Captures report the revision actually rendered. `expected_revision` rejects a stale scene
capture; camera/tab-only changes require a subsequent rendered frame, observable through
`system_status.rendered_frames`. Inline PNG responses require the adapter's `--root` and an
image smaller than 8 MB. The native capture tool writes only `.png` files within the root.

### Gameplay Graphs

| Tool suffix | Action |
|---|---|
| `graph_catalog` | Discover node types, outputs and execution budgets |
| `graph_get` | Read an entity's behavior graph and node layout |
| `graph_set` | Replace its complete validated behavior graph |
| `graph_add_node` | Add a node with a unique bounded integer ID |
| `graph_update_node` | Patch node fields or layout without changing its ID |
| `graph_remove_node` | Remove a node and attached links |
| `graph_connect` | Connect a typed execution output; reject cycles and duplicate outputs |
| `graph_disconnect` | Remove one execution output link |
| `graph_state` | Last-executed simulation tick per owner/node |
| `graph_relationships` | Paged scene entity/component data with parent edges |

The **Graphs** docked tab has Scene, Classes and Logic modes. Scene links reparent actual entities;
node selection selects their real inspector. Classes shows Entity/component composition and
native field types. Logic edits executable behaviors and persists node positions in the scene.
Use the node menu and add/delete icons, drag between execution pins, and edit fields within nodes.
Middle-drag or Alt-drag pans; the minimap navigates larger graphs. The Variables popup edits
numeric scene variables. Standard undo/redo applies to graph edits. Executed nodes highlight in Play.

| Node kind | Behavior |
|---|---|
| `begin_play` | Runs once when entering Play |
| `tick` | Runs once per fixed simulation step |
| `key_pressed` | Rising-edge event, not repeated while the key stays held |
| `translate` | Owner-local position += vector * scalar * timestep |
| `rotate` | Owner-local rotation changes by vector degrees/second * scalar |
| `velocity` | Set dynamic owner X/Z velocity from vector * scalar, bounded to +/-100 |
| `set_variable` | Assign a numeric variable from value |
| `add_variable` | Add value to a numeric variable per execution |
| `branch` | Compare a variable to value; execute true or false output |
| `set_visible` | Change the owning entity's visibility |
| `set_color` | Set the owning material RGB while retaining alpha |

For movement/rotation/velocity, scalar is the named scene variable or the constant `value` when
no variable is selected. Ordinary execution output is `next`; branch outputs are `true` and
`false`. Each output has one link. Multiple inputs into an action are allowed. Graphs execute
in scene/entity order; connections and branches determine action order within each event.

Limits: 128 nodes/256 links per graph, 2,048 nodes per scene, 8,192 executed nodes per fixed step,
64 finite numeric scene variables. Graphs are acyclic and cannot call arbitrary scripts/shells.
Transform actions require no physics body; velocity requires a dynamic body. Missing variables,
missing material targets, invalid ports, cycles and unsupported types fail validation. Exported
games execute the same `BehaviorRuntime`, not an editor-only approximation.

This is **not Unreal Blueprint compatibility**. There is no C++/C# class authoring/reflection,
general dataflow type system, function/subgraph library, latent coroutine system, breakpoints,
skeletal animation graph, material/shader graph, networking, audio or collision-event graph yet.
These limits are discoverable through `system_capabilities` instead of placeholder tools.

## Resources and Prompt

Read-only JSON resources: `velos://editor/status`, `velos://scene/current`, `velos://classes`,
`velos://renderer`, `velos://assets`, `velos://graphs/scene`, `velos://graphs/nodes`,
`velos://variables`, and `velos://capabilities`. Scene resources return the first 100 entities;
use paginated tools for larger scenes. Resource subscriptions are not implemented.

Prompt: `build_game_scene`, with required `brief`. It requests capability-aware authoring,
revision checks, import completion checks, simulation validation, screenshot inspection and
verified packaging. The MCP client supplies its own model; the native chat assistant remains
separate and does not automatically gain tool access.

## Security and Failure Semantics

- Automation is disabled unless explicitly enabled by native launch flags.
- The pipe DACL allows only the current Windows user and LocalSystem; remote pipe clients are
  rejected. There is no listening TCP/HTTP port. The Windows account is the local trust boundary.
- Native scene mutation occurs only when the main thread drains requests. UI gestures, active
  jobs and simulation ownership are respected. Read-only restrictions exist in both adapter and native code.
- Paths are workspace-relative, validated against traversal, alternate streams, device names,
  hidden/ambiguous components and reparse points. Arbitrary file reads, shell commands, credential
  APIs and AI provider secrets are not tools. Same-user hostile filesystem races are not a sandbox guarantee.
- Input is bounded to 16 MB and depth 32. There are four pipe instances, a 32-request/32 MB queue,
  and one main-thread request per frame. An I/O transfer has a ten-second deadline and queued
  engine execution waits at most thirty seconds before returning a timeout.
- Cancellation is best effort: once a native mutation begins, disconnecting the MCP call does
  not roll it back. After timeout/cancellation, read scene/job state before retrying. Revision
  conflicts prevent replaying the same stale scene mutation; filesystem operations have their own checks.
- This is a trusted-local-project preview, not a hostile-asset sandbox or production security
  certification. Imported GLB/image limits and general release caveats still apply.

## Verification and Sample

```powershell
.\tools\mcp-smoke.ps1 -BuildDirectory out/build-control -Adapter nvidia -Compact
.\tools\mcp-smoke.ps1 -BuildDirectory out/build-control -Adapter warp
node tools/mcp/create-sample.mjs --output=out/my-signal-room --adapter=nvidia
```

The smoke test uses an official SDK client, not hand-written protocol stubs. It creates a scene,
checks failed-batch rollback and concurrent revisions, imports real GLB/PNG files, edits and opens
graphs, executes input-driven gameplay, restores authored state, captures real GPU images,
saves a game camera, exports a package and runs that package. Separate tests check native and
MCP read-only enforcement, disallowed paths, IPC framing/limits/shutdown and all strict schemas.

[../samples/signal-room/scene.velos](../samples/signal-room/scene.velos) is a small complete puzzle
authored through MCP by [../tools/mcp/create-sample.mjs](../tools/mcp/create-sample.mjs). Its
solve/reset state was tested through virtual input, and the exported runtime uses the same graphs.
See [../samples/signal-room/README.md](../samples/signal-room/README.md) for controls and reproduction.