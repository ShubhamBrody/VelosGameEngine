param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('auto', 'intel', 'nvidia', 'amd', 'warp')][string]$Adapter = 'auto',
    [string]$BuildDirectory = 'out\build-control',
    [switch]$Compact,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $root $BuildDirectory }
$editor = Join-Path $BuildDirectory "$Configuration\VelosEditor.exe"
if (-not (Test-Path -LiteralPath $editor)) { throw 'Build the editor and runtime before running the MCP smoke tests.' }
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw 'Install Node.js 22 or later for the MCP adapter.' }
$adapterRoot = Join-Path $PSScriptRoot 'mcp'
if (-not $SkipInstall) {
    & npm ci --prefix $adapterRoot --ignore-scripts --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'MCP dependency installation failed.' }
}
& npm test --prefix $adapterRoot
if ($LASTEXITCODE -ne 0) { throw 'MCP schema tests failed.' }
& node "$adapterRoot\tests\security.test.mjs" $editor
if ($LASTEXITCODE -ne 0) { throw 'MCP isolation tests failed.' }
& node "$adapterRoot\tests\editor.test.mjs" $editor $Adapter 1600 1000
if ($LASTEXITCODE -ne 0) { throw 'MCP editor/game workflow failed.' }
if ($Compact) {
    & node "$adapterRoot\tests\editor.test.mjs" $editor $Adapter 1024 720
    if ($LASTEXITCODE -ne 0) { throw 'Compact MCP editor/game workflow failed.' }
}
Write-Output "PASS: $Configuration MCP control, graph, import, simulation and export checks on $Adapter."