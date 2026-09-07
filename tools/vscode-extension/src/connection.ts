import { createHash } from 'node:crypto';
import * as path from 'node:path';

export interface ConnectionOptions {
	engineDirectory?: string;
	editorExecutable?: string;
	pipeName?: string;
	mode?: string;
	readOnly?: boolean;
	adapter?: string;
	nodeExecutable?: string;
}

export interface Connection {
	id: string;
	label: string;
	root: string;
	editor: string;
	pipe: string;
	mode: 'auto' | 'attach';
	readOnly: boolean;
	adapter: string;
	node: string;
	server: string;
}

export function connectionFor(root: string, name: string, extensionPath: string, options: ConnectionOptions = {}): Connection {
	if (!path.isAbsolute(root)) { throw new Error('The game workspace must be an absolute local directory.'); }
	root = path.normalize(root);
	const id = createHash('sha256').update(root.toLowerCase()).digest('hex').slice(0, 12);
	const pipe = options.pipeName?.trim() || `velos-code-${id}`;
	if (!/^velos-[a-zA-Z0-9_-]{1,90}$/.test(pipe)) { throw new Error('Pipe names must start with velos- and contain only ASCII letters, digits, dash or underscore.'); }
	const mode = options.mode ?? 'auto';
	if (mode !== 'auto' && mode !== 'attach') { throw new Error('Connection mode must be auto or attach.'); }
	const adapter = options.adapter ?? 'auto';
	if (!['auto', 'nvidia', 'intel', 'amd', 'warp'].includes(adapter)) { throw new Error('Unsupported GPU adapter preference.'); }
	const engine = options.engineDirectory?.trim() ? path.resolve(root, options.engineDirectory) : root;
	const editor = options.editorExecutable?.trim() ? path.resolve(engine, options.editorExecutable) : path.join(engine, 'out', 'build-control', 'Release', 'VelosEditor.exe');
	return {
		id, label: `Velos - ${name} [${id.slice(0, 6)}]`, root, editor, pipe, mode, readOnly: options.readOnly ?? false, adapter,
		node: options.nodeExecutable?.trim() || process.execPath,
		server: path.join(extensionPath, 'dist', 'server.mjs')
	};
}

export function adapterArguments(connection: Connection): string[] {
	const args = [connection.server, `--pipe=${connection.pipe}`, `--root=${connection.root}`];
	if (connection.readOnly) { args.push('--read-only'); }
	return args;
}

export function definitionVersion(connection: Connection): string {
	return `0.1.0:${createHash('sha256').update(JSON.stringify(connection)).digest('hex').slice(0, 20)}`;
}

export function editorArguments(connection: Connection): string[] {
	const args = [`--control-pipe=${connection.pipe}`, `--automation-root=${connection.root}`, `--adapter=${connection.adapter}`, '--isolated'];
	if (connection.readOnly) { args.push('--automation-read-only'); }
	return args;
}