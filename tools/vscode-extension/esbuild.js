const esbuild = require("esbuild");
const fs = require('node:fs/promises');
const path = require('node:path');

const production = process.argv.includes('--production');
const watch = process.argv.includes('--watch');

async function writeNotices(metadata) {
	const packages = new Map();
	for (const filename of Object.keys(metadata.inputs)) {
		const normalized = filename.replaceAll('\\', '/');
		const marker = normalized.lastIndexOf('node_modules/');
		if (marker < 0) { continue; }
		const parts = normalized.slice(marker + 13).split('/');
		const name = parts[0].startsWith('@') ? `${parts[0]}/${parts[1]}` : parts[0];
		const directory = path.resolve(normalized.slice(0, marker + 13) + name);
		const manifest = JSON.parse(await fs.readFile(path.join(directory, 'package.json'), 'utf8'));
		const key = `${manifest.name}@${manifest.version}`;
		if (packages.has(key)) { continue; }
		const files = (await fs.readdir(directory)).filter(file => /^(license|copying|notice)(\.|$)/i.test(file));
		const licenses = [];
		for (const file of files) {
			if ((await fs.stat(path.join(directory, file))).isFile()) { licenses.push(await fs.readFile(path.join(directory, file), 'utf8')); }
		}
		if (!licenses.length) { throw new Error(`Missing license text for bundled dependency ${key}.`); }
		packages.set(key, `${key}\nDeclared license: ${JSON.stringify(manifest.license ?? 'See package notice')}\n\n${licenses.join('\n\n')}`);
	}
	const text = 'Third-party software bundled with Velos MCP Tools\n\n' + [...packages.entries()].sort(([first], [second]) => first.localeCompare(second)).map(([, notice]) => notice).join('\n\n============================================================\n\n');
	await fs.writeFile('dist/THIRD_PARTY_NOTICES.txt', text, 'utf8');
}

const esbuildProblemMatcherPlugin = {
	name: 'esbuild-problem-matcher',

	setup(build) {
		build.onStart(() => {
			console.log('[watch] build started');
		});
		build.onEnd((result) => {
			result.errors.forEach(({ text, location }) => {
				console.error(`[ERROR] ${text}`);
				console.error(`    ${location.file}:${location.line}:${location.column}:`);
			});
			console.log('[watch] build finished');
		});
	},
};

async function main() {
	const ctx = await esbuild.context({
		entryPoints: [
			'src/extension.ts'
		],
		bundle: true,
		format: 'cjs',
		minify: production,
		sourcemap: !production,
		sourcesContent: false,
		platform: 'node',
		target: 'node22',
		outfile: 'dist/extension.js',
		external: ['vscode'],
		logLevel: 'silent',
		plugins: [
			esbuildProblemMatcherPlugin,
		],
	});
	const adapter = await esbuild.context({
		entryPoints: ['../mcp/server.mjs'], bundle: true, platform: 'node', format: 'esm', target: 'node22',
		outfile: 'dist/server.mjs', sourcemap: !production, minify: production,
		metafile: true, nodePaths: [path.resolve('node_modules')],
		banner: { js: "import { createRequire as velosCreateRequire } from 'node:module'; const require = velosCreateRequire(import.meta.url);" }
	});
	if (watch) {
		await ctx.watch();
		await adapter.watch();
	} else {
		await ctx.rebuild();
		const result = await adapter.rebuild();
		await writeNotices(result.metafile);
		await ctx.dispose();
		await adapter.dispose();
	}
}

main().catch(e => {
	console.error(e);
	process.exit(1);
});
