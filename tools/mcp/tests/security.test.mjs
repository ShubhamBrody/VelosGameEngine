import assert from 'node:assert/strict';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { spawn } from 'node:child_process';
import { mkdtemp, mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { request } from '../pipe.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const binary = path.resolve(process.argv[2] ?? path.join(repo,'out/build-control/Release/VelosEditor.exe'));
const base = path.join(repo,'out/validation-control');
await mkdir(base,{ recursive: true });
const root = await mkdtemp(path.join(base,'security-'));
const pipe = `velos-readonly-test-${process.pid}`;
const editor = spawn(binary,[`--control-pipe=${pipe}`,`--automation-root=${root}`,'--automation-read-only','--isolated','--no-debug-gpu'],{ cwd: root,windowsHide: true });
const exited = new Promise(resolve => editor.once('exit',resolve));
const client = new Client({ name: 'velos-readonly-test',version: '1.0' });
try {
  await new Promise((resolve,reject) => {
    let output = '';
    const timeout = setTimeout(() => reject(new Error('Read-only editor startup timed out.')),60000);
    editor.stdout.on('data',chunk => { output += chunk.toString(); if (output.includes(`VELOS_CONTROL_READY ${pipe}`)) { clearTimeout(timeout); resolve(); } });
    editor.on('error',error => { clearTimeout(timeout); reject(error); });
    editor.on('exit',code => { clearTimeout(timeout); reject(new Error(`Read-only editor exited ${code}: ${output}`)); });
  });
  const status = await request(pipe,'system.status');
  assert.equal(status.read_only,true);
  for (const [method,args] of [
    ['entity.create',{ expected_revision: status.revision,primitive: 'cube' }],
    ['simulation.play',{}],['camera.set',{ distance: 4 }],['project.save',{ expected_revision: status.revision,path: 'blocked.velos' }],
    ['viewport.capture',{ path: 'blocked.png' }],['editor.close',{ expected_revision: status.revision,discard_changes: true }]
  ]) {
    await assert.rejects(request(pipe,method,args),error => error.code === 'read_only',`Native read-only enforcement must block ${method}.`);
  }
  for (const directory of ['../','C:/Windows','.git','NUL','Assets/../../outside','Assets/private:stream']) {
    await assert.rejects(request(pipe,'project.list',{ directory }),undefined,`Invalid path must be rejected: ${directory}`);
  }
  assert.equal((await request(pipe,'system.status')).revision,status.revision);
  await client.connect(new StdioClientTransport({ command: process.execPath,args: [path.join(repo,'tools/mcp/server.mjs'),`--pipe=${pipe}`,'--read-only'] }));
  const listed = (await client.listTools()).tools;
  assert.ok(listed.length > 5 && listed.every(tool => tool.annotations.readOnlyHint));
  const attempt = await client.callTool({ name: 'velos_entity_create',arguments: { expected_revision: status.revision,primitive: 'cube' } });
  assert.equal(attempt.isError,true);
  assert.ok((await client.readResource({ uri: 'velos://capabilities' })).contents[0].text.includes('not_exposed'));
  const initialScene = await new Promise((resolve,reject) => {
    const outside = spawn(binary,[`--scene=${path.join(repo,'samples/workshop/scene.velos')}`,`--control-pipe=velos-boundary-${process.pid}`,
      `--automation-root=${root}`,'--frames=1','--isolated','--no-debug-gpu'],{ cwd: root,windowsHide: true });
    let output = '';
    outside.stdout.on('data',chunk => { output += chunk.toString(); });
    outside.stderr.on('data',chunk => { output += chunk.toString(); });
    outside.on('error',reject);
    outside.on('exit',code => resolve({ code,output }));
  });
  assert.notEqual(initialScene.code,0,'Automation must not start with an existing scene outside its root.');
  assert.ok(!initialScene.output.includes('VELOS_CONTROL_READY'),'An out-of-root scene must be rejected before publishing the control endpoint.');
  console.log('PASS: native and MCP read-only enforcement, workspace path restrictions and capability disclosure.');
} finally {
  await client.close();
  editor.kill();
  await exited;
}