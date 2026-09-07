# Velos Development

- Velos is a native Windows C++20/D3D12 engine and editor. Preserve renderer-independent scene data and main-thread scene ownership.
- Use existing scene validation and undo transactions for new authoring operations. Call `Scene::touch()` after direct component mutation.
- The MCP adapter uses the official SDK, pinned under `tools/mcp`. Keep stdout exclusively for MCP protocol messages; diagnostics use stderr.
- SDK documentation: https://github.com/modelcontextprotocol/typescript-sdk/tree/v1.x and https://modelcontextprotocol.io/specification/latest.
- Editor automation is opt-in through a current-user-only local named pipe. Preserve workspace path restrictions, read-only enforcement, explicit destructive-operation flags and scene revision checks.
- All authored scene/component features should have corresponding MCP coverage or an explicit unsupported capability. Do not expose arbitrary shell commands, credential contents or paths outside the configured root.
- Behavior graphs are native, bounded, acyclic execution graphs, not Unreal Blueprint compatibility. Persist graph edits through the scene transaction path and execute them through `BehaviorRuntime`.
- Build with `tools/build.ps1 -Configuration Release -BuildDirectory out/build-control -Test`. Run `tools/mcp-smoke.ps1 -BuildDirectory out/build-control -Adapter nvidia -Compact` for MCP authoring, graph, import, simulation and export checks.
- Preserve user launch settings, autosaves and previous preview output. Git publication must use the verified ShubhamBrody account; do not rewrite history or change global identity settings without permission.