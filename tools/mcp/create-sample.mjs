import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { parseArgs, promisify } from 'node:util';
import { execFile } from 'node:child_process';
import { access } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import assert from 'node:assert/strict';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const { values } = parseArgs({ options: { output: { type: 'string',default: 'samples/signal-room' }, build: { type: 'string',default: 'out/build-control/Release' }, adapter: { type: 'string',default: 'nvidia' } } });
const relative = values.output.replaceAll('\\','/');
if (path.isAbsolute(relative) || relative.split('/').some(part => !part || part.startsWith('.') || part.includes(':'))) throw new Error('Choose a new workspace-relative sample directory.');
const output = path.resolve(repo,relative);
try { await access(output); throw new Error('Sample destination already exists; choose a new directory.'); } catch (error) { if (error.code !== 'ENOENT') throw error; }
const binaries = path.resolve(repo,values.build);
await promisify(execFile)(path.join(binaries,'velos_mcp_assets.exe'),[path.join(output,'Sources')]);
const client = new Client({ name: 'velos-sample-author',version: '1.0' });
const transport = new StdioClientTransport({ command: process.execPath,args: [path.join(repo,'tools/mcp/server.mjs'),
  `--editor=${path.join(binaries,'VelosEditor.exe')}`,`--root=${repo}`,`--pipe=velos-sample-${process.pid}`,`--adapter=${values.adapter}`,'--debug-gpu'] });
async function call(name,args = {}) {
  const response = await client.callTool({ name: `velos_${name}`,arguments: args },undefined,{ timeout: 90000 });
  if (response.isError) throw new Error(`${name}: ${JSON.stringify(response.content)}`);
  return response.structuredContent;
}
async function revision() { return (await call('system_status')).revision; }
async function edit(name,args) { return call(name,{ ...args,expected_revision: await revision() }); }
async function waitJob(id) {
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline) {
    const result = await call('jobs_get',{ job: id });
    if (result.state === 'succeeded') return result.result;
    if (result.state === 'failed' || result.state === 'cancelled') throw new Error(JSON.stringify(result));
  }
  throw new Error('Sample import/export exceeded its deadline.');
}

function switchGraph(key,toggles,ownVariable) {
  const nodes = [{ id: 1,kind: 'key_pressed',key,position: [24,24] }];
  const links = [];
  let tails = [{ id: 1,output: 'next' }];
  let next = 2;
  for (const [index,variable] of toggles.entries()) {
    const branch = next++;
    const on = next++;
    const off = next++;
    const horizontal = 320 + index * 640;
    nodes.push({ id: branch,kind: 'branch',variable,comparison: 'equal',value: 0,position: [horizontal,24] },
      { id: on,kind: 'set_variable',variable,value: 1,position: [horizontal + 320,24] },
      { id: off,kind: 'set_variable',variable,value: 0,position: [horizontal + 320,260] });
    for (const tail of tails) links.push({ from: tail.id,to: branch,output: tail.output });
    links.push({ from: branch,to: on,output: 'true' },{ from: branch,to: off,output: 'false' });
    tails = [{ id: on,output: 'next' },{ id: off,output: 'next' }];
  }
  const tick = next++;
  const condition = next++;
  const onColor = next++;
  const offColor = next++;
  nodes.push({ id: tick,kind: 'tick',position: [24,620] },{ id: condition,kind: 'branch',variable: ownVariable,comparison: 'equal',value: 1,position: [320,620] },
    { id: onColor,kind: 'set_color',vector: [0.22,0.83,0.61],position: [640,580] },{ id: offColor,kind: 'set_color',vector: [0.11,0.15,0.17],position: [640,830] });
  links.push({ from: tick,to: condition },{ from: condition,to: onColor,output: 'true' },{ from: condition,to: offColor,output: 'false' });
  return { nodes,links };
}

