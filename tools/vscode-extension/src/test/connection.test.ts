import { test } from 'node:test';
import * as assert from 'node:assert/strict';
import * as path from 'node:path';
import { adapterArguments, connectionFor, editorArguments } from '../connection';

test('connection definitions attach to a stable root-specific pipe without starting a process', () => {
	const root = path.resolve('game project');
	const extension = path.resolve('plugin');
	const connection = connectionFor(root, 'Game', extension);
	assert.match(connection.pipe, /^velos-code-[a-f0-9]{12}$/);
	assert.equal(connectionFor(root, 'Renamed workspace', extension).pipe, connection.pipe);
	assert.notEqual(connectionFor(path.join(root, 'other'), 'Other', extension).pipe, connection.pipe);
	assert.notEqual(connectionFor(path.join(root, 'other'), 'Game', extension).label, connection.label);
	assert.ok(adapterArguments(connection).includes(`--root=${root}`));
	assert.ok(!adapterArguments(connection).some(argument => argument.startsWith('--editor=')));
	assert.ok(connection.editor.endsWith(path.join('out', 'build-control', 'Release', 'VelosEditor.exe')));
});

test('custom engine paths, explicit attach and read-only policy remain separate', () => {
	const root = path.resolve('game');
	const engine = path.resolve('engine with spaces');
	const connection = connectionFor(root, 'Game', root, { engineDirectory: engine, pipeName: 'velos-existing', mode: 'attach', readOnly: true, adapter: 'warp' });
	assert.equal(connection.root, root);
	assert.ok(connection.editor.startsWith(engine));
	assert.equal(connection.pipe, 'velos-existing');
	assert.equal(connection.mode, 'attach');
	assert.ok(adapterArguments(connection).includes('--read-only'));
	assert.ok(editorArguments(connection).includes('--automation-read-only'));
	assert.ok(editorArguments(connection).includes('--adapter=warp'));
	assert.ok(!editorArguments(connection).some(argument => argument.includes('shell')));
});

test('invalid pipe names, modes and adapter preferences fail early', () => {
	const root = path.resolve('game');
	for (const pipeName of ['other-service', 'velos-../bad', 'velos-bad name', 'velos-' + 'x'.repeat(91)]) {
		assert.throws(() => connectionFor(root, 'Game', root, { pipeName }));
	}
	assert.throws(() => connectionFor(root, 'Game', root, { mode: 'shell' }));
	assert.throws(() => connectionFor(root, 'Game', root, { adapter: 'invalid' }));
	assert.throws(() => connectionFor('relative', 'Game', root));
});