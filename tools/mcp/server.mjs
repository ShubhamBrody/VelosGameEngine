import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { CallToolRequestSchema, ListToolsRequestSchema, ListResourcesRequestSchema, ReadResourceRequestSchema,
  ListPromptsRequestSchema, GetPromptRequestSchema, ErrorCode, McpError } from '@modelcontextprotocol/sdk/types.js';
import { parseArgs } from 'node:util';
import { spawn } from 'node:child_process';
import { readFile, realpath, stat } from 'node:fs/promises';
import path from 'node:path';
import { tools, toolByName } from './catalog.mjs';
import { request } from './pipe.mjs';

const { values } = parseArgs({ options: {
  pipe: { type: 'string', default: 'velos-editor' }, editor: { type: 'string' }, root: { type: 'string' },
  adapter: { type: 'string', default: 'auto' }, 'read-only': { type: 'boolean', default: false },
  'debug-gpu': { type: 'boolean', default: false }, width: { type: 'string', default: '1600' }, height: { type: 'string', default: '1000' }
} });
if (process.platform !== 'win32') throw new Error('The current native Velos backend requires Windows.');
if (!/^velos-[a-zA-Z0-9_-]{1,90}$/.test(values.pipe)) throw new Error('Invalid control pipe name.');
if (!['auto','nvidia','intel','amd','warp'].includes(values.adapter)) throw new Error('Unsupported adapter preference.');
if (values.editor && !values.root) throw new Error('--root is required when launching an editor.');
const root = values.root ? await realpath(path.resolve(values.root)) : null;
let child;
let launch;

