import assert from 'node:assert/strict';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { mkdtemp, mkdir, copyFile, writeFile, readFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const editor = path.resolve(process.argv[2] ?? path.join(repo,'out/build-control/Release/VelosEditor.exe'));
const adapter = process.argv[3] ?? 'nvidia';
const width = Number(process.argv[4] ?? 1600);
const height = Number(process.argv[5] ?? 1000);
const parent = path.join(repo,'out/validation-control');
await mkdir(parent, { recursive: true });
const root = await mkdtemp(path.join(parent,'mcp-'));
await copyFile(path.join(repo,'samples/graphics-lab/Assets/map-0.png'), path.join(root,'map.png'));
const geometry = Buffer.alloc(36);
[[-0.5,0,0],[0.5,0,0],[0,1,0]].flat().forEach((value,index) => geometry.writeFloatLE(value,index * 4));
const gltf = { asset: { version: '2.0' }, buffers: [{ byteLength: geometry.length }], bufferViews: [{ buffer: 0, byteLength: geometry.length }],
  accessors: [{ bufferView: 0, componentType: 5126, count: 3, type: 'VEC3', min: [-0.5,0,0], max: [0.5,1,0] }],
  meshes: [{ primitives: [{ attributes: { POSITION: 0 } }] }], nodes: [{ mesh: 0 }], scenes: [{ nodes: [0] }], scene: 0 };
let document = Buffer.from(JSON.stringify(gltf));
document = Buffer.concat([document, Buffer.alloc((4 - document.length % 4) % 4, 32)]);
const binary = Buffer.alloc(12 + 8 + document.length + 8 + geometry.length);
binary.writeUInt32LE(0x46546c67,0); binary.writeUInt32LE(2,4); binary.writeUInt32LE(binary.length,8);
binary.writeUInt32LE(document.length,12); binary.writeUInt32LE(0x4e4f534a,16); document.copy(binary,20);
binary.writeUInt32LE(geometry.length,20 + document.length); binary.writeUInt32LE(0x004e4942,24 + document.length); geometry.copy(binary,28 + document.length);
await writeFile(path.join(root,'model.glb'),binary);

const transport = new StdioClientTransport({ command: process.execPath, args: [path.join(repo,'tools/mcp/server.mjs'),
  `--editor=${editor}`, `--root=${root}`, `--pipe=velos-sdk-test-${process.pid}`, `--adapter=${adapter}`, `--width=${width}`, `--height=${height}`, '--debug-gpu'], stderr: 'pipe' });
const client = new Client({ name: 'velos-integration-test', version: '1.0' });
let diagnostics = '';
transport.stderr?.on('data', chunk => { diagnostics += chunk.toString(); });
async function call(name, args = {}, expectError = false) {
  const response = await client.callTool({ name: `velos_${name}`, arguments: args }, undefined, { timeout: 90000 });
  if (expectError) { assert.equal(response.isError, true, `Expected ${name} to fail`); return response; }
  assert.notEqual(response.isError, true, `${name}: ${JSON.stringify(response.content)}`);
  return response.structuredContent ?? JSON.parse(response.content[0].text);
}
async function revision() { return (await call('system_status')).revision; }
async function job(id) {
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline) {
    const result = await call('jobs_get', { job: id });
    if (result.state === 'succeeded') return result.result;
    if (result.state === 'failed' || result.state === 'cancelled') throw new Error(`Job ${id}: ${JSON.stringify(result)}`);
  }
  throw new Error('Import/export did not complete in the test deadline.');
}

