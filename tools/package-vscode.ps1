param(
    [switch]$Test,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$extension = Join-Path $PSScriptRoot 'vscode-extension'
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw 'Node.js 22 or later is required to build the extension.' }
if (-not $SkipInstall) {
    & npm ci --prefix $extension --ignore-scripts --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'VS Code plugin dependency installation failed.' }
}
& npm --prefix $extension run test:unit
if ($LASTEXITCODE -ne 0) { throw 'Connection tests failed.' }
$manifest = Get-Content -LiteralPath (Join-Path $extension 'package.json') -Raw | ConvertFrom-Json
$directory = Join-Path $root 'out\vsix'
[void][System.IO.Directory]::CreateDirectory($directory)
$package = Join-Path $directory "$($manifest.name)-$($manifest.version)-win32-x64.vsix"
Push-Location $extension
try {
    & '.\node_modules\.bin\vsce.cmd' package --target win32-x64 --no-dependencies --skip-license --out $package --githubBranch main
    if ($LASTEXITCODE -ne 0) { throw 'VSIX packaging failed.' }
} finally { Pop-Location }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($package)
try {
    $names = @($archive.Entries | ForEach-Object { $_.FullName })
    foreach ($required in @('extension/package.json','extension/dist/extension.js','extension/dist/server.mjs','extension/dist/THIRD_PARTY_NOTICES.txt')) {
        if ($names -notcontains $required) { throw "VSIX is missing $required" }
    }
    if (@($names | Where-Object { $_ -match '(^|/)(node_modules|src|scripts|out|\.vscode)/|\.env$|autosave|\.map$' }).Count -ne 0) {
        throw 'VSIX contains unexpected source, dependency, user configuration or generated test files.'
    }
    $reader = New-Object System.IO.StreamReader($archive.GetEntry('extension/package.json').Open())
    try { $packed = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if ($packed.publisher -ne 'ShubhamBrody' -or $packed.name -ne 'velos-mcp-tools' -or $packed.capabilities.untrustedWorkspaces.supported -ne $false) {
        throw 'Packaged plugin identity or workspace-trust policy differs from the expected manifest.'
    }
    Write-Output "PASS: VSIX contains only the plugin, bundled adapter, documentation and dependency notices ($($names.Count) entries)."
} finally { $archive.Dispose() }
if ($Test) {
    $fixture = Join-Path $root ('out\validation-vscode\vsix-' + [guid]::NewGuid().ToString('N'))
    [System.IO.Compression.ZipFile]::ExtractToDirectory($package,$fixture)
    & node (Join-Path $extension 'scripts\test-host.cjs') (Join-Path $fixture 'extension')
    if ($LASTEXITCODE -ne 0) { throw 'Packaged extension failed its real VS Code host integration test.' }
}
Write-Output "Velos MCP Tools VSIX: $package"