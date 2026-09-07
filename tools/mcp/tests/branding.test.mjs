import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

test('stdio initialization advertises the canonical Velos logo without starting an editor', { timeout: 15000 }, async () => {
  const client = new Client({ name: 'velos-branding-test', version: '1.0' });
  try {
    await client.connect(new StdioClientTransport({ command: process.execPath, args: [fileURLToPath(new URL('../server.mjs', import.meta.url))] }));
    const server = client.getServerVersion();
    assert.equal(server.name, 'velos-editor');
    assert.equal(server.title, 'Velos Engine');
    assert.equal(server.version, '0.1.1');
    assert.equal(server.icons?.length, 1);
    const icon = server.icons[0];
    const prefix = 'data:image/png;base64,';
    assert.ok(icon.src.startsWith(prefix));
    assert.equal(icon.mimeType, 'image/png');
    assert.deepEqual(icon.sizes, ['256x256']);
    assert.deepEqual(Buffer.from(icon.src.slice(prefix.length), 'base64'), await readFile(new URL('../../../assets/branding/velos-icon.png', import.meta.url)));
    assert.equal((await client.listTools()).tools.length, 60);
  } finally { await client.close(); }
});