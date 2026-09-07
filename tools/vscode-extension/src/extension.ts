import * as vscode from 'vscode';
import { stat } from 'node:fs/promises';
import { adapterArguments, Connection, connectionFor, definitionVersion } from './connection';
import { EditorConnections } from './editorConnection';

const providerId = 'velos.mcpServers';

export class VelosProvider implements vscode.McpServerDefinitionProvider<vscode.McpStdioServerDefinition>, vscode.Disposable {
	private readonly changed = new vscode.EventEmitter<void>();
	readonly onDidChangeMcpServerDefinitions = this.changed.event;

	constructor(private readonly extensionPath: string, private readonly editors: EditorConnections, private readonly output: vscode.OutputChannel) {}

	refresh(): void { this.changed.fire(); }
	dispose(): void { this.changed.dispose(); }

	connection(folder: vscode.WorkspaceFolder): Connection {
		const config = vscode.workspace.getConfiguration('velosMcp', folder.uri);
		return connectionFor(folder.uri.fsPath, folder.name, this.extensionPath, {
			engineDirectory: config.get<string>('engineDirectory'), editorExecutable: config.get<string>('editorExecutable'),
			pipeName: config.get<string>('pipeName'), mode: config.get<string>('mode'), readOnly: config.get<boolean>('readOnly'),
			adapter: config.get<string>('adapter'), nodeExecutable: config.get<string>('nodeExecutable')
		});
	}

	async provideMcpServerDefinitions(token: vscode.CancellationToken): Promise<vscode.McpStdioServerDefinition[]> {
		if (process.platform !== 'win32' || !vscode.workspace.isTrusted || vscode.env.remoteName) { return []; }
		const result: vscode.McpStdioServerDefinition[] = [];
		for (const folder of vscode.workspace.workspaceFolders ?? []) {
			if (token.isCancellationRequested) { return []; }
			if (folder.uri.scheme !== 'file' || !vscode.workspace.getConfiguration('velosMcp', folder.uri).get('enabled', true)) { continue; }
			try {
				const connection = this.connection(folder);
				if (connection.mode === 'auto' && !(await stat(connection.editor).catch(() => undefined))?.isFile()) { continue; }
				const definition = new vscode.McpStdioServerDefinition(connection.label, connection.node, adapterArguments(connection),
					{ ELECTRON_RUN_AS_NODE: '1' }, definitionVersion(connection));
				definition.cwd = folder.uri;
				result.push(definition);
			} catch (error) { this.output.appendLine(String(error)); }
		}
		return result;
	}

	async resolveMcpServerDefinition(definition: vscode.McpStdioServerDefinition, token: vscode.CancellationToken): Promise<vscode.McpStdioServerDefinition | undefined> {
		if (!vscode.workspace.isTrusted || vscode.env.remoteName || token.isCancellationRequested) { return undefined; }
		const folder = (vscode.workspace.workspaceFolders ?? []).find(candidate => candidate.uri.scheme === 'file' && this.connection(candidate).label === definition.label);
		if (!folder || !vscode.workspace.getConfiguration('velosMcp', folder.uri).get('enabled', true)) {
			throw new Error('The Velos connection or permissions changed. Refresh and restart its MCP server before using it.');
		}
		const connection = this.connection(folder);
		if (definition.version !== definitionVersion(connection) || definition.command !== connection.node
			|| JSON.stringify(definition.args) !== JSON.stringify(adapterArguments(connection))) {
			throw new Error('The Velos connection or permissions changed. Refresh and restart its MCP server before using it.');
		}
		await this.editors.ensure(connection, async () => {
			if (token.isCancellationRequested) { return false; }
			const answer = await vscode.window.showInformationMessage(`Start Velos for ${connection.root}?`, {
				modal: true, detail: `MCP access is ${connection.readOnly ? 'read-only' : 'read/write'} within this game workspace. The editor will remain open when MCP disconnects.`
			}, 'Start Editor');
			return answer === 'Start Editor' && !token.isCancellationRequested;
		});
		return token.isCancellationRequested ? undefined : definition;
	}
}

