const { runTests } = require('@vscode/test-electron');
const { mkdir, mkdtemp, access } = require('node:fs/promises');
const path = require('node:path');

async function main() {
	const extension = path.resolve(__dirname, '..');
	const developmentPath = process.argv[2] ? path.resolve(process.argv[2]) : extension;
	const repo = path.resolve(extension, '../..');
	const code = process.env.VELOS_VSCODE_EXECUTABLE || path.join(process.env.LOCALAPPDATA, 'Programs', 'Microsoft VS Code', 'Code.exe');
	await access(code);
	await access(path.join(repo, 'out', 'build-control', 'Release', 'VelosEditor.exe'));
	const evidence = path.join(repo, 'out', 'validation-vscode');
	await mkdir(evidence, { recursive: true });
	const fixture = await mkdtemp(path.join(evidence, 'host-'));
	const workspace = path.join(fixture, 'Game Workspace');
	await mkdir(workspace);
	await runTests({
		vscodeExecutablePath: code,
		extensionDevelopmentPath: developmentPath,
		extensionTestsPath: path.join(extension, 'out', 'test', 'extension.test.js'),
		extensionTestsEnv: { VELOS_TEST_REPO: repo },
		launchArgs: [workspace, '--user-data-dir', path.join(fixture, 'profile'), '--extensions-dir', path.join(fixture, 'extensions'),
			'--disable-extensions', '--disable-workspace-trust', '--skip-welcome', '--skip-release-notes', '--disable-gpu']
	});
	console.log(`VS Code host-test evidence: ${fixture}`);
}

main().catch(error => { console.error(error); process.exitCode = 1; });