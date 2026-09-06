param(
    [string]$Scene = '',
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Scene) { $Scene = Join-Path $root 'samples\workshop\scene.velos' }
if (-not $Output) { $Output = Join-Path $root ('out\packages\workshop-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$runtime = Join-Path $root 'out\build\Release\VelosRuntime.exe'
if (-not (Test-Path -LiteralPath $runtime)) { throw 'Build Release first with tools/build.ps1 -Configuration Release -Test.' }
& $runtime "--package=$Scene" "--output=$Output"
if ($LASTEXITCODE -ne 0) { throw 'Scene export failed.' }
Write-Output "Run the exported game: $Output\VelosRuntime.exe"