try {
  await client.connect(transport);
  await edit('scene_new',{ name: 'Signal Room' });
  await edit('project_save',{ path: `${relative}/scene.velos` });
  const objects = [
    { op: 'settings',fields: { ambient: 0.5,variables: { a: 0,b: 0,c: 0,solved: 0 } } },
    { op: 'create',primitive: 'plane',name: 'Stage',fields: { position: [0,-0.7,0],scale: [24,1,24],mesh: { color: [0.16,0.19,0.2,1],roughness: 0.95 } } },
    { op: 'create',primitive: 'cube',name: 'Control board',fields: { position: [0,2.2,-0.6],scale: [10.6,5.1,0.7],mesh: { color: [0.1,0.13,0.15,1],roughness: 0.7 } } },
    { op: 'create',name: 'Key light',fields: { light: { kind: 'directional',direction: [-0.2,-0.6,-1],intensity: 3 } } }
  ];
  const indices = {};
  function add(name,primitive,fields) { indices[name] = objects.length; objects.push({ op: 'create',name,primitive,fields }); }
  for (let index = 0; index < 3; ++index) {
    const horizontal = (index - 1) * 3;
    add(`Switch ${index + 1}`,'cube',{ position: [horizontal,1.4,0.2],scale: [2.3,2.1,0.65],mesh: { color: [0.11,0.15,0.17,1],roughness: 0.7 } });
    add(`Key ${index + 1}`,'quad',{ position: [horizontal,1.4,0.56],scale: [1.3,1.3,1],mesh: { color: [1,1,1,1],unlit: true,surface: 1,shadow: false } });
    add(`Target ${index + 1}`,'cube',{ position: [horizontal,3.25,0.15],scale: [0.75,0.28,0.45],mesh: { color: index === 1 ? [0.11,0.15,0.17,1] : [0.22,0.83,0.61,1],unlit: true,shadow: false } });
  }
  add('Signal Room title','quad',{ position: [0,5.2,0.2],scale: [6,1.2,1],mesh: { color: [1,1,1,1],unlit: true,surface: 1,shadow: false } });
  add('Target label','quad',{ position: [0,4.1,0.2],scale: [3,0.75,1],mesh: { color: [0.65,0.76,0.77,1],unlit: true,surface: 1,shadow: false } });
  add('Solved banner','quad',{ position: [0,-0.15,0.4],scale: [4,1,1],visible: false,mesh: { color: [0.22,0.94,0.68,1],unlit: true,surface: 1,shadow: false } });
  const created = await edit('scene_transaction',{ operations: objects,label: 'Author Signal Room' });
  const entity = name => created.results[indices[name]].entity;
  for (let index = 0; index < 3; ++index) {
    await edit('entity_reparent',{ entity: entity(`Key ${index + 1}`),parent: entity(`Switch ${index + 1}`),preserve_world: true });
    await waitJob((await edit('assets_import_texture',{ path: `${relative}/Sources/${['one','two','three'][index]}.png`,entity: entity(`Key ${index + 1}`),slot: 0 })).job);
    await edit('graph_set',{ entity: entity(`Switch ${index + 1}`),graph: switchGraph(String(index + 1),[['a','b'],['b','c'],['a','b','c']][index],['a','b','c'][index]) });
  }
  for (const [name,file] of [['Signal Room title','title'],['Target label','target'],['Solved banner','aligned']]) {
    await waitJob((await edit('assets_import_texture',{ path: `${relative}/Sources/${file}.png`,entity: entity(name),slot: 0 })).job);
  }
  await edit('graph_set',{ entity: entity('Solved banner'),graph: { nodes: [
    { id: 1,kind: 'tick',position: [24,24] },{ id: 2,kind: 'branch',variable: 'a',comparison: 'equal',value: 1,position: [300,24] },
    { id: 3,kind: 'branch',variable: 'b',comparison: 'equal',value: 0,position: [600,24] },{ id: 4,kind: 'branch',variable: 'c',comparison: 'equal',value: 1,position: [900,24] },
    { id: 5,kind: 'set_variable',variable: 'solved',value: 1,position: [1200,24] },{ id: 6,kind: 'set_visible',visible: true,position: [1500,24] },
    { id: 7,kind: 'set_variable',variable: 'solved',value: 0,position: [600,340] },{ id: 8,kind: 'set_visible',visible: false,position: [900,340] }
  ],links: [{ from: 1,to: 2 },{ from: 2,to: 3,output: 'true' },{ from: 2,to: 7,output: 'false' },{ from: 3,to: 4,output: 'true' },{ from: 3,to: 7,output: 'false' },
    { from: 4,to: 5,output: 'true' },{ from: 4,to: 7,output: 'false' },{ from: 5,to: 6 },{ from: 7,to: 8 }] } });
  const reset = await edit('entity_create',{ name: 'Reset logic' });
  await edit('graph_set',{ entity: reset.results[0].entity,graph: { nodes: [{ id: 1,kind: 'key_pressed',key: 'R',position: [24,24] },
    ...['a','b','c','solved'].map((variable,index) => ({ id: index + 2,kind: 'set_variable',variable,value: 0,position: [300 + index * 300,24] }))],
    links: [1,2,3,4].map(from => ({ from,to: from + 1 })) } });
  await edit('camera_set',{ target: [0,2.2,0],yaw_degrees: 0,pitch_degrees: 8,distance: 14,save_to_scene: true });
  await edit('project_save',{});
  await call('editor_focus',{ panel: 'Viewport' });
  await call('viewport_capture',{ path: `${relative}/preview-editor.png`,expected_revision: await revision() });
  await call('simulation_step',{ steps: 1 });
  for (const key of ['1','2']) {
    await call('simulation_input',{ keys: [key] });
    await call('simulation_step',{ steps: 1 });
    await call('simulation_input',{ keys: [] });
    await call('simulation_step',{ steps: 1 });
  }
  const variables = (await call('variables_get')).values;
  assert.deepEqual(variables,{ a: 1,b: 0,c: 1,solved: 1 });
  await call('viewport_capture',{ path: `${relative}/preview-solved.png`,expected_revision: await revision() });
  await call('simulation_input',{ keys: ['R'] });
  await call('simulation_step',{ steps: 2 });
  assert.equal((await call('variables_get')).values.solved,0);
  await call('simulation_stop');
  await call('editor_graph',{ view: 'logic',entity: entity('Switch 1') });
  await call('viewport_capture',{ path: `${relative}/preview-graph.png`,expected_revision: await revision() });
  await call('editor_focus',{ panel: 'Viewport' });
  const exported = await waitJob((await edit('build_export',{ directory: `out/signal-room-${process.pid}` })).job);
  assert.equal((await call('editor_logs')).gpu_validation,'');
  await promisify(execFile)(path.join(repo,exported.runtime),['--frames=12',`--adapter=${values.adapter}`,'--debug-gpu',`--capture=${path.join(output,'preview-runtime.png')}`],{ cwd: repo,maxBuffer: 1024 * 1024 });
  console.log(`PASS: MCP authored Signal Room, tested solve/reset, preserved authored state and exported ${exported.runtime}`);
  console.log(`Sample: ${relative}/scene.velos`);
} finally {
  try { await call('simulation_stop'); await edit('editor_close',{ discard_changes: true }); } catch {}
  await client.close();
}