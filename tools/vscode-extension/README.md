# Velos MCP Tools

A Windows desktop VS Code extension that exposes the native Velos game editor to Copilot and
other VS Code MCP clients. It registers a stable MCP server provider and bundles the official
MCP adapter: **60 tools, nine resources and one game-authoring prompt**.

## Requirements

- Windows x64 and VS Code 1.136 or later.
- A built native Velos editor with MCP control support, and a supported D3D12/SM6 GPU.
- A trusted local game workspace. Remote/WSL/SSH, virtual and untrusted workspaces are not supported.
- Copilot or another VS Code MCP consumer for model-driven tool use; model/account access is separate.

The VSIX bundles the JavaScript adapter and SDK dependencies. It uses VS Code's Node runtime by
default, so installing the plugin does not require a separate Node/npm installation. Building
the extension from source requires Node 22+ and npm. The native engine binaries are not inside the VSIX.

## Install and Connect

Install the local VSIX with **Extensions: Install from VSIX**, or:

```powershell
code --install-extension out/vsix/velos-mcp-tools-0.1.0-win32-x64.vsix
```

1. Open the game workspace and trust it only if its contents are trusted.
2. Run **Velos: Configure Connection** to select `VelosEditor.exe`. In the engine checkout the
	default `out/build-control/Release/VelosEditor.exe` is detected automatically.
3. Run **Velos: Start or Connect Editor**, or start its **Velos - workspace** entry in **MCP: List Servers**.
4. Select the Velos tools in Copilot Chat's tools picker and approve the operations you intend to allow.

Discovery does not launch processes. When VS Code resolves a server that needs a new native
editor, the plugin asks before launching it. The explicit Start or Connect Editor command
starts it directly. Editor work is preserved when the MCP connection or VS Code closes.

For an existing editor, run **Velos: Attach to Running Editor** and enter its `velos-` control
pipe name. Its native automation root must match the selected game workspace, particularly for
inline screenshots. Attaching never widens the native editor's permissions or changes its root.

The old workspace `mcp.json` configuration remains an alternative. Use one Velos registration
at a time to avoid duplicate tool listings. This extension does not overwrite or remove existing
MCP configurations, user launch settings or scenes.

## Commands

| Command | Action |
|---|---|
| Velos: Start or Connect Editor | Start the native editor, or reuse its current pipe without resetting the scene |
| Velos: Attach to Running Editor | Select and validate an existing private control pipe |
| Velos: Configure Connection | Select the native editor executable for the game workspace |
| Velos: Show Connection Status | Read scene/revision, dirty state, entities and native control status |
| Velos: Toggle Read-only MCP Access | Change the adapter policy; restart its MCP server to apply |
| Velos: Manage MCP Servers | Open VS Code's normal MCP server controls |
| Velos: Show Output | Inspect bounded native connection diagnostics |

The status-bar Velos item opens the connection status. Scene authoring remains in the MCP tools:
generation, imports, positioning/rotation/scale, materials, lighting, variables, behavior graphs,
simulation/input, actual GPU screenshots and verified standalone exports.

## Settings

| Setting | Default | Purpose |
|---|---|---|
| `velosMcp.enabled` | true | Offer the provider for this trusted local workspace |
| `velosMcp.engineDirectory` | workspace | Separate engine checkout used to locate the executable |
| `velosMcp.editorExecutable` | `out/build-control/Release/VelosEditor.exe` | Absolute path or path relative to engine directory |
| `velosMcp.mode` | auto | Auto starts only when needed; attach never launches |
| `velosMcp.pipeName` | workspace hash | Stable root-specific name or an explicit existing pipe |
| `velosMcp.readOnly` | false | Restrict adapter tools and new native editors to reads |
| `velosMcp.adapter` | auto | auto/nvidia/intel/amd/warp preference for new editors |
| `velosMcp.nodeExecutable` | VS Code Node | Optional external Node 22+ executable |

Each local workspace folder can have its own connection. Configuration changes invalidate stale
server definitions. Restart the MCP server after permission changes. Read/write mode cannot
elevate a native editor that was started read-only; start a new appropriately configured editor.

## Safety and Limits

MCP operations still go through native scene validation, revision checks, undo transactions,
workspace path restrictions and a current-Windows-user-only named pipe. No arbitrary shell,
credential or general file-reading tool is added by this extension. Trusted workspace settings
can choose a native executable, so workspace trust is required before connection actions.

The plugin does not add engine capabilities such as audio, animation, terrain or networking.
Gameplay graphs are native Velos graphs, not Unreal Blueprint compatibility. It does not grant
blanket approval to Copilot tools, choose an AI model, or store API credentials.

This is an installable local development preview, not a Marketplace publication. Velos's project
license remains undecided; the manifest is marked UNLICENSED. Bundled third-party notices are
included in `dist/THIRD_PARTY_NOTICES.txt` without changing their licenses.

## Build and Test

From the Velos repository root:

```powershell
.\tools\package-vscode.ps1 -Test
```

The script installs pinned build dependencies, compiles/typechecks/lints, runs unit tests, builds
the VSIX, verifies its file allowlist and tests the extracted package in a real isolated VS Code
profile. The native editor must already be built. Set `VELOS_VSCODE_EXECUTABLE` if VS Code is
installed somewhere other than its usual per-user Windows location.

For development, open `tools/vscode-extension`, run `npm ci --ignore-scripts`, then `npm run
compile`. Press F5 to debug in an Extension Development Host. `npm test` exercises actual provider
activation, nonlaunching discovery, startup/reconnect, all 60 bundled tools, a real scene edit,
read-only denial and stale-configuration rejection. Temporary projects live under `out/validation-vscode`.

[Complete MCP API reference](https://github.com/ShubhamBrody/VelosGameEngine/blob/main/docs/MCP.md)
and [Signal Room example](https://github.com/ShubhamBrody/VelosGameEngine/tree/main/samples/signal-room).
