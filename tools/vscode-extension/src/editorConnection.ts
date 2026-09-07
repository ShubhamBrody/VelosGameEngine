import { spawn, ChildProcess } from 'node:child_process';
import { stat } from 'node:fs/promises';
import { request } from '../../mcp/pipe.mjs';
import { Connection, editorArguments } from './connection';

export interface EditorStatus {
	engine: 'Velos';
	api_version: number;
	scene: string;
	entities: number;
	revision: string;
	read_only: boolean;
	playing: boolean;
	paused: boolean;
	dirty: boolean;
	control: { pipe: string };
}

export class EditorConnections {
	private readonly starting = new Map<string, Promise<EditorStatus>>();
	private readonly children = new Set<ChildProcess>();
	private disposed = false;

	constructor(private readonly log: (message: string) => void) {}

	async status(connection: Connection, timeout = 2500): Promise<EditorStatus> {
		const result = await request(connection.pipe, 'system.status', {}, { timeout });
		if (!result || typeof result !== 'object' || !('engine' in result) || result.engine !== 'Velos'
			|| !('api_version' in result) || result.api_version !== 1 || !('control' in result)
			|| !result.control || typeof result.control !== 'object' || !('pipe' in result.control) || result.control.pipe !== connection.pipe) {
			throw new Error('The selected pipe did not identify itself as a compatible Velos editor.');
		}
		return result as EditorStatus;
	}

	async ensure(connection: Connection, approveLaunch: () => Promise<boolean>): Promise<EditorStatus> {
		if (this.disposed) { throw new Error('The extension is shutting down.'); }
		const pending = this.starting.get(connection.pipe);
		if (pending) { return pending; }
		const operation = this.connect(connection, approveLaunch);
		this.starting.set(connection.pipe, operation);
		try { return await operation; }
		finally { this.starting.delete(connection.pipe); }
	}

	private async connect(connection: Connection, approveLaunch: () => Promise<boolean>): Promise<EditorStatus> {
		try { return await this.status(connection); }
		catch (error) {
			if (!(error instanceof Error) || !('code' in error) || error.code !== 'ENOENT') { throw error; }
		}
		if (connection.mode === 'attach') { throw new Error(`No editor is listening on ${connection.pipe}. Start it with --control-pipe=${connection.pipe} and the intended --automation-root first.`); }
		const executable = await stat(connection.editor).catch(() => undefined);
		if (!executable?.isFile() || !connection.editor.toLowerCase().endsWith('.exe')) {
			throw new Error(`VelosEditor.exe was not found at ${connection.editor}. Run Velos: Configure Connection and select the engine directory or editor executable.`);
		}
		if (!(await approveLaunch())) { throw new Error('Editor launch cancelled. No scene changes were made.'); }
		if (this.disposed) { throw new Error('The extension is shutting down.'); }
		await new Promise<void>((resolve, reject) => {
			const child = spawn(connection.editor, editorArguments(connection), { cwd: connection.root, windowsHide: false, stdio: ['ignore', 'pipe', 'pipe'] });
			this.children.add(child);
			let pending = true;
			let output = '';
			const fail = (error: Error) => {
				if (!pending) { return; }
				pending = false;
				clearTimeout(timer);
				reject(error);
			};
			const timer = setTimeout(() => fail(new Error(`Velos did not become ready within 60 seconds. Its process was left open; inspect the Velos MCP output before retrying.`)), 60000);
			child.stdout?.on('data', (chunk: Buffer) => {
				output = (output + chunk.toString('utf8')).slice(-16384);
				if (pending && output.split(/\r?\n/).includes(`VELOS_CONTROL_READY ${connection.pipe}`)) {
					pending = false;
					clearTimeout(timer);
					this.log(`Editor ready: ${connection.pipe} (PID ${child.pid}).`);
					resolve();
				}
			});
			child.stderr?.on('data', (chunk: Buffer) => this.log(chunk.toString('utf8').slice(0, 4096)));
			child.on('error', fail);
			child.on('exit', (code) => {
				this.children.delete(child);
				this.log(`Editor ${connection.pipe} exited with code ${code}.`);
				fail(new Error(`Velos exited before its control connection was ready (${code}). Check the Velos MCP output.`));
			});
		});
		return this.status(connection, 10000);
	}

	dispose(): void {
		this.disposed = true;
		for (const child of this.children) {
			child.stdout?.destroy();
			child.stderr?.destroy();
			child.removeAllListeners();
			child.unref();
		}
		this.children.clear();
	}
}