export function activate(context: vscode.ExtensionContext) {
	const output = vscode.window.createOutputChannel('Velos MCP');
	const editors = new EditorConnections(message => output.appendLine(message));
	const provider = new VelosProvider(context.extensionPath, editors, output);
	const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 20);
	status.text = '$(plug) Velos';
	status.tooltip = 'Velos MCP connection';
	status.command = 'velosMcp.status';
	if (process.platform === 'win32' && !vscode.env.remoteName) { status.show(); }
	context.subscriptions.push(output, editors, provider, status, vscode.lm.registerMcpServerDefinitionProvider(providerId, provider));
	const chooseFolder = async (uri?: vscode.Uri): Promise<vscode.WorkspaceFolder> => {
		if (!vscode.workspace.isTrusted) { throw new Error('Trust the workspace before connecting to a native editor.'); }
		if (process.platform !== 'win32' || vscode.env.remoteName) { throw new Error('Velos currently requires a local Windows workspace and native D3D12 editor.'); }
		const folders = (vscode.workspace.workspaceFolders ?? []).filter(folder => folder.uri.scheme === 'file');
		const selected = uri ? vscode.workspace.getWorkspaceFolder(uri) : folders.length === 1 ? folders[0]
			: await vscode.window.showQuickPick(folders.map(folder => ({ label: folder.name, description: folder.uri.fsPath, folder })), { title: 'Velos game workspace' }).then(item => item?.folder);
		if (!selected) { throw new Error('Open or choose a local game workspace first.'); }
		return selected;
	};
	const register = (name: string, action: (uri?: vscode.Uri) => Promise<unknown>) => {
		context.subscriptions.push(vscode.commands.registerCommand(name, async (uri?: vscode.Uri) => {
			try { return await action(uri); }
			catch (error) { output.appendLine(String(error)); void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error)); return undefined; }
		}));
	};
	register('velosMcp.startEditor', async uri => {
		const connection = provider.connection(await chooseFolder(uri));
		const result = await vscode.window.withProgress({ location: vscode.ProgressLocation.Notification, title: 'Connecting to Velos' },
			() => editors.ensure(connection, async () => true));
		status.text = `$(plug) Velos: ${result.read_only || connection.readOnly ? 'Read-only' : 'Connected'}`;
		output.appendLine(JSON.stringify(result, null, 2));
		provider.refresh();
		return result;
	});
	register('velosMcp.status', async uri => {
		const connection = provider.connection(await chooseFolder(uri));
		const result = await editors.status(connection);
		status.text = `$(plug) Velos: ${result.read_only || connection.readOnly ? 'Read-only' : 'Connected'}`;
		status.tooltip = `${result.scene} | ${connection.pipe} | ${result.entities} entities | revision ${result.revision}`;
		output.appendLine(JSON.stringify(result, null, 2));
		output.show(true);
		return result;
	});
	register('velosMcp.configure', async uri => {
		const folder = await chooseFolder(uri);
		const selected = await vscode.window.showOpenDialog({ title: 'Select VelosEditor.exe', canSelectFiles: true, canSelectFolders: false, canSelectMany: false, filters: { 'Velos editor': ['exe'] } });
		if (!selected?.[0]) { return; }
		const config = vscode.workspace.getConfiguration('velosMcp', folder.uri);
		await config.update('editorExecutable', selected[0].fsPath, vscode.ConfigurationTarget.WorkspaceFolder);
		await config.update('mode', 'auto', vscode.ConfigurationTarget.WorkspaceFolder);
		provider.refresh();
	});
	register('velosMcp.attach', async uri => {
		const folder = await chooseFolder(uri);
		const pipe = await vscode.window.showInputBox({ title: 'Attach to a running Velos editor', value: provider.connection(folder).pipe,
			prompt: 'Use the existing editor control pipe. Its automation root must match this game workspace.',
			validateInput: value => /^velos-[a-zA-Z0-9_-]{1,90}$/.test(value) ? undefined : 'Enter a valid velos- pipe name.' });
		if (!pipe) { return; }
		const config = vscode.workspace.getConfiguration('velosMcp', folder.uri);
		const candidate = { ...provider.connection(folder), pipe, mode: 'attach' as const };
		const result = await editors.status(candidate);
		await config.update('pipeName', pipe, vscode.ConfigurationTarget.WorkspaceFolder);
		await config.update('mode', 'attach', vscode.ConfigurationTarget.WorkspaceFolder);
		provider.refresh();
		status.text = '$(plug) Velos: Attached';
		output.appendLine(`Attached to ${pipe}; editor permissions are ${result.read_only ? 'read-only' : 'read/write'}.`);
		return result;
	});
	register('velosMcp.readOnly', async uri => {
		const folder = await chooseFolder(uri);
		const config = vscode.workspace.getConfiguration('velosMcp', folder.uri);
		const readOnly = !config.get('readOnly', false);
		await config.update('readOnly', readOnly, vscode.ConfigurationTarget.WorkspaceFolder);
		provider.refresh();
		void vscode.window.showInformationMessage(`Velos MCP ${readOnly ? 'read-only' : 'read/write'} selected. Restart its MCP server to apply the adapter policy. A native read-only editor cannot be elevated by this setting.`);
	});
	register('velosMcp.manageServers', async () => {
		const available = await vscode.commands.getCommands(true);
		if (!available.includes('workbench.mcp.listServer')) { throw new Error('Use the Command Palette: MCP: List Servers. The MCP management command is unavailable in this VS Code build.'); }
		await vscode.commands.executeCommand('workbench.mcp.listServer');
	});
	context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event => { if (event.affectsConfiguration('velosMcp')) { provider.refresh(); } }),
		vscode.workspace.onDidChangeWorkspaceFolders(() => provider.refresh()), vscode.workspace.onDidGrantWorkspaceTrust(() => provider.refresh()),
		vscode.commands.registerCommand('velosMcp.showOutput', () => output.show()));
	return { provider, version: context.extension.packageJSON.version };
}