try {
  await client.connect(transport);
  assert.ok((await client.listTools()).tools.length >= 40);
  assert.equal((await client.listPrompts()).prompts[0].name,'build_game_scene');
  assert.ok((await client.readResource({ uri: 'velos://classes' })).contents[0].text.includes('MeshRenderer'));
  await call('scene_new', { expected_revision: await revision(), name: 'MCP Integration' });
  const created = await call('scene_transaction', { expected_revision: await revision(), operations: [
    { op: 'create', primitive: 'plane', name: 'Floor', fields: { scale: [16,1,16], body: { motion: 'static' } } },
    { op: 'create', primitive: 'sphere', name: 'Player', as: 'player', fields: { position: [0,3,0], mesh: { color: [0.95,0.43,0.28,1] }, body: { motion: 'dynamic' }, keyboardDrive: 4 } },
    { op: 'create', primitive: 'cube', name: 'Mint block', fields: { position: [-2,1,0], mesh: { color: [0.23,0.73,0.58,1] } } },
    { op: 'create', name: 'Sun', fields: { light: { kind: 'directional' } } }
  ] });
  const player = created.results[1].entity;
  const peerTransport = new StdioClientTransport({ command: process.execPath, args: [path.join(repo,'tools/mcp/server.mjs'),`--pipe=velos-sdk-test-${process.pid}`,`--root=${root}`] });
  const peer = new Client({ name: 'velos-peer-test', version: '1.0' });
  try {
    await peer.connect(peerTransport);
    const expected = await revision();
    const edits = await Promise.all([client,peer].map((connection,index) => connection.callTool({ name: 'velos_entity_patch',
      arguments: { entity: player, expected_revision: expected, fields: { name: `Concurrent edit ${index}` } } })));
    assert.equal(edits.filter(result => result.isError !== true).length,1,'Only one client may commit against a shared scene revision.');
    assert.ok(edits.some(result => result.structuredContent?.error?.code === 'revision_conflict'));
    await call('history_undo', { expected_revision: await revision() });
  } finally { await peer.close(); }
  const stale = await revision();
  await call('entity_transform', { expected_revision: stale, entity: player, translate: [1,0,0], rotation_degrees: [0,30,0] });
  await call('entity_delete', { expected_revision: stale, entity: player }, true);
  await call('history_undo', { expected_revision: await revision() });
  assert.equal((await call('entity_get',{ entity: player })).entities[0].position[0],0);
  const before = (await call('scene_get')).total_entities;
  await call('scene_transaction', { expected_revision: await revision(), operations: [
    { op: 'create', primitive: 'cube' }, { op: 'patch', entity: player, fields: { scale: [0,1,1] } }
  ] }, true);
  assert.equal((await call('scene_get')).total_entities,before);
  await call('project_save', { expected_revision: await revision(), path: 'game/scene.velos' });
  await call('project_save', { expected_revision: await revision(), path: '../escape.velos' }, true);
  await call('project_list', { directory: '.git' }, true);
  const imported = await job((await call('assets_import_model', { expected_revision: await revision(), path: 'model.glb', name: 'Imported triangle' })).job);
  assert.equal(imported.vertices,3);
  const texture = await job((await call('assets_import_texture', { expected_revision: await revision(), path: 'map.png', entity: player, slot: 0 })).job);
  assert.ok(texture.mips > 1);
  await call('project_save', { expected_revision: await revision() });
  const block = created.results[2].entity;
  await call('variables_set', { expected_revision: await revision(), values: { speed: 2, score: 0 } });
  await call('graph_set', { expected_revision: await revision(), entity: block, graph: { nodes: [
    { id: 1, kind: 'tick', position: [0,0] }, { id: 2, kind: 'translate', vector: [0,0,-1], variable: 'speed', position: [280,0] },
    { id: 3, kind: 'key_pressed', key: 'Space', position: [0,240] }, { id: 4, kind: 'add_variable', variable: 'score', value: 1, position: [280,240] }
  ], links: [{ from: 1, to: 2 }, { from: 3, to: 4 }] } });
  await call('graph_connect', { expected_revision: await revision(), entity: block, from: 2, to: 2 }, true);
  await call('project_save', { expected_revision: await revision() });
  const graphRevision = await revision();
  const graphBefore = (await call('graph_get',{ entity: block })).graph;
  for (const view of ['scene','classes','logic']) {
    await call('editor_graph', { view, entity: block });
    const visible = (await call('system_status')).graph;
    assert.equal(visible.visible,true,`${view} graph must actually be visible`);
    assert.ok(visible.nodes > 0,`${view} graph must contain real nodes`);
    await call('viewport_capture', { path: `graph-${view}.png`, expected_revision: await revision() });
  }
  assert.equal(await revision(),graphRevision,'Opening graph views must not dirty the scene.');
  assert.deepEqual((await call('graph_get',{ entity: block })).graph,graphBefore,'Graph node positions must be preserved when opened.');
  await call('editor_focus', { panel: 'Viewport' });
  const authored = (await call('entity_get',{ entity: player })).entities[0];
  await call('simulation_step', { steps: 30 });
  assert.ok((await call('entity_get',{ entity: block })).entities[0].position[2] < -0.9, 'Graph movement must run through MCP-controlled simulation.');
  await call('simulation_input', { keys: ['Space'] });
  await call('simulation_step', { steps: 1 });
  assert.equal((await call('variables_get')).values.score,1);
  assert.ok((await call('graph_state')).executed.length >= 4);
  const falling = (await call('entity_get',{ entity: player })).entities[0];
  assert.ok(falling.position[1] < authored.position[1]);
  await call('simulation_stop');
  assert.equal((await call('variables_get')).values.score,0, 'Stop must restore authored variables.');
  assert.deepEqual((await call('entity_get',{ entity: player })).entities[0],authored);
  await call('camera_set', { target: [0,1,0], distance: 12, yaw_degrees: 22, save_to_scene: true, expected_revision: await revision() });
  await call('project_save', { expected_revision: await revision() });
  assert.ok(Math.abs((await call('scene_get')).view.yaw - 22 * Math.PI / 180) < 0.0001);
  const screenshot = await client.callTool({ name: 'velos_viewport_capture', arguments: { path: 'capture.png', expected_revision: await revision(), include_image: true } });
  assert.notEqual(screenshot.isError,true,JSON.stringify(screenshot.content));
  assert.ok(screenshot.content.some(content => content.type === 'image' && content.mimeType === 'image/png'));
  const captured = await readFile(path.join(root,'capture.png'));
  assert.ok(captured.readUInt32BE(16) >= 900 && captured.readUInt32BE(20) >= 600, 'Editor capture must use a real viewport, including when launched from a stdio client.');
  const built = await job((await call('build_export', { expected_revision: await revision(), directory: 'export' })).job);
  assert.equal(built.verified,true);
  await call('build_verify', { directory: 'export' });
  assert.equal((await call('editor_logs')).gpu_validation,'');
  await new Promise((resolve,reject) => {
    const runtime = spawn(path.join(root,'export/VelosRuntime.exe'), ['--frames=12',`--adapter=${adapter}`,'--debug-gpu',`--capture=${path.join(root,'runtime.png')}`], { cwd: root, windowsHide: true });
    let output = '';
    runtime.stdout.on('data',chunk => { output += chunk.toString(); });
    runtime.stderr.on('data',chunk => { output += chunk.toString(); });
    runtime.on('error',reject);
    runtime.on('exit',code => code === 0 ? resolve() : reject(new Error(`Export runtime exited ${code}: ${output}`)));
  });
  const runtimeImage = await readFile(path.join(root,'runtime.png'));
  assert.ok(runtimeImage.length > 1000 && runtimeImage.readUInt32BE(16) >= 900 && runtimeImage.readUInt32BE(20) >= 600, 'Hidden runtime launches must retain real render-target dimensions.');
  console.log(`PASS: real MCP SDK -> private native editor -> scene authoring/import/play/capture/export. Evidence: ${root}`);
} catch (error) {
  console.error(diagnostics);
  throw error;
} finally {
  try { await call('simulation_stop'); await call('editor_close', { expected_revision: await revision(), discard_changes: true }); } catch {}
  await client.close();
}