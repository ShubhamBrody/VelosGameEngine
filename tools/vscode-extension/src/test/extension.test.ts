import * as assert from 'node:assert/strict';
import * as path from 'node:path';
import { pathToFileURL } from 'node:url';
import * as vscode from 'vscode';
import type { VelosProvider } from '../extension';

export async function run(): Promise<void> {
	const { Client } = await import('@modelcontextprotocol/sdk/client/index.js');
	const { StdioClientTransport } = await import('@modelcontextprotocol/sdk/client/stdio.js');
	const repo = process.env.VELOS_TEST_REPO;
	assert.ok(repo, 'The extension-host fixture must specify its engine repository.');
	const pipeModule = await import(pathToFileURL(path.join(repo, 'tools', 'mcp', 'pipe.mjs')).href);
	const nativeRequest = pipeModule.request as (pipe: string, method: string, args?: Record<string, unknown>, options?: { timeout: number }) => Promise<Record<string, unknown>>;
	const folder = vscode.workspace.workspaceFolders?.[0];
	assert.ok(folder && folder.uri.scheme === 'file', 'A local isolated game workspace is required.');
	assert.ok(vscode.workspace.isTrusted, 'The fixture must be trusted.');
	const extension = vscode.extensions.getExtension<{ provider: VelosProvider; version: string }>('ShubhamBrody.velos-mcp-tools');
	assert.ok(extension, 'VS Code must discover the development extension from its real manifest.');
	const api = await extension.activate();
	const config = vscode.workspace.getConfiguration('velosMcp', folder.uri);
	await config.update('engineDirectory', repo, vscode.ConfigurationTarget.WorkspaceFolder);
	await config.update('readOnly', false, vscode.ConfigurationTarget.WorkspaceFolder);
	const token = new vscode.CancellationTokenSource();
	const client = new Client({ name: 'velos-vscode-host-test', version: '1.0' });
	const readonlyClient = new Client({ name: 'velos-vscode-readonly-test', version: '1.0' });
	let pipe: string | undefined;
	try {
		const commands = await vscode.commands.getCommands(true);
		assert.ok(commands.includes('workbench.mcp.listServer'), 'The MCP server-management command must exist in the tested VS Code build.');
		for (const command of ['velosMcp.startEditor', 'velosMcp.attach', 'velosMcp.status', 'velosMcp.configure', 'velosMcp.readOnly', 'velosMcp.manageServers']) {
			assert.ok(commands.includes(command), `VS Code must register ${command}.`);
		}
		const definitions = await api.provider.provideMcpServerDefinitions(token.token);
		assert.equal(definitions.length, 1, 'One MCP definition must be contributed for the fixture.');
		const definition = definitions[0];
		assert.ok(definition instanceof vscode.McpStdioServerDefinition, 'Use the real stable VS Code MCP definition class.');
		assert.equal(definition.command, process.execPath, 'Use VS Code Node when no external executable is configured.');
		assert.equal(definition.env.ELECTRON_RUN_AS_NODE, '1');
		assert.equal(definition.cwd?.fsPath, folder.uri.fsPath);
		assert.ok(!definition.args.some(argument => argument.startsWith('--editor=')), 'Discovery must produce attach-only adapter arguments.');
		pipe = definition.args.find(argument => argument.startsWith('--pipe='))?.slice(7);
		assert.ok(pipe);
		await assert.rejects(nativeRequest(pipe, 'system.status', {}, { timeout: 1000 }), error => error instanceof Error && 'code' in error && error.code === 'ENOENT', 'Discovery must not launch the editor.');
		const cancelled = new vscode.CancellationTokenSource();
		cancelled.cancel();
		assert.equal(await api.provider.resolveMcpServerDefinition(definition, cancelled.token), undefined);
		cancelled.dispose();
		const status = await vscode.commands.executeCommand<{ engine: string }>('velosMcp.startEditor', folder.uri);
		assert.equal(status?.engine, 'Velos', 'The actual VS Code start command must establish a native editor connection.');
		const resolved = await api.provider.resolveMcpServerDefinition(definition, token.token);
		assert.ok(resolved);
		await client.connect(new StdioClientTransport({ command: resolved.command, args: resolved.args,
			env: Object.fromEntries(Object.entries(resolved.env).filter(([, value]) => value !== null).map(([key, value]) => [key, String(value)])) }));
		assert.equal((await client.listTools()).tools.length, 60, 'The bundled adapter must expose all real Velos tools.');
		assert.equal((await client.listResources()).resources.length, 9);
		const current = await nativeRequest(pipe, 'system.status');
		const created = await client.callTool({ name: 'velos_entity_create', arguments: {
			expected_revision: current.revision, primitive: 'cube', name: 'VS Code plugin probe', fields: { position: [1, 2, 3] }
		} });
		assert.notEqual(created.isError, true, JSON.stringify(created.content));
		const authored = await nativeRequest(pipe, 'system.status');
		const reconnected = await vscode.commands.executeCommand<Record<string, unknown>>('velosMcp.startEditor', folder.uri);
		assert.equal(reconnected?.revision, authored.revision, 'Repeated connection must reuse the running editor without resetting its scene.');
		await config.update('readOnly', true, vscode.ConfigurationTarget.WorkspaceFolder);
		await assert.rejects(api.provider.resolveMcpServerDefinition(definition, token.token), /changed/, 'An obsolete read/write definition must not start after policy changes.');
		const readonlyDefinition = (await api.provider.provideMcpServerDefinitions(token.token))[0];
		assert.ok(readonlyDefinition.args.includes('--read-only'));
		await api.provider.resolveMcpServerDefinition(readonlyDefinition, token.token);
		await readonlyClient.connect(new StdioClientTransport({ command: readonlyDefinition.command, args: readonlyDefinition.args, env: { ELECTRON_RUN_AS_NODE: '1' } }));
		const readonlyTools = (await readonlyClient.listTools()).tools;
		assert.ok(readonlyTools.length > 0 && readonlyTools.every(tool => tool.annotations?.readOnlyHint));
		const denied = await readonlyClient.callTool({ name: 'velos_entity_delete', arguments: { expected_revision: authored.revision, entity: '1' } });
		assert.equal(denied.isError, true, 'The bundled read-only server must reject explicit calls to hidden write tools.');
		assert.equal((await nativeRequest(pipe, 'system.status')).revision, authored.revision);
		await config.update('enabled', false, vscode.ConfigurationTarget.WorkspaceFolder);
		assert.equal((await api.provider.provideMcpServerDefinitions(token.token)).length, 0, 'Disabling the connection removes its definition.');
		console.log('PASS: VS Code activation, stable MCP provider, discovery isolation, native startup/reconnect, 60 bundled tools, scene authoring and read-only enforcement.');
	} finally {
		await readonlyClient.close();
		await client.close();
		if (pipe) {
			try {
				const current = await nativeRequest(pipe, 'system.status');
				await nativeRequest(pipe, 'editor.close', { expected_revision: current.revision, discard_changes: true });
			} catch {}
		}
		token.dispose();
	}
}