function ready() {
  if (!values.editor) return Promise.resolve();
  launch ??= new Promise((resolve, reject) => {
    const args = [`--control-pipe=${values.pipe}`, `--automation-root=${root}`, `--adapter=${values.adapter}`, '--isolated',
      `--width=${values.width}`, `--height=${values.height}`, values['debug-gpu'] ? '--debug-gpu' : '--no-debug-gpu'];
    if (values['read-only']) args.push('--automation-read-only');
    child = spawn(path.resolve(values.editor), args, { cwd: root, windowsHide: false, stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    const timeout = setTimeout(() => reject(new Error('Editor did not announce control readiness within 60 seconds.')), 60000);
    child.stdout.on('data', chunk => {
      output = (output + chunk.toString()).slice(-4096);
      if (output.includes(`VELOS_CONTROL_READY ${values.pipe}`)) { clearTimeout(timeout); resolve(); }
    });
    child.stderr.on('data', chunk => process.stderr.write(chunk));
    child.on('error', error => { clearTimeout(timeout); reject(error); });
    child.on('exit', code => { clearTimeout(timeout); if (!output.includes('VELOS_CONTROL_READY')) reject(new Error(`Editor exited before readiness (${code}).`)); });
  });
  return launch;
}

async function engine(method, args = {}, signal) {
  await ready();
  return request(values.pipe, method, args, { signal });
}

const server = new Server({ name: 'velos-editor', version: '0.1.0' }, {
  capabilities: { tools: {}, resources: {}, prompts: {} },
  instructions: 'Control the native Velos editor. Read status/scene before edits and pass expected_revision. Use atomic scene transactions, inspect job results, then capture the rendered revision. Files are confined to the configured workspace. Do not assume unsupported engines features exist. No shell, arbitrary code execution or credential access is provided.'
});
server.setRequestHandler(ListToolsRequestSchema, async () => ({ tools: tools.filter(tool => !values['read-only'] || tool.definition.annotations.readOnlyHint).map(tool => tool.definition) }));
server.setRequestHandler(CallToolRequestSchema, async (message, extra) => {
  const tool = toolByName.get(message.params.name);
  if (!tool) throw new McpError(ErrorCode.InvalidParams, `Unknown tool: ${message.params.name}`);
  if (values['read-only'] && !tool.definition.annotations.readOnlyHint) return { isError: true, content: [{ type: 'text', text: 'read_only: This MCP session is read-only.' }] };
  const parsed = tool.schema.safeParse(message.params.arguments ?? {});
  if (!parsed.success) return { isError: true, content: [{ type: 'text', text: `invalid_arguments: ${parsed.error.message}` }] };
  const { include_image: includeImage, ...args } = parsed.data;
  try {
    const result = await engine(tool.method, args, extra.signal);
    const content = [{ type: 'text', text: JSON.stringify(result) }];
    if (tool.method === 'viewport.capture' && includeImage) {
      if (!root) throw new Error('Configure --root to return captured image content. The PNG was saved by the editor.');
      const filename = await realpath(path.resolve(root, result.path));
      const relative = path.relative(root, filename);
      if (relative.startsWith('..') || path.isAbsolute(relative) || path.extname(filename).toLowerCase() !== '.png') throw new Error('Capture path is outside the configured workspace.');
      if ((await stat(filename)).size > 8 * 1024 * 1024) throw new Error('Captured PNG is too large for inline MCP content; use the file result.');
      content.push({ type: 'image', mimeType: 'image/png', data: (await readFile(filename)).toString('base64') });
    }
    return { content, structuredContent: result };
  } catch (error) {
    const data = { code: error.code ?? 'control_error', message: error.message };
    return { isError: true, content: [{ type: 'text', text: JSON.stringify(data) }], structuredContent: { error: data } };
  }
});

const resources = [
  { uri: 'velos://editor/status', name: 'Editor Status', method: 'system.status' },
  { uri: 'velos://scene/current', name: 'Scene Snapshot (First 100 Entities)', method: 'scene.get' },
  { uri: 'velos://classes', name: 'Native Component Classes', method: 'classes.list' },
  { uri: 'velos://renderer', name: 'Renderer State', method: 'renderer.get' },
  { uri: 'velos://assets', name: 'Referenced Assets', method: 'assets.list' },
  { uri: 'velos://graphs/scene', name: 'Scene Relationships', method: 'graph.relationships' },
  { uri: 'velos://graphs/nodes', name: 'Gameplay Node Catalog', method: 'graph.catalog' },
  { uri: 'velos://variables', name: 'Gameplay Variables', method: 'variables.get' },
  { uri: 'velos://capabilities', name: 'Engine Capabilities', method: 'system.capabilities' }
];
server.setRequestHandler(ListResourcesRequestSchema, async () => ({ resources: resources.map(({ method, ...resource }) => ({ ...resource, mimeType: 'application/json' })) }));
server.setRequestHandler(ReadResourceRequestSchema, async message => {
  const resource = resources.find(item => item.uri === message.params.uri);
  if (!resource) throw new McpError(ErrorCode.InvalidParams, 'Unknown Velos resource URI.');
  return { contents: [{ uri: resource.uri, mimeType: 'application/json', text: JSON.stringify(await engine(resource.method)) }] };
});
server.setRequestHandler(ListPromptsRequestSchema, async () => ({ prompts: [{
  name: 'build_game_scene', description: 'Plan and author a playable scene using current Velos capabilities, with checks and a verified export.',
  arguments: [{ name: 'brief', description: 'The game/scene to build', required: true }]
}] }));
server.setRequestHandler(GetPromptRequestSchema, async message => {
  if (message.params.name !== 'build_game_scene') throw new McpError(ErrorCode.InvalidParams, 'Unknown prompt.');
  const brief = message.params.arguments?.brief;
  if (!brief || brief.length > 8000) throw new McpError(ErrorCode.InvalidParams, 'Provide a brief containing 1-8000 characters.');
  return { messages: [{ role: 'user', content: { type: 'text', text: `Build this in Velos: ${brief}\nRead status and component classes. Preserve unsaved work. Use revision-checked atomic transactions to create geometry, materials, lighting, physics and gameplay. Import only approved workspace assets. Inspect job results; test Play/Step/Stop restoration. Capture and inspect the actual viewport. Save then export into an empty directory and verify the package. Report unsupported requirements rather than pretending they exist.` } }] };
});
server.onclose = () => {
  if (child && child.exitCode === null) {
    child.stdout.destroy();
    child.stderr.destroy();
    child.unref();
    process.stderr.write(`MCP disconnected; editor ${values.pipe} remains open to preserve work. Reconnect using --pipe.\n`);
  }
};
await server.connect(new StdioServerTransport());